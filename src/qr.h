#pragma once

#include "keys.h"
#include <span>
#include <string>

namespace td {
struct QRImage {
    int width;
    SecretBytes modules;
    explicit QRImage(const std::string& text, int version = 0);
    QRImage(std::span<const uint8_t> data, bool numeric);
};

}
