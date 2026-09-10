#include "transaction.h"

#include <chainparams.h>
#include <consensus/tx_check.h>
#include <consensus/validation.h>
#include <key_io.h>
#include <policy/policy.h>
#include <script/interpreter.h>
#include <script/solver.h>
#include <streams.h>

#include <algorithm>
#include <set>

namespace td {
namespace {
template <typename Metadata>
std::optional<Position> Locate(const Policy& policy, const CScript& script, const Metadata& metadata)
{
    Require(metadata.hd_keypaths.size() + metadata.m_tap_bip32_paths.size() <= 128,
        "Too many derivation hints");
    std::set<Position> candidates;
    const auto add = [&](const KeyOriginInfo& origin) {
        const auto found = policy.Positions(origin);
        candidates.insert(found.begin(), found.end());
        Require(candidates.size() <= 16, "Too many candidate address positions");
    };
    for (const auto& [pubkey, origin] : metadata.hd_keypaths) add(origin);
    for (const auto& [pubkey, leaf_origin] : metadata.m_tap_bip32_paths) add(leaf_origin.second);
    std::optional<Position> result;
    for (const auto candidate : candidates) {
        if (policy.Script(candidate.branch, candidate.index) == script) {
            Require(!result.has_value(), "Ambiguous policy script");
            result = candidate;
        }
    }
    return result;
}

void AddAmount(CAmount& total, CAmount amount)
{
    Require(MoneyRange(amount) && MoneyRange(total + amount), "Amount out of range");
    total += amount;
}

int SigningRule(const CTxOut& output)
{
    return output.scriptPubKey.IsPayToTaproot() ? SIGHASH_DEFAULT : SIGHASH_ALL;
}

void CheckSignatures(const PSBTInput& input, int rule)
{
    Require(!input.sighash_type || *input.sighash_type == rule, "Unsupported signing rule");
    for (const auto& [key, signature] : input.partial_sigs) {
        Require(!signature.second.empty() && signature.second.back() == rule,
            "Existing signature has an incompatible signing rule");
    }
    if (rule == SIGHASH_DEFAULT) {
        Require(input.partial_sigs.empty(), "Unexpected ECDSA signatures in Taproot input");
        Require(input.m_tap_key_sig.empty() || input.m_tap_key_sig.size() == 64,
            "Existing Taproot signature has an incompatible signing rule");
        for (const auto& [key, signature] : input.m_tap_script_sigs) {
            Require(signature.size() == 64, "Existing Taproot signature has an incompatible signing rule");
        }
    } else {
        Require(input.m_tap_key_sig.empty() && input.m_tap_script_sigs.empty(), "Unexpected Taproot signatures");
    }
}

void CheckScripts(const PSBTInput& input, const PSBTInput& expected)
{
    Require(input.redeem_script.empty() || input.redeem_script == expected.redeem_script, "Wrong redeem script");
    Require(input.witness_script.empty() || input.witness_script == expected.witness_script, "Wrong witness script");
    Require(input.m_tap_internal_key.IsNull() || input.m_tap_internal_key == expected.m_tap_internal_key,
        "Wrong Taproot internal key");
    Require(input.m_tap_merkle_root.IsNull() || input.m_tap_merkle_root == expected.m_tap_merkle_root,
        "Wrong Taproot tree root");
    for (const auto& [leaf, controls] : input.m_tap_scripts) {
        const auto found = expected.m_tap_scripts.find(leaf);
        Require(found != expected.m_tap_scripts.end(), "Taproot leaf is outside the wallet policy");
        for (const auto& control : controls) {
            Require(found->second.contains(control), "Taproot control block is outside the wallet policy");
        }
    }
}

std::optional<int64_t> Estimate(PartiallySignedTransaction psbt, const Policy& policy,
                             const std::vector<InputReview>& inputs)
{
    for (size_t index = 0; index < inputs.size(); ++index) {
        if (inputs[index].finalized) continue;
        if (!inputs[index].position) return {};
        FlatSigningProvider provider;
        provider = policy.PublicProvider(*inputs[index].position);
        // Existing signatures are untrusted until verified and must not control
        // the estimated witness size. Use Core's fixed dummy signatures instead.
        psbt.inputs[index].partial_sigs.clear();
        psbt.inputs[index].m_tap_key_sig.clear();
        psbt.inputs[index].m_tap_script_sigs.clear();
        // A null txdata selects Core's dummy-signature creator. No private keys
        // are provided. Hashlocked/unfinished scripts may have no size estimate.
        if (SignPSBTInput(provider, psbt, index, nullptr) != PSBTError::OK) return {};
    }
    auto tx = *psbt.tx;
    for (size_t index = 0; index < tx.vin.size(); ++index) {
        tx.vin[index].scriptSig = psbt.inputs[index].final_script_sig;
        tx.vin[index].scriptWitness = psbt.inputs[index].final_script_witness;
    }
    return GetVirtualTransactionSize(CTransaction{tx});
}

template <typename Map>
size_t ChangedEntries(const Map& before, const Map& after)
{
    size_t count = 0;
    for (const auto& [key, value] : after) {
        const auto previous = before.find(key);
        if (previous == before.end() || previous->second != value) ++count;
    }
    return count;
}
}

ReviewedTransaction::ReviewedTransaction(Policy policy, const Keys& session,
    std::span<const unsigned char> tag, std::span<const std::byte> raw_psbt)
    : policy_(std::move(policy))
{
    Require(policy_.Authorized(session, tag), "Wallet policy is not authorized for this seed");
    std::copy(tag.begin(), tag.end(), tag_.begin());
    Require(!raw_psbt.empty() && raw_psbt.size() <= MAX_PSBT_BYTES, "PSBT size limit exceeded");
    std::string error;
    Require(DecodeRawPSBT(psbt_, raw_psbt, error) && psbt_.GetVersion() == 0, "Invalid PSBTv0");
    Require(psbt_.inputs.size() <= 128 && psbt_.outputs.size() <= 128, "Too many transaction inputs or outputs");
    const CTransaction tx{*psbt_.tx};
    TxValidationState state;
    Require(!tx.IsCoinBase() && CheckTransaction(tx, state), "Invalid unsigned transaction");
    review_.policy_id = policy_.ID();
    review_.policy_name = policy_.Name();
    review_.policy_template = policy_.Template();
    review_.signer = session.RootFingerprint();
    if (policy_.IsDefault(session)) review_.default_account = policy_.KeyInformation()[0].origin;
    review_.network = Params().GetChainType();
    review_.version = tx.version;
    review_.locktime = tx.nLockTime;

    std::vector<CTxOut> utxos;
    CAmount inputs_total = 0, outputs_total = 0;
    bool all_taproot = true;
    for (size_t index = 0; index < psbt_.inputs.size(); ++index) {
        const auto& input = psbt_.inputs[index];
        CTxOut utxo;
        Require(psbt_.GetInputUTXO(utxo, index), "Missing or incorrect previous output");
        Require(input.witness_utxo.IsNull() || input.witness_utxo == utxo, "Conflicting previous-output data");
        Require(!utxo.scriptPubKey.IsUnspendable(), "Input spends an unspendable script");
        AddAmount(inputs_total, utxo.nValue);
        all_taproot &= utxo.scriptPubKey.IsPayToTaproot();
        utxos.push_back(utxo);
        Require(input.m_musig2_participants.empty() && input.m_musig2_pubnonces.empty()
            && input.m_musig2_partial_sigs.empty(), "MuSig2 is not supported");
        for (const auto& [key, signature] : input.partial_sigs) {
            Require(!signature.second.empty()
                && CheckSignatureEncoding(signature.second, STANDARD_SCRIPT_VERIFY_FLAGS, nullptr),
                "Invalid partial signature encoding");
        }
    }
    // Legacy/SegWit-v0 signatures do not commit to every input's amount. Full
    // previous transactions prevent a coordinator understating other input values.
    if (!all_taproot) {
        for (const auto& input : psbt_.inputs) {
            Require(bool(input.non_witness_utxo), "Full previous transactions are required outside all-Taproot spends");
        }
    }

    const auto txdata = PrecomputePSBTData(psbt_);
    auto public_psbt = psbt_;
    size_t signable = 0;
    for (size_t index = 0; index < psbt_.inputs.size(); ++index) {
        const auto& input = psbt_.inputs[index];
        const auto& utxo = utxos[index];
        const auto position = Locate(policy_, utxo.scriptPubKey, input);
        const bool finalized = PSBTInputSigned(input);
        Require(!finalized || PSBTInputSignedAndVerified(psbt_, index, &txdata), "Invalid finalized input");
        review_.inputs.push_back({tx.vin[index].prevout, utxo.nValue, tx.vin[index].nSequence, position, finalized,
            position && !finalized ? std::optional{SigningRule(utxo)} : std::nullopt});
        if (!position) {
            ++review_.unrecognized_inputs;
            continue;
        }
        AddAmount(review_.recognized_inputs, utxo.nValue);
        if (finalized) continue;
        ++signable;
        const int rule = SigningRule(utxo);
        CheckSignatures(input, rule);
        auto provider = policy_.PublicProvider(*position);
        auto& clean = public_psbt.inputs[index];
        clean = PSBTInput{};
        clean.non_witness_utxo = input.non_witness_utxo;
        clean.witness_utxo = input.witness_utxo;
        const auto status = SignPSBTInput(provider, public_psbt, index, &txdata, rule, nullptr, false);
        Require(status == PSBTError::OK || status == PSBTError::INCOMPLETE, "Policy input cannot be processed");
        CheckScripts(input, clean);
        // Preimages and existing signatures are public PSBT data. Scripts above
        // came exclusively from the authorized descriptor, not from the request.
        clean.partial_sigs = input.partial_sigs;
        clean.m_tap_key_sig = input.m_tap_key_sig;
        clean.m_tap_script_sigs = input.m_tap_script_sigs;
        clean.ripemd160_preimages = input.ripemd160_preimages;
        clean.sha256_preimages = input.sha256_preimages;
        clean.hash160_preimages = input.hash160_preimages;
        clean.hash256_preimages = input.hash256_preimages;
    }
    Require(signable > 0, "No unfinished inputs were verified as belonging to this policy");

    for (size_t index = 0; index < tx.vout.size(); ++index) {
        const auto& output = tx.vout[index];
        const auto position = Locate(policy_, output.scriptPubKey, psbt_.outputs[index]);
        AddAmount(outputs_total, output.nValue);
        if (position) AddAmount(review_.recognized_outputs, output.nValue);
        CTxDestination destination;
        std::string address;
        if (ExtractDestination(output.scriptPubKey, destination)) address = EncodeDestination(destination);
        review_.outputs.push_back({output.nValue, output.scriptPubKey, address, position});
    }
    Require(outputs_total <= inputs_total, "Transaction spends more than its inputs");
    review_.fee = inputs_total - outputs_total;
    review_.estimated_vsize = Estimate(std::move(public_psbt), policy_, review_.inputs);
}

std::optional<SigningResult> ReviewedTransaction::Sign(const Keys& session,
    const std::function<bool(const TransactionReview&)>& approve) const
{
    Require(Params().GetChainType() == review_.network, "Network changed after review");
    Require(policy_.Authorized(session, tag_), "Seed changed after review");
    Require(bool(approve), "Missing transaction approval callback");
    if (!approve(review_)) return {};
    Require(Params().GetChainType() == review_.network, "Network changed during approval");

    auto signed_psbt = psbt_;
    const auto txdata = PrecomputePSBTData(signed_psbt);
    size_t added = 0;
    for (size_t index = 0; index < review_.inputs.size(); ++index) {
        const auto& input = review_.inputs[index];
        if (!input.position || input.finalized) continue;
        auto provider = policy_.PrivateProvider(*input.position, session);
        CTxOut utxo;
        Require(signed_psbt.GetInputUTXO(utxo, index), "Previous output unavailable");
        const auto status = SignPSBTInput(provider, signed_psbt, index, &txdata, SigningRule(utxo), nullptr, false);
        Require(status == PSBTError::OK || status == PSBTError::INCOMPLETE, "Core signing failed");
        const auto& before = psbt_.inputs[index];
        const auto& after = signed_psbt.inputs[index];
        added += ChangedEntries(before.partial_sigs, after.partial_sigs);
        added += ChangedEntries(before.m_tap_script_sigs, after.m_tap_script_sigs);
        added += !after.m_tap_key_sig.empty() && after.m_tap_key_sig != before.m_tap_key_sig;
    }
    Require(added > 0, "No new local signature was added");
    Require(CTransaction{*signed_psbt.tx}.GetHash() == CTransaction{*psbt_.tx}.GetHash(), "Unsigned transaction changed");
    auto finalized = signed_psbt;
    const bool complete = FinalizePSBT(finalized);
    if (complete) {
        for (size_t index = 0; index < finalized.inputs.size(); ++index) {
            Require(PSBTInputSignedAndVerified(finalized, index, &txdata), "Final script verification failed");
        }
    }
    DataStream stream;
    stream << signed_psbt;
    Require(stream.size() <= 2 * MAX_PSBT_BYTES, "Signed PSBT size limit exceeded");
    return SigningResult{{stream.begin(), stream.end()}, added, complete};
}
}
