#pragma once

// Lab 2-3 的鍵盤處理：終端機（ssh / 序列埠）用 raw mode，
// 板子上直接插 USB 鍵盤時讀 /dev/input/event*
void keyboard_open ( );
void keyboard_close ( );
int read_key ( );       // 沒有按鍵時回傳 -1，不會卡住
