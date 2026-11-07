// =============================================================================
// 模块：视频缩略图生成（core/thumbnailgenerator.h）
// -----------------------------------------------------------------------------
// 职责：调用 ffmpeg 从视频中抽取一帧，缩放为适合界面展示的 PNG 预览图。
//
// 设计要点：
//   - 使用 image2pipe 把 PNG 直接写到 stdout，不产生临时文件、不需要清理；
//   - 取帧位置为“时长的 10%，但最多前跳 1 秒”，尽量避开片头黑屏或台标；
//   - 全程异步，单次 30 秒超时，失败不影响主流程（只是不显示缩略图）。
// =============================================================================

#ifndef THUMBNAILGENERATOR_H
#define THUMBNAILGENERATOR_H

#include <QByteArray>
#include <QObject>
#include <QPixmap>
#include <QString>

class QProcess;
class QTimer;

// ThumbnailGenerator：单帧缩略图生成器。
// 每次 generate() 会取消上一次未完成的请求，只保留最新一次的结果。
class ThumbnailGenerator : public QObject
{
    Q_OBJECT

public:
    explicit ThumbnailGenerator(QObject *parent = nullptr);

    // 生成缩略图。durationSeconds 用于计算取帧位置，未知时传负数（将从 0 秒取帧）。
    // 成功发 ready，失败发 failed。
    void generate(const QString &ffmpegPath, const QString &filePath, double durationSeconds);
    // 是否有生成任务正在进行。
    bool isBusy() const;

signals:
    // 生成成功，pixmap 为已解码的缩略图。
    void ready(const QPixmap &pixmap);
    // 生成失败：message 为中文说明，detail 为 ffmpeg 的原始错误输出。
    void failed(const QString &message, const QString &detail);

private:
    // ffmpeg 正常结束后的处理入口：校验退出码并解码 PNG。
    void handleFinished();
    // 统一失败出口：清理进程并发出 failed。
    void fail(const QString &message, const QString &detail);

    // 当前任务使用的子进程；每次 generate() 重新创建，结束后回收。
    QProcess *m_process = nullptr;
    // 30 秒超时定时器（单次触发）。
    QTimer *m_timeout = nullptr;
    // 本次任务的参数与输出缓冲（stdout 即 PNG 字节流）。
    QString m_ffmpegPath;
    QString m_filePath;
    QByteArray m_stdout;
    QByteArray m_stderr;
    // 请求序号：丢弃旧任务迟到的回调，避免缩略图错配。
    int m_requestId = 0;
};

#endif // THUMBNAILGENERATOR_H
