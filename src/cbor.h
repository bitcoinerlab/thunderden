#pragma once

#include "keys.h"
#include <cbor-lite.hpp>

namespace td {
// QR commands use only definite arrays, unsigned integers, bytes and printable text.
// No generic object tree, recursion, maps, floats or peer-sized allocations.
class CborReader {
    std::span<const uint8_t> data_;
    uint8_t Byte() {
        Require(!data_.empty(), "Truncated CBOR");
        const auto byte = data_.front(); data_ = data_.subspan(1); return byte;
    }
    uint64_t Head(unsigned major) {
        const auto head = Byte();
        Require(head >> 5 == major, "Wrong CBOR type");
        const unsigned info = head & 31;
        if (info < 24) return info;
        Require(info <= 27, "Indefinite or reserved CBOR");
        uint64_t value = 0;
        for (unsigned i = 0; i < (1U << (info - 24)); ++i) value = (value << 8) | Byte();
        const uint64_t minimum[]{24, 256, 65536, uint64_t{1} << 32};
        Require(value >= minimum[info - 24], "Non-canonical CBOR integer");
        return value;
    }
    std::span<const uint8_t> Data(unsigned major, size_t maximum) {
        const auto size = Head(major);
        Require(size <= maximum && size <= data_.size(), "CBOR length limit exceeded");
        const auto result = data_.first(size); data_ = data_.subspan(size); return result;
    }
public:
    explicit CborReader(std::span<const uint8_t> data) : data_(data) {}
    uint64_t UInt(uint64_t maximum = UINT32_MAX) {
        const auto value = Head(0); Require(value <= maximum, "Integer limit exceeded"); return value;
    }
    size_t Array(size_t maximum) {
        const auto size = Head(4); Require(size <= maximum, "Array limit exceeded"); return size;
    }
    void Tuple(size_t size) { Require(Array(size) == size, "Wrong array length"); }
    std::span<const uint8_t> Bytes(size_t maximum) { return Data(2, maximum); }
    std::string Text(size_t maximum) {
        const auto bytes = Data(3, maximum);
        for (const auto c : bytes) Require(c >= 32 && c <= 126, "Invalid text character");
        return {bytes.begin(), bytes.end()};
    }
    void End() const { Require(data_.empty(), "Trailing CBOR data"); }
};

struct CborWriter {
    std::vector<uint8_t> data;
    void UInt(uint64_t n) { CborLite::encodeUnsigned(data, n); }
    void Array(size_t n) { CborLite::encodeArraySize(data, n); }
    void Bytes(std::span<const uint8_t> bytes) { CborLite::encodeBytes(data, bytes); }
    void Text(const std::string& text) { CborLite::encodeText(data, text); }
};
}
