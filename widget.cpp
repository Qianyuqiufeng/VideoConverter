// =============================================================================
// 模块：主窗口的实现（widget.cpp）
// -----------------------------------------------------------------------------
// 本文件承载应用的全部交互编排。建议阅读顺序：
//   1. Widget 构造函数：建立信号/槽连接，理清服务与界面的关系；
//   2. importVideo()：导入与探测流程（含路径与权限校验）；
//   3. startConversion()：转换前的全部前置校验（路径、编码器、覆盖策略）；
//   4. onConversionFinished() -> onOutputValidated()：转换后的二次校验闭环；
//   5. 其余为界面状态、文本省略与自适应布局的辅助函数。
//
// 线程模型：所有耗时操作（探测、缩略图、转码）都在 core 层通过 QProcess
//           异步执行，本类只在主线程响应信号并更新界面，不会阻塞事件循环。
// =============================================================================

#include "widget.h"
#include "ui_widget.h"

#include "conversionrequest.h"
#include "droparea.h"
#include "ffmpeglocator.h"
#include "mediaprobe.h"
#include "settingsdialog.h"
#include "thumbnailgenerator.h"
#include "videoinfo.h"
#include "videoconverter.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>

namespace {

// 生成默认输出文件的主干名：原文件名 + "_converted"。
// 幂等处理：已经以 "_converted" 结尾时不再重复追加，避免 a_converted_converted.mp4。
QString convertedBaseName(const QString &baseName)
{
    if (baseName.endsWith(QStringLiteral("_converted"), Qt::CaseInsensitive))
        return baseName;
    return baseName + QStringLiteral("_converted");
}

} // namespace

// 构造：创建服务对象、初始化界面、连接信号，最后触发启动自检。
// 这里建立的是“界面 -> 服务”与“服务 -> 界面”的完整双向关系，
// 后续所有交互都通过信号驱动，不再有同步阻塞调用。
Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
    , m_locator(new FFmpegLocator(this))
    , m_probe(new MediaProbe(this))
    , m_outputProbe(new MediaProbe(this))
    , m_thumbnails(new ThumbnailGenerator(this))
    , m_converter(new VideoConverter(this))
{
    // 由 widget.ui 生成的界面初始化；此后所有控件通过 ui-> 访问。
    ui->setupUi(this);
    ui->toolStatusLabel->setTextFormat(Qt::RichText);
    setConversionState(QStringLiteral("idle"), QStringLiteral("请先选择视频。"));
    applyResponsiveLayout();

    // 下拉框内容完全由 FormatCatalog 决定，避免界面与转换逻辑各写一份格式列表。
    for (const TargetFormat format : FormatCatalog::allFormats())
        ui->formatCombo->addItem(FormatCatalog::displayName(format));

    connect(ui->formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &Widget::onFormatChanged);
    connect(ui->outputDirButton, &QPushButton::clicked, this, &Widget::browseOutputDir);
    connect(ui->outputNameEdit, &QLineEdit::textChanged,
            this, &Widget::updateOutputPathPreview);
    resetOutputSettings();

    connect(ui->settingsButton, &QPushButton::clicked, this, &Widget::openSettings);
    connect(ui->browseButton, &QPushButton::clicked, this, &Widget::browseForVideo);
    connect(ui->dropArea, &DropArea::fileDropped, this, &Widget::importVideo);
    connect(ui->dropArea, &DropArea::browseRequested, this, &Widget::browseForVideo);
    // 输入探测成功：刷新信息面板与状态，随后尝试生成缩略图。
    // 注意：缩略图失败不会影响转换能力，因此这里只是不显示预览。
    connect(m_probe, &MediaProbe::succeeded, this,
            [this](const VideoInfo &info) {
                updateInfoPanel(info);
                m_probeSucceeded = true;
                m_probeDuration = info.durationSeconds;
                ui->outputGroup->setEnabled(true);
                setConversionState(QStringLiteral("ready"),
                                   QStringLiteral("可以开始转换。"));
                updateStartButton();

                const QString ffmpegPath = m_locator->ffmpegPath();
                if (ffmpegPath.isEmpty() || !QFileInfo::exists(ffmpegPath)) {
                    ui->thumbnailLabel->setPixmap(QPixmap());
                    ui->thumbnailLabel->setText(QStringLiteral("缩略图不可用"));
                    ui->thumbnailLabel->setAccessibleDescription(
                        QStringLiteral("缩略图未加载"));
                    return;
                }
                // 仅在 ffmpeg 路径有效时生成缩略图；否则保持“缩略图不可用”。
                m_thumbnails->generate(ffmpegPath, m_currentFile, info.durationSeconds);
            });
    // 输入探测失败：清空信息面板、禁止输出设置，并把原始错误放进“详细信息”。
    connect(m_probe, &MediaProbe::failed, this,
            [this](const QString &message, const QString &detail) {
                resetInfoPanel();
                m_probeSucceeded = false;
                m_probeDuration = -1.0;
                ui->outputGroup->setEnabled(false);
                setConversionState(QStringLiteral("error"),
                                   QStringLiteral("无法读取视频信息。"));
                updateStartButton();
                if (!m_currentFile.isEmpty())
                    setFileNameDisplay(QFileInfo(m_currentFile).fileName());

                ui->thumbnailLabel->setPixmap(QPixmap());
                ui->thumbnailLabel->setText(QStringLiteral("—"));
                ui->thumbnailLabel->setAccessibleDescription(QStringLiteral("缩略图未加载"));
                m_thumbnailSource = QPixmap();

                QMessageBox box(QMessageBox::Warning, QStringLiteral("无法读取视频信息"),
                                message, QMessageBox::Ok, this);
                if (!detail.isEmpty())
                    box.setDetailedText(detail);
                box.exec();
            });
    connect(m_thumbnails, &ThumbnailGenerator::ready, this, [this](const QPixmap &pixmap) {
        m_thumbnailSource = pixmap;
        applyThumbnailPixmap();
    });
    connect(m_thumbnails, &ThumbnailGenerator::failed, this,
            [this](const QString &message, const QString &detail) {
                m_thumbnailSource = QPixmap();
                ui->thumbnailLabel->setPixmap(QPixmap());
                ui->thumbnailLabel->setText(message);
                ui->thumbnailLabel->setToolTip(detail);
                ui->thumbnailLabel->setAccessibleDescription(QStringLiteral("缩略图未加载"));
            });

    // 转换相关连接：开始、取消、进度、完成、取消完成、输出校验。
    connect(ui->startButton, &QPushButton::clicked, this, &Widget::startConversion);
    // 取消：先禁用按钮并提示“正在取消”，避免用户重复点击；
    // 真正的进程结束由 VideoConverter::cancelled 信号收尾。
    connect(ui->cancelButton, &QPushButton::clicked, this, [this] {
        if (!m_converter->isRunning())
            return;
        ui->cancelButton->setEnabled(false);
        setConversionState(QStringLiteral("working"), QStringLiteral("正在取消..."));
        m_converter->cancel();
    });
    // 进度：percent < 0 表示总时长未知，进度条切换为不确定动画（范围 0,0）。
    connect(m_converter, &VideoConverter::progressChanged, this, [this](int percent) {
        if (percent < 0) {
            ui->progressBar->setRange(0, 0);
            setConversionState(QStringLiteral("working"),
                               QStringLiteral("正在转换视频。"));
        } else {
            ui->progressBar->setRange(0, 100);
            ui->progressBar->setValue(percent);
            setConversionState(QStringLiteral("working"), QStringLiteral("正在转换视频。"));
        }
    });
    connect(m_converter, &VideoConverter::finished, this, &Widget::onConversionFinished);
    connect(m_converter, &VideoConverter::cancelled, this, &Widget::onConversionCancelled);
    connect(m_outputProbe, &MediaProbe::succeeded, this, &Widget::onOutputValidated);
    connect(m_outputProbe, &MediaProbe::failed, this, &Widget::onOutputValidationFailed);
    connect(m_locator, &FFmpegLocator::checkFinished, this,
            [this](const FFmpegCheckResult &result) {
                if (result.toolsAvailable) {
                    QString text;
                    if (result.missingEncoders.isEmpty()) {
                        text = QStringLiteral(
                            "<span style=\"color:#1a7f37;\">●</span> FFmpeg 可用");
                    } else {
                        text = QStringLiteral(
                                   "<span style=\"color:#bf8700;\">●</span> FFmpeg 可用（缺少编码器：%1）")
                                   .arg(result.missingEncoders.join(QStringLiteral(", "))
                                            .toHtmlEscaped());
                    }
                    ui->toolStatusLabel->setText(text);
                } else {
                    ui->toolStatusLabel->setText(
                        QStringLiteral(
                            "<span style=\"color:#d1242f;\">●</span> FFmpeg 不可用：%1")
                            .arg(result.errorMessage.toHtmlEscaped()));
                }
                ui->toolStatusLabel->setToolTip(result.errorDetail);
            });

    // 启动流程：先读配置（含自动探测），再在当前事件循环空闲时异步自检，
    // 保证窗口先显示出来，不会因为检测进程而推迟首帧。
    m_locator->loadSettings();
    QTimer::singleShot(0, m_locator, &FFmpegLocator::checkAsync);
}

Widget::~Widget()
{
    delete ui;
}

// 打开设置对话框。用户确认后：应用新路径 -> 持久化 -> 立即重新自检。
// 点击“恢复默认”时改为清除配置，让程序回到内置工具。
void Widget::openSettings()
{
    SettingsDialog dialog(m_locator, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    if (dialog.usingDefaults()) {
        m_locator->restoreDefaults();
    } else {
        m_locator->setPaths(dialog.ffmpegPath(), dialog.ffprobePath());
        m_locator->saveSettings();
    }
    m_locator->checkAsync();
}

// 通过文件对话框选择视频；过滤器覆盖常见容器，同时允许“所有文件”。
void Widget::browseForVideo()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择视频文件"), QString(),
        QStringLiteral("视频文件 (*.mp4 *.avi *.mov *.mkv *.wmv *.m4v *.flv *.webm);;所有文件 (*)"));
    if (!filePath.isEmpty())
        importVideo(filePath);
}

// 导入视频（拖放与文件对话框共用入口）。
// 流程：
//   1. 校验存在性、是否为文件、是否为空、是否可读；
//   2. 记录当前文件并更新拖放区显示；
//   3. 按“输入目录 + 原文件名_converted + 目标格式后缀”预填输出设置；
//   4. 检查 ffprobe 是否可用，然后发起异步探测。
void Widget::importVideo(const QString &filePath)
{
    const QFileInfo info(filePath);

    QString error;
    if (!info.exists()) {
        error = QStringLiteral("文件不存在：%1").arg(QDir::toNativeSeparators(filePath));
    } else if (!info.isFile()) {
        error = QStringLiteral("请选择一个文件，而不是文件夹。");
    } else if (info.size() <= 0) {
        error = QStringLiteral("文件为空，无法处理。");
    } else if (!info.isReadable()) {
        error = QStringLiteral("文件不可读，请检查访问权限。");
    }

    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("无法导入"), error);
        return;
    }

    m_currentFile = info.absoluteFilePath();
    ui->dropArea->setFileName(m_currentFile);
    setFileNameDisplay(info.fileName());
    setConversionState(QStringLiteral("working"), QStringLiteral("正在读取视频信息..."));

    // 默认输出到源文件所在目录，文件名主干加 "_converted"，后缀随目标格式。
    // 这里用 QSignalBlocker 暂时屏蔽 textChanged，避免逐字符触发路径预览刷新。
    ui->outputDirEdit->setText(QDir::toNativeSeparators(info.absolutePath()));
    m_autoOutputName = convertedBaseName(info.completeBaseName()) + QLatin1Char('.')
        + FormatCatalog::extension(currentTargetFormat());
    {
        const QSignalBlocker blocker(ui->outputNameEdit);
        ui->outputNameEdit->setText(m_autoOutputName);
    }
    updateOutputPathPreview();
    ui->outputGroup->setEnabled(false);
    m_probeSucceeded = false;
    m_probeDuration = -1.0;
    updateStartButton();

    // 探测前先确认 ffprobe 存在；缺失时给出明确指引（去“工具设置”配置），
    // 而不是让用户面对一个模糊的失败提示。
    const QString ffprobePath = m_locator->ffprobePath();
    if (ffprobePath.isEmpty() || !QFileInfo::exists(ffprobePath)) {
        resetInfoPanel();
        setFileNameDisplay(info.fileName());
        setConversionState(QStringLiteral("error"), QStringLiteral("无法读取视频信息。"));
        m_thumbnailSource = QPixmap();
        ui->thumbnailLabel->setPixmap(QPixmap());
        ui->thumbnailLabel->setText(QStringLiteral("缩略图不可用"));
        ui->thumbnailLabel->setAccessibleDescription(QStringLiteral("缩略图未加载"));
        QMessageBox::warning(this, QStringLiteral("无法读取视频信息"),
                             QStringLiteral("未找到 ffprobe.exe，请先在“工具设置”中配置。"));
        return;
    }

    resetInfoPanel();
    setFileNameDisplay(info.fileName());
    const QString loading = QStringLiteral("读取中...");
    ui->containerValue->setText(loading);
    ui->resolutionValue->setText(loading);
    ui->durationValue->setText(loading);
    ui->frameRateValue->setText(loading);
    ui->codecValue->setText(loading);
    ui->sizeValue->setText(loading);

    ui->thumbnailLabel->setPixmap(QPixmap());
    ui->thumbnailLabel->setText(QStringLiteral("生成中..."));
    ui->thumbnailLabel->setToolTip(QString());
    ui->thumbnailLabel->setAccessibleDescription(QStringLiteral("缩略图生成中"));
    m_thumbnailSource = QPixmap();

    // 发起异步探测；结果通过 m_probe 的 succeeded / failed 信号回到本类。
    m_probe->probe(ffprobePath, m_currentFile);
}

// 用探测结果填充信息面板。所有字段都做“空值 / 非法值”兜底，
// 保证界面不会出现空白或 -1 这类原始数值。
void Widget::updateInfoPanel(const VideoInfo &info)
{
    setFileNameDisplay(info.fileName.isEmpty() ? QStringLiteral("未知") : info.fileName);
    ui->containerValue->setText(info.container.isEmpty() ? QStringLiteral("未知") : info.container);

    if (info.width > 0 && info.height > 0) {
        ui->resolutionValue->setText(
            QStringLiteral("%1 × %2").arg(info.width).arg(info.height));
    } else {
        ui->resolutionValue->setText(QStringLiteral("未知"));
    }

    ui->durationValue->setText(formatDurationText(info.durationSeconds));
    ui->frameRateValue->setText(formatFrameRateText(info.frameRate));
    ui->codecValue->setText(info.codec.isEmpty() ? QStringLiteral("未知") : info.codec);
    ui->sizeValue->setText(formatFileSize(info.sizeBytes));
}

// 把信息面板全部恢复为占位符“—”，用于切换文件或探测失败时。
void Widget::resetInfoPanel()
{
    const QString placeholder = QStringLiteral("—");
    setFileNameDisplay(QString());
    ui->containerValue->setText(placeholder);
    ui->resolutionValue->setText(placeholder);
    ui->durationValue->setText(placeholder);
    ui->frameRateValue->setText(placeholder);
    ui->codecValue->setText(placeholder);
    ui->sizeValue->setText(placeholder);
}

// 选择输出目录；选择成功后立即刷新完整路径预览。
void Widget::browseOutputDir()
{
    const QString startDir = ui->outputDirEdit->text().trimmed();
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择输出目录"), startDir);
    if (dir.isEmpty())
        return;

    ui->outputDirEdit->setText(QDir::toNativeSeparators(dir));
    updateOutputPathPreview();
}

// 目标格式切换。
// 若用户没有手动改过文件名（当前值仍等于自动生成值），则同步替换后缀；
// 若用户自定义过文件名，只替换其后缀、保留主干，避免覆盖用户的命名。
void Widget::onFormatChanged()
{
    const TargetFormat format = currentTargetFormat();

    if (!m_currentFile.isEmpty()) {
        const QFileInfo input(m_currentFile);
        const QString current = ui->outputNameEdit->text().trimmed();

        QString base;
        if (current.isEmpty() || current == m_autoOutputName)
            base = convertedBaseName(input.completeBaseName());
        else
            base = QFileInfo(current).completeBaseName();

        if (base.isEmpty())
            base = convertedBaseName(input.completeBaseName());

        m_autoOutputName = convertedBaseName(input.completeBaseName()) + QLatin1Char('.')
            + FormatCatalog::extension(format);

        const QSignalBlocker blocker(ui->outputNameEdit);
        ui->outputNameEdit->setText(base + QLatin1Char('.')
                                    + FormatCatalog::extension(format));
    }

    updateOutputPathPreview();
}

// 刷新“完整输出路径”与状态提示。
// 目录或文件名为空时显示占位符；目标已存在时提前告知将询问覆盖或改名。
void Widget::updateOutputPathPreview()
{
    const QString dir = ui->outputDirEdit->text().trimmed();
    const QString name = ui->outputNameEdit->text().trimmed();

    if (dir.isEmpty() || name.isEmpty()) {
        m_outputPathText.clear();
        ui->outputPathLabel->setText(QStringLiteral("—"));
        ui->outputPathLabel->setToolTip(QString());
        ui->outputStatusLabel->setText(QStringLiteral("—"));
        return;
    }

    const QString path = QDir(dir).filePath(name);
    m_outputPathText = QDir::toNativeSeparators(path);
    updateOutputPathLabel();
    ui->outputStatusLabel->setText(
        QFileInfo::exists(path)
            ? QStringLiteral("输出文件已存在，转换时会询问覆盖或自动改名。")
            : QStringLiteral("可以转换。"));
}

// 清空输出目录 / 文件名，并禁用输出分组（未导入视频前不可编辑）。
void Widget::resetOutputSettings()
{
    ui->outputDirEdit->clear();
    {
        const QSignalBlocker blocker(ui->outputNameEdit);
        ui->outputNameEdit->clear();
    }
    m_outputPathText.clear();
    ui->outputPathLabel->setText(QStringLiteral("—"));
    ui->outputPathLabel->setToolTip(QString());
    ui->outputStatusLabel->setText(QStringLiteral("—"));
    ui->outputGroup->setEnabled(false);
}

// 把下拉框索引映射回格式枚举；越界时回退 MP4，保证永远有合法值。
TargetFormat Widget::currentTargetFormat() const
{
    const int index = ui->formatCombo->currentIndex();
    const QVector<TargetFormat> formats = FormatCatalog::allFormats();
    if (index >= 0 && index < formats.size())
        return formats.at(index);
    return TargetFormat::Mp4;
}

// 开始转换前的完整前置校验：
//   1. 必须有成功的探测结果，且当前没有任务在跑；
//   2. 输出目录与文件名非空、目录确实存在；
//   3. FFmpeg 自检通过，且目标格式所需编码器齐全；
//   4. 输出路径不能与输入相同；
//   5. 目标已存在时让用户选择：覆盖（二次确认）/ 自动改名 / 取消。
// 全部通过后才组装 ConversionRequest 并交给 VideoConverter。
void Widget::startConversion()
{
    if (m_converter->isRunning() || !m_probeSucceeded)
        return;

    const QString dir = ui->outputDirEdit->text().trimmed();
    const QString name = ui->outputNameEdit->text().trimmed();
    if (dir.isEmpty() || name.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("输出设置不完整"),
                             QStringLiteral("请先选择输出目录并填写文件名。"));
        return;
    }
    if (!QFileInfo(dir).isDir()) {
        QMessageBox::warning(this, QStringLiteral("输出目录无效"),
                             QStringLiteral("输出目录不存在：%1")
                                 .arg(QDir::toNativeSeparators(dir)));
        return;
    }

    // 使用启动时自检的缓存结果，避免每次点击都重新起进程。
    const FFmpegCheckResult &check = m_locator->lastResult();
    if (!check.toolsAvailable) {
        QMessageBox::warning(this, QStringLiteral("FFmpeg 不可用"),
                             QStringLiteral("请先在“工具设置”中配置可用的 FFmpeg。"));
        return;
    }

    // 只检查“当前目标格式”需要的编码器：例如缺 mpeg4 不影响导出 MP4。
    const QStringList required = FormatCatalog::requiredEncoders(currentTargetFormat());
    QStringList missingEncoders;
    for (const QString &encoder : required) {
        if (check.missingEncoders.contains(encoder))
            missingEncoders.append(encoder);
    }
    if (!missingEncoders.isEmpty()) {
        QMessageBox::warning(
            this, QStringLiteral("编码器缺失"),
            QStringLiteral("当前 FFmpeg 缺少编码器：%1\n无法完成本次转换。")
                .arg(missingEncoders.join(QStringLiteral(", "))));
        return;
    }

    QString outputPath = QDir(dir).filePath(name);
    const QString inputPath = QDir::cleanPath(QFileInfo(m_currentFile).absoluteFilePath());
    const QString targetPath = QDir::cleanPath(QFileInfo(outputPath).absoluteFilePath());
    if (targetPath.compare(inputPath, Qt::CaseInsensitive) == 0) {
        QMessageBox::warning(this, QStringLiteral("输出路径无效"),
                             QStringLiteral("输出文件不能与输入文件相同，请修改文件名。"));
        return;
    }
    // 目标已存在：默认推荐“自动改名”，覆盖则要求二次确认（不可恢复）。
    if (QFileInfo::exists(outputPath)) {
        QMessageBox box(QMessageBox::Question, QStringLiteral("文件已存在"),
                        QStringLiteral("输出文件已存在：\n%1\n请选择处理方式。")
                            .arg(QDir::toNativeSeparators(outputPath)),
                        QMessageBox::NoButton, this);
        QPushButton *overwriteButton =
            box.addButton(QStringLiteral("覆盖"), QMessageBox::DestructiveRole);
        QPushButton *renameButton =
            box.addButton(QStringLiteral("自动改名"), QMessageBox::AcceptRole);
        box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
        box.setDefaultButton(renameButton);
        box.exec();

        if (box.clickedButton() == overwriteButton) {
            const auto answer = QMessageBox::warning(
                this, QStringLiteral("确认覆盖"),
                QStringLiteral("覆盖后原文件无法恢复，确定继续吗？"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes)
                return;
        } else if (box.clickedButton() == renameButton) {
            outputPath = makeUniqueOutputPath(outputPath);
            const QFileInfo renamed(outputPath);
            {
                const QSignalBlocker blocker(ui->outputNameEdit);
                ui->outputNameEdit->setText(renamed.fileName());
            }
            updateOutputPathPreview();
        } else {
            return;
        }
    }

    // 组装请求：输入、最终输出路径、目标格式。
    // 总时长单独传入，用于把 ffmpeg 的进度换算成百分比。
    ConversionRequest request;
    request.inputPath = m_currentFile;
    request.outputPath = outputPath;
    request.format = currentTargetFormat();

    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);
    setConversionState(QStringLiteral("working"), QStringLiteral("正在转换视频。"));
    setConvertingUi(true);
    m_converter->start(request, m_locator->ffmpegPath(), m_probeDuration);
}

// 转码进程结束。
// 成功并不等于“文件可用”：还要用 ffprobe 再解析一次输出文件，
// 确认它确实是一条可读的视频流，然后才向用户报告成功。
void Widget::onConversionFinished(bool success, const QString &message, const QString &detail)
{
    const QString outputPath =
        QDir(ui->outputDirEdit->text().trimmed())
            .filePath(ui->outputNameEdit->text().trimmed());

    if (success) {
        if (m_locator->ffprobePath().isEmpty()) {
            setConvertingUi(false);
            ui->progressBar->setRange(0, 100);
            ui->progressBar->setValue(0);
            setConversionState(QStringLiteral("error"), QStringLiteral("转换失败，请查看详情。"));
            QMessageBox::warning(
                this, QStringLiteral("输出校验失败"),
                QStringLiteral("未找到 ffprobe.exe，无法校验输出文件。"));
            updateStartButton();
            return;
        }

        m_lastOutputPath = outputPath;
        ui->cancelButton->setEnabled(false);
        setConversionState(QStringLiteral("working"), QStringLiteral("正在校验输出..."));
        // 二次校验：输出文件必须能被 ffprobe 解析出视频流。
        m_outputProbe->probe(m_locator->ffprobePath(), outputPath);
        return;
    }

    setConvertingUi(false);
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);
    setConversionState(QStringLiteral("error"), QStringLiteral("转换失败，请查看详情。"));
    QMessageBox box(QMessageBox::Warning, QStringLiteral("转换失败"), message,
                    QMessageBox::Ok, this);
    if (!detail.isEmpty())
        box.setDetailedText(detail);
    box.exec();

    updateStartButton();
    if (m_pendingClose) {
        m_pendingClose = false;
        close();
    }
}

// 输出校验通过：进度置满、状态置为 success，并提示最终文件路径。
void Widget::onOutputValidated(const VideoInfo &info)
{
    Q_UNUSED(info);

    setConvertingUi(false);
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(100);
    setConversionState(QStringLiteral("success"), QStringLiteral("转换成功。"));
    updateOutputPathPreview();
    QMessageBox::information(
        this, QStringLiteral("转换完成"),
        QStringLiteral("输出文件：\n%1").arg(QDir::toNativeSeparators(m_lastOutputPath)));

    updateStartButton();
    if (m_pendingClose) {
        m_pendingClose = false;
        close();
    }
}

// 输出校验失败：进程虽结束，但文件不可用；按失败处理并展示细节。
void Widget::onOutputValidationFailed(const QString &message, const QString &detail)
{
    setConvertingUi(false);
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);
    setConversionState(QStringLiteral("error"), QStringLiteral("转换失败，请查看详情。"));

    QString text = QStringLiteral("转换进程已结束，但输出文件无法通过校验。");
    if (!message.isEmpty())
        text += QStringLiteral("\n") + message;

    QMessageBox box(QMessageBox::Warning, QStringLiteral("输出校验失败"), text,
                    QMessageBox::Ok, this);
    if (!detail.isEmpty())
        box.setDetailedText(detail);
    box.exec();

    updateStartButton();
    if (m_pendingClose) {
        m_pendingClose = false;
        close();
    }
}

// 用户取消：恢复正常界面状态，刷新输出路径提示（半成品已被 core 层删除）。
void Widget::onConversionCancelled()
{
    setConvertingUi(false);
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);
    setConversionState(QStringLiteral("idle"), QStringLiteral("已取消。"));
    updateOutputPathPreview();
    updateStartButton();

    if (m_pendingClose) {
        m_pendingClose = false;
        close();
    }
}

// 统一切换“转换中 / 空闲”的控件可用性，避免状态分散在各处导致遗漏。
void Widget::setConvertingUi(bool converting)
{
    ui->startButton->setEnabled(!converting && m_probeSucceeded);
    ui->cancelButton->setEnabled(converting);
    ui->outputGroup->setEnabled(!converting && m_probeSucceeded);
    ui->browseButton->setEnabled(!converting);
    ui->dropArea->setEnabled(!converting);
    ui->settingsButton->setEnabled(!converting);
}

// 开始按钮的启用条件：探测成功且当前没有转换任务。
void Widget::updateStartButton()
{
    ui->startButton->setEnabled(m_probeSucceeded && !m_converter->isRunning());
}

// 为已存在的目标生成不冲突的文件名：“名称(1).后缀”、“名称(2).后缀”……
// 最多尝试 9999 次，极端情况下返回原路径由上层处理。
QString Widget::makeUniqueOutputPath(const QString &path) const
{
    const QFileInfo info(path);
    const QString dir = info.absolutePath();
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix();

    for (int index = 1; index < 10000; ++index) {
        const QString name = suffix.isEmpty()
            ? QStringLiteral("%1(%2)").arg(base).arg(index)
            : QStringLiteral("%1(%2).%3").arg(base).arg(index).arg(suffix);
        const QString candidate = QDir(dir).filePath(name);
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
    return path;
}

// 关闭窗口时的安全策略：
//   - 正在转换：询问用户；确认后设置 m_pendingClose，发起取消并暂不关闭，
//     等 cancelled / finished 回调到达后再真正 close()；
//   - 空闲：直接接受关闭。
void Widget::closeEvent(QCloseEvent *event)
{
    if (m_converter->isRunning()) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("正在转换"),
            QStringLiteral("转换尚未完成，确定取消并退出吗？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }

        m_pendingClose = true;
        setConversionState(QStringLiteral("working"), QStringLiteral("正在取消..."));
        m_converter->cancel();
        event->ignore();
        return;
    }

    event->accept();
}

// 更新底部状态文本，并通过动态属性 state 驱动 QSS 变色。
// Qt 样式表不会自动响应属性变化，因此必须 unpolish + polish 强制重新计算样式。
void Widget::setConversionState(const QString &state, const QString &text)
{
    ui->conversionStatusLabel->setText(text);
    ui->conversionStatusLabel->setProperty("state", state);
    ui->conversionStatusLabel->style()->unpolish(ui->conversionStatusLabel);
    ui->conversionStatusLabel->style()->polish(ui->conversionStatusLabel);
}

// 记录完整文件名并刷新显示（实际省略在 updateFileNameLabel 中完成）。
void Widget::setFileNameDisplay(const QString &fileName)
{
    m_fileNameText = fileName;
    updateFileNameLabel();
}

// 文件名过长时按可用宽度做中间省略，完整文本放入 ToolTip。
void Widget::updateFileNameLabel()
{
    if (m_fileNameText.isEmpty()) {
        ui->fileNameValue->setText(QStringLiteral("—"));
        ui->fileNameValue->setToolTip(QString());
        return;
    }

    const int available = ui->fileNameValue->contentsRect().width() - 4;
    const QString text = available > 0
        ? ui->fileNameValue->fontMetrics().elidedText(
              m_fileNameText, Qt::ElideMiddle, available)
        : m_fileNameText;
    ui->fileNameValue->setText(text);
    ui->fileNameValue->setToolTip(m_fileNameText);
}

// 输出路径过长时按可用宽度做中间省略，完整路径放入 ToolTip。
void Widget::updateOutputPathLabel()
{
    if (m_outputPathText.isEmpty()) {
        ui->outputPathLabel->setText(QStringLiteral("—"));
        ui->outputPathLabel->setToolTip(QString());
        return;
    }

    const int available = ui->outputPathLabel->contentsRect().width() - 4;
    const QString text = available > 0
        ? ui->outputPathLabel->fontMetrics().elidedText(
              m_outputPathText, Qt::ElideMiddle, available)
        : m_outputPathText;
    ui->outputPathLabel->setText(text);
    ui->outputPathLabel->setToolTip(m_outputPathText);
}

// 尺寸变化后延迟一帧再计算省略：
// 此时布局已完成，控件宽度才是最终值，否则会用到旧宽度导致省略不准确。
void Widget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    QTimer::singleShot(0, this, [this] {
        updateFileNameLabel();
        updateOutputPathLabel();
    });
}

// 窗口状态变化（最大化 / 还原）时切换预览区尺寸。
void Widget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange)
        applyResponsiveLayout();
    QWidget::changeEvent(event);
}

// 自适应布局：普通窗口与最大化使用两套预览高度。
// 240 / 320 为设计稿中的预览区高度；缩略图宽度按 16:9 比例与之匹配。
// 尺寸变化后需要重新缩放缩略图，故延迟到下一轮事件循环执行。
void Widget::applyResponsiveLayout()
{
    const bool fullscreen = isMaximized();
    const int previewHeight = fullscreen ? 320 : 240;
    const int thumbnailWidth = fullscreen ? 569 : 427;

    ui->dropArea->setFixedHeight(previewHeight);
    ui->thumbnailLabel->setFixedSize(thumbnailWidth, previewHeight);
    ui->previewLayout->setAlignment(ui->thumbnailLabel, Qt::AlignTop);

    QTimer::singleShot(0, this, [this] { applyThumbnailPixmap(); });
}

// 按缩略图控件当前内容区尺寸缩放原始截图并显示。
// 原图已缓存（m_thumbnailSource），窗口尺寸变化时无需重新调用 ffmpeg。
void Widget::applyThumbnailPixmap()
{
    if (m_thumbnailSource.isNull())
        return;

    const QSize target = ui->thumbnailLabel->contentsRect().size();
    QPixmap display = m_thumbnailSource;
    if (target.isValid()
        && (m_thumbnailSource.width() != target.width()
            || m_thumbnailSource.height() != target.height())) {
        display = m_thumbnailSource.scaled(target, Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation);
    }

    ui->thumbnailLabel->setText(QString());
    ui->thumbnailLabel->setPixmap(display);
    ui->thumbnailLabel->setToolTip(QString());
    ui->thumbnailLabel->setAccessibleDescription(
        QStringLiteral("已加载缩略图 %1×%2").arg(display.width()).arg(display.height()));
}
