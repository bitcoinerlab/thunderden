#include "entropy.h"
#include <util/strencodings.h>
#include <cstdio>
#include <numeric>

namespace {
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<typename F> void Reject(F action) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid entropy input accepted");
}
std::string Text(const td::SecretBytes& bytes) { return {bytes.begin(), bytes.end()}; }
}
int main()
{
    try {
        using td::ManualSource;
        const auto zero12 = td::EntropyMnemonic(td::SecretBytes(16));
        Check(Text(zero12) == "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about", "BIP39 128-bit vector");
        const auto zero24 = td::EntropyMnemonic(td::SecretBytes(32));
        Check(Text(zero24) == std::string("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon ")
            + "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon art", "BIP39 256-bit vector");
        Check(Text(td::EntropyMnemonic(td::SecretBytes(16, 255))) == "zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo wrong", "BIP39 all-ones vector");
        for (unsigned size : {0, 15, 17, 20, 31, 33}) Reject([&] { td::EntropyMnemonic(td::SecretBytes(size)); });
        td::SecretBytes random(32); std::iota(random.begin(), random.end(), 0);
        Check(HexStr(td::MixEntropy(12, {}, ManualSource::None, {}, random)) == "3516466a8e1c9b59d942448a9de9b95b", "Independent 12-word mixing vector");
        Check(HexStr(td::MixEntropy(24, {}, ManualSource::None, {}, random)) == "d7faee9d2c34fa852449bfc28948df7c84fdb761031b2e7d4e879079b4e7ce8b", "Independent 24-word mixing vector");
        const td::SecretBytes pairs{0,0,1,1,0,1,1,0,1};
        Check(td::ManualSamples(ManualSource::CoinPairs, pairs) == td::SecretBytes({0,1}), "Von Neumann pairs and incomplete pair");
        Check(td::ManualSamples(ManualSource::Coins, pairs) == pairs, "Repeated coin results removed");
        const td::SecretBytes dice{0,1,2,3,4,5};
        Check(td::ManualSamples(ManualSource::Dice, dice) == dice, "Dice encoding changed");
        Reject([&] { td::ManualSamples(ManualSource::Coins, dice); });
        Reject([&] { td::ManualSamples(ManualSource::Dice, td::SecretBytes{6}); });
        Reject([&] { td::ManualSamples(ManualSource::Coins, td::SecretBytes(td::MAX_MANUAL_TOSSES + 1)); });
        for (unsigned words : {12,24}) {
            Check(td::ManualTarget(ManualSource::Coins, words) == 150 * words / 12, "Coin target");
            Check(td::ManualTarget(ManualSource::CoinPairs, words) == 128 * words / 12, "Pair target");
            Check(td::ManualTarget(ManualSource::Dice, words) == 64 * words / 12, "Dice target");
            for (auto source : {ManualSource::Coins, ManualSource::CoinPairs, ManualSource::Dice}) {
                const auto contribution = td::ManualContribution(source, td::SecretBytes{0,1});
                for (auto camera : {td::SecretBytes{}, td::SecretBytes(32)}) {
                    const auto entropy = td::MixEntropy(words, camera, source, contribution, random);
                    td::ValidateMnemonic(td::EntropyMnemonic(entropy));
                    Check(entropy != td::MixEntropy(words, camera, ManualSource::None, {}, random), "Manual source not included");
                }
            }
            Check(td::MixEntropy(words, {}, ManualSource::None, {}, random)
                != td::MixEntropy(words, td::SecretBytes(32), ManualSource::None, {}, random), "Absent camera confused with zero digest");
        }
        Reject([&] { td::MixEntropy(15, {}, ManualSource::None, {}, random); });
        Reject([&] { td::MixEntropy(12, {}, ManualSource::Coins, {}, random); });
        Reject([&] { td::MixEntropy(12, {}, ManualSource::None, {}, {}); });
        std::puts("PASS: BIP39 reference vectors, versioned mixing, source presence, manual alphabets and pair extraction");
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
