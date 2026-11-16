# Lab 3-2 安全帽偵測（helmet）原始碼完整導讀

`README.md` 是「怎麼編譯、怎麼在板子上跑、demo 流程」；這份是「程式為什麼這樣寫」。
所有行號對應 `src/` 底下的檔案。

---

## 0. 先建立全貌

題目：給一張照片（最大 6000x4000），把「有戴安全帽的人」框出來，存成 `lab3_final_<組別>.jpg`，顯示在 HDMI。評分用 F1，所以漏框和多框都會扣分。

整個程式只有 **4 個模組、7 個檔案**：

```
helmet/
├── src/main.cpp              主流程（110 行）：讀設定 → 載模型 → 讀圖 → 偵測 → 畫框 → 存檔 → 顯示
├── src/helmet_detector.hpp   Box 結構 + HelmetDetector 類別宣告（42 行）
├── src/helmet_detector.cpp   核心：letterbox、跑 ncnn、解碼、切塊、NMS（182 行）
├── src/config.hpp / .cpp     從 config.yml 讀參數（30 + 40 行）
├── src/framebuffer.hpp / .cpp  mmap 寫 HDMI framebuffer（Lab 2-3 的升級版）（37 + 118 行）
├── config.yml                參數
├── Makefile / run.sh         交叉編譯 / 板子上執行
└── models/helmet.ncnn.param + .bin   YOLO26s 模型（結構 + 權重）
```

資料流一張圖：

```
照片 (例 3000x2000 BGR)
   │
   ▼  HelmetDetector::find_helmets
   ├── detect(整張)            ──┐
   ├── detect(左上塊)            │  每次 detect = letterbox 640 → ncnn → 解出 (cx,cy,w,h,score) → 換回原圖座標
   ├── detect(右上塊)            │
   ├── detect(左下塊)            │
   └── detect(右下塊)          ──┘
           │ 所有框丟在同一個 vector
           ▼
   merge_boxes：按分數排序 → NMS（IoU）→ 包含去重
           │
           ▼
std::vector<Box>（每個人一個框，原圖座標）
   │
   ▼  main
draw_boxes → imwrite → sync → Framebuffer::show → 等 Enter
```

---

## 1. `main.cpp` 逐段

### 第 1-8 行：用法

```
./run.sh <照片.jpg> <組別> [輸出資料夾]
```
`run.sh` 只是 `cd` 到自己的資料夾、設 `LD_LIBRARY_PATH=.`（讓板子找到旁邊的 `libopencv_world.so.3.4`、`libgomp.so.1`），再執行 `./helmet "$@"`。

### 第 25-30 行 `now_seconds()`

`gettimeofday` 回傳秒 + 微秒，合成一個 double。只是拿來算各階段耗時。

### 第 32-43 行 `draw_boxes(img, boxes)`

```cpp
int t = max(2, lround(max(img.cols, img.rows) / 600.0));
```
線寬依圖片大小決定：1024 寬 → 2 px；6000 寬 → 10 px。因為 6000x4000 的圖縮到 1920x1080 顯示時會縮 3 倍，線太細就看不到。
每個框：`cv::rectangle` 畫綠框，`cv::putText` 在框上方寫 `helmet`，字的大小和粗細也跟 `t` 走。

### 第 45-57 行：參數與設定

- `argv[1]` 照片路徑、`argv[2]` 組別（只是拼進檔名）、`argv[3]` 輸出資料夾（預設 `.`；demo 時給 SD 卡路徑就直接存到卡上）。
- 第 54 行拼出 `out_name = <dir>/lab3_final_<組別>.jpg`。
- `cfg.load("config.yml")`：注意是相對路徑，所以一定要在 helmet 資料夾裡執行（run.sh 的 `cd` 就是為了這個）。

### 第 59-62 行：載模型

`HelmetDetector detector; detector.load(cfg)` 讀 param + bin。19 MB 的權重在板子上要 1 秒多，所以單獨計時 `t_load`。

### 第 64-71 行：讀圖

`cv::imread(photo, IMREAD_COLOR)`：讀成 3 通道 BGR，**原始解析度**（不縮），而且 OpenCV 會依 EXIF 方向自動轉正（手機直拍的照片不會躺著）。印出 `image WxH`。

### 第 73-77 行：偵測

`detector.find_helmets(img)` 回傳 `std::vector<Box>`，每個元素是一個戴安全帽的人，`x1 y1 x2 y2` 是原圖座標、`score` 是信心。逐一印出來，方便和訓練機的結果對照。

### 第 79-89 行：畫框、存檔

- `draw_boxes` 直接畫在 `img` 上（原圖被改掉，但後面只需要畫好框的版本）。
- `imwrite` 用 `IMWRITE_JPEG_QUALITY = 95`（config 可調），**原尺寸存檔**，作業要求輸出不能變形或縮小。
- 第 88 行 `sync()`：強制把檔案系統快取寫進 SD 卡。沒有這行，直接拔卡可能拿到 0 byte 的檔案。

### 第 91-94 行：印時間

`load / read / detect / save / total` 和 `detector.passes()`（模型跑了幾次：小圖 1 次，大圖 5 次）。`fflush` 是因為序列埠輸出有緩衝。

### 第 96-108 行：顯示

`Framebuffer::open("/dev/fb2")` → `show(img)` 等比例縮放置中 → 若 `wait_enter` 就 `std::cin.get()` 等 Enter。
等 Enter 的原因：程式結束後 framebuffer 的內容其實還在，但等著比較保險，而且助教拍照時程式還活著。

---

## 2. `helmet_detector.hpp`

```cpp
struct Box { float x1, y1, x2, y2; float score; };   // 第 10-14 行，原圖座標的一個框
```

```cpp
class HelmetDetector {
public:
    bool load(const Config&);                    // 載模型
    std::vector<Box> find_helmets(const cv::Mat&);   // 對外唯一入口
    int passes() const;                          // 上一次 find_helmets 跑了幾次模型
private:
    std::vector<Box> detect(const cv::Mat&);     // 對「一張圖」跑一次模型
    ncnn::Net net;        // 第 31 行：ncnn 的模型物件，權重載進這裡
    Config cfg;
    int pass_count;
};
```

兩個自由函式：
- `make_tiles(w, h, n, overlap)`：算 n x n 個切塊的矩形。
- `merge_boxes(boxes, nms_iou, contain)`：去重。

設計上 `detect` 是 private：外面不該直接呼叫「只跑一次」的版本，一律走 `find_helmets`。

---

## 3. `helmet_detector.cpp` 逐段（最重要的檔案）

### 3.1 `load()`（第 8-24 行）

```cpp
net.opt.num_threads = cfg.ncnn_threads;   // 4：板子四核全開
net.opt.lightmode = true;                 // 推論途中釋放用不到的中間結果，板子只有 2 GB 又沒 swap
net.load_param("models/helmet.ncnn.param");   // 網路結構（文字檔，322 行，每行一層）
net.load_model("models/helmet.ncnn.bin");     // 權重（19 MB，fp16 存，載入時轉 fp32）
```
任何一步失敗印錯誤回 false，main 就結束。

### 3.2 `detect()`（第 26-71 行）：一張圖跑一次模型

**用 3000x2000 的照片當例子帶數字。**

#### (a) letterbox，第 30-36 行

模型輸入固定 640x640。直接拉成 640x640 會把人壓扁（寬縮 4.7 倍、高縮 3.1 倍），訓練時沒看過壓扁的人，分數會掉。所以「等比例縮小 + 補灰邊」：

```cpp
double r = min(640.0/3000, 640.0/2000);  // = min(0.2133, 0.32) = 0.2133，取小的才塞得進去
int nw = lround(3000 * r);               // 640
int nh = lround(2000 * r);               // 427
int px = (640 - 640) / 2;                // 0   左右補邊
int py = (640 - 427) / 2;                // 106 上下各補 106 列
cv::Mat padded(640, 640, CV_8UC3, Scalar(114,114,114));   // 全灰的畫布
cv::resize(bgr, padded(Rect(px, py, nw, nh)), Size(nw, nh), 0, 0, INTER_AREA);
```

`padded(Rect(...))` 是指向畫布中間那塊的「視窗」（不複製），`resize` 把縮小後的圖直接寫進去。
結果：中間 640x427 是照片，上下各 106 列灰色。114 是 YOLO 訓練時用的補邊顏色。
`INTER_AREA` 是縮小時品質最好的插值法。

**記住 r、px、py，最後要反著算回去。**

#### (b) 轉成 ncnn 輸入，第 38-42 行

```cpp
ncnn::Mat in = ncnn::Mat::from_pixels(padded.data, PIXEL_BGR2RGB, 640, 640);
in.substract_mean_normalize(mean={0,0,0}, norm={1/255,1/255,1/255});
```
- OpenCV 像素是 BGR，模型訓練時吃 RGB → `PIXEL_BGR2RGB` 邊複製邊換順序。
- 0~255 → 0~1：每個值乘 1/255，mean 是 0 代表不減平均（這個模型訓練時就是只除 255）。
- `ncnn::Mat` 是 ncnn 自己的陣列型別，欄位 `w h c data`，和 `cv::Mat` 不同。

#### (c) 跑模型，第 44-48 行

```cpp
ncnn::Extractor ex = net.create_extractor();   // 這一次推論的工作區（ncnn 函式庫提供，不是我們寫的）
ex.input("in0", in);                           // 餵進輸入層（名字在 param 檔第 3 行）
ncnn::Mat out;
ex.extract("out0", out);                       // 真正的推論在這行：從 in0 一路算到 out0
pass_count++;
```
`Net` 存權重（可重複用），`Extractor` 存這一次的中間結果，跑完丟掉就釋放記憶體。
板子上這一行約 5 秒（4 核 NEON，fp32）。

#### (d) 輸出格式與解碼，第 50-69 行

`out` 是 **5 列 x 8400 欄** 的 float 表（`out.h == 5`，`out.w == 8400`）：

```
row 0  cx     [cx0  cx1  ...  cx8399]     框中心 x（640x640 letterbox 圖上的座標）
row 1  cy     [cy0  cy1  ...  cy8399]     框中心 y
row 2  w      [w0   w1   ...  w8399 ]     框寬
row 3  h      [h0   h1   ...  h8399 ]     框高
row 4  score  [s0   s1   ...  s8399 ]     信心 0~1（已過 sigmoid）
                ↑ 一個候選框的五個數字是直的取
```

8400 哪來的：YOLO 把 640x640 切成 80x80、40x40、20x20 三層格子，每格一個候選框，6400+1600+400 = 8400。
可在 `models/helmet.ncnn.param` 最後幾行看到 `Reshape ... 0=8400`、`Sigmoid`、`Concat ... out0`（把 4 列座標和 1 列分數疊成 5 列）。
因為模型只有「戴安全帽的人」一個類別，所以沒有 class 那一列。

`out.row(k)` 回傳第 k 列開頭的 `float*`，所以 `cx[i]` 是第 i 個候選框的中心 x。

迴圈（第 58-69 行）對 8400 個候選：
1. `score[i] < 0.50` 跳過（絕大多數都在這裡被丟掉，通常剩個位數）。
2. 中心 + 寬高 → 左上、右下：`cx ± w/2`、`cy ± h/2`。
3. **扣補邊、除以 r** 回到原圖：

假設模型說 cx=320, cy=200, w=100, h=80：
```
x1 = (320 - 50 - 0)   / 0.2133 = 1266
y1 = (200 - 40 - 106) / 0.2133 =  253     ← 先扣掉上面 106 列灰邊
x2 = (320 + 50 - 0)   / 0.2133 = 1735
y2 = (200 + 40 - 106) / 0.2133 =  629
```
這個框在 3000x2000 原圖上是 (1266,253)-(1735,629)。(a) 怎麼變形，(d) 就反著做回來。

### 3.3 `find_helmets()`（第 73-116 行）：整張 + 切塊 + 合併

#### 第 77 行：整張圖先跑一次
抓近的、大的人。

#### 第 79-106 行：大圖再切 2x2 各跑一次

為什麼：6000x4000 縮到 640x427，遠處的人頭可能只剩 5 px，模型看不到。切成四塊，每塊 3300x2200 再縮到 640，同一個人頭變成 10 px 左右，就看得到了。

條件（第 82 行）：`tile_grid >= 2` 且 `min(w,h) >= 2 * 640 = 1280`。小圖不切，因為把小圖放大會把臉、肩膀之類的誤判成帽子。

`make_tiles(w, h, 2, 0.2)`（第 118-133 行）算四個矩形，相鄰重疊 20%。以 3000x2000 為例：

```
tw = 3000 / (2 - 1*0.2) = 1667    每塊寬      sx = 1667 * 0.8 = 1333   每塊起點間距
th = 2000 / 1.8        = 1111    每塊高      sy = 1111 * 0.8 =  889
塊 0: (0,    0)   1667x1111        塊 1: (1333, 0)   1667x1111
塊 2: (0,  889)   1667x1111        塊 3: (1333, 889) 1667x1111
```
公式 `n*tw - (n-1)*overlap*tw = w` 的意思：n 塊的總寬扣掉 n-1 段重疊剛好等於圖寬。重疊是為了在邊界上的人至少在某一塊是完整的。

對每塊 `detect(bgr(tile))`：`bgr(tile)` 是子影像視窗（不複製），回來的框座標是**塊內座標**。

兩個過濾（第 95-99 行）：

| 條件 | 意思 | 為什麼 |
|---|---|---|
| `cut` | 框貼到切塊的邊（≤ 2 px），而且那條邊不是原圖的邊 | 人被切到一半，框不完整。丟掉，交給整張圖或相鄰那塊（重疊 20% 就是讓他在鄰塊完整） |
| `!small` | 框在「整張圖縮到 640」的尺度下 ≥ 32 px | 大的人整張圖已經抓到了，切塊再給一次只會多出重複框（還要靠 NMS 救）；切塊只負責補「小於 32 px 的遠處小目標」 |

第 98 行的 `r` 是整張圖的縮放比（`min(640/w, 640/h)`），`max(框寬, 框高) * r` 就是這個框在整張圖縮到 640 時有多大。

通過的框 `+= tile.x / tile.y` 換回原圖座標（第 100-103 行），加進同一個 `boxes`。

#### 第 108 行：`merge_boxes()`（第 154-182 行）

此時 `boxes` 裡混著整張圖和四塊的結果，同一個人可能有 2 到 3 個框。

1. 第 157 行：按 score 由高到低排（`stable_sort` 讓同分時順序固定）。
2. **NMS**（第 160-167 行）：逐一看，和「已保留」的任何一框 IoU > 0.45 就丟。
   IoU = 交集 / 聯集（`box_iou` 第 147-152 行）。同一頂帽子被兩次 detect 各框一次，兩框幾乎重疊，IoU 很高，留分數高的那個。
3. **包含去重**（第 170-180 行）：交集 > 0.70 × 小框面積 就丟。
   處理 IoU 不高但其實是同一人的情況：例如一個框住整個頭、一個只框帽子，小框幾乎整個在大框裡，IoU 可能只有 0.3，NMS 不會去掉，但 F1 會當成一個多餘的 FP。

#### 第 109-114 行：裁到圖內
切塊邊界附近或 letterbox 誤差可能讓座標略為超出，`max(0, …)`、`min(w, …)` 修掉。

---

## 4. `config.hpp` / `config.cpp`

`Config` 是一個 struct，欄位和預設值都在 `config.hpp`（每個旁邊有註解）。
`config.cpp` 第 7-12 行的 `read_field()`：`fs[name]` 不是空才覆蓋，所以 yml 可以只寫想改的欄位。
用 `cv::FileStorage` 讀 YAML 是 OpenCV 內建功能，不用另外裝 yaml 函式庫。

| 參數 | 值 | 影響哪一行 |
|---|---|---|
| `input_size` | 640 | detect (a) 的 S，匯出模型時固定，不能改 |
| `ncnn_threads` | 4 | load 第 11 行 |
| `conf_threshold` | 0.50 | detect 第 60 行；在自備驗證集上 F1 最高 |
| `nms_iou` | 0.45 | merge_boxes 第 165 行 |
| `contain` | 0.70 | merge_boxes 第 177 行 |
| `tile_grid` / `tile_overlap` / `tile_max_box` | 2 / 0.2 / 32 | find_helmets 第 81-99 行 |
| `fb_device` | /dev/fb2 | main 第 97 行（fb0 是 LCD） |
| `jpeg_quality` | 95 | main 第 82 行 |
| `wait_enter` | 1 | main 第 101 行 |

---

## 5. `framebuffer.cpp`（Lab 2-3 的 mmap 版升級）

- `open()` 第 14-54 行：`open` → `ioctl(FBIOGET_VSCREENINFO)` 拿 xres/yres/bpp/`red.offset`，`ioctl(FBIOGET_FSCREENINFO)` 拿 `line_length`（一列實際幾 byte）和 `smem_len` → `mmap` → 寫 `/sys/class/graphics/fb2/blank` = 0 把螢幕叫醒 → 清黑。
- `convert()` 第 67-80 行：依 bpp 選轉色。HDMI fb2 是 32 bpp，所以走 `BGR2BGRA`（或 `red_offset == 0` 時 `BGR2RGBA`）；LCD 16 bpp 走 `BGR2BGR565`（Lab 2 的做法）。
- `show()` 第 82-118 行：
  - 第 87-94 行等比例縮放：6000x4000 → scale = min(1920/6000, 1080/4000) = 0.27 → 1620x1080，置中後左右各留 150 px 黑邊。
  - 第 96-100 行：尺寸變了才清畫面。
  - 第 113-117 行：一列一列 `memcpy` 到 `mem + (y + y_offset) * line_length + x_offset * bytes_per_pixel`。這和 Lab 2-3 的寫法完全一樣，只是 bytes_per_pixel 變 4。

---

## 6. `Makefile`

```make
ifeq ($(origin CXX),default)      # 沒人在命令列指定 CXX 才換成交叉編譯器
CXX := arm-linux-gnueabihf-g++
endif
OPENCV_DIR ?= /usr/local/arm-opencv/install                        # Lab 2 編的 ARM OpenCV
NCNN_DIR   ?= $(CURDIR)/../face/third_party/ncnn/build-armhf/install   # 借 face 的 ncnn
CXXFLAGS := -O2 -std=c++11 -march=armv7-a -mfpu=neon -mfloat-abi=hard -fopenmp -I... 
LDFLAGS  := -L... -fopenmp -Wl,--allow-shlib-undefined
LDLIBS   := -lncnn -lopencv_world -lpthread -ldl
```

- `-mfpu=neon`：讓 ncnn 用 ARM 的 SIMD 指令，沒有它會慢好幾倍。
- `-fopenmp`：ncnn 的多執行緒靠 OpenMP，所以板子上要有 `libgomp.so.1`。
- `--allow-shlib-undefined`：`libopencv_world` 當初編譯時開了 Qt，連結會找不到 libQt5 符號；我們沒用視窗功能，叫連結器別管，板子上執行時再解析。
- `src/*.cpp` → `build/*.o` → `helmet`。改 `.hpp` 會全部重編（`src/*.hpp` 是每個 .o 的相依）。

---

## 7. 模型檔案

- `helmet.ncnn.param`（文字檔，322 行）：第 1 行 magic `7767517`，第 2 行 `320 382` = 320 層、382 個 blob，之後每行一層：`層型別 層名 輸入數 輸出數 輸入blob 輸出blob 參數`。第 3 行 `Input in0` 就是 `ex.input("in0")` 的名字，最後一行 `Concat ... out0` 就是 `ex.extract("out0")`。
- `helmet.ncnn.bin`（19 MB）：所有卷積權重，順序和 param 對應。
- 這兩個檔案是訓練機上用 Ultralytics 訓練 YOLO26s 後 `export format=ncnn` 產生的；板子只做推論。

---

## 8. 抽問可能的問題（附答案位置）

| 問題 | 答 | 看哪裡 |
|---|---|---|
| 為什麼不用 OpenCV dnn？ | 板子的 OpenCV 3.4.7 沒編 dnn，也沒 Python；ncnn 是純 CPU、支援 NEON，而且 3.1 已經編好了 | README.md 開頭 |
| 大圖為什麼不直接縮到 640？ | 遠處的人頭會剩幾個像素，模型看不到；所以大圖再切 2x2 | find_helmets 第 79-82 行 |
| 切塊會不會重複框同一個人？ | 會，靠 NMS + 包含去重合併；另外切塊只收小框、丟掉被切到的框，先減少重複 | 第 95-99、154-182 行 |
| letterbox 的 114 是什麼？ | YOLO 訓練時的補邊顏色，推論用一樣的 | detect 第 35 行 |
| 門檻 0.5 怎麼來的？ | 在自備驗證集上掃 0.3~0.7，F1 最高的區間；正式測試圖沒參與 | config.yml |
| 一張 6000x4000 要多久？ | 約 34 秒：模型 5 次 28 秒、讀圖 2 秒、存檔 3 秒 | README.md 實測表 |
| 輸出格式？ | 5 x 8400，每欄 (cx,cy,w,h,score) | detect 第 50-57 行 |
| 怎麼確定板子算的和電腦一樣？ | 同一個 ncnn 模型在訓練機跑，框差 ≤ 0.1 px | README.md 實測 |
| 為什麼要 sync()？ | 確保寫進 SD 卡再拔 | main 第 88 行 |
| 顯示會變形嗎？ | 不會，等比例縮放置中；存檔是原尺寸 | framebuffer show 第 87-89 行 |

---

## 9. 名詞

| 名詞 | 一句話 |
|---|---|
| YOLO | 一次看整張圖、直接輸出所有框的物件偵測模型；26s 是 Ultralytics 的小型版本 |
| ncnn | 騰訊的手機 / 嵌入式 CPU 推論引擎，沒有 GPU 也能跑 |
| blob | ncnn 裡一層的輸出張量，用名字（in0、out0）取用 |
| letterbox | 等比例縮放 + 補邊到固定大小，避免變形 |
| IoU | 交集 / 聯集，兩框多重疊 |
| NMS | Non-Maximum Suppression，重疊框只留分數最高的 |
| tiling | 大圖切塊各跑一次，補救小目標 |
| F1 | 2PR/(P+R)。P 精確率（框的裡面有幾個是對的）、R 召回率（該框的有幾個框到）；多框、漏框都扣 |
| fp16 / fp32 | 權重存 16 位元省空間，載入後用 32 位元計算 |
| NEON | ARM 的 SIMD 指令集，一次算 4 個 float |
