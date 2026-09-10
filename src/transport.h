#pragma once

#include "keys.h"
#include <fountain-decoder.hpp>
#include <ur-encoder.hpp>

#include <map>
#include <optional>

namespace td {
inline constexpr size_t MAX_UR_MESSAGE = 2 * 1024 * 1024 + 65536;
inline constexpr size_t MAX_UR_PARTS = 1024;
inline constexpr size_t MAX_QR_TEXT = 4296;

std::vector<uint8_t> CborBytes(std::span<const uint8_t> bytes);
std::vector<uint8_t> UnwrapBytes(const std::vector<uint8_t>& cbor);

struct QRMessage {
    std::string type;
    std::vector<uint8_t> cbor;
};

// One instance per scan. A conflicting stream never replaces collected data.
class URReceiver {
    struct Header {
        uint64_t parts, bytes, checksum, fragment;
        bool operator==(const Header&) const = default;
    };
    std::string type_;
    std::optional<Header> header_;
    std::map<uint32_t, Digest> seen_;
    ur::FountainDecoder decoder_;
    std::optional<QRMessage> result_;
public:
    // Invalid frames throw; identical repeats return false without spending work.
    bool Receive(std::string frame);
    double Progress() const;
    const std::optional<QRMessage>& Result() const { return result_; }
};

class URSender {
    ur::UREncoder encoder_;
public:
    URSender(const QRMessage& message, size_t fragment_bytes = 200);
    size_t Parts() const { return encoder_.seq_len(); }
    std::string Next();
};
}
