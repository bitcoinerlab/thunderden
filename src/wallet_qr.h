#pragma once

#include "policy.h"
#include "transport.h"

namespace td {
QRMessage PublicHDKey(const Keys& keys, const Path& path);
QRMessage PublicAccount(const Keys& keys, const Path& path);
std::string PublicKeyText(const Keys& keys, const Path& path);
Policy ImportMultisig(const QRMessage& message);
}
