// =============================================================================
// 模块：主窗口（widget.h）—— 应用控制器
// -----------------------------------------------------------------------------
// 职责：把 core 层的服务与界面组装起来，负责完整用户流程：
//   导入视频 -> FFprobe 读取信息 -> 生成缩略图 -> 选择目标格式/输出路径
//   -> FFmpeg 转换 -> FFprobe 校验输出 -> 展示结果。
//
// 本类不是“纯视图”：它持有业务状态（当前文件、探测是否成功、待关闭标志等），
// 并把 core 服务的异步信号翻译成界面状态变化。
//
// 持有的服务（均以 Qt 父子关系管理，随窗口销毁）：
//   - FFmpegLocator      ：定位工具、启动时自检、设置对话框读写
//   - MediaProbe         ：探测“输入文件”的元信息（m_probe）
//   - MediaProbe         ：探测“输出文件”以校验转换结果（m_outputProbe）
//   - ThumbnailGenerator ：生成预览缩略图
//   - VideoConverter     ：执行实际转码
//
// 说明：界面控件由 widget.ui 生成（Ui::Widget），业务逻辑全部写在本类中，
//       让 .ui 只描述布局与初始文案。
// =============================================================================

#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QPixmap>

#include "formatcatalog.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class Widget;
}
QT_END_NAMESPACE

class FFmpegLocator;
class MediaProbe;
class ThumbnailGenerator;
class VideoConverter;
class QCloseEvent;
class QEvent;
class QResizeEvent;
struct VideoInfo;

// Widget：应用主窗口与业务编排者。
// 生命周期内只创建一个，所有子服务由 Qt 对象树负责释放。
class Widget : public QWidget
{
    Q_OBJECT

public:
    // 构造：初始化界面、填装格式列表、连接所有信号并触发启动自检。
    Widget(QWidget *parent = nullptr);
    // 析构：释放 uic 生成的界面对象（其余子对象由 Qt 父子关系回收）。
    ~Widget();

protected:
    // 关闭窗口：若正在转换，先询问并走“取消 -> 关闭”的安全流程。
    void closeEvent(QCloseEvent *event) override;
    // 尺寸变化：延迟刷新文件名 / 输出路径的省略显示。
    void resizeEvent(QResizeEvent *event) override;
    // 窗口状态变化（如最大化）：切换预览区尺寸。
    void changeEvent(QEvent *event) override;

private:
    void openSettings();  // 打开工具设置并应用新路径
    void browseForVideo();  // 通过文件对话框选择视频
    void importVideo(const QString &filePath);  // 校验并导入视频（拖放/对话框共用入口）
    void updateInfoPanel(const VideoInfo &info);  // 用探测结果刷新信息面板
    void resetInfoPanel();  // 信息面板恢复为占位符
    void browseOutputDir();  // 选择输出目录
    void onFormatChanged();  // 切换目标格式时同步输出文件后缀
    void updateOutputPathPreview();  // 刷新完整输出路径与状态提示
    void resetOutputSettings();  // 清空输出设置并禁用输出分组
    TargetFormat currentTargetFormat() const;  // 下拉框索引 -> 格式枚举
    void startConversion();  // 转换前校验并启动任务
    void onConversionFinished(bool success, const QString &message, const QString &detail);  // 转码结束：成功后再做输出校验
    void onConversionCancelled();  // 用户取消后的收尾
    void onOutputValidated(const VideoInfo &info);  // 输出文件校验通过
    void onOutputValidationFailed(const QString &message, const QString &detail);  // 输出文件校验失败
    void setConvertingUi(bool converting);  // 转换期间禁用/恢复交互控件
    void updateStartButton();  // 按探测与运行状态刷新按钮
    QString makeUniqueOutputPath(const QString &path) const;  // 生成不冲突的“名称(n).后缀”
    void setConversionState(const QString &state, const QString &text);  // 状态文本 + QSS 动态属性
    void setFileNameDisplay(const QString &fileName);  // 记录并刷新文件名显示
    void updateFileNameLabel();  // 文件名中间省略
    void updateOutputPathLabel();  // 输出路径中间省略
    void applyResponsiveLayout();  // 普通/最大化两种预览尺寸
    void applyThumbnailPixmap();  // 按控件尺寸缩放并显示缩略图

    // uic 生成的界面对象，所有控件通过 ui->xxx 访问。
    Ui::Widget *ui;
    // 工具定位 / 自检服务（主窗口与设置对话框共享）。
    FFmpegLocator *m_locator = nullptr;
    // 输入文件探测与输出文件校验各用一个实例，避免互相打断。
    MediaProbe *m_probe = nullptr;
    MediaProbe *m_outputProbe = nullptr;
    // 缩略图生成与转码服务。
    ThumbnailGenerator *m_thumbnails = nullptr;
    VideoConverter *m_converter = nullptr;
    // 当前导入的视频绝对路径；为空表示未导入。
    QString m_currentFile;
    // 自动生成的输出文件名（随格式变化），用于判断用户是否手动改过文件名。
    QString m_autoOutputName;
    // 最近一次转换的目标路径，校验成功时用于提示用户。
    QString m_lastOutputPath;
    // 需要省略显示的长文本：完整文件名、完整输出路径。
    QString m_fileNameText;
    QString m_outputPathText;
    // 原始缩略图（未按控件尺寸缩放），窗口尺寸变化时据此重新缩放。
    QPixmap m_thumbnailSource;
    // 输入文件的时长（秒），未知为 -1；转码时用于计算进度百分比。
    double m_probeDuration = -1.0;
    // 探测是否成功：只有成功后才允许配置输出与开始转换。
    bool m_probeSucceeded = false;
    // 用户已确认“取消并退出”，等待转换结束后真正关闭窗口。
    bool m_pendingClose = false;
};
#endif // WIDGET_H
