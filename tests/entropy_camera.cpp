#include "camera.h"
#include "camera_entropy.h"
#include "keys.h"
#include <libv4l2.h>
#include <linux/videodev2.h>
#include <fcntl.h>
#include <cstdarg>
#include <cstring>
#include <cstdio>
#include <cerrno>

namespace {
unsigned scenario = 0;
v4l2_pix_format negotiated{};
const unsigned char frame[]{10,80,20,90,99,99,30,81,40,91};
}
extern "C" int __real_open(const char*, int, ...);
extern "C" int __wrap_open(const char* name, int flags, ...)
{
    if (std::string_view(name).starts_with("/dev/video")) {
        if (std::strcmp(name, "/dev/video0") == 0) return 63;
        errno = ENOENT; return -1;
    }
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list args; va_start(args, flags); mode = va_arg(args, int); va_end(args); }
    return __real_open(name, flags, mode);
}
extern "C" int __wrap_v4l2_fd_open(int fd, int flags)
{
    td::Require(fd == 63 && flags == V4L2_DISABLE_CONVERSION, "Native capture enabled conversion"); return fd;
}
extern "C" int __wrap_v4l2_close(int) { return 0; }
extern "C" int __wrap_v4l2_ioctl(int, unsigned long request, ...)
{
    va_list args; va_start(args, request); auto* value = va_arg(args, void*); va_end(args);
    if (request == VIDIOC_QUERYCAP) { static_cast<v4l2_capability*>(value)->capabilities = V4L2_CAP_VIDEO_CAPTURE; return 0; }
    if (request == VIDIOC_ENUM_FMT) {
        auto& format = *static_cast<v4l2_fmtdesc*>(value);
        if (format.index > 1) { errno = EINVAL; return -1; }
        format.pixelformat = format.index ? V4L2_PIX_FMT_YUYV : V4L2_PIX_FMT_GREY;
        format.flags = format.index ? 0 : V4L2_FMT_FLAG_EMULATED;
        return 0;
    }
    if (request == VIDIOC_S_FMT) {
        auto& pix = static_cast<v4l2_format*>(value)->fmt.pix;
        td::Require(pix.pixelformat == V4L2_PIX_FMT_YUYV, "Emulated GREY was accepted as native");
        pix.width = scenario == 3 ? 3 : 2; pix.height = 2; pix.bytesperline = 6; pix.sizeimage = 12;
        if (scenario == 4) pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        negotiated = pix; return 0;
    }
    if (request == VIDIOC_G_FMT) {
        static_cast<v4l2_format*>(value)->fmt.pix = negotiated;
        if (scenario == 2) static_cast<v4l2_format*>(value)->fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
        return 0;
    }
    errno = EINVAL; return -1;
}
extern "C" ssize_t __wrap_v4l2_read(int, void* target, size_t size)
{
    td::Require(size >= sizeof(frame), "Frame allocation too small");
    std::memcpy(target, frame, sizeof(frame)); return scenario == 1 ? 9 : sizeof(frame);
}
int main()
{
    try {
        for (scenario = 0; scenario < 5; ++scenario) {
            bool failed = false;
            try {
                td::Camera camera(true);
                td::Require(camera.Capture(), "No frame");
                td::Require(std::vector<unsigned char>(camera.Samples().begin(), camera.Samples().end())
                    == std::vector<unsigned char>({10,20,30,40}), "Native sample bytes changed");
            } catch (const std::exception&) { failed = true; }
            td::Require(failed == (scenario != 0), "Native format negotiation/capture regression");
        }
        std::puts("PASS: native format preference, disabled conversion, driver negotiation, stride, truncation and format changes");
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
