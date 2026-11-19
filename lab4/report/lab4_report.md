# Lab 4：在 E9V3 上建置 MP3 播放器

- 組別：30
- 組員：ep（314581047）

## 一、實驗目標與環境

本實驗只使用課程提供的六個原始碼套件（不使用 apt-get），在 Ubuntu 上交叉編譯出 madplay 與 aplay，再到 E9V3 開發板上播放 `music.mp3`，並用麥克風錄製 `.wav` 檔後回放。

| 項目 | 內容 |
| --- | --- |
| 開發板 | Embedsky E9V3（TQIMX6Q，ARM Cortex-A9） |
| 交叉編譯器 | gcc-linaro-5.3-2016.02-x86_64_arm-linux-gnueabihf（位於 `/opt/EmbedSky/`，指令前綴為 `arm-linux-gnueabihf-`） |
| 原始碼套件 | zlib-1.2.3、libid3tag-0.15.1b、libmad-0.15.1b、madplay-0.15.2b、alsa-lib-1.0.26、alsa-utils-1.0.26 |

## 二、問題分析

### 2.1 為什麼板子原本無法播放音訊

板子上已經有 ALSA 音效卡驅動，但它只存在於核心（kernel space）。驅動只提供 `/dev/snd/pcmC0D0p` 這類裝置檔，並且只接受原始 PCM 樣本。它不會解析 WAV 或 MP3 的檔案格式，也不會自己決定取樣率與聲道數。

要播放聲音，使用者空間（user space）必須有程式依序完成以下工作：

1. 讀取並解析音訊檔案。如果是 MP3，還要先解碼成 PCM。
2. 開啟 PCM 裝置，透過 ioctl 設定硬體參數（格式、聲道、取樣率、buffer 大小）。
3. 持續把 PCM 樣本寫進驅動。

板子上原本沒有任何程式或函式庫在做這些事，所以連不需要解碼的 `.wav` 都無法播放。我們沒辦法直接執行 `cat a.wav > /dev/snd/pcmC0D0p`，因為 ALSA 的 PCM 裝置必須先透過 ioctl 設定好參數，才會接收資料。

### 2.2 各元件的角色

| 元件 | 所在層級 | 作用 |
| --- | --- | --- |
| ALSA 驅動 | 核心 | 驅動音效晶片（codec），提供 `/dev/snd/*`，用 DMA 把 PCM 送到硬體。板子原本就有。 |
| alsa-lib（libasound） | 使用者空間函式庫 | 提供 `snd_pcm_open()`、`snd_pcm_writei()` 等 API，並把它們轉成 ioctl。也負責讀取 `alsa.conf` 設定檔。 |
| alsa-utils | 使用者空間程式 | 提供 `aplay`（播放 WAV）、`arecord`（錄音）、`amixer`（調整音量與靜音）。 |
| libmad | 使用者空間函式庫 | MPEG 音訊解碼器，以定點數運算把 MP3 解成 PCM。 |
| libid3tag | 使用者空間函式庫 | 讀取 MP3 的 ID3 標籤。編譯 madplay 時必須有它。 |
| zlib | 使用者空間函式庫 | 壓縮函式庫，libid3tag 需要它來處理壓縮過的 ID3v2 frame。 |
| madplay | 使用者空間程式 | 呼叫 libmad 解碼 MP3，輸出 WAV 串流。 |

aplay 只能播放 WAV，不能解碼 MP3；madplay 負責解碼 MP3。因此本實驗用 pipe 把兩者串起來：

```
madplay -o wav:- music.mp3 | aplay
```

套件之間的相依關係決定了編譯順序：

```
zlib --> libid3tag --+
                     +--> madplay
libmad --------------+

alsa-lib --> alsa-utils (aplay / arecord / amixer)
```

## 三、建置流程

所有編譯都在 Ubuntu 虛擬機上進行。原因是交叉編譯器安裝在 Ubuntu 上，而且這些 2000 年代的 autotools 套件在 Linux 上編譯比較順利。

### 3.1 把檔案從 macOS 傳到 Ubuntu

```
scp -O -r ~/nycu/eos/NYCU-2026-EOS/lab4 ubuntu@192.168.56.110:~/
```

`-O` 的原因請見第四節的問題 1。

### 3.2 設定環境變數

```
export PREFIX=$HOME/lab4/install HOST=arm-linux-gnueabihf \
       CPPFLAGS="-I$HOME/lab4/install/include" LDFLAGS="-L$HOME/lab4/install/lib"
```

- `PREFIX`：所有產物都安裝到同一個目錄。這樣不會把 ARM 程式裝進 Ubuntu 的系統目錄，之後打包也比較方便。
- `HOST`：告訴 configure 目標平台是 ARM Linux，這是開啟交叉編譯的開關。
- `CPPFLAGS` / `LDFLAGS`：讓後面的套件可以找到前面已安裝的標頭檔與函式庫。

### 3.3 解壓縮

```
cd ~/lab4/lab4_package
for f in *.tar.gz; do tar xzf "$f"; done
for f in *.tar.bz2; do tar xjf "$f"; done
```

### 3.4 zlib

```
cd zlib-1.2.3
CC=arm-linux-gnueabihf-gcc AR="arm-linux-gnueabihf-ar rc" \
RANLIB=arm-linux-gnueabihf-ranlib ./configure --prefix=$PREFIX --shared
make && make install
```

zlib 的 configure 是手寫的，不支援 `--host`，所以要直接用 `CC`、`AR`、`RANLIB` 指定交叉編譯工具。

### 3.5 libid3tag

```
cd ../libid3tag-0.15.1b
./configure --host=$HOST --prefix=$PREFIX
make && make install
```

### 3.6 libmad

```
cd ../libmad-0.15.1b
sed -i 's/-fforce-mem//g' configure
./configure --host=$HOST --prefix=$PREFIX CFLAGS="-O2 -marm"
make && make install
```

- `-fforce-mem` 是舊版 gcc 的參數，gcc 4.3 以後已經移除，必須從 configure 中刪掉。
- `-marm` 的原因請見第四節的問題 2。

### 3.7 madplay

```
cd ../madplay-0.15.2b
./configure --host=$HOST --prefix=$PREFIX --disable-nls
make && make install
```

`--disable-nls` 會關閉多國語言翻譯，避免需要額外的 gettext 函式庫。

### 3.8 alsa-lib

```
cd ../alsa-lib-1.0.26
./configure --host=$HOST --prefix=$PREFIX --disable-python CC=arm-linux-gnueabihf-gcc
make && make install
```

- 不需要 Python 綁定，所以用 `--disable-python` 關掉。
- `CC=` 的原因請見第四節的問題 3。

### 3.9 alsa-utils

```
cd ../alsa-utils-1.0.26
./configure --host=$HOST --prefix=$PREFIX CC=arm-linux-gnueabihf-gcc \
  --with-alsa-prefix=$PREFIX/lib --with-alsa-inc-prefix=$PREFIX/include \
  --disable-alsamixer --disable-xmlto --disable-nls
make
make install udevrulesdir=$PREFIX/lib/udev/rules.d
```

- `--with-alsa-*`：指定使用剛剛編好的 ARM 版 alsa-lib，而不是 Ubuntu 系統上的 x86 版。
- `--disable-alsamixer`：alsamixer 需要 ncurses，而且 `amixer` 已經足夠使用。
- `--disable-xmlto`：不產生 man page 文件。
- `udevrulesdir=` 的原因請見第四節的問題 4。

### 3.10 驗證與打包

```
file $PREFIX/bin/madplay $PREFIX/bin/aplay $PREFIX/lib/libmad.so.0 $PREFIX/lib/libasound.so.2
cd ~/lab4 && tar czf lab4_board.tar.gz -C install bin lib share
```

`file` 指令的結果應該顯示 `ELF 32-bit LSB ... ARM, EABI5`，表示這些檔案確實是 ARM 版本。打包時只需要 `bin`（執行檔）、`lib`（共享函式庫）、`share/alsa`（alsa-lib 設定檔）三個目錄，`include` 只有編譯時才用得到。

## 四、遇到的問題與解決方法

### 問題 1：scp 上傳失敗（path canonicalization failed）

**錯誤訊息：**

```
scp: realpath ./lab4: No such file
scp: upload "./lab4": path canonicalization failed
```

**原因：** macOS 上的 OpenSSH 是 9.0 以後的版本，`scp` 預設改用 SFTP 協定傳輸。上傳目錄之前，它會先向遠端查詢目標路徑的完整路徑；但 Ubuntu 14.04 的舊版 sshd 無法對尚未存在的路徑做 realpath，所以上傳失敗。

**解法：** 加上 `-O`，改回使用舊版的 SCP 協定。

### 問題 2：libmad 編譯時組譯器報錯（Thumb mode）

**錯誤訊息：**

```
Error: selected processor does not support Thumb mode `rsc r7,r7,#0'
make[2]: *** [synth.lo] Error 1
```

**原因：** configure 偵測到目標平台是 ARM，因此啟用了 `FPM_ARM`，也就是在 `fixed.h` 中使用 ARM 組合語言加速定點數乘法。但 linaro gcc 5.3 預設產生 Thumb-2 指令，而 `rsc`（reverse subtract with carry）在 Thumb-2 中不存在，所以組譯失敗。

**解法：** 先執行 `make distclean`，再用 `CFLAGS="-O2 -marm"` 重新 configure，強制 libmad 以 ARM 模式編譯。這樣可以保留組合語言的加速效果。Cortex-A9 同時支援 ARM 與 Thumb 指令集，兩種模式的程式碼可以混用。另一個替代方案是 `--enable-fpm=default`，改用純 C 的定點數運算。

### 問題 3：alsa-lib configure 失敗（C compiler cannot create executables）

**錯誤訊息：**

```
checking for cross-compiler... arm-arm-unknown-linux-gnueabihf-gcc
configure: error: C compiler cannot create executables
```

**原因：** alsa-lib 1.0.26 的 configure 會自己推測交叉編譯器的名稱，方式是把「CPU 名稱」和「正規化後的完整 host 名稱」接起來，結果得到不存在的 `arm-arm-unknown-linux-gnueabihf-gcc`。

**解法：** 用 `CC=arm-linux-gnueabihf-gcc` 明確指定編譯器。alsa-utils 也加上同樣的參數，避免發生相同的問題。

### 問題 4：alsa-utils 執行 make install 時 Permission denied

**錯誤訊息：**

```
/usr/bin/install -c -m 644 90-alsa-restore.rules '/lib/udev/rules.d'
/usr/bin/install: cannot remove '/lib/udev/rules.d/90-alsa-restore.rules': Permission denied
```

**原因：** 編譯本身已經成功。但 alsactl 的 udev 規則檔安裝路徑寫死為 `/lib/udev/rules.d`，不會跟著 `--prefix` 改變，而一般使用者沒有寫入這個系統目錄的權限。

**解法：** 不使用 sudo，因為那樣會把 ARM 版本的設定檔寫進 Ubuntu 主機。改成在 make 時覆蓋 Makefile 變數：`make install udevrulesdir=$PREFIX/lib/udev/rules.d`。這樣不需要重新 configure 或重新編譯。

### 問題 5：板子上執行 madplay 找不到 libmad.so.0

**錯誤訊息：**

```
madplay: error while loading shared libraries: libmad.so.0: cannot open shared object file: No such file or directory
```

**原因：** 設定函式庫路徑時只打了 `LD_LIBRARY_PATH=/home/embedsky/lab4/install/lib`，沒有加 `export`。沒有 export 的變數只存在於目前的 shell，不會傳給 shell 啟動的子程序，所以 madplay 的動態連結器（ld.so）看不到這個路徑，在系統預設的 `/lib`、`/usr/lib` 中也找不到 `libmad.so.0`。

**解法：** 改用 `export LD_LIBRARY_PATH=/home/embedsky/lab4/install/lib`。

### 問題 6：部署路徑與編譯時的 prefix 不同

**原因：** 編譯時的 `--prefix` 是 Ubuntu 上的 `/home/ubuntu/lab4/install`，但板子上的使用者目錄是 `/home/embedsky`，檔案實際放在 `/home/embedsky/lab4/install`。共享函式庫可以用 `LD_LIBRARY_PATH` 指定位置，但 alsa-lib 的設定檔路徑在編譯時已經寫死成 `/home/ubuntu/lab4/install/share/alsa/alsa.conf`。放在別的地方，aplay 會找不到 ALSA 的設定。

**解法：** 在板子上建立 symlink，讓編譯時寫死的路徑指到實際的位置，不需要重新編譯：

```
mkdir -p /home/ubuntu/lab4
ln -s /home/embedsky/lab4/install /home/ubuntu/lab4/install
```

## 五、上板執行

### 5.1 部署

<!-- 待補：說明用什麼方式把檔案傳到板子上（SD 卡 / USB） -->

把 `lab4_board.tar.gz` 和 `music.mp3` 傳到板子的 `/home/embedsky/lab4`，然後執行：

```
cd /home/embedsky/lab4
mkdir -p install
tar xzf lab4_board.tar.gz -C install
mkdir -p /home/ubuntu/lab4
ln -s /home/embedsky/lab4/install /home/ubuntu/lab4/install
export PATH=/home/embedsky/lab4/install/bin:$PATH
export LD_LIBRARY_PATH=/home/embedsky/lab4/install/lib
```

- `PATH`：讓 shell 找得到 madplay、aplay 等執行檔。
- `LD_LIBRARY_PATH`：讓執行檔找得到 `libmad.so`、`libasound.so` 等共享函式庫。必須用 `export`，原因請見問題 5。
- symlink：讓 alsa-lib 可以在編譯時寫死的路徑找到 `alsa.conf`，原因請見問題 6。

### 5.2 播放 MP3（Demo 2.1）

```
madplay --version
aplay -l
madplay -o wav:- music.mp3 | aplay
```

- `madplay --version`：確認執行檔和共享函式庫都能正確載入。
- `aplay -l`：確認系統有偵測到音效卡。
- 最後一行用 madplay 把 MP3 解碼成 WAV 串流，再透過 pipe 交給 aplay 播放。實測可以從耳機孔正常聽到音樂。

<!-- 待補：播放成功的截圖 -->

### 5.3 錄音與回放（Demo 2.2）

```
arecord -l
amixer scontrols
arecord -f cd -d 5 test.wav
aplay test.wav
```

`-f cd` 表示 16-bit、44100 Hz、雙聲道；`-d 5` 表示錄 5 秒。錄好的 WAV 不需要解碼，可以直接用 aplay 播放。如果錄到的是一片無聲，要用 `amixer` 開啟麥克風的 Capture 開關。

<!-- 待補：錄音與播放的截圖 -->

## 六、MP3 播放的訊號處理流程

MP3 是經過心理聲學模型去除人耳聽不到的成分，再以 Huffman 編碼壓縮的頻域資料。播放 MP3 就是把這個過程反過來，還原成時域的 PCM 樣本，再交給硬體轉成類比訊號。完整流程如下：

```
music.mp3
  |
  | (1) madplay 讀檔，把資料一塊一塊餵給 libmad
  v
[ libmad ]
  (2) 尋找 frame 同步字，解析 header（位元率、取樣率、聲道模式）
  (3) Huffman 解碼，得到量化後的頻譜係數
  (4) 反量化（requantize），還原頻譜係數的大小
  (5) 立體聲處理（M/S stereo、intensity stereo）
  (6) 混疊消除（alias reduction）
  (7) IMDCT：頻域轉回時域，再做重疊相加（overlap-add）
  (8) 多相合成濾波器組（polyphase synthesis filterbank）輸出 PCM
  |
  |  定點數 PCM（mad_fixed_t，約 28-bit 精度）
  v
[ madplay ]
  (9)  縮減為 16-bit 樣本（捨入或 dither）
  (10) 加上 WAV header，寫到 stdout
  |
  |  pipe
  v
[ aplay ]
  (11) 從 stdin 讀取 WAV header，取得 44100 Hz、雙聲道、S16_LE 等資訊
  (12) 開啟 "default" 裝置，設定 hw_params
  (13) 迴圈：讀一塊 PCM，呼叫 snd_pcm_writei() 寫出
  v
[ alsa-lib ]
  (14) 讀取 alsa.conf，把 "default" 解析為實際的硬體裝置
  (15) 硬體不支援該格式時，由 plug 外掛進行格式或取樣率轉換
  (16) 把 API 呼叫轉成 ioctl（HW_PARAMS、PREPARE、WRITEI_FRAMES）
  |
  ======== user space / kernel space ========
  v
[ ALSA 核心驅動 ]
  (17) 把 PCM 樣本放進 ring buffer
  (18) DMA 把資料經 I2S 送到 codec；每播完一個 period 就發出中斷，通知可以繼續寫入
  v
[ codec 晶片 ]
  (19) DAC 把數位樣本轉成類比電壓，經耳機放大器輸出到耳機孔
  v
喇叭 / 耳機
```

錄音則是反方向：麥克風 → codec 的 ADC → I2S / DMA → ALSA 驅動 → alsa-lib → arecord 加上 WAV header → `.wav` 檔。

## 七、參考資料

1. Underbit Technologies, *MAD: MPEG Audio Decoder*. https://www.underbit.com/products/mad/
2. ALSA Project, *Advanced Linux Sound Architecture*. https://www.alsa-project.org/
3. ALSA Project, *ALSA library API reference*. https://www.alsa-project.org/alsa-doc/alsa-lib/
4. ISO/IEC 11172-3:1993, *Coding of moving pictures and associated audio — Part 3: Audio*.
5. OpenSSH 9.0 Release Notes（scp 預設改用 SFTP 協定）. https://www.openssh.com/txt/release-9.0
6. Free Software Foundation, *GNU Make Manual — Overriding Variables*. https://www.gnu.org/software/make/manual/make.html#Overriding
7. Arm Ltd., *ARM Architecture Reference Manual, ARMv7-A and ARMv7-R edition*（RSC 指令與 Thumb-2 指令集）.
8. 本課程 Lab 4 講義（115_lab4.pdf）.
