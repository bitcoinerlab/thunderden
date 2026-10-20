#include "camera_entropy.h"
#include "entropy_process.h"
#include "assessment_progress.h"
#include "terminal.h"
#include <crypto/common.h>
#include <algorithm>
#include <memory>

namespace td {
std::optional<SecretBytes> Terminal::CameraEntropy(bool& unchecked)
{
    unchecked = false;
    if (!Confirm("Collect camera randomness", {"We'll collect camera readings and check part of the data for patterns using statistical software from NIST, "
        "the US National Institute of Standards and Technology. This helps estimate how unpredictable the checked data is.", "",
        "The checks run until they finish or you stop them.", "",
        "A still scene is fine; movement is not required."}, "start")) throw Cancelled{};
    SecretBytes digest(32);
    std::array<unsigned char, 17> profile{};
    size_t total = 0;
    unsigned frames = 0, warnings = 0;
    EntropyAssessment result{};
    AssessmentProgress progress;
    bool stopped = false;
    {
        EntropyFile samples, report;
        EntropyHash hash;
        EntropyWorker camera(EntropyExecutable(false));
        std::unique_ptr<EntropyWorker> assessment;
        const auto view = View();
        Draw(view, "Collect camera randomness", {}, "Esc: stop camera"); Flush();
        const auto start = std::chrono::steady_clock::now();
        auto last_frame = start, painted = start - std::chrono::seconds(1);
        bool have_profile = false, captured = false, ready = false;
        ReviewLines previous;
        std::array<unsigned char, 8> header{};
        size_t head = 0, body = 0, length = 0;
        unsigned type = 0;
        SecretBytes payload(ENTROPY_CHUNK);
        while (true) {
            const auto now = std::chrono::steady_clock::now();
            Require(View() == view, "The screen changed. Start camera collection again.");
            if (assessment) progress.Read(*assessment);
            Require(!progress.Constant(), "The checked camera data had no variation");
            const auto assessed = assessment ? assessment->PollExit() : std::nullopt;
            if (captured) {
                const auto exit = camera.PollExit();
                if (exit) Require(*exit == 0, "Camera collection failed");
                if (exit && assessed) {
                    report.Seal(); result = ParseAssessment(report.Read(), *assessed); break;
                }
                if (exit && !ready) {
                    ready = true;
                    Draw(view, "Checking camera data", {}, "Esc: stop checks"); previous.clear();
                    painted = start - std::chrono::seconds(1);
                }
            }
            const int key = Key(0);
            if (key == 27 || key == 3) {
                if (!ready) return std::nullopt; // Discard the attempt and reap both workers before offering retry/skip.
                stopped = true; break;
            }
            Require(captured || now - last_frame < std::chrono::seconds(10), "The camera stopped sending images");
            Require(ready || now - start < std::chrono::seconds(ENTROPY_CAPTURE_SECONDS + 10), "Camera collection took too long");
            if (now - painted >= std::chrono::milliseconds(250)) {
                ReviewLines lines{captured ? "Camera: finished" : frames ? "Camera: collecting images" : "Camera: starting",
                    "Check: " + std::to_string(progress.Current()) + " of 9"};
                if (view.height >= 7) {
                    const auto label = Wrap({"Current check: " + progress.Check()}, view.width);
                    lines.push_back(label[0]); lines.push_back(label.size() > 1 ? label[1] : "");
                } else lines.push_back(progress.Check());
                lines.push_back(progress.Pass());
                lines.push_back("Elapsed: " + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now - start).count()) + " seconds");
                for (size_t row = 0; row < lines.size(); ++row)
                    if (row >= previous.size() || previous[row] != lines[row]) Row(view, row, lines[row]);
                previous = std::move(lines);
                painted = now;
            }
            if (captured) {
                const int waiting = Key(20);
                if (waiting == 27 || waiting == 3) {
                    if (!ready) return std::nullopt;
                    stopped = true; break;
                }
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
                Require(!warnings, "The camera image froze or stayed almost entirely dark or bright");
                captured = true;
            }
            head = body = 0;
        }
        if (assessment) {
            if (stopped) assessment->Stop();
            progress.Read(*assessment);
        }
        progress.Boundary();
        hash.Finalize(digest.data());
    } // All workers are dead and reaped before any manual input or fresh RNG request.
    Require(!progress.Constant() && !result.constant, "The checked camera data had no variation");
    if (stopped) {
        if (Menu("Camera checks stopped early", {"Continue without camera data", "Use unchecked camera data"},
            std::to_string(progress.Finished()) + " of 9 checks finished. There is no final randomness estimate. "
            "You can leave the camera data out or add it as an unchecked extra. Fresh randomness from Bitcoin Core is used either way. "
            "Camera data may add useful randomness, but it never replaces or weakens Bitcoin Core's protection.",
            true, {}, true, progress.Details()) == 0) return SecretBytes{};
        unchecked = true;
        return CameraContribution(profile, total, digest);
    }
    const auto score = std::to_string(result.bits_per_sample);
    const auto format = ReadBE32(profile.data() + 1);
    std::string fourcc;
    for (unsigned shift = 0; shift < 32; shift += 8) fourcc += char(format >> shift);
    ReviewLines summary{"Estimated randomness in checked data:", score + " bits per camera value", "",
        "Camera values included: " + std::to_string(total)};
    if (result.warning) summary.insert(summary.begin(), {
        "Camera collection is complete, but NIST reported a warning or could not calculate every estimate.", ""});
    if (!Confirm(result.warning ? "Camera checks returned a warning" : "Camera data ready", summary, "use camera data",
        {"The randomness checks examined " + std::to_string(ENTROPY_TEST_SAMPLES) + " of the collected camera values.", "",
            "Separate bitstring checks use at most the first 1,000,000 bits.", "",
            "Image size: " + std::to_string(ReadBE32(profile.data() + 5)) + " x " + std::to_string(ReadBE32(profile.data() + 9)),
            "Camera output format: " + fourcc,
            profile[14] == 1 ? "Readings used: brightness" : "Readings used: red, green and blue",
            "Analysis software: NIST EntropyAssessment"}, result.warning,
        "Press D for details, or Enter to continue and use the camera data.")) throw Cancelled{};
    return CameraContribution(profile, total, digest);
}
}
