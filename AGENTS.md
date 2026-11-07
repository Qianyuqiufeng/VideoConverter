# AGENTS.md

Qt 6 Widgets desktop app (C++17). Intended product: a Windows video format converter driven by FFmpeg/FFprobe. The code today is still the Qt Creator "Widget" starter template — `widget.*` is a placeholder, not real functionality.

## Build

The build source of truth is the qmake project `VideoConverter.pro`, **not CMake**. There is no `CMakeLists.txt` anywhere in the repo, despite the requirements doc naming CMake.

- Configured kit: Qt 6.8.2 MSVC2022 64-bit (`Desktop_Qt_6_8_2_MSVC2022_64bit-{Debug,Profile,Release}` in `VideoConverter.pro.user`). No other kit is set up.
- Normal path is Qt Creator: open the project, pick that kit, Build.
- `qmake` / `nmake` are not on the WSL PATH. The CLI equivalent requires a Windows MSVC x64 developer prompt with Qt's `bin` on PATH: `qmake VideoConverter.pro && nmake`.
- **New source files are not picked up automatically.** qmake does not glob: add every new `.cpp` / `.h` / `.ui` to `SOURCES` / `HEADERS` / `FORMS` in `VideoConverter.pro`, or it silently won't compile.
- `ui_widget.h` is generated from `widget.ui`; edit the `.ui`, never the generated header.

There is no CI, no test suite, no linter, and no formatter. Verification is build + run.

## Do not edit

- `build/` — Qt Creator's clangd index cache, committed by accident. The IDE regenerates it; large, binary, noisy.
- `VideoConverter.pro.user` — machine-local Qt Creator state (kit paths, run config). Hand edits break the user's IDE setup.
- `.omc/`, `.omo/` — local agent-tool state, not project files.

There is no `.gitignore` and no `.gitattributes`. Working-tree files are CRLF while the committed blobs are LF, so the working tree already shows every source file as modified. Preserve each file's existing line endings — rewriting a file to LF creates a whole-file diff.

## Product scope

`视频格式转换器项目需求文档.md` is the authoritative spec (Chinese). Read it before implementing features. What it treats as non-negotiable:

- P0: file-dialog **and** drag/drop import, FFprobe metadata display, FFmpeg thumbnail, format conversion, live progress, Qt Widgets + QSS UI aimed at non-technical users.
- FFmpeg/FFprobe run through async `QProcess`; never block the GUI thread.
- Progress comes from `ffmpeg -progress pipe:1`, percent derived from the FFprobe-reported duration.
- Paths containing Chinese characters and spaces must work: pass argv lists via `QProcess`, never build shell command strings.
- Process lifecycle must be explicit: cancel, prevent double-start, clean up on window close.
- FFmpeg binaries are **not** vendored here. Runtime discovery of `ffmpeg`/`ffprobe` has to be implemented (the doc suggests `third_party/ffmpeg/`).

The doc's `CMakeLists.txt` and `ui/` + `core/` + `resources/` sections are aspirational; that layout was never created. The real project is flat in the repo root.


## 编程原则
1. Think Before Coding
Don't assume. Don't hide confusion. Surface tradeoffs.
Before implementing:
State your assumptions explicitly. If uncertain, ask.
If multiple interpretations exist, present them - don't pick silently.
If a simpler approach exists, say so. Push back when warranted.
If something is unclear, stop. Name what's confusing. Ask.
2. Simplicity First
Minimum code that solves the problem. Nothing speculative.
No features beyond what was asked.
No abstractions for single-use code.
No "flexibility" or "configurability" that wasn't requested.
No error handling for impossible scenarios.
If you write 200 lines and it could be 50, rewrite it.
Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.
3. Surgical Changes
Touch only what you must. Clean up only your own mess.
When editing existing code:
Don't "improve" adjacent code, comments, or formatting.
Don't refactor things that aren't broken.
Match existing style, even if you'd do it differently.
If you notice unrelated dead code, mention it - don't delete it.
When your changes create orphans:
Remove imports/variables/functions that YOUR changes made unused.
Don't remove pre-existing dead code unless asked.
The test: Every changed line should trace directly to the user's request.
4. Goal-Driven Execution
Define success criteria. Loop until verified.
Transform tasks into verifiable goals:
"Add validation" → "Write tests for invalid inputs, then make them pass"
"Fix the bug" → "Write a test that reproduces it, then make it pass"
"Refactor X" → "Ensure tests pass before and after"
For multi-step tasks, state a brief plan:
```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```
Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.
---
These guidelines are working if: fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.