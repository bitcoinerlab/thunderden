// Real pinned libv4l2, with only a synthetic streaming-only device interface.
#include <libv4l2.h>
#include <linux/videodev2.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>

static int fixture_fd = -1;
static unsigned reads, stream_requests;
extern "C" long syscall(long number, ...) noexcept
{
    using Call = long(*)(long,...);
    static auto real = reinterpret_cast<Call>(dlsym(RTLD_NEXT, "syscall"));
    va_list args; va_start(args, number);
    if (number == SYS_ioctl) {
        const int fd = va_arg(args, int);
        const auto request = va_arg(args, unsigned long);
        void* value = va_arg(args, void*); va_end(args);
        if (fd != fixture_fd) return real(number, fd, request, value);
        if (request == VIDIOC_QUERYCAP) {
            auto& cap = *static_cast<v4l2_capability*>(value); std::memset(&cap, 0, sizeof(cap));
            cap.capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING; return 0;
        }
        if (request == VIDIOC_G_FMT) {
            auto& format = *static_cast<v4l2_format*>(value); format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            format.fmt.pix.width = 640; format.fmt.pix.height = 480; format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
            format.fmt.pix.field = V4L2_FIELD_NONE; format.fmt.pix.bytesperline = 1280; format.fmt.pix.sizeimage = 614400;
            return 0;
        }
        if (request == VIDIOC_REQBUFS || request == VIDIOC_STREAMON) ++stream_requests;
        errno = EINVAL; return -1;
    }
    if (number == SYS_read) {
        const int fd = va_arg(args, int); auto* data = va_arg(args, void*); const auto size = va_arg(args, size_t); va_end(args);
        if (fd != fixture_fd) return real(number, fd, data, size);
        ++reads; errno = EINVAL; return -1; // A streaming-only UVC interface has no read operation.
    }
    if (number == SYS_close) { const int fd = va_arg(args, int); va_end(args); return real(number, fd); }
    va_end(args); errno = ENOSYS; return -1;
}
int main()
{
    fixture_fd = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (fixture_fd < 0) return 2;
    const auto registered = v4l2_fd_open(fixture_fd, V4L2_DISABLE_CONVERSION);
    unsigned char bytes[32]{};
    const auto result = registered >= 0 ? v4l2_read(fixture_fd, bytes, sizeof(bytes)) : -2;
    const auto error = errno;
    std::printf("registered=%d read_result=%zd errno=%d raw_read_calls=%u streaming_setup_calls=%u\n",
        registered >= 0, result, error, reads, stream_requests);
    if (registered >= 0) v4l2_close(fixture_fd); else close(fixture_fd);
    return registered >= 0 && result == -1 && error == EINVAL && reads == 1 && stream_requests == 0 ? 0 : 1;
}
