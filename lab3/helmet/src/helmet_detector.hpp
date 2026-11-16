#pragma once

#include "config.hpp"

#include <net.h>
#include <opencv2/core/core.hpp>
#include <vector>

// 一個偵測框（原圖座標）
struct Box
{
    float x1, y1, x2, y2;
    float score;
};

// YOLO26s 戴安全帽偵測：整張圖跑一次 + 切塊各跑一次 → NMS + 去掉包含框
class HelmetDetector
{
public:
    bool load ( const Config &cfg );

    // 回傳最終的框（已合併、已裁到圖內）
    std::vector<Box> find_helmets ( const cv::Mat &bgr );

    int passes ( ) const { return pass_count; }    // 上一次 find_helmets 跑了幾次模型

private:
    // 對一張圖（或一個切塊）跑一次模型，回傳該圖座標的框
    std::vector<Box> detect ( const cv::Mat &bgr );

    ncnn::Net net;
    Config cfg;
    int pass_count = 0;
};

// 把 w x h 的圖切成 n x n 塊（互相重疊 overlap）
std::vector<cv::Rect> make_tiles ( int w, int h, int n, double overlap );

// NMS 去掉重疊框，再去掉被另一個框包住的框（避免同一人兩個框）
std::vector<Box> merge_boxes ( std::vector<Box> boxes, double nms_iou, double contain );

float box_iou ( const Box &a, const Box &b );
