# Lab 1：交叉編譯 Hello World

目標：在 x86 開發機上用交叉編譯器編出 ARM 執行檔，傳到 E9V3 板子執行。

| 檔案 | 說明 |
|---|---|
| `hello_word.c` | 印出兩位組員學號組成的字串 |

## 編譯

```sh
arm-linux-gnueabihf-gcc hello_word.c -o hello_word
file hello_word      # 確認是 ARM（ELF 32-bit ARM, EABI5），不是 x86
```

## 傳到板子並執行

用 minicom 的 zmodem（Ctrl-A S）或 SD 卡把 `hello_word` 放到板子，然後：

```sh
chmod +x hello_word
./hello_word
```

若在開發機上直接執行會出現 `Exec format error`，那是正常的（它是 ARM 程式）。
