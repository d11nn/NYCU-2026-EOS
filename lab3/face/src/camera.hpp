#pragma once

#include <opencv2/core/core.hpp>
#include <opencv2/videoio/videoio.hpp>
#include <condition_variable>
#include <mutex>
#include <thread>

// 背景執行緒一直讀攝影機，只保留「最新的一張」。
// 主迴圈處理得比攝影機慢時，不會累積舊畫面造成延遲。
class Camera
{
public:
    bool open ( int index, int width, int height );
    void start ( );
    void stop ( );

    // 等到比 last_id 新的影格出現，複製到 frame，回傳新的影格編號；逾時或停止時回傳 -1。
    // captured_at 會填入這幀從攝影機讀到的時間（秒），用來量延遲
    long wait_frame ( long last_id, cv::Mat &frame, double *captured_at = nullptr );

private:
    void loop ( );

    cv::VideoCapture capture;
    std::thread worker;
    std::mutex mtx;
    std::condition_variable cond;
    cv::Mat latest;
    long frame_id = 0;
    double latest_time = 0;
    bool running = false;
};
