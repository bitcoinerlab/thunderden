// Public fixture assessment and real RNG smoke test on the image kernel/libraries.
#include "entropy.h"
#include "entropy_process.h"
#include "isolation.h"
#include <crypto/common.h>
#include <chrono>
#include <cstdio>
#include <thread>

int main()
{
    try {
        td::PrepareSigner();
        {
            td::EntropyFile input, report;
            std::vector<unsigned char> bytes(1048576);
            for (size_t i = 0; i < bytes.size(); i += 32) {
                unsigned char index[8]; WriteBE64(index, i);
                CSHA256().Write(index, 8).Finalize(bytes.data() + i);
            }
            for (auto& byte : bytes) byte &= 1; // Public binary fixture; not generation entropy.
            input.Append(bytes); input.Seal();
            td::EntropyWorker worker(td::EntropyExecutable(true), input.FD(), report.FD());
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(125);
            std::optional<int> status;
            while (!(status = worker.PollExit())) {
                td::Require(std::chrono::steady_clock::now() < deadline, "Guest assessment timed out");
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            report.Seal(); const auto result = td::ParseAssessment(report.Read(), *status);
            td::Require(!result.constant && result.bits_per_sample >= 0 && result.bits_per_sample <= 1, "Invalid guest assessment");
        }
        for (unsigned count : {12,24}) {
            const auto first = td::GenerateMnemonic(count, {}, td::ManualSource::None, {});
            const auto second = td::GenerateMnemonic(count, {}, td::ManualSource::None, {});
            td::ValidateMnemonic(first); td::ValidateMnemonic(second);
            td::Require(first != second, "Fresh generation repeated its output");
        }
        std::puts("PASS: isolated complete NIST assessment and fresh 12/24-word generation on guest runtime");
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
