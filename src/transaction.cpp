#include "transaction.h"

#include <chainparams.h>
#include <crypto/common.h>
#include <consensus/tx_check.h>
#include <consensus/validation.h>
#include <key_io.h>
#include <policy/policy.h>
#include <script/interpreter.h>
#include <script/solver.h>
#include <streams.h>
#include <util/strencodings.h>

#include <algorithm>
#include <set>

namespace td {
// One immutable request is shared by candidate reviews. No caller can construct
// or mutate this snapshot; it is decoded and validated only by ReadPSBT below.
struct PSBTData {
    PartiallySignedTransaction psbt;
    std::vector<CTxOut> utxos;
    PrecomputedTransactionData txdata;
    CAmount fee{0};
    size_t missing_previous{0};
    bool finalized{false};
};

namespace {
CTxOut PreviousOutput(const PartiallySignedTransaction& psbt, size_t index)
{
    const auto& input = psbt.inputs[index];
    CTxOut utxo;
    Require(psbt.GetInputUTXO(utxo, index), "Missing or incorrect previous output");
    Require(input.witness_utxo.IsNull() || input.witness_utxo == utxo, "Conflicting previous-output data");
    Require(MoneyRange(utxo.nValue), "Amount out of range");
    Require(!utxo.scriptPubKey.IsUnspendable(), "Input spends an unspendable script");
    return utxo;
}

// Only recognized witness families qualify for compact UTXO data. For P2SH,
// the redeem script must actually hash to the supplied output; field presence
// alone does not establish SegWit. Policy providers supply our own scripts.
int WitnessVersion(const CScript& output, const SigningProvider& provider)
{
    CScript script = output;
    const bool wrapped = script.IsPayToScriptHash();
    if (wrapped) {
        std::vector<std::vector<unsigned char>> solutions;
        Solver(script, solutions);
        if (!provider.GetCScript(CScriptID{uint160(solutions[0])}, script)) return -1;
        Require(GetScriptForDestination(ScriptHash(script)) == output, "Wrong redeem script");
    }
    int version;
    std::vector<unsigned char> program;
    if (!script.IsWitnessProgram(version, program)) return -1;
    if (version == 0 && (program.size() == 20 || program.size() == 32)) return 0;
    return !wrapped && version == 1 && program.size() == 32 ? 1 : -1;
}

FlatSigningProvider ExternalScripts(const PSBTInput& input)
{
    FlatSigningProvider provider;
    auto script = input.redeem_script;
    if (script.empty() && !input.final_script_sig.empty()) {
        // A finalized wrapped-witness scriptSig is a single redeem-script push.
        auto pc = input.final_script_sig.begin();
        opcodetype opcode;
        std::vector<unsigned char> data;
        if (input.final_script_sig.GetOp(pc, opcode, data) && pc == input.final_script_sig.end()
            && opcode <= OP_PUSHDATA4) script = CScript(data.begin(), data.end());
    }
    if (!script.empty()) provider.scripts.emplace(CScriptID(script), script);
    return provider;
}

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

std::shared_ptr<const PSBTData> ReadPSBT(std::span<const std::byte> raw)
{
    Require(!raw.empty() && raw.size() <= ReviewedTransaction::MAX_PSBT_BYTES, "PSBT size limit exceeded");
    auto request = std::make_shared<PSBTData>();
    auto& psbt = request->psbt;
    std::string error;
    Require(DecodeRawPSBT(psbt, raw, error) && psbt.GetVersion() == 0, "Invalid PSBTv0");
    Require(psbt.inputs.size() <= 128 && psbt.outputs.size() <= 128, "Too many transaction inputs or outputs");
    size_t account_keys = 0;
    for (const auto& [origin, xpubs] : psbt.m_xpubs) {
        Require(origin.path.size() <= 32, "Account origin is too deep");
        account_keys += xpubs.size();
        Require(account_keys <= 32, "Too many account keys in this PSBT");
    }
    TxValidationState state;
    const CTransaction tx{*psbt.tx};
    Require(!tx.IsCoinBase() && CheckTransaction(tx, state), "Invalid unsigned transaction");
    CAmount inputs_total = 0, outputs_total = 0;
    for (size_t i = 0; i < psbt.inputs.size(); ++i) {
        const auto& input = psbt.inputs[i];
        request->utxos.push_back(PreviousOutput(psbt, i));
        AddAmount(inputs_total, request->utxos.back().nValue);
        request->missing_previous += !input.non_witness_utxo;
        Require(input.m_musig2_participants.empty() && input.m_musig2_pubnonces.empty()
            && input.m_musig2_partial_sigs.empty(), "MuSig2 is not supported");
        for (const auto& [key, signature] : input.partial_sigs) {
            Require(!signature.second.empty()
                && CheckSignatureEncoding(signature.second, STANDARD_SCRIPT_VERIFY_FLAGS, nullptr),
                "Invalid partial signature encoding");
        }
    }
    for (const auto& output : tx.vout) AddAmount(outputs_total, output.nValue);
    Require(outputs_total <= inputs_total, "Transaction spends more than its inputs");
    request->fee = inputs_total - outputs_total;
    request->txdata = PrecomputePSBTData(psbt);
    request->finalized = true;
    for (size_t i = 0; i < psbt.inputs.size(); ++i) {
        const auto& input = psbt.inputs[i];
        if (!PSBTInputSigned(input)) { request->finalized = false; continue; }
        Require(input.non_witness_utxo || WitnessVersion(request->utxos[i].scriptPubKey, ExternalScripts(input)) >= 0,
            "Full previous transactions are required for legacy or unrecognized input types");
        Require(PSBTInputSignedAndVerified(psbt, i, &request->txdata), "Invalid finalized input");
    }
    return request;
}

void DiscoverMultisig(const PSBTData& request, const Keys& keys, const ApprovedWallet* loaded, SigningChoices& result)
{
    const auto fingerprint = keys.RootFingerprint();
    const bool mainnet = Params().GetChainType() == ChainType::MAIN;
    bool ambiguous = false;
    for (size_t i = 0; i < request.psbt.inputs.size(); ++i) {
        const auto& input = request.psbt.inputs[i];
        if (PSBTInputSigned(input)) continue;
        const auto& output = request.utxos[i].scriptPubKey;
        if (loaded && Locate(loaded->policy, output, input)) continue;
        const auto local_hint = [&](unsigned script_type) {
            for (const auto& [pubkey, hint] : input.hd_keypaths) {
                auto path = hint.path;
                if (path.size() < 3 || path.size() > 6 || path[path.size() - 2] > 1 || path.back() >= 0x80000000U) continue;
                path.resize(path.size() - 2);
                if (MultisigAccountPath(path, script_type, mainnet)
                    && std::equal(fingerprint.begin(), fingerprint.end(), hint.fingerprint)
                    && keys.PublicAt(hint.path).pubkey == pubkey) return true;
            }
            return false;
        };
        auto script = output;
        unsigned script_type = 0;
        if (output.IsPayToScriptHash()) {
            if (input.redeem_script.empty()) {
                if (local_hint(0) || local_hint(1)) { result.multisig = true; result.needs_wallet = true; }
                continue;
            }
            Require(GetScriptForDestination(ScriptHash(input.redeem_script)) == output, "Wrong redeem script");
            script = input.redeem_script;
            script_type = 1;
        }
        int version;
        std::vector<unsigned char> program;
        if (script.IsWitnessProgram(version, program)) {
            if (version != 0 || program.size() != 32) continue;
            if (input.witness_script.empty()) {
                if (local_hint(output.IsPayToScriptHash() ? 1 : 2)) { result.multisig = true; result.needs_wallet = true; }
                continue;
            }
            Require(GetScriptForDestination(WitnessV0ScriptHash(input.witness_script)) == script, "Wrong witness script");
            script = input.witness_script;
            if (!output.IsPayToScriptHash()) script_type = 2;
        } else if (!output.IsPayToScriptHash()) continue;
        else script_type = 0;
        std::vector<std::vector<unsigned char>> solutions;
        if (Solver(script, solutions) != TxoutType::MULTISIG) continue;
        bool ours = false;
        for (size_t n = 1; n + 1 < solutions.size(); ++n) {
            const CPubKey pubkey(solutions[n]);
            const auto hint = input.hd_keypaths.find(pubkey);
            if (hint == input.hd_keypaths.end()) continue;
            const auto& origin = hint->second;
            if (origin.path.size() <= 32 && std::equal(fingerprint.begin(), fingerprint.end(), origin.fingerprint)
                && keys.PublicAt(origin.path).pubkey == pubkey) ours = true;
        }
        if (!ours) continue;
        result.multisig = true;
        result.needs_wallet = true;
        std::vector<std::string> account_keys;
        std::optional<Position> position;
        bool complete = true;
        CPubKey previous;
        for (size_t n = 1; n + 1 < solutions.size(); ++n) {
            const CPubKey pubkey(solutions[n]);
            Require(pubkey.IsFullyValid() && pubkey.IsCompressed() && (!previous.IsValid() || previous < pubkey),
                "PSBT discovery requires distinct, sorted multisig keys");
            previous = pubkey;
            const auto hint = input.hd_keypaths.find(pubkey);
            if (hint == input.hd_keypaths.end()) { complete = false; continue; }
            auto origin = hint->second;
            Require(origin.path.size() >= 3 && origin.path.size() <= 6, "Invalid multisig key path");
            const Position child{origin.path[origin.path.size() - 2], origin.path.back()};
            Require(child.branch <= 1 && child.index < 0x80000000U, "Invalid multisig receiving/change path");
            Require(!position || *position == child, "Cosigners describe different address positions");
            position = child;
            origin.path.resize(origin.path.size() - 2);
            Require(MultisigAccountPath(origin.path, script_type, mainnet), "Unsupported multisig account path");
            const auto found = request.psbt.m_xpubs.find(origin);
            if (found == request.psbt.m_xpubs.end()) { complete = false; continue; }
            Require(found->second.size() == 1, "Ambiguous multisig account key");
            const auto& account = *found->second.begin();
            Require(ReadBE32(account.version) == (mainnet ? 0x0488B21EU : 0x043587CFU)
                && account.nDepth == origin.path.size() && account.nChild == origin.path.back(), "Conflicting account key metadata");
            if (origin.path.size() == 1) Require(std::equal(account.vchFingerprint, account.vchFingerprint + 4, origin.fingerprint),
                "Conflicting parent fingerprint");
            CExtPubKey branch, derived;
            Require(account.Derive(branch, child.branch) && branch.Derive(derived, child.index)
                && derived.pubkey == pubkey, "Account key does not derive the multisig input key");
            account_keys.push_back("[" + HexStr(std::span(origin.fingerprint)) + PathText(origin.path).substr(1)
                + "]" + EncodePublic(account, mainnet));
        }
        if (!complete) continue;
        // Script key order changes at each index. Canonical account ordering
        // gives one candidate identity for every input in the same sorted wallet.
        std::sort(account_keys.begin(), account_keys.end());
        auto policy = MultisigPolicy(script_type, solutions.front()[0], std::move(account_keys), mainnet);
        Require(!policy.OwnedKeys(keys).empty() && policy.Script(position->branch, position->index) == output,
            "Reconstructed wallet does not match this input");
        if (result.setup && result.setup->ID() != policy.ID()) ambiguous = true;
        else result.setup = std::move(policy);
    }
    // Multiple new wallets are not one unambiguous setup. Let the user supply
    // the intended descriptor; a matching local key alone never authorizes it.
    if (ambiguous) result.setup.reset();
}
}

ReviewedTransaction::ReviewedTransaction(Policy policy, const Keys& session,
    std::span<const unsigned char> tag, std::span<const std::byte> raw_psbt)
    : ReviewedTransaction(ReadPSBT(raw_psbt), std::move(policy), session, tag)
{
}

ReviewedTransaction::ReviewedTransaction(std::shared_ptr<const PSBTData> request, Policy policy,
    const Keys& session, std::span<const unsigned char> tag)
    : policy_(std::move(policy)), request_(std::move(request))
{
    Require(policy_.Authorized(session, tag), "Wallet policy is not authorized for this seed");
    std::copy(tag.begin(), tag.end(), tag_.begin());
    const auto& psbt = request_->psbt;
    const auto& txdata = request_->txdata;
    const auto& tx = *psbt.tx;
    review_.policy_id = policy_.ID();
    review_.policy_name = policy_.Name();
    review_.policy_template = policy_.Template();
    review_.policy_descriptor = policy_.DescriptorText();
    review_.signer = session.RootFingerprint();
    if (policy_.IsDefault(session)) review_.default_account = policy_.KeyInformation()[0].origin;
    review_.network = Params().GetChainType();
    review_.version = tx.version;
    review_.locktime = tx.nLockTime;
    review_.fee = request_->fee;
    auto public_psbt = psbt;
    size_t recognized = 0;
    for (size_t index = 0; index < psbt.inputs.size(); ++index) {
        const auto& input = psbt.inputs[index];
        const auto& utxo = request_->utxos[index];
        const auto position = Locate(policy_, utxo.scriptPubKey, input);
        auto provider = position ? policy_.PublicProvider(*position) : ExternalScripts(input);
        const auto witness_version = WitnessVersion(utxo.scriptPubKey, provider);
        Require(input.non_witness_utxo || witness_version >= 0,
            "Full previous transactions are required for legacy or unrecognized input types");
        const bool finalized = PSBTInputSigned(input);
        review_.inputs.push_back({tx.vin[index].prevout, utxo.nValue, tx.vin[index].nSequence, position, finalized,
            position && !finalized ? std::optional{SigningRule(utxo)} : std::nullopt, bool(input.non_witness_utxo)});
        if (!position) {
            ++review_.unrecognized_inputs;
            continue;
        }
        AddAmount(review_.recognized_inputs, utxo.nValue);
        ++recognized;
        if (finalized) continue;
        const int rule = SigningRule(utxo);
        CheckSignatures(input, rule);
        // DEFAULT binds every input amount. SegWit v0 ALL binds only its own;
        // legacy ALL binds none. Warn if any signature we may release leaves
        // an amount unauthenticated. Consent does not upgrade that assurance.
        review_.fee_unverified |= witness_version != 1
            && request_->missing_previous > size_t(witness_version == 0 && !input.non_witness_utxo);
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
    Require(recognized > 0, "No inputs were verified as belonging to this policy");

    for (size_t index = 0; index < tx.vout.size(); ++index) {
        const auto& output = tx.vout[index];
        const auto position = Locate(policy_, output.scriptPubKey, psbt.outputs[index]);
        if (position) AddAmount(review_.recognized_outputs, output.nValue);
        CTxDestination destination;
        std::string address;
        if (ExtractDestination(output.scriptPubKey, destination)) address = EncodeDestination(destination);
        review_.outputs.push_back({output.nValue, output.scriptPubKey, address, position});
    }
    review_.estimated_vsize = Estimate(std::move(public_psbt), policy_, review_.inputs);
}

std::optional<SigningResult> ReviewedTransaction::Sign(const Keys& session,
    const std::function<bool(const TransactionReview&)>& approve) const
{
    Require(Params().GetChainType() == review_.network, "Network changed after review");
    Require(policy_.Authorized(session, tag_), "Seed changed after review");
    Require(std::any_of(review_.inputs.begin(), review_.inputs.end(), [](const auto& input) {
        return input.position && !input.finalized;
    }), "Nothing more to sign for this wallet");
    Require(bool(approve), "Missing transaction approval callback");
    if (!approve(review_)) return {};
    Require(Params().GetChainType() == review_.network, "Network changed during approval");

    const auto& psbt = request_->psbt;
    auto signed_psbt = psbt;
    const auto& txdata = request_->txdata;
    size_t added = 0;
    for (size_t index = 0; index < review_.inputs.size(); ++index) {
        const auto& input = review_.inputs[index];
        if (!input.position || input.finalized) continue;
        auto provider = policy_.PrivateProvider(*input.position, session);
        const auto& utxo = request_->utxos[index];
        const auto status = SignPSBTInput(provider, signed_psbt, index, &txdata, SigningRule(utxo), nullptr, false);
        Require(status == PSBTError::OK || status == PSBTError::INCOMPLETE, "Core signing failed");
        const auto& before = psbt.inputs[index];
        const auto& after = signed_psbt.inputs[index];
        added += ChangedEntries(before.partial_sigs, after.partial_sigs);
        added += ChangedEntries(before.m_tap_script_sigs, after.m_tap_script_sigs);
        added += !after.m_tap_key_sig.empty() && after.m_tap_key_sig != before.m_tap_key_sig;
    }
    Require(added > 0, "No new local signature was added");
    Require(CTransaction{*signed_psbt.tx}.GetHash() == CTransaction{*psbt.tx}.GetHash(), "Unsigned transaction changed");
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

size_t ReviewedTransaction::PendingInputCount(const Keys& session) const
{
    Require(policy_.Authorized(session, tag_), "Wallet policy is not authorized for this seed");
    auto public_psbt = request_->psbt;
    const auto& txdata = request_->txdata;
    const auto fingerprint = session.RootFingerprint();
    size_t pending = 0;
    for (size_t i = 0; i < review_.inputs.size(); ++i) {
        const auto& facts = review_.inputs[i];
        if (!facts.position || facts.finalized) continue;
        const auto provider = policy_.PublicProvider(*facts.position);
        if (SignPSBTInput(provider, public_psbt, i, &txdata, facts.signing_rule, nullptr, false) == PSBTError::OK) continue;
        const auto& input = public_psbt.inputs[i];
        const auto& utxo = request_->utxos[i];
        const int witness_version = WitnessVersion(utxo.scriptPubKey, provider);
        if (witness_version == 1) {
            // Automatic selection supports BIP86, not inferred Taproot policies.
            Require(review_.default_account.has_value(), "Load a supported multisig wallet setup");
            Require(input.m_tap_key_sig.empty(), "Invalid existing Taproot signature");
            ++pending;
            continue;
        }
        auto script_code = utxo.scriptPubKey.IsPayToScriptHash() ? input.redeem_script : utxo.scriptPubKey;
        if (witness_version == 0) {
            int version;
            std::vector<unsigned char> program;
            Require(script_code.IsWitnessProgram(version, program), "Missing witness program");
            script_code = program.size() == 20 ? GetScriptForDestination(PKHash(uint160(program))) : input.witness_script;
        }
        const MutableTransactionSignatureChecker checker(&*public_psbt.tx, i, utxo.nValue, txdata, MissingDataBehavior::FAIL);
        bool missing = false;
        for (const auto& [id, key_origin] : provider.origins) {
            const auto& [pubkey, origin] = key_origin;
            if (!std::equal(fingerprint.begin(), fingerprint.end(), origin.fingerprint)
                || session.PublicAt(origin.path).pubkey != pubkey) continue;
            const auto sig = input.partial_sigs.find(id);
            if (sig == input.partial_sigs.end()) missing = true;
            else Require(sig->second.first == pubkey && checker.CheckECDSASignature(sig->second.second,
                {pubkey.begin(), pubkey.end()}, script_code, witness_version == 0 ? SigVersion::WITNESS_V0 : SigVersion::BASE),
                "Invalid existing signature for this wallet");
        }
        pending += missing;
    }
    return pending;
}

SigningChoices FindSigningWallets(std::span<const std::byte> raw, const Keys& keys, const ApprovedWallet* loaded)
{
    const auto request = ReadPSBT(raw);
    const auto& psbt = request->psbt;
    if (request->finalized) return {{}, false, false, {}};
    std::set<std::pair<unsigned, unsigned>> accounts;
    const auto fingerprint = keys.RootFingerprint();
    const auto coin = Params().GetChainType() == ChainType::MAIN ? 0x80000000U : 0x80000001U;
    const auto hint = [&](const KeyOriginInfo& origin) {
        const auto& path = origin.path;
        if (!std::equal(fingerprint.begin(), fingerprint.end(), origin.fingerprint) || path.size() != 5
            || path[1] != coin || path[2] < 0x80000000U || path[2] > 0x80000064U
            || path[3] > 1 || path[4] >= 0x80000000U) return;
        for (unsigned purpose : {44, 49, 84, 86}) if (path[0] == (0x80000000U | purpose)) {
            accounts.emplace(purpose, path[2] & 0x7fffffffU);
            Require(accounts.size() <= 8, "Too many signing accounts in one request");
        }
    };
    for (const auto& input : psbt.inputs) {
        Require(input.hd_keypaths.size() + input.m_tap_bip32_paths.size() <= 128, "Too many derivation hints");
        for (const auto& [pubkey, origin] : input.hd_keypaths) hint(origin);
        for (const auto& [pubkey, leaf_origin] : input.m_tap_bip32_paths) hint(leaf_origin.second);
    }
    SigningChoices result;
    const auto add = [&](Policy policy, const Digest& proof) {
        bool matches = false;
        for (size_t i = 0; i < psbt.inputs.size(); ++i) {
            const auto& utxo = request->utxos[i];
            matches |= Locate(policy, utxo.scriptPubKey, psbt.inputs[i]).has_value();
        }
        if (!matches) return;
        result.needs_wallet = false;
        auto transaction = std::unique_ptr<ReviewedTransaction>(new ReviewedTransaction(request, std::move(policy), keys, proof));
        const auto count = transaction->PendingInputCount(keys);
        if (count) result.wallets.push_back({std::move(transaction), count});
    };
    for (const auto& [purpose, account] : accounts) add(DefaultPolicy(keys, purpose, account), Digest{});
    if (loaded) add(loaded->policy, loaded->proof);
    if (result.wallets.empty()) DiscoverMultisig(*request, keys, loaded, result);
    return result;
}
}
