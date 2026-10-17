# Cheet

[English](README.md)

Windows 托盘截图工具，基于 **C++ + Qt Quick (QML)**。支持框选标注、复制到剪贴板，以及一键问询 ChatGPT / Gemini / Claude（桌面客户端或浏览器）。

设置保存在：`%LOCALAPPDATA%\Cheet\settings.json`

## 许可证

[MIT](LICENSE) — 免费开源，允许他人商用、修改与再分发。

## 功能

- 系统托盘（截图 / 设置 / 退出）
- 全局热键截图（RegisterHotKey）
- 深色设置窗口（截图与快捷键 / AI 问询 / 常规）
- 中英文界面、开机自启
- 全屏框选 + 标注（矩形 / 椭圆 / 箭头 / 画笔 / 文字 / 撤销）
- AI 问询：优先桌面客户端，否则在浏览器中定位目标标签页或新开页面 → 粘贴图片与提示词 → 监测上传完成后发送

## 环境

- Windows 10/11
- Visual Studio 2022（MSVC）
- Qt **6.5+**（需含 Qt Quick / Quick Controls 2）
- CMake 3.21+
- Ninja（可选）

## 构建

```powershell
# 指向本机 Qt MSVC 套件（示例路径，请改成你的安装位置）
$env:QTDIR = "C:\Qt\6.8.3\msvc2022_64"
$env:CMAKE_PREFIX_PATH = $env:QTDIR
$env:PATH = "$env:QTDIR\bin;" + $env:PATH

# 在 “x64 Native Tools” 或已执行 vcvars64 的终端中：
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 或：
.\build.bat
```

运行：

```powershell
.\build\Cheet.exe
```

## 工程约定

- 语言标准：**C++17**（`CMAKE_CXX_EXTENSIONS OFF`）
- UI：Qt 6 Quick / Widgets；业务逻辑在 `src/`，界面在 `qml/`
- 平台层：`src/platform/`（COM / UI Automation RAII），AI 发送：`src/ai/`，截图：`src/capture/`
- Windows 构建：MSVC，开启 `/W4 /permissive- /utf-8`，并定义 `UNICODE`、`NOMINMAX`、`WIN32_LEAN_AND_MEAN`
- 代码风格：见仓库根目录 `.clang-format` / `.editorconfig`
