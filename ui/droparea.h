// =============================================================================
// 模块：拖放 / 点击选择区域（ui/droparea.h）
// -----------------------------------------------------------------------------
// 职责：提供“把视频拖到这里，或点击选择文件”的可视区域，屏蔽底层拖放细节，
//       只向主窗口暴露两个语义化信号：fileDropped / browseRequested。
//
// 设计要点：
//   - 继承 QFrame 而非 QWidget，便于通过 QSS（QFrame#dropArea）定制虚线与圆角；
//   - 内部所有子标签设置 WA_TransparentForMouseEvents，保证点击任意位置都能
//     触发文件选择，而不会被文字标签截获；
//   - 只接受“本地文件”URL，拖入文件夹或网页链接时拒绝；
//   - 文件名过长时按中间省略显示，完整路径放在 ToolTip 中。
// =============================================================================

#ifndef DROPAREA_H
#define DROPAREA_H

#include <QFrame>
#include <QString>

class QLabel;
class QMimeData;
class QResizeEvent;

// DropArea：文件拖放区控件。
// 本身不负责读取视频，只把用户选择的路径通过信号交给主窗口。
class DropArea : public QFrame
{
    Q_OBJECT

public:
    explicit DropArea(QWidget *parent = nullptr);

    // 显示“已选择”状态：标题显示文件名，ToolTip 显示完整路径与大小。
    void setFileName(const QString &filePath);
    // 恢复“将视频拖放到这里”的空状态。
    void clearFile();

signals:
    // 用户拖入一个本地文件时发出，参数为绝对路径。
    void fileDropped(const QString &filePath);
    // 用户单击该区域时发出，表示希望打开文件选择对话框。
    void browseRequested();

protected:
    // 拖放事件：拖入时判断内容是否可接受；释放时取出本地文件路径。
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    // 左键点击视为“浏览文件”。
    void mousePressEvent(QMouseEvent *event) override;
    // 尺寸变化后重新计算标题的省略显示。
    void resizeEvent(QResizeEvent *event) override;

private:
    // 判断拖放内容是否为可接受的本地文件 URL。
    bool acceptsMimeData(const QMimeData *mimeData) const;
    // 切换到“等待拖入 / 点击选择”的空状态文案。
    void updateHint();
    // 设置标题文本，并按当前可用宽度做中间省略。
    void updateTitle(const QString &text);

    // 区域内的四个标签：图标、主标题（文件名）、副标题（提示）、支持格式。
    QLabel *m_iconLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_subtitleLabel = nullptr;
    QLabel *m_formatsLabel = nullptr;
    // 当前选中的文件完整路径；为空表示未选择。
    QString m_fileName;
    // 标题的完整文本（用于窗口尺寸变化后重新省略）。
    QString m_titleText;
};

#endif // DROPAREA_H
