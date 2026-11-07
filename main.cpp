// =============================================================================
// 程序入口（main.cpp）
// -----------------------------------------------------------------------------
// 职责：
//   1. 创建 QApplication（Qt Widgets 应用对象）；
//   2. 从 Qt 资源系统加载全局样式表 resources/style.qss，统一字体、配色与控件外观；
//   3. 创建并显示主窗口 Widget，随后进入事件循环。
//
// 说明：样式表按 UTF-8 解码，确保中文界面文案与 QSS 注释不会乱码。
// =============================================================================

#include "widget.h"

#include <QApplication>
#include <QFile>
#include <QString>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 样式表已被编译进 Qt 资源（见 resources/resources.qrc），
    // 运行时通过 ":/" 前缀访问，不依赖外部文件是否存在。
    QFile styleFile(QStringLiteral(":/style.qss"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text))
        a.setStyleSheet(QString::fromUtf8(styleFile.readAll()));

    // 主窗口自身负责 FFmpeg 自检、文件导入、探测、缩略图与转换等全部业务编排。
    Widget w;
    w.show();
    return a.exec();
}
