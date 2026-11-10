# Cross-compile face3 for the E9V3 (armhf) on the x86 Ubuntu build host.
#
#   make OPENCV_DIR=/path/to/opencv-3.4.7/install
#
# OPENCV_DIR must contain include/opencv2/ and lib/libopencv_world.so (the Lab 2 build).
# NCNN_DIR is where scripts/build_ncnn.sh installs ncnn.

# Makefile
# 名字 := 值 代表用的時候寫成 $(名字)會自己把它換成值
# X ?= 值 代表沒人設定過才設定
#
# make 內建的 CXX 是 g++（x86），沒有在命令列指定時改用交叉編譯器
#
# origin 代表 CXX 這個變數的 value 從哪裡來的
# default 內建 CXX = g++, 所以意思是如果 CXX 還是 g++ 就改成交叉編譯器
# 為什麼不直接寫 CXX ?= arm-linux-gnueabihf-g++？
# 因為 CXX 已經有內建值 g++ 了，?= 會認為「有人設定過」而不動它，結果還是 g++。
# 這是 make 的一個小陷阱，所以才用 origin 判斷。
ifeq ($(origin CXX),default) # ifeq (A,B) 代表只有 A 等於 B 才執行這裡
CXX := arm-linux-gnueabihf-g++
endif
OPENCV_DIR ?= $(HOME)/opencv-3.4.7/install
NCNN_DIR   ?= $(CURDIR)/third_party/ncnn/build-armhf/install

TARGET   := face3
SRCS     := $(wildcard src/*.cpp)
OBJS     := $(SRCS:src/%.cpp=build/%.o)

CXXFLAGS := -O2 -std=c++11 -Wall -march=armv7-a -mfpu=neon -mfloat-abi=hard -fopenmp \
            -I$(OPENCV_DIR)/include -I$(NCNN_DIR)/include/ncnn
# libopencv_world 編譯時開了 Qt5 的 highgui，連結時會去找 libQt5*.so。
# 我們沒用到 Qt 的視窗功能，那些符號留到板子上執行時再由板子的 Qt5 函式庫解析
LDFLAGS  := -L$(OPENCV_DIR)/lib -L$(NCNN_DIR)/lib -fopenmp -Wl,--allow-shlib-undefined
LDLIBS   := -lncnn -lopencv_world -lpthread -ldl

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

build/%.o: src/%.cpp src/*.hpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf build $(TARGET)

.PHONY: all clean
