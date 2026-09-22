#include "camera.h"
#include "hardware.h"

#include <fcntl.h>
#include <linux/kd.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <pty.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <system_error>

namespace {
constexpr int CAMERA_FD = 100;
bool streaming = false, closed = false;
int read_error = 0;
int open_error = 0;
bool second_camera = false;
constexpr int FRAMEBUFFER_FD = 101;
constexpr unsigned WIDTH = 320, HEIGHT = 240;
std::array<uint8_t, WIDTH * HEIGHT * 4> framebuffer{};
int display_mode = KD_TEXT;

void Check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
}

extern "C" int __wrap_v4l2_open(const char* path, int, ...)
{
    if (second_camera && std::strcmp(path, "/dev/video2") == 0) return CAMERA_FD;
    if (open_error) {
        errno = std::strcmp(path, "/dev/video0") == 0 ? open_error : ENOENT;
        return -1;
    }
    Check(std::strcmp(path, "/dev/video0") == 0, "Unexpected camera path");
    return CAMERA_FD;
}

extern "C" int __wrap_v4l2_ioctl(int fd, unsigned long request, ...)
{
    Check(fd == CAMERA_FD, "Unexpected camera descriptor");
    va_list args;
    va_start(args, request);
    void* argument = va_arg(args, void*);
    va_end(args);
    if (request == VIDIOC_QUERYCAP) {
        auto& cap = *static_cast<v4l2_capability*>(argument);
        cap.capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
    } else {
        Check(request == VIDIOC_S_FMT, "Unexpected camera ioctl");
        auto& pixels = static_cast<v4l2_format*>(argument)->fmt.pix;
        pixels.width = pixels.height = 2;
        pixels.pixelformat = V4L2_PIX_FMT_RGB24;
        pixels.bytesperline = 6;
        pixels.sizeimage = 12;
    }
    return 0;
}

extern "C" int __wrap_poll(pollfd* descriptors, nfds_t count, int)
{
    Check(count == 1 && descriptors[0].fd == CAMERA_FD, "Unexpected poll");
    // A streaming camera reports POLLERR until buffers are queued and streaming
    // starts. libv4l performs that setup lazily on its first read.
    descriptors[0].revents = streaming ? POLLIN : POLLERR;
    return 1;
}

extern "C" ssize_t __wrap_v4l2_read(int fd, void* buffer, size_t size)
{
    Check(fd == CAMERA_FD && size >= 12, "Invalid camera read");
    if (read_error) { errno = read_error; return -1; }
    streaming = true;
    std::memset(buffer, 64, 12);
    return 12;
}

extern "C" int __wrap_v4l2_close(int fd)
{
    Check(fd == CAMERA_FD, "Unexpected camera close");
    closed = true;
    return 0;
}

extern "C" int __wrap_open(const char* path, int, ...)
{
    Check(std::strcmp(path, "/dev/fb0") == 0, "Unexpected display open");
    return FRAMEBUFFER_FD;
}

extern "C" int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list args;
    va_start(args, request);
    if (request == KDSETMODE) {
        display_mode = va_arg(args, int);
        va_end(args);
        return 0;
    }
    void* argument = va_arg(args, void*);
    va_end(args);
    if (request == FBIOGET_FSCREENINFO) {
        Check(fd == FRAMEBUFFER_FD, "Wrong framebuffer descriptor");
        auto& info = *static_cast<fb_fix_screeninfo*>(argument);
        info = {};
        info.type = FB_TYPE_PACKED_PIXELS;
        info.visual = FB_VISUAL_TRUECOLOR;
        info.line_length = WIDTH * 4;
        info.smem_len = framebuffer.size();
    } else if (request == FBIOGET_VSCREENINFO) {
        auto& info = *static_cast<fb_var_screeninfo*>(argument);
        info = {};
        info.xres = WIDTH;
        info.yres = HEIGHT;
        info.bits_per_pixel = 32;
        info.red = {16, 8, 0};
        info.green = {8, 8, 0};
        info.blue = {0, 8, 0};
        info.transp = {24, 8, 0};
    } else if (request == KDFONTOP) {
        auto& font = *static_cast<console_font_op*>(argument);
        font.width = 8;
        font.height = 16;
        font.charcount = 256;
        std::memset(font.data, 0xff, font.charcount * 32);
    } else {
        Check(request == KDGETMODE, "Unexpected display ioctl");
        *static_cast<int*>(argument) = display_mode;
    }
    return 0;
}

extern "C" void* __wrap_mmap(void*, size_t length, int, int, int fd, off_t)
{
    Check(fd == FRAMEBUFFER_FD && length == framebuffer.size(), "Unexpected framebuffer mapping");
    return framebuffer.data();
}

extern "C" int __wrap_munmap(void* address, size_t length)
{
    Check(address == framebuffer.data() && length == framebuffer.size(), "Unexpected framebuffer unmap");
    return 0;
}

extern "C" int __real_close(int);
extern "C" int __wrap_close(int fd) { return fd == FRAMEBUFFER_FD ? 0 : __real_close(fd); }

int main()
{
    try {
        {
            td::Camera camera;
            Check(camera.Capture(), "Camera did not start capture");
            Check(streaming && camera.Width() == 2 && camera.Height() == 2
                && camera.Gray().size() == 4
                && std::all_of(camera.Gray().begin(), camera.Gray().end(), [](auto c) { return c == 64; }),
                "Camera frame was not converted correctly");
            Check(camera.Capture(), "Subsequent capture failed");
            for (const int error : {EINTR, EIO}) {
                read_error = error;
                Check(!camera.Capture(), "Transient capture error was not retried");
            }
            read_error = ENODEV;
            bool rejected = false;
            try { camera.Capture(); }
            catch (const std::runtime_error&) { rejected = true; }
            Check(rejected, "Disconnected camera was accepted");
        }
        Check(closed, "Camera descriptor was not closed");
        std::puts("PASS: streaming-camera startup, frame conversion, transient errors and disconnect");
        for (const int error : {EACCES, EPERM}) {
            open_error = error;
            bool denied = false;
            try { td::Camera camera; }
            catch (const std::system_error& failure) { denied = failure.code() == std::errc::permission_denied; }
            Check(denied, "Camera access failure lost its reason");
        }
        second_camera = true;
        { td::Camera camera; Check(camera.Width() == 2, "Inaccessible camera prevented selecting another device"); }
        second_camera = false;
        open_error = ENOENT;
        bool absent = false;
        try { td::Camera camera; }
        catch (const std::runtime_error& error) { absent = std::strcmp(error.what(), "No usable webcam found") == 0; }
        Check(absent, "Missing camera was reported as access denied");
        std::puts("PASS: missing and inaccessible cameras distinguished; other usable cameras remain selectable");

        int master, slave;
        Check(openpty(&master, &slave, nullptr, nullptr, nullptr) == 0, "Cannot open test tty");
        {
            td::Terminal terminal(slave);
            td::Display display(terminal);
            const std::array<uint8_t, 4> black{}, white{255, 255, 255, 255};
            const size_t marker = (HEIGHT - 1) * WIDTH * 4;
            const size_t image_pixel = (WIDTH - (HEIGHT - 48)) / 2 * 4;
            display.Preview(black, 2, 2, 0);
            framebuffer[marker] = 17;
            display.Preview(white, 2, 2, 0);
            Check(framebuffer[image_pixel] == 255, "Unchanged progress prevented preview updates");
            Check(framebuffer[marker] == 17, "Unchanged preview caption was erased and redrawn");
            display.Preview(black, 2, 2, 0.5);
            Check(framebuffer[marker] == 255, "Changed progress did not redraw the caption");
            display.QR(td::QRImage("public test"), "QR code");
            framebuffer[marker] = 17;
            display.Preview(black, 2, 2, 0.5);
            Check(framebuffer[marker] == 255, "Clearing the display did not invalidate the preview caption");
        }
        close(master);
        Check(display_mode == KD_TEXT, "Display did not return to text mode");
        std::puts("PASS: live preview updates without erasing an unchanged controls banner");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Camera test failed: %s\n", error.what());
        return 1;
    }
}
