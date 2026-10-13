#pragma once
#include "keys.h"
#include <crypto/sha256.h>

namespace td {
// Secret hash state is erased, including on cancellation and exceptions.
struct EntropyHash : CSHA256 {
    ~EntropyHash() { memory_cleanse(this, sizeof(*this)); }
};
enum class ManualSource : unsigned char { None, Coins, CoinPairs, Dice };
inline constexpr size_t MAX_MANUAL_TOSSES = 16384;
unsigned ManualTarget(ManualSource source, unsigned words);
SecretBytes ManualSamples(ManualSource source, std::span<const unsigned char> raw);
SecretBytes ManualContribution(ManualSource source, std::span<const unsigned char> samples);
SecretBytes CameraContribution(std::span<const unsigned char> profile, uint64_t count, std::span<const unsigned char> digest);
SecretBytes MixEntropy(unsigned words, std::span<const unsigned char> camera, ManualSource source,
    std::span<const unsigned char> manual, std::span<const unsigned char> random);
// Production always obtains 32 fresh bytes using Core's strong, fail-stop RNG.
SecretBytes GenerateMnemonic(unsigned words, std::span<const unsigned char> camera, ManualSource source,
    std::span<const unsigned char> manual);
}
