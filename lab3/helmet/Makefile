# Cross-compile helmet for the E9V3 (armhf) on the x86 Ubuntu build host.
#
#   make
#
# OPENCV_DIR must contain include/opencv2/ and lib/libopencv_world.so (the Lab 2 build).
# NCNN_DIR is the ncnn that ../face/scripts/build_ncnn.sh installed (same library as 3.1).

# make 內建的 CXX 是 g++（x86），沒有在命令列指定時改用交叉編譯器
ifeq ($(origin CXX),default)
CXX := arm-linux-gnueabihf-g++
endif
OPENCV_DIR ?= /usr/local/arm-opencv/install
NCNN_DIR   ?= $(CURDIR)/../face/third_party/ncnn/build-armhf/install

TARGET   := helmet
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
