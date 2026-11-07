// =============================================================================
// 模块：拖放 / 点击选择区域的实现（ui/droparea.cpp）
// -----------------------------------------------------------------------------
// 要点：
//   - 拖放只接受第一个 URL 且必须是本地文件；
//   - 子控件全部透明化鼠标事件，使整块区域成为可点击目标；
//   - 标题使用中间省略（ElideMiddle），保留扩展名，方便用户辨认。
// =============================================================================

#include "droparea.h"

#include "videoinfo.h"

#include <QColor>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QResizeEvent>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// 生成区域中央的视频图标。
// 优先加载资源中的 SVG；若资源缺失，则用 QPainter 现场绘制一个
// “圆角矩形 + 播放三角”的占位图标，保证界面不会出现空白。
// devicePixelRatio 设为 2.0，在高分屏上保持清晰。
QPixmap makeVideoPixmap()
{
    const QPixmap source(QStringLiteral(":/icons/video.svg"));
    if (!source.isNull()) {
        QPixmap pixmap = source.scaled(112, 112, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
        pixmap.setDevicePixelRatio(2.0);
        return pixmap;
    }

    QPixmap pixmap(112, 112);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(2.0, 2.0);
    painter.setPen(Qt::NoPen);

    painter.setBrush(QColor(QStringLiteral("#e8f0fe")));
    painter.drawRoundedRect(QRectF(4, 10, 48, 36), 8, 8);

    painter.setBrush(QColor(QStringLiteral("#2f6fed")));
    QPolygonF triangle;
    triangle << QPointF(24, 20) << QPointF(24, 36) << QPointF(38, 28);
    painter.drawPolygon(triangle);
    painter.end();

    return pixmap;
}

} // namespace

// 构造：创建四个标签并纵向居中排列。
// objectName 供 QSS 精确匹配样式；整块区域开启拖放并显示手型光标，
// 暗示“可以点击/拖入”。
DropArea::DropArea(QWidget *parent)
    : QFrame(parent)
    , m_iconLabel(new QLabel(this))
    , m_titleLabel(new QLabel(this))
    , m_subtitleLabel(new QLabel(this))
    , m_formatsLabel(new QLabel(this))
{
    setObjectName(QStringLiteral("dropArea"));
    setAcceptDrops(true);
    setFrameShape(QFrame::StyledPanel);
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(240);

    m_iconLabel->setObjectName(QStringLiteral("dropIcon"));
    m_iconLabel->setPixmap(makeVideoPixmap());
    m_iconLabel->setFixedSize(56, 56);
    m_iconLabel->setAlignment(Qt::AlignCenter);

    m_titleLabel->setObjectName(QStringLiteral("dropTitle"));
    m_titleLabel->setAlignment(Qt::AlignCenter);

    m_subtitleLabel->setObjectName(QStringLiteral("dropSubtitle"));
    m_subtitleLabel->setAlignment(Qt::AlignCenter);

    m_formatsLabel->setObjectName(QStringLiteral("dropFormats"));
    m_formatsLabel->setAlignment(Qt::AlignCenter);

    for (QLabel *label : {m_titleLabel, m_subtitleLabel, m_formatsLabel}) {
        label->setWordWrap(false);
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    m_iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(6);
    layout->addStretch();
    layout->addWidget(m_iconLabel, 0, Qt::AlignHCenter);
    layout->addSpacing(4);
    layout->addWidget(m_titleLabel);
    layout->addWidget(m_subtitleLabel);
    layout->addSpacing(6);
    layout->addWidget(m_formatsLabel);
    layout->addStretch();

    updateHint();
}

// 切换为“已选择文件”状态。
// 标题显示文件名（过长时中间省略），副标题提示可重新选择，
// ToolTip 则给出完整路径与人类可读的文件大小。
void DropArea::setFileName(const QString &filePath)
{
    m_fileName = filePath;
    if (m_fileName.isEmpty()) {
        updateHint();
        setToolTip(QString());
        return;
    }

    const QFileInfo info(m_fileName);
    updateTitle(info.fileName());
    m_subtitleLabel->setText(QStringLiteral("已选择，点击可重新选择"));
    m_formatsLabel->setText(QStringLiteral("支持 MP4、AVI、MOV、MKV、WMV"));
    m_titleLabel->setToolTip(info.fileName());
    setToolTip(QStringLiteral("%1\n%2")
                   .arg(QDir::toNativeSeparators(info.absoluteFilePath()),
                        formatFileSize(info.size())));
}

// 清除已选文件，恢复初始提示。
void DropArea::clearFile()
{
    m_fileName.clear();
    updateHint();
    setToolTip(QString());
}

// 空状态文案：主标题、副标题与支持格式提示。
void DropArea::updateHint()
{
    updateTitle(QStringLiteral("将视频拖放到这里"));
    m_titleLabel->setToolTip(QString());
    m_subtitleLabel->setText(QStringLiteral("或点击选择文件"));
    m_formatsLabel->setText(QStringLiteral("支持 MP4、AVI、MOV、MKV、WMV"));
}

// 设置标题并在可用宽度内做中间省略。
// 首次布局时控件宽度可能还是 0，此时退回用区域宽度估算。
void DropArea::updateTitle(const QString &text)
{
    m_titleText = text;

    int available = m_titleLabel->width();
    if (available <= 0)
        available = width() - 40;

    const QString display = available > 0
        ? m_titleLabel->fontMetrics().elidedText(text, Qt::ElideMiddle, available)
        : text;
    m_titleLabel->setText(display);
    m_titleLabel->setToolTip(text);
}

// 判断拖放内容是否可接受：必须是 URL 列表，且第一个 URL 是本地文件。
// 文件夹、网页链接、纯文本都会被拒绝。
bool DropArea::acceptsMimeData(const QMimeData *mimeData) const
{
    if (!mimeData || !mimeData->hasUrls())
        return false;

    const QList<QUrl> urls = mimeData->urls();
    return !urls.isEmpty() && urls.first().isLocalFile();
}

// 拖动进入：内容可接受才接受拖动动作，否则忽略（光标显示禁止）。
void DropArea::dragEnterEvent(QDragEnterEvent *event)
{
    if (acceptsMimeData(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

// 拖动离开：无条件接受即可，本控件没有额外的悬停状态需要清理
// （悬停高亮由 QSS 的 :hover 自动处理）。
void DropArea::dragLeaveEvent(QDragLeaveEvent *event)
{
    event->accept();
}

// 文件释放：再次校验内容，取出第一个本地文件路径并通过信号交给主窗口。
void DropArea::dropEvent(QDropEvent *event)
{
    if (!acceptsMimeData(event->mimeData())) {
        event->ignore();
        return;
    }

    const QString filePath = event->mimeData()->urls().first().toLocalFile();
    event->acceptProposedAction();
    if (!filePath.isEmpty())
        emit fileDropped(filePath);
}

// 左键点击整块区域等价于“选择视频”，其余按键保持基类行为。
void DropArea::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit browseRequested();
        event->accept();
        return;
    }
    QFrame::mousePressEvent(event);
}

// 尺寸变化后重新计算标题省略，避免文字被裁切。
void DropArea::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    updateTitle(m_titleText);
}
