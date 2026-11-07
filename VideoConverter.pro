# =============================================================================
# 工程文件（qmake）：VideoConverter
# -----------------------------------------------------------------------------
# 模块构成：
#   core/  —— 业务核心：工具定位、元信息探测、缩略图、格式目录、转码
#   ui/    —— 可复用控件：拖放区域、工具设置对话框
#   widget —— 主窗口，负责把 core 与 ui 组装成完整流程
#
# 注意：qmake 不会自动扫描目录，新增 .cpp/.h/.ui 必须手动登记到下方对应列表，
#       否则文件不会参与编译。
# =============================================================================
QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

# 参与编译的源文件。
SOURCES += \
    main.cpp \
    widget.cpp \
    core/ffmpeglocator.cpp \
    core/mediaprobe.cpp \
    core/thumbnailgenerator.cpp \
    core/formatcatalog.cpp \
    core/videoconverter.cpp \
    ui/settingsdialog.cpp \
    ui/droparea.cpp

# 头文件清单（qmake 据此生成依赖关系，缺失会导致增量编译不正确）。
HEADERS += \
    widget.h \
    core/ffmpeglocator.h \
    core/mediaprobe.h \
    core/videoinfo.h \
    core/thumbnailgenerator.h \
    core/formatcatalog.h \
    core/conversionrequest.h \
    core/videoconverter.h \
    ui/settingsdialog.h \
    ui/droparea.h

# Qt Designer 界面文件；uic 会生成 ui_widget.h。
FORMS += \
    widget.ui

# 让各文件可以直接 #include "ffmpeglocator.h" 等，而不必写相对路径。
INCLUDEPATH += $$PWD/core $$PWD/ui
# 编译期写入内置 FFmpeg 目录，作为 FFmpegLocator 自动搜索的第 3 个位置。
DEFINES     += FFMPEG_BIN_DIR=\\\"$$PWD/third_party/ffmpeg/bin\\\"
# Qt 资源：把样式表与图标编译进可执行文件。
RESOURCES   += resources/resources.qrc

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
