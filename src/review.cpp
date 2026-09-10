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

ReviewLines PolicyReview(const Policy& policy, const Keys& keys)
{
    const auto owned = policy.OwnedKeys(keys);
    CTxDestination destination;
    Require(ExtractDestination(policy.Script(0, 0), destination), "Policy has no displayable receive address");
    ReviewLines lines{
        "Network: " + ChainTypeToString(Params().GetChainType()),
        "Wallet: " + (policy.Name().empty() ? std::string("Default account") : policy.Name()),
        "Signer fingerprint: " + HexStr(keys.RootFingerprint()),
        "Wallet ID: " + HexStr(policy.ID()),
        "Exact policy template:", policy.Template(),
    };
    for (size_t i = 0; i < policy.KeyInformation().size(); ++i) {
        lines.push_back("Key " + std::to_string(i) + (std::find(owned.begin(), owned.end(), i) != owned.end()
            ? " - verified local key:" : " - external key:"));
        lines.push_back(policy.KeyInformation()[i].text);
    }
    lines.push_back("First receive address (branch 0, index 0):");
    lines.push_back(EncodeDestination(destination));
    lines.push_back("Back up the complete wallet policy as well as your recovery words.");
    return lines;
}

ReviewLines TransactionLines(const TransactionReview& review)
{
    ReviewLines lines{
        "Network: " + ChainTypeToString(review.network),
        "Wallet: " + (review.policy_name.empty() ? std::string("Default account") : review.policy_name),
        "Signer fingerprint: " + HexStr(review.signer),
        "Wallet ID: " + HexStr(review.policy_id),
        "Policy: " + review.policy_template,
    };
    if (review.default_account) lines.push_back("Account: " + PathText(*review.default_account));
    lines.push_back("Transaction fee: " + Amount(review.fee));
    if (review.estimated_vsize) {
        const auto rate = review.fee * 100 / *review.estimated_vsize;
        lines.push_back("Estimated size: " + std::to_string(*review.estimated_vsize) + " vB");
        lines.push_back("Approximate fee rate: " + std::to_string(rate / 100) + "."
            + (rate % 100 < 10 ? "0" : "") + std::to_string(rate % 100) + " sat/vB");
    } else {
        lines.push_back("Fee-rate estimate unavailable: unfinished input or script condition.");
    }
    lines.push_back("Recognized wallet inputs: " + Amount(review.recognized_inputs));
    lines.push_back("Recognized wallet outputs: " + Amount(review.recognized_outputs));
    lines.push_back("Wallet net spend (inputs minus outputs): " + Amount(review.recognized_inputs - review.recognized_outputs));
    lines.push_back("Unrecognized inputs: " + std::to_string(review.unrecognized_inputs));
    lines.push_back("Transaction version: " + std::to_string(review.version));
    lines.push_back("Locktime: " + std::to_string(review.locktime)
        + (review.locktime < 500000000 ? " (block height)" : " (Unix time)"));
    const bool all_final = std::all_of(review.inputs.begin(), review.inputs.end(), [](const auto& input) {
        return input.sequence == 0xffffffff;
    });
    if (all_final) lines.push_back("All sequences are final: locktime is inactive.");
    const bool rbf = std::any_of(review.inputs.begin(), review.inputs.end(), [](const auto& input) {
        return input.sequence < 0xfffffffe;
    });
    lines.push_back(std::string("Signals opt-in replacement: ") + (rbf ? "yes" : "no"));
    lines.push_back("Chain state and timelock maturity are not known by this offline signer.");
    for (size_t i = 0; i < review.outputs.size(); ++i) {
        const auto& output = review.outputs[i];
        const std::string role = !output.position ? "EXTERNAL DESTINATION" :
            output.position->branch == 1 ? "VERIFIED CHANGE" : "VERIFIED RECEIVE / SELF-PAYMENT";
        lines.push_back("--- Output " + std::to_string(i + 1) + " of " + std::to_string(review.outputs.size()) + " ---");
        lines.push_back(role);
        lines.push_back("Amount: " + Amount(output.amount));
        lines.push_back(output.address.empty() ? "Raw script (hex):" : "Destination:");
        lines.push_back(output.address.empty() ? HexStr(output.script) : output.address);
        if (output.position) lines.push_back("Policy branch/index: " + std::to_string(output.position->branch)
            + "/" + std::to_string(output.position->index));
    }
    for (size_t i = 0; i < review.inputs.size(); ++i) {
        const auto& input = review.inputs[i];
        lines.push_back("--- Input " + std::to_string(i + 1) + " of " + std::to_string(review.inputs.size()) + " ---");
        lines.push_back(input.previous.hash.ToString() + ":" + std::to_string(input.previous.n));
        lines.push_back("Amount: " + Amount(input.amount));
        lines.push_back("Sequence: " + std::to_string(input.sequence));
        lines.push_back(input.finalized ? "Already finalized and verified" : input.position ? "Verified policy input" : "Unrecognized input - not signed");
        if (input.position) lines.push_back("Policy branch/index: " + std::to_string(input.position->branch)
            + "/" + std::to_string(input.position->index));
        if (input.signing_rule) lines.push_back(*input.signing_rule == SIGHASH_DEFAULT ? "Signing rule: DEFAULT" : "Signing rule: ALL");
    }
    return lines;
}
}
