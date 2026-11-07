// =============================================================================
// 模块：目标格式目录（core/formatcatalog.h）
// -----------------------------------------------------------------------------
// 职责：集中管理“本程序支持哪些输出格式”这一唯一事实来源，避免格式字符串、
//       扩展名、编码器列表散落在界面与转换逻辑里。
//
// 提供四类查询，且全部由同一个枚举派生，保证界面下拉框、输出文件名后缀、
// FFmpeg 编码参数三者始终一致：
//   - allFormats()       ：界面下拉框的候选列表（顺序即显示顺序）
//   - displayName()      ：给用户看的名字（MP4 / AVI / MKV / MOV）
//   - extension()        ：输出文件后缀（小写）
//   - requiredEncoders() ：该格式依赖的 FFmpeg 编码器，用于转换前预检
// =============================================================================

#ifndef FORMATCATALOG_H
#define FORMATCATALOG_H

#include <QString>
#include <QStringList>
#include <QVector>

// 程序支持的输出容器格式。
// 注意：界面下拉框的索引通过 FormatCatalog::allFormats() 映射回本枚举，
//       因此调整枚举顺序不会破坏映射；真正决定显示顺序的是 allFormats() 的返回值。
enum class TargetFormat {
    Mp4,
    Avi,
    Mkv,
    Mov
};

// 格式目录查询接口集合。全部为无状态函数，可安全地在任意线程读取。
namespace FormatCatalog {


// 返回所有受支持格式，顺序即界面下拉框中的显示顺序。
QVector<TargetFormat> allFormats();
// 返回该格式在界面上显示的名称（全大写，如 "MP4"）。
QString displayName(TargetFormat format);
// 返回该格式对应的文件扩展名（不含点，小写），用于生成默认输出文件名。
QString extension(TargetFormat format);
// 返回该格式在 FFmpeg 中必须存在的编码器名称列表。
// 主窗口在开始转换前会用它与自检结果比对，缺编码器时直接拦截并提示用户。
QStringList requiredEncoders(TargetFormat format);

} // namespace FormatCatalog

#endif // FORMATCATALOG_H
