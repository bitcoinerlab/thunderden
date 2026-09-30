#include "review.h"

#include <chainparams.h>
#include <key_io.h>
#include <script/solver.h>
#include <util/moneystr.h>
#include <util/strencodings.h>
#include <util/time.h>

#include <algorithm>

namespace td {
std::string Amount(CAmount value)
{
    return FormatMoney(value) + " BTC (" + std::to_string(value) + " sats)";
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

namespace {
ReviewLines PolicyRules(const Policy& policy, const Keys& keys)
{
    const auto owned = policy.OwnedKeys(keys);
    CTxDestination destination;
    Require(ExtractDestination(policy.Script(0, 0), destination), "Policy has no displayable receive address");
    ReviewLines lines{"Spending rules (exact policy):", policy.Template()};
    for (size_t i = 0; i < policy.KeyInformation().size(); ++i) {
        lines.push_back("Key " + std::to_string(i) + (std::find(owned.begin(), owned.end(), i) != owned.end()
            ? " - verified local key:" : " - external key:"));
        lines.push_back(policy.KeyInformation()[i].text);
    }
    lines.push_back("First receiving address (index 0):");
    lines.push_back(EncodeDestination(destination));
    return lines;
}
}

ReviewLines PolicyDetails(const Policy& policy, const Keys& keys)
{
    auto lines = PolicyRules(policy, keys);
    lines.insert(lines.begin(), {"Wallet ID: " + HexStr(policy.ID()), ""});
    lines.insert(lines.end(), {"", "Full public descriptor:", policy.DescriptorText()});
    return lines;
}

ReviewLines PolicyReview(const Policy& policy, const Keys& keys)
{
    const bool standard = policy.IsDefault(keys);
    ReviewLines lines{
        "Check the spending rules and public keys for this wallet. They must match the wallet you intended to set up.", "",
        "Network: " + NetworkName(Params().GetChainType()),
        standard ? "Account: " + std::to_string(policy.KeyInformation()[0].origin[2] & 0x7fffffff) : "Wallet: " + policy.Name(),
        standard ? "Address type: " + AccountType(policy.KeyInformation()[0].origin[0] & 0x7fffffff) : "",
        "Master fingerprint: " + HexStr(keys.RootFingerprint()),
    };
    const auto rules = PolicyRules(policy, keys);
    lines.insert(lines.end(), rules.begin(), rules.end());
    return lines;
}

ReviewLines TransactionLines(const TransactionReview& review)
{
    ReviewLines lines{
        "Network: " + NetworkName(review.network),
        review.default_account ? "Account: " + std::to_string(review.default_account->at(2) & 0x7fffffff) : "Wallet: " + review.policy_name,
    };
    if (review.default_account) lines.push_back("Address type: " + AccountType(review.default_account->at(0) & 0x7fffffff));
    CAmount change = 0;
    size_t change_count = 0, destination = 0;
    const auto destinations = std::count_if(review.outputs.begin(), review.outputs.end(), [](const auto& output) {
        return !output.position || output.position->branch != 1;
    });
    for (const auto& output : review.outputs) {
        if (output.position && output.position->branch == 1) {
            change += output.amount;
            ++change_count;
            continue;
        }
        lines.push_back("");
        lines.push_back("Destination " + std::to_string(++destination) + " of " + std::to_string(destinations)
            + (output.position ? " (this wallet's receiving address)" : ""));
        lines.push_back("Amount: " + Amount(output.amount));
        if (output.address.empty()) lines.push_back("No conventional address. Script (hex):");
        lines.push_back(output.address.empty() ? HexStr(output.script) : output.address);
    }
    lines.push_back("");
    if (!destinations) lines.push_back("All outputs belong to this wallet.");
    lines.push_back(std::string(review.fee_unverified ? "Unverified fee: " : "Fee: ") + Amount(review.fee));
    if (review.estimated_vsize) {
        const auto rate = review.fee * 100 / *review.estimated_vsize;
        lines.push_back(std::string(review.fee_unverified ? "Unverified fee rate: " : "Estimated fee rate: ") + std::to_string(rate / 100) + "."
            + (rate % 100 < 10 ? "0" : "") + std::to_string(rate % 100) + " sat/vB");
    } else {
        lines.push_back(review.fee_unverified ? "Fee rate unavailable; input amounts are not fully verified." : "Fee rate unavailable; fee above is exact.");
    }
    if (!review.unrecognized_inputs) {
        lines.push_back(std::string(review.fee_unverified ? "Wallet decrease (unverified amounts): " : "Wallet decrease: ")
            + Amount(review.recognized_inputs - review.recognized_outputs));
    } else {
        lines.push_back("Inputs not verified as this wallet: " + std::to_string(review.unrecognized_inputs));
        lines.push_back(std::string(review.fee_unverified ? "Wallet inputs (unverified amounts): " : "Verified wallet inputs: ") + Amount(review.recognized_inputs));
        lines.push_back("Verified wallet outputs: " + Amount(review.recognized_outputs));
        lines.push_back(review.fee_unverified ? "Wallet ownership is checked; input amounts are not fully verified."
            : "These wallet totals cover verified inputs/outputs only.");
    }
    if (change_count) lines.push_back("Verified change: " + Amount(change) + " (" + std::to_string(change_count)
        + (change_count == 1 ? " output)" : " outputs)"));
    if (review.fee_unverified) lines.push_back("The actual fee may be higher than shown. Continuing does not verify these amounts.");
    else if (std::any_of(review.inputs.begin(), review.inputs.end(), [](const auto& input) { return !input.full_previous; }))
        lines.push_back("Compact input data: incorrect amounts would make the signatures added here invalid.");
    const bool all_final = std::all_of(review.inputs.begin(), review.inputs.end(), [](const auto& input) { return input.sequence == 0xffffffff; });
    bool timed = review.locktime && !all_final;
    if (timed) lines.push_back("Transaction locktime: " + (review.locktime < 500000000
        ? "after block " + std::to_string(review.locktime) : "after " + FormatISO8601DateTime(review.locktime)));
    for (size_t i = 0; i < review.inputs.size(); ++i) {
        const auto sequence = review.inputs[i].sequence;
        const auto delay = sequence & CTxIn::SEQUENCE_LOCKTIME_MASK;
        if (int32_t(review.version) < 2 || (sequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG) || !delay) continue;
        timed = true;
        lines.push_back("Input " + std::to_string(i + 1) + " relative delay: "
            + (sequence & CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG
                ? std::to_string(delay << CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) + " seconds" : std::to_string(delay) + " blocks"));
    }
    if (timed) lines.push_back("Timing depends on chain state; this device cannot check maturity.");
    return lines;
}

ReviewLines TransactionDetails(const TransactionReview& review)
{
    ReviewLines lines{
        "Wallet ID: " + HexStr(review.policy_id),
        "Master fingerprint: " + HexStr(review.signer),
        "", "Spending rules (exact policy):", review.policy_template,
        "", "Full public descriptor:", review.policy_descriptor, "",
        "Recognized wallet inputs: " + Amount(review.recognized_inputs),
        "Recognized wallet outputs: " + Amount(review.recognized_outputs),
        "Unrecognized inputs: " + std::to_string(review.unrecognized_inputs),
    };
    if (review.default_account) lines.push_back("Account path: " + PathText(*review.default_account));
    if (review.fee_unverified) lines.push_back("Input totals and fee use amounts that are not fully verified.");
    if (review.estimated_vsize) lines.push_back("Estimated size: " + std::to_string(*review.estimated_vsize) + " vB");
    lines.push_back("Transaction version: " + std::to_string(review.version));
    lines.push_back("Locktime: " + std::to_string(review.locktime)
        + (review.locktime < 500000000 ? " (block height)" : " (Unix time)"));
    const bool all_final = std::all_of(review.inputs.begin(), review.inputs.end(), [](const auto& input) { return input.sequence == 0xffffffff; });
    if (all_final) lines.push_back("All sequences are final: locktime is inactive.");
    const bool rbf = std::any_of(review.inputs.begin(), review.inputs.end(), [](const auto& input) { return input.sequence < 0xfffffffe; });
    lines.push_back(std::string("Signals opt-in replacement: ") + (rbf ? "yes" : "no"));
    for (size_t i = 0; i < review.inputs.size(); ++i) {
        const auto& input = review.inputs[i];
        lines.push_back("--- Input " + std::to_string(i + 1) + " of " + std::to_string(review.inputs.size()) + " ---");
        lines.push_back(input.previous.hash.ToString() + ":" + std::to_string(input.previous.n));
        lines.push_back("Amount: " + Amount(input.amount));
        lines.push_back(input.full_previous ? "Amount checked against the previous transaction"
            : "Amount supplied by the wallet app; previous transaction not included");
        lines.push_back("Sequence: " + std::to_string(input.sequence));
        lines.push_back(input.finalized ? "Finalized signatures verified against the supplied output data"
            : input.position ? "Verified policy script" : "Unrecognized input - not signed");
        if (input.position) lines.push_back(AddressPosition(*input.position));
        if (input.signing_rule) lines.push_back(*input.signing_rule == SIGHASH_DEFAULT ? "Signing rule: DEFAULT" : "Signing rule: ALL");
    }
    for (size_t i = 0; i < review.outputs.size(); ++i) {
        const auto& output = review.outputs[i];
        if (!output.position || output.position->branch != 1) continue;
        lines.push_back("--- Verified change output " + std::to_string(i + 1) + " ---");
        lines.push_back("Amount: " + Amount(output.amount));
        lines.push_back(output.address.empty() ? HexStr(output.script) : output.address);
        lines.push_back(AddressPosition(*output.position));
    }
    return lines;
}

ReviewLines FeeWarning()
{
    return {
        "Wallet apps such as Sparrow may omit previous transactions to keep QR codes small.", "",
        "This means ThunderDen cannot fully verify the fee.", "",
        "This is usually fine, but repeated signing requests can be abused to hide a higher fee.", "",
        "If your wallet unexpectedly asks you to sign again, stop and check why. Otherwise, this warning is expected and you can continue.", "",
        "Learn more: github.com/bitcoinerlab/thunderden/blob/master/docs/FEES.md",
    };
}
}
