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

void Framebuffer::convert ( const cv::Mat &bgr, cv::Mat &out ) const
{
    if ( bits_per_pixel == 16 )
        cv::cvtColor ( bgr, out, cv::COLOR_BGR2BGR565 );
    else if ( bits_per_pixel == 32 )
        cv::cvtColor ( bgr, out, red_offset == 0 ? cv::COLOR_BGR2RGBA : cv::COLOR_BGR2BGRA );
    else if ( bits_per_pixel == 24 )
    {
        if ( red_offset == 0 ) cv::cvtColor ( bgr, out, cv::COLOR_BGR2RGB );
        else out = bgr;
    }
    else
        out.release ( );
}

void Framebuffer::show ( const cv::Mat &bgr )
{
    if ( !mem || bgr.empty ( ) ) return;

    // 等比例縮放，取寬、高比例較小的那個，保證整張圖放得進螢幕
    double scale = std::min ( (double) xres / bgr.cols, (double) yres / bgr.rows );
    int w = std::max ( 1, (int) ( bgr.cols * scale ) );
    int h = std::max ( 1, (int) ( bgr.rows * scale ) );
    cv::Mat shown;
    if ( w != bgr.cols || h != bgr.rows )
        cv::resize ( bgr, shown, cv::Size ( w, h ), 0, 0, scale < 1.0 ? cv::INTER_AREA : cv::INTER_LINEAR );
    else
        shown = bgr;

    if ( shown.size ( ) != last_size )
    {
        clear ( );      // 尺寸改變時清掉舊的黑邊
        last_size = shown.size ( );
    }

    cv::Mat pixels;
    convert ( shown, pixels );
    if ( pixels.empty ( ) )
    {
        fprintf ( stderr, "unsupported framebuffer depth: %u bpp\n", bits_per_pixel );
        return;
    }

    const int bytes_per_pixel = bits_per_pixel / 8;
    const int x_offset = ( xres - w ) / 2;
    const int y_offset = ( yres - h ) / 2;
    for ( int y = 0; y < h; y++ )
    {
        unsigned char *dst = mem + ( y + y_offset ) * line_length + x_offset * bytes_per_pixel;
        memcpy ( dst, pixels.ptr ( y ), w * bytes_per_pixel );
    }
}
