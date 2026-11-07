# FFmpeg 本地二进制说明

本目录只存放开发阶段使用的 FFmpeg 可执行文件，**不提交到 Git**。

## 文件放置

下载 Windows x64 构建后，把两个文件放到：

```
third_party/ffmpeg/bin/ffmpeg.exe
third_party/ffmpeg/bin/ffprobe.exe
```

`bin/.gitkeep` 用于保留目录；`.gitignore` 已忽略 `bin/` 下的所有内容（除 `.gitkeep`）。

## 下载来源

任选其一，优先选择 x64 release 构建：

- gyan.dev：https://www.gyan.dev/ffmpeg/builds/
- BtbN（GitHub Releases）：https://github.com/BtbN/FFmpeg-Builds/releases

下载后建议记录实际版本号（`ffmpeg -version` 第一行），便于复现测试结果。

## 本项目需要的编码器

转换功能依赖以下编码器，启动时会检查：

- `libx264`（MP4 / MOV / MKV 视频）
- `aac`（MP4 / MOV / MKV 音频）
- `mpeg4`（AVI 视频）
- `libmp3lame`（AVI 音频）

检查命令：

```
ffmpeg -hide_banner -encoders | findstr /i "libx264 aac mpeg4 libmp3lame"
```

缺少某个编码器时，程序应给出明确提示或使用已验证的降级方案，不能静默失败。

## 许可证

- FFmpeg 默认以 LGPL 2.1+ 发布，但启用 `--enable-gpl` 的构建（本项目需要的 `libx264` 属于 GPL 组件）整体按 GPL 分发。
- 用 `ffmpeg -version` 查看构建参数，确认是否包含 `--enable-gpl`。
- 正式分发软件之前，必须确认所选构建的许可证，并评估随软件分发时的义务（许可证文本、源码获取方式等）；如不接受 GPL 约束，需要改用 LGPL 兼容的编码方案。
- 此处只做技术提醒，不构成法律意见。

## 当前开发环境

- 版本：`ffmpeg version 9.0.2-essentials_build-www.gyan.dev`
- 来源：gyan.dev essentials build（Windows x64）
- 构建参数包含 `--enable-gpl`、`--enable-libx264`、`--enable-libmp3lame`
- 已验证编码器：`libx264`、`aac`、`mpeg4`、`libmp3lame`
- 本机原始位置：`F:\ffmpeg\ffmpeg-9.0.2-essentials_build\bin`（保留不动，不要删除）
- 项目内 `third_party/ffmpeg/bin/` 保留一份副本，保证开箱即用；两份都不能提交到 Git

## 约定

- 不把 `ffmpeg.exe` / `ffprobe.exe` 提交进仓库。
- 不在源码里硬编码绝对路径；程序启动时按配置和约定顺序查找。
