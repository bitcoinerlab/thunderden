#pragma once
#include <algorithm>
#include <cstdint>

namespace td {
struct CaptureArea {
    uint32_t x{}, y{}, width{}, height{};
    bool Fits(unsigned w, unsigned h) const {
        return width && height && width <= w && height <= h && x <= w - width && y <= h - height;
    }
    bool operator==(const CaptureArea&) const = default;
};
static_assert(sizeof(CaptureArea) == 16);

inline void AdjustCaptureCorner(CaptureArea& area, int dx, int dy, bool bottom_right, unsigned width, unsigned height)
{
    if (!area.Fits(width, height)) return;
    int left = area.x, top = area.y, right = left + area.width, bottom = top + area.height;
    // A detector hint may start smaller than the normal minimum. Do not shrink it further.
    const int min_width = std::min(32U, area.width), min_height = std::min(32U, area.height);
    if (bottom_right) {
        right = std::clamp(right + dx, left + min_width, int(width));
        bottom = std::clamp(bottom + dy, top + min_height, int(height));
    } else {
        left = std::clamp(left + dx, 0, right - min_width);
        top = std::clamp(top + dy, 0, bottom - min_height);
    }
    area = {unsigned(left), unsigned(top), unsigned(right - left), unsigned(bottom - top)};
}
}
