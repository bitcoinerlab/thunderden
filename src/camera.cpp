#include "camera.h"
#include "transport.h"
#include "camera_entropy.h"

#include <libv4l2.h>
#include <linux/videodev2.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <stdexcept>
#include <system_error>

namespace td {
std::vector<uint8_t> Grayscale(std::span<const uint8_t> frame, unsigned width,
    unsigned height, unsigned stride, uint32_t format)
{
    Require(width > 0 && width <= 1920 && height > 0 && height <= 1080, "Camera dimensions out of range");
    const bool yuyv = format == V4L2_PIX_FMT_YUYV;
    Require(yuyv || format == V4L2_PIX_FMT_RGB24 || format == V4L2_PIX_FMT_BGR24, "Unsupported camera format");
    Require(!yuyv || width % 2 == 0, "Invalid YUYV width");
    const size_t channels = yuyv ? 2 : 3;
    Require(stride >= channels * width && stride <= 16 * 1024 * 1024 / height
        && frame.size() >= (height - 1) * size_t(stride) + width * channels, "Truncated camera frame");
    std::vector<uint8_t> result(width * height);
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
        const auto* pixel = frame.data() + y * stride + x * channels;
        result[y * width + x] = yuyv ? pixel[0] : format == V4L2_PIX_FMT_RGB24
            ? (77U * pixel[0] + 150U * pixel[1] + 29U * pixel[2]) >> 8
            : (29U * pixel[0] + 150U * pixel[1] + 77U * pixel[2]) >> 8;
    }
    return result;
}

Camera::Camera(bool native) : native_(native)
{
    const auto usable = [native](const v4l2_pix_format& pix) {
        const bool yuyv = pix.pixelformat == V4L2_PIX_FMT_YUYV || (native && pix.pixelformat == V4L2_PIX_FMT_UYVY);
        const bool grey = native && pix.pixelformat == V4L2_PIX_FMT_GREY;
        const size_t channels = grey ? 1 : yuyv ? 2 : 3;
        const size_t stride = std::max<size_t>(pix.bytesperline, pix.width * channels);
        return pix.width && pix.width <= (native ? 640U : 1920U) && pix.height && pix.height <= (native ? 480U : 1080U)
            && (!yuyv || pix.width % 2 == 0)
            && (yuyv || grey || pix.pixelformat == V4L2_PIX_FMT_RGB24 || pix.pixelformat == V4L2_PIX_FMT_BGR24)
            && stride <= 16 * 1024 * 1024 / pix.height && pix.sizeimage <= 16 * 1024 * 1024;
    };
    bool denied = false;
    for (unsigned index = 0; index < 32; ++index) {
        const auto path = "/dev/video" + std::to_string(index);
        int fd;
        if (native) {
            fd = open(path.c_str(), O_RDWR | O_CLOEXEC | O_NONBLOCK);
            if (fd >= 0 && v4l2_fd_open(fd, V4L2_DISABLE_CONVERSION) < 0) { close(fd); fd = -1; }
        } else fd = v4l2_open(path.c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            denied |= errno == EACCES || errno == EPERM;
            continue;
        }
        v4l2_capability cap{};
        v4l2_format format{};
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.width = 640;
        format.fmt.pix.height = 480;
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
        format.fmt.pix.field = V4L2_FIELD_ANY;
        bool configured = v4l2_ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0
            && ((cap.capabilities & V4L2_CAP_DEVICE_CAPS ? cap.device_caps : cap.capabilities) & V4L2_CAP_VIDEO_CAPTURE);
        if (native) configured = configured
            && ((cap.capabilities & V4L2_CAP_DEVICE_CAPS ? cap.device_caps : cap.capabilities) & V4L2_CAP_STREAMING);
        if (configured && native) {
            configured = false;
            for (auto preferred : {V4L2_PIX_FMT_GREY, V4L2_PIX_FMT_YUYV, V4L2_PIX_FMT_UYVY, V4L2_PIX_FMT_RGB24, V4L2_PIX_FMT_BGR24}) {
                for (unsigned n = 0; n < 64 && !configured; ++n) {
                    v4l2_fmtdesc available{}; available.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; available.index = n;
                    if (v4l2_ioctl(fd, VIDIOC_ENUM_FMT, &available) < 0) break;
                    if (available.pixelformat != preferred || (available.flags & V4L2_FMT_FLAG_EMULATED)) continue;
                    format = {}; format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; format.fmt.pix.field = V4L2_FIELD_ANY;
                    format.fmt.pix.width = 640; format.fmt.pix.height = 480; format.fmt.pix.pixelformat = preferred;
                    configured = v4l2_ioctl(fd, VIDIOC_S_FMT, &format) == 0 && format.fmt.pix.pixelformat == preferred
                        && usable(format.fmt.pix);
                }
                if (configured) break;
            }
        } else if (configured) configured = v4l2_ioctl(fd, VIDIOC_S_FMT, &format) == 0;
        if (!configured || !usable(format.fmt.pix)) {
            v4l2_close(fd);
            continue;
        }
        const auto& pix = format.fmt.pix;
        const bool yuyv = pix.pixelformat == V4L2_PIX_FMT_YUYV || (native && pix.pixelformat == V4L2_PIX_FMT_UYVY);
        const bool grey = native && pix.pixelformat == V4L2_PIX_FMT_GREY;
        const size_t channels = grey ? 1 : yuyv ? 2 : 3;
        const size_t stride = std::max<size_t>(pix.bytesperline, pix.width * channels);
        fd_ = fd;
        width_ = pix.width;
        height_ = pix.height;
        stride_ = stride;
        format_ = pix.pixelformat;
        try {
            if (native_) StartStreaming();
            else frame_.resize(std::max<size_t>(pix.sizeimage, stride * pix.height));
        } catch (...) { Close(); throw; }
        return;
    }
    if (denied) throw std::system_error(EACCES, std::generic_category());
    throw std::runtime_error("No usable webcam found");
}

void Camera::StartStreaming()
{
    v4l2_requestbuffers request{};
    request.count = 2; request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; request.memory = V4L2_MEMORY_MMAP;
    Require(v4l2_ioctl(fd_, VIDIOC_REQBUFS, &request) == 0 && request.count > 0 && request.count <= 4,
        "Cannot allocate native camera buffers");
    buffers_.resize(request.count);
    for (unsigned i = 0; i < buffers_.size(); ++i) {
        v4l2_buffer buffer{};
        buffer.type = request.type; buffer.memory = request.memory; buffer.index = i;
        Require(v4l2_ioctl(fd_, VIDIOC_QUERYBUF, &buffer) == 0 && buffer.length > 0 && buffer.length <= 16 * 1024 * 1024,
            "Invalid native camera buffer");
        auto* data = static_cast<uint8_t*>(v4l2_mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buffer.m.offset));
        Require(data != MAP_FAILED, "Cannot map native camera buffer");
        buffers_[i] = {data, buffer.length};
        Require(v4l2_ioctl(fd_, VIDIOC_QBUF, &buffer) == 0, "Cannot queue native camera buffer");
    }
    auto type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    Require(v4l2_ioctl(fd_, VIDIOC_STREAMON, &type) == 0, "Cannot start native camera stream");
    streaming_ = true;
}

void Camera::Close()
{
    if (streaming_) { auto type = V4L2_BUF_TYPE_VIDEO_CAPTURE; v4l2_ioctl(fd_, VIDIOC_STREAMOFF, &type); }
    for (auto buffer : buffers_) if (!buffer.empty()) v4l2_munmap(buffer.data(), buffer.size());
    buffers_.clear(); streaming_ = false;
    if (fd_ >= 0) v4l2_close(fd_);
    fd_ = -1;
}
Camera::~Camera() { Close(); }

bool Camera::Capture()
{
    if (native_) {
        v4l2_format current{}; current.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        const auto step = format_ == V4L2_PIX_FMT_GREY ? 1U : format_ == V4L2_PIX_FMT_RGB24 || format_ == V4L2_PIX_FMT_BGR24 ? 3U : 2U;
        Require(v4l2_ioctl(fd_, VIDIOC_G_FMT, &current) == 0 && current.fmt.pix.width == width_ && current.fmt.pix.height == height_
            && current.fmt.pix.pixelformat == format_ && std::max(current.fmt.pix.bytesperline, width_ * step) == stride_, "Camera sample format changed");
        pollfd ready{fd_, POLLIN, 0};
        const int state = poll(&ready, 1, 50);
        if (!state || (state < 0 && errno == EINTR)) return false;
        Require(state > 0 && (ready.revents & POLLIN) && !(ready.revents & (POLLERR | POLLHUP | POLLNVAL)), "Native camera polling failed");
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; buffer.memory = V4L2_MEMORY_MMAP;
        const int result = v4l2_ioctl(fd_, VIDIOC_DQBUF, &buffer);
        if (result < 0 && (errno == EAGAIN || errno == EINTR || errno == EIO)) return false;
        Require(result == 0 && buffer.index < buffers_.size() && buffer.bytesused <= buffers_[buffer.index].size(), "Invalid native camera frame");
        const bool usable = !(buffer.flags & V4L2_BUF_FLAG_ERROR);
        if (usable) gray_ = CameraSamples(buffers_[buffer.index].first(buffer.bytesused), width_, height_, stride_, format_);
        Require(v4l2_ioctl(fd_, VIDIOC_QBUF, &buffer) == 0, "Cannot requeue native camera buffer");
        return usable;
    }
    // For QR capture, libv4l starts streaming on the first read. The parent handles cancellation
    // independently while this worker waits for a frame.
    const auto count = v4l2_read(fd_, frame_.data(), frame_.size());
    if (count < 0 && (errno == EAGAIN || errno == EINTR || errno == EIO)) return false;
    if (count <= 0) throw std::runtime_error("Webcam frame read failed");
    gray_ = Grayscale(std::span(frame_).first(count), width_, height_, stride_, format_);
    return true;
}

}
