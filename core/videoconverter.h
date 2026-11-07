// =============================================================================
// 模块：视频格式转换（core/videoconverter.h）
// -----------------------------------------------------------------------------
// 职责：把 ConversionRequest 交给 ffmpeg 执行实际转码，并在过程中汇报进度。
//
// 设计要点：
//   - 参数以 QStringList（argv）形式传递，不拼接 shell 字符串，
//     因此中文路径、空格、特殊字符都不会被二次解析；
//   - 通过 -progress pipe:1 读取机器可读的进度，百分比由 FFprobe 给出的
//     总时长换算，避免解析人读日志；
//   - 支持取消：kill 进程后删除写了一半的输出文件，不留残片；
//   - 转换结束后的“输出是否有效”由主窗口用 FFprobe 二次校验。
// =============================================================================

#ifndef VIDEOCONVERTER_H
#define VIDEOCONVERTER_H

#include "conversionrequest.h"

#include <QByteArray>
#include <QObject>
#include <QString>

class QProcess;

// VideoConverter：单任务转码器。
// 同一实例同一时刻只能执行一个转换；start() 在运行中会被直接忽略。
class VideoConverter : public QObject
{
    Q_OBJECT

public:
    explicit VideoConverter(QObject *parent = nullptr);

    // 开始转换。durationSeconds 用于把 out_time 换算成百分比；
    // 未知时长传负数即可，此时进度条会停留在“不确定”状态。
    void start(const ConversionRequest &request, const QString &ffmpegPath,
               double durationSeconds);
    // 请求取消：立即结束进程，handleFinished 会删除未完成的输出文件。
    void cancel();
    // 是否有转换任务正在运行（含取消后等待进程退出的短暂阶段）。
    bool isRunning() const;

signals:
    // 进度变化：0~100 为确定进度；-1 表示总时长未知，界面应显示忙碌态。
    void progressChanged(int percent);
    // 附加状态文本，例如 "速度 1.8x"。
    void statusChanged(const QString &text);
    // 转换结束（成功或失败）。detail 为 ffmpeg 的原始错误输出，可为空。
    void finished(bool success, const QString &message, const QString &detail);
    // 用户主动取消后发出（与 finished(false) 互斥）。
    void cancelled();

private:
    // ffmpeg 结束后的统一处理：区分取消 / 失败 / 成功。
    void handleFinished();
    // 从 -progress 输出缓冲中按行解析进度并发出信号。
    void parseProgressLines();
    // 启动失败等早期错误的统一出口。
    void emitFailure(const QString &message, const QString &detail);

    // 常驻子进程（生命周期内复用）。
    QProcess *m_process = nullptr;
    // 当前任务的请求参数与总时长。
    ConversionRequest m_request;
    double m_durationSeconds = -1.0;
    // 增量输出缓冲。stdout 只保留未解析完的进度行，stderr 只保留末尾 64KB。
    QByteArray m_stdout;
    QByteArray m_stderr;
    // 是否正在运行 / 是否由用户主动取消。
    bool m_running = false;
    bool m_cancelled = false;
};

#endif // VIDEOCONVERTER_H
