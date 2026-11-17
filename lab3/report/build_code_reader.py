#!/usr/bin/env python3
"""Build lab31_code_reader.html: real source excerpts from ../face/src beside notes mapped to the Lab 3.1 spec.

Run from anywhere:  python3 lab3/report/build_code_reader.py
Every block names an anchor string expected on its first line, so the build fails loudly if the code shifts.
"""
import html
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "face", "src"))
OUT = os.path.join(HERE, "lab31_code_reader.html")

REQS = [
    ("r21", "2.1", "影像輸入與顯示", "攝影機影像要顯示在螢幕上（HDMI 或 LCD）。"),
    ("r22", "2.2", "偵測人臉", "用公開演算法偵測人臉。"),
    ("r23", "2.3", "抽取特徵", "從偵測到的臉抽出特徵。"),
    ("r24", "2.4", "辨識身分", "用特徵設計辨識個人的演算法。"),
    ("g1", "3.1.2(1)", "三張臉都畫框", "同時偵測三個人，每張臉畫一個框。"),
    ("g2", "3.1.2(2)", "30 秒內認出", "組員顯示學號，其他人顯示 unknown，30 秒內。"),
    ("g3", "3.1.2(3)", "移動後 10 秒內重抓", "鏡頭移動後，10 秒內重新畫出正確的框。"),
    ("st", "3.1.3", "框和標籤不閃", "框和標籤不能 jitter 或 flicker。"),
]

STEPS = [
    {
        "id": "main", "phase": "啟動", "title": "main()：程式入口",
        "reqs": [],
        "summary": "決定要跑哪個模式，讀設定檔，再交給三個函式之一。demo 時走的是 run，也就是 run_live。",
        "blocks": [
            {"file": "main.cpp", "start": 375, "end": 387, "anchor": "int main", "notes": [
                ("377", "三元運算子 `條件 ? A : B`。沒給參數就當成 `run`，所以 `./run.sh` 和 `./run.sh run` 一樣。"),
                ("378-379", "先讀 `config.yml`。讀不到就回傳 1 結束，三個模式都需要它。"),
                ("381", "即時辨識。3.1 的評分全部發生在這條路徑上。"),
                ("382-383", "`enroll` 和 `score` 是離線工具，用來建立組員範本和校準門檻。它們要多一個資料夾參數，所以檢查 `argc > 2`。"),
                ("385-386", "參數不對時印出用法並結束。"),
            ]},
        ],
    },
    {
        "id": "config", "phase": "啟動", "title": "Config：所有可調參數",
        "reqs": ["r21", "r22", "r23", "r24", "g3", "st"],
        "summary": "一個 struct 裝全部參數，欄位有預設值，config.yml 有寫的才覆蓋。現場調參數不用重新編譯。",
        "blocks": [
            {"file": "config.hpp", "start": 6, "end": 49, "anchor": "struct Config", "notes": [
                ("6", "`struct` 就是一包資料。其他類別在 `load` 或 `configure` 時從這裡抄自己需要的值。"),
                ("9-13", "攝影機和螢幕（2.1）。這裡的預設是 `/dev/fb0`，但 `config.yml` 改成 `/dev/fb2`，也就是 HDMI。"),
                ("18-22", "Haar 偵測參數（2.2）。在第 5 步 detect 會用到。"),
                ("25-30", "MobileFaceNet 模型檔和輸入輸出層名稱（2.3）。"),
                ("34-35", "身分判定的兩個門檻（2.4）。在 Gallery::match 用到。"),
                ("38-46", "追蹤和穩定參數，負責 3.1.2(3) 和不閃爍。每一個都會在 Tracker 裡出現。"),
            ]},
            {"file": "config.cpp", "start": 7, "end": 22, "anchor": "template <typename T>", "notes": [
                ("7-12", "`template` 讓同一個函式能讀 int、double、string。`node.empty()` 代表檔案沒寫這個欄位，那就保留 struct 裡的預設值。"),
                ("16-21", "`cv::FileStorage` 是 OpenCV 讀寫 YAML 的類別。打不開就回傳 false，main 會結束。"),
                ("22", "之後每個欄位一行，都是同樣的寫法。"),
            ]},
        ],
    },
    {
        "id": "still", "phase": "離線準備", "title": "StillPipeline：一張照片變成一個特徵向量",
        "reqs": ["r22", "r23"],
        "summary": "enroll 和 score 共用。偵測、對齊、抽特徵三步，跟即時模式用的是同樣三個類別。",
        "blocks": [
            {"file": "main.cpp", "start": 44, "end": 66, "anchor": "struct StillPipeline", "notes": [
                ("46-48", "三個元件。即時模式也用這三個類別，只是對齊和抽特徵放到背景執行緒。"),
                ("52", "`&&` 會短路。任何一個載入失敗就回傳 false，後面的不會執行。"),
                ("58-59", "偵測所有臉，一張都沒有就回傳 false。"),
                ("60-61", "`std::max_element` 加一個 lambda 比較函式，用面積找最大的臉。登錄照裡最大的臉就是本人。"),
                ("62", "對齊成 112×112。`eyes_found` 告訴呼叫者眼睛是真的找到，還是用比例估計的。"),
                ("63", "抽出 128 維、長度為 1 的向量。這就是 2.3 要的特徵。"),
            ]},
        ],
    },
    {
        "id": "enroll", "phase": "離線準備", "title": "run_enroll：建立組員範本 gallery.yml",
        "reqs": ["r24", "g2"],
        "summary": "把 enroll/<學號>/ 底下的照片全部算成特徵，每個人取平均當範本存檔，最後印出相似度統計幫你設門檻。",
        "blocks": [
            {"file": "main.cpp", "start": 95, "end": 163, "anchor": "static int run_enroll", "notes": [
                ("100", "`list_images`（第 68 行）遞迴找 jpg、png、bmp，排序後回傳。"),
                ("107", "建 `aligned/` 資料夾。對齊結果存在這裡，讓你用肉眼確認兩眼水平、臉置中。"),
                ("117-122", "找不到臉的照片跳過並計數。"),
                ("124", "`parent_name` 取上一層資料夾名稱，`enroll/0812345/3.bmp` 得到 `0812345`。畫面上顯示的學號就是從這裡來的（3.1.1）。"),
                ("125", "把這張照片的特徵加到那個人底下。"),
                ("130", "所有照片處理完，算每個人的平均範本。"),
                ("131-135", "沒有任何人或存檔失敗就結束。`save` 寫出 `gallery.yml`。"),
                ("141-157", "統計每個人的登錄照跟自己範本的平均和最低相似度。"),
                ("151-152", "`q != p` 是跟別人的範本比，記下最高值。這個數字越低，兩位組員越不會互相認錯。"),
                ("160-161", "提醒 `sim_threshold` 要低於組員的最低分，否則組員會被判成 unknown。"),
            ]},
        ],
    },
    {
        "id": "gallery", "phase": "離線準備", "title": "Gallery：範本怎麼存、怎麼平均",
        "reqs": ["r24"],
        "summary": "Person 是一位組員，存所有樣本和一個平均範本。比對只用平均範本。",
        "blocks": [
            {"file": "gallery.hpp", "start": 7, "end": 19, "anchor": "struct Person", "notes": [
                ("9", "顯示用的文字，也就是學號。"),
                ("10", "`std::vector<std::vector<float> >` 是二維陣列。每一列是一張登錄照的 128 維特徵。"),
                ("11", "平均後再正規化的範本。即時比對只用這個。"),
                ("14-19", "一次比對的結果。`index = -1` 代表 unknown。`best` 和 `second` 用來檢查第一名有沒有明顯勝出。"),
            ]},
            {"file": "gallery.cpp", "start": 7, "end": 54, "anchor": "void Gallery::add", "notes": [
                ("9-16", "已經有這個標籤，就把特徵加進去後結束。"),
                ("17-20", "沒有就新增一個人。"),
                ("29-32", "把所有樣本逐維相加。"),
                ("33", "再做 L2 正規化。這裡不用先除以張數，因為正規化會把長度變成 1，方向跟平均一樣。"),
                ("41-52", "存成 YAML。每個人存標籤和一個 N×128 的 `cv::Mat`。只存樣本不存平均，`load` 時會重算（第 75 行）。"),
            ]},
        ],
    },
    {
        "id": "score", "phase": "離線準備", "title": "run_score：拿陌生人照片校準門檻",
        "reqs": ["r24", "g2"],
        "summary": "對資料夾裡每張照片印出判定結果。拿陌生人照片跑，最後一行的最高分就是門檻的下限。",
        "blocks": [
            {"file": "main.cpp", "start": 168, "end": 205, "anchor": "static int run_score", "notes": [
                ("172", "需要先用 enroll 建好的 `gallery.yml`。"),
                ("188", "跟即時模式呼叫的是同一個 `gallery.match`，這裡印的判定就是即時模式會給的判定。"),
                ("190-196", "不管有沒有過門檻，都找出最像的人一起印，方便看陌生人離門檻多近。"),
                ("197", "記錄這個資料夾裡最高的 `best`。陌生人裡的最高分要低於 `sim_threshold`，否則陌生人會被認成組員。"),
            ]},
        ],
    },
    {
        "id": "live-init", "phase": "即時模式初始化", "title": "run_live 開頭：開裝置、啟動執行緒",
        "reqs": ["r21"],
        "summary": "讀範本，載入模型，打開螢幕和攝影機，最後啟動兩個背景執行緒。之後程式同時有三個執行緒在跑。",
        "blocks": [
            {"file": "main.cpp", "start": 240, "end": 265, "anchor": "static int run_live", "notes": [
                ("242-247", "讀 `gallery.yml`，印出每個人有幾張樣本。"),
                ("249-253", "宣告五個物件。這時還沒開任何裝置。"),
                ("254", "`detector.load` 載入臉的 Haar。`recognizer.init` 載入眼睛 Haar 和 MobileFaceNet，並記住 gallery 的指標。"),
                ("255-256", "打開 framebuffer（下一步會講）。HDMI 會印出 1920x1080 @ 32 bpp。"),
                ("257", "打開攝影機，`camera_index` 是 2，也就是 `/dev/video2`。"),
                ("258", "Tracker 從設定抄參數。"),
                ("260-261", "Ctrl+C 或 kill 時只把 `quit_flag` 設成 1（第 31-32 行）。主迴圈下一圈看到就正常離開並清理。`volatile sig_atomic_t` 是 signal handler 裡可以安全寫入的型別。"),
                ("262", "把終端機改成不用按 Enter 就讀得到按鍵。"),
                ("263-264", "啟動攝影機執行緒和辨識執行緒。"),
            ]},
        ],
    },
    {
        "id": "camera", "phase": "即時模式初始化", "title": "Camera：背景讀攝影機，只留最新一幀",
        "reqs": ["r21", "g3"],
        "summary": "攝影機執行緒一直讀，新幀直接蓋掉舊幀。主迴圈比攝影機慢時不會排隊，延遲不會越積越多。",
        "blocks": [
            {"file": "camera.hpp", "start": 9, "end": 33, "anchor": "// 背景執行緒", "notes": [
                ("20", "主執行緒只會呼叫這個函式來拿影像。"),
                ("25", "OpenCV 的攝影機物件。"),
                ("27-28", "`mtx` 保護下面四個變數，兩個執行緒都會碰它們。`cond` 用來叫醒在等新幀的主執行緒。"),
                ("29-32", "最新一幀、它的拍攝時間、編號、是否還在跑。"),
            ]},
            {"file": "camera.cpp", "start": 7, "end": 72, "anchor": "bool Camera::open", "notes": [
                ("9-14", "`VideoCapture` 打開 `/dev/videoN`，失敗就回傳 false。"),
                ("16", "格式設成 MJPG。USB 2.0 頻寬下用未壓縮格式拿不到 640×480 的高幀率。"),
                ("26-27", "`std::thread` 開一個執行緒跑 `loop`。`&Camera::loop, this` 的意思是在這個物件上呼叫成員函式。"),
                ("32-37", "停止時先在鎖內把 `running` 改成 false，叫醒等待的人，再 `join` 等執行緒結束。"),
                ("46-49", "每圈先檢查要不要停。`lock_guard` 在離開大括號時自動解鎖。"),
                ("50", "`read` 會一直等到攝影機給出下一幀。讀失敗就再試。"),
                ("51-52", "記下讀到的時間，主迴圈用它算「拍到到顯示」的延遲。"),
                ("55", "`cv::swap` 只交換兩個 Mat 的指標，不複製像素。`latest` 永遠是最新一幀，舊的直接被蓋掉。"),
                ("57", "編號加一，讓等待的人知道有新幀。"),
                ("59", "叫醒在 `wait_frame` 裡等待的主執行緒。"),
                ("65-67", "`wait_for` 等最多 2 秒，直到出現比 `last_id` 新的幀或攝影機停了。等待時會暫時放開鎖，攝影機執行緒才寫得進來。"),
                ("68", "逾時或停止回傳 -1，主迴圈會 `continue` 再等。"),
                ("69", "`copyTo` 才是真的複製像素。主執行緒拿到自己的副本，之後在上面畫框不影響攝影機執行緒。"),
            ]},
        ],
    },
    {
        "id": "fb-open", "phase": "即時模式初始化", "title": "Framebuffer::open：把螢幕記憶體映射進來",
        "reqs": ["r21"],
        "summary": "不用 X 視窗或 Qt，直接把 /dev/fb2 的記憶體 mmap 進程式。寫這塊記憶體就等於改螢幕上的像素。",
        "blocks": [
            {"file": "framebuffer.cpp", "start": 14, "end": 54, "anchor": "bool Framebuffer::open", "notes": [
                ("16", "`::open` 前面的 `::` 表示呼叫全域的 POSIX `open`，不是這個類別自己的 `open`。"),
                ("25", "`ioctl` 向驅動程式查螢幕資訊。VSCREENINFO 是解析度、bpp、顏色排列。FSCREENINFO 是每列幾個 byte 和記憶體大小。"),
                ("31-36", "記下來給 `show` 用。`line_length` 可能比 寬×每像素 byte 數 大，因為每列尾端可能有對齊用的空間。"),
                ("38", "`mmap` 把螢幕記憶體映射到程式的位址空間。"),
                ("39", "映射完 fd 就可以關，映射仍然有效。"),
                ("48-50", "往 sysfs 的 `blank` 寫 0，叫醒休眠的螢幕。"),
                ("52", "清成全黑。"),
            ]},
        ],
    },
    {
        "id": "track", "phase": "每一幀", "title": "先看資料：Track 的每個欄位由誰寫",
        "reqs": ["g1", "g2", "g3", "st"],
        "summary": "主迴圈的每一步都在讀寫 Track。一張臉就是一個 Track。先記住每個欄位由哪個函式寫入，後面的程式就好讀。",
        "blocks": [
            {"file": "tracker.hpp", "start": 9, "end": 30, "anchor": "// 顯示用的標籤狀態", "notes": [
                ("10-14", "標籤的三種狀態。-2 是還沒有結果，畫黃框 `...`。-1 是 unknown，畫紅框。0 以上是 gallery 裡第幾位組員，畫綠框顯示學號。"),
                ("18", "每張臉唯一的編號。背景辨識的結果靠它找回對應的 track。"),
                ("19", "平滑過的框。`Rect2f` 用 float 存，EMA 才不會被整數截斷。寫入者是 `Tracker::update`。"),
                ("20", "累計被偵測到幾幀，`update` 加一。`visible()` 用它決定畫不畫。"),
                ("21", "連續沒被偵測到幾幀，`update` 寫入。大於 0 時主迴圈不送辨識，因為框是舊位置。"),
                ("22", "最後一次被偵測到的時間。超過 `track_timeout` 就刪掉。"),
                ("23", "最近幾次辨識結果 (標籤, 相似度)。`deque` 是兩端都能快速增刪的佇列。寫入者是 `add_vote`。"),
                ("24", "目前畫在畫面上的標籤。寫入者是 `add_vote` 和 `resolve_duplicates`。"),
                ("25-26", "上次送辨識的時間，以及是否還有工作在背景跑。主迴圈送工作時設定，`add_vote` 收到結果時清掉 `pending`。"),
                ("28", "被偵測到至少 `min_hits` 次才畫出來。"),
            ]},
        ],
    },
    {
        "id": "loop", "phase": "每一幀", "title": "主迴圈前半：拿幀、偵測、追蹤、收結果",
        "reqs": ["r21", "r22", "g1", "g2", "g3"],
        "summary": "每一圈處理一幀。這一段把整個流程串起來，細節在後面幾步。",
        "blocks": [
            {"file": "main.cpp", "start": 278, "end": 303, "anchor": "while ( !quit_flag )", "notes": [
                ("278", "`quit_flag` 由 signal handler 設定，按 q 則是在迴圈底部 `break`。"),
                ("282", "拿最新一幀。傳入上次處理過的編號，保證拿到的是新的。"),
                ("283", "-1 代表逾時，回到迴圈開頭。"),
                ("285", "這一幀的時間戳記，追蹤和送辨識都用它。"),
                ("290", "偵測這一幀所有的臉（2.2）。"),
                ("293", "把偵測框對應到既有的臉（3.1.2(1)(3)）。"),
                ("296", "拿走背景執行緒做完的所有辨識結果。"),
                ("299", "每個結果投一票給它的 track。"),
                ("303", "兩個框顯示同一個學號時，留一個。"),
            ]},
        ],
    },
    {
        "id": "detect", "phase": "每一幀", "title": "FaceDetector::detect：Haar 找臉",
        "reqs": ["r22", "g1"],
        "summary": "縮小、灰階、等化，再用 Haar cascade 找所有的臉，最後合併重複框。",
        "blocks": [
            {"file": "face_detector.cpp", "start": 42, "end": 69, "anchor": "std::vector<cv::Rect> FaceDetector::detect", "notes": [
                ("45-46", "算縮放比例。640 寬縮到 320 是 0.5。只縮小，不放大。"),
                ("48", "Haar 只看亮度，先轉灰階。"),
                ("49", "像素變成 1/4，偵測快很多。`INTER_AREA` 是縮小時品質較好的插值。"),
                ("52", "直方圖等化把亮度分布拉平，逆光或太暗的臉比較容易被找到。"),
                ("56-57", "`detectMultiScale` 用影像金字塔在各種大小找臉。1.15 是每層縮小的倍率。`min_neighbors` 4 表示同一位置至少要 4 個候選重疊才算臉，越大誤報越少。最小臉 30×30。"),
                ("60-65", "座標除以縮放比例，換回 640×480 原圖的座標。"),
                ("68", "合併重疊框，每張臉只剩一個框。三個人就是三個框（3.1.2(1)）。"),
            ]},
        ],
    },
    {
        "id": "merge", "phase": "每一幀", "title": "merge_overlaps：一張臉只留一個框",
        "reqs": ["r22", "g1", "st"],
        "summary": "Haar 常在不同縮放層級對同一張臉各給一個框。大框優先，跟已保留的框重疊太多的就丟掉。",
        "blocks": [
            {"file": "face_detector.cpp", "start": 22, "end": 40, "anchor": "std::vector<cv::Rect> merge_overlaps", "notes": [
                ("25-26", "依面積由大到小排序。`[] (a, b) { ... }` 是 lambda，當作比較函式。"),
                ("33", "`a & b` 是 OpenCV 的矩形交集。"),
                ("34-35", "重疊比例用較小框的面積當分母，不是 IoU。大框包住小框時 IoU 很小，但這個比例接近 1，會被判成同一張臉。"),
                ("37", "沒跟任何保留的框重複才保留。"),
            ]},
        ],
    },
    {
        "id": "update", "phase": "每一幀", "title": "Tracker::update：把框接到同一張臉上",
        "reqs": ["g1", "g3", "st"],
        "summary": "每個偵測框配給最近的既有 track。小幅變動用 EMA 平滑，大幅移動直接跳過去。沒配到的框變成新 track，太久沒看到的 track 刪掉。",
        "blocks": [
            {"file": "tracker.cpp", "start": 41, "end": 119, "anchor": "// 兩個框中心的距離", "notes": [
                ("42-47", "中心距離除以臉寬。用「幾個臉寬」當單位，人離鏡頭遠近都適用。"),
                ("61", "函式內的 struct，存一個候選配對。"),
                ("63-72", "每個 track 跟每個偵測框都試一次。"),
                ("67-68", "大小差超過 2 倍的不配，避免臉的框被背景的小誤報搶走。"),
                ("70", "IoU 夠大或中心夠近都算候選。人移動快、FPS 低時兩幀的框可能完全不重疊，IoU 是 0，靠中心距離仍然接得上。移動時不會丟掉 track（3.1.2(3)）。"),
                ("74", "依距離排序，最近的先配。"),
                ("79-80", "貪婪配對。一個 track 只配一個框，一個框只配一個 track。"),
                ("87", "距離超過 `snap_distance`（0.25 個臉寬）代表人真的在動，`alpha` 用 1，框直接跳到新位置，不會拖在臉後面。否則用 0.4。"),
                ("88-91", "EMA 公式。新值 = alpha × 偵測 + (1 − alpha) × 舊值。靜止時 Haar 框每幀差幾個像素，平滑後框不會抖（3.1.3）。"),
                ("92-94", "更新計數和最後看到的時間。"),
                ("99-100", "這一幀沒配到的 track，`missed` 加一，但先不刪。"),
                ("101-103", "erase-remove 寫法。`remove_if` 把要刪的移到尾端，`erase` 真的刪掉。超過 0.4 秒沒看到才刪，偶爾一兩幀漏偵測不會讓框閃掉。用秒數而不是幀數，FPS 低時舊框也不會殘留太久。"),
                ("106-115", "沒配到的偵測框變成新 track。`hits` 從 1 開始，要到 `min_hits`（2）才畫出來，過濾掉只出現一幀的誤報。"),
                ("118", "最後合併重疊的 track（下一步）。"),
            ]},
        ],
    },
    {
        "id": "merge-tracks", "phase": "每一幀", "title": "merge_overlapping_tracks：同一張臉不能有兩個 track",
        "reqs": ["g1", "st"],
        "summary": "兩個 track 重疊太多就是同一張臉，留比較久的那個，它通常已經有標籤。",
        "blocks": [
            {"file": "tracker.cpp", "start": 121, "end": 146, "anchor": "void Tracker::merge_overlapping_tracks", "notes": [
                ("130-132", "跟 `merge_overlaps` 一樣，用較小框的面積當分母。"),
                ("134-139", "留 `hits` 多的。它比較早出現，保留它標籤就不會從 `...` 重來。"),
                ("142-145", "重建清單。`swap` 交換兩個 vector 的內容，不複製元素。"),
            ]},
        ],
    },
    {
        "id": "vote", "phase": "每一幀", "title": "add_vote：多數決加遲滯",
        "reqs": ["g2", "st"],
        "summary": "第一個結果立刻顯示。之後要換標籤，新標籤的票數要比目前的多而且至少 3 票，單次誤判不會讓標籤跳掉。",
        "blocks": [
            {"file": "tracker.cpp", "start": 14, "end": 27, "anchor": "float Track::mean_score", "notes": [
                ("14-27", "某個標籤在投票裡的平均相似度。`resolve_duplicates` 用它決定誰保留學號。"),
            ]},
            {"file": "tracker.cpp", "start": 148, "end": 176, "anchor": "void Tracker::add_vote", "notes": [
                ("150-151", "用 id 找 track。找不到代表辨識完成前人已經離開，結果丟掉。"),
                ("152", "清掉 `pending`，之後可以再送辨識。"),
                ("153-154", "加進投票，超過 7 筆就從前面丟掉最舊的。"),
                ("157-161", "第一個結果直接顯示。新的臉從 `...` 到出現學號只需要一次辨識（3.1.2(2)）。"),
                ("164-165", "`std::map` 數每個標籤的票數。`count[x]++` 在 x 不存在時會自動建立成 0 再加一。"),
                ("166-174", "找票數最多的標籤。起始值是目前顯示的標籤，平手時保持不變。"),
                ("175", "換標籤要兩個條件。票數比目前顯示的多，而且至少 `vote_min`（3）票（3.1.3 不能 flicker）。"),
            ]},
        ],
    },
    {
        "id": "dup", "phase": "每一幀", "title": "resolve_duplicates：同一個學號只能出現一次",
        "reqs": ["g2", "st"],
        "summary": "兩個框顯示同一位組員時，平均相似度較低的改成 unknown。外人就算被誤認，也不會跟本人同時掛同一個學號。",
        "blocks": [
            {"file": "tracker.cpp", "start": 178, "end": 196, "anchor": "void Tracker::resolve_duplicates", "notes": [
                ("183", "`shown_label < 0` 是 pending 或 unknown，不用檢查。"),
                ("186-193", "兩個框顯示同一位組員時，平均相似度低的改成 unknown。題目是兩位組員加一個外人（3.1.1）。"),
                ("187-188", "只改 `shown_label`，`votes` 不動。下一票可能讓它翻回組員，但這個函式每幀都在畫圖之前執行，畫面上看不到翻回的那一下。"),
            ]},
        ],
    },
    {
        "id": "send", "phase": "每一幀", "title": "送辨識工作給背景執行緒",
        "reqs": ["g2", "g3"],
        "summary": "符合條件的 track 把臉附近的小圖複製一份送出去。新臉立刻送，之後每秒送一次。",
        "blocks": [
            {"file": "recognizer.hpp", "start": 14, "end": 27, "anchor": "struct RecogJob", "notes": [
                ("14-19", "送給背景執行緒的工作。`region` 是已經複製出來的小圖，`face` 是臉在小圖裡的位置。"),
                ("21-27", "回傳的結果。`label` 是 gallery 索引或 `LABEL_UNKNOWN`。`millis` 用來印效能。"),
            ]},
            {"file": "main.cpp", "start": 305, "end": 329, "anchor": "3. 需要", "notes": [
                ("310", "三種情況跳過。還沒畫出來的不送。已經有工作在跑的不送，避免同一張臉塞滿佇列。這一幀沒被偵測到的不送，因為框是舊位置。"),
                ("311", "同一張臉每 `recog_interval`（1 秒）最多送一次。新 track 的 `last_request` 是 -1e9，所以第一次一定會送。"),
                ("314-316", "臉框四邊各擴大 30%，總共 1.6 倍。找眼睛和旋轉需要一點臉外面的影像。"),
                ("317", "`&=` 跟整張影像取交集，避免超出邊界。"),
                ("322", "`clone()` 複製像素。主執行緒等一下要在 `frame` 上畫框，背景執行緒拿到的是乾淨的副本。"),
                ("323", "把臉框平移成小圖裡的座標。`region.tl()` 是左上角。"),
                ("324-328", "放進佇列成功才標記 `pending`。佇列滿時下一幀會再試。"),
            ]},
        ],
    },
    {
        "id": "recognizer", "phase": "背景辨識", "title": "Recognizer：工作佇列和背景執行緒",
        "reqs": ["g2", "g3"],
        "summary": "mutex 加 condition variable 的工作佇列。慢的推論在鎖外面做，主迴圈的 FPS 不受辨識速度影響。",
        "blocks": [
            {"file": "recognizer.cpp", "start": 31, "end": 79, "anchor": "bool Recognizer::push", "notes": [
                ("33-37", "鎖內檢查佇列上限 4（板子沒有 swap，佇列一定要有上限），放進去。"),
                ("38", "解鎖後叫醒背景執行緒。"),
                ("44-47", "`swap` 一次拿走所有結果並清空，鎖住的時間很短。"),
                ("57", "`cond.wait` 放開鎖睡覺，直到有工作或要停止才醒來並重新上鎖。"),
                ("58-60", "要停就結束，否則取出一個工作。"),
                ("63-67", "大括號外面，沒有拿鎖。對齊、抽特徵、比對都在這裡做，主執行緒不用等。"),
                ("71", "比對結果 `index` 是 -1 時轉成 `LABEL_UNKNOWN`。"),
                ("75-76", "鎖內把結果放進 `results`，下一幀主迴圈的 `take_results` 會收走。"),
            ]},
        ],
    },
    {
        "id": "eyes", "phase": "背景辨識", "title": "FaceAligner::find_eyes：在臉的上半部找兩隻眼睛",
        "reqs": ["r23"],
        "summary": "用眼睛的 Haar cascade 找左右眼，再用兩眼距離和傾斜角做合理性檢查。",
        "blocks": [
            {"file": "face_aligner.cpp", "start": 28, "end": 69, "anchor": "bool FaceAligner::find_eyes", "notes": [
                ("31-33", "只在臉框上方 60% 找，避開鼻孔和嘴巴的誤判，也比較快。"),
                ("36-37", "轉灰階並等化。"),
                ("40-41", "眼睛最小是臉寬的 1/8，至少 8 像素。"),
                ("44-56", "中心在左半邊的是左眼候選，右半邊是右眼候選，各取面積最大的。"),
                ("57", "少一隻眼睛就失敗。"),
                ("59-62", "取眼框中心，加上 `upper` 的位移換回 region 的座標。"),
                ("65-67", "兩眼水平距離要在臉寬的 25% 到 65%。垂直差不能超過水平差的 0.47 倍，約 25 度，因為 tan 25° ≈ 0.47。"),
            ]},
        ],
    },
    {
        "id": "align", "phase": "背景辨識", "title": "FaceAligner::align：轉正並裁成 112×112",
        "reqs": ["r23"],
        "summary": "算一個相似變換把兩眼對到 ArcFace 樣板的位置。模型是用這樣對齊的臉訓練的，輸入要對到一樣的位置。",
        "blocks": [
            {"file": "face_aligner.cpp", "start": 8, "end": 16, "anchor": "// ArcFace", "notes": [
                ("9-10", "ArcFace 112×112 樣板裡兩眼的位置。"),
                ("14-16", "找不到眼睛時，用臉框的固定比例估計眼睛位置。"),
            ]},
            {"file": "face_aligner.cpp", "start": 71, "end": 99, "anchor": "cv::Mat FaceAligner::align", "notes": [
                ("74-79", "找不到眼睛就用估計值，所以一定會有輸出。"),
                ("83-86", "兩眼的向量長度比是縮放倍率，`atan2` 的角度差是要轉多少度。"),
                ("87-93", "相似變換矩陣 [a −b tx; b a ty]。a、b 包含旋轉和縮放，tx、ty 讓左眼剛好落在樣板左眼。"),
                ("96-97", "`warpAffine` 套用矩陣，輸出 112×112。超出原圖的部分用邊緣像素補。"),
            ]},
        ],
    },
    {
        "id": "embed", "phase": "背景辨識", "title": "FaceEmbedder：MobileFaceNet 算出 128 維特徵",
        "reqs": ["r23", "r24"],
        "summary": "用 ncnn 跑 MobileFaceNet，取 fc1 層的輸出，正規化成長度 1。兩個向量的內積就是 cosine 相似度。",
        "blocks": [
            {"file": "face_embedder.cpp", "start": 7, "end": 63, "anchor": "bool FaceEmbedder::load", "notes": [
                ("9-10", "ncnn 用 2 個執行緒，其他核心留給攝影機和偵測。`lightmode` 在推論途中釋放用不到的中間結果。"),
                ("11-20", "載入網路結構 `.param` 和權重 `.bin`。"),
                ("31-32", "`from_pixels` 把 OpenCV 的 BGR 位元組轉成 ncnn 的 float 張量，同時轉成 RGB。模型第一層已經做了 (x − 127.5) × 0.0078125 的正規化，這裡不用再減平均值。"),
                ("34-37", "一次推論。餵進 `data` 層，取出 `fc1` 層的輸出。"),
                ("40-43", "攤平並複製到 `std::vector<float>`，共 128 個數。"),
                ("44", "正規化成長度 1。"),
                ("48-55", "每個數除以向量長度。"),
                ("57-63", "兩個長度為 1 的向量，cosine 相似度就是內積。範圍 −1 到 1，越大越像。"),
            ]},
        ],
    },
    {
        "id": "match", "phase": "背景辨識", "title": "Gallery::match：學號還是 unknown",
        "reqs": ["r24", "g2"],
        "summary": "跟每位組員的範本算相似度。第一名夠高，而且明顯贏過第二名，才算認出。這就是 2.4 要的辨識演算法。",
        "blocks": [
            {"file": "gallery.cpp", "start": 79, "end": 100, "anchor": "MatchResult Gallery::match", "notes": [
                ("83-96", "掃一次所有人，同時記第一名和第二名。新的分數比第一名高時，原本的第一名退成第二名。"),
                ("97-98", "兩個條件都要過。`best ≥ 0.45` 擋住跟誰都不像的人。`best − second ≥ 0.08` 擋住跟兩位組員都差不多像的人。沒過的話 `index` 維持 -1，也就是 unknown（3.1.1）。"),
            ]},
        ],
    },
    {
        "id": "draw", "phase": "每一幀", "title": "draw_tracks：畫框和學號",
        "reqs": ["g1", "g2", "r21"],
        "summary": "穩定的 track 才畫。綠框顯示學號，紅框顯示 unknown，黃框顯示 ... 表示還在辨識。",
        "blocks": [
            {"file": "main.cpp", "start": 210, "end": 238, "anchor": "static cv::Scalar label_color", "notes": [
                ("212-214", "OpenCV 的顏色順序是 BGR，所以 (0, 0, 255) 是紅色，(0, 255, 255) 是黃色。"),
                ("223", "還沒被偵測到 `min_hits` 次的不畫。"),
                ("225-227", "標籤轉成文字。學號字串來自 gallery，也就是 enroll 時的資料夾名稱。"),
                ("230", "float 框四捨五入成整數像素。"),
                ("231", "畫框，線寬 2。"),
                ("233-236", "算文字大小，在框上方畫實心色塊再寫黑字。`std::max` 讓臉貼近畫面上緣時標籤不會跑出去。"),
            ]},
            {"file": "main.cpp", "start": 334, "end": 341, "anchor": "// 4. 畫框", "notes": [
                ("335", "畫所有框。"),
                ("338", "輸出到螢幕（下一步）。"),
                ("341", "拍到到顯示在螢幕上的總延遲。"),
            ]},
        ],
    },
    {
        "id": "show", "phase": "每一幀", "title": "Framebuffer::show：直接寫進螢幕記憶體",
        "reqs": ["r21"],
        "summary": "等比例縮放後置中，在螢幕記憶體上建一個 Mat，轉色時結果直接寫進螢幕。",
        "blocks": [
            {"file": "framebuffer.cpp", "start": 67, "end": 121, "anchor": "void Framebuffer::convert", "notes": [
                ("70-77", "依螢幕的 bpp 轉成對應的像素格式。LCD 的 16 bpp 用 RGB565。HDMI 的 32 bpp 依 `red_offset` 決定 RGBA 還是 BGRA。"),
                ("85-92", "每像素幾個 byte 對應的 Mat 型別。16 bpp 是 2 byte，所以是 `CV_8UC2`。"),
                ("97-98", "取寬、高比例較小的那個，整張圖放得進螢幕而且不變形。`display_fit` 是 0 時不放大。"),
                ("103-107", "需要縮放才 resize。"),
                ("109-113", "尺寸改變時才清黑邊，不用每幀清。"),
                ("117-119", "算置中的位置，直接在螢幕記憶體上建一個 Mat。最後一個參數 `line_length` 是每列的 byte 數。這個 Mat 不擁有記憶體，寫它就是寫螢幕。"),
                ("120", "`cvtColor` 的輸出直接落在螢幕上，不用先轉到暫存影像再複製。"),
            ]},
        ],
    },
    {
        "id": "tail", "phase": "結束", "title": "按鍵、效能報告、結束清理",
        "reqs": [],
        "summary": "q 離開，s 截圖。每 30 幀印一次各階段平均耗時。離開迴圈後依序關掉所有東西。",
        "blocks": [
            {"file": "main.cpp", "start": 343, "end": 373, "anchor": "int key = read_key", "notes": [
                ("343-344", "讀按鍵，q 跳出迴圈。"),
                ("345-350", "s 把畫好框的這一幀存成 `snapshot_N.jpg`。"),
                ("353-364", "每 30 幀印一次平均耗時再歸零。現場用這行判斷瓶頸在偵測、顯示還是攝影機。"),
                ("367-370", "清理順序。還原終端機，停辨識執行緒，停攝影機，解除螢幕映射。"),
            ]},
        ],
    },
]

KEYWORDS = set("""
alignas alignof and auto bool break case catch char class const constexpr continue default delete do double else enum
explicit extern false float for friend goto if inline int long mutable namespace new noexcept nullptr operator private
protected public return short signed sizeof static static_cast struct switch template this throw true try typedef
typename union unsigned using virtual void volatile while size_t uint32_t
""".split())

TOKEN = re.compile(
    r'(?P<c>//.*$)|(?P<s>"(?:\\.|[^"\\])*")|(?P<ch>\'(?:\\.|[^\'\\])\')|(?P<p>^\s*#\s*\w+)'
    r'|(?P<n>\b\d+(?:\.\d+)?f?\b)|(?P<id>[A-Za-z_]\w*)'
)


def highlight(line):
    out, pos = [], 0
    for m in TOKEN.finditer(line):
        out.append(html.escape(line[pos:m.start()]))
        text = html.escape(m.group(0))
        kind = m.lastgroup
        if kind == "id":
            out.append(f'<span class="k">{text}</span>' if m.group(0) in KEYWORDS else text)
        else:
            cls = {"c": "c", "s": "s", "ch": "s", "p": "p", "n": "n"}[kind]
            out.append(f'<span class="{cls}">{text}</span>')
        pos = m.end()
    out.append(html.escape(line[pos:]))
    return "".join(out)


def inline(text):
    parts = text.split("`")
    return "".join(f"<code>{html.escape(p)}</code>" if i % 2 else html.escape(p) for i, p in enumerate(parts))


def parse_range(r):
    a, _, b = r.partition("-")
    return int(a), int(b or a)


_cache = {}


def source(name):
    if name not in _cache:
        with open(os.path.join(SRC, name), encoding="utf-8") as f:
            _cache[name] = f.read().split("\n")
    return _cache[name]


def render_block(step_id, bi, block, errors):
    lines = source(block["file"])
    s, e = block["start"], block["end"]
    if e > len(lines):
        errors.append(f"{block['file']}: range {s}-{e} beyond {len(lines)} lines")
        return ""
    if block["anchor"] not in lines[s - 1]:
        errors.append(f"{block['file']}:{s} expected {block['anchor']!r}, found {lines[s - 1].strip()!r}")
    markers = {}
    notes_html = []
    for ni, (rng, text) in enumerate(block["notes"], 1):
        a, b = parse_range(rng)
        if not (s <= a <= b <= e):
            errors.append(f"{step_id}/{block['file']}: note {rng} outside {s}-{e}")
        markers.setdefault(a, ni)
        label = f"L{a}" if a == b else f"L{a}-{b}"
        notes_html.append(
            f'<li><button type="button" class="note" data-from="{a}" data-to="{b}" data-n="{ni}">'
            f'<span class="nn">{ni}</span><span class="nl">{label}</span><span class="nt">{inline(text)}</span></button></li>'
        )
    rows = []
    for n in range(s, e + 1):
        mk = markers.get(n)
        mk_html = f'<button type="button" class="mk" data-n="{mk}" aria-label="說明 {mk}">{mk}</button>' if mk else '<span class="mk"></span>'
        rows.append(f'<div class="row" data-l="{n}"><span class="no">{n}</span>{mk_html}<span class="src">{highlight(lines[n - 1]) or " "}</span></div>')
    bid = f"{step_id}-b{bi}"
    return (
        f'<div class="block" id="{bid}">'
        f'<div class="code-col"><div class="fname">lab3/face/src/{block["file"]} <span>L{s}-{e}</span></div>'
        f'<div class="code" tabindex="0"><div class="code-inner">{"".join(rows)}</div></div></div>'
        f'<ol class="notes">{"".join(notes_html)}</ol></div>'
    )


def main():
    errors = []
    req_by_id = {r[0]: r for r in REQS}
    steps_html, toc_html = [], []
    req_steps = {r[0]: [] for r in REQS}
    phase = None
    for i, st in enumerate(STEPS, 1):
        for r in st["reqs"]:
            req_steps[r].append((i, st))
        if st["phase"] != phase:
            phase = st["phase"]
            toc_html.append(f'<li class="toc-phase">{html.escape(phase)}</li>')
        toc_html.append(f'<li data-reqs="{" ".join(st["reqs"])}"><a href="#{st["id"]}"><span>{i}</span>{html.escape(st["title"])}</a></li>')
        chips = "".join(f'<span class="tag" data-r="{r}">{req_by_id[r][1]}</span>' for r in st["reqs"])
        blocks = "".join(render_block(st["id"], bi, b, errors) for bi, b in enumerate(st["blocks"]))
        steps_html.append(
            f'<section class="step" id="{st["id"]}" data-reqs="{" ".join(st["reqs"])}">'
            f'<header class="step-head"><div class="step-no">{i}</div><div class="step-meta">'
            f'<div class="phase">{html.escape(st["phase"])}</div><h2>{html.escape(st["title"])}</h2>'
            f'<p>{inline(st["summary"])}</p><div class="tags">{chips}</div></div></header>{blocks}</section>'
        )
    if errors:
        sys.exit("anchor/range check failed:\n  " + "\n  ".join(errors))

    req_rows = []
    filters = []
    for rid, num, name, spec in REQS:
        links = " ".join(f'<a href="#{st["id"]}">{i}</a>' for i, st in req_steps[rid])
        req_rows.append(f'<tr><td><span class="tag" data-r="{rid}">{num}</span></td><td>{html.escape(name)}</td><td>{html.escape(spec)}</td><td class="steps">{links}</td></tr>')
        filters.append(f'<button type="button" class="chip" data-r="{rid}" aria-pressed="false"><i></i>{num} {html.escape(name)}</button>')

    with open(os.path.join(HERE, "code_reader_template.html"), encoding="utf-8") as f:
        page = f.read()
    page = (page.replace("{{FILTERS}}", "".join(filters))
                .replace("{{REQ_ROWS}}", "".join(req_rows))
                .replace("{{TOC}}", "".join(toc_html))
                .replace("{{STEPS}}", "".join(steps_html)))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(page)
    print(f"wrote {OUT} ({len(STEPS)} steps, {sum(len(s['blocks']) for s in STEPS)} code blocks)")


if __name__ == "__main__":
    main()
