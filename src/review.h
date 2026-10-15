#pragma once
#include "transaction.h"

namespace td {
using ReviewLines = std::vector<std::string>;
struct ReviewScreen {
    std::string title;
    ReviewLines summary;
    ReviewLines details;
    bool warning{false};
};
std::string Amount(CAmount value);
std::string NetworkName(ChainType network);
std::string AccountType(unsigned purpose);
std::string AddressPosition(Position position);
ReviewLines PublicKeyReview(const Keys& keys, const Path& path);
ReviewLines PolicyDetails(const Policy& policy, const Keys& keys);
ReviewLines PolicyReview(const Policy& policy, const Keys& keys);
ReviewLines AddressReview(const Policy& policy, const Keys& keys, Position position, const std::string& address);
// Disclosure heuristic, not a derivation limit or a guarantee of wallet discovery.
inline constexpr uint32_t CHANGE_INDEX_WARNING_THRESHOLD = 50000;
bool HasHighChangeIndex(const TransactionReview& review);
ReviewLines TransactionLines(const TransactionReview& review);
ReviewLines TransactionDetails(const TransactionReview& review);
}
