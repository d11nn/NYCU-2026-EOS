#include "keyboard.hpp"

#include <fcntl.h>
#include <linux/input.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <vector>

static struct termios saved_term;
static bool term_changed = false;
static std::vector<int> evdev_fds;

void keyboard_open ( )
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
    // 在板子的本機 console（/dev/ttyN）執行時，按鍵已經從 stdin 進來，不用再開 evdev
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

void keyboard_close ( )
{
    if ( term_changed ) tcsetattr ( STDIN_FILENO, TCSANOW, &saved_term );
    for ( size_t i = 0; i < evdev_fds.size ( ); i++ ) close ( evdev_fds[i] );
    evdev_fds.clear ( );
}

int read_key ( )
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
                case KEY_Q: return 'q';
                case KEY_S: return 's';
            }
        }
    }
    return -1;
}
