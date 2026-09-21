#include "camera.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
constexpr int CAMERA_FD = 100;
bool streaming = false, closed = false;
int read_error = 0;

void Check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
}

extern "C" int __wrap_v4l2_open(const char* path, int, ...)
{
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
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Camera test failed: %s\n", error.what());
        return 1;
    }
}
