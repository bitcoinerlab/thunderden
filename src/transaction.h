#pragma once

#include "policy.h"
#include <consensus/amount.h>
#include <psbt.h>
#include <util/chaintype.h>

#include <functional>
#include <optional>

namespace td {
struct InputReview {
    COutPoint previous;
    CAmount amount;
    uint32_t sequence;
    std::optional<Position> position;
    bool finalized;
    std::optional<int> signing_rule; // Present only for inputs this signer will process.
    bool full_previous{false};
};

struct OutputReview {
    CAmount amount;
    CScript script;
    std::string address; // Empty for scripts without a conventional address.
    std::optional<Position> position; // Branch 0 is receive/self-payment, 1 is change.
};

struct TransactionReview {
    Digest policy_id;
    std::string policy_name;
    std::string policy_template;
    std::string policy_descriptor;
    Fingerprint signer;
    std::optional<Path> default_account;
    ChainType network;
    uint32_t version;
    uint32_t locktime;
    CAmount fee{0};
    CAmount recognized_inputs{0};
    CAmount recognized_outputs{0};
    size_t unrecognized_inputs{0};
    bool fee_unverified{false};
    std::optional<int64_t> estimated_vsize; // Weight-based estimate, not relay policy.
    std::vector<InputReview> inputs;
    std::vector<OutputReview> outputs;
};

struct SigningResult {
    std::vector<std::byte> psbt;
    size_t added_signatures;
    bool complete; // Core can finalize and verify scripts; not chain/maturity proof.
};

struct PSBTData;
struct SigningChoices;
SigningChoices FindSigningWallets(std::span<const std::byte> raw, const Keys& keys, const ApprovedWallet* loaded);

class ReviewedTransaction {
    Policy policy_;
    Digest tag_{};
    std::shared_ptr<const PSBTData> request_;
    TransactionReview review_;
    ReviewedTransaction(std::shared_ptr<const PSBTData> request, Policy policy, const Keys& session,
        std::span<const unsigned char> tag);
    // Public-only completion checks used by automatic policy discovery.
    size_t PendingInputCount(const Keys& session) const;
    friend SigningChoices FindSigningWallets(std::span<const std::byte>, const Keys&, const ApprovedWallet*);
public:
    static constexpr size_t MAX_PSBT_BYTES = 1024 * 1024;
    ReviewedTransaction(Policy policy, const Keys& session,
        std::span<const unsigned char> tag, std::span<const std::byte> raw_psbt);
    ReviewedTransaction(const ReviewedTransaction&) = delete;
    ReviewedTransaction& operator=(const ReviewedTransaction&) = delete;
    ReviewedTransaction(ReviewedTransaction&&) = delete;
    ReviewedTransaction& operator=(ReviewedTransaction&&) = delete;
    const TransactionReview& Review() const { return review_; }
    // The callback must render the supplied review and obtain local user consent.
    // A declined review returns no result. Invalid requests throw before consent.
    std::optional<SigningResult> Sign(const Keys& session,
        const std::function<bool(const TransactionReview&)>& approve) const;
};

struct SigningChoice {
    std::unique_ptr<ReviewedTransaction> transaction;
    size_t inputs;
};
struct SigningChoices {
    std::vector<SigningChoice> wallets;
    bool needs_wallet{true};
};
}
