#include "camera.h"
#include "transport.h"

#include <libv4l2.h>
#include <linux/videodev2.h>
#include <fcntl.h>

#include <algorithm>
#include <cerrno>
#include <memory>
#include <stdexcept>

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

Camera::Camera()
{
    for (unsigned index = 0; index < 32; ++index) {
        const auto path = "/dev/video" + std::to_string(index);
        const int fd = v4l2_open(path.c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        v4l2_capability cap{};
        v4l2_format format{};
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.width = 640;
        format.fmt.pix.height = 480;
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
        format.fmt.pix.field = V4L2_FIELD_ANY;
        if (v4l2_ioctl(fd, VIDIOC_QUERYCAP, &cap) != 0
            || !((cap.capabilities & V4L2_CAP_DEVICE_CAPS ? cap.device_caps : cap.capabilities) & V4L2_CAP_VIDEO_CAPTURE)
            || v4l2_ioctl(fd, VIDIOC_S_FMT, &format) != 0) {
            v4l2_close(fd);
            continue;
        }
        const auto& pix = format.fmt.pix;
        const bool yuyv = pix.pixelformat == V4L2_PIX_FMT_YUYV;
        const size_t channels = yuyv ? 2 : 3;
        const size_t stride = std::max<size_t>(pix.bytesperline, pix.width * channels);
        if (!pix.width || pix.width > 1920 || !pix.height || pix.height > 1080
            || (yuyv && pix.width % 2) || (!yuyv && pix.pixelformat != V4L2_PIX_FMT_RGB24 && pix.pixelformat != V4L2_PIX_FMT_BGR24)
            || stride > 16 * 1024 * 1024 / pix.height || pix.sizeimage > 16 * 1024 * 1024) {
            v4l2_close(fd);
            continue;
        }
        try { frame_.resize(std::max<size_t>(pix.sizeimage, stride * pix.height)); }
        catch (...) { v4l2_close(fd); throw; }
        fd_ = fd;
        width_ = pix.width;
        height_ = pix.height;
        stride_ = stride;
        format_ = pix.pixelformat;
        return;
    }
    throw std::runtime_error("No usable webcam found");
}

Camera::~Camera() { if (fd_ >= 0) v4l2_close(fd_); }

bool Camera::Capture()
{
    // libv4l starts streaming on the first read. The parent handles cancellation
    // independently while this worker waits for a frame.
    const auto count = v4l2_read(fd_, frame_.data(), frame_.size());
    if (count < 0 && (errno == EAGAIN || errno == EINTR || errno == EIO)) return false;
    if (count <= 0) throw std::runtime_error("Webcam frame read failed");
    gray_ = Grayscale(std::span(frame_).first(count), width_, height_, stride_, format_);
    return true;
}

QRScanner::QRScanner() : scanner_(zbar::zbar_image_scanner_create())
{
    Require(scanner_ != nullptr, "QR scanner allocation failed");
    zbar::zbar_image_scanner_set_config(scanner_, zbar::ZBAR_NONE, zbar::ZBAR_CFG_ENABLE, 0);
    zbar::zbar_image_scanner_set_config(scanner_, zbar::ZBAR_QRCODE, zbar::ZBAR_CFG_ENABLE, 1);
    zbar::zbar_image_scanner_set_config(scanner_, zbar::ZBAR_QRCODE, zbar::ZBAR_CFG_BINARY, 1);
}

QRScanner::~QRScanner() { zbar::zbar_image_scanner_destroy(scanner_); }

std::vector<std::string> QRScanner::Scan(std::span<const uint8_t> gray, unsigned width, unsigned height)
{
    Require(width > 0 && width <= 1920 && height > 0 && height <= 1080
        && gray.size() == width * height, "Invalid camera image geometry");
    using namespace zbar;
    const std::unique_ptr<zbar_image_t, decltype(&zbar_image_destroy)> image(zbar_image_create(), zbar_image_destroy);
    Require(bool(image), "QR image allocation failed");
    zbar_image_set_format(image.get(), zbar_fourcc('Y', '8', '0', '0'));
    zbar_image_set_size(image.get(), width, height);
    zbar_image_set_data(image.get(), gray.data(), gray.size(), nullptr);
    Require(zbar_scan_image(scanner_, image.get()) >= 0, "QR image scan failed");
    std::vector<std::string> result;
    for (auto symbol = zbar_image_first_symbol(image.get()); symbol; symbol = zbar_symbol_next(symbol)) {
        const auto size = zbar_symbol_get_data_length(symbol);
        if (size > 0 && size <= MAX_QR_TEXT && zbar_symbol_get_type(symbol) == ZBAR_QRCODE) {
            result.emplace_back(zbar_symbol_get_data(symbol), size);
        }
    }
    return result;
}
}
