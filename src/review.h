#pragma once
#include "transaction.h"

namespace td {
using ReviewLines = std::vector<std::string>;
std::string Amount(CAmount value);
std::string PathText(const Path& path);
ReviewLines PolicyReview(const Policy& policy, const Keys& keys);
ReviewLines TransactionLines(const TransactionReview& review);
}
