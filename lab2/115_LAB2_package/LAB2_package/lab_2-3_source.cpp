#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <linux/fb.h>
#include <linux/input.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
static std::vector<int> evdev_fds;

static void keyboard_open ( )
{
    if ( isatty ( STDIN_FILENO ) )
    {
        tcgetattr ( STDIN_FILENO, &saved_term );
        struct termios t = saved_term;
        t.c_lflag &= ~( ICANON | ECHO );
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        tcsetattr ( STDIN_FILENO, TCSANOW, &t );
        term_changed = true;
    }
    const char *tty = isatty ( STDIN_FILENO ) ? ttyname ( STDIN_FILENO ) : NULL;
    if ( tty && strncmp ( tty, "/dev/tty", 8 ) == 0 && tty[8] >= '0' && tty[8] <= '9' ) return;
    for ( int i = 0; i < 16; i++ )
    {
        std::string path = "/dev/input/event" + std::to_string ( i );
        int fd = open ( path.c_str ( ), O_RDONLY | O_NONBLOCK );
        if ( fd < 0 ) continue;
        struct input_event drain;
        while ( read ( fd, &drain, sizeof ( drain ) ) == sizeof ( drain ) ) { }
        evdev_fds.push_back ( fd );
    }
}

static void keyboard_close ( )
{
    if ( term_changed ) tcsetattr ( STDIN_FILENO, TCSANOW, &saved_term );
    for ( size_t i = 0; i < evdev_fds.size ( ); i++ ) close ( evdev_fds[i] );
}

static int read_key ( )
{
    unsigned char c;
    if ( isatty ( STDIN_FILENO ) && read ( STDIN_FILENO, &c, 1 ) == 1 ) return c;
    struct input_event ev;
    for ( size_t i = 0; i < evdev_fds.size ( ); i++ )
    {
        while ( read ( evdev_fds[i], &ev, sizeof ( ev ) ) == sizeof ( ev ) )
        {
            if ( ev.type != EV_KEY || ev.value != 1 ) continue;
            switch ( ev.code )
            {
                case KEY_J: return 'j';
                case KEY_L: return 'l';
                case KEY_C: return 'c';
                case KEY_Q: return 'q';
                case KEY_SPACE: return ' ';
            }
        }
    }
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
static void on_signal ( int ) { quit_flag = 1; }

struct framebuffer_map
{
    unsigned char *mem;
    size_t size;
    uint32_t line_length;
};

static struct framebuffer_map map_framebuffer ( const char *framebuffer_device_path )
{
    struct framebuffer_map map;
    struct fb_fix_screeninfo fix_info;
    int fb_fd = open ( framebuffer_device_path, O_RDWR );
    if ( fb_fd < 0 )
    {
        perror ( "open" );
        exit ( 1 );
    }
    if ( ioctl ( fb_fd, FBIOGET_FSCREENINFO, &fix_info ) < 0 )
    {
        perror ( "ioctl FBIOGET_FSCREENINFO" );
        exit ( 1 );
    }
    map.size = fix_info.smem_len;
    map.line_length = fix_info.line_length;
    map.mem = (unsigned char *) mmap ( NULL, map.size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0 );
    if ( map.mem == MAP_FAILED )
    {
        perror ( "mmap" );
        exit ( 1 );
    }
    close ( fb_fd );
    return map;
}

static void unblank_framebuffer ( )
{
    std::ofstream blank ( "/sys/class/graphics/fb0/blank" );
    blank << 0;
}

static void clear_screen ( const framebuffer_map &fb, const framebuffer_info &fb_info )
{
    memset ( fb.mem, 0, (size_t) fb.line_length * fb_info.yres );
}

static double now_seconds ( )
{
    struct timeval tv;
    gettimeofday ( &tv, NULL );
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

static int run_scroll_board ( const char *image_path, int speed )
{
    framebuffer_info fb_info = get_framebuffer_info ( "/dev/fb0" );
    const int bytes_per_pixel = fb_info.bits_per_pixel / 8;
    unblank_framebuffer ( );
    framebuffer_map fb = map_framebuffer ( "/dev/fb0" );

    cv::Mat picture = cv::imread ( image_path, cv::IMREAD_UNCHANGED );
    if ( picture.empty ( ) )
    {
        std::cerr << "Could not read " << image_path << std::endl;
        return 1;
    }
    if ( picture.channels ( ) == 4 )
    {
        cv::Mat bgr ( picture.rows, picture.cols, CV_8UC3 );
        for ( int y = 0; y < picture.rows; y++ )
        {
            const cv::Vec4b *src = picture.ptr<cv::Vec4b> ( y );
            cv::Vec3b *dst = bgr.ptr<cv::Vec3b> ( y );
            for ( int x = 0; x < picture.cols; x++ )
            {
                int a = src[x][3];
                dst[x] = cv::Vec3b ( src[x][0] * a / 255, src[x][1] * a / 255, src[x][2] * a / 255 );
            }
        }
        picture = bgr;
    }
    else if ( picture.channels ( ) == 1 )
    {
        cv::cvtColor ( picture, picture, cv::COLOR_GRAY2BGR );
    }
    if ( picture.rows > (int) fb_info.yres )
    {
        double scale = (double) fb_info.yres / picture.rows;
        cv::resize ( picture, picture, cv::Size ( (int) ( picture.cols * scale ), fb_info.yres ) );
    }

    cv::Mat picture565;
    cv::cvtColor ( picture, picture565, cv::COLOR_BGR2BGR565 );
    const int W = picture565.cols;
    const int H = picture565.rows;
    const int y_offset = ( fb_info.yres - H ) / 2;
    const int view_w = std::min ( (int) fb_info.xres, W );
    const int x_offset = ( fb_info.xres - view_w ) / 2;

    std::cout << "scroll board: image " << W << "x" << H << ", screen " << fb_info.xres << "x" << fb_info.yres
              << "  (j = move left, l = move right, space = pause, q = quit)" << std::endl;

    clear_screen ( fb, fb_info );
    signal ( SIGINT, on_signal );
    signal ( SIGTERM, on_signal );
    keyboard_open ( );

    double position = 0;
    int direction = -1;
    bool paused = false;
    const double frame_interval = 1.0 / 30.0;
    double next_frame = now_seconds ( );
    const double keys_enabled_at = now_seconds ( ) + 1.0;

    while ( !quit_flag )
    {
        int key = read_key ( );
        if ( now_seconds ( ) < keys_enabled_at ) key = -1;
        if ( key == 'j' || key == 'J' ) { direction = -1; paused = false; std::cout << "move left" << std::endl; }
        else if ( key == 'l' || key == 'L' ) { direction = 1; paused = false; std::cout << "move right" << std::endl; }
        else if ( key == ' ' ) { paused = !paused; std::cout << ( paused ? "paused" : "resumed" ) << std::endl; }
        else if ( key == 'q' || key == 'Q' ) break;

        int start = ( ( (int) position % W ) + W ) % W;
        for ( int y = 0; y < H; y++ )
        {
            unsigned char *dst = fb.mem + ( y + y_offset ) * fb.line_length + x_offset * bytes_per_pixel;
            const unsigned char *row = picture565.ptr ( y );
            int first = std::min ( view_w, W - start );
            memcpy ( dst, row + start * bytes_per_pixel, first * bytes_per_pixel );
            if ( first < view_w )
                memcpy ( dst + first * bytes_per_pixel, row, ( view_w - first ) * bytes_per_pixel );
        }

        if ( !paused && W > (int) fb_info.xres )
            position -= direction * speed;

        next_frame += frame_interval;
        double delay = next_frame - now_seconds ( );
        if ( delay > 0 ) usleep ( (useconds_t) ( delay * 1000000 ) );
        else next_frame = now_seconds ( );
    }

    keyboard_close ( );
    munmap ( fb.mem, fb.size );
    return 0;
}

int main ( int argc, const char *argv[] )
{
    if ( argc >= 3 && std::string ( argv[1] ) == "scroll" )
    {
        int speed = ( argc >= 4 ) ? atoi ( argv[3] ) : 6;
        return run_scroll_board ( argv[2], speed > 0 ? speed : 6 );
    }

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
    unblank_framebuffer ( );

    // open the framebuffer device
    // http://www.cplusplus.com/reference/fstream/ofstream/ofstream/
    // ofs = ......
    framebuffer_map fb = map_framebuffer ( "/dev/fb0" );

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
    int cam_w = 1280, cam_h = 720;
    if ( argc > 2 ) sscanf ( argv[2], "%dx%d", &cam_w, &cam_h );
    camera.set ( cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc ( 'M', 'J', 'P', 'G' ) );
    camera.set ( cv::CAP_PROP_FRAME_WIDTH, cam_w );
    camera.set ( cv::CAP_PROP_FRAME_HEIGHT, cam_h );

    clear_screen ( fb, fb_info );

    std::string shot_dir = make_screenshot_dir ( base_dir );
    int shot_count = 0;
    std::cout << "camera " << camera.get ( cv::CAP_PROP_FRAME_WIDTH ) << "x" << camera.get ( cv::CAP_PROP_FRAME_HEIGHT )
              << ", screen " << fb_info.xres << "x" << fb_info.yres << " @ " << fb_info.bits_per_pixel << " bpp" << std::endl;
    std::cout << "screenshots will be saved to " << shot_dir << "  (press c = capture, q = quit)" << std::endl;

    ScreenshotWriter writer;
    writer.start ( );
    signal ( SIGINT, on_signal );
    signal ( SIGTERM, on_signal );
    keyboard_open ( );

    int frames = 0;
    double fps_start = now_seconds ( );

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

        int w = frame.cols;
        int h = frame.rows;
        if ( w > (int) fb_info.xres || h > (int) fb_info.yres )
        {
            double scale = std::min ( (double) fb_info.xres / w, (double) fb_info.yres / h );
            w = (int) ( w * scale );
            h = (int) ( h * scale );
            cv::resize ( frame, shown, cv::Size ( w, h ) );
        }
        else
        {
            shown = frame;
        }
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
            unsigned char *dst = fb.mem + ( y + y_offset ) * fb.line_length + x_offset * bytes_per_pixel;

            // write to the framebuffer by "std::ostream::write()"
            // you could use "cv::Mat::ptr()" to get the pointer of the corresponding row.
            // you also need to cacluate how many bytes required to write to the buffer
            // http://www.cplusplus.com/reference/ostream/ostream/write/
            // https://docs.opencv.org/3.4.7/d3/d63/classcv_1_1Mat.html#a13acd320291229615ef15f96ff1ff738
            memcpy ( dst, frame565.ptr ( y ), w * bytes_per_pixel );
        }

        frames++;
        if ( frames % 60 == 0 )
        {
            std::cout << "frame " << frame.cols << "x" << frame.rows << " shown at (" << x_offset << "," << y_offset
                      << "), " << 60 / ( now_seconds ( ) - fps_start ) << " fps" << std::endl;
            fps_start = now_seconds ( );
        }

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

    keyboard_close ( );
    writer.finish ( );
    munmap ( fb.mem, fb.size );

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
