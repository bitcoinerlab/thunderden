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
ReviewLines TransactionLines(const TransactionReview& review);
ReviewLines TransactionDetails(const TransactionReview& review);
ReviewLines FeeWarning();
}
