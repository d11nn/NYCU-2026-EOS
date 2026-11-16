# NYCU 2026 EOS 課程作業

開發機是 x86 Ubuntu（交叉編譯），目標板是 TQ-IMX6 E9V3（i.MX6Q，4× Cortex-A9，Ubuntu 16.04、kernel 4.1.15）。

| 資料夾 | 內容 |
|---|---|
| [`lab1/`](lab1/) | 交叉編譯 hello world |
| [`lab2/`](lab2/) | 直接寫 framebuffer：顯示圖片、攝影機即時畫面、跑馬燈 |
| [`lab3/`](lab3/) | 3.1 即時人臉辨識（`face/`）、3.2 安全帽偵測（`helmet/`） |
| `tools/` | 序列埠傳檔工具 lrzsz 的原始碼壓縮檔 |

各 lab 資料夾裡有講義 PDF、原始碼、編好的 ARM 執行檔和 README。

### 沒有放進 repo 的東西

| 項目 | 原因 | 怎麼取得 |
|---|---|---|
| `board_package/`（板子映像檔、U-Boot、交叉編譯器安裝包） | 2.6 GB，其中多個檔案超過 GitHub 單檔 100 MB 上限 | 課程 Lab 1 提供的套件 |
| OpenCV 3.4.7 原始碼（`lab2/115_LAB2_package/.../opencv-3.4.7*`、`lab2/115_LAB2_package.zip`） | 第三方原始碼，約 360 MB | 課程 Lab 2 套件，或 OpenCV 官方 3.4.7 版 |
| ncnn（`lab3/face/third_party/`） | 第三方原始碼 | `lab3/face/scripts/build_ncnn.sh` |
| 模型權重（`models/`、`*.ncnn.bin`） | 檔案大，人臉模型沒有授權條款 | `lab3/face/scripts/get_models.sh`；安全帽模型由訓練機匯出 |
| CoreMark、解壓後的 lrzsz | 第三方原始碼 | github.com/eembc/coremark；`tools/lrzsz-0.12.20.tar.gz` |

## 開發環境

| 項目 | 說明 |
|---|---|
| 交叉編譯器 | `arm-linux-gnueabihf-gcc` / `g++`（Linaro 5.3） |
| OpenCV 3.4.7（ARM） | Lab 2 用 cmake 交叉編譯，安裝在 `/usr/local/arm-opencv/install`（`lib/libopencv_world.so.3.4.7`） |
| ncnn（ARM） | Lab 3 用，`lab3/face/scripts/build_ncnn.sh` 產生 |
| 序列埠 | `/dev/ttyUSB0`，115200 8N1（minicom） |

## 板子重點

| 項目 | 內容 |
|---|---|
| LCD | `/dev/fb0`，800x480，16 bpp（BGR565） |
| HDMI | `/dev/fb2`，1920x1080，32 bpp |
| 攝影機 | Logitech C270 → `/dev/video2`（640x480 30 fps；1280x720 需用 MJPEG） |
| SD 卡 | 插入後自動掛在 `/run/media/mmcblk1p1` |
| 傳檔 | SD 卡最快；序列埠用 zmodem（minicom Ctrl-A S，或 `rz` / `sz`） |
| OpenCV 執行期 | 板子沒裝 OpenCV，執行檔旁邊放 `libopencv_world.so.3.4`，`LD_LIBRARY_PATH=.` 後執行（各 lab 的 `run.sh` 就是做這件事） |
