# Lab 3：人臉辨識（3.1）+ 安全帽偵測（3.2）— 原始碼導讀

這份 README 說明 `face/` 和 `helmet/` 每一個檔案在做什麼。
安全帽部分另有一份更細的逐行導讀：`helmet/CODE_GUIDE.md`。
先看「整體架構」抓住資料怎麼流，再照「逐檔導讀」對著原始碼讀；每個段落都標了行號。
兩個專案各自的 README（`face/README.md`、`helmet/README.md`）有參數調整表和實測時間，這裡不重複。
3.2 從資料、訓練、評估到搬上板子是怎麼做的，寫在 `helmet/README.md` 的 A 部分。

## 0. 資料夾

```
lab3/
├── 115_lab3.pdf                講義（3.1 即時人臉辨識、3.2 安全帽、3.3 報告）
├── 115_lab3_final_demo.pdf     final demo 規則（輸出 lab3_final_<組別>.jpg，F1 評分）
├── mock.jpg            final demo 前公布的練習圖（6000x4000 拼貼）
├── lab3-report.md      3.1 人臉辨識的流程報告
├── lab3-1.pdf          3.1 報告的 PDF 版
├── lab3-2-report.md    3.2 安全帽偵測的報告
├── lab3-report.pdf     完整報告（3.1 + 3.2）的 PDF 版
├── assets/             報告用的圖
├── report/             3.1 的補充文件
│   ├── face_recognition_selection.md   技術選型報告（為什麼選 Haar + MobileFaceNet + ncnn）
│   ├── lab31_explained.html            3.1 流程圖解
│   ├── face_pipeline.html              人臉辨識 pipeline 示意
│   ├── lab31_code_reader.html          原始碼對照講義需求的導讀頁
│   ├── build_code_reader.py、code_reader_template.html   產生上面那頁（python3 lab3/report/build_code_reader.py）
│   └── build_report.py                 把兩份報告的 .md 合成 lab3-report.pdf（python3 lab3/report/build_report.py）
├── face/               3.1 即時人臉辨識（執行檔 face3）
│   ├── src/            11 個 .cpp + 對應 .hpp（見第 2 節）
│   ├── scripts/        build_ncnn.sh（交叉編譯 ncnn）、get_models.sh（下載模型）
│   └── config.yml、Makefile、run.sh
└── helmet/             3.2 安全帽偵測（執行檔 helmet）
    ├── src/            main.cpp、helmet_detector.*、framebuffer.*、config.*
    ├── CODE_GUIDE.md   安全帽部分的逐行導讀
    └── config.yml、Makefile、run.sh

helmet_work/            安全帽開發過程的工作檔
├── stage ~ stage7/     每次改版放到板子的部署包（執行檔、config、MD5SUMS、預期輸出 expected.txt、測試圖）
├── tools/              用序列埠 zmodem 傳檔的腳本（sz_to_board.sh、rz_from_board.sh、sercmd.py）和傳檔紀錄
├── fromboard/          從板子抓回來的結果圖
└── multi-scale-tiles.patch   同時用多種切塊大小（例如 2x2 加 4x4）的修改，尚未在板子上測試
```

`face/face3`、`face/libgomp.so.1` 是編好的 ARM 執行檔和板子上需要的 OpenMP 執行期函式庫。

模型檔（`models/`）、ncnn（`third_party/`）和編譯產物（`build/`）不在 repo 裡，分別由 `face/scripts/get_models.sh`、
`face/scripts/build_ncnn.sh` 和 `make` 產生；安全帽模型 `helmet.ncnn.param/.bin` 由訓練機匯出後放到 `helmet/models/`。

## 1. 整體架構

### 1.1 共同的基礎（兩個專案都用）

| 元件 | 來源 | 說明 |
|---|---|---|
| OpenCV 3.4.7（ARM） | Lab 2 編的 `/usr/local/arm-opencv/install` | 讀圖、縮放、轉色、Haar cascade、畫框 |
| ncnn（ARM） | `face/scripts/build_ncnn.sh` → `face/third_party/ncnn/build-armhf/install` | 騰訊的 CPU 推論引擎，跑 MobileFaceNet 和 YOLO。沒有 GPU 的板子上用 NEON + OpenMP |
| Framebuffer | `framebuffer.cpp`（兩邊幾乎一樣） | Lab 2-3 的 mmap 寫法，再依 `bits_per_pixel` 自動選 16 / 32 bpp 轉色 |
| Config | `config.cpp` | 用 `cv::FileStorage` 讀 `config.yml`，沒寫的欄位用 `config.hpp` 裡的預設值 |

### 1.2 3.1 人臉辨識的資料流（`face/`）

```
 Camera thread ──最新一幀──▶ 主迴圈 (main.cpp run_live)
                               │
                               ├─ FaceDetector::detect   Haar cascade → 臉框 (Rect)
                               ├─ Tracker::update        把臉框對到既有的 track（同一張臉），平滑、計時
                               ├─ Recognizer::take_results → Tracker::add_vote  收回背景辨識結果，投票
                               ├─ 把需要辨識的 track 剪一塊影像 push 給 Recognizer（背景 thread）
                               │        └─ FaceAligner::align → FaceEmbedder::embed → Gallery::match
                               ├─ draw_tracks            畫框 + 標籤（黃 ... / 紅 unknown / 綠 學號）
                               └─ Framebuffer::show      mmap 寫進 /dev/fb2
```

重點設計：偵測（快，每幀）和辨識（慢，MobileFaceNet 約 100 ms 以上）分開。
辨識在背景 thread 做，每個 track 每 `recog_interval` 秒才辨識一次，畫面不會卡。

### 1.3 3.2 安全帽偵測的資料流（`helmet/`）

```
 imread 照片(最大 6000x4000)
   └─ HelmetDetector::find_helmets
        ├─ detect(整張圖)        letterbox 到 640x640 → ncnn → 解出 (cx,cy,w,h,score) → 回原圖座標
        ├─ detect(2x2 切塊各一次) 只收「小的」框（遠處的人），碰到切塊邊的丟掉
        └─ merge_boxes           NMS（IoU）→ 包含去重（一人一框）
   └─ draw_boxes → imwrite lab3_final_<組別>.jpg → sync → Framebuffer::show → 等 Enter
```

## 2. face/ 逐檔導讀

讀的順序建議：`config` → `camera` → `framebuffer` → `face_detector` → `face_aligner` → `face_embedder` → `gallery` → `recognizer` → `tracker` → `main`。

### 2.1 `config.hpp` / `config.cpp`

- `config.hpp`：一個 `struct Config`，每個欄位旁邊的註解就是參數意義（攝影機、Haar、ncnn 模型、門檻、追蹤）。
- `config.cpp:7-12`：`read_field()` 樣板函式，`fs[name]` 不是空的才覆蓋，所以 yml 可以只寫想改的欄位。
- `config.cpp:14-57`：`Config::load()` 開 `cv::FileStorage` 逐欄讀。

### 2.2 `camera.hpp` / `camera.cpp` — 背景抓影像，只留最新一張

- 為什麼要獨立 thread：主迴圈每幀要做偵測（幾十 ms），如果直接 `capture.read()`，V4L2 的 buffer 會累積舊影像，畫面延遲越來越大。
- `camera.cpp:7-22` `open()`：`VideoCapture(index)`，設 `MJPG`（USB 2.0 頻寬才夠）和解析度。
- `camera.cpp:41-61` `loop()`：無窮迴圈 `capture.read(frame)`，拿到就 **`cv::swap(latest, frame)`**（交換指標，不複製像素），
  記 `latest_time`（`gettimeofday`，和 main 的 `now_seconds()` 同一個時鐘，用來量延遲），`frame_id++`，`cond.notify_all()`。
- `camera.cpp:63-72` `wait_frame(last_id, frame, &t)`：主迴圈呼叫，`cond.wait_for` 最多等 2 秒，直到 `frame_id > last_id`，
  再 `latest.copyTo(frame)`。回傳新的 id；逾時或已停止回 -1。
- 同步方式：`std::mutex mtx` 保護 `latest / frame_id / running`；`std::condition_variable cond` 讓主迴圈睡著等新幀而不是空轉。

### 2.3 `framebuffer.hpp` / `framebuffer.cpp` — Lab 2-3 的 mmap 版升級

- `framebuffer.cpp:14-54` `open()`：`open` → 兩個 `ioctl`（`FBIOGET_VSCREENINFO` 拿 xres/yres/bpp/`red.offset`，
  `FBIOGET_FSCREENINFO` 拿 `line_length`、`smem_len`）→ `mmap` → 關 fd（映射還在）→ 寫 `/sys/class/graphics/fbN/blank` 0 把螢幕叫醒 → 清黑。
- `framebuffer.cpp:67-78` `convert()`：依 bpp 選轉色：16 → `BGR2BGR565`；32 → 看 `red_offset` 決定 `BGR2RGBA` 或 `BGR2BGRA`（HDMI fb2 是 32 bpp）。
- `framebuffer.cpp:80-121` `show(bgr, fit)`：
  - 第 97-100 行：等比例縮放（取寬、高比例較小者）。`fit=false` 時只縮不放，640x480 在 1080p 上就維持 640x480 置中，
    每幀寫入量從 6 MB 降到 1.2 MB（`config.yml` 的 `display_fit: 0` 就是為了速度）。
  - 第 109-113 行：影像尺寸改變才清畫面（否則舊的黑邊殘留）。
  - 第 117-120 行：**在 framebuffer 記憶體上直接建一個 `cv::Mat screen(h, w, type, mem + offset, line_length)`**，
    `cvtColor` 的輸出就直接落在螢幕上，省掉一次暫存 + 逐列 memcpy。這是和 helmet 版 `show()` 唯一的差別（helmet 用逐列 memcpy，`helmet/src/framebuffer.cpp:113-117`）。

### 2.4 `keyboard.hpp` / `keyboard.cpp` — Lab 2-3 的鍵盤處理搬過來

- `keyboard.cpp:15-39` `keyboard_open()`：stdin 是終端機就設 raw mode（`~ICANON`、`~ECHO`、`VMIN=VTIME=0`）；
  stdin 不是本機 console（`/dev/ttyN`）時再開 `/dev/input/event0~15`（USB 鍵盤），開完先把舊事件讀光（drain）。
- `keyboard.cpp:48-66` `read_key()`：先讀 stdin 一個 byte；沒有就掃 evdev，只認 `KEY_Q`、`KEY_S` 的按下事件（`value == 1`）。回 -1 代表沒按鍵。

### 2.5 `face_detector.hpp` / `face_detector.cpp` — Haar cascade 偵測

- `face_detector.cpp:42-69` `detect(bgr)`，五步：
  1. 轉灰階、縮到 `detect_width`（320）寬：Haar 只看亮度，像素少 4 倍就快 4 倍。
  2. `equalizeHist`：直方圖等化，減少光線影響。
  3. `cascade.detectMultiScale(small, faces, scale_factor, min_neighbors, CASCADE_SCALE_IMAGE, Size(min_face, min_face))`：
     多尺度掃描。`scale_factor` 是金字塔每層縮小比例，`min_neighbors` 是幾個相鄰視窗都說有臉才算。
  4. 座標除以 scale 換回原圖。
  5. `merge_overlaps()`（第 22-40 行）：Haar 常對同一張臉給大小兩個框；面積大的優先，之後的框只要「交集 / 小框面積」超過 `overlap_merge` 就丟。

### 2.6 `face_aligner.hpp` / `face_aligner.cpp` — 把臉轉正成 112x112

MobileFaceNet 訓練時的輸入是 ArcFace 標準樣板：兩眼固定在 (38.3, 51.7) 和 (73.5, 51.5)。
臉沒對齊，embedding 就不準，所以辨識前一定要做這一步。

- `face_aligner.cpp:9-16`：樣板眼睛座標、找不到眼睛時用臉框比例（x 30% / 70%，y 38%）估。
- `face_aligner.cpp:28-69` `find_eyes()`：只在臉框上 60% 找（第 31 行，避免把鼻孔嘴巴當眼睛）；
  左半、右半各取面積最大的候選（第 44-57 行）；合理性檢查：兩眼距離在臉寬 25%~65%，傾斜不超過約 25 度（第 65-67 行）。
- `face_aligner.cpp:71-99` `align()`：相似變換（旋轉 + 等比縮放 + 平移）。
  第 83-86 行由「實際兩眼向量」和「樣板兩眼向量」算 scale 和 angle；第 87-93 行組成 2x3 矩陣
  `[a -b tx; b a ty]`，tx/ty 讓左眼剛好落在樣板左眼；`warpAffine` 輸出 112x112，邊界用 `BORDER_REPLICATE` 補。

### 2.7 `face_embedder.hpp` / `face_embedder.cpp` — MobileFaceNet → 128 維向量

- `face_embedder.cpp:7-25` `load()`：`net.opt.num_threads`、`lightmode=true`（推論途中釋放用不到的中間結果，板子記憶體小），
  `load_param` + `load_model`。
- `face_embedder.cpp:27-46` `embed()`：
  - `ncnn::Mat::from_pixels(data, PIXEL_BGR2RGB, w, h)`：OpenCV 是 BGR，InsightFace 模型吃 RGB。
  - 不用減平均：這個 param 檔第一層已含 `(x-127.5)*0.0078125`（註解第 29-30 行）。
  - `create_extractor` → `input("data")` → `extract("fc1")`：ncnn 的標準三步。
  - `out.reshape(w*h*c)` 攤平（ncnn 每個 channel 有對齊 padding，reshape 會處理），複製成 `vector<float>` 後 `l2_normalize`。
- `face_embedder.cpp:48-63`：`l2_normalize`（向量除以長度）和 `cosine_similarity`（兩個單位向量的內積，範圍 -1~1，越大越像）。

### 2.8 `gallery.hpp` / `gallery.cpp` — 登錄資料與比對規則

- `Person`：`label`（學號）、`samples`（每張登錄照片的 embedding）、`mean`（平均後再 L2 正規化 = 範本）。
- `gallery.cpp:23-35` `finalize()`：算每個人的平均範本。用平均而不是逐張比，對光線 / 角度比較穩。
- `gallery.cpp:37-77` `save()` / `load()`：`cv::FileStorage` 存成 `gallery.yml`，每個人一個 N x 128 的矩陣。
- `gallery.cpp:79-100` `match(embedding, threshold, margin)`：對每個人算 cosine，記最高（best）和第二高（second）。
  **判定規則（第 97 行）：`best >= threshold` 且 `best - second >= margin` 才回傳 index，否則 -1（unknown）。**
  `margin` 是為了兩位組員長得像時不要亂猜。

### 2.9 `recognizer.hpp` / `recognizer.cpp` — 背景辨識 thread

和 Lab 2 的 `ScreenshotWriter` 同一個 pattern：工作佇列 + mutex + condition_variable。

- `RecogJob`：`track_id`、`region`（臉附近的一小塊影像，**已 clone**，主執行緒改原圖不影響）、`face`（臉框在 region 內的座標）。
- `RecogResult`：`track_id`、`label`（gallery index 或 `LABEL_UNKNOWN`）、`score`、`millis`。
- `recognizer.cpp:31-40` `push()`：佇列滿 4 個（`MAX_JOBS`）就丟掉回 false。板子沒 swap，佇列一定要有上限。
- `recognizer.cpp:42-48` `take_results()`：`out.swap(results)` 一次把所有完成的結果拿走。
- `recognizer.cpp:50-79` `loop()`：`cond.wait` 直到有工作或要停 → 取一個 job → `aligner.align` → `embedder.embed` → `gallery->match` → 結果推進 `results`。
- `finish()`（第 21-29 行）：設 `stopping`、喚醒、`join`。

### 2.10 `tracker.hpp` / `tracker.cpp` — 讓框和標籤穩定

Haar 每幀各自獨立偵測，框會抖、偶爾漏一幀、同一張臉有時兩個框。Tracker 把「每幀的偵測框」對應到「同一個人（track）」。

- `Track` 欄位（`tracker.hpp:16-30`）：`box`（平滑後）、`hits`（累計被偵測幀數，`>= min_hits` 才畫）、`missed`、`last_seen`、
  `votes`（最近幾次辨識 (label, score)）、`shown_label`（畫面上顯示的，-2 PENDING 顯示 "..."、-1 UNKNOWN、>=0 組員 index）、`pending`（辨識進行中）。
- `tracker.cpp:56-119` `update(detections, now)`，五步：
  1. 第 58-74 行：列出所有可能配對 (track, detection)。條件：IoU ≥ `iou_match` **或** 中心距離 ≤ `center_match` 個臉寬
     （FPS 低、人動得快時 IoU 會接近 0，靠中心距離補救）；大小差超過 2 倍不配。距離近的先配（greedy）。
  2. 第 82-95 行：配對成功 → 更新框。小幅變動用 EMA 平滑（`new = α·det + (1-α)·old`）消抖；移動超過 `snap_distance` 個臉寬就直接跳過去，框才不會拖在臉後面。
  3. 第 97-103 行：沒配到的 track `missed++`；超過 `track_timeout` **秒**（用時間不用幀數，不受 FPS 影響）才刪。
  4. 第 105-115 行：沒配到的偵測框 → 新 track。
  5. `merge_overlapping_tracks()`（第 121-146 行）：兩個 track 重疊太多視為同一張臉，留 hits 多的。
- `tracker.cpp:148-176` `add_vote(track_id, label, score)`：結果進 `votes`（最多 `vote_window` 個）。
  第一次直接顯示（讓標籤盡快出現）；之後多數決 + 遲滯：新標籤票數要 ≥ `vote_min` 且比目前顯示的多才換。
- `tracker.cpp:178-196` `resolve_duplicates()`：兩個框都顯示同一個組員 → 平均相似度高的留，另一個改 unknown。

### 2.11 `main.cpp` — 三種模式

| 模式 | 函式 | 用途 |
|---|---|---|
| `./face3 enroll <dir>` | `run_enroll` (95-163) | 建 `gallery.yml`。子資料夾名 = 學號（`parent_name()` 第 85-90 行）。對每張圖跑 `StillPipeline::process`（偵測最大臉 → 對齊 → embedding），對齊結果存到 `aligned/` 供肉眼檢查；最後印同一人 / 不同人的相似度統計，幫你決定 `sim_threshold` |
| `./face3 score <dir>` | `run_score` (168-205) | 對一個資料夾（例如陌生人照片）每張印 best / second / 判定，用來校準門檻 |
| `./face3 run`（預設） | `run_live` (240-373) | 即時辨識 |

`run_live` 主迴圈（第 278-365 行）每幀做：

1. `camera.wait_frame` 拿最新一幀（第 282 行）。
2. `detector.detect` → `tracker.update`（第 290-293 行）。
3. `recognizer.take_results` → 每個結果 `tracker.add_vote`；再 `resolve_duplicates`（第 296-303 行）。
4. 第 306-329 行：對每個可見、沒在辨識中、這幀有被偵測到、距上次辨識超過 `recog_interval` 的 track：
   把臉框外擴 30%（第 315-316 行）的區域 `clone()` 成 `RecogJob` push 給背景 thread。
5. `draw_tracks`（第 217-238 行）畫框和標籤；`fb.show`（第 338 行）。
6. `read_key`：`q` 離開、`s` 存 `snapshot_N.jpg`。
7. 每 30 幀印一次各階段平均耗時（wait / detect / track / draw / display / latency / recog）— 找瓶頸用。

### 2.12 `Makefile` / `run.sh` / `scripts/`

- `Makefile`：`CXX` 沒指定就用 `arm-linux-gnueabihf-g++`；`-march=armv7-a -mfpu=neon -mfloat-abi=hard -fopenmp`；
  連結 `-lncnn -lopencv_world`。`-Wl,--allow-shlib-undefined` 是因為 libopencv_world 當初開了 Qt，連結時會找 libQt5，
  我們沒用到視窗功能，留到板子上再解析。
- `run.sh`：`cd` 到自己的資料夾、`LD_LIBRARY_PATH=.`、執行 `face3`。板子上要把 `libopencv_world.so.3.4` 和 `libgomp.so.1` 放旁邊。
- `scripts/build_ncnn.sh`：clone ncnn 20240410，用 `toolchains/arm-linux-gnueabihf.toolchain.cmake` 交叉編譯（Vulkan 關、OpenMP 開），安裝到 `build-armhf/install`。
- `scripts/get_models.sh`：抓 OpenCV 的兩個 Haar cascade 和 liguiyuan/mobilefacenet-ncnn 的模型。

## 3. helmet/ 逐檔導讀

### 3.1 `config.hpp` / `config.cpp`

同 face。重點參數：`input_size 640`（匯出模型時固定）、`conf_threshold 0.50`（自備驗證集上 F1 最高）、
`nms_iou 0.45`、`contain 0.70`、`tile_grid 2`、`tile_overlap 0.2`、`tile_max_box 32`、`fb_device /dev/fb2`、`wait_enter 1`。

### 3.2 `helmet_detector.hpp` / `helmet_detector.cpp` — 核心

- `Box`：`x1,y1,x2,y2,score`，原圖座標。
- `load()`（第 8-24 行）：同 face embedder，載入 `helmet.ncnn.param/.bin`。
- `detect(bgr)`（第 26-71 行）＝ **跑一次模型**：
  1. 第 30-36 行 letterbox：等比例縮到最長邊 640，其餘補灰（114）成 640x640（和訓練時一樣，人不會被壓扁）。
     記下縮放比 `r` 和補邊 `px, py`。
  2. 第 38-42 行：`from_pixels(PIXEL_BGR2RGB)`、`substract_mean_normalize(mean=0, norm=1/255)` → 0~1。
  3. 第 44-48 行：`input("in0")` / `extract("out0")`，`pass_count++`。
  4. 第 50-69 行解碼輸出：`out` 是 5 x N，每一欄一個候選框 `(cx, cy, w, h, score)`（模型只有一個類別，所以沒有 class 維度；這個匯出版本不含 NMS，重複框由 `merge_boxes` 去掉）。
     分數低於 `conf_threshold` 丟掉；座標「先扣補邊，再除以 r」換回原圖（第 63-66 行）。
- `find_helmets(bgr)`（第 73-116 行）＝ 整張 + 切塊 + 合併：
  1. 第 77 行：整張圖跑一次（近的、大的人）。
  2. 第 81-106 行：圖的短邊 ≥ 2×640 才切塊（小圖放大會把臉誤判成帽子）。
     `make_tiles()`（第 118-133 行）算 n x n 個重疊 20% 的矩形；每塊各跑一次 `detect`。
     切塊的框兩個過濾（第 95-99 行）：
     - `cut`：框貼到切塊的邊、而那條邊不是原圖的邊 → 帽子被切到一半，丟掉（交給整張圖或鄰塊）。
     - `small`：框在「整張圖縮到 640 的尺度」下要小於 `tile_max_box` px 才收。大的帽子整張圖已經抓到，切塊只補遠處的小目標。
     通過的框加上 `tile.x / tile.y` 換回原圖座標。
  3. 第 108 行 `merge_boxes()`（第 154-182 行）：按分數排序 →
     (1) NMS：和已保留框 IoU > `nms_iou` 就丟（同一頂帽子被整張圖和切塊各框一次）；
     (2) 包含：交集 > `contain` × 小框面積 就丟（IoU 不高但其實是同一人，一人只留一框，F1 才不會被重複框扣分）。
  4. 第 109-114 行裁到圖內。

### 3.3 `main.cpp`

- 用法（第 3-8 行）：`./run.sh <照片> <組別> [輸出資料夾]` → `lab3_final_<組別>.jpg`。輸出資料夾給 SD 卡路徑就直接存到卡上。
- 流程：`cfg.load` → `detector.load` → `imread`（原解析度，依 EXIF 轉正） → `find_helmets` → 印每個框 →
  `draw_boxes`（第 33-43 行，線寬依圖片大小調整）→ `imwrite` 品質 95（原尺寸存，不變形）→ **`sync()`**（第 88 行，確保寫進 SD 卡再拔）→
  印各階段時間和模型跑了幾次（`passes()`）→ `fb.show` 等比例顯示在 HDMI → `wait_enter` 時等 Enter 才結束（畫面才會留著給 TA 看）。

### 3.4 `framebuffer.*`、`Makefile`、`run.sh`

和 face 幾乎相同；`Makefile` 的 `NCNN_DIR` 指到 `../face/third_party/ncnn/build-armhf/install`，
**所以 helmet/ 和 face/ 要維持在同一個上層資料夾才能編**。`OPENCV_DIR` 預設 `/usr/local/arm-opencv/install`。

## 4. 編譯與執行速查

```sh
# 開發機：編譯
cd lab3/face   && make OPENCV_DIR=/usr/local/arm-opencv/install
cd lab3/helmet && make

# 板子（SD 卡或 zmodem 把 face3/helmet、config.yml、models/、libopencv_world.so.3.4、libgomp.so.1 放同一資料夾）
./run.sh enroll enroll/          # face：enroll/<學號>/*.jpg → gallery.yml，看印出的相似度統計
./run.sh score strangers/        # face：確認陌生人 best 都低於 sim_threshold
./run.sh                         # face：即時辨識（q 離開、s 截圖）
./run.sh /run/media/mmcblk1p1/hidden.jpg 30 /run/media/mmcblk1p1   # helmet：輸出 lab3_final_30.jpg 到 SD 卡
```

## 5. 看程式時會碰到的名詞

| 名詞 | 一句話 |
|---|---|
| Haar cascade | 用很多「亮暗矩形差」的弱分類器串成的傳統人臉偵測器，快但會誤判、框會抖 |
| embedding | 把一張臉變成一個 128 維向量，同一個人的向量距離近 |
| cosine similarity | 兩個單位向量的內積，1 最像、0 無關 |
| gallery | 登錄過的人的範本集合 |
| EMA | 指數移動平均，`new = α·x + (1-α)·old`，用來把框的座標平滑 |
| IoU | 交集 / 聯集，衡量兩個框有多重疊 |
| NMS | Non-Maximum Suppression：重疊太多的框只留分數最高的 |
| letterbox | 等比例縮放再補邊到固定大小，避免變形 |
| tiling | 大圖切塊各跑一次模型，補救小目標縮到 640 後看不到的問題 |
| F1 | 2·P·R/(P+R)：final demo 評分方式，漏框和多框都扣分，所以門檻和去重很重要 |
| mutex / condition_variable | 多執行緒的鎖和「有事再叫醒我」的機制，camera / recognizer 都靠它 |
| mmap framebuffer | 把顯示記憶體映射到程式位址空間直接寫，比 write() 快很多（Lab 2-3 學到的） |
