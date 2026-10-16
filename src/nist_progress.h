#pragma once
#include <cstdint>

// Thunder Den, 2026-10-16: progress-only instrumentation of NIST's pinned
// non-IID estimators. The official comparator and estimator mathematics stay unchanged.
namespace td {
void NistInput(unsigned alphabet, uint64_t values, uint64_t bits);
void NistStart(unsigned test, unsigned branch, uint64_t total);
void NistUpdate(uint64_t processed);
void NistResult(unsigned test, double estimate);

template<typename F> double NistEstimate(unsigned test, unsigned branch, uint64_t total, F calculate)
{
    NistStart(test, branch, total);
    const double result = calculate();
    NistResult(test, result);
    return result;
}
template<typename F> void NistSequences(unsigned branch, uint64_t total, double& tuple, double& lrs, F calculate)
{
    NistStart(5, branch, total);
    calculate();
    NistResult(5, tuple);
    NistResult(6, lrs);
}
}
