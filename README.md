# ⏰ 时间窗 TimeWindow

**Windows 悬浮时钟 · Floating Clock for Windows**

<p align="center">
  <a href="https://github.com/Yichenlemon">
    <img src="https://github.com/Yichenlemon.png" width="90" style="border-radius:50%;" alt="亦辰曦"/>
  </a>
</p>

<p align="center">
  <a href="https://github.com/Yichenlemon/timewindow/releases"><img alt="Release" src="https://img.shields.io/github/v/release/Yichenlemon/timewindow?label=Release&color=136FF0&style=flat-square"/></a>
  <a href="https://github.com/Yichenlemon/timewindow/releases"><img alt="Downloads" src="https://img.shields.io/github/downloads/Yichenlemon/timewindow/total?label=Downloads&color=52B7FF&style=flat-square"/></a>
  <a href="https://github.com/Yichenlemon/timewindow/stargazers"><img alt="Stars" src="https://img.shields.io/github/stars/Yichenlemon/timewindow?label=Stars&style=flat-square"/></a>
  <a href="https://github.com/Yichenlemon/timewindow/network"><img alt="Forks" src="https://img.shields.io/github/forks/Yichenlemon/timewindow?label=Forks&style=flat-square"/></a>
  <a href="https://github.com/Yichenlemon/timewindow/watchers"><img alt="Watchers" src="https://img.shields.io/github/watchers/Yichenlemon/timewindow?label=Watchers&style=flat-square"/></a>
</p>
<p align="center">
  <a href="https://github.com/Yichenlemon/timewindow/issues"><img alt="Issues" src="https://img.shields.io/github/issues/Yichenlemon/timewindow?label=Issues&color=ff6b6b&style=flat-square"/></a>
  <a href="https://github.com/Yichenlemon/timewindow/pulls"><img alt="PRs" src="https://img.shields.io/github/issues-pr/Yichenlemon/timewindow?label=PRs&color=7c5cff&style=flat-square"/></a>
  <a href="https://github.com/Yichenlemon/timewindow"><img alt="Last commit" src="https://img.shields.io/github/last-commit/Yichenlemon/timewindow?style=flat-square&color=22c55e"/></a>
  <a href="https://github.com/Yichenlemon/timewindow"><img alt="Repo size" src="https://img.shields.io/github/repo-size/Yichenlemon/timewindow?label=Size&color=22c55e&style=flat-square"/></a>
  <a href="#"><img alt="C / Win32" src="https://img.shields.io/badge/C%20%2F%20Win32-136FF0?style=flat-square"/></a>
  <a href="#"><img alt="Platform" src="https://img.shields.io/badge/Platform-Windows-0aa2c0?style=flat-square"/></a>
  <a href="#"><img alt="Build" src="https://img.shields.io/badge/Build-passing-22c55e?style=flat-square&logo=github"/></a>
</p>

<p align="center">
  <a href="#中文">中文目录</a> · <a href="#english">English TOC</a> · <a href="#下载与安装">下载 Releases</a>
</p>

---

## 中文

一个可悬浮在任意窗口上的实时时钟工具，支持 18 种时间格式（含农历、天干地支、生肖）、自定义格式、颜色与透明度主题，一键常驻系统托盘。

### 目录
- [功能特性](#功能特性)
- [技术栈](#技术栈)
- [18 种时间格式](#18-种时间格式)
- [下载与安装](#下载与安装)
- [界面截图](#界面截图)
- [使用说明](#使用说明)
- [界面与主题](#界面与主题)
- [从源码编译](#从源码编译)
- [关于](#关于)

### 功能特性
- 🕘 **悬浮时钟**：透明无边框小窗，可拖动、可锁定，始终置顶显示当前时间
- 📅 **18 种时间格式**：还原原版全部预设，含农历日期、天干地支、十二生肖
- ⚙️ **自定义格式**：提供预设选择器 + 完整格式说明，支持手动输入高级格式
- 🎨 **主题定制**：主界面背景、分区标题背景均可自定义颜色；悬浮窗支持透明度调节
- 🖱️ **平滑滚动**：设置界面双缓冲渲染，滚轮平滑滚动且遵循 Windows 系统滚动行数设置
- 🌈 **统一图标**：托盘、标题栏、任务栏、文件图标采用一致的线性渐变蓝钟表
- 🪟 **单文件交付**：单 EXE，无控制台窗口，`-mwindows` 编译；内含图标与 UTF-8 中文资源信息

### 技术栈
- 🅲 **语言**：C（原生 Win32 API + 自绘界面）
- 🎨 **图形**：GDI+（抗锯齿、渐变、透明悬浮窗）
- 🎛️ **控件**：Common Controls v6（Visual Styles / 应用清单）
- 🔨 **构建**：MinGW-w64 + GCC + windres
- 🪟 **交付**：单文件 EXE，`-mwindows`，无运行时依赖；兼容 Windows 7/10/11

### 界面截图
两张图左右并排，下方为图注。

<table>
  <tr>
    <td align="center"><img src="screenshots/settings.png" width="330" alt="主设置界面"/></td>
    <td align="center"><img src="screenshots/float.gif" width="330" alt="悬浮时钟"/></td>
  </tr>
  <tr>
    <td align="center"><em>主设置界面 · 完整配置项</em></td>
    <td align="center"><em>悬浮时钟 · 实时显示（GIF）</em></td>
  </tr>
</table>

### 18 种时间格式
还原自原版 APP 的全部预先格式，覆盖年份、月份、星期、时分秒，并完整保留：
- **农历**（初几、几月）
- **天干地支**（年份干支）
- **十二生肖**
- **毫秒等级别精度**（HH:mm:ss:S / :SS / :SSS）

### 下载与安装
前往 [Releases](https://github.com/Yichenlemon/timewindow/releases) 下载最新版的 `timewindow.exe`，直接运行即可，无需安装任何运行库。

### 使用说明
- 拖动悬浮窗边缘可移动；右键悬浮窗或托盘图标可呼出菜单
- 托盘菜单支持：显示设置 / 显示悬浮窗 / 隐藏悬浮窗 / 退出
- 设置界面可切换时间格式、自定义格式、主题颜色与透明度

### 界面与主题
- 背景色：可通过「设置 → 主界面背景色」选择
- 分区标题背景：可通过「设置 → 分区标题背景色」选择
- 悬浮窗透明度：通过「设置 → 背景不透明度」调节（0–255）

### 从源码编译
需要 **MinGW-w64 + GDI+（`libgdiplus`）**：

```bash
g++ timewindow.c timewindow.res -o timewindow.exe -mwindows \
    -lgdiplus -lshlwapi -lole32 -luxtheme -lcomctl32 \
    -lwinmm -luser32 -lgdi32 -lshell32 -lcomdlg32
```

其中 `timewindow.res` 由 `windres timewindow.rc -O coff -o timewindow.res` 生成（内含应用 manifest、图标与版本信息）。图标源文件为 `app.ico`，可用 `icon_gen.py`（需 Pillow）重新生成。

### 关于
- © 亦辰曦
- 本项目仅供学习与交流使用。

---

## English

A lightweight **floating clock** for Windows, fully faithful to the Android app `com.likpia.timewindow`, rebuilt as a **single self-contained EXE** with native Win32/GDI+. No Electron, no runtime dependency — the binary is only ~290 KB (far under the 5 MB limit).

<div id="english"></div>

### TOC
- [Features](#features)
- [Tech Stack](#tech-stack)
- [Screenshots](#screenshots)
- [18 Time Formats](#18-time-formats)
- [Download & Install](#download--install)
- [Usage](#usage)
- [UI & Theme](#ui--theme)
- [Build from Source](#build-from-source)
- [About](#about)

### Features
- 🕘 **Floating clock**: transparent, borderless, draggable, always-on-top live time
- 📅 **18 time formats**: all original presets including **Lunar calendar, Heavenly Stems / Earthly Branches, and the Chinese zodiac**
- ⚙️ **Custom format**: preset picker + full format documentation, plus manual entry
- 🎨 **Theme**: customizable background colors and floating-window opacity
- 🖱️ **Smooth scrolling**: double-buffered settings UI, wheel speed follows system setting
- 🌈 **Consistent icon**: tray / title bar / taskbar / file icon share one gradient-blue clock
- 🪟 **Single file**: no console window (`-mwindows`), icon + UTF-8 Chinese version info embedded

### Tech Stack
- 🅲 **Language**: C (native Win32 API + custom-drawn UI)
- 🎨 **Graphics**: GDI+ (anti-aliasing, gradients, transparent floating window)
- 🎛️ **Controls**: Common Controls v6 (Visual Styles / app manifest)
- 🔨 **Build**: MinGW-w64 + GCC + windres
- 🪟 **Delivery**: single-file EXE, `-mwindows`, no runtime dependency; Windows 7/10/11

### Screenshots
Two images side by side, with captions below.

<table>
  <tr>
    <td align="center"><img src="screenshots/settings.png" width="330" alt="Settings window"/></td>
    <td align="center"><img src="screenshots/float.gif" width="330" alt="Floating clock"/></td>
  </tr>
  <tr>
    <td align="center"><em>Settings window · full configuration</em></td>
    <td align="center"><em>Floating clock · live display (GIF)</em></td>
  </tr>
</table>

### 18 Time Formats
All presets restored from the original app, covering year / month / weekday / time, including:
- **Lunar calendar** dates
- **Heavenly Stems & Earthly Branches** (year)
- **Chinese zodiac**
- **Millisecond precision** (`HH:mm:ss:S` / `:SS` / `:SSS`)

### Download & Install
Grab the latest `timewindow.exe` from the [Releases](https://github.com/Yichenlemon/timewindow/releases) page and run it — no extra runtime required.

### Usage
- Drag the floating window to move; right-click it or the tray icon for the menu
- Tray menu: Show Settings / Show Floating / Hide Floating / Quit
- Settings let you switch formats, custom format, theme colors and opacity

### UI & Theme
- Background color: via “Settings → Main background color”
- Section title background: via “Settings → Section title background color”
- Floating-window opacity: via “Settings → Background opacity” (0–255)

### Build from Source
Requires **MinGW-w64 + GDI+ (`libgdiplus`)**:

```bash
g++ timewindow.c timewindow.res -o timewindow.exe -mwindows \
    -lgdiplus -lshlwapi -lole32 -luxtheme -lcomctl32 \
    -lwinmm -luser32 -lgdi32 -lshell32 -lcomdlg32
```

`timewindow.res` comes from `windres timewindow.rc -O coff -o timewindow.res` (embeds the manifest, icon and version info). The icon source is `app.ico`; regenerate it with `icon_gen.py` (requires Pillow) if needed.

### About
- © 亦辰曦
- Made for learning and sharing.

---

## 项目数据与快速上手

### 仓库统计
<img src="https://github-readme-stats.vercel.app/api/card?repo=Yichenlemon/timewindow" width="420" alt="Repo stats"/>

### Star 历史
<img src="https://starchart.cc/Yichenlemon/timewindow.svg" width="420" alt="Star history"/>

### 快速上手
| 操作 | 方法 |
| --- | --- |
| 拖动悬浮窗 | 按住悬浮窗拖动（未锁定状态下） |
| 呼出菜单 | 右键悬浮窗 或 托盘图标 |
| 打开设置 | 托盘菜单「显示设置」 |
| 关闭悬浮窗 | 托盘菜单「隐藏悬浮窗」 |
| 退出程序 | 托盘菜单「退出」 |

> ⚠️ 本项目为个人学习项目，若用于商业或分发请自行评估版权与许可风险。
