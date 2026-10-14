#include "camera.h"
#include "camera_entropy.h"
#include <crypto/common.h>
#include <linux/videodev2.h>
#include <chrono>
#include <cstdio>
#include <unistd.h>

int EntropyCameraMain();
namespace {
unsigned captured = 0, boundary_ms = 0, now_ms = 0;
bool slow_send = false;
std::vector<unsigned char> output;
}
// Test-only device, clock and confinement substitutes; run the real worker loop.
namespace td {
void LockProcess() {}
void ConfineScanner() {}
Camera::Camera(bool) { width_ = height_ = 128; format_ = V4L2_PIX_FMT_GREY; }
Camera::~Camera() = default;
bool Camera::Capture()
{
    ++captured;
    now_ms = captured < 72 ? 0 : captured == 72 ? boundary_ms : 20000;
    gray_.assign(width_ * height_, 32 + captured);
    return true;
}
}
extern "C" std::chrono::steady_clock::time_point __wrap__ZNSt6chrono3_V212steady_clock3nowEv() noexcept
{ return std::chrono::steady_clock::time_point(std::chrono::milliseconds(now_ms)); }
extern "C" ssize_t __real_write(int, const void*, size_t);
extern "C" ssize_t __wrap_write(int fd, const void* data, size_t size)
{
    if (fd != STDOUT_FILENO) return __real_write(fd, data, size);
    const auto* bytes = static_cast<const unsigned char*>(data);
    output.insert(output.end(), bytes, bytes + size);
    if (slow_send && captured == 72) now_ms = 25000;
    return size;
}
int main()
{
    try {
        for (boundary_ms = 19999; boundary_ms <= 20001; ++boundary_ms) for (bool slow : {false, true}) {
            captured = now_ms = 0; slow_send = slow; output.clear();
            const auto result = EntropyCameraMain();
            size_t samples = 0;
            unsigned frames = 0, completed = 0;
            for (size_t offset = 0; offset < output.size();) {
                td::Require(output.size() - offset >= 8, "Truncated capture record");
                const auto type = ReadBE32(output.data() + offset), size = ReadBE32(output.data() + offset + 4);
                offset += 8;
                td::Require(size <= output.size() - offset, "Truncated capture body");
                if (type == 2) frames = ReadBE32(output.data() + offset);
                if (type == 3) samples += size;
                if (type == 4) ++completed;
                offset += size;
            }
            const bool timely = boundary_ms < 20000;
            td::Require(result == (timely ? 0 : 1) && completed == unsigned(timely)
                && frames == (timely ? 72U : 71U) && samples == (timely ? td::ENTROPY_TEST_SAMPLES : 0),
                "A late frame entered the contribution or a timely batch was lost");
        }
        std::puts("PASS: real capture loop rejects frames at/after cutoff; timely batches may drain afterward");
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
