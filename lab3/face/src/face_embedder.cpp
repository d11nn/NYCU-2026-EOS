#include "face_embedder.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

bool FaceEmbedder::load ( const Config &cfg )
{
    net.opt.num_threads = cfg.ncnn_threads;
    net.opt.lightmode = true;       // 推論中途釋放用不到的中間結果，省記憶體
    if ( net.load_param ( cfg.model_param.c_str ( ) ) != 0 )
    {
        std::cerr << "Could not load " << cfg.model_param << std::endl;
        return false;
    }
    if ( net.load_model ( cfg.model_bin.c_str ( ) ) != 0 )
    {
        std::cerr << "Could not load " << cfg.model_bin << std::endl;
        return false;
    }
    input_blob = cfg.input_blob;
    output_blob = cfg.output_blob;
    input_rgb = cfg.input_rgb != 0;
    return true;
}

std::vector<float> FaceEmbedder::embed ( const cv::Mat &aligned_bgr )
{
    // 影像轉成 ncnn::Mat。這個 MobileFaceNet 模型的 param 檔第一層已經內含
    // (x - 127.5) * 0.0078125 的正規化，所以這裡不用再減平均值
    int type = input_rgb ? ncnn::Mat::PIXEL_BGR2RGB : ncnn::Mat::PIXEL_BGR;
    ncnn::Mat in = ncnn::Mat::from_pixels ( aligned_bgr.data, type, aligned_bgr.cols, aligned_bgr.rows );

    ncnn::Extractor ex = net.create_extractor ( );
    ex.input ( input_blob.c_str ( ), in );
    ncnn::Mat out;
    ex.extract ( output_blob.c_str ( ), out );

    // 攤平成一維（如果輸出是 c×h×w 的 3D blob，reshape 會處理 ncnn 每個 channel 的對齊間隔）
    ncnn::Mat flat = out.reshape ( out.w * out.h * out.c );
    std::vector<float> feature ( flat.w );
    const float *p = (const float *) flat.data;
    for ( size_t i = 0; i < feature.size ( ); i++ ) feature[i] = p[i];
    l2_normalize ( feature );
    return feature;
}

void l2_normalize ( std::vector<float> &v )
{
    double sum = 0;
    for ( size_t i = 0; i < v.size ( ); i++ ) sum += (double) v[i] * v[i];
    double norm = std::sqrt ( sum );
    if ( norm < 1e-12 ) return;
    for ( size_t i = 0; i < v.size ( ); i++ ) v[i] = (float) ( v[i] / norm );
}

float cosine_similarity ( const std::vector<float> &a, const std::vector<float> &b )
{
    double dot = 0;
    size_t n = std::min ( a.size ( ), b.size ( ) );
    for ( size_t i = 0; i < n; i++ ) dot += (double) a[i] * b[i];
    return (float) dot;
}
