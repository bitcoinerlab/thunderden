#pragma once
#include "keys.h"

namespace td {
class Terminal;
SecretBytes NewMnemonic(Terminal& terminal);
// Keep the v1 source tags stable; the retired tag 2 is not accepted.
enum class ManualSource : unsigned char { None = 0, Coins = 1, Dice = 3 };
unsigned ManualTarget(ManualSource source, unsigned words);
SecretBytes ManualContribution(ManualSource source, std::span<const unsigned char> samples);
SecretBytes CameraContribution(std::span<const unsigned char> profile, uint64_t count, std::span<const unsigned char> digest);
SecretBytes MixEntropy(unsigned words, std::span<const unsigned char> camera, ManualSource source,
    std::span<const unsigned char> manual, std::span<const unsigned char> random);
// Production always obtains 32 fresh bytes using Core's strong, fail-stop RNG.
SecretBytes GenerateMnemonic(unsigned words, std::span<const unsigned char> camera, ManualSource source,
    std::span<const unsigned char> manual);
}
