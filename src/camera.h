#pragma once

#include <zbar.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace td {
// RGB24/BGR24/YUYV camera frames, with row padding honored explicitly.
std::vector<uint8_t> Grayscale(std::span<const uint8_t> frame, unsigned width,
    unsigned height, unsigned stride, uint32_t format);

class Camera {
    int fd_{-1};
    unsigned width_{}, height_{}, stride_{};
    uint32_t format_{};
    std::vector<uint8_t> frame_, gray_;
public:
    Camera();
    ~Camera();
    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;
    bool Capture();
    unsigned Width() const { return width_; }
    unsigned Height() const { return height_; }
    std::span<const uint8_t> Gray() const { return gray_; }
};

class QRScanner {
    zbar::zbar_image_scanner_t* scanner_;
public:
    QRScanner();
    ~QRScanner();
    QRScanner(const QRScanner&) = delete;
    QRScanner& operator=(const QRScanner&) = delete;
    std::vector<std::string> Scan(std::span<const uint8_t> gray, unsigned width, unsigned height);
};
}
