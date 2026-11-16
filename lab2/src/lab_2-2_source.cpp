#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <linux/fb.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>

struct framebuffer_info
{
    uint32_t bits_per_pixel;    // depth of framebuffer
    uint32_t xres_virtual;      // how many pixel in a row in virtual screen
    uint32_t xres;
    uint32_t yres;
};

struct framebuffer_info get_framebuffer_info ( const char *framebuffer_device_path );

static struct termios saved_term;
static bool term_changed = false;

static void keyboard_raw_mode ( )
{
    if ( !isatty ( STDIN_FILENO ) ) return;
    tcgetattr ( STDIN_FILENO, &saved_term );
    struct termios t = saved_term;
    t.c_lflag &= ~( ICANON | ECHO );
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr ( STDIN_FILENO, TCSANOW, &t );
    term_changed = true;
}

static void keyboard_restore ( )
{
    if ( term_changed ) tcsetattr ( STDIN_FILENO, TCSANOW, &saved_term );
}

static int read_key ( )
{
    unsigned char c;
    if ( read ( STDIN_FILENO, &c, 1 ) == 1 ) return c;
    return -1;
}

static std::string make_screenshot_dir ( const std::string &base )
{
    for ( int id = 0; ; id++ )
    {
        std::string dir = base + "/screenshot_" + std::to_string ( id );
        struct stat st;
        if ( stat ( dir.c_str ( ), &st ) != 0 )
        {
            if ( mkdir ( dir.c_str ( ), 0755 ) != 0 )
            {
                perror ( "mkdir" );
                exit ( 1 );
            }
            return dir;
        }
    }
}

class ScreenshotWriter
{
public:
    void start ( ) { worker = std::thread ( &ScreenshotWriter::loop, this ); }

    void push ( const std::string &path, const cv::Mat &img )
    {
        {
            std::lock_guard<std::mutex> lock ( mtx );
            jobs.push ( std::make_pair ( path, img.clone ( ) ) );
        }
        cond.notify_one ( );
    }

    void finish ( )
    {
        {
            std::lock_guard<std::mutex> lock ( mtx );
            stopping = true;
        }
        cond.notify_one ( );
        if ( worker.joinable ( ) ) worker.join ( );
    }

private:
    void loop ( )
    {
        for ( ; ; )
        {
            std::pair<std::string, cv::Mat> job;
            {
                std::unique_lock<std::mutex> lock ( mtx );
                cond.wait ( lock, [this] { return stopping || !jobs.empty ( ); } );
                if ( jobs.empty ( ) ) return;
                job = jobs.front ( );
                jobs.pop ( );
            }
            if ( cv::imwrite ( job.first, job.second ) )
                std::cout << "saved " << job.first << std::endl;
            else
                std::cerr << "failed to save " << job.first << std::endl;
        }
    }

    std::queue<std::pair<std::string, cv::Mat> > jobs;
    std::mutex mtx;
    std::condition_variable cond;
    std::thread worker;
    bool stopping = false;
};

static volatile sig_atomic_t quit_flag = 0;
static void on_sigint ( int ) { quit_flag = 1; }

int main ( int argc, const char *argv[] )
{
    // variable to store the frame get from video stream
    cv::Mat frame;
    cv::Mat shown;
    cv::Mat frame565;
    cv::Size2f frame_size;
    std::string base_dir = ( argc > 1 ) ? argv[1] : ".";

    // open video stream device
    // https://docs.opencv.org/3.4.7/d8/dfe/classcv_1_1VideoCapture.html#a5d5f5dacb77bbebdcbfb341e3d4355c1
    cv::VideoCapture camera ( 2 );

    // get info of the framebuffer
    // fb_info = ......
    framebuffer_info fb_info = get_framebuffer_info ( "/dev/fb0" );
    const int bytes_per_pixel = fb_info.bits_per_pixel / 8;

    // open the framebuffer device
    // http://www.cplusplus.com/reference/fstream/ofstream/ofstream/
    // ofs = ......
    std::ofstream ofs ( "/dev/fb0" );

    // check if video stream device is opened success or not
    // https://docs.opencv.org/3.4.7/d8/dfe/classcv_1_1VideoCapture.html#a9d2ca36789e7fcfe7a7be3b328038585
    // if( !check )
    // {
    //     std::cerr << "Could not open video device." << std::endl;
    //     return 1;
    // }
    if ( !camera.isOpened ( ) )
    {
        std::cerr << "Could not open video device." << std::endl;
        return 1;
    }

    // set propety of the frame
    // https://docs.opencv.org/3.4.7/d8/dfe/classcv_1_1VideoCapture.html#a8c6d8c2d37505b5ca61ffd4bb54e9a7c
    // https://docs.opencv.org/3.4.7/d4/d15/group__videoio__flags__base.html#gaeb8dd9c89c10a5c63c139bf7c4f5704d
    camera.set ( cv::CAP_PROP_FRAME_WIDTH, 640 );
    camera.set ( cv::CAP_PROP_FRAME_HEIGHT, 480 );

    {
        std::vector<char> black ( fb_info.xres_virtual * bytes_per_pixel, 0 );
        for ( uint32_t y = 0; y < fb_info.yres; y++ )
        {
            ofs.seekp ( y * fb_info.xres_virtual * bytes_per_pixel, std::ios::beg );
            ofs.write ( black.data ( ), black.size ( ) );
        }
        ofs.flush ( );
    }

    std::string shot_dir = make_screenshot_dir ( base_dir );
    int shot_count = 0;
    std::cout << "screenshots will be saved to " << shot_dir << "  (press c = capture, q = quit)" << std::endl;

    ScreenshotWriter writer;
    writer.start ( );
    signal ( SIGINT, on_sigint );
    keyboard_raw_mode ( );

    while ( !quit_flag )
    {
        // get video frame from stream
        // https://docs.opencv.org/3.4.7/d8/dfe/classcv_1_1VideoCapture.html#a473055e77dd7faa4d26d686226b292c1
        // https://docs.opencv.org/3.4.7/d8/dfe/classcv_1_1VideoCapture.html#a199844fb74226a28b3ce3a39d1ff6765
        // frame = ......
        camera >> frame;
        if ( frame.empty ( ) ) break;

        // get size of the video frame
        // https://docs.opencv.org/3.4.7/d3/d63/classcv_1_1Mat.html#a146f8e8dda07d1365a575ab83d9828d1
        // frame_size = ......
        frame_size = frame.size ( );

        double scale = std::min ( (double) fb_info.xres / frame_size.width,
                                  (double) fb_info.yres / frame_size.height );
        int w = (int) ( frame_size.width * scale );
        int h = (int) ( frame_size.height * scale );
        if ( w != frame.cols || h != frame.rows )
            cv::resize ( frame, shown, cv::Size ( w, h ) );
        else
            shown = frame;
        int x_offset = ( fb_info.xres - w ) / 2;
        int y_offset = ( fb_info.yres - h ) / 2;

        // transfer color space from BGR to BGR565 (16-bit image) to fit the requirement of the LCD
        // https://docs.opencv.org/3.4.7/d8/d01/group__imgproc__color__conversions.html#ga397ae87e1288a81d2363b61574eb8cab
        // https://docs.opencv.org/3.4.7/d8/d01/group__imgproc__color__conversions.html#ga4e0972be5de079fed4e3a10e24ef5ef0
        cv::cvtColor ( shown, frame565, cv::COLOR_BGR2BGR565 );

        // output the video frame to framebufer row by row
        for ( int y = 0; y < h; y++ )
        {
            // move to the next written position of output device framebuffer by "std::ostream::seekp()"
            // http://www.cplusplus.com/reference/ostream/ostream/seekp/
            ofs.seekp ( ( ( y + y_offset ) * fb_info.xres_virtual + x_offset ) * bytes_per_pixel, std::ios::beg );

            // write to the framebuffer by "std::ostream::write()"
            // you could use "cv::Mat::ptr()" to get the pointer of the corresponding row.
            // you also need to cacluate how many bytes required to write to the buffer
            // http://www.cplusplus.com/reference/ostream/ostream/write/
            // https://docs.opencv.org/3.4.7/d3/d63/classcv_1_1Mat.html#a13acd320291229615ef15f96ff1ff738
            ofs.write ( reinterpret_cast<const char *> ( frame565.ptr ( y ) ), w * bytes_per_pixel );
        }
        ofs.flush ( );

        int key = read_key ( );
        if ( key == 'c' || key == 'C' )
        {
            writer.push ( shot_dir + "/" + std::to_string ( shot_count++ ) + ".bmp", frame );
        }
        else if ( key == 'q' || key == 'Q' )
        {
            break;
        }
    }

    keyboard_restore ( );
    writer.finish ( );

    // closing video stream
    // https://docs.opencv.org/3.4.7/d8/dfe/classcv_1_1VideoCapture.html#afb4ab689e553ba2c8f0fec41b9344ae6
    camera.release ( );

    std::cout << std::endl << "bye, " << shot_count << " screenshot(s) in " << shot_dir << std::endl;
    return 0;
}

struct framebuffer_info get_framebuffer_info ( const char *framebuffer_device_path )
{
    struct framebuffer_info fb_info;        // Used to return the required attrs.
    struct fb_var_screeninfo screen_info;   // Used to get attributes of the device from OS kernel.

    // open deive with linux system call "open( )"
    // https://man7.org/linux/man-pages/man2/open.2.html
    int fb_fd = open ( framebuffer_device_path, O_RDWR );
    if ( fb_fd < 0 )
    {
        perror ( "open" );
        exit ( 1 );
    }

    // get attributes of the framebuffer device thorugh linux system call "ioctl()"
    // the command you would need is "FBIOGET_VSCREENINFO"
    // https://man7.org/linux/man-pages/man2/ioctl.2.html
    // https://www.kernel.org/doc/Documentation/fb/api.txt
    if ( ioctl ( fb_fd, FBIOGET_VSCREENINFO, &screen_info ) < 0 )
    {
        perror ( "ioctl" );
        exit ( 1 );
    }

    // put the required attributes in variable "fb_info" you found with "ioctl() and return it."
    // fb_info.xres_virtual = ......
    // fb_info.bits_per_pixel = ......
    fb_info.xres_virtual = screen_info.xres_virtual;
    fb_info.bits_per_pixel = screen_info.bits_per_pixel;
    fb_info.xres = screen_info.xres;
    fb_info.yres = screen_info.yres;
    close ( fb_fd );

    return fb_info;
};
