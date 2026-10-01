#pragma once

#include "policy.h"
#include "transport.h"

namespace td {
QRMessage PublicHDKey(const Keys& keys, const Path& path);
QRMessage PublicAccount(const Keys& keys, const Path& path);
std::string PublicKeyText(const Keys& keys, const Path& path);
bool IsMultisigSetup(const QRMessage& message);
Policy ImportMultisig(const QRMessage& message);
}
