#include "face_aligner.hpp"

#include <opencv2/imgproc/imgproc.hpp>
#include <cmath>
#include <iostream>
#include <vector>

// ArcFace / InsightFace 112x112 樣板中兩眼的位置（影像中的左眼、右眼）
static const cv::Point2f TEMPLATE_LEFT_EYE ( 38.2946f, 51.6963f );
static const cv::Point2f TEMPLATE_RIGHT_EYE ( 73.5318f, 51.5014f );
static const int ALIGNED_SIZE = 112;

// 找不到眼睛時，用 Haar 臉框的固定比例估計眼睛位置
static const float GUESS_EYE_X_LEFT = 0.30f;
static const float GUESS_EYE_X_RIGHT = 0.70f;
static const float GUESS_EYE_Y = 0.38f;

bool FaceAligner::load ( const std::string &eye_cascade_path )
{
    if ( !eye_cascade.load ( eye_cascade_path ) )
    {
        std::cerr << "Could not load eye cascade " << eye_cascade_path << std::endl;
        return false;
    }
    return true;
}

bool FaceAligner::find_eyes ( const cv::Mat &bgr, const cv::Rect &face, cv::Point2f &left, cv::Point2f &right )
{
    // 只在臉框的上半部找眼睛，減少誤判（鼻孔、嘴巴）也減少運算
    cv::Rect upper ( face.x, face.y, face.width, face.height * 6 / 10 );
    upper &= cv::Rect ( 0, 0, bgr.cols, bgr.rows );
    if ( upper.area ( ) <= 0 ) return false;

    cv::Mat gray;
    cv::cvtColor ( bgr ( upper ), gray, cv::COLOR_BGR2GRAY );
    cv::equalizeHist ( gray, gray );

    std::vector<cv::Rect> eyes;
    int min_eye = std::max ( 8, face.width / 8 );
    eye_cascade.detectMultiScale ( gray, eyes, 1.1, 3, cv::CASCADE_SCALE_IMAGE, cv::Size ( min_eye, min_eye ) );

    // 左半邊、右半邊各取面積最大的一個候選
    int best_left = -1, best_right = -1;
    for ( size_t i = 0; i < eyes.size ( ); i++ )
    {
        float cx = eyes[i].x + eyes[i].width * 0.5f;
        if ( cx < upper.width * 0.5f )
        {
            if ( best_left < 0 || eyes[i].area ( ) > eyes[best_left].area ( ) ) best_left = (int) i;
        }
        else
        {
            if ( best_right < 0 || eyes[i].area ( ) > eyes[best_right].area ( ) ) best_right = (int) i;
        }
    }
    if ( best_left < 0 || best_right < 0 ) return false;

    left = cv::Point2f ( upper.x + eyes[best_left].x + eyes[best_left].width * 0.5f,
                         upper.y + eyes[best_left].y + eyes[best_left].height * 0.5f );
    right = cv::Point2f ( upper.x + eyes[best_right].x + eyes[best_right].width * 0.5f,
                          upper.y + eyes[best_right].y + eyes[best_right].height * 0.5f );

    // 合理性檢查：兩眼距離要在臉寬的 25%–65% 之間，傾斜不能超過約 25 度
    float dx = right.x - left.x, dy = right.y - left.y;
    if ( dx < face.width * 0.25f || dx > face.width * 0.65f ) return false;
    if ( std::fabs ( dy ) > dx * 0.47f ) return false;
    return true;
}

cv::Mat FaceAligner::align ( const cv::Mat &bgr, const cv::Rect &face, bool *eyes_found )
{
    cv::Point2f left, right;
    bool found = find_eyes ( bgr, face, left, right );
    if ( !found )
    {
        left = cv::Point2f ( face.x + face.width * GUESS_EYE_X_LEFT, face.y + face.height * GUESS_EYE_Y );
        right = cv::Point2f ( face.x + face.width * GUESS_EYE_X_RIGHT, face.y + face.height * GUESS_EYE_Y );
    }
    if ( eyes_found ) *eyes_found = found;

    // 相似變換（旋轉 + 等比例縮放 + 平移）：把 (left, right) 對到樣板的兩眼位置
    cv::Point2f src = right - left;
    cv::Point2f dst = TEMPLATE_RIGHT_EYE - TEMPLATE_LEFT_EYE;
    double scale = std::sqrt ( dst.dot ( dst ) / src.dot ( src ) );
    double angle = std::atan2 ( dst.y, dst.x ) - std::atan2 ( src.y, src.x );
    double a = scale * std::cos ( angle );
    double b = scale * std::sin ( angle );
    // [x'] = [a -b] [x] + [tx]
    // [y']   [b  a] [y]   [ty]，平移量讓左眼剛好落在樣板左眼
    double tx = TEMPLATE_LEFT_EYE.x - ( a * left.x - b * left.y );
    double ty = TEMPLATE_LEFT_EYE.y - ( b * left.x + a * left.y );
    cv::Mat M = ( cv::Mat_<double> ( 2, 3 ) << a, -b, tx, b, a, ty );

    cv::Mat aligned;
    cv::warpAffine ( bgr, aligned, M, cv::Size ( ALIGNED_SIZE, ALIGNED_SIZE ),
                     cv::INTER_LINEAR, cv::BORDER_REPLICATE );
    return aligned;
}
