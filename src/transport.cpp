#include "transport.h"
#include "cbor.h"

#include <bytewords.hpp>
#include <crypto/sha256.h>

#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace td {
namespace {
uint32_t Number(std::string_view text)
{
    uint32_t result{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    Require(error == std::errc{} && end == text.data() + text.size() && result > 0
        && text.front() != '0', "Invalid UR sequence number");
    return result;
}

size_t FragmentSize(const QRMessage& message, size_t preferred)
{
    Require(!message.cbor.empty() && message.cbor.size() <= MAX_UR_MESSAGE, "UR message size limit exceeded");
    Require(message.type == "crypto-psbt" || message.type == "bytes"
        || message.type == "output-descriptor" || message.type == "hdkey" || message.type == "crypto-output",
        "Unsupported outgoing UR type");
    const auto size = std::max(preferred, (message.cbor.size() + MAX_UR_PARTS - 1) / MAX_UR_PARTS);
    Require(size >= 10 && size <= 2080, "UR fragment size limit exceeded");
    return size;
}
}

std::vector<uint8_t> CborBytes(std::span<const uint8_t> bytes)
{
    Require(bytes.size() <= MAX_UR_MESSAGE - 9, "UR message size limit exceeded");
    std::vector<uint8_t> result;
    CborLite::encodeBytes(result, bytes);
    return result;
}

std::vector<uint8_t> UnwrapBytes(const std::vector<uint8_t>& cbor)
{
    Require(cbor.size() <= MAX_UR_MESSAGE, "UR message size limit exceeded");
    CborReader in(cbor);
    const auto bytes = in.Bytes(MAX_UR_MESSAGE);
    in.End();
    return {bytes.begin(), bytes.end()};
}

bool URReceiver::Receive(std::string frame)
{
    Require(!result_, "Scan is already complete");
    Require(!frame.empty() && frame.size() <= MAX_QR_TEXT, "QR text size limit exceeded");
    for (char& c : frame) {
        Require((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
            || c == ':' || c == '/' || c == '-', "Invalid UR character");
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    }
    Require(frame.starts_with("ur:"), "Expected UR v2");
    const auto slash = frame.find('/', 3);
    Require(slash != frame.npos, "Missing UR body");
    const auto type = frame.substr(3, slash - 3);
    Require(type == "crypto-psbt" || type == "bytes" || type == "crypto-output", "Unsupported incoming UR type");
    const auto maximum = type == "crypto-output" ? MAX_WALLET_SETUP : MAX_UR_MESSAGE;
    const auto finish = [&](std::vector<uint8_t> payload) {
        Require(!payload.empty() && payload.size() <= maximum, "QR payload size limit exceeded");
        // Wallet CBOR is parsed by the signer, independently of this untrusted
        // scanner. It is a tagged script, not the byte-string used by PSBTs.
        if (type != "crypto-output") UnwrapBytes(payload);
        result_ = QRMessage{type, std::move(payload)};
    };
    Require(type_.empty() || type_ == type, "Conflicting UR type");
    const auto second = frame.find('/', slash + 1);
    const auto body = frame.substr((second == frame.npos ? slash : second) + 1);
    Require(!body.empty() && body.find_first_not_of("abcdefghijklmnopqrstuvwxyz") == body.npos,
        "Invalid Bytewords body");
    std::vector<uint8_t> cbor;
    try {
        cbor = ur::Bytewords::decode(ur::Bytewords::style::minimal, body);
    } catch (const std::exception&) {
        throw std::invalid_argument("Invalid Bytewords checksum or encoding");
    }
    if (second == frame.npos) {
        Require(!header_, "Single-part UR conflicts with multipart scan");
        finish(std::move(cbor));
        return true;
    }
    const auto sequence = std::string_view(frame).substr(slash + 1, second - slash - 1);
    const auto dash = sequence.find('-');
    Require(dash != sequence.npos, "Missing UR sequence count");
    const auto number = Number(sequence.substr(0, dash)), parts = Number(sequence.substr(dash + 1));
    Require(parts <= MAX_UR_PARTS, "Too many UR fragments");

    CborReader in(cbor);
    in.Tuple(5);
    Require(in.UInt() == number && in.UInt() == parts, "UR sequence/header mismatch");
    const auto length = in.UInt(maximum), checksum = in.UInt();
    const auto data = in.Bytes(MAX_QR_TEXT);
    in.End();
    Require(length > 0 && !data.empty(), "Invalid fountain fragment");
    Require(length <= parts * data.size() && (parts - 1) * data.size() < length,
        "Inconsistent fountain fragment geometry");
    const Header header{parts, length, checksum, data.size()};
    Require(!header_ || *header_ == header, "Conflicting UR stream");
    Digest digest;
    CSHA256().Write(cbor.data(), cbor.size()).Finalize(digest.data());
    if (const auto found = seen_.find(number); found != seen_.end()) {
        Require(found->second == digest, "Conflicting repeated UR fragment");
        return false;
    }
    Require(seen_.size() < 4 * parts + 64, "UR scan work limit exceeded; restart scan");
    type_ = type;
    header_ = header;
    seen_.emplace(number, digest);
    // Only validated fields reach the upstream fountain decoder.
    ur::FountainEncoder::Part part(number, parts, length, checksum, {data.begin(), data.end()});
    Require(decoder_.receive_part(part) && !decoder_.is_failure(), "Invalid fountain message");
    if (decoder_.is_success()) {
        finish(decoder_.result_message());
    }
    return true;
}

double URReceiver::Progress() const
{
    return result_ ? 1.0 : decoder_.estimated_percent_complete();
}

URSender::URSender(const QRMessage& message, size_t fragment_bytes)
    : encoder_(ur::UR(message.type, message.cbor), FragmentSize(message, fragment_bytes), 0, 1)
{
}

std::string URSender::Next()
{
    auto result = encoder_.next_part();
    Require(result.size() <= MAX_QR_TEXT, "Encoded QR exceeds capacity");
    for (auto& c : result) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    return result;
}
}
