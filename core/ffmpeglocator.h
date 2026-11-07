// =============================================================================
// 模块：FFmpeg 工具定位与可用性自检（core/ffmpeglocator.h）
// -----------------------------------------------------------------------------
// 职责：
//   1. 维护 ffmpeg.exe / ffprobe.exe 的路径，并持久化到用户配置文件；
//   2. 用户配置有效时优先使用，否则回退到随程序分发的内置工具；
//   3. 以异步方式检测两个工具能否运行、版本号是否可读、所需编码器是否齐全。
//
// 设计要点：
//   - 所有外部进程通过 QProcess 异步执行，不阻塞 GUI；
//   - 检测是一个三段式状态机（ffmpeg 版本 -> ffprobe 版本 -> 编码器列表），
//     每段都有超时保护，结果统一通过 checkFinished 信号广播；
//   - 本类不弹出任何界面，错误文案交由调用方（设置对话框 / 主窗口）展示。
// =============================================================================

#ifndef FFMPEGLOCATOR_H
#define FFMPEGLOCATOR_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;
class QTimer;

// 一次自检的完整结果。由 FFmpegLocator 填充后通过 checkFinished 发出。
// 该结构是值类型，调用方可以安全保存副本。
struct FFmpegCheckResult
{
    // 两个工具都可运行且编码器齐全时为 true；否则为 false。
    bool toolsAvailable = false;
    // ffmpeg -version 的第一行（含版本号），检测失败时为空。
    QString ffmpegVersion;
    // ffprobe -version 的第一行（含版本号），检测失败时为空。
    QString ffprobeVersion;
    // 本项目需要但当前 FFmpeg 未提供的编码器名称列表。
    QStringList missingEncoders;
    // 面向用户的简短错误说明（中文），成功时为空。
    QString errorMessage;
    // 面向排查的原始错误输出（进程 stderr 等），可为空。
    QString errorDetail;
};

// FFmpegLocator：工具路径管理器 + 异步自检器。
// 通常在程序启动时创建一次，主窗口与设置对话框共享同一个实例。
class FFmpegLocator : public QObject
{
    Q_OBJECT

public:
    explicit FFmpegLocator(QObject *parent = nullptr);

    // 从配置文件读取路径；用户配置有效时优先，否则回退到内置工具。
    void loadSettings();
    // 把当前路径写入配置文件（INI），并立即 sync 落盘。
    void saveSettings() const;

    // 内置工具的默认路径（忽略用户配置），找不到时返回空字符串。
    static QString defaultFfmpegPath();
    static QString defaultFfprobePath();
    // 恢复默认：删除配置中的两个路径，并把内存路径重置为内置工具。
    void restoreDefaults();

    // 当前使用的工具路径（可能为空，表示尚未找到）。
    QString ffmpegPath() const;
    QString ffprobePath() const;
    // 更新内存中的路径；不会自动保存，也不会自动重新检测。
    void setPaths(const QString &ffmpegPath, const QString &ffprobePath);

    // 启动一次异步自检；若已在检测中则立即返回（防重入）。
    // 结果通过 checkFinished 信号返回，本函数不阻塞。
    void checkAsync();
    // 是否正在检测（内部状态机是否处于非 Idle）。
    bool isChecking() const;
    // 最近一次自检结果；尚未检测时为空结构（toolsAvailable == false）。
    const FFmpegCheckResult &lastResult() const;

    // 返回配置文件的完整路径（Windows 下通常位于
    // %APPDATA%/VideoConverter/VideoConverter.ini）。
    static QString settingsFilePath();

signals:
    // 自检结束（无论成功或失败）时发出；同一时刻只会有一个检测在进行。
    void checkFinished(const FFmpegCheckResult &result);

private:
    // 自检状态机的阶段。Idle 既表示“空闲”，也充当防重入标志。
    enum class Stage {
        Idle,           // 未检测 / 已结束
        FfmpegVersion,  // 正在读取 ffmpeg -version
        FfprobeVersion, // 正在读取 ffprobe -version
        Encoders        // 正在读取 ffmpeg -encoders
    };

    // 按当前 Stage 启动对应命令，并重置超时计时。
    void startStage();
    // 外部进程结束后的统一回调：判定成败、解析输出、推进状态机。
    void handleFinished();
    // 统一失败出口：停止计时、结束进程、填充结果并发出 checkFinished。
    void finishWithError(const QString &userMessage, const QString &detail);
    // 按“有效用户配置 -> 内置搜索”的规则确定两个工具的最终路径。
    void resolveEffectivePaths(const QString &savedFfmpegPath,
                               const QString &savedFfprobePath);
    // 按约定顺序搜索工具可执行文件；找不到返回空字符串。
    static QString resolveTool(const QString &toolName);

    // 当前生效的工具路径。
    QString m_ffmpegPath;
    QString m_ffprobePath;
    // 最近一次自检结果（对外以 const 引用暴露）。
    FFmpegCheckResult m_result;
    // 复用的子进程对象；每次检测只调用 start()，不重复创建。
    QProcess *m_process = nullptr;
    // 单次检测的超时定时器（单次触发）。
    QTimer *m_timeout = nullptr;
    // 状态机当前阶段；Idle 表示没有检测在进行。
    Stage m_stage = Stage::Idle;
    // 当前阶段累积的 stdout / stderr；每段开始时清空。
    QByteArray m_stdout;
    QByteArray m_stderr;
};

#endif // FFMPEGLOCATOR_H
