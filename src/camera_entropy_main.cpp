#include "camera.h"
#include "camera_entropy.h"
#include "isolation.h"
#include <crypto/common.h>
#include <linux/videodev2.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <chrono>

namespace {
void Send(uint32_t type, std::span<const unsigned char> bytes)
{
    unsigned char header[8]; WriteBE32(header, type); WriteBE32(header + 4, bytes.size());
    for (auto part : {std::span<const unsigned char>(header), bytes}) while (!part.empty()) {
        const auto n = write(STDOUT_FILENO, part.data(), part.size());
        if (n < 0 && errno == EINTR) continue;
        td::Require(n > 0, "Capture pipe closed"); part = part.subspan(n);
    }
}
}
int main()
{
    try {
        td::LockProcess();
        td::Camera camera(true);
        td::ConfineScanner();
        const auto profile = td::CameraProfile(camera.Width(), camera.Height(), camera.Format(), td::ENTROPY_BATCH_FRAMES);
        Send(1, profile);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(td::ENTROPY_CAPTURE_SECONDS);
        const size_t frame_size = camera.Width() * camera.Height() * profile[14];
        std::vector<unsigned char> batch(frame_size * td::ENTROPY_BATCH_FRAMES), previous, chunk(td::ENTROPY_CHUNK);
        unsigned frames = 0, in_batch = 0, repeated = 0, saturated = 0, warnings = 0;
        size_t sent = 0;
        while (sent < td::ENTROPY_MAX_SAMPLES && std::chrono::steady_clock::now() < deadline) {
            if (!camera.Capture()) continue;
            // A bounded poll or driver call can finish after the acquisition cutoff.
            if (std::chrono::steady_clock::now() >= deadline) break;
            unsigned char count[4]; WriteBE32(count, ++frames); Send(2, count);
            if (frames <= td::ENTROPY_WARMUP_FRAMES) continue;
            const auto samples = camera.Samples();
            td::Require(samples.size() == frame_size, "Camera format changed");
            repeated = std::equal(samples.begin(), samples.end(), previous.begin(), previous.end()) ? repeated + 1 : 0;
            const bool limited_y = camera.Format() == V4L2_PIX_FMT_YUYV || camera.Format() == V4L2_PIX_FMT_UYVY;
            const auto dark = std::count_if(samples.begin(), samples.end(), [&](auto c) { return c <= (limited_y ? 16 : 1); });
            const auto bright = std::count_if(samples.begin(), samples.end(), [&](auto c) { return c >= (limited_y ? 235 : 254); });
            saturated = std::max(dark, bright) * 100 >= ssize_t(frame_size) * 99 ? saturated + 1 : 0;
            if (repeated >= 8) warnings |= 1;
            if (saturated >= 8) warnings |= 2;
            previous.assign(samples.begin(), samples.end());
            std::copy(samples.begin(), samples.end(), batch.begin() + in_batch++ * frame_size);
            if (in_batch != td::ENTROPY_BATCH_FRAMES) continue;
            for (size_t offset = 0; offset < batch.size() && sent < td::ENTROPY_MAX_SAMPLES;) {
                const auto n = std::min({chunk.size(), batch.size() - offset, td::ENTROPY_MAX_SAMPLES - sent});
                auto bytes = std::span(chunk).first(n);
                td::TemporalSamples(batch, frame_size, td::ENTROPY_BATCH_FRAMES, offset, bytes);
                Send(3, bytes); offset += n; sent += n;
            }
            in_batch = 0;
        }
        td::Require(sent >= td::ENTROPY_TEST_SAMPLES, "Not enough camera samples");
        unsigned char flags[4]; WriteBE32(flags, warnings); Send(4, flags);
        return 0;
    } catch (...) { return 1; }
}
