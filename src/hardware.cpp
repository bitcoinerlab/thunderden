#include "hardware.h"

#include <linux/kd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <support/allocators/zeroafterfree.h>

namespace td {
namespace {
constexpr std::array<uint8_t, 3> BACKGROUND{25, 26, 24}, BRIGHT{255, 255, 250}, ACCENT{247, 147, 26}, MUTED{168, 171, 163};

void ShowFrames(Terminal& terminal, std::string frame, URSender* sender, const ReviewScreen* review)
{
    const bool animated = sender && sender->Parts() > 1;
    // Reserve sequence growth for uppercase UR frames. Plain public-key QRs
    // contain case-sensitive Base58, so size those from the actual payload.
    const QRImage probe(animated ? std::string(std::min(MAX_QR_TEXT, frame.size() + 64), 'A') : frame);
    const int version = (probe.width - 17) / 4;
    const auto caption = review ? "Left/b: Review   Esc: Finish" : "Esc: Finish";
    while (true) {
        {
            Display display(terminal);
            terminal.Flush();
            display.QR(QRImage(frame, version), caption);
            while (true) {
                const int key = terminal.Key(animated ? 250 : -1);
                if (key == 27 || key == 3 || key == 'q') return;
                if (review && (key == 'b' || key == KEY_LEFT)) break;
                if (animated) {
                    frame = sender->Next();
                    display.QR(QRImage(frame, version), caption);
                }
            }
        } // Return the framebuffer to text mode before reopening the review.
        if (!terminal.Revisit(*review)) return;
    }
}
}

void ShowQR(Terminal& terminal, const QRMessage& message, const ReviewScreen* review)
{
    URSender sender(message);
    ShowFrames(terminal, sender.Next(), &sender, review);
}

void ShowQR(Terminal& terminal, const std::string& text, const ReviewScreen* review)
{
    ShowFrames(terminal, text, nullptr, review);
}

bool ShowSeedQR(Terminal& terminal, std::span<const unsigned char> mnemonic, bool reviewing)
{
    auto format = SeedQRFormat::Standard;
    Display display(terminal);
    while (true) {
        const auto data = EncodeSeedQR(mnemonic, format);
        const QRImage image(data, format == SeedQRFormat::Standard);
        const std::string caption = std::string(format == SeedQRFormat::Standard ? "Standard SeedQR   F: Compact" : "CompactSeedQR   F: Standard")
            + (reviewing ? "   Q: Words   Esc: Back" : "   Esc: Words");
        display.QR(image, caption, "Private recovery backup. Keep it private like your words.");
        terminal.Flush();
        while (true) {
            const int key = terminal.Key();
            if (key == 27 || key == 3 || (reviewing && (key == '\r' || key == '\n'))) return reviewing;
            if (key == 'q' || key == 'Q') return false;
            if (key == 'f' || key == 'F') break;
        }
        format = format == SeedQRFormat::Standard ? SeedQRFormat::Compact : SeedQRFormat::Standard;
    }
}

bool ShowRecoveryWords(Terminal& terminal, std::span<const unsigned char> mnemonic, bool reviewing)
{
    // Text-only development consoles retain the existing paginated word backup.
    const int fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (fd < 0) return false;
    close(fd);
    auto format = SeedQRFormat::Standard;
    bool visible = true;
    Display display(terminal);
    while (true) {
        const auto data = EncodeSeedQR(mnemonic, format);
        const QRImage image(data, format == SeedQRFormat::Standard);
        if (!display.RecoveryWords(mnemonic, visible ? &image : nullptr, format, reviewing)) return false;
        terminal.Flush();
        while (true) {
            const int key = terminal.Key();
            if (key == 27 || key == 3) {
                if (reviewing) return true;
                throw Cancelled{};
            }
            if (key == '\r' || key == '\n') return true;
            if (key == 'q' || key == 'Q') { visible = !visible; break; }
            if (visible && (key == 'f' || key == 'F')) {
                format = format == SeedQRFormat::Standard ? SeedQRFormat::Compact : SeedQRFormat::Standard;
                break;
            }
        }
    }
}

void ConfigureConsole(int tty)
{
    // Linux console palette: charcoal, paper and a restrained orange accent.
    const std::array<unsigned char, 48> palette{
        25,26,24, 239,68,68, 80,190,115, 247,147,26,
        110,155,210, 180,140,200, 95,185,185, 239,239,233,
        168,171,163, 255,115,115, 120,220,150, 255,185,80,
        150,185,230, 210,170,225, 140,215,215, 255,255,250};
    const auto ignored = ioctl(tty, PIO_CMAP, palette.data());
    (void)ignored; // Presentation can fall back to the console's existing palette.
    const int fd = open("/dev/fb0", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    fb_var_screeninfo info{};
    const int status = ioctl(fd, FBIOGET_VSCREENINFO, &info);
    close(fd);
    if (status != 0) return;
    const char* name = info.xres >= 1280 && info.yres >= 768 ? "TER16x32"
        : info.xres >= 800 && info.yres >= 432 ? "TER10x18" : "VGA8x16";
    console_font_op font{KD_FONT_OP_SET_DEFAULT, 0, 0, 0, 0,
        reinterpret_cast<unsigned char*>(const_cast<char*>(name))};
    if (ioctl(tty, KDFONTOP, &font) != 0 || info.xres < 2560 || info.yres < 1536) return;
    // Double the built-in 16x32 bitmap exactly for high-DPI panels. No font files
    // or fractional scaling are needed, and the kernel still draws the console.
    std::vector<uint8_t> small(512 * 32 * 2), large(512 * 64 * 4);
    font = {KD_FONT_OP_GET, 0, 16, 32, 512, small.data()};
    if (ioctl(tty, KDFONTOP, &font) != 0 || font.width != 16 || font.height != 32 || font.charcount > 512) return;
    for (unsigned ch = 0; ch < font.charcount; ++ch) {
        for (unsigned y = 0; y < 32; ++y) for (unsigned x = 0; x < 16; ++x) {
            if (!(small[ch * 64 + y * 2 + x / 8] & (0x80 >> (x % 8)))) continue;
            for (unsigned dy = 0; dy < 2; ++dy)
                large[ch * 256 + (2 * y + dy) * 4 + x / 4] |= 0xc0 >> (2 * (x % 4));
        }
    }
    font = {KD_FONT_OP_SET_TALL, 0, 32, 64, font.charcount, large.data()};
    const auto resized = ioctl(tty, KDFONTOP, &font);
    (void)resized; // An unsupported font size leaves the already-selected font.
}

Display::Display(Terminal& tty) : tty_(tty.FD()), network_(tty.Network())
{
    fd_ = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (fd_ < 0) throw std::runtime_error("A framebuffer display is required");
    try {
        Require(ioctl(fd_, FBIOGET_FSCREENINFO, &fixed_) == 0 && ioctl(fd_, FBIOGET_VSCREENINFO, &variable_) == 0,
            "Cannot read framebuffer layout");
        const auto& v = variable_;
        Require(fixed_.type == FB_TYPE_PACKED_PIXELS && fixed_.visual == FB_VISUAL_TRUECOLOR
            && (v.bits_per_pixel == 16 || v.bits_per_pixel == 24 || v.bits_per_pixel == 32)
            && v.xres >= 320 && v.xres <= 4096 && v.yres >= 240 && v.yres <= 4096,
            "Unsupported framebuffer layout");
        uint32_t mask = 0;
        for (const auto channel : {v.red, v.green, v.blue, v.transp}) {
            Require(channel.length <= 8 && channel.offset <= v.bits_per_pixel
                && channel.offset + channel.length <= v.bits_per_pixel && !channel.msb_right, "Unsupported framebuffer channel");
            const uint32_t bits = channel.length ? ((1U << channel.length) - 1) << channel.offset : 0;
            Require(!(mask & bits), "Overlapping framebuffer channels");
            mask |= bits;
        }
        Require(v.red.length && v.green.length && v.blue.length, "Missing framebuffer color channel");
        const uint64_t row_end = (uint64_t(v.xoffset) + v.xres) * (v.bits_per_pixel / 8);
        const uint64_t end = (uint64_t(v.yoffset) + v.yres - 1) * fixed_.line_length + row_end;
        Require(row_end <= fixed_.line_length && end <= fixed_.smem_len && fixed_.smem_len <= 128 * 1024 * 1024,
            "Framebuffer memory bounds invalid");
        auto memory = mmap(nullptr, fixed_.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        Require(memory != MAP_FAILED, "Cannot map framebuffer");
        memory_ = static_cast<uint8_t*>(memory);
        font_.resize(512 * 64 * 4);
        console_font_op font{KD_FONT_OP_GET_TALL, 0, 32, 64, 512, font_.data()};
        Require(ioctl(tty_, KDFONTOP, &font) == 0 && font.width > 0 && font.width <= 32
            && font.height > 0 && font.height <= 64 && font.charcount >= 128 && font.charcount <= 512, "Cannot read console font");
        font_width_ = font.width;
        font_height_ = font.height;
        int mode;
        Require(ioctl(tty_, KDGETMODE, &mode) == 0 && mode == KD_TEXT, "Display needs a Linux text console");
        Require(ioctl(tty_, KDSETMODE, KD_GRAPHICS) == 0, "Cannot activate framebuffer display");
        graphics_ = true;
        Clear();
    } catch (...) { Close(); throw; }
}

void Display::Close()
{
    // Camera previews and recovery QR/word pixels are secrets too. Do not leave
    // them in the framebuffer when returning to text mode, including on cancel.
    if (graphics_ && memory_) Clear();
    if (graphics_) ioctl(tty_, KDSETMODE, KD_TEXT);
    if (memory_) munmap(memory_, fixed_.smem_len);
    if (fd_ >= 0) close(fd_);
}

Display::~Display() { Close(); }

void Display::Pixel(unsigned x, unsigned y, uint8_t gray)
{
    Pixel(x, y, {gray, gray, gray});
}

void Display::Pixel(unsigned x, unsigned y, std::array<uint8_t, 3> color)
{
    uint32_t value = 0;
    size_t i = 0;
    for (const auto channel : {variable_.red, variable_.green, variable_.blue}) {
        value |= (uint32_t(color[i++]) >> (8 - channel.length)) << channel.offset;
    }
    if (variable_.transp.length) value |= ((1U << variable_.transp.length) - 1) << variable_.transp.offset;
    auto* pixel = memory_ + (y + variable_.yoffset) * size_t(fixed_.line_length)
        + (x + variable_.xoffset) * size_t(variable_.bits_per_pixel / 8);
    for (unsigned i = 0; i < variable_.bits_per_pixel / 8; ++i) pixel[i] = (value >> (8 * i)) & 0xff;
}

void Display::Clear()
{
    preview_progress_ = -1;
    qr_width_ = 0;
    for (unsigned y = 0; y < variable_.yres; ++y) for (unsigned x = 0; x < variable_.xres; ++x) Pixel(x, y, BACKGROUND);
}

unsigned Display::Caption(const ReviewLines& lines)
{
    const auto wrapped = Wrap(lines, (variable_.xres - 32) / font_width_);
    const unsigned height = wrapped.size() * (font_height_ + 4) + 16;
    Require(height < variable_.yres / 2, "Not enough room for the camera or QR code.");
    for (unsigned y = variable_.yres - height; y < variable_.yres; ++y) {
        for (unsigned x = 0; x < variable_.xres; ++x) Pixel(x, y, BACKGROUND);
    }
    for (size_t row = 0; row < wrapped.size(); ++row) {
        Text(16, variable_.yres - height + 8 + row * (font_height_ + 4), wrapped[row]);
    }
    return height;
}

void Display::Text(unsigned xoff, unsigned yoff, std::string_view text, std::array<uint8_t, 3> color)
{
    Require(xoff + text.size() * font_width_ <= variable_.xres && yoff + font_height_ <= variable_.yres, "Display text does not fit");
    const unsigned stride = (font_width_ + 7) / 8;
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned ch = static_cast<unsigned char>(text[i]);
        Require(ch >= 32 && ch <= 126, "Invalid display text");
        const auto* glyph = font_.data() + ch * 64 * stride;
        for (unsigned y = 0; y < font_height_; ++y) for (unsigned x = 0; x < font_width_; ++x)
            Pixel(xoff + i * font_width_ + x, yoff + y, (glyph[y * stride + x / 8] & (0x80 >> (x % 8))) ? color : BACKGROUND);
    }
}

void Display::Preview(std::span<const uint8_t> gray, unsigned width, unsigned height, double progress, bool seed)
{
    qr_width_ = 0;
    Require(width > 0 && width <= 1920 && height > 0 && height <= 1080
        && gray.size() == size_t(width) * height, "Invalid preview image");
    const int percent = int(progress * 100);
    if (percent != preview_progress_) {
        caption_height_ = Caption({seed ? "Point this device's camera at your seed QR backup."
                : "Point this device's camera at the code in your wallet app.",
            std::to_string(percent) + "% scanned   Esc: Cancel"});
        preview_progress_ = percent;
    }
    const unsigned fit_w = variable_.xres, fit_h = variable_.yres - caption_height_;
    const unsigned draw_w = std::min(fit_w, unsigned(uint64_t(width) * fit_h / height));
    const unsigned draw_h = unsigned(uint64_t(height) * draw_w / width);
    for (unsigned y = 0; y < draw_h; ++y) for (unsigned x = 0; x < draw_w; ++x) {
        Pixel((fit_w - draw_w) / 2 + x, (fit_h - draw_h) / 2 + y,
            gray[(uint64_t(y) * height / draw_h) * width + uint64_t(x) * width / draw_w]);
    }
}

void Display::QR(const QRImage& image, std::string_view caption, std::string_view instruction)
{
    const unsigned modules = image.width + 8;
    const ReviewLines lines{std::string(instruction), std::string(caption)};
    if (image.width != qr_width_ || lines != qr_caption_) {
        Clear();
        caption_height_ = Caption(lines);
        qr_width_ = image.width;
        qr_caption_ = lines;
    }
    const unsigned scale = std::min(variable_.xres, variable_.yres - caption_height_) / modules;
    Require(scale >= 2, "This QR code is too large for the screen.");
    Code(image, (variable_.xres - modules * scale) / 2, (variable_.yres - caption_height_ - modules * scale) / 2, scale);
}

void Display::Code(const QRImage& image, unsigned x, unsigned y, unsigned scale)
{
    Require(scale > 0 && x + (image.width + 8) * scale <= variable_.xres
        && y + (image.width + 8) * scale <= variable_.yres, "QR code does not fit");
    const unsigned modules = image.width + 8, side = modules * scale, bytes = variable_.bits_per_pixel / 8;
    const size_t stride = size_t(side) * bytes;
    uint32_t black = variable_.transp.length ? ((1U << variable_.transp.length) - 1) << variable_.transp.offset : 0;
    uint32_t white = black;
    for (const auto channel : {variable_.red, variable_.green, variable_.blue})
        white |= ((1U << channel.length) - 1) << channel.offset;
    // Recovery QRs share this path. Wipe even on exceptions, without the locked
    // allocator's small-allocation limit: scaled QR pixels can occupy megabytes.
    std::vector<uint8_t, zero_after_free_allocator<uint8_t>> pixels(stride * side);
    for (unsigned row = 0; row < modules; ++row) {
        auto* line = pixels.data() + row * scale * stride;
        auto* out = line;
        for (unsigned column = 0; column < modules; ++column) {
            const bool dark = row >= 4 && row < modules - 4 && column >= 4 && column < modules - 4
                && image.modules[(row - 4) * image.width + column - 4];
            const uint32_t value = dark ? black : white;
            for (unsigned dx = 0; dx < scale; ++dx) for (unsigned byte = 0; byte < bytes; ++byte)
                *out++ = (value >> (8 * byte)) & 0xff;
        }
        for (unsigned dy = 1; dy < scale; ++dy) std::memcpy(line + dy * stride, line, stride);
    }
    // Keep the old QR visible throughout preparation. This short row copy is
    // not a vblank-synchronized swap, but never exposes a white-fill stage.
    for (unsigned row = 0; row < side; ++row)
        std::memcpy(memory_ + (y + row + variable_.yoffset) * size_t(fixed_.line_length)
            + (x + variable_.xoffset) * bytes, pixels.data() + row * stride, stride);
}

bool Display::RecoveryWords(std::span<const unsigned char> mnemonic, const QRImage* qr, SeedQRFormat format, bool reviewing)
{
    const std::string_view words(reinterpret_cast<const char*>(mnemonic.data()), mnemonic.size());
    const size_t count = 1 + std::count(words.begin(), words.end(), ' ');
    // Match Terminal::View/Draw's centered console grid, including its blank
    // title/body row and two bottom rows. Only the private body uses pixel layout.
    const unsigned screen_columns = variable_.xres / font_width_, screen_rows = variable_.yres / font_height_;
    if (screen_columns < 40 || screen_rows < 12) return false;
    const unsigned width = std::min(80U, screen_columns - 2), left = (screen_columns - width) / 2 * font_width_;
    const unsigned body = screen_rows >= 20 ? 6 : 5, top = (body - 1) * font_height_;
    const unsigned divider = (screen_rows - 3) * font_height_, bottom = divider - (reviewing ? 1 : 2) * font_height_;
    const unsigned row = font_height_ + std::max(4U, font_height_ / 8), gap = font_height_ / 2;
    const unsigned columns = count == 24 ? 2 : 1, per_column = (count + columns - 1) / columns;
    const unsigned words_width = columns * 15 * font_width_, qr_x = left + words_width + 3 * font_width_;
    const unsigned right = left + width * font_width_;
    if (top >= bottom || per_column * row > bottom - top || qr_x >= right) return false;
    const unsigned available = right - qr_x;
    if (available < 21 * font_width_ || available < 37 * 3 || 2 * row + gap + 37 * 3 > bottom - top) return false;
    Clear();
    Text(left, (body - 5) * font_height_, "THUNDER DEN", BRIGHT);
    Text(right - network_.size() * font_width_, (body - 5) * font_height_, network_, ACCENT);
    const std::string rule(width, '-');
    Text(left, (body - 4) * font_height_, rule, MUTED);
    Text(left, (body - 3) * font_height_, reviewing ? "Private recovery backup" : "Write down your recovery words", BRIGHT);
    Text(left, divider, rule, MUTED);
    // Render secrets directly from the secure mnemonic, never through ReviewLines
    // or a normal std::string screen cache.
    size_t start = 0;
    for (size_t i = 0; i < count; ++i) {
        const auto end = words.find(' ', start);
        const unsigned x = left + (i / per_column) * 15 * font_width_, y = top + (i % per_column) * row;
        Text(x, y, std::to_string(i + 1) + ".");
        Text(x + 4 * font_width_, y, words.substr(start, end == words.npos ? end : end - start));
        start = end + 1;
    }
    if (qr) {
        const unsigned modules = qr->width + 8;
        const unsigned scale = std::min(available, bottom - top - 2 * row - gap) / modules;
        Require(scale >= 3, "Seed QR does not fit");
        const unsigned x = qr_x + (available - modules * scale) / 2;
        Code(*qr, x, top, scale);
        const std::string_view label = format == SeedQRFormat::Standard ? "Standard SeedQR" : "CompactSeedQR";
        const std::string_view action = format == SeedQRFormat::Standard ? "F: Switch to Compact" : "F: Switch to Standard";
        Text(qr_x + (available - label.size() * font_width_) / 2, top + modules * scale + gap, label);
        Text(qr_x + (available - action.size() * font_width_) / 2, top + modules * scale + gap + row, action, MUTED);
    } else {
        constexpr std::string_view hidden = "Seed QR hidden";
        Text(qr_x + (available - hidden.size() * font_width_) / 2, top, hidden, MUTED);
    }
    if (!reviewing) Text(left, bottom, "Copy every word in order.");
    Text(left, divider + font_height_, reviewing ? qr ? "Q: Hide QR   Enter: Back   Esc: Back" : "Q: Show QR   Enter: Back   Esc: Back"
        : qr ? "Q: Hide QR   Enter: Check backup   Esc: Cancel" : "Q: Show QR   Enter: Check backup   Esc: Cancel", MUTED);
    return true;
}
}
