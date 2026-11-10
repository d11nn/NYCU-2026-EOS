#include "framebuffer.hpp"

#include <opencv2/imgproc/imgproc.hpp>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <algorithm>

bool Framebuffer::open ( const std::string &device )
{
    int fd = ::open ( device.c_str ( ), O_RDWR );
    if ( fd < 0 )
    {
        perror ( "open framebuffer" );
        return false;
    }

    struct fb_var_screeninfo var_info;
    struct fb_fix_screeninfo fix_info;
    if ( ioctl ( fd, FBIOGET_VSCREENINFO, &var_info ) < 0 || ioctl ( fd, FBIOGET_FSCREENINFO, &fix_info ) < 0 )
    {
        perror ( "ioctl framebuffer" );
        ::close ( fd );
        return false;
    }
    xres = var_info.xres;
    yres = var_info.yres;
    bits_per_pixel = var_info.bits_per_pixel;
    red_offset = var_info.red.offset;
    line_length = fix_info.line_length;
    mem_size = fix_info.smem_len;

    mem = (unsigned char *) mmap ( NULL, mem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 );
    ::close ( fd );
    if ( mem == MAP_FAILED )
    {
        perror ( "mmap framebuffer" );
        mem = nullptr;
        return false;
    }

    // 螢幕可能因為閒置被關掉（blank），先打開；例如 /dev/fb0 → /sys/class/graphics/fb0/blank
    std::string name = device.substr ( device.find_last_of ( '/' ) + 1 );
    std::ofstream blank ( "/sys/class/graphics/" + name + "/blank" );
    if ( blank ) blank << 0;

    clear ( );
    return true;
}

void Framebuffer::close ( )
{
    if ( mem ) munmap ( mem, mem_size );
    mem = nullptr;
}

void Framebuffer::clear ( )
{
    if ( mem ) memset ( mem, 0, (size_t) line_length * yres );
}

void Framebuffer::convert ( const cv::Mat &bgr, cv::Mat &screen ) const
{
    // screen 是指向 framebuffer 記憶體的 Mat，大小和型別都已經對了，cvtColor 會直接寫進去
    if ( bits_per_pixel == 16 )
        cv::cvtColor ( bgr, screen, cv::COLOR_BGR2BGR565 );
    else if ( bits_per_pixel == 32 )
        cv::cvtColor ( bgr, screen, red_offset == 0 ? cv::COLOR_BGR2RGBA : cv::COLOR_BGR2BGRA );
    else if ( bits_per_pixel == 24 && red_offset == 0 )
        cv::cvtColor ( bgr, screen, cv::COLOR_BGR2RGB );
    else
        bgr.copyTo ( screen );
}

void Framebuffer::show ( const cv::Mat &bgr, bool fit )
{
    if ( !mem || bgr.empty ( ) ) return;

    int type;
    if ( bits_per_pixel == 16 ) type = CV_8UC2;
    else if ( bits_per_pixel == 24 ) type = CV_8UC3;
    else if ( bits_per_pixel == 32 ) type = CV_8UC4;
    else
    {
        fprintf ( stderr, "unsupported framebuffer depth: %u bpp\n", bits_per_pixel );
        return;
    }

    // 等比例縮放，取寬、高比例較小的那個，保證整張圖放得進螢幕。
    // fit = false（原尺寸顯示）時只縮小不放大：640x480 的畫面在 1080p 螢幕上維持 640x480，
    // 每幀要寫的資料從約 6 MB 降到約 1.2 MB
    double scale = std::min ( (double) xres / bgr.cols, (double) yres / bgr.rows );
    if ( !fit && scale > 1.0 ) scale = 1.0;
    int w = std::max ( 1, (int) ( bgr.cols * scale ) );
    int h = std::max ( 1, (int) ( bgr.rows * scale ) );

    cv::Mat shown;
    if ( w != bgr.cols || h != bgr.rows )
        // 放大用 INTER_NEAREST（最快）；縮小用 INTER_AREA（3.2 的大圖縮小時比較不會有鋸齒）
        cv::resize ( bgr, shown, cv::Size ( w, h ), 0, 0, scale < 1.0 ? cv::INTER_AREA : cv::INTER_NEAREST );
    else
        shown = bgr;

    if ( shown.size ( ) != last_size )
    {
        clear ( );      // 尺寸改變時清掉舊的黑邊
        last_size = shown.size ( );
    }

    // 直接在 framebuffer 記憶體上建立一個 Mat（置中的那一塊），轉色時結果直接寫進螢幕，
    // 不需要先轉到暫存影像再一列一列 memcpy
    const int x_offset = ( xres - w ) / 2;
    const int y_offset = ( yres - h ) / 2;
    cv::Mat screen ( h, w, type, mem + y_offset * line_length + x_offset * ( bits_per_pixel / 8 ), line_length );
    convert ( shown, screen );
}
