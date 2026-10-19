# Cheet

[中文](README.zh-CN.md)

A Windows tray screenshot tool built with **C++ and Qt Quick (QML)**. Capture a region, annotate it, copy to the clipboard, or send an AI inquire to ChatGPT / Gemini / Claude / 豆包 / 千问 / DeepSeek (desktop app or browser).

Settings are stored at: `%LOCALAPPDATA%\Cheet\settings.json`

## License

[MIT](LICENSE) — free and open source; commercial use, modification, and distribution are allowed.

## Features

- System tray (capture / settings / quit)
- Global capture hotkey (`RegisterHotKey`)
- Dark settings UI (capture & hotkey / AI inquire / general)
- Chinese & English UI, start with Windows
- Full-screen selection with annotations (rectangle / ellipse / arrow / pen / text / undo)
- AI inquire (ChatGPT / Gemini / Claude / 豆包 / 千问 / DeepSeek): prefer the desktop app when enabled; otherwise find or open the target browser tab → paste image & prompt → wait for upload → send

## Requirements

- Windows 10/11
- Visual Studio 2022 (MSVC)
- Qt **6.5+** (Qt Quick / Quick Controls 2)
- CMake 3.21+
- Ninja (optional)

## Build

```powershell
# Point to your Qt MSVC kit (example path — use your own install)
$env:QTDIR = "C:\Qt\6.8.3\msvc2022_64"
$env:CMAKE_PREFIX_PATH = $env:QTDIR
$env:PATH = "$env:QTDIR\bin;" + $env:PATH

# From an “x64 Native Tools” prompt or after running vcvars64:
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Or:
.\build.bat
```

Run:

```powershell
.\build\Cheet.exe
```

## Project conventions

- Language: **C++17** (`CMAKE_CXX_EXTENSIONS OFF`)
- UI: Qt 6 Quick / Widgets; logic under `src/`, QML under `qml/`
- Platform: `src/platform/` (COM / UI Automation RAII); AI send: `src/ai/`; capture: `src/capture/`
- Windows build: MSVC with `/W4 /permissive- /utf-8`, plus `UNICODE`, `NOMINMAX`, `WIN32_LEAN_AND_MEAN`
- Style: see `.clang-format` and `.editorconfig` in the repo root
