#!/bin/sh
# Send files to the E9V3 over the serial console with zmodem (same as minicom Ctrl-A S).
# usage: sz_to_board.sh <dir on board> <files...>      (do not run while minicom has the port open)
D=$1; shift
T=$(dirname "$0")
python3 $T/sercmd.py -t 5 "mkdir -p $D" > /dev/null
python3 $T/sercmd.py --raw -t 2 "cd $D && rz -b -y\r" > /dev/null
stty -F /dev/ttyUSB0 115200 raw -echo -ixon -ixoff -crtscts cs8 -cstopb -parenb
sz -b -y "$@" < /dev/ttyUSB0 > /dev/ttyUSB0
