// =============================================================================
// 模块：视频元信息探测的实现（core/mediaprobe.cpp）
// -----------------------------------------------------------------------------
// 流程：probe() 启动 ffprobe（JSON 输出）
//       -> handleFinished() 检查退出码
//       -> parseOutput() 解析 JSON 并填 VideoInfo。
//
// 解析细节集中在本文件的匿名命名空间中：容器名、编码名、帧率与数值字段的
// 兼容处理都只在这里做一次，避免界面层重复判断。
// =============================================================================

#include "mediaprobe.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QProcess>
#include <QTimer>

namespace {

// 单次 ffprobe 调用的超时上限（毫秒）。大文件或慢速磁盘仍应在此时间内返回元数据。
constexpr int kProbeTimeoutMs = 15000;

// 解析 ffprobe 的帧率字段。该字段是分数形式（如 "30000/1001"），
// 需要分子 / 分母求值；格式非法或分母非正时返回 -1。
double parseFrameRate(const QString &value)
{
    const QStringList parts = value.split(QLatin1Char('/'));
    if (parts.size() != 2)
        return -1.0;

    bool numOk = false;
    bool denOk = false;
    const double num = parts.at(0).toDouble(&numOk);
    const double den = parts.at(1).toDouble(&denOk);
    if (!numOk || !denOk || den <= 0.0)
        return -1.0;
    return num / den;
}

// 兼容读取 JSON 数值：不同 ffprobe 版本对 duration/size 等字段
// 可能输出数字，也可能输出字符串（甚至 "N/A"）。
double parseNumber(const QJsonValue &value, bool *ok)
{
    if (value.isString()) {
        return value.toString().toDouble(ok);
    }
    if (value.isDouble()) {
        *ok = true;
        return value.toDouble();
    }
    *ok = false;
    return -1.0;
}

// 推断容器格式的展示名。
// 优先使用文件扩展名（用户最直观）；扩展名不在白名单时，再用 ffprobe 的
// format_name（可能是逗号分隔的多个别名）做映射。
QString displayContainer(const QString &filePath, const QString &formatName)
{
    const QString suffix = QFileInfo(filePath).suffix().toUpper();
    if (suffix == QLatin1String("MP4") || suffix == QLatin1String("MOV")
        || suffix == QLatin1String("M4V") || suffix == QLatin1String("MKV")
        || suffix == QLatin1String("AVI") || suffix == QLatin1String("WMV")
        || suffix == QLatin1String("FLV") || suffix == QLatin1String("WEBM")) {
        return suffix;
    }

    const QStringList names = formatName.split(QLatin1Char(','));
    if (names.contains(QLatin1String("mp4")) || names.contains(QLatin1String("mov")))
        return QStringLiteral("MP4");
    if (names.contains(QLatin1String("matroska")))
        return QStringLiteral("MKV");
    if (names.contains(QLatin1String("avi")))
        return QStringLiteral("AVI");
    if (names.contains(QLatin1String("asf")))
        return QStringLiteral("WMV");
    if (names.contains(QLatin1String("flv")))
        return QStringLiteral("FLV");
    if (names.contains(QLatin1String("webm")))
        return QStringLiteral("WebM");
    return names.isEmpty() ? QString() : names.first().toUpper();
}

// 把 ffprobe 的编码器内部名（如 h264）转成用户熟悉的展示名（H.264）。
// 未收录的编码直接转大写，保证界面永远有内容可显示。
QString displayCodec(const QString &codecName)
{
    const QString codec = codecName.toLower();
    if (codec == QLatin1String("h264"))
        return QStringLiteral("H.264");
    if (codec == QLatin1String("hevc"))
        return QStringLiteral("H.265");
    if (codec == QLatin1String("mpeg4"))
        return QStringLiteral("MPEG-4");
    if (codec == QLatin1String("mpeg2video"))
        return QStringLiteral("MPEG-2");
    if (codec == QLatin1String("vp9"))
        return QStringLiteral("VP9");
    if (codec == QLatin1String("av1"))
        return QStringLiteral("AV1");
    if (codec == QLatin1String("prores"))
        return QStringLiteral("ProRes");
    return codec.isEmpty() ? QString() : codec.toUpper();
}

} // namespace

// 构造：只创建超时定时器；QProcess 在每次 probe() 时新建，
// 这样一次探测的异常状态不会污染下一次。
MediaProbe::MediaProbe(QObject *parent)
    : QObject(parent)
    , m_timeout(new QTimer(this))
{
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(kProbeTimeoutMs);

    // 超时：进程仍在运行则按失败处理（fail 内部会结束进程）。
    connect(m_timeout, &QTimer::timeout, this, [this] {
        if (m_process && m_process->state() != QProcess::NotRunning) {
            fail(QStringLiteral("读取视频信息超时，请确认文件可以正常播放。"), QString());
        }
    });
}

// 开始探测一个文件。
// 关键点：每次请求自增 requestId；回调闭包捕获该 id，只有 id 仍等于当前值
// 时才采纳结果。这样用户快速连续导入不同文件时，旧进程的迟到信号不会覆盖新结果。
void MediaProbe::probe(const QString &ffprobePath, const QString &filePath)
{
    ++m_requestId;

    if (m_process) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning)
            m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_ffprobePath = ffprobePath;
    m_filePath = filePath;
    m_stdout.clear();
    m_stderr.clear();

    m_process = new QProcess(this);
    const int id = m_requestId;

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this, id] {
        if (id == m_requestId)
            m_stdout += m_process->readAllStandardOutput();
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this, id] {
        if (id == m_requestId)
            m_stderr += m_process->readAllStandardError();
    });
    connect(m_process, &QProcess::finished, this, [this, id](int, QProcess::ExitStatus) {
        if (id == m_requestId)
            handleFinished();
    });
    connect(m_process, &QProcess::errorOccurred, this, [this, id](QProcess::ProcessError error) {
        if (id == m_requestId && error == QProcess::FailedToStart) {
            fail(QStringLiteral("无法启动 ffprobe，请检查工具路径。"), m_process->errorString());
        }
    });

    m_timeout->start();
    // 参数说明：
    //   -v error              只输出错误，减少噪声；
    //   -print_format json    结构化输出，便于稳定解析；
    //   -show_format          容器级信息（时长、大小、format_name）；
    //   -show_streams         流级信息（分辨率、编码、帧率）。
    m_process->start(m_ffprobePath,
                     {QStringLiteral("-v"), QStringLiteral("error"),
                      QStringLiteral("-print_format"), QStringLiteral("json"),
                      QStringLiteral("-show_format"), QStringLiteral("-show_streams"),
                      m_filePath});
}

bool MediaProbe::isBusy() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

// ffprobe 正常结束：读空输出，检查退出码，再交给 parseOutput 解析。
// 非零退出或非正常结束时，优先用 stderr 作为细节，没有 stderr 再用 stdout。
void MediaProbe::handleFinished()
{
    if (!m_process)
        return;

    m_timeout->stop();
    m_stdout += m_process->readAllStandardOutput();
    m_stderr += m_process->readAllStandardError();

    if (m_process->exitStatus() != QProcess::NormalExit || m_process->exitCode() != 0) {
        const QString detail = QString::fromUtf8(m_stderr).trimmed();
        fail(QStringLiteral("无法读取视频信息，文件可能不是有效的视频。"),
             detail.isEmpty() ? QString::fromUtf8(m_stdout).trimmed() : detail);
        return;
    }

    VideoInfo info;
    QString errorMessage;
    if (!parseOutput(m_filePath, m_stdout, info, errorMessage)) {
        fail(errorMessage, QString::fromUtf8(m_stdout).trimmed());
        return;
    }

    emit succeeded(info);
}

// 统一失败出口：停止计时、结束并回收进程，然后发出 failed。
// 先 disconnect(this) 是为了防止 kill() 触发的 finished 信号再次进入处理流程。
void MediaProbe::fail(const QString &message, const QString &detail)
{
    m_timeout->stop();

    if (m_process) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning)
            m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }

    emit failed(message, detail);
}

// 把 ffprobe 的 JSON 文档解析为 VideoInfo。
// 约定：没有任何可用视频流、或 JSON 无法解析时返回 false 并填写 errorMessage。
bool MediaProbe::parseOutput(const QString &filePath, const QByteArray &json, VideoInfo &info,
                             QString &errorMessage) const
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        errorMessage = QStringLiteral("FFprobe 返回的数据无法解析。");
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonObject format = root.value(QStringLiteral("format")).toObject();
    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();

    QJsonObject videoStream;
    for (const QJsonValue &value : streams) {
        const QJsonObject stream = value.toObject();
        if (stream.value(QStringLiteral("codec_type")).toString() != QLatin1String("video"))
            continue;

        const QJsonObject disposition = stream.value(QStringLiteral("disposition")).toObject();
        // 跳过封面图：MP3 / 部分 MP4 会把专辑封面伪装成一条 video 流
        // （disposition.attached_pic == 1），它不是真正的视频内容。
        if (disposition.value(QStringLiteral("attached_pic")).toInt() == 1)
            continue;

        videoStream = stream;
        break;
    }

    if (videoStream.isEmpty()) {
        errorMessage = QStringLiteral("文件中没有可用的视频流。");
        return false;
    }

    info.fileName = QFileInfo(filePath).fileName();
    info.container = displayContainer(filePath, format.value(QStringLiteral("format_name")).toString());
    info.width = videoStream.value(QStringLiteral("width")).toInt();
    info.height = videoStream.value(QStringLiteral("height")).toInt();
    info.codec = displayCodec(videoStream.value(QStringLiteral("codec_name")).toString());

    bool ok = false;
    // 时长优先取容器级 format.duration；部分流（如某些 MKV）容器未写时长，
    // 此时回退到视频流自身的 duration，二者都没有则记为 -1（未知）。
    const double formatDuration =
        parseNumber(format.value(QStringLiteral("duration")), &ok);
    if (ok && formatDuration > 0.0) {
        info.durationSeconds = formatDuration;
    } else {
        const double streamDuration =
            parseNumber(videoStream.value(QStringLiteral("duration")), &ok);
        info.durationSeconds = (ok && streamDuration > 0.0) ? streamDuration : -1.0;
    }

    // 帧率优先使用 avg_frame_rate（平均帧率，更贴近实际播放），
    // 它缺失或非法时再回退到 r_frame_rate（原始帧率）。
    const double frameRate = parseFrameRate(
        videoStream.value(QStringLiteral("avg_frame_rate")).toString());
    info.frameRate = frameRate > 0.0
        ? frameRate
        : parseFrameRate(videoStream.value(QStringLiteral("r_frame_rate")).toString());

    // 文件大小优先使用 ffprobe 报告的 size；解析失败时回退到文件系统大小，
    // 保证界面始终能显示一个有意义的数值。
    const double size = parseNumber(format.value(QStringLiteral("size")), &ok);
    info.sizeBytes = (ok && size >= 0.0)
        ? static_cast<qint64>(size)
        : QFileInfo(filePath).size();

    return true;
}
