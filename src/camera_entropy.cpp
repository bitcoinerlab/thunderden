#include "camera_entropy.h"
#include <crypto/common.h>
#include <linux/videodev2.h>

namespace td {
namespace {
unsigned Components(uint32_t format)
{
    if (format == V4L2_PIX_FMT_RGB24 || format == V4L2_PIX_FMT_BGR24) return 3;
    Require(format == V4L2_PIX_FMT_GREY || format == V4L2_PIX_FMT_YUYV || format == V4L2_PIX_FMT_UYVY, "Unsupported native sample format");
    return 1;
}
}

std::array<unsigned char, 17> CameraProfile(unsigned width, unsigned height, uint32_t format, unsigned frames)
{
    Require(width && width <= 640 && height && height <= 480 && frames && frames <= 128, "Invalid camera entropy profile");
    const auto components = Components(format);
    Require((format != V4L2_PIX_FMT_YUYV && format != V4L2_PIX_FMT_UYVY) || width % 2 == 0, "Invalid packed YUV width");
    std::array<unsigned char, 17> result{};
    result[0] = 1; WriteBE32(result.data() + 1, format); WriteBE32(result.data() + 5, width); WriteBE32(result.data() + 9, height);
    result[13] = 0; result[14] = components; WriteBE16(result.data() + 15, frames);
    return result;
}

std::vector<unsigned char> CameraSamples(std::span<const unsigned char> frame, unsigned width, unsigned height, unsigned stride, uint32_t format)
{
    const auto profile = CameraProfile(width, height, format, ENTROPY_BATCH_FRAMES);
    const unsigned components = profile[14], step = components == 3 ? 3 : format == V4L2_PIX_FMT_GREY ? 1 : 2;
    Require(stride >= width * step && stride <= 16 * 1024 * 1024 / height
        && frame.size() >= (height - 1) * size_t(stride) + width * step, "Truncated camera samples");
    std::vector<unsigned char> result(width * height * components);
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
        const auto* pixel = frame.data() + y * stride + x * step;
        auto* out = result.data() + (y * width + x) * components;
        if (components == 1) out[0] = pixel[format == V4L2_PIX_FMT_UYVY ? 1 : 0];
        else for (unsigned c = 0; c < 3; ++c) out[c] = pixel[format == V4L2_PIX_FMT_BGR24 ? 2 - c : c];
    }
    return result;
}

void TemporalSamples(std::span<const unsigned char> batch, size_t frame_samples, unsigned frames,
    size_t offset, std::span<unsigned char> output)
{
    Require(frames && frames <= 128 && frame_samples && frame_samples <= 640 * 480 * 3
        && batch.size() == frame_samples * frames && offset <= batch.size() && output.size() <= batch.size() - offset,
        "Invalid temporal sample block");
    for (size_t i = 0; i < output.size(); ++i) output[i] = batch[((offset + i) % frames) * frame_samples + (offset + i) / frames];
}
}
