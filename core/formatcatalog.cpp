// =============================================================================
// 模块：目标格式目录的实现（core/formatcatalog.cpp）
// -----------------------------------------------------------------------------
// 说明：这里的实现刻意保持“查表”式写法——没有任何分支依赖运行环境，
//       因此结果稳定、易测试，也让界面与转换逻辑共享同一份事实来源。
// =============================================================================

#include "formatcatalog.h"

namespace FormatCatalog {

// 输出格式的权威顺序：MP4、AVI、MKV、MOV。
// Widget 构造下拉框时直接按此顺序添加，currentTargetFormat() 再按索引取回。
QVector<TargetFormat> allFormats()
{
    return {TargetFormat::Mp4, TargetFormat::Avi, TargetFormat::Mkv, TargetFormat::Mov};
}

// 枚举 -> 界面显示名。switch 覆盖全部枚举值，新增格式时编译器会提示遗漏。
QString displayName(TargetFormat format)
{
    switch (format) {
    case TargetFormat::Mp4:
        return QStringLiteral("MP4");
    case TargetFormat::Avi:
        return QStringLiteral("AVI");
    case TargetFormat::Mkv:
        return QStringLiteral("MKV");
    case TargetFormat::Mov:
        return QStringLiteral("MOV");
    }
    return QString();
}

// 枚举 -> 文件扩展名（小写、不带点），用于拼接默认输出文件名。
QString extension(TargetFormat format)
{
    switch (format) {
    case TargetFormat::Mp4:
        return QStringLiteral("mp4");
    case TargetFormat::Avi:
        return QStringLiteral("avi");
    case TargetFormat::Mkv:
        return QStringLiteral("mkv");
    case TargetFormat::Mov:
        return QStringLiteral("mov");
    }
    return QString();
}

// 枚举 -> 该格式在 FFmpeg 中依赖的编码器。
// AVI 为保证兼容性使用 MPEG-4 视频 + MP3 音频；
// MP4 / MKV / MOV 统一使用 H.264（libx264）+ AAC。
QStringList requiredEncoders(TargetFormat format)
{
    if (format == TargetFormat::Avi)
        return {QStringLiteral("mpeg4"), QStringLiteral("libmp3lame")};
    return {QStringLiteral("libx264"), QStringLiteral("aac")};
}

} // namespace FormatCatalog
