# Lab 3 — 3.2 戴安全帽偵測（helmet，板子上執行的 C++ 版）

讀一張照片（最大 6000×4000）→ YOLO26s（ncnn）只框「有戴安全帽的人」→ 存成 `lab3_final_<組別>.jpg`（原尺寸）→ 等比例顯示到 HDMI。

板子的 OpenCV 3.4.7（Lab 2 交叉編譯的 `libopencv_world.so.3.4`）沒有 dnn 模組、Python 也沒有 cv2，所以推論用 ncnn（和 3.1 `face3` 同一份 ncnn），讀圖/縮放/畫框/存檔用 OpenCV。

## 三台機器的分工

| 機器 | 用途 |
|---|---|
| 訓練機 | 訓練 YOLO26s、匯出 ncnn 模型（`models/helmet.ncnn.param/.bin`）、在驗證集上算 F1 挑門檻 |
| Ubuntu 建置主機（`wayne-TUF-Gaming-FX505DT`） | `arm-linux-gnueabihf-g++` **交叉編譯**；minicom（`/dev/ttyUSB0`，115200）連板子 |
| E9V3（i.MX6Q，4× Cortex‑A9 1 GHz，2 GB） | **執行** `helmet` |

## 1. 編譯（在 Ubuntu 建置主機）

這個資料夾放在 `face/` 旁邊（共用 `face/third_party/ncnn` 編好的 ncnn）：

```bash
cd lab3/helmet
make            # 預設 OPENCV_DIR=/usr/local/arm-opencv/install，NCNN_DIR=../face/third_party/ncnn/build-armhf/install
```

## 2. 板子上的檔案

```
/root/helmet/
├── helmet                     ← 執行檔
├── run.sh
├── config.yml
├── libopencv_world.so.3.4     ← cp /root/lab3/libopencv_world.so.3.4 .
├── libgomp.so.1               ← cp /root/lab3/libgomp.so.1 .
└── models/
    ├── helmet.ncnn.param
    └── helmet.ncnn.bin        ← 19 MB（權重存 fp16，載入時轉 fp32）
```

## 3. 執行

```bash
cd /root/helmet
./run.sh <照片.jpg> <組別> [輸出資料夾]     # 例：./run.sh hidden.jpg 8 → ./lab3_final_8.jpg
```

終端機印出每個框和各階段時間；畫面等比例置中顯示在 HDMI（`/dev/fb2`，1920×1080），按 Enter 結束（結束後畫面仍保留）。存檔後會 `sync`，可以直接拔 SD 卡。

實測（E9V3，2026-11-01）：

| 照片 | 模型跑幾次 | 讀圖 | 偵測 | 存檔 | 總共 |
|---|---|---|---|---|---|
| 1024×683 | 1（整張） | 0.1 s | 5.1 s | 0.1 s | 6.1 s |
| 3000×2000 | 5（整張 + 2×2 切塊） | 0.6 s | 26.2 s | 0.8 s | 28.4 s |
| 6000×4000（上限） | 5 | 2.2 s | 27.9 s | 3.1 s | **34.1 s** |

板子輸出的框和訓練機用同一個 ncnn 模型算的結果一致（差 ≤ 0.1 px）。

## 4. Demo 當天（11/5）：用 SD 卡傳檔

SD 卡（64 GB，FAT）插在板子上會自動掛載到 `/run/media/mmcblk1p1`。

1. 20:40 從 E3 下載照片，在筆電上用讀卡機把它存到 SD 卡根目錄，**改名成 `hidden.jpg`**（避免檔名有空白或中文）。
2. SD 卡插回板子，確認看得到：`ls /run/media/mmcblk1p1/hidden.jpg`
3. 執行（結果直接存回 SD 卡）：
   ```bash
   cd /root/helmet
   ./run.sh /run/media/mmcblk1p1/hidden.jpg <組別> /run/media/mmcblk1p1
   ```
4. HDMI 顯示結果後舉手給助教拍照、抽問；按 Enter 結束程式。
5. 拔 SD 卡回筆電，把 `lab3_final_<組別>.jpg` 上傳 E3（21:30 前）。

備案：沒有讀卡機時用 minicom zmodem 傳（Ctrl‑A S 傳進去、板子 `sz lab3_final_<組別>.jpg` 傳出來），約 11 KB/s，6000×4000 照片來回要 10–20 分鐘，只在不得已時用。

## 程式碼地圖（抽問時找程式碼用）

| 問題 | 檔案:函式 |
|---|---|
| 主流程：讀設定 → 載模型 → 讀圖 → 偵測 → 畫框 → 存檔 → 顯示 | `src/main.cpp` `main` |
| 畫綠框和 `helmet` 字、線寬怎麼決定 | `src/main.cpp` `draw_boxes` |
| 載入 ncnn 模型、執行緒數 | `src/helmet_detector.cpp` `HelmetDetector::load` |
| letterbox（等比例縮放 + 補灰邊 114，不變形） | `src/helmet_detector.cpp` `HelmetDetector::detect` 前半 |
| BGR→RGB、除以 255、送進模型 | `src/helmet_detector.cpp` `HelmetDetector::detect`（`from_pixels`、`substract_mean_normalize`） |
| 模型輸出 5×N 怎麼解（cx, cy, w, h, score）、信心門檻、換回原圖座標 | `src/helmet_detector.cpp` `HelmetDetector::detect` 後半 |
| 整張圖 + 2×2 切塊、什麼時候切、被切到的框怎麼丟 | `src/helmet_detector.cpp` `HelmetDetector::find_helmets` |
| 切塊座標怎麼算（重疊 20%） | `src/helmet_detector.cpp` `make_tiles` |
| NMS（去重疊框）、包含框（一人只留一框） | `src/helmet_detector.cpp` `merge_boxes`、`box_iou` |
| 顯示到 HDMI、等比例縮放置中、16/32 bpp 轉換 | `src/framebuffer.cpp` `Framebuffer::show` / `convert` |
| 所有參數 | `config.yml`、`src/config.hpp` |

## 參數（config.yml，不用重新編譯）

| 參數 | 值 | 說明 |
|---|---|---|
| `conf_threshold` | 0.50 | 信心門檻，在自備驗證集（val + eval_hwd2024 + 一般帽子 eval_hats）上 F1 最高的區間；正式測試圖沒有參與 |
| `nms_iou` | 0.45 | IoU 超過就當同一頂帽子的重複框 |
| `contain` | 0.70 | 小框 70% 以上落在另一框內 → 同一人重複框（重複框算 FP） |
| `tile_grid` | 2 | 短邊 ≥ 1280 的大圖再切 2×2 重疊塊各跑一次 |
| `tile_max_box` | 32 | 切塊只補「整張圖裡小於 32 px（模型尺度）」的遠處小帽子；近的大頭整張圖就抓得到，切塊放大後反而容易誤判 |
| `fb_device` | `/dev/fb2` | HDMI（`/dev/fb0` 是 LCD） |

Final demo 的測試圖是 6000×4000 的拼貼圖（很多張小照片拼在一起，每個人頭都很小），當天把 `tile_grid` 改成 4：
整張 + 4×4 切塊共 17 次推論，約 107 秒；框數從 2×2 的 71 個增加到 99 個。事前在同型的練習圖上逐框人工比對，
4×4 比 2×2 多 21 個正確框、少 4 個誤框。注意切塊條件是短邊 ≥ `tile_grid` × 640，所以 `tile_grid: 4` 只對短邊 ≥ 2560 的圖有效。
