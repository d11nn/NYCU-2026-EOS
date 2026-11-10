#include "camera.hpp"

#include <sys/time.h>
#include <chrono>
#include <iostream>

bool Camera::open ( int index, int width, int height )
{
    capture.open ( index );
    if ( !capture.isOpened ( ) )
    {
        std::cerr << "Could not open video device " << index << std::endl;
        return false;
    }
    // MJPG 才能在 USB 2.0 頻寬下拿到較高的解析度和幀率（Lab 2-3 同樣設定）
    capture.set ( cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc ( 'M', 'J', 'P', 'G' ) );
    capture.set ( cv::CAP_PROP_FRAME_WIDTH, width );
    capture.set ( cv::CAP_PROP_FRAME_HEIGHT, height );
    std::cout << "camera " << capture.get ( cv::CAP_PROP_FRAME_WIDTH ) << "x"
              << capture.get ( cv::CAP_PROP_FRAME_HEIGHT ) << std::endl;
    return true;
}

void Camera::start ( )
{
    running = true;
    worker = std::thread ( &Camera::loop, this );
}

void Camera::stop ( )
{
    {
        std::lock_guard<std::mutex> lock ( mtx );
        running = false;
    }
    cond.notify_all ( );
    if ( worker.joinable ( ) ) worker.join ( );
    capture.release ( );
}

void Camera::loop ( )
{
    cv::Mat frame;
    for ( ; ; )
    {
        {
            std::lock_guard<std::mutex> lock ( mtx );
            if ( !running ) return;
        }
        if ( !capture.read ( frame ) || frame.empty ( ) ) continue;
        struct timeval tv;
        gettimeofday ( &tv, NULL );     // 跟 main.cpp 的 now_seconds ( ) 用同一個時鐘
        {
            std::lock_guard<std::mutex> lock ( mtx );
            cv::swap ( latest, frame );     // 交換，不複製影像資料
            latest_time = tv.tv_sec + tv.tv_usec / 1000000.0;
            frame_id++;
        }
        cond.notify_all ( );
    }
}

long Camera::wait_frame ( long last_id, cv::Mat &frame, double *captured_at )
{
    std::unique_lock<std::mutex> lock ( mtx );
    bool ok = cond.wait_for ( lock, std::chrono::seconds ( 2 ),
                              [&] { return !running || frame_id > last_id; } );
    if ( !ok || !running ) return -1;
    latest.copyTo ( frame );
    if ( captured_at ) *captured_at = latest_time;
    return frame_id;
}
