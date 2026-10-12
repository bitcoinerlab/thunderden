#!/bin/sh
set -eu
set -a
. /opt/inputs.env
set +a
curl --fail --location --retry 3 "https://codeload.github.com/opencv/opencv/tar.gz/$OPENCV_VERSION" -o /tmp/opencv.tar.gz
printf '%s  %s\n' "$OPENCV_SHA256" /tmp/opencv.tar.gz | sha256sum -c -
mkdir -p /opt/opencv /opt/ocr
tar -xf /tmp/opencv.tar.gz --strip-components=1 -C /opt/opencv
curl --fail --location --retry 3 "$OCR_MODEL_BASE/det/ch_PP-OCRv5_det_mobile.onnx" -o /opt/ocr/det.onnx
curl --fail --location --retry 3 "$OCR_MODEL_BASE/rec/en_PP-OCRv5_rec_mobile.onnx" -o /opt/ocr/rec-original.onnx
curl --fail --location --retry 3 "https://raw.githubusercontent.com/RapidAI/RapidOCR/v3.9.2/LICENSE" -o /opt/ocr/LICENSE
printf '%s  %s\n' "$OCR_LICENSE_SHA256" /opt/ocr/LICENSE | sha256sum -c -
python3 -m venv /tmp/ocr-build-env
CMAKE_BUILD_PARALLEL_LEVEL=4 MAX_JOBS=4 /tmp/ocr-build-env/bin/pip install -r /work/build/ocr-requirements.txt
/tmp/ocr-build-env/bin/python /work/build/ocr-models.py /opt/ocr
cmake -S /opt/opencv -B /tmp/opencv-build -G Ninja -C /work/build/ocr-opencv.cmake \
    -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build /tmp/opencv-build --parallel 4
cmake --install /tmp/opencv-build
ldconfig
rm -rf /tmp/opencv.tar.gz /tmp/opencv-build /tmp/ocr-build-env
