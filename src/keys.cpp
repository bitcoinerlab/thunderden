#include "keys.h"
#include "english.h"

#include <base58.h>
#include <crypto/common.h>
#include <crypto/hmac_sha256.h>
#include <crypto/hmac_sha512.h>
#include <crypto/sha256.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>

#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace td {
void Require(bool condition, const char* message)
{
    if (!condition) throw std::invalid_argument(message);
}

SecretBytes MnemonicSeed(std::span<const unsigned char> mnemonic,
                         std::span<const unsigned char> passphrase)
{
    Require(!mnemonic.empty() && mnemonic.size() <= 256, "Invalid mnemonic length");
    Require(passphrase.size() <= 128, "Passphrase exceeds 128 characters");
    Require(std::all_of(passphrase.begin(), passphrase.end(), [](auto c) {
        return c >= 0x20 && c <= 0x7e;
    }), "Passphrase must contain printable ASCII only");
    const std::string_view sentence(reinterpret_cast<const char*>(mnemonic.data()), mnemonic.size());
    SecretBytes bits(33), checksum(32);
    size_t count = 0, start = 0;
    while (start < sentence.size()) {
        const size_t end = sentence.find(' ', start);
        const auto word = sentence.substr(start, end == sentence.npos ? end : end - start);
        const auto found = std::lower_bound(ENGLISH.begin(), ENGLISH.end(), word);
        Require(found != ENGLISH.end() && *found == word, "Invalid English recovery word");
        Require(count < 24, "Too many recovery words");
        const auto index = static_cast<unsigned>(found - ENGLISH.begin());
        for (unsigned bit = 0; bit < 11; ++bit) {
            const size_t position = count * 11 + bit;
            bits[position / 8] |= ((index >> (10 - bit)) & 1U) << (7 - position % 8);
        }
        ++count;
        if (end == sentence.npos) break;
        start = end + 1;
        Require(start < sentence.size(), "Invalid mnemonic spacing");
    }
    Require(count >= 12 && count % 3 == 0, "Invalid recovery word count");
    const size_t entropy_bytes = count / 3 * 4;
    CSHA256().Write(bits.data(), entropy_bytes).Finalize(checksum.data());
    const unsigned mask = 0xffU << (8 - count / 3);
    Require((bits[entropy_bytes] & mask) == (checksum[0] & mask), "Invalid mnemonic checksum");

    constexpr std::string_view prefix = "mnemonic";
    SecretBytes salt(prefix.begin(), prefix.end()), seed(64);
    salt.insert(salt.end(), passphrase.begin(), passphrase.end());
    Require(PKCS5_PBKDF2_HMAC(sentence.data(), static_cast<int>(sentence.size()),
        salt.data(), static_cast<int>(salt.size()), 2048, EVP_sha512(), seed.size(), seed.data()) == 1,
        "Seed derivation failed");
    return seed;
}

Keys::Keys(std::span<const unsigned char> mnemonic, std::span<const unsigned char> passphrase)
    : registration_key_(32)
{
    const auto seed = MnemonicSeed(mnemonic, passphrase);
    root_.SetSeed(std::as_bytes(std::span{seed}));
    Require(root_.key.IsValid(), "Invalid master key");
    SecretBytes master(64), child(64);
    constexpr std::string_view label = "Symmetric key seed";
    CHMAC_SHA512(reinterpret_cast<const unsigned char*>(label.data()), label.size())
        .Write(seed.data(), seed.size()).Finalize(master.data());
    constexpr unsigned char application[] = "\0Thunder Den wallet policy";
    CHMAC_SHA512(master.data(), 32).Write(application, sizeof(application) - 1).Finalize(child.data());
    std::copy(child.begin() + 32, child.end(), registration_key_.begin());
}

Keys::~Keys()
{
    memory_cleanse(root_.chaincode.begin(), root_.chaincode.size());
}

CExtKey Keys::Derive(std::span<const uint32_t> path) const
{
    Require(path.size() <= 255, "Derivation path exceeds maximum depth");
    CExtKey key = root_;
    for (const auto index : path) {
        CExtKey child;
        Require(key.Derive(child, index), "Child derivation failed");
        memory_cleanse(key.chaincode.begin(), key.chaincode.size());
        key = child;
        memory_cleanse(child.chaincode.begin(), child.chaincode.size());
    }
    return key;
}

Fingerprint Keys::RootFingerprint() const
{
    const auto id = root_.key.GetPubKey().GetID();
    Fingerprint result;
    std::copy_n(id.begin(), result.size(), result.begin());
    return result;
}

std::string EncodePublic(const CExtPubKey& key, bool mainnet)
{
    std::array<unsigned char, 78> bytes;
    WriteBE32(bytes.data(), mainnet ? 0x0488B21E : 0x043587CF);
    key.Encode(bytes.data() + 4);
    return EncodeBase58Check(bytes);
}

Digest Keys::RegistrationTag(const Digest& wallet_id) const
{
    Digest result;
    CHMAC_SHA256(registration_key_.data(), registration_key_.size())
        .Write(wallet_id.data(), wallet_id.size()).Finalize(result.data());
    return result;
}

bool Keys::VerifyTag(const Digest& wallet_id, std::span<const unsigned char> tag) const
{
    if (tag.size() != 32) return false;
    const auto expected = RegistrationTag(wallet_id);
    return CRYPTO_memcmp(expected.data(), tag.data(), expected.size()) == 0;
}
}
