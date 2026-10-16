# Orange Pi RealSense C Capture / 香橙派 RealSense C 图像采集

[中文](#中文说明) | [English](#english)

## 中文说明

本项目包含两个使用 librealsense2 C API 的纯 C 程序：`realsense_capture`
将彩色帧保存为 PPM 图片，`realsense_web` 同时采集彩色与深度画面并提供实时
Web 页面。

### 环境要求

- Linux（已在 ARM64 Ubuntu 20.04 上验证）
- Intel RealSense 相机（已在 D435i 上验证）
- librealsense2 及其开发文件
- 支持 C11 的 C 编译器、Make 和 pkg-config
- FFmpeg（Web 实时画面需要）

### 编译并抓取一张图片

```sh
make
./realsense_capture
```

默认以 640x480、30 FPS 启动彩色相机，并将图片保存为 `capture.ppm`。程序会先
丢弃 30 帧，让相机的自动曝光趋于稳定。

### Web 实时彩色与深度画面

`realsense_web` 同时采集 640x480、60 Hz 的 RGB8 彩色流与 Z16 深度流，并通过
内置 Web 服务显示。采集线程与 MJPEG 编码线程相互独立，编码较慢时会丢弃旧帧，
不会反向阻塞相机采集。

深度图将 0–20 米映射为近红远蓝的伪彩色画面。20 米是显示映射上限，不代表
D435i 在该距离仍能提供可靠测量；实际有效距离受光照、目标反射率等条件影响。

运行前需要确保系统中已安装 FFmpeg，然后执行：

```sh
make
./realsense_web
```

在同一局域网的浏览器中打开 `http://<香橙派IP>:8080/`。可以用 `-p` 修改端口：

```sh
./realsense_web -p 8090
```

Web 服务监听所有网络接口且不包含身份验证，只应在可信局域网中使用。

服务启动后，可以在 macOS 本地终端打开默认浏览器：

```sh
open http://192.168.1.58:8080/
```

查看服务状态与实际采集帧率：

```sh
curl http://127.0.0.1:8080/api/status
```

按 Ctrl+C 停止服务。相机和端口不能由两个 `realsense_web` 实例同时占用。

### 命令行参数

```text
./realsense_capture [-o FILE] [-n COUNT] [-r WIDTHxHEIGHT] [-f FPS] [-s SKIP]
```

- `-o FILE`：输出文件名或文件名前缀，默认为 `capture.ppm`
- `-n COUNT`：保存帧数；设为 `0` 时持续采集，直到按下 Ctrl+C
- `-r WIDTHxHEIGHT`：图像分辨率，默认为 `640x480`
- `-f FPS`：帧率，默认为 `30`
- `-s SKIP`：正式保存前丢弃的预热帧数，默认为 `30`

示例：

```sh
# 保存一张 1280x720 图片
./realsense_capture -r 1280x720 -o room.ppm

# 保存十帧，文件名为 shot_000001.ppm 到 shot_000010.ppm
./realsense_capture -n 10 -o shot.ppm

# 持续采集，按 Ctrl+C 停止
./realsense_capture -n 0 -o stream.ppm
```

从香橙派复制图片到本机：

```sh
scp opi:~/realsense-capture/capture.ppm .
```

大多数图像查看器可以直接打开 PPM。若系统安装了 ImageMagick，可以使用
`magick capture.ppm capture.jpg` 转换为 JPEG。

## English

This project contains two pure C programs built on the librealsense2 C API.
`realsense_capture` saves color frames as PPM images, while `realsense_web`
captures color and depth streams and serves a live web interface.

### Requirements

- Linux (tested on ARM64 Ubuntu 20.04)
- Intel RealSense camera (tested with a D435i)
- librealsense2 and its development files
- A C11 compiler, Make, and pkg-config
- FFmpeg (required by the live web view)

### Build and capture one image

```sh
make
./realsense_capture
```

By default, the program starts the color camera at 640x480 and 30 FPS, then
saves one image as `capture.ppm`. It discards the first 30 frames to allow
automatic exposure to settle.

### Live color and depth web view

`realsense_web` captures RGB8 color and Z16 depth streams simultaneously at
640x480 and 60 Hz. Its built-in web server displays both streams. Camera
capture and MJPEG encoding run independently, so a slow encoder drops stale
frames instead of blocking camera capture.

The depth view maps 0 to 20 meters from red (near) to blue (far). This is a
visualization limit, not a claim that the D435i measures reliably at 20 meters;
usable range depends on lighting, target reflectivity, and other conditions.

Make sure FFmpeg is installed, then run:

```sh
make
./realsense_web
```

Open `http://<orange-pi-ip>:8080/` from a browser on the same LAN. Use `-p` to
select another port:

```sh
./realsense_web -p 8090
```

The server listens on all network interfaces and has no authentication. Use it
only on a trusted LAN.

After starting the service, open the default browser from a local macOS
terminal:

```sh
open http://192.168.1.58:8080/
```

Inspect service status and the measured capture rate with:

```sh
curl http://127.0.0.1:8080/api/status
```

Press Ctrl+C to stop the service. The camera and listening port cannot be used
by two `realsense_web` instances at the same time.

### Command-line options

```text
./realsense_capture [-o FILE] [-n COUNT] [-r WIDTHxHEIGHT] [-f FPS] [-s SKIP]
```

- `-o FILE`: output file or filename prefix (default: `capture.ppm`)
- `-n COUNT`: number of frames to save; `0` runs until Ctrl+C (default: `1`)
- `-r WIDTHxHEIGHT`: color resolution (default: `640x480`)
- `-f FPS`: frame rate (default: `30`)
- `-s SKIP`: warm-up frames to discard (default: `30`)

Examples:

```sh
# Save a single 1280x720 image.
./realsense_capture -r 1280x720 -o room.ppm

# Save ten frames as shot_000001.ppm through shot_000010.ppm.
./realsense_capture -n 10 -o shot.ppm

# Capture continuously until Ctrl+C.
./realsense_capture -n 0 -o stream.ppm
```

Copy a captured image back to the local computer with:

```sh
scp opi:~/realsense-capture/capture.ppm .
```

Most image viewers can open PPM directly. With ImageMagick installed, run
`magick capture.ppm capture.jpg` to convert it to JPEG.
