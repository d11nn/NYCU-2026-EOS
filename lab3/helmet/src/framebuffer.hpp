#pragma once

#include <opencv2/core/core.hpp>
#include <cstdint>
#include <cstddef>
#include <string>

// Lab 2-3 的 mmap framebuffer，改成依照 bits_per_pixel 自動轉色彩格式
// （LCD 通常是 16 bpp，HDMI 可能是 32 bpp）
class Framebuffer
{
public:
    bool open ( const std::string &device );
    void close ( );

    // 等比例縮放（letterbox）後置中顯示，畫面不變形
    void show ( const cv::Mat &bgr );
    void clear ( );

    uint32_t width ( ) const { return xres; }
    uint32_t height ( ) const { return yres; }
    uint32_t bpp ( ) const { return bits_per_pixel; }

private:
    void convert ( const cv::Mat &bgr, cv::Mat &out ) const;

    unsigned char *mem = nullptr;
    size_t mem_size = 0;
    uint32_t line_length = 0;
    uint32_t xres = 0, yres = 0, bits_per_pixel = 0;
    uint32_t red_offset = 0;
    cv::Size last_size;     // 上一次顯示的影像大小，改變時才清畫面
};
