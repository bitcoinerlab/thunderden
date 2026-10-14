#include "camera.h"
#include "keys.h"
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <poll.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <cstdarg>
#include <cstring>
#include <cstdio>
#include <cerrno>

namespace {
unsigned scenario = 0, mappings = 0, reads = 0, dequeued = 0, requeued = 0, stopped = 0;
int device = -1;
bool streaming = false;
v4l2_pix_format negotiated{};
const unsigned char frame[]{10,80,20,90,99,99,30,81,40,91};
}
extern "C" int __real_open(const char*, int, ...);
extern "C" int __wrap_open(const char* name, int flags, ...)
{
    if (std::string_view(name).starts_with("/dev/video")) {
        if (std::strcmp(name, "/dev/video0") != 0) { errno = ENOENT; return -1; }
        td::Require(flags & O_NONBLOCK, "Native device must be nonblocking");
        device = __real_open("/dev/null", flags);
        return device;
    }
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list args; va_start(args, flags); mode = va_arg(args, int); va_end(args); }
    return __real_open(name, flags, mode);
}
extern "C" int __real_poll(pollfd*, nfds_t, int);
extern "C" int __wrap_poll(pollfd* fds, nfds_t count, int timeout)
{
    if (count != 1 || fds[0].fd != device) return __real_poll(fds, count, timeout);
    td::Require(timeout >= 0 && timeout <= 50, "Unbounded native poll");
    if (scenario == 10) return 0;
    fds[0].revents = scenario == 14 ? POLLERR : POLLIN;
    return 1;
}

// Exercise real libv4l2, replacing only the streaming-only device's syscalls.
extern "C" long syscall(long number, ...) noexcept
{
    using Call = long(*)(long,...);
    static auto real = reinterpret_cast<Call>(dlsym(RTLD_NEXT, "syscall"));
    va_list args; va_start(args, number);
    if (number == SYS_ioctl) {
        const int fd = va_arg(args, int); const auto request = va_arg(args, unsigned long);
        auto* value = va_arg(args, void*); va_end(args);
        if (fd != device) return real(number, fd, request, value);
        if (request == VIDIOC_QUERYCAP) {
            auto& cap = *static_cast<v4l2_capability*>(value);
            cap.capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING; return 0;
        }
        if (request == VIDIOC_ENUM_FMT) {
            auto& format = *static_cast<v4l2_fmtdesc*>(value);
            if (format.index > 1) { errno = EINVAL; return -1; }
            format.pixelformat = format.index ? V4L2_PIX_FMT_YUYV : V4L2_PIX_FMT_GREY;
            format.flags = format.index ? 0 : V4L2_FMT_FLAG_EMULATED; return 0;
        }
        if (request == VIDIOC_S_FMT) {
            auto& pix = static_cast<v4l2_format*>(value)->fmt.pix;
            if (pix.pixelformat != V4L2_PIX_FMT_YUYV) { errno = EINVAL; return -1; }
            pix.width = scenario == 3 ? 3 : 2; pix.height = 2; pix.bytesperline = 6; pix.sizeimage = 12;
            if (scenario == 4) pix.pixelformat = V4L2_PIX_FMT_MJPEG;
            negotiated = pix; return 0;
        }
        if (request == VIDIOC_G_FMT) {
            auto& pix = static_cast<v4l2_format*>(value)->fmt.pix;
            pix = negotiated;
            if (scenario == 2 && streaming) pix.pixelformat = V4L2_PIX_FMT_RGB24;
            return 0;
        }
        if (request == VIDIOC_REQBUFS) {
            auto& buffers = *static_cast<v4l2_requestbuffers*>(value);
            buffers.count = scenario == 16 ? 5 : 2; return 0;
        }
        if (request == VIDIOC_QUERYBUF) {
            auto& buffer = *static_cast<v4l2_buffer*>(value);
            if (scenario == 5 && buffer.index == 1) { errno = EIO; return -1; }
            buffer.length = scenario == 17 ? 17 * 1024 * 1024 : 4096;
            buffer.m.offset = buffer.index * 4096; return 0;
        }
        if (request == VIDIOC_QBUF) {
            if (scenario == 7 || (scenario == 15 && dequeued)) { errno = EIO; return -1; }
            if (dequeued) ++requeued;
            return 0;
        }
        if (request == VIDIOC_STREAMON) {
            if (scenario == 8) { errno = EIO; return -1; }
            streaming = true; return 0;
        }
        if (request == VIDIOC_STREAMOFF) { streaming = false; ++stopped; return 0; }
        if (request == VIDIOC_DQBUF) {
            if (scenario == 11) { errno = EAGAIN; return -1; }
            auto& buffer = *static_cast<v4l2_buffer*>(value);
            buffer.index = scenario == 12 ? 2 : 0;
            buffer.bytesused = scenario == 1 ? 9 : scenario == 13 ? 4097 : sizeof(frame);
            buffer.flags = scenario == 9 ? V4L2_BUF_FLAG_ERROR : 0;
            ++dequeued; return 0;
        }
        errno = EINVAL; return -1;
    }
    if (number == SYS_mmap) {
        auto* addr = va_arg(args, void*); auto size = va_arg(args, size_t);
        const int prot = va_arg(args, int), flags = va_arg(args, int), fd = va_arg(args, int);
        const auto offset = va_arg(args, off_t); va_end(args);
        if (fd != device) return real(number, addr, size, prot, flags, fd, offset);
        if (scenario == 6 && mappings == 1) { errno = ENOMEM; return -1; }
        const auto result = real(number, nullptr, size, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (result != -1) { ++mappings; std::memcpy(reinterpret_cast<void*>(result), frame, sizeof(frame)); }
        return result;
    }
    if (number == SYS_munmap) {
        auto* addr = va_arg(args, void*); auto size = va_arg(args, size_t); va_end(args);
        --mappings; return real(number, addr, size);
    }
    if (number == SYS_read) { va_end(args); ++reads; errno = EINVAL; return -1; }
    if (number == SYS_close) {
        const int fd = va_arg(args, int); va_end(args);
        if (fd == device) device = -1;
        return real(number, fd);
    }
    va_end(args); errno = ENOSYS; return -1;
}
int main()
{
    try {
        for (scenario = 0; scenario < 18; ++scenario) {
            negotiated = {}; reads = dequeued = requeued = stopped = 0;
            bool failed = false;
            try {
                td::Camera camera(true);
                const bool captured = camera.Capture();
                const bool retry = scenario == 9 || scenario == 10 || scenario == 11;
                td::Require(captured != retry, "Native retry handling failed");
                if (captured) td::Require(std::vector<unsigned char>(camera.Samples().begin(), camera.Samples().end())
                    == std::vector<unsigned char>({10,20,30,40}), "Native sample bytes changed");
            } catch (const std::exception&) { failed = true; }
            const bool success = scenario == 0 || scenario == 9 || scenario == 10 || scenario == 11;
            td::Require(failed != success, "Native streaming scenario failed");
            td::Require(device == -1 && mappings == 0 && !streaming && !reads, "Native resources leaked or raw read used");
            if (scenario == 0 || scenario == 9) td::Require(dequeued == 1 && requeued == 1 && stopped == 1, "Native buffer lifecycle failed");
        }
        std::puts("PASS: real libv4l2 streaming, native bytes, retries, malformed buffers and partial-setup cleanup");
    } catch (const std::exception& e) { std::fprintf(stderr, "scenario %u: %s\n", scenario, e.what()); return 1; }
}
