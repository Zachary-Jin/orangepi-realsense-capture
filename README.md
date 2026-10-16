# Orange Pi RealSense C Capture / 香橙派 RealSense C 图像采集

[中文](#中文说明) | [English](#english)

## 中文说明

这是一个使用 librealsense2 C API 从 Intel RealSense 相机采集彩色画面的
纯 C 程序。图像保存为二进制 PPM（`P6`）格式，因此香橙派上不需要安装额外的
图像处理库。

### 环境要求

- Linux（已在 ARM64 Ubuntu 20.04 上验证）
- Intel RealSense 相机（已在 D435i 上验证）
- librealsense2 及其开发文件
- 支持 C11 的 C 编译器、Make 和 pkg-config

### 编译并抓取一张图片

```sh
make
./realsense_capture
```

默认以 640x480、30 FPS 启动彩色相机，并将图片保存为 `capture.ppm`。程序会先
丢弃 30 帧，让相机的自动曝光趋于稳定。

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

This pure C program uses the librealsense2 C API to capture RGB frames from an
Intel RealSense camera. It writes binary PPM (`P6`) images, so no additional
image-processing library is required on the Orange Pi.

### Requirements

- Linux (tested on ARM64 Ubuntu 20.04)
- Intel RealSense camera (tested with a D435i)
- librealsense2 and its development files
- A C11 compiler, Make, and pkg-config

### Build and capture one image

```sh
make
./realsense_capture
```

By default, the program starts the color camera at 640x480 and 30 FPS, then
saves one image as `capture.ppm`. It discards the first 30 frames to allow
automatic exposure to settle.

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
