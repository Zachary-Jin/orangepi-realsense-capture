<div align="center">

# Orange Pi RealSense Capture

**香橙派上的 RealSense 图像采集与实时 Web 预览**

Pure C · librealsense2 · RGB & Depth · MJPEG

[中文说明](#中文说明) · [English](#english) · [Windows 打开预览](#在电脑上打开浏览器)

</div>

---

## 中文说明

使用 **librealsense2 C API**，在香橙派上采集 RealSense 图像。项目提供两个独立程序：

| 程序 | 用途 | 默认配置 | 输出 |
| --- | --- | --- | --- |
| `realsense_capture` | 单张、批量或持续采集彩色图片 | 640×480 · 30 FPS | PPM 图片 |
| `realsense_web` | 实时彩色与深度预览 | 640×480 · 目标采集 60 FPS | Web 页面与 MJPEG 流 |

### 目录

- [环境要求](#环境要求)
- [快速开始](#快速开始)
- [在电脑上打开浏览器](#在电脑上打开浏览器)
- [Web 预览与状态](#web-预览与状态)
- [命令行参数](#命令行参数)
- [采集与导出示例](#采集与导出示例)
- [项目结构](#项目结构)
- [常见问题](#常见问题)

### 环境要求

**香橙派 / 采集端：**

| 依赖 | 说明 |
| --- | --- |
| Linux | 已在 ARM64 Ubuntu 20.04 上验证 |
| Intel RealSense 相机 | 已在 D435i 上验证 |
| librealsense2 | SDK 库与开发文件，且 `pkg-config` 能找到 `realsense2` |
| C 编译工具 | 支持 C11 的编译器、Make、pkg-config |
| FFmpeg | Web 实时预览需要，`ffmpeg` 命令须在 `PATH` 中 |

**电脑 / 浏览器端：** Windows、macOS 或 Linux，与香橙派处于同一局域网，并可访问服务端口。

> 下文的编译和采集命令在香橙派上执行；打开浏览器、复制图片的命令在自己的电脑上执行。

### 快速开始

**1. 在香橙派上获取并编译项目**

```sh
git clone https://github.com/Zachary-Jin/orangepi-realsense-capture.git
cd orangepi-realsense-capture
make
```

**2. 选择一种运行方式**

保存一张彩色图片：

```sh
./realsense_capture
```

默认先丢弃 30 帧，让自动曝光趋于稳定，再将图片保存为当前目录的 `capture.ppm`。

启动实时彩色与深度预览：

```sh
./realsense_web
```

**3. 在电脑浏览器中访问** `http://<香橙派IP>:8080/`。下文以 `192.168.1.58` 为例，请替换成香橙派的实际 IP。按 **Ctrl+C** 停止服务。

### 在电脑上打开浏览器

先启动香橙派上的 `realsense_web`，再在**自己电脑的本地终端**中执行对应命令，打开默认浏览器。如果正在通过 SSH 控制香橙派，请另开一个本地终端。

**Windows · PowerShell（包括 Windows Terminal 的 PowerShell 标签页）**

```powershell
Start-Process "http://192.168.1.58:8080/"
```

**Windows · CMD（命令提示符）**

```bat
start "" "http://192.168.1.58:8080/"
```

CMD 中的第一个 `""` 是空窗口标题，请保留。Windows Terminal 中应根据当前标签页使用的 PowerShell 或 CMD 选择命令。

**macOS · 终端**

```sh
open "http://192.168.1.58:8080/"
```

**Linux · 桌面终端（需安装 xdg-utils）**

```sh
xdg-open "http://192.168.1.58:8080/"
```

以上 IP 仅为示例；若用 `-p` 改了端口，也要同步修改 URL。也可以直接把地址粘贴到浏览器地址栏。

### Web 预览与状态

彩色流采用 **RGB8**，深度流采用 **Z16**。采集线程与 MJPEG 编码线程独立运行，编码较慢时会丢弃旧帧，避免反向阻塞采集。

- **采集目标：** 两路均为 640×480、60 FPS；实际采集帧率以页面和状态接口为准。
- **预览编码：** MJPEG 编码配置为 30 FPS，浏览器预览速度不等于相机采集速度。
- **深度显示：** 将 0–20 米映射为近红远蓝的伪彩色。20 米是显示映射上限，实际有效测量距离受光照、目标反射率等条件影响。

修改监听端口：

```sh
./realsense_web -p 8090
```

在香橙派上查看服务状态与实际采集帧率：

```sh
curl http://127.0.0.1:8080/api/status
```

从 Windows PowerShell 查询香橙派状态：

```powershell
Invoke-RestMethod "http://192.168.1.58:8080/api/status"
```

| 路径 | 内容 |
| --- | --- |
| `/` | 实时预览页面 |
| `/stream/color.mjpg` | 彩色 MJPEG 流 |
| `/stream/depth.mjpg` | 伪彩色深度 MJPEG 流 |
| `/api/status` | JSON 状态与帧率信息 |

> Web 服务监听所有网络接口，不包含身份验证，仅应在可信局域网中使用。同一相机不能被两个采集程序同时占用，同一端口也不能被两个服务实例同时占用。

### 命令行参数

**彩色图片采集**

```text
./realsense_capture [-o FILE] [-n COUNT] [-r WIDTHxHEIGHT] [-f FPS] [-s SKIP]
```

| 参数 | 含义 | 默认值 |
| --- | --- | --- |
| `-o FILE` | 输出文件名或文件名前缀 | `capture.ppm` |
| `-n COUNT` | 保存帧数；`0` 表示持续采集，直到 Ctrl+C | `1` |
| `-r WIDTHxHEIGHT` | 彩色图像分辨率 | `640x480` |
| `-f FPS` | 请求的采集帧率 | `30` |
| `-s SKIP` | 正式保存前丢弃的预热帧数 | `30` |
| `--help` | 显示帮助 | — |

**Web 实时预览**

```text
./realsense_web [-p PORT]
```

| 参数 | 含义 | 默认值 |
| --- | --- | --- |
| `-p PORT` | HTTP 监听端口 | `8080` |
| `--help` | 显示帮助 | — |

### 采集与导出示例

在香橙派上执行：

```sh
# 保存一张 1280×720 图片
./realsense_capture -r 1280x720 -o room.ppm

# 保存十帧：shot_000001.ppm 到 shot_000010.ppm
./realsense_capture -n 10 -o shot.ppm

# 持续采集，按 Ctrl+C 停止
./realsense_capture -n 0 -o stream.ppm
```

在电脑本地终端中将图片复制到当前目录（需要本机已安装 `scp`，请替换用户名、IP 和远程路径）：

```sh
scp YOUR_USER@192.168.1.58:~/orangepi-realsense-capture/capture.ppm .
```

支持 PPM 的图像查看器可直接打开图片。若本机安装了 ImageMagick，可转换为 JPEG：

```sh
magick capture.ppm capture.jpg
```

### 项目结构

```text
orangepi-realsense-capture/
├── realsense_capture.c   # 彩色图片采集
├── realsense_web.c       # 彩色 / 深度采集与 Web 服务
├── Makefile              # 编译两个程序
└── README.md             # 使用说明
```

### 常见问题

| 现象 | 检查方向 |
| --- | --- |
| 编译时找不到 `realsense2` | 确认 SDK 与开发文件已安装，运行 `pkg-config --modversion realsense2` 检查配置 |
| Web 预览无法启动或没有画面 | 确认 `ffmpeg -version` 可运行，并查看香橙派终端的错误输出 |
| 电脑无法访问页面 | 确认服务仍在运行、IP 和端口正确、电脑可访问香橙派，且防火墙允许该端口 |
| 相机或端口被占用 | 关闭占用相机的其他程序；端口冲突可用 `-p` 换端口 |
| 实际帧率低于 60 FPS | 查看 `/api/status`，检查相机支持的模式、USB 连接与系统负载；60 FPS 是采集目标 |

---

## English

Capture RealSense images on an Orange Pi using the **librealsense2 C API**. Two independent programs support saving color images and viewing live color and depth streams in a browser.

| Program | Purpose | Default configuration | Output |
| --- | --- | --- | --- |
| `realsense_capture` | Single, batch, or continuous color capture | 640×480 · 30 FPS | PPM images |
| `realsense_web` | Live color and depth preview | 640×480 · target capture rate of 60 FPS | Web page and MJPEG streams |

### Contents

- [Requirements](#requirements)
- [Quick start](#quick-start)
- [Open a browser on your computer](#open-a-browser-on-your-computer)
- [Web preview and status](#web-preview-and-status)
- [Command-line options](#command-line-options)
- [Capture and export examples](#capture-and-export-examples)
- [Project structure](#project-structure)
- [Troubleshooting](#troubleshooting)

### Requirements

**Orange Pi / capture host:**

| Dependency | Details |
| --- | --- |
| Linux | Tested on ARM64 Ubuntu 20.04 |
| Intel RealSense camera | Tested with a D435i |
| librealsense2 | SDK libraries and development files; `pkg-config` must find `realsense2` |
| C build tools | C11 compiler, Make, and pkg-config |
| FFmpeg | Required for the web preview; `ffmpeg` must be available in `PATH` |

**Computer / browser client:** Windows, macOS, or Linux on the same LAN, with access to the server port.

> Run build and capture commands on the Orange Pi. Run browser and file-copy commands on your own computer.

### Quick start

**1. Clone and build on the Orange Pi**

```sh
git clone https://github.com/Zachary-Jin/orangepi-realsense-capture.git
cd orangepi-realsense-capture
make
```

**2. Choose a capture mode**

Save one color image:

```sh
./realsense_capture
```

The program discards 30 warm-up frames to allow automatic exposure to settle, then saves `capture.ppm` in the current directory.

Start the live color and depth preview:

```sh
./realsense_web
```

**3. Open** `http://<orange-pi-ip>:8080/` in your computer's browser. The examples below use `192.168.1.58`; replace it with your Orange Pi's actual IP address. Press **Ctrl+C** to stop the server.

### Open a browser on your computer

Start `realsense_web` on the Orange Pi first. Then run the appropriate command in a **local terminal on your computer** to open the default browser. If you are connected to the Orange Pi over SSH, open a separate local terminal.

**Windows · PowerShell (including a PowerShell tab in Windows Terminal)**

```powershell
Start-Process "http://192.168.1.58:8080/"
```

**Windows · Command Prompt (CMD)**

```bat
start "" "http://192.168.1.58:8080/"
```

Keep the first `""` in the CMD command: it supplies an empty window title. In Windows Terminal, choose the command for the shell running in your tab.

**macOS · Terminal**

```sh
open "http://192.168.1.58:8080/"
```

**Linux · Desktop terminal (requires xdg-utils)**

```sh
xdg-open "http://192.168.1.58:8080/"
```

Replace the example IP address and update the port if you use `-p`. You can also paste the URL directly into your browser's address bar.

### Web preview and status

The camera provides **RGB8** color and **Z16** depth streams. Capture and MJPEG encoding run in separate threads; a slow encoder drops stale frames instead of blocking camera capture.

- **Capture target:** both streams at 640×480 and 60 FPS. Check the page or status API for the measured capture rate.
- **Preview encoding:** MJPEG is configured for 30 FPS. Browser preview speed differs from camera capture speed.
- **Depth display:** maps 0–20 meters from red (near) to blue (far). This is a visualization limit; usable measurement range depends on lighting, target reflectivity, and other conditions.

Select a different port:

```sh
./realsense_web -p 8090
```

Inspect status and measured capture rate on the Orange Pi:

```sh
curl http://127.0.0.1:8080/api/status
```

Query the Orange Pi from Windows PowerShell:

```powershell
Invoke-RestMethod "http://192.168.1.58:8080/api/status"
```

| Path | Content |
| --- | --- |
| `/` | Live preview page |
| `/stream/color.mjpg` | Color MJPEG stream |
| `/stream/depth.mjpg` | Colorized depth MJPEG stream |
| `/api/status` | JSON status and frame-rate information |

> The server listens on all network interfaces and has no authentication. Use it only on a trusted LAN. Two capture programs cannot use the same camera simultaneously, and two server instances cannot share the same listening port.

### Command-line options

**Color image capture**

```text
./realsense_capture [-o FILE] [-n COUNT] [-r WIDTHxHEIGHT] [-f FPS] [-s SKIP]
```

| Option | Description | Default |
| --- | --- | --- |
| `-o FILE` | Output file or filename prefix | `capture.ppm` |
| `-n COUNT` | Frames to save; `0` runs until Ctrl+C | `1` |
| `-r WIDTHxHEIGHT` | Color resolution | `640x480` |
| `-f FPS` | Requested capture rate | `30` |
| `-s SKIP` | Warm-up frames to discard | `30` |
| `--help` | Show help | — |

**Live web preview**

```text
./realsense_web [-p PORT]
```

| Option | Description | Default |
| --- | --- | --- |
| `-p PORT` | HTTP listening port | `8080` |
| `--help` | Show help | — |

### Capture and export examples

Run on the Orange Pi:

```sh
# Save a single 1280×720 image.
./realsense_capture -r 1280x720 -o room.ppm

# Save ten frames as shot_000001.ppm through shot_000010.ppm.
./realsense_capture -n 10 -o shot.ppm

# Capture continuously until Ctrl+C.
./realsense_capture -n 0 -o stream.ppm
```

Copy an image into the current directory from your computer's local terminal. A local `scp` installation is required; replace the username, IP address, and remote path:

```sh
scp YOUR_USER@192.168.1.58:~/orangepi-realsense-capture/capture.ppm .
```

Open the image in a PPM-compatible viewer, or convert it to JPEG with ImageMagick installed locally:

```sh
magick capture.ppm capture.jpg
```

### Project structure

```text
orangepi-realsense-capture/
├── realsense_capture.c   # Color image capture
├── realsense_web.c       # Color / depth capture and web server
├── Makefile              # Build both programs
└── README.md             # Usage documentation
```

### Troubleshooting

| Symptom | What to check |
| --- | --- |
| Build cannot find `realsense2` | Confirm SDK and development files are installed; check `pkg-config --modversion realsense2` |
| Web preview fails to start or shows no frames | Check `ffmpeg -version` and inspect terminal errors on the Orange Pi |
| Computer cannot reach the page | Check the running server, IP address, port, network connectivity, and firewall rules |
| Camera or port is busy | Close other camera applications; use `-p` to change a conflicting port |
| Measured capture rate is below 60 FPS | Check `/api/status`, supported camera modes, USB connection, and system load; 60 FPS is the capture target |
