// =============================================================================
// 模块：FFmpeg 工具定位与可用性自检（core/ffmpeglocator.cpp）
// -----------------------------------------------------------------------------
// 本文件实现三件事：
//   1. 路径持久化：读写 INI 配置，并在缺失时自动探测；
//   2. 路径搜索：程序目录 -> ffmpeg/bin -> 编译期 FFMPEG_BIN_DIR -> 系统 PATH；
//   3. 异步自检：三段式调用 ffmpeg / ffprobe，全程不阻塞界面。
//
// 线程模型：所有外部命令都通过 QProcess 异步执行，结果经信号回到 GUI 线程；
//           每一段都有 10 秒超时，避免损坏的可执行文件让界面永久等待。
// =============================================================================

#include "ffmpeglocator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

namespace {

// QSettings 的组织名 / 应用名，共同决定配置文件路径。
constexpr char kSettingsOrg[] = "VideoConverter";
constexpr char kSettingsApp[] = "VideoConverter";
// 每一次外部命令调用的超时上限（毫秒）。
constexpr int kCheckTimeoutMs = 10000;

// 在指定目录下查找可执行文件；找到返回绝对路径，否则返回空字符串。
// 目录为空、文件不存在、路径是目录而非文件时都视为未找到。
QString findInDir(const QString &dir, const QString &exeName)
{
    if (dir.isEmpty())
        return QString();

    const QFileInfo candidate(QDir(dir).filePath(exeName));
    return candidate.isFile() ? candidate.absoluteFilePath() : QString();
}

} // namespace

// 构造：创建常驻的 QProcess 与超时定时器，并把它们的信号接到内部处理逻辑。
// QProcess 在对象生命周期内复用，既减少分配，也保证同一时刻只有一条命令在跑。
FFmpegLocator::FFmpegLocator(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
    , m_timeout(new QTimer(this))
{
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(kCheckTimeoutMs);

    // 分阶段累积标准输出 / 标准错误，由 handleFinished() 统一解析。
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_stdout += m_process->readAllStandardOutput();
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        m_stderr += m_process->readAllStandardError();
    });
    connect(m_process, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        handleFinished();
    });
    // 只有“进程无法启动”（路径错误、权限不足等）在这里处理；
    // 进程能启动但异常退出，则由 finished 回调统一判定。
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_stage != Stage::Idle) {
            finishWithError(QStringLiteral("无法启动 FFmpeg，请检查路径和文件权限。"),
                            m_process->errorString());
        }
    });
    // 超时保护：任何阶段超时都立即判失败，避免界面卡在“检测中”。
    connect(m_timeout, &QTimer::timeout, this, [this] {
        if (m_stage == Stage::Idle)
            return;
        finishWithError(QStringLiteral("检测 FFmpeg 超时，请确认程序可以正常运行。"), QString());
    });
}

// 读取配置：优先使用用户手动指定的路径；未指定时尝试自动探测。
// ffprobe 会优先在 ffmpeg 同目录查找，因为二者通常来自同一份发行包。
void FFmpegLocator::loadSettings()
{
    const QSettings settings(settingsFilePath(), QSettings::IniFormat);
    m_ffmpegPath = settings.value(QStringLiteral("tools/ffmpegPath")).toString().trimmed();
    m_ffprobePath = settings.value(QStringLiteral("tools/ffprobePath")).toString().trimmed();

    if (m_ffmpegPath.isEmpty())
        m_ffmpegPath = resolveTool(QStringLiteral("ffmpeg"));

    if (m_ffprobePath.isEmpty()) {
        if (!m_ffmpegPath.isEmpty()) {
            m_ffprobePath = findInDir(QFileInfo(m_ffmpegPath).absolutePath(),
                                      QStringLiteral("ffprobe.exe"));
        }
        if (m_ffprobePath.isEmpty())
            m_ffprobePath = resolveTool(QStringLiteral("ffprobe"));
    }
}

// 保存路径到 INI 配置。sync() 确保立即写盘，避免程序异常退出时丢失设置。
void FFmpegLocator::saveSettings() const
{
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("tools/ffmpegPath"), m_ffmpegPath);
    settings.setValue(QStringLiteral("tools/ffprobePath"), m_ffprobePath);
    settings.sync();
}

QString FFmpegLocator::ffmpegPath() const
{
    return m_ffmpegPath;
}

QString FFmpegLocator::ffprobePath() const
{
    return m_ffprobePath;
}

void FFmpegLocator::setPaths(const QString &ffmpegPath, const QString &ffprobePath)
{
    m_ffmpegPath = ffmpegPath.trimmed();
    m_ffprobePath = ffprobePath.trimmed();
}

// 启动一次异步自检。
// 若正在检测则直接返回，保证同一时刻只有一条检测链在跑。
// 先做文件存在性预检：路径为空或文件不存在时立即失败，不启动任何进程。
void FFmpegLocator::checkAsync()
{
    if (m_stage != Stage::Idle)
        return;

    m_result = FFmpegCheckResult();
    m_stdout.clear();
    m_stderr.clear();

    if (m_ffmpegPath.isEmpty() || !QFileInfo::exists(m_ffmpegPath)) {
        finishWithError(QStringLiteral("未找到 ffmpeg.exe，请在“工具设置”中指定它的路径。"),
                        m_ffmpegPath);
        return;
    }
    if (m_ffprobePath.isEmpty() || !QFileInfo::exists(m_ffprobePath)) {
        finishWithError(QStringLiteral("未找到 ffprobe.exe，请在“工具设置”中指定它的路径。"),
                        m_ffprobePath);
        return;
    }

    m_stage = Stage::FfmpegVersion;
    startStage();
}

bool FFmpegLocator::isChecking() const
{
    return m_stage != Stage::Idle;
}

const FFmpegCheckResult &FFmpegLocator::lastResult() const
{
    return m_result;
}

QString FFmpegLocator::settingsFilePath()
{
    const QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                             QString::fromLatin1(kSettingsOrg),
                             QString::fromLatin1(kSettingsApp));
    return settings.fileName();
}

// 按当前 Stage 启动对应的外部命令，并重新开始超时计时。
// 每段开始前清空输出缓冲，防止上一段的输出混入本段解析。
void FFmpegLocator::startStage()
{
    m_stdout.clear();
    m_stderr.clear();
    m_timeout->start();

    switch (m_stage) {
    case Stage::FfmpegVersion:
        m_process->start(m_ffmpegPath, {QStringLiteral("-version")});
        break;
    case Stage::FfprobeVersion:
        m_process->start(m_ffprobePath, {QStringLiteral("-version")});
        break;
    case Stage::Encoders:
        m_process->start(m_ffmpegPath,
                         {QStringLiteral("-hide_banner"), QStringLiteral("-encoders")});
        break;
    case Stage::Idle:
        break;
    }
}

// 外部进程结束后的统一处理：
//   1. 停止超时计时，并读空剩余输出；
//   2. 非正常退出 -> 按当前阶段生成用户可读的错误说明；
//   3. 正常退出   -> 解析本段结果并推进到下一阶段；
//      最后一段（编码器列表）结束后填充结果并发出 checkFinished。
void FFmpegLocator::handleFinished()
{
    if (m_stage == Stage::Idle)
        return;

    m_timeout->stop();
    m_stdout += m_process->readAllStandardOutput();
    m_stderr += m_process->readAllStandardError();

    const int exitCode = m_process->exitCode();
    const QProcess::ExitStatus exitStatus = m_process->exitStatus();
    const QString output = QString::fromUtf8(m_stdout).trimmed();
    const QString errorOutput = QString::fromUtf8(m_stderr).trimmed();

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        const QString detail = errorOutput.isEmpty() ? output : errorOutput;
        switch (m_stage) {
        case Stage::FfmpegVersion:
            finishWithError(QStringLiteral("ffmpeg.exe 运行失败，请检查它是否为可用的 Windows x64 版本。"),
                            detail);
            break;
        case Stage::FfprobeVersion:
            finishWithError(QStringLiteral("ffprobe.exe 运行失败，请检查它是否为可用的 Windows x64 版本。"),
                            detail);
            break;
        case Stage::Encoders:
            finishWithError(QStringLiteral("无法读取 FFmpeg 编码器列表。"), detail);
            break;
        case Stage::Idle:
            break;
        }
        return;
    }

    switch (m_stage) {
    case Stage::FfmpegVersion:
        m_result.ffmpegVersion = output.section(QLatin1Char('\n'), 0, 0).trimmed();
        m_stage = Stage::FfprobeVersion;
        startStage();
        break;
    case Stage::FfprobeVersion:
        m_result.ffprobeVersion = output.section(QLatin1Char('\n'), 0, 0).trimmed();
        m_stage = Stage::Encoders;
        startStage();
        break;
    case Stage::Encoders: {
        // 本项目实际用到的编码器：
        // MP4/MKV/MOV -> libx264 + aac；AVI -> mpeg4 + libmp3lame。
        // 只要列表中出现缺失项，就记入 missingEncoders 供上层提前拦截。
        const QStringList required = {
            QStringLiteral("libx264"),
            QStringLiteral("aac"),
            QStringLiteral("mpeg4"),
            QStringLiteral("libmp3lame"),
        };
        for (const QString &encoder : required) {
            if (!output.contains(encoder, Qt::CaseInsensitive))
                m_result.missingEncoders.append(encoder);
        }
        m_result.toolsAvailable = true;
        m_stage = Stage::Idle;
        emit checkFinished(m_result);
        break;
    }
    case Stage::Idle:
        break;
    }
}

// 统一失败出口：复位状态机、停止计时、结束仍存活的进程，然后发出结果。
// 无论失败发生在哪个阶段，调用方都只需监听 checkFinished。
void FFmpegLocator::finishWithError(const QString &userMessage, const QString &detail)
{
    m_stage = Stage::Idle;
    m_timeout->stop();
    if (m_process->state() != QProcess::NotRunning)
        m_process->kill();

    m_result.toolsAvailable = false;
    m_result.errorMessage = userMessage;
    m_result.errorDetail = detail;
    emit checkFinished(m_result);
}

// 按约定顺序搜索工具：找到即返回绝对路径，全部失败才返回空字符串。
// 搜索顺序体现“随程序分发优先、系统安装兜底”的原则。
QString FFmpegLocator::resolveTool(const QString &toolName) const
{
    const QString exeName = toolName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)
                                ? toolName
                                : toolName + QStringLiteral(".exe");

    const QString appDir = QCoreApplication::applicationDirPath();

    // 1) 可执行文件同目录：用户把 ffmpeg.exe 直接放在程序旁边。
    QString path = findInDir(appDir, exeName);
    if (!path.isEmpty())
        return path;

    // 2) 程序目录下的 ffmpeg/bin 子目录：便携版常见布局。
    path = findInDir(QDir(appDir).filePath(QStringLiteral("ffmpeg/bin")), exeName);
    if (!path.isEmpty())
        return path;

    // 3) 编译期写入的 third_party/ffmpeg/bin（见 VideoConverter.pro 的 DEFINES）。
#ifdef FFMPEG_BIN_DIR
    path = findInDir(QString::fromUtf8(FFMPEG_BIN_DIR), exeName);
    if (!path.isEmpty())
        return path;
#endif

    // 4) 系统 PATH。找不到时返回空字符串，由调用方提示用户手动配置。
    return QStandardPaths::findExecutable(toolName);
}
