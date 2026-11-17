# Lab 3 — 3.1 即時人臉辨識：技術選型報告

> 適用平台：Embedsky E9V3（NXP i.MX6Q）
> 撰寫日期：2026-10-29
> 狀態：已定案（2026-10-29）：偵測用 Haar cascade，辨識用 MobileFaceNet（ncnn）。§2 的比較表保留原始評估，供報告參考

---

## 0. 結論摘要

| 階段   | 選定方案                                                           | 一句話理由                                                                         |
| ---- | -------------------------------------------------------------- | ----------------------------------------------------------------------------- |
| 人臉偵測 | **Haar cascade**（OpenCV objdetect，`haarcascade_frontalface_default.xml`）〔組員決定，2026-10-29〕 | 現有的 OpenCV 3.4 world 就有，不用任何新依賴。漏偵測和閃爍的缺點靠追蹤時保留框、EMA 平滑來彌補；原本建議的 libfacedetection 留作升級選項 |
| 人臉對齊 | 雙眼 2 點相似變換 → 112×112（ArcFace 標準樣板的眼睛位置）     | Haar 沒有關鍵點：在臉框上半部用 `haarcascade_eye.xml` 找雙眼；找不到時，用臉框的固定比例估計眼睛位置 |
| 特徵萃取 | **MobileFaceNet**（預訓練，128 維 embedding）                         | 參數不到 100 萬、LFW 99.55%，是**專為手機和嵌入式設計**的模型中，準度和成本比例最好的                          |
| 推論框架 | **ncnn**（騰訊）                                                   | 板上的 OpenCV 3.4 **沒有編進 dnn**。ncnn 對 armv7 加 NEON 有最佳化，而且不用動已經能用的 OpenCV        |
| 身分判定 | 跟登錄的 embedding 算 cosine 相似度，加門檻和差距 → 學號或 `unknown`             | 開放集合問題：陌生人不會出現在訓練資料裡，**只能靠門檻拒絕**                                              |
| 穩定輸出 | IoU 追蹤、框座標 EMA 平滑、多幀標籤投票加遲滯                                    | 3.1.3 明確要求「不可抖動或閃爍」                                                           |
| 執行環境 | C++，和 Lab 2 一樣用 `libopencv_world.so.3.4` 加 `LD_LIBRARY_PATH=.` | 沿用已驗證的擷取、顯示流程                                                                 |
| 辨識備案 | **LBPH**（OpenCV contrib 或自行實作），偵測和對齊不變                         | 幾乎零運算成本、不用任何模型。但判定 `unknown` 和抗姿態變化較弱，比較見 §3.1                                |

---

## 1. 限制條件

### 1.1 板子（2026-10-29 在板上實測）

| 項目 | 實測結果 | 對選型的意義 |
|---|---|---|
| CPU | 4 × `ARMv7 Processor rev 10 (v7l)`，即 Cortex‑A9 | **沒有 NPU，GPU 也不能拿來推論**。所有運算都在 4 顆 A9 上，A9 的 NEON 是 64 位元資料路徑，效能弱 |
| 時脈 | **還沒確認**（`BogoMIPS 6.00` 是 timer 換算值，不代表時脈） | 估速度時暫時當作約 1 GHz |
| RAM | 2013 MB，可用 1819 MB，**Swap 0** | 記憶體不是瓶頸，但超過上限會被 OOM 直接殺掉 |
| 系統 OpenCV | `/usr/lib` 裡是 2.4.11 | Lab 3 不使用 |
| Lab 2 用的 OpenCV | `~/lab2/libopencv_world.so.3.4` | `grep _ZN2cv3dnn` 結果是 0，`CascadeClassifier` 是 166：**有 objdetect，沒有 dnn** |
| 交叉編譯器 | 組員確認可用 | 假設支援 C++11（ncnn 需要） |

**推論：** 不能用 OpenCV 4.5.4 以後才有的 `FaceDetectorYN` / `FaceRecognizerSF`，也不能用 OpenCV dnn。任何神經網路都必須用**獨立的推論引擎**或**純 C++ 實作**來跑。

### 1.2 速度參考（同等級硬體的公開數據）

找不到 i.MX6Q 本身的公開數據，以下用最接近的平台推估：

| 平台 | 模型 | 延遲 | 出處 |
|---|---|---|---|
| Zynq 7020，**Cortex‑A9 766 MHz × 2**，ncnn，2 執行緒 | mobilenet_v2（224×224） | 450 ms | ncnn benchmark [4] |
| 同上 | blazeface | 72 ms | 同上 |
| 同上 | mobilenet_ssd | 1354 ms | 同上 |
| Raspberry Pi 4（Cortex‑A72 1.5 GHz），4 執行緒 | libfacedetection 320×240 | 33 ms | libfacedetection README [2] |
| 同上 | libfacedetection 160×120 | 8 ms | 同上 |

**換算到 E9V3（4 × A9，約 1 GHz）的估計值，必須實測：**

- MobileFaceNet 的運算量約是 mobilenet_v2 的 0.7 倍（約 0.22 G vs 約 0.3 G MAdds），輸入又只有 112×112。用 4 執行緒估計 **每張臉約 100–250 ms**。
- A72 每個時脈的效能大約是 A9 的 2–3 倍，時脈也高 1.5 倍。所以 libfacedetection 在 320×240、4 執行緒下估計 **約 100–150 ms（7–10 fps）**；降到 160×120 估計約 25–40 ms。
- 物件偵測型的 SSD 模型（mobilenet_ssd）在 A9 上要 1 秒以上，**不能每幀都跑**。

### 1.3 資料集

| 項目 | 規模 | 來源 |
|---|---|---|
| 已知人物 | **2 人**（組員） | — |
| 登錄影像 | 每人約 20–30 張，涵蓋正面、左右轉約 30°、抬頭、低頭、有無眼鏡 | **用板子的攝影機拍**（Lab 2 的按 `c` 存圖功能），跟 demo 時的畫質一致 |
| 負樣本（校準門檻用） | 約 50–100 張其他人的臉 | 同學，或公開資料集（例如 LFW）抽樣 |

**推論：** 總共只有約 60 張正樣本，規模比大型人臉資料集（數十萬到數百萬張）**小 4 個數量級以上**。

- **從頭訓練**深度模型完全不可行。
- **Fine‑tune** 預訓練模型也幾乎一定會過擬合，而且陌生人資料無從取得。
- 正確做法是用 **預訓練 embedding 加登錄（enrollment）**：模型權重不動，資料只拿來**建立兩位組員的特徵範本**和**校準門檻**。

### 1.4 任務特性與評分

| 要求（講義 3.1） | 技術意義 |
|---|---|
| 3 張臉同時框出（10%） | 偵測器要能處理多張臉。召回率比極致精度重要 |
| 30 秒或 10 秒內辨識正確（各 10%） | 辨識延遲的預算很寬，**每張臉 0.1–0.3 秒完全夠用** |
| 「其他人」顯示 `unknown` | **開放集合辨識**：要能拒絕沒看過的人 |
| 框和標籤不可抖動或閃爍 | 需要追蹤、平滑和投票，**跟模型本身同樣重要** |

---

## 2. 人臉偵測選型

| 候選                                   | 模型規模           | 準度                                                       | 在 E9V3 上的可行性                                                       | 評估                                            |
| ------------------------------------ | -------------- | -------------------------------------------------------- | ------------------------------------------------------------------ | --------------------------------------------- |
| **libfacedetection（YuNet）**          | 75,856 個參數 [3] | WIDER Face AP easy/medium/hard 0.887 / 0.871 / 0.768 [2] | 純 C++、**沒有依賴**、支援 NEON；Pi 4 上 320×240 是 33 ms [2]；**有 5 個關鍵點** [2] | ✅ **選定**                                      |
| Haar / LBP cascade（OpenCV objdetect） | 傳統特徵           | 側臉、低頭容易漏偵測，誤報多                                           | 現有的 OpenCV 3.4 就能直接用，速度最快                                          | ❌ 漏偵測會造成框**閃爍**，直接違反 3.1.3；也沒有關鍵點，不能對齊。只當最後保底 |
| OpenCV res10 SSD（Caffe）              | 約 10 MB 的 SSD  | 正臉準                                                      | **需要 OpenCV dnn，板上沒有**                                             | ❌ 要重編 OpenCV；SSD 類模型在 A9 上 1 秒以上 [4]          |
| BlazeFace                            | 輕量             | 只適合近距離大臉                                                 | ncnn 在 A9 上約 72 ms [4]                                             | △ 可行，但 3 個人站在一起時臉比較小，而且短距離版的關鍵點較粗             |
| MTCNN                                | 三個級聯網路         | 有關鍵點                                                     | 影像金字塔加上每個候選框都要過 R‑Net、O‑Net，**運算量隨人臉數增加**                          | ❌ 3 張臉同時出現時延遲不穩定                              |
| RetinaFace‑MobileNet0.25             | 約 0.4M         | 準                                                        | 需要 ncnn；運算量比 YuNet 高                                               | △ 可行但沒有必要                                     |
| YOLOv8‑face                          | 數 M 參數         | 準                                                        | 沒有 NPU 時 A9 跑不動                                                    | ❌                                             |

**選定 libfacedetection 的理由：**

1. **部署風險最低**：只要一個 C++ 編譯器 [2]，不受板上 OpenCV 缺 dnn 的影響。
2. **速度最快的 CNN 偵測器之一**：參數量不到其他小型偵測器的 1/5 [3]，輸入解析度可以依照實測 FPS 在 320×240 和 160×120 之間調整。
3. **同時輸出 5 個關鍵點**（雙眼、鼻尖、兩邊嘴角）[2]，可以直接拿來對齊，不用再跑一個關鍵點模型。
4. 準度遠高於 Haar，可以減少漏偵測造成的閃爍。

---

## 3. 人臉特徵萃取選型

| 候選                           | 參數量               | LFW                 | A9 上估計每張臉延遲      | 需要的執行環境                                            | 評估                                      |
| ---------------------------- | ----------------- | ------------------- | ---------------- | -------------------------------------------------- | --------------------------------------- |
| **MobileFaceNet**            | < 1M，模型約 4 MB [1] | **99.55%** [1]      | 約 100–250 ms（估計） | ncnn                                               | ✅ **選定**                                |
| OpenFace nn4.small2.v1       | 3.73M [5]         | 92.92% [5]          | 估計數百 ms          | OpenCV dnn（`readNetFromTorch`），**板上沒有**            | ❌ 準度明顯較低，陌生人容易被認成組員；也需要 dnn             |
| LBPH                         | 不需要模型（手工特徵）       | 沒有 LFW 標準分數（見 §3.1） | 數 ms             | OpenCV contrib `face` 模組（world 裡有沒有**還沒確認**），或自行實作 | △ **備案**，詳見 §3.1                        |
| Eigenfaces / Fisherfaces     | 不需要模型（PCA / LDA）  | 低                   | 數 ms             | OpenCV contrib `face` 模組                           | ❌ 整張臉的線性投影，對光線和姿態比 LBPH 更敏感；而且所有影像必須一樣大 |
| FaceNet（Inception‑ResNet‑v1） | 約 20M+            | 約 99.6%             | 數秒               | ncnn                                               | ❌ 準度跟 MobileFaceNet 差不多，運算量卻高很多倍        |
| ArcFace ResNet50/100         | 數十 M              | 約 99.8%             | 數秒以上             | ncnn                                               | ❌ 準度提升在這個任務（2 人加陌生人）裡沒有意義               |
| dlib ResNet‑34               | 約 22 MB           | 約 99.4%             | 約 1 秒以上          | dlib（交叉編譯麻煩）                                       | ❌                                       |
| SFace（OpenCV Zoo）            | 約 37 MB 的 ONNX    | 高                   | 慢                | 需要 OpenCV ≥ 4.5.4                                  | ❌                                       |

**選定 MobileFaceNet 的理由：**

1. **為行動裝置設計**：論文報告在手機上約 18 ms、模型 4 MB [1]。在 A9 上大約慢 10 倍，但仍在 3.1 的時間預算內。
2. **準度和成本的比例最好**：LFW 99.55% [1]，跟 FaceNet、ArcFace 等大型模型差距在 0.3% 以內，運算量卻只有零頭。
3. **開放集合需要好的 embedding**：判定 `unknown` 完全靠「組員和陌生人的相似度分布要分得開」。LFW 92.9% 的 OpenFace 或 LBPH 分布重疊多，門檻怎麼設都會有誤判。
4. **符合資料集規模**：用 ArcFace loss 在大規模資料上預訓練 [1]，**完全不用再訓練**，60 張照片只拿來登錄。

### 3.1 LBPH（OpenCV `cv::face::LBPHFaceRecognizer`）專項比較

#### 運作方式（依照 OpenCV contrib 3.4 原始碼 [7]，方法源自 Ahonen 等人 [6]）

1. **LBP 編碼**：灰階影像（`CV_8UC1`）的每個像素和周圍 `neighbors=8` 個點（半徑 `radius=1`）比亮度，大於等於中心記 1、否則記 0，組成 8 位元碼，共 2⁸ = 256 種圖樣。
2. **空間直方圖**：LBP 影像切成 `grid_x × grid_y = 8 × 8` 格，每格統計 256 bin 的直方圖並正規化，串起來得到 **8 × 8 × 256 = 16,384 維**的特徵。
3. **訓練**：只是把每張訓練照片的直方圖**存起來**，不學任何參數。之後也可以用 `update()` 追加照片。
4. **預測**：跟**每一張**訓練照片的直方圖算卡方距離（`HISTCMP_CHISQR_ALT`），取最近的一張（1‑NN），回傳 `(label, distance)`，距離越小越像。
5. **陌生人**：`threshold` 預設是 `DBL_MAX`，也就是**預設永遠不會判成 unknown**，一定要自己設門檻：`distance > threshold` 時回傳 -1。

#### LBPH 和 MobileFaceNet 的逐項比較

| 面向 | LBPH（OpenCV） | MobileFaceNet（ncnn） | 對本 Lab 的影響 |
|---|---|---|---|
| **特徵來源** | 手工設計的局部紋理統計 | 在大規模人臉資料上用 ArcFace loss 學到的 embedding [1] | 學習型特徵是專門訓練來「同一人靠近、不同人遠離」的 |
| **公開準度** | 原始方法在 FERET（受控環境、正臉）上評估 [6]；OpenCV LBPH **沒有 LFW 這類非受控資料集的標準分數**，這裡不做數字比較 | LFW 99.55% [1] | LBPH 的準度高度依賴拍攝條件 |
| **需要的訓練資料** | 每人幾張到數十張，直接存起來 | 不訓練；每人數十張用來登錄 | **兩者都適合 60 張照片的規模**，這點 LBPH 沒有劣勢 |
| **每張臉的運算量** | LBP 編碼加上 60 次 16,384 維的距離計算，約 100 萬次運算，估計**數 ms** | 約 0.22 G MAdds，估計 **100–250 ms** | LBPH 快約兩個數量級。但 §1.4 已經說明辨識時間預算很寬，**這個優勢換不到分數** |
| **光線** | 只比大小關係，對**整體**亮度變化有一定的不變性；側光、陰影仍會改變紋理 | 訓練資料涵蓋大量光線變化 | demo 在室內固定燈光，影響中等 |
| **姿態、表情、模糊** | 格子是固定位置，臉一轉，同一格裡就是不同部位，**直方圖會明顯改變** | 對中等程度的轉頭和表情有容忍度 | **3.1.2(3) 要求移動攝影機**，模糊和角度變化正好是 LBPH 的弱點 |
| **對齊需求** | 很高：裁切位置差幾個像素就會影響每一格 | 有對齊比較好，但容忍度較高 | 兩者都會用 libfacedetection 的 5 點對齊，可以減輕 LBPH 這個缺點 |
| **判定 unknown（開放集合）** | 卡方距離沒有固定尺度，會隨裁切大小、格子數、光線改變；1‑NN 對**任何**臉都會找出一個「最近的組員」，陌生人和組員的距離分布容易重疊 | cosine 相似度在 [-1, 1] 之間，組員和陌生人的分布通常分得比較開 | **這是兩者最關鍵的差異**：`unknown` 判錯會直接影響 3.1.2(2)(3) 的分數 |
| **門檻穩定性** | 換場地、換燈光通常要重新校準 | 比較穩定，仍建議在 demo 場地校準 | LBPH 必須在 demo 教室現場拍照、現場調門檻 |
| **部署相依** | world 裡有沒有 contrib `face` **還沒確認**；如果沒有，可以重編 OpenCV 加 contrib，或**自己實作**（演算法只有上面 4 步，約 100 行） | 要交叉編譯 ncnn，並準備 ncnn 格式的模型 | 自行實作 LBPH 完全沒有外部依賴，是**部署風險最低**的辨識方案 |
| **抽問和報告的可解釋性** | 每一步都能在程式碼裡指出來（編碼、直方圖、距離、門檻） | 推論是黑盒子，能指出的是前處理、呼叫 ncnn、cosine 和門檻 | 兩者在 3.2.2 抽問都講得清楚，LBPH 在報告裡比較好寫演算法細節 |

#### 為什麼不選 LBPH 當主方案

- 本 Lab 的瓶頸**不是**辨識速度（§1.4），LBPH 最大的優點（快）在這裡用不到。
- 本 Lab 的評分重點是**陌生人要判成 unknown**，而且**攝影機移動後要能重新辨識**。這兩點正好對應 LBPH 最弱的兩個面向：開放集合的門檻，以及姿態和模糊。
- 3 個人同時在畫面中時，臉通常比較小、角度也不一。LBPH 在 8×8 格子裡的紋理統計會更不穩定。

#### 什麼情況下改用 LBPH

| 情況 | 做法 |
|---|---|
| ncnn 交叉編譯失敗，或 MobileFaceNet 轉換和載入卡太久 | 偵測和對齊**完全不變**，只把辨識器換成 LBPH，管線其餘部分照用 |
| 時間不夠，要先有能 demo 的版本 | 先做 LBPH 版本拿到 3.1.2(1) 的框，再換成 MobileFaceNet |

**建議的保底組合**：libfacedetection 加上 5 點對齊、裁成固定大小的灰階影像（例如 112×112），再交給 LBPH，配合直方圖等化（`equalizeHist`）。用 LBPH 時，§5 的 cosine 門檻改成卡方距離門檻，§6 的多幀投票和遲滯照樣使用，可以部分彌補單幀判斷的不穩定。

---

## 4. 推論框架選型

板上 OpenCV 沒有 dnn，所以 MobileFaceNet 要另外找地方跑。

| 選項 | 優點 | 缺點 | 評估 |
|---|---|---|---|
| **ncnn** | 專為行動和嵌入式 ARM 設計，有 armv7 加 NEON 最佳化，有 Cortex‑A9 的公開 benchmark [4]；沒有第三方依賴；**跟現有 OpenCV 互不干擾** | 要多交叉編譯一個函式庫；MobileFaceNet 要轉成 ncnn 格式，或使用社群已轉好的模型 | ✅ **選定** |
| 重編 OpenCV 3.4.7，打開 dnn | 之後所有模型都用同一套 API | OpenCV 完整編譯時間長，而且要動 Lab 2 已經驗證過的環境；OpenCV 3.4 dnn 在 ARMv7 上的最佳化程度不如 ncnn | △ 備案 |
| TFLite / ONNX Runtime | 生態系大 | armv7 加舊工具鏈的交叉編譯比 ncnn 複雜很多 | ❌ |

**選定 ncnn 的理由：**

- 最快：專為 ARM 手機 CPU 調校過。
- 風險最低：不動既有環境。
- 有同等級 A9 的實測數據可以參考 [4]。
- 3.2 安全帽偵測如果選了 YOLO 類模型，ncnn 也能直接沿用。

---

## 5. 身分判定策略

1. **登錄**：每位組員的每張照片都跑「偵測 → 對齊 → MobileFaceNet → L2 正規化」，取平均當作範本，也保留全部 embedding。
2. **比對**：對每張臉算 `s_k = cos(e, template_k)`，k ∈ {組員 A, 組員 B}。
3. **判定**：
   - `s_max ≥ T`，而且 `s_max − s_second ≥ M` → 顯示該組員的學號；
   - 其他情況 → 顯示 `unknown`。
4. **校準 T 和 M**：用組員的留一法（leave‑one‑out）分數和負樣本分數畫直方圖，把 T 設在兩群中間偏保守的位置。目標是**陌生人被認成組員的機率接近 0**；把組員誤判成 `unknown` 比較能接受，因為多幀投票會補回來。
5. **不使用 SVM 或分類器**：2 個類別的分類器一定會把陌生人分到其中一類，不適合開放集合。

---

## 6. 系統管線與時間預算

```
[執行緒 1 擷取]  V4L2 MJPG 640×480 ──► 最新影格（只保留 1 張，不排隊）
                                             │
[執行緒 2 偵測加追蹤] 縮小到 320×240 → libfacedetection → IoU 對應 track
                     → 框座標 EMA 平滑 → 漏偵測 ≤ N 幀時保留框
                                             │ 需要辨識的 track
[執行緒 3 辨識]  對齊 112×112 → ncnn MobileFaceNet → cosine 比對
                → 寫回 track 的投票歷史（最近 K 次多數決加遲滯）
                                             │
[主執行緒 顯示]  畫框和標籤 → BGR565/BGRA → /dev/fb0（依照實際 bpp）
```

| 項目 | 頻率 | 估計耗時 | 說明 |
|---|---|---|---|
| 偵測 | 每幀 | 約 100–150 ms（320×240） | 不夠快就降到 160×120，或改成每 2 幀偵測一次 |
| 辨識 | 新 track 立即一次，之後每 track 約每 1 秒一次 | 每張臉約 100–250 ms | 3 張臉每秒更新，約佔 1 個核心 |
| 顯示 | 每幀 | 低 | 沿用 Lab 2 的 mmap 寫法 |

**是否符合 3.1 的時間要求：** 新臉出現後，第一次辨識約 0.3 秒、多幀投票穩定約 1–3 秒，遠低於 10 秒的上限。

---

## 7. 風險與備案

| 風險 | 機率 | 備案 |
|---|---|---|
| ncnn 交叉編譯失敗 | 低 | 重編 OpenCV 3.4.7 打開 dnn，用 dnn 載入 MobileFaceNet（ONNX 或 Caffe 格式，需要實測能不能載入） |
| libfacedetection 在 A9 上太慢 | 中 | 輸入降到 160×120；偵測隔幀跑，中間用追蹤補；使用 OpenMP 多執行緒 |
| MobileFaceNet 太慢 | 低 | 降低每個 track 的辨識頻率；ncnn int8 量化 |
| `unknown` 誤判 | 中 | 增加登錄照片的角度和光線多樣性；在 demo 教室重新拍登錄照；調整 T 和 M |
| ncnn 和 OpenCV dnn 都走不通 | 低 | libfacedetection 加對齊，再接 **LBPH**（contrib 或自行實作），見 §3.1 |
| 最壞情況 | — | Haar cascade（world 裡已確認有）加 LBPH |

---

## 8. 待驗證事項

- [ ] CPU 時脈：`cat /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq`
- [ ] libfacedetection 在板上 320×240 和 160×120 的實際延遲
- [ ] ncnn MobileFaceNet 在板上每張臉的實際延遲（1 和 4 執行緒）
- [ ] 板上有沒有 `libgomp`（OpenMP 執行時期函式庫，影響 libfacedetection 的多執行緒）
- [ ] 保底方案用：`grep -c LBPHFaceRecognizer ~/lab2/libopencv_world.so.3.4`

---

## 參考資料

1. S. Chen et al., *MobileFaceNets: Efficient CNNs for Accurate Real-Time Face Verification on Mobile Devices*, 2018. <https://arxiv.org/abs/1804.07573>
2. S. Yu, *libfacedetection*（README、benchmark、範例程式）. <https://github.com/ShiqiYu/libfacedetection>
3. W. Wu, H. Peng, S. Yu, *YuNet: A Tiny Millisecond-level Face Detector*, Machine Intelligence Research, 2023. 摘要：<https://hub.baai.ac.cn/view/31672>；模型：<https://huggingface.co/opencv/face_detection_yunet>
4. Tencent ncnn benchmark（Z7‑Lite 7020，Cortex‑A9 766 MHz × 2；i.MX7 Dual，Cortex‑A7）. <https://github.com/Tencent/ncnn/blob/master/benchmark/README.md>
5. OpenFace, *Models and Accuracies*. <https://cmusatyalab.github.io/openface/models-and-accuracies/>
6. T. Ahonen, A. Hadid, M. Pietikäinen, *Face Description with Local Binary Patterns: Application to Face Recognition*, IEEE TPAMI, 2006.
7. OpenCV contrib 3.4，`modules/face/src/lbph_faces.cpp` 和 `include/opencv2/face/facerec.hpp`. <https://github.com/opencv/opencv_contrib/tree/3.4/modules/face>
