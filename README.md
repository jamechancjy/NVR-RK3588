# nvr-rk3588

RK3588 上的网络视频录像机（NVR）。目标规格：

- 摄像头接入：RTSP / ONVIF / GB28181（国标），H.264 / H.265
- 通道数：最高 **196 路**
- 硬盘：最多 **10 块**（写满一块顺序写下一块，回绕覆盖最旧）
- 实时预览：Web 端 **WebRTC**，支持 4 用户同时接入
- 录像：报警(IO)、移动侦测、人形触发、定时/连续
- 本地 UI：**LVGL**
- 语言：C/C++（RK3588 硬件加速用 MPP / RGA / RKNN）

完整架构设计见 [`docs/architecture.md`](docs/architecture.md)。

## 已实现（骨架 + 存储引擎）

本仓库当前落地的是**存储子系统**（设计文档中细节最完整的部分）和整体工程骨架：

- `nvr::StorageEngine` — 全局顺序存储：所有通道汇入统一写入流，写满一块逻辑盘顺序切下一块，回绕覆盖。
- `nvr::LogIndex` — `log.txt` 定长记录索引，滚动 `end` 标记（每条 I 帧记录带 end，清除上一条）；记录只改/清空、绝不删除，文件固定大小。
- `nvr::RecordFile` — 预分配 1GB 定长数据文件（A0001…），逐帧顺序写，每 100 帧刷盘；内容可覆盖。
- `nvr::DiskManager` — 逻辑盘发现（Linux 顺序）、是否已格式化检测、格式化（建 log.txt + 预分配数据文件）。
- 上电恢复 — 定位**全局唯一 end**（最靠近初始逻辑盘），前后行校验取最后一个，清理其余所有 end。
- `nvr::MediaHub` / `nvr::StreamSource` / `nvr::Recorder` — 数据分发、接入源接口、带预录像 ring buffer 的录像器。
- `nvr::RtspSource` — **RTSP 拉流接入**（FFmpeg libavformat，不解码）：拉 H.264/H.265 视频 + AAC 音频压缩帧，自动重连，直接喂给录像/存储链路。

## 帧 / 日志格式

数据文件内每帧：`[通道][音视频][帧率][I/P][帧长][数据]`（见 `include/nvr/storage/frame.h`）。

`log.txt` 记录（定长）：`A0003_CH018_20260126152030_10_P_000000001234_E`
- 数据文件名 / 通道 / 时间 / 该秒内第几个 I 帧 / 类型(A=IO报警 M=移动 P=人形 T=定时) / 文件内偏移 / end 标记。

## 构建（Host / 开发机）

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

演示（合成帧跑通存储链路）：

```bash
mkdir -p /tmp/nvr_storage/disk0 /tmp/nvr_storage/disk1
./build/src/nvrd --root /tmp/nvr_storage
```

RTSP 实时接入（需 FFmpeg 开发库；CMake 会自动探测，`FFmpeg (RTSP ingest): TRUE` 即启用）：

```bash
# 拉一路 RTSP，连续录像写入存储；Ctrl-C 停止（或 --seconds N 定时停）
./build/src/nvrd --root /tmp/nvr_storage --rtsp rtsp://user:pass@<cam-ip>:554/stream1
```

不带 FFmpeg 时 RtspSource 不编译、RTSP 工厂返回 nullptr，其余功能不受影响。

## 构建（RK3588 目标）

需 Rockchip SDK（MPP / RGA / RKNN）与 aarch64 交叉工具链：

```bash
cmake -S . -B build-rk -DNVR_PLATFORM=RK3588 \
  -DCMAKE_TOOLCHAIN_FILE=<aarch64-toolchain.cmake>
cmake --build build-rk -j
```

`NVR_PLATFORM=HOST`（默认）用桩化 HAL 在 x86 上构建，便于开发和 CI；`RK3588` 接真实硬件编解码。

## 路线图

见架构文档第 7 节（M1 骨架 → M6 196 路压测）。当前处于 M1/M2：工程骨架 + 存储引擎。后续：真实 RTSP/ONVIF/GB28181 接入、MPP 解码预览、WebRTC 网关、RKNN 人形/移动侦测、LVGL 本地 UI、Web 后端。
