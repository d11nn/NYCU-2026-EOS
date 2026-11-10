#pragma once

#include <string>

// 所有可調參數都從 config.yml 讀，調整時不用重新編譯
struct Config
{
    // 攝影機與螢幕
    int camera_index = 2;
    int camera_width = 640;
    int camera_height = 480;
    std::string fb_device = "/dev/fb0";
    int display_fit = 0;            // 1：放大塞滿螢幕；0：原尺寸置中顯示（HDMI 1080p 上快很多）

    // Haar 偵測
    std::string face_cascade = "models/haarcascade_frontalface_default.xml";
    std::string eye_cascade = "models/haarcascade_eye.xml";
    int detect_width = 320;         // 偵測前先把影像縮到這個寬度
    double scale_factor = 1.15;
    int min_neighbors = 4;
    int min_face = 30;              // 縮小後影像上的最小臉寬（像素）
    double overlap_merge = 0.6;     // 兩個偵測框重疊超過小框面積的這個比例，就只留大的

    // MobileFaceNet（ncnn）
    std::string model_param = "models/mobilefacenet.param";
    std::string model_bin = "models/mobilefacenet.bin";
    std::string input_blob = "data";
    std::string output_blob = "fc1";
    int input_rgb = 1;              // 1：送 RGB 給模型；0：送 BGR
    int ncnn_threads = 2;

    // 身分判定
    std::string gallery = "gallery.yml";
    double sim_threshold = 0.45;    // cosine 相似度低於此值 → unknown
    double sim_margin = 0.08;       // 第一名和第二名差距小於此值 → unknown

    // 追蹤與穩定
    double recog_interval = 1.0;    // 每個 track 每隔幾秒重新辨識一次
    int vote_window = 7;            // 投票看最近幾次辨識結果
    int vote_min = 3;               // 至少幾票才改變顯示的標籤
    double iou_match = 0.3;         // IoU 大於此值就算同一張臉
    double center_match = 1.0;      // 或者：兩框中心距離 < 臉寬 × 此值，也算同一張臉（移動快時 IoU 會太小）
    double ema_alpha = 0.4;         // 框座標平滑係數，越小越穩但越慢跟上
    double snap_distance = 0.25;    // 中心移動超過臉寬 × 此值，視為真的在移動，框直接跳到新位置不平滑
    int min_hits = 2;               // 累計偵測到幾幀才顯示（濾掉偶發誤報）
    double track_timeout = 0.4;     // 多久（秒）沒被偵測到就移除框；用時間而不是幀數，不受 FPS 影響

    bool load ( const std::string &path );
};
