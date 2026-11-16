#!/bin/sh
# On the board: load libopencv_world.so.3.4 and libgomp.so.1 from this folder, same as Lab 2's demo.sh
cd "$(dirname "$0")"
export LD_LIBRARY_PATH=.
./helmet "$@"
