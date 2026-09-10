#pragma once

#include <zbar.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace td {
struct QRImage {
    int width;
    std::vector<uint8_t> modules;
    explicit QRImage(const std::string& text, int version = 0);
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
