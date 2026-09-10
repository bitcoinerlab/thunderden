#pragma once
#include "qr.h"
#include "terminal.h"

#include <linux/fb.h>

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
