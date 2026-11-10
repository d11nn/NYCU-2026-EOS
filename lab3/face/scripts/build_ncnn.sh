#!/bin/bash
# Cross-compile ncnn for the E9V3 (armv7 + NEON, hard float).
# Run on the x86 Ubuntu build host, from the lab3/face folder:
#   bash scripts/build_ncnn.sh
# Needs: git, cmake, arm-linux-gnueabihf-gcc/g++ on PATH.
set -e

NCNN_TAG=${NCNN_TAG:-20240410}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$ROOT/third_party"
cd "$ROOT/third_party"

if [ ! -d ncnn ]; then
    git clone --depth 1 --branch "$NCNN_TAG" https://github.com/Tencent/ncnn.git
fi

cd ncnn
mkdir -p build-armhf
cd build-armhf
cmake -DCMAKE_TOOLCHAIN_FILE=../toolchains/arm-linux-gnueabihf.toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DNCNN_VULKAN=OFF \
      -DNCNN_OPENMP=ON \
      -DNCNN_BUILD_TOOLS=OFF \
      -DNCNN_BUILD_EXAMPLES=OFF \
      -DNCNN_BUILD_BENCHMARK=ON \
      ..
make -j"$(nproc)"
make install

echo
echo "ncnn installed to $ROOT/third_party/ncnn/build-armhf/install"
echo "benchncnn (to measure speed on the board): $ROOT/third_party/ncnn/build-armhf/benchmark/benchncnn"
