#pragma once

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

}
