#pragma once

#include "config.hpp"
#include <opencv2/core/core.hpp>
#include <opencv2/objdetect/objdetect.hpp>
#include <vector>

// Haar cascade 人臉偵測：在縮小的灰階影像上偵測，座標再放大回原圖
class FaceDetector
{
public:
    bool load ( const Config &cfg );
    std::vector<cv::Rect> detect ( const cv::Mat &bgr );

private:
    cv::CascadeClassifier cascade;
    int detect_width = 320;
    double scale_factor = 1.15;
    int min_neighbors = 4;
    int min_face = 30;
    double overlap_merge = 0.6;
};

// 重疊（或一個包住另一個）的框只留最大的那個。
// overlap = 交集面積 / 較小框的面積，超過 threshold 就視為同一張臉
std::vector<cv::Rect> merge_overlaps ( std::vector<cv::Rect> boxes, double threshold );
