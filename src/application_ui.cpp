#include "application.h"
#include "terminal.h"

namespace td {
bool LoadWallet(Terminal& terminal, const Keys& keys, std::optional<ApprovedWallet>& loaded, const QRMessage& message)
{
    auto policy = ImportMultisig(message);
    const auto proof = ApproveWallet(policy, keys, [&](auto lines, const auto& details) {
        lines.insert(lines.begin(), {loaded ? "This will replace the loaded multisig wallet." : "Load this wallet for the current session.",
            "Compare the cosigner keys with their devices or your trusted wallet backup.",
            "Receiving addresses use /0/*; change uses /1/*.", ""});
        return terminal.Approve("Check this multisig wallet", lines, "REGISTER", details);
    });
    if (!proof) return false;
    loaded.emplace(ApprovedWallet{std::move(policy), *proof});
    return true;
}

bool ApproveTransaction(Terminal& terminal, const TransactionReview& review, std::optional<ReviewScreen>& completed)
{
    if (review.fee_unverified && !terminal.Confirm("Fee not fully verified", FeeWarning(), "continue to review", {}, true)) return false;
    completed = ReviewScreen{review.fee_unverified ? "Signed - fee not fully verified" : "Signed transaction - review",
        TransactionLines(review), TransactionDetails(review), review.fee_unverified};
    return terminal.Approve(review.fee_unverified ? "Review - fee not fully verified" : "Review transaction",
        completed->summary, "SIGN", completed->details, review.fee_unverified);
}

std::optional<QRMessage> SignRequest(Terminal& terminal, const Keys& keys, std::optional<ApprovedWallet>& loaded,
    const QRMessage& message, const std::function<QRMessage()>& scan,
    const std::function<bool(const TransactionReview&)>& approve)
{
    // Own this PSBT throughout any nested setup scan; never keep a span into a
    // scanner buffer which the second scan could replace.
    const auto bytes = UnwrapBytes(message.cbor);
    while (true) {
        auto options = FindSigningWallets(std::as_bytes(std::span(bytes)), keys, loaded ? &*loaded : nullptr);
        if (options.wallets.empty()) {
            if (options.matched_wallet) {
                terminal.Notice("Nothing more to sign", {"This request is already finalized, or your signatures are already present for the matched wallets."});
                return {};
            }
            if (!terminal.Confirm("Load a multisig wallet?", {"No matching wallet was found.",
                "For multisig, show the wallet setup QR in your wallet app's settings and scan it here.",
                "For a single-signature wallet, check the network, recovery words and the key paths supplied by your wallet app."},
                "scan wallet setup")) return {};
            if (!LoadWallet(terminal, keys, loaded, scan())) return {};
            continue;
        }
        ReviewLines labels;
        for (const auto& option : options.wallets) {
            const auto& review = option.transaction->Review();
            auto name = std::string("Loaded multisig");
            if (review.default_account) {
                name = AccountType(review.default_account->at(0) & 0x7fffffff);
                name = name.substr(0, name.find(" (")) + " account " + std::to_string(review.default_account->at(2) & 0x7fffffff);
            }
            // Fit the 40-column console too; the full identity is in the review.
            labels.push_back(name + " (" + std::to_string(option.inputs) + ")");
        }
        while (true) {
            const auto selected = options.wallets.size() == 1 ? 0 : terminal.Menu("Choose a wallet to sign with", labels,
                "Input counts are in parentheses. This pass signs for one wallet; existing signatures are kept.");
            const auto result = options.wallets[selected].transaction->Sign(keys, approve);
            if (result) return QRMessage{"crypto-psbt", CborBytes({
                reinterpret_cast<const uint8_t*>(result->psbt.data()), result->psbt.size()})};
            if (options.wallets.size() == 1) return {};
        }
    }
}
}
