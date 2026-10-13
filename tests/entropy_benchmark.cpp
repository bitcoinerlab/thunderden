// Development-only timings. Camera mode is for lab captures, never wallet material.
#include "camera.h"
#include "camera_entropy.h"
#include "entropy_process.h"
#include "isolation.h"
#include <crypto/common.h>
#include <sys/resource.h>
#include <signal.h>
#include <unistd.h>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <numeric>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
volatile sig_atomic_t stopped = 0;
void Stop(int) { stopped = 1; }
double Seconds(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }
}
int main(int argc, char** argv)
{
    const auto started = Clock::now();
    try {
        td::LockProcess();
        struct sigaction action{}; action.sa_handler = Stop;
        sigaction(SIGINT, &action, nullptr); sigaction(SIGTERM, &action, nullptr);
        const bool camera_mode = argc >= 2 && std::string_view(argv[1]) == "--camera";
        const bool serial = argc == 3 && std::string_view(argv[2]) == "--serial";
        td::Require(argc == 1 || (camera_mode && (argc == 2 || serial)), "Use entropy-benchmark [--camera [--serial]]");
        if (!camera_mode) {
            for (unsigned components : {1, 3}) for (unsigned frames : {32, 64, 128}) {
                const size_t per_frame = 640 * 480 * components;
                std::vector<unsigned char> batch(per_frame * frames), chunk(td::ENTROPY_CHUNK);
                std::iota(batch.begin(), batch.end(), 0); // Public repeatable data, not randomness.
                td::EntropyHash hash;
                double transpose = 0, hashing = 0;
                for (size_t offset = 0; offset < batch.size();) {
                    const auto n = std::min(chunk.size(), batch.size() - offset);
                    auto bytes = std::span(chunk).first(n);
                    auto tick = Clock::now(); td::TemporalSamples(batch, per_frame, frames, offset, bytes); transpose += Seconds(tick);
                    tick = Clock::now(); hash.Write(bytes.data(), bytes.size()); hashing += Seconds(tick);
                    offset += n;
                }
                std::printf("{\"components\":%u,\"frames\":%u,\"samples\":%zu,\"serialize_seconds\":%.6f,\"core_sha_seconds\":%.6f}\n",
                    components, frames, batch.size(), transpose, hashing);
            }
            return 0;
        }
        std::fprintf(stderr, "Lab camera measurement only. Keep recovery words and private material away from the camera.\n");
        td::EntropyFile input, report;
        std::unique_ptr<td::EntropyWorker> assessment;
        std::optional<int> status;
        td::EntropyHash hash;
        unsigned width{}, height{}, format{}, components{}, frames = 0;
        size_t total = 0;
        double warmup{}, capture = 0, transpose = 0, hashing = 0, assessment_time = 0;
        auto assessment_start = Clock::now();
        const auto assess = [&] {
            input.Seal(); assessment_start = Clock::now();
            assessment = std::make_unique<td::EntropyWorker>(td::EntropyExecutable(true), input.FD(), report.FD());
        };
        {
            td::Camera camera(true); width = camera.Width(); height = camera.Height(); format = camera.Format();
            components = td::CameraProfile(width, height, format, td::ENTROPY_BATCH_FRAMES)[14];
            const size_t per_frame = width * height * components;
            std::vector<unsigned char> batch(per_frame * td::ENTROPY_BATCH_FRAMES), chunk(td::ENTROPY_CHUNK);
            unsigned position = 0;
            while (Seconds(started) < td::ENTROPY_CAPTURE_SECONDS && total < td::ENTROPY_MAX_SAMPLES) {
                td::Require(!stopped, "Cancelled");
                const auto before = Clock::now(); const bool ready = camera.Capture(); capture += Seconds(before);
                if (!ready) continue;
                ++frames;
                if (frames <= td::ENTROPY_WARMUP_FRAMES) { warmup = Seconds(started); continue; }
                std::copy(camera.Samples().begin(), camera.Samples().end(), batch.begin() + position++ * per_frame);
                if (position != td::ENTROPY_BATCH_FRAMES) continue;
                for (size_t offset = 0; offset < batch.size() && total < td::ENTROPY_MAX_SAMPLES;) {
                    const auto n = std::min({chunk.size(), batch.size() - offset, td::ENTROPY_MAX_SAMPLES - total});
                    auto bytes = std::span(chunk).first(n);
                    auto tick = Clock::now(); td::TemporalSamples(batch, per_frame, td::ENTROPY_BATCH_FRAMES, offset, bytes); transpose += Seconds(tick);
                    tick = Clock::now(); hash.Write(bytes.data(), bytes.size()); hashing += Seconds(tick);
                    if (total < td::ENTROPY_TEST_SAMPLES) input.Append(bytes.first(std::min(n, td::ENTROPY_TEST_SAMPLES - total)));
                    total += n; offset += n;
                    if (!serial && !assessment && total >= td::ENTROPY_TEST_SAMPLES) assess();
                }
                position = 0;
                if (assessment && !status) { status = assessment->PollExit(); if (status) assessment_time = Seconds(assessment_start); }
            }
        }
        td::Require(total >= td::ENTROPY_TEST_SAMPLES, "Not enough lab samples");
        if (!assessment) assess();
        while (!status) {
            td::Require(!stopped && Seconds(started) < td::ENTROPY_TOTAL_SECONDS, "Assessment cancelled or timed out");
            status = assessment->PollExit(); assessment_time = Seconds(assessment_start);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        report.Seal(); const auto result = td::ParseAssessment(report.Read(), *status);
        rusage own{}, child{}; getrusage(RUSAGE_SELF, &own); getrusage(RUSAGE_CHILDREN, &child);
        std::printf("{\"width\":%u,\"height\":%u,\"fourcc\":%u,\"components\":%u,\"frames\":%u,\"T\":64,\"N_test\":%zu,\"N_total\":%zu,\"concurrent\":%s,\"warmup_seconds\":%.3f,\"capture_calls_seconds\":%.3f,\"serialize_seconds\":%.6f,\"core_sha_seconds\":%.6f,\"assessment_seconds\":%.3f,\"total_seconds\":%.3f,\"parent_peak_rss_kib\":%ld,\"assessment_peak_rss_kib\":%ld,\"bits_per_sample\":%.9f,\"constant\":%s}\n",
            width, height, format, components, frames, td::ENTROPY_TEST_SAMPLES, total, serial ? "false" : "true", warmup, capture,
            transpose, hashing, assessment_time, Seconds(started), own.ru_maxrss, child.ru_maxrss, result.bits_per_sample, result.constant ? "true" : "false");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Lab measurement incomplete after %.3f seconds: %s\n", Seconds(started), error.what()); return 1;
    }
}
