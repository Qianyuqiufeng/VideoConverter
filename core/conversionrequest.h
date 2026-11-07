// =============================================================================
// 模块：转换请求参数（core/conversionrequest.h）
// -----------------------------------------------------------------------------
// 职责：把“一次转换需要知道的所有输入”打包成一个值对象，由主窗口（Widget）
//       填充后交给 VideoConverter 执行。这样转换模块不必再回头读取界面控件，
//       也便于后续支持批量转换 / 队列。
//
// 说明：本结构只描述“从哪来、到哪去、转成什么格式”，不包含时长等探测信息；
//       时长由调用方另行传入，因为它是 FFprobe 的结果而不是用户的输入。
// =============================================================================

#ifndef CONVERSIONREQUEST_H
#define CONVERSIONREQUEST_H

#include "formatcatalog.h"

#include <QString>

struct ConversionRequest
{
    // 源视频的绝对路径。
    QString inputPath;
    // 目标文件的完整路径（目录 + 文件名，已由主窗口完成覆盖 / 改名处理）。
    QString outputPath;
    // 目标容器格式，决定 VideoConverter 选择哪一组编码参数。
    TargetFormat format = TargetFormat::Mp4;
};

#endif // CONVERSIONREQUEST_H
