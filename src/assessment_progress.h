#pragma once
#include "entropy_process.h"
#include <array>

namespace td {
// Fixed-size, bounded worker records; no estimator text, input bytes or hashes.
class AssessmentProgress {
    std::array<unsigned char, 20> pending_{};
    size_t used_{}, records_{};
    unsigned alphabet_{}, test_{}, branch_{};
    uint32_t processed_{}, total_{};
    std::array<std::array<std::optional<double>, 10>, 2> results_{};
    void Record();
public:
    void Feed(std::span<const unsigned char> bytes);
    void Read(EntropyWorker& worker);
    void Boundary() const;
    bool Constant() const { return alphabet_ == 1; }
    unsigned Current() const;
    unsigned Finished() const;
    std::string Check() const;
    std::string Pass() const;
    std::vector<std::string> Details() const;
};
}
