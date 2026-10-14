#include "entropy.h"
#include <crypto/common.h>
#include <random.h>
#include <algorithm>

namespace td {
namespace {
void Domain(EntropyHash& hash, std::string_view label)
{
    const unsigned char zero = 0;
    hash.Write(reinterpret_cast<const unsigned char*>(label.data()), label.size()).Write(&zero, 1);
}
void Field(EntropyHash& hash, unsigned char tag, std::span<const unsigned char> bytes)
{
    unsigned char length[8]; WriteBE64(length, bytes.size());
    hash.Write(&tag, 1).Write(length, 8).Write(bytes.data(), bytes.size());
}
SecretBytes Finish(EntropyHash& hash)
{
    SecretBytes result(32); hash.Finalize(result.data()); return result;
}
void Words(unsigned words) { Require(words == 12 || words == 24, "Choose 12 or 24 words"); }
}

unsigned ManualTarget(ManualSource source, unsigned words)
{
    Words(words);
    Require(source == ManualSource::Coins || source == ManualSource::Dice, "Unknown manual source");
    return (source == ManualSource::Coins ? 150 : 64) * (words / 12);
}

SecretBytes ManualContribution(ManualSource source, std::span<const unsigned char> samples)
{
    Require(!samples.empty() && samples.size() <= ManualTarget(source, 24), "Invalid manual contribution size");
    Require(std::all_of(samples.begin(), samples.end(), [&](auto c) { return c < (source == ManualSource::Dice ? 6 : 2); }),
        "Invalid toss or roll");
    const auto type = static_cast<unsigned char>(source);
    EntropyHash hash;
    Domain(hash, "ThunderDen/entropy/manual/v1");
    Field(hash, 1, {&type, 1}); Field(hash, 2, samples);
    return Finish(hash);
}

SecretBytes CameraContribution(std::span<const unsigned char> profile, uint64_t count, std::span<const unsigned char> digest)
{
    Require(!profile.empty() && profile.size() <= 64 && count > 0 && digest.size() == 32, "Invalid camera contribution");
    unsigned char length[8]; WriteBE64(length, count);
    EntropyHash hash;
    Domain(hash, "ThunderDen/entropy/camera/v1");
    Field(hash, 1, profile); Field(hash, 2, length); Field(hash, 3, digest);
    return Finish(hash);
}

SecretBytes MixEntropy(unsigned words, std::span<const unsigned char> camera, ManualSource source,
    std::span<const unsigned char> manual, std::span<const unsigned char> random)
{
    Words(words);
    Require(random.size() == 32 && (camera.empty() || camera.size() == 32)
        && (source == ManualSource::None || source == ManualSource::Coins || source == ManualSource::Dice)
        && (source == ManualSource::None ? manual.empty() : manual.size() == 32),
        "Invalid entropy contributions");
    SecretBytes camera_record{static_cast<unsigned char>(!camera.empty())}, manual_record{static_cast<unsigned char>(source)};
    camera_record.insert(camera_record.end(), camera.begin(), camera.end());
    manual_record.insert(manual_record.end(), manual.begin(), manual.end());
    const auto count = static_cast<unsigned char>(words);
    EntropyHash hash;
    Domain(hash, "ThunderDen/entropy/final/v1");
    Field(hash, 1, {&count, 1}); Field(hash, 2, camera_record); Field(hash, 3, manual_record); Field(hash, 4, random);
    auto result = Finish(hash);
    if (words == 12) { memory_cleanse(result.data() + 16, 16); result.resize(16); }
    return result;
}

SecretBytes GenerateMnemonic(unsigned words, std::span<const unsigned char> camera, ManualSource source,
    std::span<const unsigned char> manual)
{
    SecretBytes random(32);
    GetStrongRandBytes(random); // Core terminates the process if OS randomness fails.
    return EntropyMnemonic(MixEntropy(words, camera, source, manual, random));
}
}
