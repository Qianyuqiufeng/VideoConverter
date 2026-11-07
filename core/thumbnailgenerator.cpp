// =============================================================================
// 模块：视频缩略图生成的实现（core/thumbnailgenerator.cpp）
// -----------------------------------------------------------------------------
// 流程：generate() 计算取帧位置并启动 ffmpeg
//       -> handleFinished() 检查退出码
//       -> 用 QPixmap::loadFromData 解码 stdout 中的 PNG。
// =============================================================================

#include "thumbnailgenerator.h"

#include <QProcess>
#include <QTimer>

namespace {

// 单次缩略图生成最多等待 30 秒。
constexpr int kThumbnailTimeoutMs = 30000;
// 取帧位置：时长的 10% 处，但最多前跳 1 秒。
// 这样短视频不会跳到结尾，长视频也不会因片头黑屏而抽到全黑画面。
constexpr double kMaxSeekSeconds = 1.0;
constexpr double kSeekRatio = 0.1;

} // namespace

// 构造：只创建超时定时器；QProcess 按任务创建，避免残留状态。
ThumbnailGenerator::ThumbnailGenerator(QObject *parent)
    : QObject(parent)
    , m_timeout(new QTimer(this))
{
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(kThumbnailTimeoutMs);

    connect(m_timeout, &QTimer::timeout, this, [this] {
        if (m_process && m_process->state() != QProcess::NotRunning)
            fail(QStringLiteral("生成缩略图超时。"), QString());
    });
}

// 生成一帧缩略图。
// 与 MediaProbe 相同：requestId 保证只有最新任务的回调会被采纳，
// 用户快速切换视频时不会显示上一个视频的缩略图。
void ThumbnailGenerator::generate(const QString &ffmpegPath, const QString &filePath,
                                  double durationSeconds)
{
    ++m_requestId;

    if (m_process) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning)
            m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_ffmpegPath = ffmpegPath;
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
            fail(QStringLiteral("无法启动 FFmpeg 生成缩略图。"), m_process->errorString());
        }
    });

    double seekSeconds = 0.0;
    if (durationSeconds > 0.0)
        seekSeconds = qMin(kMaxSeekSeconds, durationSeconds * kSeekRatio);

    m_timeout->start();
    // 参数说明：
    //   -ss <秒>           放在 -i 之前，属于快速定位（input seek），比解码后跳转快；
    //   -frames:v 1        只解一帧；
    //   -vf scale=400:-2   宽度固定 400，高度按比例计算且保证为偶数（编码器要求）；
    //   -an                丢弃音频；
    //   -f image2pipe ... 直接输出 PNG 到 stdout，不落地临时文件。
    m_process->start(m_ffmpegPath,
                     {QStringLiteral("-hide_banner"),
                      QStringLiteral("-loglevel"), QStringLiteral("error"),
                      QStringLiteral("-ss"), QString::number(seekSeconds, 'f', 3),
                      QStringLiteral("-i"), m_filePath,
                      QStringLiteral("-frames:v"), QStringLiteral("1"),
                      QStringLiteral("-vf"), QStringLiteral("scale=400:-2"),
                      QStringLiteral("-an"),
                      QStringLiteral("-f"), QStringLiteral("image2pipe"),
                      QStringLiteral("-vcodec"), QStringLiteral("png"),
                      QStringLiteral("pipe:1")});
}

bool ThumbnailGenerator::isBusy() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

// ffmpeg 结束：先检查退出码，再把 stdout 当作 PNG 字节流解码。
// 任何一个环节失败都只影响缩略图，不影响主流程。
void ThumbnailGenerator::handleFinished()
{
    if (!m_process)
        return;

    m_timeout->stop();
    m_stdout += m_process->readAllStandardOutput();
    m_stderr += m_process->readAllStandardError();

    if (m_process->exitStatus() != QProcess::NormalExit || m_process->exitCode() != 0) {
        fail(QStringLiteral("无法生成视频缩略图。"),
             QString::fromUtf8(m_stderr).trimmed());
        return;
    }

    QPixmap pixmap;
    if (m_stdout.isEmpty() || !pixmap.loadFromData(m_stdout, "PNG")) {
        fail(QStringLiteral("缩略图数据无效。"),
             QString::fromUtf8(m_stderr).trimmed());
        return;
    }

    emit ready(pixmap);
}

// 统一失败出口：停止计时、结束并回收进程，然后发出 failed。
// 先 disconnect(this) 避免 kill() 再次触发 finished 造成重入。
void ThumbnailGenerator::fail(const QString &message, const QString &detail)
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
