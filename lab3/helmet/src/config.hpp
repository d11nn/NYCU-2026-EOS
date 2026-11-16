#pragma once

#include <string>

// 所有可調參數都從 config.yml 讀，調整時不用重新編譯
struct Config
{
    // YOLO26s（ncnn）
    std::string model_param = "models/helmet.ncnn.param";
    std::string model_bin = "models/helmet.ncnn.bin";
    std::string input_blob = "in0";
    std::string output_blob = "out0";
    int input_size = 640;           // 模型輸入邊長（匯出時固定，不能改）
    int ncnn_threads = 4;

    // 偵測與合併
    double conf_threshold = 0.50;   // 信心門檻：在自備驗證集上 F1 最高
    double nms_iou = 0.45;          // 同一頂帽子的重複框，IoU 超過此值只留最高分
    double contain = 0.70;          // 小框有 70% 以上落在另一個框內，視為同一人重複框
    int tile_grid = 2;              // 額外切 n x n 塊各跑一次（0 = 只跑整張圖）
    double tile_overlap = 0.2;      // 切塊之間的重疊比例
    double tile_max_box = 32;       // 切塊只保留在整張圖（模型尺度）小於這個像素的框

    // 輸出
    std::string fb_device = "/dev/fb2";     // HDMI（/dev/fb0 是 LCD）
    int jpeg_quality = 95;
    int wait_enter = 1;             // 1：顯示後等按 Enter 才結束

    bool load ( const std::string &path );
};
