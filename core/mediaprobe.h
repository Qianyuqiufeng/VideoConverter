// =============================================================================
// 模块：视频元信息探测（core/mediaprobe.h）
// -----------------------------------------------------------------------------
// 职责：调用 ffprobe，把视频文件的容器、分辨率、时长、帧率、编码、大小
//       解析成 VideoInfo 对象。这是主窗口信息面板与缩略图的数据来源。
//
// 设计要点：
//   - 全程异步：通过 QProcess 执行 ffprobe，绝不阻塞 GUI；
//   - 单次探测 15 秒超时，失败通过 failed 信号给出中文说明与原始细节；
//   - 用自增 requestId 丢弃过期回调，支持用户快速连续导入不同文件。
// =============================================================================

#ifndef MEDIAPROBE_H
#define MEDIAPROBE_H

#include "videoinfo.h"

#include <QByteArray>
#include <QObject>
#include <QString>

class QProcess;
class QTimer;

// MediaProbe：单个视频文件的元信息探测器。
// 每次调用 probe() 会取消上一次尚未完成的探测，并启动新的 ffprobe 进程。
class MediaProbe : public QObject
{
    Q_OBJECT

public:
    explicit MediaProbe(QObject *parent = nullptr);

    // 探测一个文件。ffprobePath 由 FFmpegLocator 提供，filePath 为目标视频。
    // 结果异步返回：成功发 succeeded，失败发 failed。
    void probe(const QString &ffprobePath, const QString &filePath);
    // 是否有探测正在进行。
    bool isBusy() const;

signals:
    // 探测成功：info 为解析后的完整信息。
    void succeeded(const VideoInfo &info);
    // 探测失败：message 为可直接展示的中文说明，detail 为原始错误输出。
    void failed(const QString &message, const QString &detail);

private:
    // ffprobe 正常结束后的处理入口。
    void handleFinished();
    // 统一失败出口：清理进程并发出 failed 信号。
    void fail(const QString &message, const QString &detail);
    // 把 ffprobe 的 JSON 解析为 VideoInfo；失败时写 errorMessage 并返回 false。
    bool parseOutput(const QString &filePath, const QByteArray &json, VideoInfo &info,
                     QString &errorMessage) const;

    // 当前探测使用的子进程；每次 probe() 重新创建，结束后回收。
    QProcess *m_process = nullptr;
    // 15 秒超时定时器（单次触发）。
    QTimer *m_timeout = nullptr;
    // 本次探测的参数与输出缓冲。
    QString m_ffprobePath;
    QString m_filePath;
    QByteArray m_stdout;
    QByteArray m_stderr;
    // 请求序号：回调中比对，只有最新一次请求的结果才会被采纳。
    int m_requestId = 0;
};

#endif // MEDIAPROBE_H
