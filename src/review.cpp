#include "review.h"

#include <chainparams.h>
#include <key_io.h>
#include <script/solver.h>
#include <util/moneystr.h>
#include <util/strencodings.h>

#include <algorithm>

namespace td {
std::string Amount(CAmount value)
{
    return FormatMoney(value) + " BTC (" + std::to_string(value) + " sats)";
}

std::string PathText(const Path& path)
{
    std::string text = "m";
    for (const auto index : path) text += "/" + std::to_string(index & 0x7fffffffU) + (index & 0x80000000U ? "h" : "");
    return text;
}

std::string NetworkName(ChainType network)
{
    if (network == ChainType::MAIN) return "Bitcoin mainnet";
    if (network == ChainType::TESTNET) return "Testnet3";
    if (network == ChainType::TESTNET4) return "Testnet4";
    if (network == ChainType::SIGNET) return "Signet";
    return "Regtest";
}

ReviewLines PublicKeyReview(const Keys& keys, const Path& path)
{
    return {
        "An extended public key (xpub) helps your wallet app find addresses for this account and show their balances and transactions.",
        "", "It contains no private keys and cannot spend your bitcoin on its own.",
        "", "Share it only with a wallet app you trust. Anyone who has it may be able to follow this account's activity.",
        "", "Network: " + NetworkName(Params().GetChainType()),
        "Master fingerprint: " + HexStr(keys.RootFingerprint()), "Path: " + PathText(path),
    };
}

std::string AccountType(unsigned purpose)
{
    switch (purpose) {
    case 44: return "Legacy (BIP44)";
    case 49: return "Wrapped SegWit (BIP49)";
    case 84: return "Native SegWit (BIP84)";
    case 86: return "Taproot (BIP86)";
    default: throw std::invalid_argument("Unsupported account type");
    }
}

std::string AddressPosition(Position position)
{
    return std::string(position.branch ? "Change address" : "Receiving address")
        + " (index " + std::to_string(position.index) + ")";
}

ReviewLines PolicyReview(const Policy& policy, const Keys& keys)
{
    const auto owned = policy.OwnedKeys(keys);
    const bool standard = policy.IsDefault(keys);
    CTxDestination destination;
    Require(ExtractDestination(policy.Script(0, 0), destination), "Policy has no displayable receive address");
    ReviewLines lines{
        "Check the spending rules and public keys for this wallet. They must match the wallet you intended to set up.", "",
        "Network: " + NetworkName(Params().GetChainType()),
        standard ? "Account: " + std::to_string(policy.KeyInformation()[0].origin[2] & 0x7fffffff) : "Wallet: " + policy.Name(),
        standard ? "Address type: " + AccountType(policy.KeyInformation()[0].origin[0] & 0x7fffffff) : "",
        "Master fingerprint: " + HexStr(keys.RootFingerprint()),
        "Wallet ID: " + HexStr(policy.ID()),
        "", "Spending rules (exact policy):", policy.Template(),
    };
    for (size_t i = 0; i < policy.KeyInformation().size(); ++i) {
        lines.push_back("Key " + std::to_string(i) + (std::find(owned.begin(), owned.end(), i) != owned.end()
            ? " - verified local key:" : " - external key:"));
        lines.push_back(policy.KeyInformation()[i].text);
    }
    lines.push_back("First receiving address (index 0):");
    lines.push_back(EncodeDestination(destination));
    return lines;
}

ReviewLines TransactionLines(const TransactionReview& review)
{
    ReviewLines lines{
        "Your wallet app is asking you to sign a transaction. Check each destination, amount and the fee before continuing.", "",
        "Network: " + NetworkName(review.network),
        review.default_account ? "Account: " + std::to_string(review.default_account->at(2) & 0x7fffffff) : "Wallet: " + review.policy_name,
    };
    lines.push_back("Transaction fee: " + Amount(review.fee));
    if (review.estimated_vsize) {
        const auto rate = review.fee * 100 / *review.estimated_vsize;
        lines.push_back("Estimated size: " + std::to_string(*review.estimated_vsize) + " vB");
        lines.push_back("Approximate fee rate: " + std::to_string(rate / 100) + "."
            + (rate % 100 < 10 ? "0" : "") + std::to_string(rate % 100) + " sat/vB");
    } else {
        lines.push_back("The fee rate cannot be estimated yet because some inputs or spending conditions are incomplete.");
    }
    lines.push_back("Recognized wallet inputs: " + Amount(review.recognized_inputs));
    lines.push_back("Recognized wallet outputs: " + Amount(review.recognized_outputs));
    lines.push_back("Net amount leaving this wallet: " + Amount(review.recognized_inputs - review.recognized_outputs));
    lines.push_back("Unrecognized inputs: " + std::to_string(review.unrecognized_inputs));
    for (size_t i = 0; i < review.outputs.size(); ++i) {
        const auto& output = review.outputs[i];
        const std::string role = !output.position ? "EXTERNAL DESTINATION" :
            output.position->branch == 1 ? "VERIFIED CHANGE" : "VERIFIED RECEIVE / SELF-PAYMENT";
        lines.push_back("--- Output " + std::to_string(i + 1) + " of " + std::to_string(review.outputs.size()) + " ---");
        lines.push_back(role);
        lines.push_back(!output.position ? "This destination has not been verified as belonging to this wallet. Check it carefully."
            : output.position->branch == 1 ? "This returns bitcoin to a verified change address in this wallet."
            : "This sends bitcoin to a verified receiving address in this wallet.");
        lines.push_back("Amount: " + Amount(output.amount));
        lines.push_back(output.address.empty() ? "Raw script (hex):" : "Destination:");
        lines.push_back(output.address.empty() ? HexStr(output.script) : output.address);
        if (output.position) lines.push_back(AddressPosition(*output.position));
    }
    lines.push_back("");
    lines.push_back("--- Wallet and transaction details ---");
    lines.push_back("Master fingerprint: " + HexStr(review.signer));
    lines.push_back("Wallet ID: " + HexStr(review.policy_id));
    lines.push_back("Policy: " + review.policy_template);
    if (review.default_account) lines.push_back("Account path: " + PathText(*review.default_account));
    lines.push_back("Transaction version: " + std::to_string(review.version));
    lines.push_back("Locktime: " + std::to_string(review.locktime)
        + (review.locktime < 500000000 ? " (block height)" : " (Unix time)"));
    const bool all_final = std::all_of(review.inputs.begin(), review.inputs.end(), [](const auto& input) { return input.sequence == 0xffffffff; });
    if (all_final) lines.push_back("All sequences are final: locktime is inactive.");
    const bool rbf = std::any_of(review.inputs.begin(), review.inputs.end(), [](const auto& input) { return input.sequence < 0xfffffffe; });
    lines.push_back(std::string("Signals opt-in replacement: ") + (rbf ? "yes" : "no"));
    lines.push_back("This offline device cannot check the current blockchain or whether a waiting period has ended. Your wallet app must check before broadcasting.");
    for (size_t i = 0; i < review.inputs.size(); ++i) {
        const auto& input = review.inputs[i];
        lines.push_back("--- Input " + std::to_string(i + 1) + " of " + std::to_string(review.inputs.size()) + " ---");
        lines.push_back(input.previous.hash.ToString() + ":" + std::to_string(input.previous.n));
        lines.push_back("Amount: " + Amount(input.amount));
        lines.push_back("Sequence: " + std::to_string(input.sequence));
        lines.push_back(input.finalized ? "Already finalized and verified" : input.position ? "Verified policy input" : "Unrecognized input - not signed");
        if (input.position) lines.push_back(AddressPosition(*input.position));
        if (input.signing_rule) lines.push_back(*input.signing_rule == SIGHASH_DEFAULT ? "Signing rule: DEFAULT" : "Signing rule: ALL");
    }
    lines.push_back("");
    lines.push_back("Signing returns your approval to the wallet app. It does not broadcast the transaction or confirm that payment has been made.");
    return lines;
}
}
