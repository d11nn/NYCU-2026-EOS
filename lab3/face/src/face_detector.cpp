#include "face_detector.hpp"

#include <opencv2/imgproc/imgproc.hpp>
#include <algorithm>
#include <iostream>

bool FaceDetector::load ( const Config &cfg )
{
    if ( !cascade.load ( cfg.face_cascade ) )
    {
        std::cerr << "Could not load face cascade " << cfg.face_cascade << std::endl;
        return false;
    }
    detect_width = cfg.detect_width;
    scale_factor = cfg.scale_factor;
    min_neighbors = cfg.min_neighbors;
    min_face = cfg.min_face;
    overlap_merge = cfg.overlap_merge;
    return true;
}

std::vector<cv::Rect> merge_overlaps ( std::vector<cv::Rect> boxes, double threshold )
{
    // 大的框排前面，先保留；之後的框只要跟已保留的框重疊太多就丟掉
    std::sort ( boxes.begin ( ), boxes.end ( ),
                [] ( const cv::Rect &a, const cv::Rect &b ) { return a.area ( ) > b.area ( ); } );
    std::vector<cv::Rect> kept;
    for ( size_t i = 0; i < boxes.size ( ); i++ )
    {
        bool duplicate = false;
        for ( size_t k = 0; k < kept.size ( ) && !duplicate; k++ )
        {
            double inter = ( boxes[i] & kept[k] ).area ( );
            double smaller = std::min ( boxes[i].area ( ), kept[k].area ( ) );
            if ( smaller > 0 && inter / smaller > threshold ) duplicate = true;
        }
        if ( !duplicate ) kept.push_back ( boxes[i] );
    }
    return kept;
}

std::vector<cv::Rect> FaceDetector::detect ( const cv::Mat &bgr )
{
    // 1. 轉灰階並縮小：Haar 只看亮度，縮小可以大幅減少運算量
    double scale = (double) detect_width / bgr.cols;
    if ( scale > 1.0 ) scale = 1.0;
    cv::Mat gray, small;
    cv::cvtColor ( bgr, gray, cv::COLOR_BGR2GRAY );
    cv::resize ( gray, small, cv::Size ( ), scale, scale, cv::INTER_AREA );

    // 2. 直方圖等化：降低光線明暗對偵測的影響
    cv::equalizeHist ( small, small );

    // 3. 多尺度偵測
    std::vector<cv::Rect> faces;
    cascade.detectMultiScale ( small, faces, scale_factor, min_neighbors,
                               cv::CASCADE_SCALE_IMAGE, cv::Size ( min_face, min_face ) );

    // 4. 座標換回原圖
    for ( size_t i = 0; i < faces.size ( ); i++ )
    {
        cv::Rect &r = faces[i];
        r = cv::Rect ( cvRound ( r.x / scale ), cvRound ( r.y / scale ),
                       cvRound ( r.width / scale ), cvRound ( r.height / scale ) );
    }

    // 5. Haar 會在不同縮放層級對同一張臉各給一個框（一大一小），合併成一個
    return merge_overlaps ( faces, overlap_merge );
}
