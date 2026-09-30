#pragma once

#include "policy.h"
#include "review.h"
#include "transport.h"

namespace td {
Policy DefaultPolicy(const Keys& keys, unsigned purpose, unsigned account);
QRMessage PublicDescriptor(const Policy& policy, const Keys& keys);
QRMessage PublicHDKey(const Keys& keys, const Path& path);
std::string PublicKeyText(const Keys& keys, const Path& path);
Policy ImportMultisig(const QRMessage& message);
using WalletApproval = std::function<bool(const ReviewLines&, const ReviewLines&)>;
std::optional<Digest> ApproveWallet(const Policy& policy, const Keys& keys, const WalletApproval& approve);
struct ApprovedWallet {
    Policy policy;
    Digest proof;
};
struct SigningChoice {
    std::unique_ptr<ReviewedTransaction> transaction;
    size_t inputs;
};
struct SigningChoices {
    std::vector<SigningChoice> wallets;
    bool matched_wallet{false};
};
SigningChoices FindSigningWallets(std::span<const std::byte> psbt, const Keys& keys, const ApprovedWallet* loaded);
class Terminal;
bool LoadWallet(Terminal& terminal, const Keys& keys, std::optional<ApprovedWallet>& loaded, const QRMessage& message);
bool ApproveTransaction(Terminal& terminal, const TransactionReview& review, std::optional<ReviewScreen>& completed);
std::optional<QRMessage> SignRequest(Terminal& terminal, const Keys& keys, std::optional<ApprovedWallet>& loaded,
    const QRMessage& message, const std::function<QRMessage()>& scan,
    const std::function<bool(const TransactionReview&)>& approve);
}
