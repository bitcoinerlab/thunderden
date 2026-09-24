#pragma once

#include "keys.h"
#include <script/descriptor.h>

namespace td {
struct Position {
    unsigned branch;
    uint32_t index;
    auto operator<=>(const Position&) const = default;
};

struct KeyInfo {
    std::string text;
    CExtPubKey key;
    Fingerprint fingerprint{};
    Path origin;
    bool has_origin{false};
};

// Public policy data is immutable after construction. Core's network parameters
// must be selected before construction and remain fixed while policies are used.
class Policy {
    struct Reference { size_t key; uint32_t receive; uint32_t change; };
    std::string name_, text_, public_text_;
    std::vector<KeyInfo> keys_;
    std::vector<Reference> references_;
    std::vector<std::unique_ptr<Descriptor>> descriptors_;
    bool mainnet_;
public:
    Policy(std::string name, std::string text, std::vector<std::string> keys, bool mainnet);
    const std::string& Name() const { return name_; }
    const std::string& Template() const { return text_; }
    const std::vector<KeyInfo>& KeyInformation() const { return keys_; }
    Digest ID() const;
    std::vector<size_t> OwnedKeys(const Keys& session) const;
    bool IsDefault(const Keys& session) const;
    bool Authorized(const Keys& session, std::span<const unsigned char> tag) const;
    CScript Script(unsigned branch, uint32_t index) const;
    std::string DescriptorText(unsigned branch) const;
    std::string DescriptorText() const { return public_text_ + "#" + GetDescriptorChecksum(public_text_); }
    std::vector<Position> Positions(const KeyOriginInfo& hint) const;
    FlatSigningProvider PublicProvider(Position position) const;
    FlatSigningProvider PrivateProvider(Position position, const Keys& session) const;
};
}
