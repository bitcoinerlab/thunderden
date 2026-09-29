#pragma once

#include "policy.h"
#include "transport.h"

namespace td {
Policy DefaultPolicy(const Keys& keys, unsigned purpose, unsigned account);
QRMessage PublicDescriptor(const Policy& policy, const Keys& keys);
QRMessage PublicHDKey(const Keys& keys, const Path& path);
}
