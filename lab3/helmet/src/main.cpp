// Lab 3 - 3.2 戴安全帽偵測
//
// 用法（在板子上，跟 config.yml 同一個資料夾執行）：
//   ./run.sh <照片.jpg> <組別> [輸出資料夾]
//     例：./run.sh test.jpg 8 → lab3_final_8.jpg
//         ./run.sh /run/media/mmcblk1p1/hidden.jpg 8 /run/media/mmcblk1p1 → 結果直接存到 SD 卡
//
// 讀一張照片（最大 6000x4000）→ 只框「有戴安全帽的人」→ 存成 JPG（原尺寸）並等比例顯示在 HDMI。

#include "config.hpp"
#include "framebuffer.hpp"
#include "helmet_detector.hpp"

#include <opencv2/imgcodecs/imgcodecs.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <sys/time.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

static double now_seconds ( )
{
    struct timeval tv;
    gettimeofday ( &tv, NULL );
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

// 畫綠框與 helmet 標籤（線寬依圖片大小調整，大圖縮到螢幕上也看得清楚）
static void draw_boxes ( cv::Mat &img, const std::vector<Box> &boxes )
{
    int t = std::max ( 2, (int) std::lround ( std::max ( img.cols, img.rows ) / 600.0 ) );
    for ( size_t i = 0; i < boxes.size ( ); i++ )
    {
        cv::Point p1 ( (int) boxes[i].x1, (int) boxes[i].y1 ), p2 ( (int) boxes[i].x2, (int) boxes[i].y2 );
        cv::rectangle ( img, p1, p2, cv::Scalar ( 0, 255, 0 ), t );
        cv::putText ( img, "helmet", cv::Point ( p1.x, std::max ( 0, p1.y - 2 * t ) ), cv::FONT_HERSHEY_SIMPLEX,
                      t / 3.0, cv::Scalar ( 0, 255, 0 ), std::max ( 1, t / 2 ) );
    }
}

int main ( int argc, char **argv )
{
    if ( argc < 3 )
    {
        std::cerr << "usage: " << argv[0] << " <photo.jpg> <group number> [output dir]" << std::endl;
        return 1;
    }
    const std::string photo = argv[1];
    const std::string out_dir = argc > 3 ? argv[3] : ".";
    const std::string out_name = out_dir + "/lab3_final_" + argv[2] + ".jpg";

    Config cfg;
    if ( !cfg.load ( "config.yml" ) ) return 1;

    double t0 = now_seconds ( );
    HelmetDetector detector;
    if ( !detector.load ( cfg ) ) return 1;
    double t_load = now_seconds ( );

    cv::Mat img = cv::imread ( photo, cv::IMREAD_COLOR );      // 原始解析度，依 EXIF 轉正
    if ( img.empty ( ) )
    {
        std::cerr << "Could not read " << photo << std::endl;
        return 1;
    }
    double t_read = now_seconds ( );
    printf ( "image %dx%d\n", img.cols, img.rows );

    std::vector<Box> boxes = detector.find_helmets ( img );
    double t_detect = now_seconds ( );

    for ( size_t i = 0; i < boxes.size ( ); i++ )
        printf ( "box %zu: %.1f %.1f %.1f %.1f  score %.3f\n", i, boxes[i].x1, boxes[i].y1, boxes[i].x2, boxes[i].y2, boxes[i].score );

    draw_boxes ( img, boxes );
    std::vector<int> params;
    params.push_back ( cv::IMWRITE_JPEG_QUALITY );
    params.push_back ( cfg.jpeg_quality );
    if ( !cv::imwrite ( out_name, img, params ) )      // 原尺寸存檔，不變形
    {
        std::cerr << "Could not write " << out_name << std::endl;
        return 1;
    }
    sync ( );       // 寫進 SD 卡，拔卡前不會遺失
    double t_save = now_seconds ( );

    printf ( "%zu helmets -> %s\n", boxes.size ( ), out_name.c_str ( ) );
    printf ( "time: load %.1f s | read %.1f s | detect %.1f s (%d passes) | save %.1f s | total %.1f s\n",
             t_load - t0, t_read - t_load, t_detect - t_read, detector.passes ( ), t_save - t_detect, t_save - t0 );
    fflush ( stdout );

    Framebuffer fb;
    if ( fb.open ( cfg.fb_device ) )
    {
        fb.show ( img );        // 等比例縮放置中，不變形
        printf ( "shown on %s (%ux%u, %u bpp)\n", cfg.fb_device.c_str ( ), fb.width ( ), fb.height ( ), fb.bpp ( ) );
        if ( cfg.wait_enter )
        {
            printf ( "Press Enter to exit\n" );
            fflush ( stdout );
            std::cin.get ( );
        }
        fb.close ( );
    }
    return 0;
}
