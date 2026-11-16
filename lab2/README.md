# Lab 2：Framebuffer 影像輸出（圖片 / 攝影機 / 跑馬燈）

目標：不靠 X / Qt，直接把像素寫進 Linux framebuffer 裝置（`/dev/fb0`）來顯示畫面。
三個 part 的程式都在 `src/`。

## 檔案

| 路徑 | 內容 |
|---|---|
| `src/lab_2-1_source.cpp` | Part 1：顯示一張 BMP |
| `src/lab_2-2_source.cpp` | Part 2：攝影機即時顯示 + 按 `c` 截圖、`q` 離開 |
| `src/lab_2-3_source.cpp` | Part 3：高解析度攝影機（mmap 寫 fb）+ `scroll` 跑馬燈模式 |

## 編譯（開發機）

OpenCV 3.4.7 已交叉編譯到 `/usr/local/arm-opencv/install`（講義 4.2 節的 cmake-gui 流程），
編譯指令照講義，再加 `-std=c++11 -pthread`（Part 2/3 用到 thread）：

```sh
cd src
arm-linux-gnueabihf-g++ -std=c++11 -pthread lab_2-1_source.cpp -o demo \
    -I/usr/local/arm-opencv/install/include -L/usr/local/arm-opencv/install/lib -lopencv_world
arm-linux-gnueabihf-g++ -std=c++11 -pthread lab_2-2_source.cpp -o lab22 (同上參數)
arm-linux-gnueabihf-g++ -std=c++11 -pthread lab_2-3_source.cpp -o lab23 (同上參數)
```

## 板子上的檔案與執行

板子 `/root/lab2/` 有：`demo`、`lab22`、`lab23`、`NYCU_logo.bmp`、`advance.png`、
`libopencv_world.so.3.4`、`demo.sh`。板子沒裝 OpenCV，所以要先讓動態連結器找到旁邊的 `.so`：

```sh
cd /root/lab2
export LD_LIBRARY_PATH=.
./demo                      # Part 1：顯示 NYCU_logo.bmp（左上角）
./lab22 [截圖資料夾]         # Part 2：640x480 攝影機，c 截圖、q 離開
./lab23 [截圖資料夾] [WxH]   # Part 3：預設 1280x720 MJPEG，c 截圖、q 離開
./lab23 scroll advance.png [speed]   # Part 3 跑馬燈：j 往左、l 往右、空白鍵暫停、q 離開
```

跑馬燈自動啟動：`/etc/systemd/system/lab2-scroll.service`（目前 disabled），
`/root/.profile` 裡有一段只在 `/root/lab2/.autostart` 存在時才會執行：登入序列埠就停掉跑馬燈並跑 `lab22`。
要 demo 時 `systemctl enable --now lab2-scroll` 加 `touch /root/lab2/.autostart`，demo 完反向操作。

## 程式重點

### Part 1（`lab_2-1_source.cpp`）：framebuffer 基本功

1. `get_framebuffer_info()`：`open("/dev/fb0")` 之後用 `ioctl(fd, FBIOGET_VSCREENINFO, &info)` 向核心問
   螢幕資訊，取 `xres_virtual`（一列有幾個像素，含不可見的部分）和 `bits_per_pixel`（LCD 是 16）。
2. `cv::imread` 讀圖 → `cv::cvtColor(..., COLOR_BGR2BGR565)` 把 24-bit BGR 轉成 LCD 要的 16-bit BGR565
   （R 5 bit、G 6 bit、B 5 bit）。
3. 一列一列輸出：`ofs.seekp(y * xres_virtual * bytes_per_pixel)` 移到第 y 列開頭，
   `ofs.write(image.ptr(y), width * bytes_per_pixel)` 寫一列。
   位移要用 `xres_virtual` 而不是圖片寬度，否則圖片會歪掉；沒做置中所以圖貼在左上角。

### Part 2（`lab_2-2_source.cpp`）：攝影機 + 鍵盤 + 背景存檔

- `cv::VideoCapture camera(2)`：C270 在板子上是 `/dev/video2`；設 640x480。
- 先把整個螢幕填黑，再對每一幀做「等比例縮放 + 置中」（letterbox），避免變形。
- 鍵盤：`keyboard_raw_mode()` 用 `termios` 關掉 `ICANON`（不用等 Enter）和 `ECHO`，
  `VMIN=0, VTIME=0` 讓 `read()` 不會卡住；`read_key()` 沒按鍵就回傳 -1。
- 截圖不能卡住顯示：`ScreenshotWriter` 是一條背景 thread，主迴圈只把 (檔名, 影像複本) 推進
  佇列（`std::mutex` + `std::condition_variable`），背景 thread 負責 `cv::imwrite`。
  這個 pattern 在 Lab 3 的 `Recognizer` 又用了一次。
- `signal(SIGINT, ...)` 讓 Ctrl-C 也能正常收尾（還原終端機、等 thread 結束）。

### Part 3（`lab_2-3_source.cpp`）：mmap 與跑馬燈

- 為什麼改用 mmap：Part 2 用 `ofstream::write` 走檔案系統，一幀 1280x720 要 0.49 秒；
  `map_framebuffer()` 用 `mmap()` 把 framebuffer 記憶體直接映射進程式，之後就是 `memcpy`，快一百倍以上。
  要用 `FBIOGET_FSCREENINFO` 拿 `line_length`（一列實際佔幾 byte）和 `smem_len`（映射長度）。
- 攝影機模式：`CAP_PROP_FOURCC = MJPG` 才拿得到 1280x720（USB 2.0 頻寬不夠傳 raw）；畫面比螢幕大才縮小。
- `scroll` 模式（`run_scroll_board()`）：
  1. 讀 `advance.png`（含 alpha），先和黑底合成成 BGR，再轉成 BGR565 一次，之後每幀只做 memcpy。
  2. `position` 是目前視窗在圖片上的 x 起點，取 `% W` 做環繞：一列分兩段 memcpy（圖尾 + 圖頭）。
  3. 固定 30 fps：每幀算 `next_frame`，用 `usleep` 補差。
  4. 鍵盤同時讀 stdin（序列埠 / ssh）和 `/dev/input/event*`（板子上插的 USB 鍵盤）。
     開始前先把 evdev 的舊事件清掉並忽略前 1 秒的按鍵（避免開機時殘留的空白鍵讓它暫停）；
     若 stdin 本身就是本機 console（`/dev/ttyN`），按鍵已經從 stdin 進來，就不再開 evdev（避免讀兩次）。

## Demo 時 TA 常問

- 一列有幾個像素、一個像素幾 bit → 看板子 `ioctl` 的結果：LCD 800x480、16 bpp；HDMI 1920x1080。
- 為什麼要 BGR565 → LCD 的 framebuffer 格式是 16 bit。
- seekp 的位移怎麼算 → `(y * xres_virtual + x) * bytes_per_pixel`。
- 為什麼圖片會變形 / 貼左上角 → 沒有照螢幕比例縮放、沒有加 offset。
