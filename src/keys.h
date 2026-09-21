#pragma once

#include <key.h>
#include <support/allocators/secure.h>

#include <array>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace td {
using SecretBytes = std::vector<unsigned char, secure_allocator<unsigned char>>;
using Digest = std::array<unsigned char, 32>;
using Fingerprint = std::array<unsigned char, 4>;
using Path = std::vector<uint32_t>;

inline void Require(bool condition, const char* message)
{
    if (!condition) throw std::invalid_argument(message);
}
unsigned MnemonicWordIndex(std::string_view word);
void ValidateMnemonic(std::span<const unsigned char> mnemonic);
SecretBytes MnemonicSeed(std::span<const unsigned char> mnemonic,
                         std::span<const unsigned char> passphrase);
std::string EncodePublic(const CExtPubKey& key, bool mainnet);

// The application owns one Core ECC_Context for the lifetime of its key sessions.
class Keys {
    CExtKey root_;
    SecretBytes registration_key_;
public:
    Keys(std::span<const unsigned char> mnemonic, std::span<const unsigned char> passphrase);
    ~Keys();
    Keys(const Keys&) = delete;
    Keys& operator=(const Keys&) = delete;
    CExtKey Derive(std::span<const uint32_t> path) const;
    Fingerprint RootFingerprint() const;
    Digest RegistrationTag(const Digest& wallet_id) const;
    bool VerifyTag(const Digest& wallet_id, std::span<const unsigned char> tag) const;
};
}
