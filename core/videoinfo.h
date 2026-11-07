// =============================================================================
// 模块：视频元信息数据模型（core/videoinfo.h）
// -----------------------------------------------------------------------------
// 职责：
//   1. 定义 VideoInfo 结构体，作为 FFprobe 解析结果在各模块之间传递的统一载体；
//   2. 提供文件大小、时长、帧率三个纯函数，把原始数值转成界面可直接显示的
//      中文 / 人类可读文本。
//
// 依赖：仅依赖 Qt Core（QString / QtGlobal），不涉及 UI 或进程逻辑，
//       因此 core 层与 ui 层都可以自由复用本文件。
//
// 约定：信息缺失时数值字段用 -1 表示“未知”，由格式化函数负责兜底显示。
// =============================================================================

#ifndef VIDEOINFO_H
#define VIDEOINFO_H

#include <QString>
#include <QtGlobal>

// VideoInfo：一条视频文件的“摘要信息”。
// 由 MediaProbe（内部调用 ffprobe）解析产生，供主窗口信息面板、缩略图
// 与转换流程共同使用。
struct VideoInfo
{
    // 仅文件名（不含目录），用于标题与信息面板展示。
    QString fileName;
    // 容器格式的展示名，如 "MP4"、"MKV"；由扩展名或 ffprobe 的 format_name 推断。
    QString container;
    // 视频宽度（像素）；解析失败时为 0。
    int width = 0;
    // 视频高度（像素）；解析失败时为 0。
    int height = 0;
    // 时长（秒）。优先取容器时长，其次取视频流时长；-1 表示未知。
    double durationSeconds = -1.0;
    // 平均帧率（FPS）。优先取 avg_frame_rate，缺失时回退 r_frame_rate；-1 表示未知。
    double frameRate = -1.0;
    // 视频编码格式的展示名，如 "H.264"、"H.265"；空字符串表示未知。
    QString codec;
    // 文件字节数。优先取 ffprobe 的 format.size，缺失时回退文件系统大小。
    qint64 sizeBytes = 0;
};

// 把字节数格式化为 B / KB / MB / GB，并按数量级保留 1~2 位小数。
// 采用 1024 进制，与 Windows 资源管理器的显示习惯一致。
inline QString formatFileSize(qint64 bytes)
{
    constexpr double kb = 1024.0;
    if (bytes < kb)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < kb * kb)
        return QStringLiteral("%1 KB").arg(bytes / kb, 0, 'f', 1);
    if (bytes < kb * kb * kb)
        return QStringLiteral("%1 MB").arg(bytes / (kb * kb), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(bytes / (kb * kb * kb), 0, 'f', 2);
}

// 把秒数格式化为 HH:MM:SS；负数（未知时长）显示为“未知”。
// 例如 3725.4 秒 -> "01:02:05"。
inline QString formatDurationText(double seconds)
{
    if (seconds < 0.0)
        return QStringLiteral("未知");

    const int total = qRound(seconds);
    const int hours = total / 3600;
    const int minutes = (total % 3600) / 60;
    const int secs = total % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(secs, 2, 10, QLatin1Char('0'));
}

// 把帧率格式化为 "29.97 FPS" 形式；非正数（未知帧率）显示为“未知”。
inline QString formatFrameRateText(double fps)
{
    if (fps <= 0.0)
        return QStringLiteral("未知");
    return QStringLiteral("%1 FPS").arg(fps, 0, 'f', 2);
}

#endif // VIDEOINFO_H
