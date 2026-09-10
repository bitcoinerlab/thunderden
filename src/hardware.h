#pragma once
#include "qr.h"
#include "terminal.h"

#include <linux/fb.h>

namespace td {
class Display {
    int fd_{-1}, tty_;
    uint8_t* memory_{nullptr};
    fb_fix_screeninfo fixed_{};
    fb_var_screeninfo variable_{};
    bool graphics_{false};
    std::vector<uint8_t> font_;
    unsigned font_width_{}, font_height_{};
    void Close();
    void Pixel(unsigned x, unsigned y, uint8_t gray);
    void Clear();
    void Caption(std::string_view text);
public:
    explicit Display(Terminal& tty);
    ~Display();
    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;
    void Preview(std::span<const uint8_t> gray, unsigned width, unsigned height, double progress);
    void QR(const QRImage& image, std::string_view caption);
};
}
