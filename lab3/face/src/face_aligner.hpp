#pragma once

#include <opencv2/core/core.hpp>
#include <opencv2/objdetect/objdetect.hpp>
#include <string>

// 把臉轉正並裁成 MobileFaceNet 要的 112x112：
// 讓兩眼落在 ArcFace 標準樣板的眼睛位置（兩眼水平、距離固定）
class FaceAligner
{
public:
    bool load ( const std::string &eye_cascade_path );

    // face 是 image 上的臉框。eyes_found 告訴呼叫端是真的找到眼睛，還是用比例估計的
    cv::Mat align ( const cv::Mat &bgr, const cv::Rect &face, bool *eyes_found = nullptr );

private:
    bool find_eyes ( const cv::Mat &bgr, const cv::Rect &face, cv::Point2f &left, cv::Point2f &right );

    cv::CascadeClassifier eye_cascade;
};
