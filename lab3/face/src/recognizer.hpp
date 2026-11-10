#pragma once

#include "config.hpp"
#include "face_aligner.hpp"
#include "face_embedder.hpp"
#include "gallery.hpp"
#include <opencv2/core/core.hpp>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

struct RecogJob
{
    int track_id;
    cv::Mat region;     // 臉附近的一小塊影像（已複製，主執行緒可以繼續改原圖）
    cv::Rect face;      // 臉框在 region 裡的位置
};

struct RecogResult
{
    int track_id;
    int label;          // Gallery 索引，或 LABEL_UNKNOWN
    float score;        // 最高相似度
    double millis;      // 對齊 + 推論花的時間
};

// 背景辨識執行緒：對齊 → MobileFaceNet → 跟 Gallery 比對。
// 寫法延續 Lab 2 的 ScreenshotWriter（mutex + condition_variable 的工作佇列）
class Recognizer
{
public:
    bool init ( const Config &cfg, const Gallery *gallery );
    void start ( );
    void finish ( );

    bool push ( const RecogJob &job );                  // 佇列滿時丟棄並回傳 false
    std::vector<RecogResult> take_results ( );          // 取走目前所有完成的結果

private:
    void loop ( );

    FaceAligner aligner;
    FaceEmbedder embedder;
    const Gallery *gallery = nullptr;
    float threshold = 0.45f, margin = 0.08f;

    std::queue<RecogJob> jobs;
    std::vector<RecogResult> results;
    std::mutex mtx;
    std::condition_variable cond;
    std::thread worker;
    bool stopping = false;
    static const size_t MAX_JOBS = 4;   // 板子沒有 swap，佇列一定要有上限
};
