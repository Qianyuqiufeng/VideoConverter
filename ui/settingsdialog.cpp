// =============================================================================
// 模块：FFmpeg 工具设置对话框的实现（ui/settingsdialog.cpp）
// -----------------------------------------------------------------------------
// 界面结构：两行“路径输入框 + 浏览按钮” + 状态标签 + 检测/确定/取消按钮。
// 交互逻辑：
//   - 浏览选择后自动补全另一个工具（同目录时）；
//   - “检测”走异步流程，按钮在检测期间禁用；
//   - “确定”前做非空与存在性校验，路径可疑时允许用户二次确认。
// =============================================================================

#include "settingsdialog.h"

#include "ffmpeglocator.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

// 构造：搭建界面并把各按钮连接到对应处理函数。
// 传入的 locator 只用于回填初始路径；检测使用本对话框自己的 m_checker。
SettingsDialog::SettingsDialog(const FFmpegLocator *locator, QWidget *parent)
    : QDialog(parent)
    , m_checker(new FFmpegLocator(this))
{
    setWindowTitle(QStringLiteral("工具设置"));
    setModal(true);
    resize(640, 220);

    m_ffmpegEdit = new QLineEdit(this);
    m_ffprobeEdit = new QLineEdit(this);
    if (locator) {
        m_ffmpegEdit->setText(QDir::toNativeSeparators(locator->ffmpegPath()));
        m_ffprobeEdit->setText(QDir::toNativeSeparators(locator->ffprobePath()));
    }

    auto *ffmpegBrowse = new QPushButton(QStringLiteral("浏览..."), this);
    auto *ffprobeBrowse = new QPushButton(QStringLiteral("浏览..."), this);

    auto *ffmpegRow = new QWidget(this);
    auto *ffmpegRowLayout = new QHBoxLayout(ffmpegRow);
    ffmpegRowLayout->setContentsMargins(0, 0, 0, 0);
    ffmpegRowLayout->addWidget(m_ffmpegEdit);
    ffmpegRowLayout->addWidget(ffmpegBrowse);

    auto *ffprobeRow = new QWidget(this);
    auto *ffprobeRowLayout = new QHBoxLayout(ffprobeRow);
    ffprobeRowLayout->setContentsMargins(0, 0, 0, 0);
    ffprobeRowLayout->addWidget(m_ffprobeEdit);
    ffprobeRowLayout->addWidget(ffprobeBrowse);

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("ffmpeg.exe"), ffmpegRow);
    form->addRow(QStringLiteral("ffprobe.exe"), ffprobeRow);

    m_statusLabel = new QLabel(
        QStringLiteral("尚未检测。建议先点击“检测”确认工具可用。"), this);
    m_statusLabel->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_checkButton = buttons->addButton(QStringLiteral("检测"), QDialogButtonBox::ActionRole);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(form);
    mainLayout->addWidget(m_statusLabel);
    mainLayout->addWidget(buttons);

    connect(ffmpegBrowse, &QPushButton::clicked, this, &SettingsDialog::browseFfmpeg);
    connect(ffprobeBrowse, &QPushButton::clicked, this, &SettingsDialog::browseFfprobe);
    connect(m_checkButton, &QPushButton::clicked, this, &SettingsDialog::startCheck);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    connect(m_checker, &FFmpegLocator::checkFinished, this, &SettingsDialog::onCheckFinished);
}

// 返回输入框中的 ffmpeg 路径（已去除首尾空白）。
QString SettingsDialog::ffmpegPath() const
{
    return m_ffmpegEdit->text().trimmed();
}

// 返回输入框中的 ffprobe 路径（已去除首尾空白）。
QString SettingsDialog::ffprobePath() const
{
    return m_ffprobeEdit->text().trimmed();
}

// 浏览选择 ffmpeg.exe。
// 若 ffprobe 仍为空，且所选 ffmpeg 的同目录存在 ffprobe.exe，则自动补全，
// 减少用户两次选择的麻烦。
void SettingsDialog::browseFfmpeg()
{
    const QFileInfo current(m_ffmpegEdit->text().trimmed());
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择 ffmpeg.exe"), current.absolutePath(),
        QStringLiteral("可执行文件 (*.exe);;所有文件 (*)"));
    if (file.isEmpty())
        return;

    m_ffmpegEdit->setText(QDir::toNativeSeparators(file));

    if (m_ffprobeEdit->text().trimmed().isEmpty()) {
        const QString sibling =
            QDir(QFileInfo(file).absolutePath()).filePath(QStringLiteral("ffprobe.exe"));
        if (QFileInfo::exists(sibling))
            m_ffprobeEdit->setText(QDir::toNativeSeparators(sibling));
    }
}

// 浏览选择 ffprobe.exe；与 browseFfmpeg() 对称，必要时自动补全 ffmpeg。
void SettingsDialog::browseFfprobe()
{
    const QFileInfo current(m_ffprobeEdit->text().trimmed());
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择 ffprobe.exe"), current.absolutePath(),
        QStringLiteral("可执行文件 (*.exe);;所有文件 (*)"));
    if (file.isEmpty())
        return;

    m_ffprobeEdit->setText(QDir::toNativeSeparators(file));

    if (m_ffmpegEdit->text().trimmed().isEmpty()) {
        const QString sibling =
            QDir(QFileInfo(file).absolutePath()).filePath(QStringLiteral("ffmpeg.exe"));
        if (QFileInfo::exists(sibling))
            m_ffmpegEdit->setText(QDir::toNativeSeparators(sibling));
    }
}

// 启动异步检测：
//   1. 两个路径都必须先填写；
//   2. 检测期间禁用按钮并提示“正在检测”；
//   3. 把路径交给私有检测器，结果经 checkFinished 回到 onCheckFinished。
void SettingsDialog::startCheck()
{
    const QString ffmpeg = ffmpegPath();
    const QString ffprobe = ffprobePath();
    if (ffmpeg.isEmpty() || ffprobe.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("请先填写两个工具的完整路径。"));
        return;
    }

    m_checkButton->setEnabled(false);
    m_statusLabel->setText(QStringLiteral("正在检测，请稍候..."));
    m_checker->setPaths(ffmpeg, ffprobe);
    m_checker->checkAsync();
}

// 检测结果展示：
//   - 不可用：状态栏给出简短原因，ToolTip 保留原始错误输出；
//   - 可用：显示两个版本号；若缺编码器，追加缺失列表（不阻止保存）。
void SettingsDialog::onCheckFinished(const FFmpegCheckResult &result)
{
    m_checkButton->setEnabled(true);

    if (!result.toolsAvailable) {
        m_statusLabel->setText(QStringLiteral("不可用：%1").arg(result.errorMessage));
        setToolTip(result.errorDetail);
        return;
    }

    QString text = QStringLiteral("可用。%1；%2").arg(result.ffmpegVersion, result.ffprobeVersion);
    if (!result.missingEncoders.isEmpty()) {
        text += QStringLiteral("；缺少编码器：%1")
                    .arg(result.missingEncoders.join(QStringLiteral(", ")));
    }
    m_statusLabel->setText(text);
    setToolTip(QString());
}

// 确定前的校验：
//   1. 两个路径都不能为空；
//   2. 文件不存在时弹窗二次确认（允许用户先保存、之后再放置文件）；
//   3. 校验通过才真正关闭对话框并返回 Accepted。
void SettingsDialog::accept()
{
    const QString ffmpeg = ffmpegPath();
    const QString ffprobe = ffprobePath();

    if (ffmpeg.isEmpty() || ffprobe.isEmpty()) {
        QMessageBox::warning(
            this, QStringLiteral("路径不完整"),
            QStringLiteral("请填写 ffmpeg.exe 和 ffprobe.exe 的完整路径。"));
        return;
    }

    if (!QFileInfo::exists(ffmpeg) || !QFileInfo::exists(ffprobe)) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("文件不存在"),
            QStringLiteral("指定的文件不存在，仍要保存这些路径吗？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    QDialog::accept();
}
