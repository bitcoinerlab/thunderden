#include "assessment_progress.h"
#include "camera_entropy.h"
#include "nist_progress.h"
#include <crypto/common.h>
#include <unistd.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>

namespace td {
namespace {
unsigned active_test{}, active_branch{};
uint32_t active_total{};
auto last_update = std::chrono::steady_clock::time_point{};
void ProgressRecord(unsigned kind, unsigned test, unsigned branch, uint32_t processed, uint32_t total, double estimate = -1)
{
    unsigned char record[20]{static_cast<unsigned char>(kind), static_cast<unsigned char>(test), static_cast<unsigned char>(branch), 0};
    WriteBE32(record + 4, processed); WriteBE32(record + 8, total); WriteBE64(record + 12, std::bit_cast<uint64_t>(estimate));
    // Descriptor 5 is a nonblocking pipe. Each record fits PIPE_BUF: either it is sent
    // whole or omitted. A slow reader must never stall an estimator.
    const auto ignored = write(5, record, sizeof(record)); (void)ignored;
}
unsigned Group(unsigned test) { return test <= 5 ? test : test - 1; }
constexpr std::array<const char*, 9> checks{"Counting common values", "Checking repetitions", "Checking value pairs",
    "Checking compression", "Checking sequences", "Using recent values", "Looking back in time", "Predicting next value", "Predicting sequences"};
constexpr std::array<const char*, 10> methods{"Most Common Value", "Collision", "Markov", "Compression", "T-Tuple", "LRS",
    "MultiMCW", "Lag", "MultiMMC", "LZ78Y"};
}

void NistInput(unsigned alphabet, uint64_t values, uint64_t bits)
{ ProgressRecord(0, 0, std::min(alphabet, 3U), values, bits); }
void NistStart(unsigned test, unsigned branch, uint64_t total)
{
    active_test = test; active_branch = branch; active_total = total;
    last_update = std::chrono::steady_clock::now();
    ProgressRecord(1, test, branch, 0, total);
}
void NistUpdate(uint64_t processed)
{
    const auto now = std::chrono::steady_clock::now();
    if (now - last_update < std::chrono::seconds(1)) return;
    last_update = now;
    ProgressRecord(2, active_test, active_branch, processed, active_total);
}
void NistResult(unsigned test, double estimate)
{ ProgressRecord(3, test, active_branch, active_total, active_total, estimate); }

void AssessmentProgress::Record()
{
    Require(++records_ <= 4096 && pending_[3] == 0, "Invalid camera check progress");
    const unsigned kind = pending_[0], test = pending_[1], branch = pending_[2];
    const auto processed = ReadBE32(pending_.data() + 4), total = ReadBE32(pending_.data() + 8);
    const double estimate = std::bit_cast<double>(ReadBE64(pending_.data() + 12));
    if (kind == 0) {
        Require(!alphabet_ && !test && branch >= 1 && branch <= 3 && processed == ENTROPY_TEST_SAMPLES
            && total == 1000000 && estimate == -1, "Invalid camera check input");
        alphabet_ = branch; return;
    }
    Require(alphabet_ >= 2 && kind >= 1 && kind <= 3 && test >= 1 && test <= 10 && branch >= 1 && branch <= 2
        && (branch != 2 || alphabet_ == 3) && !(alphabet_ == 3 && branch == 1 && test >= 2 && test <= 4)
        && total == (branch == 1 ? ENTROPY_TEST_SAMPLES : 1000000) && processed <= total
        && std::isfinite(estimate), "Invalid camera check progress");
    Require(Group(test) >= Current(), "Camera checks moved backwards");
    if (kind == 3) {
        Require(processed == total && estimate >= -1 && (estimate == -1 || estimate >= 0)
            && estimate <= (branch == 2 || alphabet_ == 2 ? 1 : 8)
            && !results_[branch - 1][test - 1], "Invalid completed camera check");
        results_[branch - 1][test - 1] = estimate;
    } else Require(estimate == -1 && (kind != 1 || !processed), "Invalid camera check counter");
    // Tuple and LRS share one pass, including both branches.
    if (test == 6) test_ = 5;
    else test_ = test;
    branch_ = branch; processed_ = processed; total_ = total;
}
void AssessmentProgress::Feed(std::span<const unsigned char> bytes)
{
    while (!bytes.empty()) {
        const auto size = std::min(bytes.size(), pending_.size() - used_);
        std::copy_n(bytes.begin(), size, pending_.begin() + used_);
        used_ += size; bytes = bytes.subspan(size);
        if (used_ == pending_.size()) { Record(); used_ = 0; }
    }
}
void AssessmentProgress::Read(EntropyWorker& worker)
{
    std::array<unsigned char, 1024> bytes;
    while (const auto size = worker.ReadAvailable(bytes)) Feed(std::span(bytes).first(size));
}
void AssessmentProgress::Boundary() const { Require(!used_, "Incomplete camera check progress"); }
unsigned AssessmentProgress::Current() const { return Group(test_); }
unsigned AssessmentProgress::Finished() const
{
    unsigned finished = 0;
    for (unsigned group = 1; group <= 9; ++group) {
        bool complete = alphabet_ >= 2;
        const unsigned first = group <= 5 ? group : group + 1, last = group == 5 ? 6 : first;
        for (unsigned test = first; test <= last; ++test) {
            complete &= results_[alphabet_ == 3 ? 1 : 0][test - 1].has_value();
            if (alphabet_ == 3 && (test == 1 || test >= 5)) complete &= results_[0][test - 1].has_value();
        }
        finished += complete;
    }
    return finished;
}
std::string AssessmentProgress::Check() const { return test_ ? checks[Current() - 1] : "Waiting for camera data"; }
std::string AssessmentProgress::Pass() const
{
    // Suffix-array construction and compression do not expose a linear sample pass.
    if (!test_) return "";
    if (test_ == 4 || test_ == 5) return "This check: working...";
    return std::string(branch_ == 2 ? "Bits: " : "Values: ") + std::to_string(processed_) + "/" + std::to_string(total_)
        + " (" + std::to_string(uint64_t(processed_) * 100 / total_) + "%)";
}
std::vector<std::string> AssessmentProgress::Details() const
{
    std::vector<std::string> lines{"Completed checks only. Unfinished checks could lower the result.", "",
        "There is no final randomness estimate until all checks finish.", "",
        "Counts and percentages describe one pass, not the whole assessment.", ""};
    if (test_) {
        lines.push_back(std::string("Last check: ") + (test_ == 5 ? "T-Tuple / LRS" : methods[test_ - 1])
            + (branch_ == 2 ? " (bits)" : " (camera values)"));
        lines.emplace_back("");
    }
    bool any = false;
    for (unsigned test = 0; test < 10; ++test) for (unsigned branch = 0; branch < 2; ++branch) {
        if (!results_[branch][test]) continue;
        any = true;
        lines.push_back(std::string(methods[test]) + (branch ? " (bits): " : " (camera values): ")
            + (*results_[branch][test] < 0 ? "could not calculate an estimate"
                : std::to_string(*results_[branch][test]) + (branch ? " bits per bit" : " bits per value")));
    }
    if (!any) lines.emplace_back("No individual check finished.");
    return lines;
}
}
