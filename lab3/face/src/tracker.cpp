#include "tracker.hpp"

#include <algorithm>
#include <cmath>
#include <map>

float iou ( const cv::Rect2f &a, const cv::Rect2f &b )
{
    float inter = ( a & b ).area ( );
    float uni = a.area ( ) + b.area ( ) - inter;
    return uni > 0 ? inter / uni : 0.0f;
}

float Track::mean_score ( int label ) const
{
    float sum = 0;
    int n = 0;
    for ( size_t i = 0; i < votes.size ( ); i++ )
    {
        if ( votes[i].first == label )
        {
            sum += votes[i].second;
            n++;
        }
    }
    return n ? sum / n : -1.0f;
}

void Tracker::configure ( const Config &cfg )
{
    iou_match = (float) cfg.iou_match;
    center_match = (float) cfg.center_match;
    ema_alpha = (float) cfg.ema_alpha;
    snap_distance = (float) cfg.snap_distance;
    overlap_merge = (float) cfg.overlap_merge;
    track_timeout = cfg.track_timeout;
    vote_window = cfg.vote_window;
    vote_min = cfg.vote_min;
}

// 兩個框中心的距離，除以框 a 的寬度（以「幾個臉寬」為單位，跟臉離鏡頭多遠無關）
static float center_distance ( const cv::Rect2f &a, const cv::Rect2f &b )
{
    float dx = ( a.x + a.width * 0.5f ) - ( b.x + b.width * 0.5f );
    float dy = ( a.y + a.height * 0.5f ) - ( b.y + b.height * 0.5f );
    return std::sqrt ( dx * dx + dy * dy ) / std::max ( a.width, 1.0f );
}

Track *Tracker::find ( int track_id )
{
    for ( size_t i = 0; i < list.size ( ); i++ )
        if ( list[i].id == track_id ) return &list[i];
    return nullptr;
}

void Tracker::update ( const std::vector<cv::Rect> &detections, double now )
{
    // 1. 找出所有可能是同一張臉的 (track, 偵測框) 配對：
    //    IoU 夠大，或中心距離夠近（FPS 低、人移動快時 IoU 會很小，靠中心距離補救）；
    //    大小差太多（超過 2 倍）的不配，避免臉的框被背景小雜點搶走
    struct Pair { float distance; int t; int d; };
    std::vector<Pair> pairs;
    for ( size_t t = 0; t < list.size ( ); t++ )
        for ( size_t d = 0; d < detections.size ( ); d++ )
        {
            cv::Rect2f det ( detections[d] );
            float size_ratio = det.width / std::max ( list[t].box.width, 1.0f );
            if ( size_ratio < 0.5f || size_ratio > 2.0f ) continue;
            float dist = center_distance ( list[t].box, det );
            if ( iou ( list[t].box, det ) >= iou_match || dist <= center_match )
                pairs.push_back ( { dist, (int) t, (int) d } );
        }
    // 距離最近的先配
    std::sort ( pairs.begin ( ), pairs.end ( ), [] ( const Pair &a, const Pair &b ) { return a.distance < b.distance; } );

    std::vector<bool> track_used ( list.size ( ), false ), det_used ( detections.size ( ), false );
    for ( size_t i = 0; i < pairs.size ( ); i++ )
    {
        if ( track_used[pairs[i].t] || det_used[pairs[i].d] ) continue;
        track_used[pairs[i].t] = det_used[pairs[i].d] = true;

        // 2. 配對成功，更新框的位置：
        //    小幅變動（Haar 每幀框的大小略有不同）→ EMA 平滑：new = alpha * 偵測 + (1 - alpha) * 舊值，消除抖動
        //    大幅移動（人真的在動）→ 直接跳到新位置，框才不會拖在臉後面
        Track &tr = list[pairs[i].t];
        cv::Rect2f det ( detections[pairs[i].d] );
        float alpha = pairs[i].distance > snap_distance ? 1.0f : ema_alpha;
        tr.box.x = alpha * det.x + ( 1 - alpha ) * tr.box.x;
        tr.box.y = alpha * det.y + ( 1 - alpha ) * tr.box.y;
        tr.box.width = alpha * det.width + ( 1 - alpha ) * tr.box.width;
        tr.box.height = alpha * det.height + ( 1 - alpha ) * tr.box.height;
        tr.hits++;
        tr.missed = 0;
        tr.last_seen = now;
    }

    // 3. 這一幀沒被偵測到的 track：短時間內保留舊框（防閃爍），超過 track_timeout 秒才刪掉。
    //    用時間而不是幀數：FPS 低時，人走掉後舊框也不會殘留好幾秒
    for ( size_t t = 0; t < list.size ( ); t++ )
        if ( !track_used[t] ) list[t].missed++;
    list.erase ( std::remove_if ( list.begin ( ), list.end ( ),
                                  [this, now] ( const Track &tr ) { return now - tr.last_seen > track_timeout; } ),
                 list.end ( ) );

    // 4. 沒配對到的偵測框：建立新的 track
    for ( size_t d = 0; d < detections.size ( ); d++ )
    {
        if ( det_used[d] ) continue;
        Track tr;
        tr.id = next_id++;
        tr.box = cv::Rect2f ( detections[d] );
        tr.hits = 1;
        tr.last_seen = now;
        list.push_back ( tr );
    }

    // 5. 兩個 track 的框重疊太多，代表其實是同一張臉，只留比較久的那個
    merge_overlapping_tracks ( );
}

void Tracker::merge_overlapping_tracks ( )
{
    std::vector<bool> removed ( list.size ( ), false );
    for ( size_t i = 0; i < list.size ( ); i++ )
    {
        if ( removed[i] ) continue;
        for ( size_t j = i + 1; j < list.size ( ); j++ )
        {
            if ( removed[j] ) continue;
            float inter = ( list[i].box & list[j].box ).area ( );
            float smaller = std::min ( list[i].box.area ( ), list[j].box.area ( ) );
            if ( smaller <= 0 || inter / smaller <= overlap_merge ) continue;
            // hits 比較多的是比較早出現、已經有辨識結果的那個
            if ( list[i].hits >= list[j].hits ) removed[j] = true;
            else
            {
                removed[i] = true;
                break;
            }
        }
    }
    std::vector<Track> kept;
    for ( size_t i = 0; i < list.size ( ); i++ )
        if ( !removed[i] ) kept.push_back ( list[i] );
    list.swap ( kept );
}

void Tracker::add_vote ( int track_id, int label, float score )
{
    Track *tr = find ( track_id );
    if ( !tr ) return;      // 辨識完成前這張臉已經離開畫面
    tr->pending = false;
    tr->votes.push_back ( std::make_pair ( label, score ) );
    while ( (int) tr->votes.size ( ) > vote_window ) tr->votes.pop_front ( );

    // 第一次結果直接顯示，讓畫面盡快出現標籤
    if ( tr->shown_label == LABEL_PENDING )
    {
        tr->shown_label = label;
        return;
    }

    // 多數決 + 遲滯：新的標籤至少要 vote_min 票，而且票數多於目前顯示的標籤，才換掉
    std::map<int, int> count;
    for ( size_t i = 0; i < tr->votes.size ( ); i++ ) count[tr->votes[i].first]++;
    int top_label = tr->shown_label, top_count = count[tr->shown_label];
    for ( std::map<int, int>::iterator it = count.begin ( ); it != count.end ( ); ++it )
    {
        if ( it->second > top_count )
        {
            top_label = it->first;
            top_count = it->second;
        }
    }
    if ( top_label != tr->shown_label && top_count >= vote_min ) tr->shown_label = top_label;
}

void Tracker::resolve_duplicates ( )
{
    // 兩個框都顯示同一位組員時，保留平均相似度較高的那個，另一個改成 unknown
    for ( size_t i = 0; i < list.size ( ); i++ )
    {
        if ( list[i].shown_label < 0 ) continue;
        for ( size_t j = i + 1; j < list.size ( ); j++ )
        {
            if ( list[j].shown_label != list[i].shown_label ) continue;
            if ( list[i].mean_score ( list[i].shown_label ) >= list[j].mean_score ( list[j].shown_label ) )
                list[j].shown_label = LABEL_UNKNOWN;
            else
            {
                list[i].shown_label = LABEL_UNKNOWN;
                break;
            }
        }
    }
}
