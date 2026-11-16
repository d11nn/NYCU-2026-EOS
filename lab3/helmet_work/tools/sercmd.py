#!/usr/bin/env python3
"""Send one shell command to the E9V3 over /dev/ttyUSB0 (115200 8N1) and print its output.
usage: sercmd.py [-t SECONDS] [--raw] 'command'   (--raw: send text as-is, read until timeout)
Python 3.4 compatible (no pyserial on this laptop). Do not run while minicom has the port open.
"""
import os, sys, termios, time, select
PORT = '/dev/ttyUSB0'
def open_port():
    fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = 0                                   # iflag: raw, no xon/xoff
    a[1] = 0                                   # oflag
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[3] = 0                                   # lflag: no echo, non-canonical
    a[4] = a[5] = termios.B115200
    a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd
def main():
    args = sys.argv[1:]; timeout = 10.0; raw = False
    while args and args[0].startswith('-'):
        if args[0] == '-t': timeout = float(args[1]); args = args[2:]
        elif args[0] == '--raw': raw = True; args = args[1:]
        else: break
    cmd = ' '.join(args)
    fd = open_port()
    marker = '__SERDONE__'
    if raw:
        os.write(fd, cmd.encode().decode('unicode_escape').encode('latin-1'))
    else:
        # the quotes split the marker in the echoed command line, so only the real output contains it
        os.write(fd, (cmd + '; echo "__SER""DONE__ rc=$?"\r').encode())
    buf = b''; end = time.time() + timeout
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            chunk = os.read(fd, 65536)
            buf += chunk
            if not raw and (marker + ' rc=').encode() in buf and buf.rstrip().endswith((b'#', b'$', b'# ', b'$ ')) :
                break
            if not raw and (marker + ' rc=').encode() in buf:
                end = min(end, time.time() + 0.5)
    os.close(fd)
    sys.stdout.write(buf.decode('utf-8', 'replace').replace('\r', ''))
    if not raw and marker.encode() not in buf: print('\n[sercmd: no completion marker within %.0fs]' % timeout)
main()
