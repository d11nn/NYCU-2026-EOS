#pragma once

#include "config.hpp"
#include <opencv2/core/core.hpp>
#include <deque>
#include <utility>
#include <vector>

// 顯示用的標籤狀態
enum
{
    LABEL_PENDING = -2,     // 還沒有辨識結果，顯示 "..."
    LABEL_UNKNOWN = -1      // 判定為陌生人
};                          // >= 0：Gallery 裡第幾位組員

struct Track
{
    int id = 0;
    cv::Rect2f box;                         // 平滑後的框（原圖座標）
    int hits = 0;                           // 累計被偵測到的幀數
    int missed = 0;                         // 連續沒被偵測到的幀數（> 0 時不送辨識）
    double last_seen = 0;                   // 最後一次被偵測到的時間（秒）
    std::deque<std::pair<int, float> > votes;   // 最近幾次辨識結果：(label, 相似度)
    int shown_label = LABEL_PENDING;        // 目前畫在畫面上的標籤
    double last_request = -1e9;             // 上次送去辨識的時間（秒）
    bool pending = false;                   // 是否有辨識工作還在背景執行

    bool visible ( int min_hits ) const { return hits >= min_hits; }
    float mean_score ( int label ) const;   // 某個標籤在投票中的平均相似度
};

// 把每一幀的偵測框對應到同一張臉（track），並做平滑、去重複和投票
class Tracker
{
public:
    void configure ( const Config &cfg );
    void update ( const std::vector<cv::Rect> &detections, double now );
    void add_vote ( int track_id, int label, float score );
    void resolve_duplicates ( );    // 同一位組員只能出現在一個框上

    std::vector<Track> &tracks ( ) { return list; }
    Track *find ( int track_id );

private:
    void merge_overlapping_tracks ( );

    std::vector<Track> list;
    int next_id = 1;
    float iou_match = 0.3f;
    float center_match = 0.6f;
    float ema_alpha = 0.4f;
    float snap_distance = 0.25f;
    float overlap_merge = 0.6f;
    double track_timeout = 0.4;
    int vote_window = 7;
    int vote_min = 3;
};

float iou ( const cv::Rect2f &a, const cv::Rect2f &b );
