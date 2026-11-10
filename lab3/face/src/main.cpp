// Lab 3 - 3.1 即時人臉辨識
//
// 用法（在板子上，跟 config.yml 同一個資料夾執行）：
//   ./face3 run                     即時辨識，畫面輸出到 framebuffer
//   ./face3 enroll <登錄照片資料夾>   建立 gallery.yml（子資料夾名稱 = 顯示的學號）
//   ./face3 score <照片資料夾>        對每張照片印出相似度，用來校準門檻

#include "camera.hpp"
#include "config.hpp"
#include "face_aligner.hpp"
#include "face_detector.hpp"
#include "face_embedder.hpp"
#include "framebuffer.hpp"
#include "gallery.hpp"
#include "keyboard.hpp"
#include "recognizer.hpp"
#include "tracker.hpp"

#include <opencv2/imgcodecs/imgcodecs.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <sys/stat.h>
#include <sys/time.h>
#include <signal.h>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <map>
#include <string>
#include <vector>

static volatile sig_atomic_t quit_flag = 0;
static void on_signal ( int ) { quit_flag = 1; }

static double now_seconds ( )
{
    struct timeval tv;
    gettimeofday ( &tv, NULL );
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

// ---------------------------------------------------------------------------
// 共用：從一張靜態照片抽出最大那張臉的 embedding（enroll 和 score 都用這個）
// ---------------------------------------------------------------------------
struct StillPipeline
{
    FaceDetector detector;
    FaceAligner aligner;
    FaceEmbedder embedder;

    bool init ( const Config &cfg )
    {
        return detector.load ( cfg ) && aligner.load ( cfg.eye_cascade ) && embedder.load ( cfg );
    }

    // 成功時回傳 true，並輸出 embedding 和對齊後的 112x112 臉（方便人工檢查對齊有沒有對好）
    bool process ( const cv::Mat &image, std::vector<float> &feature, cv::Mat &aligned, bool &eyes_found )
    {
        std::vector<cv::Rect> faces = detector.detect ( image );
        if ( faces.empty ( ) ) return false;
        cv::Rect largest = *std::max_element ( faces.begin ( ), faces.end ( ),
            [] ( const cv::Rect &a, const cv::Rect &b ) { return a.area ( ) < b.area ( ); } );
        aligned = aligner.align ( image, largest, &eyes_found );
        feature = embedder.embed ( aligned );
        return true;
    }
};

static std::vector<std::string> list_images ( const std::string &dir )
{
    std::vector<cv::String> all;        // OpenCV 3.4 的 cv::String 不是 std::string
    std::vector<std::string> images;
    cv::glob ( dir + "/*", all, true );     // recursive
    for ( size_t i = 0; i < all.size ( ); i++ )
    {
        std::string path = all[i];
        std::string ext = path.substr ( path.find_last_of ( '.' ) + 1 );
        std::transform ( ext.begin ( ), ext.end ( ), ext.begin ( ), ::tolower );
        if ( ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "bmp" ) images.push_back ( path );
    }
    std::sort ( images.begin ( ), images.end ( ) );
    return images;
}

// 路徑 enroll/0812345/3.bmp 的上一層資料夾名稱 "0812345" 就是標籤
static std::string parent_name ( const std::string &path )
{
    size_t slash = path.find_last_of ( '/' );
    size_t prev = path.find_last_of ( '/', slash - 1 );
    return path.substr ( prev + 1, slash - prev - 1 );
}

// ---------------------------------------------------------------------------
// enroll：建立 gallery.yml，並印出同一人 / 不同人的相似度統計
// ---------------------------------------------------------------------------
static int run_enroll ( const Config &cfg, const std::string &dir )
{
    StillPipeline pipe;
    if ( !pipe.init ( cfg ) ) return 1;

    std::vector<std::string> images = list_images ( dir );
    if ( images.empty ( ) )
    {
        std::cerr << "No images found under " << dir << std::endl;
        return 1;
    }

    mkdir ( "aligned", 0755 );
    Gallery gallery;
    int used = 0, no_face = 0, no_eyes = 0;
    for ( size_t i = 0; i < images.size ( ); i++ )
    {
        cv::Mat image = cv::imread ( images[i], cv::IMREAD_COLOR );
        if ( image.empty ( ) ) continue;
        std::vector<float> feature;
        cv::Mat aligned;
        bool eyes_found = false;
        if ( !pipe.process ( image, feature, aligned, eyes_found ) )
        {
            std::cout << "  [no face] " << images[i] << std::endl;
            no_face++;
            continue;
        }
        if ( !eyes_found ) no_eyes++;
        std::string label = parent_name ( images[i] );
        gallery.add ( label, feature );
        mkdir ( ( "aligned/" + label ).c_str ( ), 0755 );
        cv::imwrite ( "aligned/" + label + "/" + std::to_string ( used ) + ".png", aligned );
        used++;
    }
    gallery.finalize ( );
    if ( gallery.people ( ).empty ( ) || !gallery.save ( cfg.gallery ) )
    {
        std::cerr << "Failed to build gallery" << std::endl;
        return 1;
    }
    std::cout << "enrolled " << used << " images (" << no_face << " without face, "
              << no_eyes << " with estimated eyes) -> " << cfg.gallery << std::endl;
    std::cout << "aligned faces saved to ./aligned/ -- check that eyes are level and centered" << std::endl;

    // 統計：每張登錄照片跟「自己的範本」和「別人的範本」的相似度
    const std::vector<Person> &people = gallery.people ( );
    float min_same = 1.0f, max_other = -1.0f;
    for ( size_t p = 0; p < people.size ( ); p++ )
    {
        float sum = 0, lo = 1.0f;
        for ( size_t s = 0; s < people[p].samples.size ( ); s++ )
        {
            float sim = cosine_similarity ( people[p].samples[s], people[p].mean );
            sum += sim;
            lo = std::min ( lo, sim );
            for ( size_t q = 0; q < people.size ( ); q++ )
                if ( q != p ) max_other = std::max ( max_other, cosine_similarity ( people[p].samples[s], people[q].mean ) );
        }
        min_same = std::min ( min_same, lo );
        printf ( "  %-12s %3zu samples, similarity to own template: mean %.3f, min %.3f\n",
                 people[p].label.c_str ( ), people[p].samples.size ( ), sum / people[p].samples.size ( ), lo );
    }
    if ( people.size ( ) > 1 )
        printf ( "  highest similarity to the OTHER member's template: %.3f\n", max_other );
    printf ( "  current sim_threshold = %.2f (should sit below %.3f; also run 'score' on strangers)\n",
             cfg.sim_threshold, min_same );
    return 0;
}

// ---------------------------------------------------------------------------
// score：對每張照片印出判定結果，拿陌生人的照片跑，用來決定 sim_threshold
// ---------------------------------------------------------------------------
static int run_score ( const Config &cfg, const std::string &dir )
{
    StillPipeline pipe;
    Gallery gallery;
    if ( !pipe.init ( cfg ) || !gallery.load ( cfg.gallery ) ) return 1;

    std::vector<std::string> images = list_images ( dir );
    float max_best = -1.0f;
    for ( size_t i = 0; i < images.size ( ); i++ )
    {
        cv::Mat image = cv::imread ( images[i], cv::IMREAD_COLOR );
        if ( image.empty ( ) ) continue;
        std::vector<float> feature;
        cv::Mat aligned;
        bool eyes_found = false;
        if ( !pipe.process ( image, feature, aligned, eyes_found ) )
        {
            printf ( "  [no face] %s\n", images[i].c_str ( ) );
            continue;
        }
        MatchResult m = gallery.match ( feature, (float) cfg.sim_threshold, (float) cfg.sim_margin );
        // 不管有沒有過門檻，都印出最像的是誰
        int nearest = 0;
        float nearest_sim = -1.0f;
        for ( size_t p = 0; p < gallery.people ( ).size ( ); p++ )
        {
            float s = cosine_similarity ( feature, gallery.people ( )[p].mean );
            if ( s > nearest_sim ) { nearest_sim = s; nearest = (int) p; }
        }
        max_best = std::max ( max_best, m.best );
        printf ( "  %-40s nearest %-12s best %.3f second %.3f -> %s%s\n", images[i].c_str ( ),
                 gallery.people ( )[nearest].label.c_str ( ), m.best, m.second,
                 m.index >= 0 ? gallery.people ( )[m.index].label.c_str ( ) : "unknown",
                 eyes_found ? "" : "  (eyes estimated)" );
    }
    printf ( "  highest 'best' similarity in this folder: %.3f\n", max_best );
    return 0;
}

// ---------------------------------------------------------------------------
// run：即時辨識
// ---------------------------------------------------------------------------
static cv::Scalar label_color ( int label )
{
    if ( label == LABEL_PENDING ) return cv::Scalar ( 0, 255, 255 );   // 黃：辨識中
    if ( label == LABEL_UNKNOWN ) return cv::Scalar ( 0, 0, 255 );     // 紅：陌生人
    return cv::Scalar ( 0, 255, 0 );                                   // 綠：組員
}

static void draw_tracks ( cv::Mat &frame, Tracker &tracker, const Gallery &gallery, int min_hits )
{
    std::vector<Track> &tracks = tracker.tracks ( );
    for ( size_t i = 0; i < tracks.size ( ); i++ )
    {
        const Track &tr = tracks[i];
        if ( !tr.visible ( min_hits ) ) continue;
        std::string text;
        if ( tr.shown_label == LABEL_PENDING ) text = "...";
        else if ( tr.shown_label == LABEL_UNKNOWN ) text = "unknown";
        else text = gallery.people ( )[tr.shown_label].label;

        cv::Scalar color = label_color ( tr.shown_label );
        cv::Rect box ( cvRound ( tr.box.x ), cvRound ( tr.box.y ), cvRound ( tr.box.width ), cvRound ( tr.box.height ) );
        cv::rectangle ( frame, box, color, 2 );
        int baseline = 0;
        cv::Size ts = cv::getTextSize ( text, cv::FONT_HERSHEY_SIMPLEX, 0.7, 2, &baseline );
        cv::Point org ( box.x, std::max ( ts.height + 4, box.y - 6 ) );
        cv::rectangle ( frame, cv::Rect ( org.x, org.y - ts.height - 4, ts.width + 6, ts.height + 8 ), color, cv::FILLED );
        cv::putText ( frame, text, cv::Point ( org.x + 3, org.y ), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar ( 0, 0, 0 ), 2 );
    }
}

static int run_live ( const Config &cfg )
{
    Gallery gallery;
    if ( !gallery.load ( cfg.gallery ) ) return 1;
    std::cout << "gallery: ";
    for ( size_t i = 0; i < gallery.people ( ).size ( ); i++ )
        std::cout << gallery.people ( )[i].label << "(" << gallery.people ( )[i].samples.size ( ) << ") ";
    std::cout << std::endl;

    FaceDetector detector;
    Recognizer recognizer;
    Tracker tracker;
    Framebuffer fb;
    Camera camera;
    if ( !detector.load ( cfg ) || !recognizer.init ( cfg, &gallery ) ) return 1;
    if ( !fb.open ( cfg.fb_device ) ) return 1;
    std::cout << "screen " << fb.width ( ) << "x" << fb.height ( ) << " @ " << fb.bpp ( ) << " bpp" << std::endl;
    if ( !camera.open ( cfg.camera_index, cfg.camera_width, cfg.camera_height ) ) return 1;
    tracker.configure ( cfg );

    signal ( SIGINT, on_signal );
    signal ( SIGTERM, on_signal );
    keyboard_open ( );
    camera.start ( );
    recognizer.start ( );
    std::cout << "running (q = quit, s = save snapshot)" << std::endl;

    cv::Mat frame;
    long frame_id = 0;
    int frames = 0, snapshots = 0;
    const int REPORT_EVERY = 30;

    // 各階段耗時（每 REPORT_EVERY 幀印一次平均），用來找出瓶頸
    double fps_start = now_seconds ( );
    double wait_ms = 0, detect_ms = 0, track_ms = 0, draw_ms = 0, display_ms = 0, latency_ms = 0;
    double recog_ms = 0;
    int recog_count = 0;

    while ( !quit_flag )
    {
        double t_wait = now_seconds ( );
        double captured_at = 0;
        long id = camera.wait_frame ( frame_id, frame, &captured_at );
        if ( id < 0 ) continue;
        frame_id = id;
        double now = now_seconds ( );
        wait_ms += ( now - t_wait ) * 1000;

        // 1. 偵測 + 追蹤
        double t0 = now_seconds ( );
        std::vector<cv::Rect> faces = detector.detect ( frame );
        double t1 = now_seconds ( );
        detect_ms += ( t1 - t0 ) * 1000;
        tracker.update ( faces, now );

        // 2. 收回背景辨識的結果，加進各 track 的投票
        std::vector<RecogResult> results = recognizer.take_results ( );
        for ( size_t i = 0; i < results.size ( ); i++ )
        {
            tracker.add_vote ( results[i].track_id, results[i].label, results[i].score );
            recog_ms += results[i].millis;
            recog_count++;
        }
        tracker.resolve_duplicates ( );

        // 3. 需要（重新）辨識的 track 送去背景執行緒：剛出現的立刻送，之後每 recog_interval 秒一次
        std::vector<Track> &tracks = tracker.tracks ( );
        for ( size_t i = 0; i < tracks.size ( ); i++ )
        {
            Track &tr = tracks[i];
            if ( !tr.visible ( cfg.min_hits ) || tr.pending || tr.missed > 0 ) continue;
            if ( now - tr.last_request < cfg.recog_interval ) continue;

            // 只複製臉附近（放大 1.6 倍）的區域，背景執行緒不會碰到正在被修改的整張影像
            cv::Rect face ( cvRound ( tr.box.x ), cvRound ( tr.box.y ), cvRound ( tr.box.width ), cvRound ( tr.box.height ) );
            int pad_x = face.width * 3 / 10, pad_y = face.height * 3 / 10;
            cv::Rect region ( face.x - pad_x, face.y - pad_y, face.width + 2 * pad_x, face.height + 2 * pad_y );
            region &= cv::Rect ( 0, 0, frame.cols, frame.rows );
            if ( region.area ( ) <= 0 ) continue;

            RecogJob job;
            job.track_id = tr.id;
            job.region = frame ( region ).clone ( );
            job.face = face - region.tl ( );
            if ( recognizer.push ( job ) )
            {
                tr.pending = true;
                tr.last_request = now;
            }
        }

        double t2 = now_seconds ( );
        track_ms += ( t2 - t1 ) * 1000;

        // 4. 畫框和標籤，輸出到螢幕
        draw_tracks ( frame, tracker, gallery, cfg.min_hits );
        double t3 = now_seconds ( );
        draw_ms += ( t3 - t2 ) * 1000;
        fb.show ( frame, cfg.display_fit != 0 );
        double t4 = now_seconds ( );
        display_ms += ( t4 - t3 ) * 1000;
        latency_ms += ( t4 - captured_at ) * 1000;     // 攝影機拍到這幀 → 出現在螢幕上

        int key = read_key ( );
        if ( key == 'q' || key == 'Q' ) break;
        if ( key == 's' || key == 'S' )
        {
            std::string name = "snapshot_" + std::to_string ( snapshots++ ) + ".jpg";
            cv::imwrite ( name, frame );
            std::cout << "saved " << name << std::endl;
        }

        frames++;
        if ( frames % REPORT_EVERY == 0 )
        {
            const double n = REPORT_EVERY;
            printf ( "fps %.1f | wait %.0f detect %.0f track %.0f draw %.0f display %.0f ms | latency %.0f ms"
                     " | recog %.0f ms/face (%d) | tracks %zu\n",
                     n / ( now_seconds ( ) - fps_start ), wait_ms / n, detect_ms / n, track_ms / n,
                     draw_ms / n, display_ms / n, latency_ms / n,
                     recog_count ? recog_ms / recog_count : 0.0, recog_count, tracks.size ( ) );
            fps_start = now_seconds ( );
            wait_ms = detect_ms = track_ms = draw_ms = display_ms = latency_ms = recog_ms = 0;
            recog_count = 0;
        }
    }

    keyboard_close ( );
    recognizer.finish ( );
    camera.stop ( );
    fb.close ( );
    std::cout << "bye" << std::endl;
    return 0;
}

int main ( int argc, const char *argv[] )
{
    std::string mode = argc > 1 ? argv[1] : "run";
    Config cfg;
    if ( !cfg.load ( "config.yml" ) ) return 1;

    if ( mode == "run" ) return run_live ( cfg );
    if ( mode == "enroll" && argc > 2 ) return run_enroll ( cfg, argv[2] );
    if ( mode == "score" && argc > 2 ) return run_score ( cfg, argv[2] );

    std::cerr << "usage: " << argv[0] << " run | enroll <dir> | score <dir>" << std::endl;
    return 1;
}
