#!/bin/bash
# Download the Haar cascades and the MobileFaceNet ncnn model into ./models.
# Run from the lab3/face folder:
#   bash scripts/get_models.sh
#
# Sources:
#   - Haar cascades: OpenCV 3.4 repository (the same files ship in your OpenCV source under data/haarcascades/)
#   - MobileFaceNet (ncnn format, InsightFace weights, 112x112 in, 128-d "fc1" out):
#     github.com/liguiyuan/mobilefacenet-ncnn (the repository states no license; course use only)
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$ROOT/models"
cd "$ROOT/models"

OPENCV_RAW=https://github.com/opencv/opencv/raw/3.4/data/haarcascades
curl -fL -o haarcascade_frontalface_default.xml "$OPENCV_RAW/haarcascade_frontalface_default.xml"
curl -fL -o haarcascade_eye.xml "$OPENCV_RAW/haarcascade_eye.xml"

MFN_RAW=https://github.com/liguiyuan/mobilefacenet-ncnn/raw/master/models
curl -fL -o mobilefacenet.param "$MFN_RAW/mobilefacenet.param"
curl -fL -o mobilefacenet.bin "$MFN_RAW/mobilefacenet.bin"

ls -l
# mobilefacenet.bin should be 4091592 bytes
