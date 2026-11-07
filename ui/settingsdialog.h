// =============================================================================
// 模块：FFmpeg 工具设置对话框（ui/settingsdialog.h）
// -----------------------------------------------------------------------------
// 职责：让用户填写 / 浏览 ffmpeg.exe 与 ffprobe.exe 的路径，并在保存前
//       执行一次异步可用性检测。
//
// 设计要点：
//   - 构造时只读取传入的 FFmpegLocator（const 指针）作为初始值；
//   - 对话框内部另建一个独立的 FFmpegLocator 作为“检测器”，
//     检测过程不会污染主窗口正在使用的配置；
//   - 点击“确定”时校验路径非空且文件存在，不存在时二次确认；
//   - 选择其中一个工具后，若另一个为空且同目录存在同名工具，会自动补全。
// =============================================================================

#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

class FFmpegLocator;
struct FFmpegCheckResult;
class QLabel;
class QLineEdit;
class QPushButton;

// SettingsDialog：模态的工具路径设置对话框。
// 用户确认后，由调用方（Widget）读取 ffmpegPath()/ffprobePath() 并持久化。
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    // locator 仅用于读取当前路径作为初始值，可为空。
    explicit SettingsDialog(const FFmpegLocator *locator, QWidget *parent = nullptr);

    // 用户填写（或浏览选择）的工具路径，已去除首尾空白。
    QString ffmpegPath() const;
    QString ffprobePath() const;
    // 用户是否选择了“恢复默认”：此时调用方应清除配置而不是保存当前路径。
    bool usingDefaults() const;

protected:
    // 重写“确定”：先校验路径完整性，再决定是否真正关闭对话框。
    void accept() override;

private:
    // 打开文件对话框选择 ffmpeg.exe / ffprobe.exe。
    void browseFfmpeg();
    void browseFfprobe();
    // 把两个输入框重置为程序内置工具；找不到内置工具时只提示，不改动输入。
    void restoreDefaults();
    // 用当前输入的路径启动一次异步检测。
    void startCheck();
    // 检测结束：更新状态文本与 ToolTip，并恢复“检测”按钮。
    void onCheckFinished(const FFmpegCheckResult &result);

    // 两个路径输入框与检测状态标签。
    QLineEdit *m_ffmpegEdit = nullptr;
    QLineEdit *m_ffprobeEdit = nullptr;
    QLabel *m_statusLabel = nullptr;
    // “检测”按钮（检测期间禁用，防止重复触发）。
    QPushButton *m_checkButton = nullptr;
    // 对话框私有的检测器，避免影响主窗口的 FFmpegLocator 状态。
    FFmpegLocator *m_checker = nullptr;
    // 是否通过“恢复默认”得到了当前路径；用户再手动选择或编辑时清除。
    bool m_usingDefaults = false;
};

#endif // SETTINGSDIALOG_H
