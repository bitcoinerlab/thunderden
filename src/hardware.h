#pragma once
#include "qr.h"
#include "terminal.h"
#include "transport.h"

#include <linux/fb.h>

namespace td {
// Configure the local console before dropping privileges or loading recovery words.
void ConfigureConsole(int tty);
// Displays an already completed result; revisiting its public review cannot
// repeat signing/registration or change the reply being shown.
void ShowQR(Terminal& terminal, const QRMessage& message, const ReviewScreen* review = nullptr);
void ShowQR(Terminal& terminal, const std::string& text, const ReviewScreen* review = nullptr);

class Display {
    int fd_{-1}, tty_;
    uint8_t* memory_{nullptr};
    fb_fix_screeninfo fixed_{};
    fb_var_screeninfo variable_{};
    bool graphics_{false};
    int preview_progress_{-1};
    unsigned caption_height_{};
    std::vector<uint8_t> font_;
    unsigned font_width_{}, font_height_{};
    void Close();
    void Pixel(unsigned x, unsigned y, uint8_t gray);
    void Clear();
    unsigned Caption(const ReviewLines& lines);
public:
    explicit Display(Terminal& tty);
    ~Display();
    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;
    void Preview(std::span<const uint8_t> gray, unsigned width, unsigned height, double progress);
    void QR(const QRImage& image, std::string_view caption);
};
}
