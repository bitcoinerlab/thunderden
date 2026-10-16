#pragma once
#include "transport.h"

namespace td {
enum class SeedQRFormat { Standard, Compact };
bool IsSeedQR(const QRMessage& message);
SecretBytes DecodeSeedQR(const QRMessage& message);
SecretBytes EncodeSeedQR(std::span<const unsigned char> mnemonic, SeedQRFormat format);
}
