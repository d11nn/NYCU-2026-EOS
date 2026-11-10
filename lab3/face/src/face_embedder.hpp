#pragma once

#include "config.hpp"
#include <opencv2/core/core.hpp>
#include <net.h>        // ncnn
#include <string>
#include <vector>

// MobileFaceNet：112x112 的臉 → 128 維特徵向量（L2 正規化，長度為 1）
class FaceEmbedder
{
public:
    bool load ( const Config &cfg );
    std::vector<float> embed ( const cv::Mat &aligned_bgr );

private:
    ncnn::Net net;
    std::string input_blob, output_blob;
    bool input_rgb = true;
};

// 兩個已正規化向量的 cosine 相似度就是內積，範圍 [-1, 1]，越大越像
float cosine_similarity ( const std::vector<float> &a, const std::vector<float> &b );
void l2_normalize ( std::vector<float> &v );
