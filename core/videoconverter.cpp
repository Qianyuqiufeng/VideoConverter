// =============================================================================
// 模块：视频格式转换的实现（core/videoconverter.cpp）
// -----------------------------------------------------------------------------
// 命令形态：
//   ffmpeg -hide_banner -loglevel error -y -i <输入>
//          -map 0:v:0 -map 0:a? -progress pipe:1 -nostats
//          <编码参数> <输出>
//
// 编码参数：
//   AVI            -> mpeg4（-q:v 3）+ libmp3lame 192k
//   MP4/MKV/MOV    -> libx264（preset medium, crf 23）+ aac 192k
//   MP4/MOV 额外加 -movflags +faststart（索引前置，便于边下边播）。
//
// 进度：ffmpeg 以 key=value 形式逐行输出到 stdout，其中 out_time_us /
//       out_time_ms 的单位都是微秒（后者是历史命名），用总时长即可换算百分比。
// =============================================================================

#include "videoconverter.h"

#include <QFile>
#include <QFileInfo>
#include <QProcess>

VideoConverter::VideoConverter(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    // stdout 承载 -progress 的机器可读输出：每读到一块就增量解析。
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_stdout += m_process->readAllStandardOutput();
        parseProgressLines();
    });
    // stderr 只用于失败时展示原因，因此只保留末尾 64KB，
    // 防止超长编码日志把内存吃满。
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        m_stderr += m_process->readAllStandardError();
        if (m_stderr.size() > 65536)
            m_stderr = m_stderr.right(65536);
    });
    connect(m_process, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        handleFinished();
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_running) {
            emitFailure(QStringLiteral("无法启动 FFmpeg，请检查工具路径。"),
                        m_process->errorString());
        }
    });
}

// 构建 argv 并启动 ffmpeg。
// 注意：这里始终使用参数列表而不是一条命令字符串，Qt 会直接传给子进程，
// 因此 "C:\我的 视频\a b.mp4" 这类路径无需额外转义。
void VideoConverter::start(const ConversionRequest &request, const QString &ffmpegPath,
                           double durationSeconds)
{
    if (m_running)
        return;

    m_request = request;
    m_durationSeconds = durationSeconds;
    m_stdout.clear();
    m_stderr.clear();
    m_cancelled = false;
    m_running = true;

    QStringList arguments;
    arguments << QStringLiteral("-hide_banner")
              << QStringLiteral("-loglevel") << QStringLiteral("error")
              << QStringLiteral("-y")
              << QStringLiteral("-i") << request.inputPath
              << QStringLiteral("-map") << QStringLiteral("0:v:0")
              << QStringLiteral("-map") << QStringLiteral("0:a?")
              << QStringLiteral("-progress") << QStringLiteral("pipe:1")
              << QStringLiteral("-nostats");

    // AVI：使用兼容性最好的 MPEG-4 Part 2 视频 + MP3 音频。
    // -q:v 3 为固定质量档位（数值越小画质越高、文件越大）。
    if (request.format == TargetFormat::Avi) {
        arguments << QStringLiteral("-c:v") << QStringLiteral("mpeg4")
                  << QStringLiteral("-q:v") << QStringLiteral("3")
                  << QStringLiteral("-c:a") << QStringLiteral("libmp3lame")
                  << QStringLiteral("-b:a") << QStringLiteral("192k");
    } else {
        // MP4 / MKV / MOV：H.264 + AAC。
        // -crf 23 为常用的“视觉无损”质量档；preset medium 在速度与压缩率之间折中。
        arguments << QStringLiteral("-c:v") << QStringLiteral("libx264")
                  << QStringLiteral("-preset") << QStringLiteral("medium")
                  << QStringLiteral("-crf") << QStringLiteral("23")
                  << QStringLiteral("-c:a") << QStringLiteral("aac")
                  << QStringLiteral("-b:a") << QStringLiteral("192k");
        // MP4/MOV 支持 faststart（把索引移到文件头）；MKV 不是 MP4 容器，不能加。
        if (request.format != TargetFormat::Mkv)
            arguments << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    }

    arguments << request.outputPath;
    m_process->start(ffmpegPath, arguments);
}

// 请求取消。kill() 后 ffmpeg 立即退出，handleFinished 会删除半成品文件。
// 若进程已经自行结束，仅复位取消标志，避免误删正常输出。
void VideoConverter::cancel()
{
    if (!m_running)
        return;

    if (m_process->state() == QProcess::NotRunning) {
        m_cancelled = false;
        return;
    }

    m_cancelled = true;
    m_process->kill();
}

bool VideoConverter::isRunning() const
{
    return m_running;
}

// 增量解析 -progress 输出。
// 只处理完整行（以 '\n' 结尾），未完整的尾部留在 m_stdout 中等待下一块数据。
//   out_time_us / out_time_ms -> 已编码时长（单位：微秒），换算成百分比；
//   progress=end             -> 收尾，直接上报 100%；
//   speed=...                -> 转成附加状态文本。
// 总时长未知（<= 0）时上报 -1，界面据此切换为不确定进度条。
void VideoConverter::parseProgressLines()
{
    int newline = -1;
    while ((newline = m_stdout.indexOf('\n')) >= 0) {
        const QByteArray line = m_stdout.left(newline).trimmed();
        m_stdout.remove(0, newline + 1);

        if (line.startsWith("out_time_us=") || line.startsWith("out_time_ms=")) {
            const qint64 microseconds = line.mid(line.indexOf('=') + 1).toLongLong();
            if (m_durationSeconds > 0.0 && microseconds >= 0) {
                const double total = m_durationSeconds * 1000000.0;
                int percent = static_cast<int>((microseconds / total) * 100.0);
                percent = qBound(0, percent, 100);
                emit progressChanged(percent);
            } else {
                emit progressChanged(-1);
            }
        } else if (line == "progress=end") {
            emit progressChanged(100);
        } else if (line.startsWith("speed=")) {
            const QString speed = QString::fromUtf8(line.mid(6));
            if (!speed.isEmpty())
                emit statusChanged(QStringLiteral("速度 %1").arg(speed));
        }
    }
}

// ffmpeg 结束后的收尾逻辑，按优先级判断：
//   1. 用户取消 -> 删除未完成的输出文件，发 cancelled；
//   2. 非正常退出 / 非零退出码 -> 发 finished(false)，附 stderr；
//   3. 输出文件不存在或为空 -> 视为失败（避免误报成功）；
//   4. 其余情况 -> 上报 100% 并发 finished(true)。
void VideoConverter::handleFinished()
{
    if (!m_running)
        return;

    m_running = false;
    m_stdout += m_process->readAllStandardOutput();
    m_stderr += m_process->readAllStandardError();
    parseProgressLines();

    if (m_cancelled) {
        QFile::remove(m_request.outputPath);
        emit cancelled();
        return;
    }

    if (m_process->exitStatus() != QProcess::NormalExit || m_process->exitCode() != 0) {
        const QString detail = QString::fromUtf8(m_stderr).trimmed();
        emit finished(false, QStringLiteral("转换失败，请查看详细信息。"), detail);
        return;
    }

    const QFileInfo output(m_request.outputPath);
    if (!output.exists() || output.size() <= 0) {
        emit finished(false, QStringLiteral("转换结束，但输出文件无效。"),
                      m_request.outputPath);
        return;
    }

    emit progressChanged(100);
    emit finished(true, QStringLiteral("转换完成。"), QString());
}

// 转换尚未真正开始（如 ffmpeg 无法启动）时的失败出口。
// 幂等：只有在 m_running 为 true 时才发信号，避免重复失败通知。
void VideoConverter::emitFailure(const QString &message, const QString &detail)
{
    if (!m_running)
        return;

    m_running = false;
    emit finished(false, message, detail);
}
