#include "seed_qr.h"
#include "english.h"
#include <algorithm>

namespace td {
bool IsSeedQR(const QRMessage& message)
{
    return message.type == "seedqr" || message.type == "compact-seedqr";
}

SecretBytes DecodeSeedQR(const QRMessage& message)
{
    Require(IsSeedQR(message) && message.cbor.empty(), "This isn't a supported seed QR.");
    const auto& data = message.secret;
    if (message.type == "compact-seedqr") {
        Require(data.size() == 16 || data.size() == 32, "Invalid CompactSeedQR length.");
        return EntropyMnemonic(data);
    }
    Require((data.size() == 48 || data.size() == 96)
        && std::all_of(data.begin(), data.end(), [](auto c) { return c >= '0' && c <= '9'; }), "Invalid SeedQR data.");
    SecretBytes words;
    words.reserve(256);
    for (size_t i = 0; i < data.size(); i += 4) {
        unsigned index = 0;
        for (size_t j = 0; j < 4; ++j) index = index * 10 + data[i + j] - '0';
        Require(index < ENGLISH.size(), "Invalid SeedQR word index.");
        if (i) words.push_back(' ');
        words.insert(words.end(), ENGLISH[index].begin(), ENGLISH[index].end());
    }
    ValidateMnemonic(words);
    return words;
}

SecretBytes EncodeSeedQR(std::span<const unsigned char> mnemonic, SeedQRFormat format)
{
    ValidateMnemonic(mnemonic);
    const std::string_view words(reinterpret_cast<const char*>(mnemonic.data()), mnemonic.size());
    const size_t count = 1 + std::count(words.begin(), words.end(), ' ');
    Require(count == 12 || count == 24, "Seed QR backups require 12 or 24 recovery words.");
    SecretBytes data(format == SeedQRFormat::Compact ? count * 4 / 3 : count * 4);
    size_t start = 0;
    for (size_t i = 0; i < count; ++i) {
        const auto end = words.find(' ', start);
        const auto index = MnemonicWordIndex(words.substr(start, end == words.npos ? end : end - start));
        if (format == SeedQRFormat::Standard) {
            unsigned number = index;
            for (size_t j = 4; j > 0; --j) { data[i * 4 + j - 1] = '0' + number % 10; number /= 10; }
        } else {
            for (size_t bit = 0; bit < 11 && i * 11 + bit < data.size() * 8; ++bit) {
                const size_t position = i * 11 + bit;
                data[position / 8] |= ((index >> (10 - bit)) & 1U) << (7 - position % 8);
            }
        }
        start = end + 1;
    }
    return data;
}
}
