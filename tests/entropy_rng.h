#pragma once
#include <span>
#include <unistd.h>

// Linked only into test executables. No production option/environment hook.
bool mock_entropy_rng = false, fail_entropy_rng = false;
unsigned entropy_rng_calls = 0;
extern "C" void __real__Z18GetStrongRandBytesSt4spanIhLm18446744073709551615EE(std::span<unsigned char>) noexcept;
extern "C" void __wrap__Z18GetStrongRandBytesSt4spanIhLm18446744073709551615EE(std::span<unsigned char> bytes) noexcept
{
    if (!mock_entropy_rng) { __real__Z18GetStrongRandBytesSt4spanIhLm18446744073709551615EE(bytes); return; }
    ++entropy_rng_calls;
    if (fail_entropy_rng) _exit(91); // Model Core's unrecoverable RNG failure.
    for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = i;
}
