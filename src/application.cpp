#include "application.h"
#include "terminal.h"
#include "wallet_qr.h"
#include <util/strencodings.h>

namespace td {
std::optional<Digest> ApproveWallet(const Policy& policy, const Keys& keys, const WalletApproval& approve)
{
    Require(!policy.Name().empty() && !policy.OwnedKeys(keys).empty(), "This wallet does not contain a key from your recovery words");
    Require(bool(approve), "Missing local approval");
    const auto id = policy.ID();
    if (!approve(PolicyReview(policy, keys), {"Wallet ID: " + HexStr(id), "", "Full public descriptor:", policy.DescriptorText()})) return {};
    return keys.RegistrationTag(id);
}

bool LoadWallet(Terminal& terminal, const Keys& keys, std::optional<ApprovedWallet>& loaded, const QRMessage& message)
{
    return LoadWallet(terminal, keys, loaded, ImportMultisig(message), false);
}

bool LoadWallet(Terminal& terminal, const Keys& keys, std::optional<ApprovedWallet>& loaded, Policy policy, bool from_psbt)
{
    const auto proof = ApproveWallet(policy, keys, [&](auto lines, const auto& details) {
        lines.front() = "Check the spending rules and compare the cosigner keys with their devices or your trusted wallet backup.";
        lines.insert(lines.begin(), {from_psbt ? "The scanned transaction includes a multisig wallet setup. Review the setup first."
            : "You scanned a multisig wallet setup.", "",
            "Review and approve it so you can verify this wallet's addresses and sign transactions.",
            "You'll still review and approve each transaction before signing.", ""});
        auto setup_details = details;
        setup_details.insert(setup_details.begin(), {"Receiving addresses use /0/*; change uses /1/*.", ""});
        return terminal.Approve(loaded && loaded->policy.ID() != policy.ID() ? "Switch wallet setup" : "Review wallet setup",
            lines, "REGISTER", setup_details, false, "Approve only if this is the wallet you intended to use.");
    });
    if (!proof) return false;
    loaded.emplace(ApprovedWallet{std::move(policy), *proof});
    return true;
}

bool ApproveTransaction(Terminal& terminal, const TransactionReview& review, std::optional<ReviewScreen>& completed)
{
    const bool high_change = HasHighChangeIndex(review);
    const auto note = high_change ? "Wallet recovery may miss this change. Confirm only if you can recover it." : "";
    completed = ReviewScreen{high_change ? "Signed - high change index" : "Signed transaction - review",
        TransactionLines(review), TransactionDetails(review), high_change};
    auto lines = completed->summary;
    lines.insert(lines.begin(), {"Check the destinations, amounts and fee before signing.", ""});
    return terminal.Approve(high_change ? "Review - high change index" : "Review transaction",
        lines, "SIGN", completed->details, high_change, note);
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
        if (options.setup) {
            if (!LoadWallet(terminal, keys, loaded, std::move(*options.setup), true)) return {};
            continue;
        }
        if (options.wallets.empty()) {
            if (!options.needs_wallet) {
                terminal.Notice("Nothing more to sign", {"This request is already finalized, or your signatures are already present for the matched wallets."});
                return {};
            }
            if (!options.multisig) {
                terminal.Notice("No matching signing key", {"Thunder Den could not find a key from your recovery words that can sign this transaction.", "",
                    "Check the network, recovery words and passphrase. Your wallet app must also include the correct key paths."});
                return {};
            }
            if (!terminal.Confirm("Wallet setup needed", {"This transaction needs more wallet information before it can be reviewed.", "",
                "Show the wallet setup QR in your wallet app, then scan it here."},
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
                "This transaction uses more than one wallet. Choose which one to review. Matching input counts are in parentheses.");
            const auto result = options.wallets[selected].transaction->Sign(keys, approve);
            if (result) return QRMessage{"crypto-psbt", CborBytes({
                reinterpret_cast<const uint8_t*>(result->psbt.data()), result->psbt.size()})};
            if (options.wallets.size() == 1) return {};
        }
    }
}
}
