# VideoConverter 视频格式转换器

面向普通 Windows 用户的视频格式转换桌面应用：导入视频后查看信息与缩略图，选择目标格式和输出位置，一键转换并实时查看进度。

- C++17 + Qt 6 Widgets（qmake 工程，非 CMake）
- FFmpeg 负责转码，FFprobe 负责元数据
- 所有外部进程通过 `QProcess` 异步执行，界面全程不卡顿
- 路径以参数列表传递，不拼 shell 命令，中文与空格路径均可正常使用

---

## 功能特性

- **两种导入方式**：文件对话框选择，或直接把视频拖到窗口的拖放区。
- **导入校验**：检查文件是否存在、是否为普通文件、是否为空、是否可读。
- **视频信息**：容器格式、分辨率、时长、帧率、编码、文件大小。
- **缩略图**：从视频中抽取一帧显示；生成失败只影响预览，不影响转换。
- **输出设置**：目标格式、输出目录、文件名，支持实时预览完整输出路径。
- **同名文件处理**：覆盖（二次确认）、自动改名、取消；默认推荐自动改名。
- **转换进度**：解析 `ffmpeg -progress`，按 FFprobe 给出的总时长换算百分比，并显示编码速度。
- **取消与退出保护**：可随时取消；转换中关闭窗口会先询问，取消完成后才退出。
- **输出复检**：转换结束后用 FFprobe 重新解析输出文件，避免“进程成功但文件无效”。
- **FFmpeg 自检**：启动时异步检查 `ffmpeg` / `ffprobe` 版本与所需编码器，状态栏用颜色区分。

## 环境要求

| 项目 | 要求 |
|---|---|
| 操作系统 | Windows 10 / 11 x64 |
| 编译工具 | Visual Studio 2022（MSVC v143） |
| Qt | Qt 6.8.2，MSVC2022 64-bit 套件 |
| 语言标准 | C++17 |
| 构建系统 | qmake（`VideoConverter.pro`） |
| 外部依赖 | Windows x64 版 `ffmpeg.exe`、`ffprobe.exe` |

> 工程按 Windows 桌面应用编写（查找 `.exe`、使用 `QSettings` 用户配置路径），不支持 Linux / macOS 构建。

## 快速开始

### 方式一：Qt Creator（推荐）

1. 用 Qt Creator 打开 `VideoConverter.pro`。
2. 选择 **Desktop Qt 6.8.2 MSVC2022 64-bit** 套件。
3. 执行 Build，然后 Run。

### 方式二：命令行

在 **x64 本机工具命令提示符**（已配置 MSVC 环境，并把 Qt 的 `bin` 加入 `PATH`）中执行：

```bat
qmake VideoConverter.pro
nmake
```

> 新增 `.cpp` / `.h` / `.ui` 后，必须手动登记到 `VideoConverter.pro` 的 `SOURCES` / `HEADERS` / `FORMS`。qmake 不会自动扫描目录，漏登记会静默漏编译。

## 准备 FFmpeg

程序不内置 FFmpeg 二进制，需要自行准备 Windows x64 版本，任选一种方式：

**方式 A：放到项目目录（推荐，开箱即用）**

```text
third_party/ffmpeg/bin/ffmpeg.exe
third_party/ffmpeg/bin/ffprobe.exe
```

该目录已被 `.gitignore` 忽略，二进制不会进入仓库。

**方式 B：在程序内指定**

运行后点击“工具设置”，分别浏览选择 `ffmpeg.exe` 与 `ffprobe.exe`，点击“检测”确认可用，再保存。路径会持久化到用户配置。

**自动查找顺序**（未手动配置时）：

1. 已保存的配置
2. 程序可执行文件同目录
3. 程序目录下的 `ffmpeg/bin/`
4. 编译期写入的 `third_party/ffmpeg/bin`（由 `.pro` 的 `FFMPEG_BIN_DIR` 定义）
5. 系统 `PATH`

**必需编码器**（启动自检会检查，缺任意一个都会在状态栏提示）：

| 编码器 | 用途 |
|---|---|
| `libx264` | MP4 / MOV / MKV 视频 |
| `aac` | MP4 / MOV / MKV 音频 |
| `mpeg4` | AVI 视频 |
| `libmp3lame` | AVI 音频 |

下载来源与版本、许可证说明见 [`third_party/ffmpeg/README.md`](third_party/ffmpeg/README.md)。

## 使用说明

1. **导入视频**：点击“选择视频”，或把文件拖入拖放区。
   文件对话框默认过滤 `mp4 / avi / mov / mkv / wmv / m4v / flv / webm`，也可选择“所有文件”；能否处理以 FFprobe 能否识别为准。
2. **查看信息**：导入成功后自动读取并显示容器、分辨率、时长、帧率、编码与大小，同时生成缩略图。
3. **选择格式**：在“目标格式”中选择 MP4 / AVI / MKV / MOV；文件名后缀会随格式自动更新（用户自定义过的文件名只替换后缀）。
4. **设置输出**：默认输出到源视频所在目录，文件名默认为 `原名_converted.扩展名`（已以 `_converted` 结尾时不会重复追加）。
5. **开始转换**：点击“开始转换”。进度条按真实编码进度推进；总时长未知时显示不确定进度。
6. **完成或取消**：完成后弹出输出路径，并已通过 FFprobe 复检；取消会终止进程并删除未完成的输出文件。

转换期间可关闭窗口：程序会询问，确认后先取消任务、再退出，不会留下残留进程或半成品。

## 输出编码策略

| 目标格式 | 视频编码 | 音频编码 | 附加参数 |
|---|---|---|---|
| MP4 | libx264，preset medium，CRF 23 | AAC 192k | `-movflags +faststart` |
| MOV | libx264，preset medium，CRF 23 | AAC 192k | `-movflags +faststart` |
| MKV | libx264，preset medium，CRF 23 | AAC 192k | — |
| AVI | mpeg4，`-q:v 3` | libmp3lame 192k | — |

统一使用 `-map 0:v:0 -map 0:a?`：只取第一条视频流，音频存在才映射。

## 项目结构

```text
VideoConverter/
├── VideoConverter.pro          # qmake 工程：源文件清单、包含路径、资源、FFmpeg 目录
├── main.cpp                    # 程序入口：加载 QSS、创建主窗口
├── widget.h / widget.cpp       # 主窗口：业务编排、状态管理
├── widget.ui                   # 主窗口布局（由 uic 生成 ui_widget.h）
├── core/                       # 与界面无关的业务核心
│   ├── videoinfo.h             # VideoInfo 数据模型 + 显示格式化
│   ├── conversionrequest.h     # 一次转换的输入/输出/格式
│   ├── formatcatalog.h/.cpp    # 目标格式目录：显示名、扩展名、所需编码器
│   ├── ffmpeglocator.h/.cpp    # 工具定位、配置持久化、异步自检
│   ├── mediaprobe.h/.cpp       # FFprobe JSON 解析为 VideoInfo
│   ├── thumbnailgenerator.h/.cpp # 抽取单帧 PNG 缩略图
│   └── videoconverter.h/.cpp   # FFmpeg 转码、进度解析、取消
├── ui/                         # 可复用界面组件
│   ├── droparea.h/.cpp         # 拖放/点击选择区域
│   └── settingsdialog.h/.cpp   # FFmpeg 路径设置对话框
├── resources/
│   ├── resources.qrc           # 资源清单（QSS + 图标）
│   ├── style.qss               # 全局浅色主题样式
│   └── icons/video.svg         # 拖放区图标
├── third_party/ffmpeg/         # FFmpeg 放置说明；bin/ 不入库
├── docs/development_summary.md # 开发记录、决策与验证结果
├── 视频格式转换器项目需求文档.md # 需求文档（权威规格）
├── AGENTS.md                   # 面向开发助手的仓库约定
└── build/                      # Qt Creator/clangd 缓存，不手改
```

## 核心模块

| 模块 | 职责 | 关键约束 |
|---|---|---|
| `FFmpegLocator` | 保存/读取工具路径，自动查找，异步自检版本与编码器 | 三段式状态机；每段 10 秒超时；不阻塞 GUI |
| `MediaProbe` | 调用 FFprobe，把 JSON 解析为 `VideoInfo` | 15 秒超时；跳过封面图流；用请求序号丢弃过期回调 |
| `ThumbnailGenerator` | 调用 FFmpeg 抽一帧 PNG | 取时长的 10%（最多 1 秒）；`image2pipe` 不落临时文件；30 秒超时 |
| `VideoConverter` | 执行转码并解析进度 | argv 传参；`-progress pipe:1`；取消后删除半成品；stderr 仅保留末尾 64KB |
| `FormatCatalog` | 格式的唯一事实来源 | 界面下拉框、后缀、编码器列表均由同一枚举派生 |
| `VideoInfo` | 跨模块传递的元信息 | 未知值用 `-1` / 空串，显示层兜底 |
| `Widget` | 组装服务与界面，驱动完整流程 | 所有耗时操作异步；转换前完整校验；转换后输出复检 |
| `DropArea` | 拖放与点击选文件 | 只接受本地文件 URL；子控件鼠标事件穿透 |
| `SettingsDialog` | 工具路径设置与检测 | 私有检测器，不污染主窗口状态 |

## 运行时流程

```text
启动
  └─ 读取配置 → 异步自检 FFmpeg/FFprobe/编码器 → 状态栏显示结果

导入视频
  └─ 校验文件 → 异步 FFprobe → 信息面板 + 异步生成缩略图

开始转换
  └─ 校验探测结果/输出目录/编码器/输入输出不同/同名策略
     → QProcess 启动 FFmpeg（-progress pipe:1）
     → 进度信号更新界面（可取消）

转换结束
  └─ 成功：异步 FFprobe 复检输出 → 报告成功
     失败：显示错误与原始细节
     取消：删除半成品 → 恢复空闲状态
```

所有子进程的 stdout / stderr 都会被读取，避免管道写满导致进程卡死。

## 配置与数据

- 配置文件：`%APPDATA%\VideoConverter\VideoConverter.ini`（`QSettings::IniFormat`，用户作用域）。
- 配置项：`tools/ffmpegPath`、`tools/ffprobePath`。
- 不写程序目录；不收集任何用户数据；转换输出写入用户选择的目录。

## 开发约定

- **构建以 `.pro` 为准**，仓库内没有 `CMakeLists.txt`；需求文档中的 CMake 章节属于早期设想。
- **新增源文件必须登记**到 `VideoConverter.pro`。
- `widget.ui` 是界面定义源，`ui_widget.h` 由 uic 生成，**不要手改生成文件**。
- 不要编辑 `build/`（IDE 缓存）与 `VideoConverter.pro.user`（机器本地套件状态）。
- 行尾约定：`main.cpp`、`widget.*`、`VideoConverter.pro` 使用 CRLF；`core/`、`ui/`、`style.qss` 使用 LF。修改时保持原样，避免整文件 diff。
- QSS 通过 `objectName` / 动态属性 `state` 匹配控件；重命名控件对象名会同时破坏样式与代码引用。
- 仓库没有 CI、单元测试与代码格式化工具，验证方式为构建 + 手动测试。

## 已知限制

- 仅支持 Windows x64。
- 尚未实现：批量转换、任务队列、历史记录、分辨率/质量设置、深色模式、打开输出目录。
- 缩略图按 400px 宽生成，全屏预览为等比平滑放大，极端要求下清晰度有限。
- SVG 图标依赖 Qt 的 SVG 图片插件；发布时若未随包提供，程序会回退到手绘图标。
- FFmpeg 二进制不随仓库分发，换机器需重新放置或配置路径。
- 启用 GPL 组件（libx264）的 FFmpeg 构建整体按 GPL 分发，正式发布前需确认许可证义务。

## 相关文档

- [`视频格式转换器项目需求文档.md`](视频格式转换器项目需求文档.md)：产品需求与 P0 范围（权威规格）。
- [`docs/development_summary.md`](docs/development_summary.md)：架构决策、实现记录、实测结果与后续建议。
- [`third_party/ffmpeg/README.md`](third_party/ffmpeg/README.md)：FFmpeg 放置、下载来源、编码器与许可证。
- [`resources/icons/README.md`](resources/icons/README.md)：图标来源与配色调整。
- [`AGENTS.md`](AGENTS.md)：构建、目录与编辑约束。

## 许可证

本仓库尚未添加开源许可证文件。随附或调用的 FFmpeg 构建可能有独立的许可证要求（本项目使用到的 `libx264` 属于 GPL 组件），分发前请阅读 `third_party/ffmpeg/README.md` 并自行确认合规义务；该说明不构成法律意见。
