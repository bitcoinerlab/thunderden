#pragma once
#include "application.h"
#include <script/solver.h>

namespace td {
inline constexpr uint32_t ADDRESS_SEARCH_WINDOW = 1000, ADDRESS_INDEX_END = 0x80000000U;
CTxDestination ReadAddress(std::span<const uint8_t> text);
// Zero means a wallet setup is required; otherwise a standard BIP purpose.
unsigned AddressPurpose(const CTxDestination& destination);
// next remains the first incompletely checked index on cancellation, or the match.
std::optional<Position> FindAddress(const Policy& policy, const CScript& script, uint32_t& next,
    uint32_t end, const std::function<bool(uint32_t)>& proceed);
void CheckAddress(Terminal& terminal, const Keys& keys, const ApprovedWallet* loaded, std::span<const uint8_t> text);
}
