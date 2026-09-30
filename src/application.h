#pragma once

#include "review.h"
#include "transport.h"

namespace td {
using WalletApproval = std::function<bool(const ReviewLines&, const ReviewLines&)>;
std::optional<Digest> ApproveWallet(const Policy& policy, const Keys& keys, const WalletApproval& approve);
class Terminal;
bool LoadWallet(Terminal& terminal, const Keys& keys, std::optional<ApprovedWallet>& loaded, const QRMessage& message);
bool ApproveTransaction(Terminal& terminal, const TransactionReview& review, std::optional<ReviewScreen>& completed);
std::optional<QRMessage> SignRequest(Terminal& terminal, const Keys& keys, std::optional<ApprovedWallet>& loaded,
    const QRMessage& message, const std::function<QRMessage()>& scan,
    const std::function<bool(const TransactionReview&)>& approve);
}
