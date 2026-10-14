#include "camera_entropy.h"
#include "entropy_process.h"
#include "terminal.h"
#include <crypto/common.h>
#include <algorithm>
#include <memory>

namespace td {
SecretBytes Terminal::CameraEntropy()
{
    if (!Confirm("Add camera samples", {"System randomness is the main source. Camera samples are an optional extra.", "",
        "A still scene is fine; movement is not required. Capture and statistical assessment may take up to two minutes."}, "start capture")) throw Cancelled{};
    SecretBytes digest(32);
    std::array<unsigned char, 17> profile{};
    size_t total = 0;
    unsigned frames = 0, warnings = 0;
    EntropyAssessment result{};
    {
        EntropyFile samples, report;
        EntropyHash hash;
        EntropyWorker camera(EntropyExecutable(false));
        std::unique_ptr<EntropyWorker> assessment;
        const auto view = View();
        Draw(view, "Camera contribution", {}, "Esc: Cancel"); Flush();
        const auto start = std::chrono::steady_clock::now();
        auto last_frame = start, painted = start - std::chrono::seconds(1);
        bool have_profile = false, captured = false;
        std::array<unsigned char, 8> header{};
        size_t head = 0, body = 0, length = 0;
        unsigned type = 0;
        SecretBytes payload(ENTROPY_CHUNK);
        while (true) {
            const auto now = std::chrono::steady_clock::now();
            Require(View() == view, "The screen changed. Start camera collection again.");
            const int key = Key(0);
            if (key == 27 || key == 3) throw Cancelled{};
            Require(now - start < std::chrono::seconds(ENTROPY_TOTAL_SECONDS), "Camera assessment timed out; contribution omitted");
            Require(captured || now - last_frame < std::chrono::seconds(10), "Camera stopped producing samples; contribution omitted");
            Require(captured || now - start < std::chrono::seconds(ENTROPY_CAPTURE_SECONDS + 10), "Camera capture timed out; contribution omitted");
            const auto assessed = assessment ? assessment->PollExit() : std::nullopt;
            if (now - painted >= std::chrono::milliseconds(250)) {
                Row(view, 0, captured ? "Capture: complete" : "Capture: " + std::to_string(frames) + " frames");
                Row(view, 1, assessed ? "Assessment: finished" : assessment ? "Assessment: running" : "Assessment: waiting for samples");
                Row(view, 3, "Samples hashed: " + std::to_string(total));
                Row(view, 4, "Elapsed: " + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now - start).count()) + " seconds");
                painted = now;
            }
            if (captured) {
                const auto exit = camera.PollExit();
                if (exit) Require(*exit == 0, "Camera capture failed; contribution omitted");
                if (exit && assessed) {
                    report.Seal(); result = ParseAssessment(report.Read(), *assessed); break;
                }
                const int waiting = Key(20);
                if (waiting == 27 || waiting == 3) throw Cancelled{};
                continue;
            }
            if (head < header.size()) {
                head += camera.Read(std::span(header).subspan(head));
                if (head != header.size()) continue;
                type = ReadBE32(header.data()); length = ReadBE32(header.data() + 4);
                Require((type == 1 && !have_profile && length == profile.size())
                    || (have_profile && ((type == 2 && length == 4) || (type == 3 && length && length <= payload.size()) || (type == 4 && length == 4))),
                    "Invalid camera entropy record");
            }
            body += camera.Read(std::span(payload).subspan(body, length - body));
            if (body != length) continue;
            const auto bytes = std::span(payload).first(length);
            if (type == 1) {
                const auto expected = CameraProfile(ReadBE32(bytes.data() + 5), ReadBE32(bytes.data() + 9), ReadBE32(bytes.data() + 1), ENTROPY_BATCH_FRAMES);
                Require(std::equal(expected.begin(), expected.end(), bytes.begin()), "Unsupported camera entropy profile");
                profile = expected; have_profile = true;
            } else if (type == 2) {
                const auto count = ReadBE32(bytes.data());
                Require(count > frames && count <= 65536, "Invalid camera frame count");
                frames = count; last_frame = now;
            } else if (type == 3) {
                Require(length <= ENTROPY_MAX_SAMPLES - total, "Camera sample limit exceeded");
                hash.Write(bytes.data(), bytes.size());
                if (total < ENTROPY_TEST_SAMPLES) samples.Append(bytes.first(std::min(length, ENTROPY_TEST_SAMPLES - total)));
                total += length; last_frame = now;
                if (!assessment && total >= ENTROPY_TEST_SAMPLES) {
                    samples.Seal(); assessment = std::make_unique<EntropyWorker>(EntropyExecutable(true), samples.FD(), report.FD());
                }
            } else {
                warnings = ReadBE32(bytes.data());
                Require(warnings <= 3 && assessment && total % ENTROPY_BATCH_FRAMES == 0, "Incomplete camera contribution");
                Require(!warnings, "Camera froze or stayed saturated; contribution omitted");
                captured = true;
            }
            head = body = 0;
        }
        hash.Finalize(digest.data());
    } // All workers are dead and reaped before any manual input or fresh RNG request.
    Require(!result.constant, "Camera block was constant (zero estimate); contribution omitted");
    const auto score = std::to_string(result.bits_per_sample);
    const auto format = ReadBE32(profile.data() + 1);
    std::string fourcc;
    for (unsigned shift = 0; shift < 32; shift += 8) fourcc += char(format >> shift);
    if (!Confirm("Camera contribution", {"Capture: complete", result.warning ? "Assessment: complete with a warning" : "Assessment: complete",
        "Assessed samples: " + std::to_string(ENTROPY_TEST_SAMPLES), "Estimate: " + score + " bits/sample",
        "Samples hashed: " + std::to_string(total), "Hashed / assessed: " + std::to_string(double(total) / ENTROPY_TEST_SAMPLES) + "x", "",
        "Statistical estimate for the assessed block only. More samples do not imply the same increase in entropy. This is not a NIST certification."}, "include camera samples",
        {"Native sample profile v1. Driver exposure and gain defaults; no noise filtering.",
            "Native format: " + fourcc,
            result.warning ? "NIST reported a diagnostic warning. Its overall estimate is shown without modification." : "NIST initial non-IID assessment uses all bits of the fixed block.",
            "Width: " + std::to_string(ReadBE32(profile.data() + 5)), "Height: " + std::to_string(ReadBE32(profile.data() + 9)),
            profile[14] == 1 ? "Components: native Y" : "Components: R, G, B", "Frames per batch: 64"})) throw Cancelled{};
    return CameraContribution(profile, total, digest);
}
}
