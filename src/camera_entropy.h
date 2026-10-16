#pragma once
#include "entropy.h"
#include <array>

namespace td {
inline constexpr unsigned ENTROPY_BATCH_FRAMES = 64, ENTROPY_WARMUP_FRAMES = 8;
inline constexpr size_t ENTROPY_TEST_SAMPLES = 1048576, ENTROPY_MAX_SAMPLES = 64 * 1024 * 1024, ENTROPY_CHUNK = 65536;
inline constexpr unsigned ENTROPY_CAPTURE_SECONDS = 20;
std::vector<unsigned char> CameraSamples(std::span<const unsigned char> frame, unsigned width, unsigned height,
    unsigned stride, uint32_t format);
std::array<unsigned char, 17> CameraProfile(unsigned width, unsigned height, uint32_t format, unsigned frames);
void TemporalSamples(std::span<const unsigned char> batch, size_t frame_samples, unsigned frames,
    size_t offset, std::span<unsigned char> output);
}
