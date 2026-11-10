# Lab 3 — 3.1 即時人臉辨識（face3）

Haar cascade 偵測 → 雙眼對齊 → MobileFaceNet（ncnn）抽特徵 → cosine 比對 → 追蹤、平滑、投票。

## 三台機器的分工

| 機器 | 架構 | 用途 |
|---|---|---|
| Mac | arm64 | 編輯程式碼（本資料夾） |
| Ubuntu 建置主機（`wayne-TUF-Gaming-FX505DT`） | x86_64 | 有 `arm-linux-gnueabihf-g++`，在這裡**交叉編譯**；也透過 minicom（`/dev/ttyUSB0`）連到板子 |
| E9V3 | armv7（Cortex‑A9） | **執行** `face3` |

在 Ubuntu 上編出來的 `face3` 是 ARM 執行檔，**只能在板子上跑**，不能在 Ubuntu 或 Mac 上執行。

## 1. 把程式碼搬到 Ubuntu 建置主機

在 Mac 上：

```bash
scp -r ~/nycu/eos/lab3/face wayne@192.168.137.180:~/lab3_face
```

## 2. 在 Ubuntu 上：交叉編譯 ncnn（只要做一次）

```bash
cd ~/lab3_face
bash scripts/build_ncnn.sh
```

- 需要 `git`、`cmake`，而且 `arm-linux-gnueabihf-g++` 要在 PATH 裡。
- 如果新版 ncnn 跟舊的交叉編譯器不合，換舊一點的版本：`NCNN_TAG=20220216 bash scripts/build_ncnn.sh`（先刪掉 `third_party/ncnn`）。

## 3. 在 Ubuntu 上：下載模型

```bash
bash scripts/get_models.sh
```

會下載到 `models/`：
- `haarcascade_frontalface_default.xml`、`haarcascade_eye.xml`（OpenCV 3.4 官方檔案；你 Lab 2 的 OpenCV 原始碼 `data/haarcascades/` 裡也有）
- `mobilefacenet.param`、`mobilefacenet.bin`（InsightFace 的 MobileFaceNet，ncnn 格式。來源 repo 沒有標示授權，只用於課程）

## 4. 在 Ubuntu 上：編譯 face3

`OPENCV_DIR` 指向你 Lab 2 交叉編譯 OpenCV 3.4 的安裝目錄（裡面要有 `include/opencv2/` 和 `lib/libopencv_world.so`）：

```bash
make OPENCV_DIR=/path/to/opencv-3.4.7/install
```

## 5. 放到板子上

板子上需要的檔案：

```
~/lab3/
├── face3
├── run.sh
├── config.yml
├── libopencv_world.so.3.4     ← 從 ~/lab2 複製
├── models/                    ← 四個模型檔
└── enroll/                    ← 登錄照片（見第 6 步）
```

用你 Lab 1 的傳檔方式（或 scp，如果板子有網路）把 `face3`、`run.sh`、`config.yml`、`models/` 傳上去，然後在板子上：

```bash
cp ~/lab2/libopencv_world.so.3.4 ~/lab3/
```

**OpenMP 函式庫**：ncnn 用 OpenMP 做多執行緒。在板子上確認有沒有 `libgomp`：

```bash
ls /lib/libgomp* /usr/lib/libgomp*
```

如果沒有，從 Ubuntu 的交叉編譯器裡找一份 `libgomp.so.1`（通常在 `arm-linux-gnueabihf/lib/` 底下）放到 `~/lab3/`。

## 6. 登錄組員的臉

1. 用 Lab 2 的 `lab23` 按 `c` 拍照（**用板子的攝影機，最好在 demo 教室**），每人約 20–30 張：正面、左右轉、抬頭、低頭、戴和不戴眼鏡。
2. 依照學號分資料夾：

```
enroll/
├── 0812345/   ← 資料夾名稱就是畫面上顯示的文字
│   ├── 0.bmp
│   └── ...
└── 0812346/
    └── ...
```

3. 建立 `gallery.yml`：

```bash
./run.sh enroll enroll
```

會印出每個人「跟自己範本的相似度」，以及「跟另一位組員範本的最高相似度」。對齊後的臉存在 `aligned/`，**打開看一下兩眼是否水平、臉是否置中**。

## 7. 校準門檻

收集一些「其他人」的照片放在 `strangers/`，然後：

```bash
./run.sh score strangers
```

最後一行是陌生人裡最高的相似度。把 `config.yml` 的 `sim_threshold` 設在「陌生人最高分」和「組員最低分」之間，偏向組員那邊一點。

如果組員分數普遍偏低（例如低於 0.4），把 `input_rgb` 改成 `0` 再跑一次 enroll，比較哪個分得比較開。

## 8. 即時執行

```bash
./run.sh run
```

- 綠框是組員（顯示學號），紅框是 `unknown`，黃框 `...` 是還在辨識。
- `q` 結束，`s` 存目前畫面成 `snapshot_N.jpg`。
- 攝影機的 `Corrupt JPEG data` 警告很多，建議這樣執行，只濾掉那些警告、保留其他錯誤：

```bash
./run.sh run 2>&1 | grep -v "Corrupt JPEG"
```

- 終端機每 30 幀印一次各階段的平均耗時：

```
fps 8.2 | wait 3 detect 70 track 1 draw 2 display 35 ms | latency 160 ms | recog 180 ms/face (6) | tracks 3
```

| 欄位 | 意思 |
|---|---|
| `wait` | 等攝影機下一幀的時間（很大代表攝影機本身就慢） |
| `detect` | Haar 偵測 |
| `track` | 追蹤、收辨識結果、送辨識工作 |
| `draw` | 畫框和文字 |
| `display` | 縮放、轉色、寫入 framebuffer |
| `latency` | 攝影機拍到 → 出現在螢幕上，總延遲 |
| `recog` | 背景執行緒每張臉的對齊 + MobileFaceNet 時間（不影響畫面 FPS） |

## 常見調整（都在 config.yml，不用重新編譯）

| 狀況 | 調整 |
|---|---|
| 畫面延遲、FPS 低，`display` 很大 | `display_fit: 0`（原尺寸顯示） |
| FPS 低，`detect` 很大 | `detect_width` 改 240；`scale_factor` 改 1.2 |
| 框會閃 | `track_timeout` 調大（例如 0.6）；`min_neighbors` 調小（漏偵測變少） |
| 人走掉後框還留著 | `track_timeout` 調小 |
| 同一張臉出現兩個框 | `overlap_merge` 調小（例如 0.4） |
| 背景冒出假框 | `min_neighbors` 調大；`min_hits` 調大 |
| 人移動時框拖在後面 | `snap_distance` 調小；`ema_alpha` 調大 |
| 框靜止時會微微抖動 | `ema_alpha` 調小；`snap_distance` 調大 |
| 人移動快時跑出新框 | `center_match` 調大 |
| 兩人靠很近時框互搶 | `center_match` 調小 |
| 標籤跳來跳去 | `vote_min` 調大 |
| 陌生人被認成組員 | `sim_threshold` 調大 |
| 組員被判 unknown | `sim_threshold` 調小；多拍不同角度的登錄照 |

## 程式碼地圖（抽問時找程式碼用）

| 問題 | 檔案:函式 |
|---|---|
| 主迴圈：偵測 → 追蹤 → 送辨識 → 畫圖 | `src/main.cpp` `run_live` |
| Haar 偵測、縮小影像、直方圖等化 | `src/face_detector.cpp` `FaceDetector::detect` |
| 合併重疊的偵測框 | `src/face_detector.cpp` `merge_overlaps` |
| 找眼睛、相似變換、裁成 112×112 | `src/face_aligner.cpp` `FaceAligner::find_eyes` / `align` |
| MobileFaceNet 推論、L2 正規化 | `src/face_embedder.cpp` `FaceEmbedder::embed` |
| cosine 相似度 | `src/face_embedder.cpp` `cosine_similarity` |
| 判斷學號或 unknown（門檻、差距） | `src/gallery.cpp` `Gallery::match` |
| 登錄、範本平均 | `src/gallery.cpp` `Gallery::add` / `finalize`；`src/main.cpp` `run_enroll` |
| IoU / 中心距離配對、EMA 平滑與大幅移動直接跳、用時間決定框的存留 | `src/tracker.cpp` `Tracker::update` |
| 合併重疊的 track | `src/tracker.cpp` `Tracker::merge_overlapping_tracks` |
| 各階段耗時、延遲 | `src/main.cpp` `run_live`（`REPORT_EVERY` 那段） |
| 多幀投票、遲滯 | `src/tracker.cpp` `Tracker::add_vote` |
| 同一學號只出現一次 | `src/tracker.cpp` `Tracker::resolve_duplicates` |
| 背景辨識執行緒、工作佇列 | `src/recognizer.cpp` `Recognizer::loop` / `push` |
| 攝影機執行緒、只保留最新一幀 | `src/camera.cpp` `Camera::loop` / `wait_frame` |
| framebuffer、等比例縮放、原尺寸顯示、直接寫入螢幕記憶體、16/32 bpp | `src/framebuffer.cpp` `Framebuffer::show` / `convert` |
| 所有參數 | `config.yml`、`src/config.hpp` |
