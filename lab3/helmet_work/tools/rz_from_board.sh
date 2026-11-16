#!/bin/sh
# Receive files from the E9V3 over the serial console with zmodem (board runs sz, laptop runs rz).
# usage: rz_from_board.sh <local dir> <files on board...>      (do not run while minicom has the port open)
D=$1; shift
mkdir -p "$D"; cd "$D"
stty -F /dev/ttyUSB0 115200 raw -echo -ixon -ixoff -crtscts cs8 -cstopb -parenb
rz -b -y < /dev/ttyUSB0 > /dev/ttyUSB0 &
sleep 1
printf "sz -b %s\r" "$*" > /dev/ttyUSB0
wait
