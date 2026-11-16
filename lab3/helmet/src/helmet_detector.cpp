#include "helmet_detector.hpp"

#include <opencv2/imgproc/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

bool HelmetDetector::load ( const Config &config )
{
    cfg = config;
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
    return true;
}

std::vector<Box> HelmetDetector::detect ( const cv::Mat &bgr )
{
    const int S = cfg.input_size;

    // letterbox：等比例縮放後補灰邊（114）成 S x S，和訓練時一樣，人不會被壓扁
    double r = std::min ( (double) S / bgr.cols, (double) S / bgr.rows );
    int nw = (int) std::lround ( bgr.cols * r );
    int nh = (int) std::lround ( bgr.rows * r );
    int px = ( S - nw ) / 2, py = ( S - nh ) / 2;
    cv::Mat padded ( S, S, CV_8UC3, cv::Scalar ( 114, 114, 114 ) );
    cv::resize ( bgr, padded ( cv::Rect ( px, py, nw, nh ) ), cv::Size ( nw, nh ), 0, 0, cv::INTER_AREA );

    // BGR → RGB，像素值 0~255 → 0~1
    ncnn::Mat in = ncnn::Mat::from_pixels ( padded.data, ncnn::Mat::PIXEL_BGR2RGB, S, S );
    const float mean[3] = { 0.f, 0.f, 0.f };
    const float norm[3] = { 1 / 255.f, 1 / 255.f, 1 / 255.f };
    in.substract_mean_normalize ( mean, norm );

    ncnn::Extractor ex = net.create_extractor ( );
    ex.input ( cfg.input_blob.c_str ( ), in );
    ncnn::Mat out;
    ex.extract ( cfg.output_blob.c_str ( ), out );
    pass_count++;

    // 輸出 5 x N：每一欄是一個候選框 (cx, cy, w, h, score)，座標在 S x S 的 letterbox 圖上
    std::vector<Box> boxes;
    if ( out.h != 5 )
    {
        std::cerr << "unexpected output shape " << out.w << "x" << out.h << "x" << out.c << std::endl;
        return boxes;
    }
    const float *cx = out.row ( 0 ), *cy = out.row ( 1 ), *bw = out.row ( 2 ), *bh = out.row ( 3 ), *score = out.row ( 4 );
    for ( int i = 0; i < out.w; i++ )
    {
        if ( score[i] < cfg.conf_threshold ) continue;
        // 換回原圖座標：先扣掉補邊，再除以縮放比
        Box b;
        b.x1 = (float) ( ( cx[i] - bw[i] / 2 - px ) / r );
        b.y1 = (float) ( ( cy[i] - bh[i] / 2 - py ) / r );
        b.x2 = (float) ( ( cx[i] + bw[i] / 2 - px ) / r );
        b.y2 = (float) ( ( cy[i] + bh[i] / 2 - py ) / r );
        b.score = score[i];
        boxes.push_back ( b );
    }
    return boxes;
}

std::vector<Box> HelmetDetector::find_helmets ( const cv::Mat &bgr )
{
    pass_count = 0;
    const int w = bgr.cols, h = bgr.rows;
    std::vector<Box> boxes = detect ( bgr );        // 整張圖一次（近的、大的人）

    // 大圖再切塊各跑一次（遠處的小帽子整張縮到 640 會看不到）。
    // 切塊比模型輸入還小就不切：放大小圖會把臉等誤判成帽子
    const int n = cfg.tile_grid;
    if ( n >= 2 && std::min ( w, h ) >= n * cfg.input_size )
    {
        // 整張圖縮到模型輸入的比例：切塊只補整張圖裡太小（< tile_max_box px）的帽子，大的整張圖已經抓得到
        const double r = std::min ( (double) cfg.input_size / w, (double) cfg.input_size / h );
        std::vector<cv::Rect> tiles = make_tiles ( w, h, n, cfg.tile_overlap );
        for ( size_t t = 0; t < tiles.size ( ); t++ )
        {
            const cv::Rect &tile = tiles[t];
            std::vector<Box> found = detect ( bgr ( tile ) );   // 座標是切塊內的座標
            for ( size_t k = 0; k < found.size ( ); k++ )
            {
                const Box &b = found[k];
                // 框碰到切塊的邊（而且那條邊不是原圖的邊）= 帽子被切到，丟掉，交給整張圖或相鄰切塊
                bool cut = ( b.x1 <= 2 && tile.x > 0 ) || ( b.y1 <= 2 && tile.y > 0 ) ||
                           ( b.x2 >= tile.width - 2 && tile.x + tile.width < w ) ||
                           ( b.y2 >= tile.height - 2 && tile.y + tile.height < h );
                bool small = std::max ( b.x2 - b.x1, b.y2 - b.y1 ) * r < cfg.tile_max_box;
                if ( cut || !small ) continue;
                Box g = b;      // 切塊座標 → 原圖座標
                g.x1 += tile.x; g.x2 += tile.x;
                g.y1 += tile.y; g.y2 += tile.y;
                boxes.push_back ( g );
            }
        }
    }

    std::vector<Box> final_boxes = merge_boxes ( boxes, cfg.nms_iou, cfg.contain );
    for ( size_t i = 0; i < final_boxes.size ( ); i++ )
    {
        Box &b = final_boxes[i];        // 裁到圖片範圍內
        b.x1 = std::max ( 0.f, b.x1 ); b.y1 = std::max ( 0.f, b.y1 );
        b.x2 = std::min ( (float) w, b.x2 ); b.y2 = std::min ( (float) h, b.y2 );
    }
    return final_boxes;
}

std::vector<cv::Rect> make_tiles ( int w, int h, int n, double overlap )
{
    // n 塊、相鄰重疊 overlap：n*tw - (n-1)*overlap*tw = w
    double tw = w / ( n - ( n - 1 ) * overlap ), th = h / ( n - ( n - 1 ) * overlap );
    double sx = tw * ( 1 - overlap ), sy = th * ( 1 - overlap );
    std::vector<cv::Rect> tiles;
    for ( int j = 0; j < n; j++ )
        for ( int i = 0; i < n; i++ )
        {
            int x0 = (int) std::lround ( i * sx ), y0 = (int) std::lround ( j * sy );
            int x1 = std::min ( w, (int) std::lround ( i * sx + tw ) );
            int y1 = std::min ( h, (int) std::lround ( j * sy + th ) );
            tiles.push_back ( cv::Rect ( x0, y0, x1 - x0, y1 - y0 ) );
        }
    return tiles;
}

static float box_area ( const Box &b )
{
    return std::max ( 0.f, b.x2 - b.x1 ) * std::max ( 0.f, b.y2 - b.y1 );
}

static float intersection ( const Box &a, const Box &b )
{
    float ix = std::max ( 0.f, std::min ( a.x2, b.x2 ) - std::max ( a.x1, b.x1 ) );
    float iy = std::max ( 0.f, std::min ( a.y2, b.y2 ) - std::max ( a.y1, b.y1 ) );
    return ix * iy;
}

float box_iou ( const Box &a, const Box &b )
{
    float inter = intersection ( a, b );
    float uni = box_area ( a ) + box_area ( b ) - inter;
    return uni > 0 ? inter / uni : 0.f;
}

std::vector<Box> merge_boxes ( std::vector<Box> boxes, double nms_iou, double contain )
{
    // 分數高的優先
    std::stable_sort ( boxes.begin ( ), boxes.end ( ), [] ( const Box &a, const Box &b ) { return a.score > b.score; } );

    // 1) NMS：跟已保留的框 IoU 超過 nms_iou 就丟掉（同一頂帽子被整張圖和切塊各框一次）
    std::vector<Box> kept;
    for ( size_t i = 0; i < boxes.size ( ); i++ )
    {
        bool overlap = false;
        for ( size_t k = 0; k < kept.size ( ) && !overlap; k++ )
            overlap = box_iou ( boxes[i], kept[k] ) > nms_iou;
        if ( !overlap ) kept.push_back ( boxes[i] );
    }

    // 2) 包含：小框大部分落在另一個框裡（IoU 不高但其實是同一個人）也丟掉，一人只留一個框
    std::vector<Box> final_boxes;
    for ( size_t i = 0; i < kept.size ( ); i++ )
    {
        bool dup = false;
        for ( size_t k = 0; k < final_boxes.size ( ) && !dup; k++ )
        {
            float smaller = std::min ( box_area ( kept[i] ), box_area ( final_boxes[k] ) );
            dup = intersection ( kept[i], final_boxes[k] ) > contain * smaller;
        }
        if ( !dup ) final_boxes.push_back ( kept[i] );
    }
    return final_boxes;
}
