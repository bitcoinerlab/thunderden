#include "assessment_progress.h"
#include "camera_entropy.h"
#include "nist_progress.h"
#include <crypto/common.h>
#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>
#include <fcntl.h>
#include <unistd.h>

namespace {
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<typename F> void Reject(F action) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid progress accepted");
}
std::array<unsigned char, 20> Record(unsigned kind, unsigned test, unsigned branch, uint32_t done, uint32_t total, double estimate = -1)
{
    std::array<unsigned char, 20> bytes{static_cast<unsigned char>(kind), static_cast<unsigned char>(test), static_cast<unsigned char>(branch)};
    WriteBE32(bytes.data() + 4, done); WriteBE32(bytes.data() + 8, total); WriteBE64(bytes.data() + 12, std::bit_cast<uint64_t>(estimate));
    return bytes;
}
}
int main()
{
    try {
        constexpr auto n = td::ENTROPY_TEST_SAMPLES;
        const auto input = Record(0, 0, 3, n, 1000000);
        td::AssessmentProgress progress;
        for (auto byte : input) progress.Feed({&byte, 1});
        progress.Boundary();
        progress.Feed(Record(3, 1, 2, 1000000, 1000000, 0.9));
        Check(progress.Finished() == 0, "One branch pretended to finish the check");
        progress.Feed(Record(3, 1, 1, n, n, 0));
        Check(progress.Finished() == 1, "Zero result was lost");
        for (unsigned test = 2; test <= 4; ++test) progress.Feed(Record(3, test, 2, 1000000, 1000000, 0.5));
        for (unsigned branch : {2, 1}) {
            const auto total = branch == 2 ? 1000000 : n;
            progress.Feed(Record(3, 5, branch, total, total, -1));
            progress.Feed(Record(3, 6, branch, total, total, 0.25));
        }
        Check(progress.Finished() == 5 && progress.Current() == 5, "Shared tuple/LRS pass counted twice");
        for (unsigned test = 7; test <= 8; ++test) for (unsigned branch : {2, 1}) {
            const auto total = branch == 2 ? 1000000 : n;
            progress.Feed(Record(3, test, branch, total, total, 0.5));
        }
        progress.Feed(Record(1, 9, 1, 0, n));
        progress.Feed(Record(2, 9, 1, 655360, n));
        Check(progress.Current() == 8 && progress.Finished() == 7 && progress.Pass() == "Values: 655360/1048576 (62%)", "Wrong current-pass counter");
        const auto details = progress.Details();
        Check(details.front().find("Unfinished checks could lower") != std::string::npos, "Partial results lost their warning");
        Reject([&] { progress.Feed(Record(1, 1, 1, 0, n)); });
        td::AssessmentProgress binary;
        binary.Feed(Record(0, 0, 2, n, 1000000)); binary.Feed(Record(3, 1, 1, n, n, 0.5));
        Check(binary.Finished() == 1, "Binary input required an unused branch");
        td::AssessmentProgress constant;
        constant.Feed(Record(0, 0, 1, n, 1000000)); Check(constant.Constant(), "Constant input not distinguished");
        for (const auto& bad : {Record(4, 1, 1, 0, n), Record(1, 0, 1, 0, n), Record(1, 11, 1, 0, n),
                Record(1, 1, 3, 0, n), Record(2, 1, 1, n + 1, n), Record(1, 1, 1, 0, n - 1),
                Record(3, 1, 2, 1000000, 1000000, 1.1), Record(3, 1, 1, n, n, -0.5),
                Record(3, 1, 1, n, n, std::numeric_limits<double>::infinity()),
                Record(3, 1, 1, n, n, std::numeric_limits<double>::quiet_NaN())}) {
            td::AssessmentProgress invalid; invalid.Feed(input); Reject([&] { invalid.Feed(bad); });
        }
        td::AssessmentProgress truncated; truncated.Feed(std::span(input).first(19)); Reject([&] { truncated.Boundary(); });
        td::AssessmentProgress flood; flood.Feed(input);
        Reject([&] { for (unsigned i = 0; i < 4096; ++i) flood.Feed(Record(2, 1, 1, 0, n)); });
        int pipefd[2];
        Check(pipe2(pipefd, O_NONBLOCK | O_CLOEXEC) == 0, "Cannot create backpressure fixture");
        const int reader = fcntl(pipefd[0], F_DUPFD_CLOEXEC, 6);
        Check(reader >= 0 && dup2(pipefd[1], 5) == 5, "Cannot keep the progress reader");
        const std::array<unsigned char, 4096> full{};
        while (write(5, full.data(), full.size()) > 0) {}
        // A full, unread progress pipe cannot stall input, stage or result hooks.
        td::NistInput(256, n, 1000000); td::NistStart(9, 1, n); td::NistResult(9, 0.5);
        close(5); close(pipefd[0]); close(pipefd[1]); close(reader);
        std::puts("PASS: bounded fragmented progress, shared stages, partial results, counts and malformed records");
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
