#include "transaction.h"

#include <chainparams.h>
#include <crypto/sha256.h>
#include <key_io.h>
#include <secp256k1.h>
#include <secp256k1_schnorrsig.h>
#include <streams.h>
#include <util/strencodings.h>

#include <cstdio>
#include <functional>
#include <stdexcept>

static size_t signature_calls;
extern "C" int __real_secp256k1_ecdsa_sign(const secp256k1_context*, secp256k1_ecdsa_signature*,
    const unsigned char*, const unsigned char*, secp256k1_nonce_function, const void*);
extern "C" int __wrap_secp256k1_ecdsa_sign(const secp256k1_context* ctx, secp256k1_ecdsa_signature* sig,
    const unsigned char* msg, const unsigned char* key, secp256k1_nonce_function nonce, const void* data)
{
    ++signature_calls;
    return __real_secp256k1_ecdsa_sign(ctx, sig, msg, key, nonce, data);
}
extern "C" int __real_secp256k1_schnorrsig_sign32(const secp256k1_context*, unsigned char*,
    const unsigned char*, const secp256k1_keypair*, const unsigned char*);
extern "C" int __wrap_secp256k1_schnorrsig_sign32(const secp256k1_context* ctx, unsigned char* sig,
    const unsigned char* msg, const secp256k1_keypair* key, const unsigned char* aux)
{
    ++signature_calls;
    return __real_secp256k1_schnorrsig_sign32(ctx, sig, msg, key, aux);
}

namespace {
const std::string MNEMONIC = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
const std::string OTHER = "all all all all all all all all all all all all";

auto Bytes(std::string_view text)
{
    return std::span{reinterpret_cast<const unsigned char*>(text.data()), text.size()};
}

void Check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void Reject(const std::function<void()>& operation)
{
    try { operation(); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Unsafe transaction was accepted");
}

std::vector<std::byte> Serialize(const PartiallySignedTransaction& psbt)
{
    DataStream stream;
    stream << psbt;
    return {stream.begin(), stream.end()};
}

PartiallySignedTransaction Decode(std::span<const std::byte> bytes)
{
    PartiallySignedTransaction psbt;
    std::string error;
    Check(DecodeRawPSBT(psbt, bytes, error), "Could not decode signed PSBT");
    return psbt;
}

std::string Key(const td::Keys& session, unsigned purpose, unsigned account = 0)
{
    const td::Path path{0x80000000U | purpose, 0x80000001U, 0x80000000U | account};
    return "[" + HexStr(session.RootFingerprint()) + "/" + std::to_string(purpose)
        + "h/1h/" + std::to_string(account) + "h]" + td::EncodePublic(session.Derive(path).Neuter(), false);
}

struct Wallet {
    std::string name, text;
    std::vector<std::string> keys;
    td::Policy Make() const { return td::Policy{name, text, keys, false}; }
    td::Digest Tag(const td::Keys& session) const
    {
        auto policy = Make();
        return policy.IsDefault(session) ? td::Digest{} : session.RegistrationTag(policy.ID());
    }
};

CTransactionRef Funding(const CScript& script, CAmount value)
{
    static unsigned counter = 0;
    uint256 hash;
    hash.begin()[0] = ++counter;
    CMutableTransaction tx;
    tx.vin.emplace_back(COutPoint{Txid::FromUint256(hash), 0});
    tx.vout.emplace_back(value, script);
    return MakeTransactionRef(tx);
}

void FillInput(PartiallySignedTransaction& psbt, size_t index,
               const td::Policy& policy, td::Position position)
{
    const auto txdata = PrecomputePSBTData(psbt);
    const auto provider = policy.PublicProvider(position);
    const auto result = SignPSBTInput(provider, psbt, index, &txdata, std::nullopt, nullptr, false);
    Check(result == PSBTError::OK || result == PSBTError::INCOMPLETE, "Fixture metadata generation failed");
}

PartiallySignedTransaction Fixture(const Wallet& wallet, const CScript& destination,
                                   uint32_t sequence = 0xffffffff, uint32_t locktime = 0)
{
    const auto policy = wallet.Make();
    const auto previous = Funding(policy.Script(0, 5), COIN);
    CMutableTransaction tx;
    tx.nLockTime = locktime;
    tx.vin.emplace_back(COutPoint{previous->GetHash(), 0}, CScript{}, sequence);
    tx.vout.emplace_back(60000000, destination);
    tx.vout.emplace_back(39999000, policy.Script(1, 6));
    PartiallySignedTransaction psbt{tx};
    psbt.inputs[0].non_witness_utxo = previous;
    FillInput(psbt, 0, policy, {0, 5});
    const auto provider = policy.PublicProvider({1, 6});
    UpdatePSBTOutput(provider, psbt, 1);
    return psbt;
}

void Verify(const td::SigningResult& result)
{
    auto psbt = Decode(result.psbt);
    Check(FinalizePSBT(psbt), "Expected finalizable PSBT");
    const auto data = PrecomputePSBTData(psbt);
    for (size_t i = 0; i < psbt.inputs.size(); ++i) {
        Check(PSBTInputSignedAndVerified(psbt, i, &data), "Core rejected resulting script/signature");
    }
}

void ScriptFamilies(const td::Keys& alice, const td::Keys& bob, const CScript& destination)
{
    struct Case {
        std::string label, text;
        unsigned purpose;
        unsigned keys;
        bool partial;
        bool foreign_internal;
        uint32_t sequence{0xffffffff};
        uint32_t locktime{0};
    };
    const std::vector<Case> cases{
        {"BIP44", "pkh(@0/**)", 44, 1, false, false},
        {"BIP49", "sh(wpkh(@0/**))", 49, 1, false, false},
        {"BIP84", "wpkh(@0/**)", 84, 1, false, false},
        {"BIP86", "tr(@0/**)", 86, 1, false, false},
        {"custom branches", "wpkh(@0/<4;9>/*)", 48, 1, false, false},
        {"repeated account key", "wsh(multi(2,@0/**,@0/<2;3>/*))", 48, 1, false, false},
        {"P2SH multisig", "sh(sortedmulti(2,@0/**,@1/**))", 48, 2, true, false},
        {"wrapped multisig", "sh(wsh(sortedmulti(2,@0/**,@1/**)))", 48, 2, true, false},
        {"native multisig", "wsh(sortedmulti(2,@0/**,@1/**))", 48, 2, true, false},
        {"Miniscript alternative", "wsh(and_v(v:pk(@0/**),or_i(pk(@1/**),older(2))))", 48, 2, true, false},
        {"relative timelock", "wsh(and_v(v:pk(@0/**),older(2)))", 48, 1, false, false, 2},
        {"absolute timelock", "wsh(and_v(v:pk(@0/**),after(150)))", 48, 1, false, false, 0xfffffffe, 150},
        {"Taproot script path", "tr(@0/**,pk(@1/**))", 48, 2, false, true},
        {"Taproot tree", "tr(@0/**,{pk(@1/**),pk(@2/**)})", 48, 3, false, true},
        {"Taproot multi_a", "tr(@0/**,multi_a(2,@1/**,@2/**))", 48, 3, true, true},
        {"Taproot Miniscript", "tr(@0/**,and_v(v:pk(@1/**),pk(@2/**)))", 48, 3, true, true},
    };
    for (const auto& test : cases) {
        Wallet wallet{test.keys == 1 && test.purpose != 48 ? "" : test.label, test.text, {}};
        if (test.foreign_internal) wallet.keys.push_back(Key(bob, 48, 1));
        wallet.keys.push_back(Key(alice, test.purpose));
        if (wallet.keys.size() < test.keys) wallet.keys.push_back(Key(bob, test.purpose));
        auto original = Fixture(wallet, destination, test.sequence, test.locktime);
        const auto unsigned_hash = CTransaction{*original.tx}.GetHash();
        const auto calls_before = signature_calls;
        td::ReviewedTransaction review(wallet.Make(), alice, wallet.Tag(alice), Serialize(original));
        Check(signature_calls == calls_before, "Signing occurred during review construction");
        const auto& facts = review.Review();
        Check(facts.policy_template == test.text && facts.signer == alice.RootFingerprint(), "Incorrect wallet identity");
        Check(facts.default_account.has_value() == wallet.name.empty(), "Default account identity missing");
        Check(facts.fee == 1000 && facts.recognized_inputs == COIN && facts.recognized_outputs == 39999000,
            "Incorrect review amounts");
        Check(facts.outputs[0].position == std::nullopt && facts.outputs[1].position == td::Position{1, 6},
            "Incorrect change classification");
        Check(facts.inputs[0].sequence == test.sequence && facts.locktime == test.locktime, "Incorrect lock fields");
        Check(facts.inputs[0].signing_rule == (test.text.starts_with("tr(") ? SIGHASH_DEFAULT : SIGHASH_ALL),
            "Missing or incorrect review signing rule");
        Check(facts.estimated_vsize.has_value(), "Expected size estimate");
        Check(!review.Sign(alice, [](const auto&) { return false; }), "Rejected transaction returned a signature");
        Check(signature_calls == calls_before, "Signing occurred without approval");
        auto result = review.Sign(alice, [](const auto&) { return true; });
        Check(result && result->added_signatures > 0 && signature_calls > calls_before, "No signing progress");
        Check(CTransaction{*Decode(result->psbt).tx}.GetHash() == unsigned_hash, "Unsigned transaction changed");
        Check(result->complete == !test.partial, "Incorrect partial-signing status");
        if (test.partial) {
            td::ReviewedTransaction second(wallet.Make(), bob, wallet.Tag(bob), result->psbt);
            result = second.Sign(bob, [](const auto&) { return true; });
            Check(result && result->complete, "Second cosigner did not complete transaction");
        }
        Verify(*result);
        std::printf("PASS: %s review, approval and Core script verification\n", test.label.c_str());
    }

    const std::vector<unsigned char> preimage(32, 0x42);
    for (const std::string kind : {"sha256", "hash256", "ripemd160", "hash160"}) {
        uint256 hash32;
        uint160 hash20;
        if (kind == "sha256") CSHA256().Write(preimage.data(), preimage.size()).Finalize(hash32.begin());
        else if (kind == "hash256") hash32 = Hash(preimage);
        else if (kind == "ripemd160") CRIPEMD160().Write(preimage.data(), preimage.size()).Finalize(hash20.begin());
        else hash20 = Hash160(preimage);
        const std::string digest = kind.ends_with("256") ? HexStr(hash32) : HexStr(hash20);
        Wallet wallet{kind, "wsh(and_v(v:pk(@0/**)," + kind + "(" + digest + ")))", {Key(alice, 48)}};
        auto psbt = Fixture(wallet, destination);
        td::ReviewedTransaction missing(wallet.Make(), alice, wallet.Tag(alice), Serialize(psbt));
        Check(!missing.Review().estimated_vsize, "Missing preimage produced a size estimate");
        auto partial = missing.Sign(alice, [](const auto&) { return true; });
        Check(partial && !partial->complete && partial->added_signatures > 0, "Missing preimage prevented partial signing");
        if (kind == "sha256") psbt.inputs[0].sha256_preimages[hash32] = preimage;
        else if (kind == "hash256") psbt.inputs[0].hash256_preimages[hash32] = preimage;
        else if (kind == "ripemd160") psbt.inputs[0].ripemd160_preimages[hash20] = preimage;
        else psbt.inputs[0].hash160_preimages[hash20] = preimage;
        td::ReviewedTransaction complete(wallet.Make(), alice, wallet.Tag(alice), Serialize(psbt));
        auto result = complete.Sign(alice, [](const auto&) { return true; });
        Check(result && result->complete, "Hashlocked transaction incomplete");
        Verify(*result);
        std::printf("PASS: %s with and without a preimage\n", kind.c_str());
    }
}

void Adversarial(const td::Keys& alice, const td::Keys& bob, const CScript& destination)
{
    Wallet wallet{"", "wpkh(@0/**)", {Key(alice, 84)}};
    auto psbt = Fixture(wallet, destination);
    const auto tag = wallet.Tag(alice);
    const auto prepare = [&](const PartiallySignedTransaction& request) {
        return td::ReviewedTransaction(wallet.Make(), alice, tag, Serialize(request));
    };
    const auto tamper = [&](const std::function<void(PartiallySignedTransaction&)>& change) {
        auto altered = psbt;
        change(altered);
        const auto calls = signature_calls;
        Reject([&] { prepare(altered); });
        Check(signature_calls == calls, "Rejected input triggered signing");
    };
    tamper([](auto& p) { p.inputs[0].non_witness_utxo.reset(); });
    tamper([](auto& p) { p.inputs[0].witness_utxo.nValue += 1; });
    tamper([](auto& p) { p.tx->vin[0].prevout.n = 99; });
    tamper([](auto& p) { p.tx->vin[0].prevout.hash.SetNull(); });
    tamper([](auto& p) { p.inputs[0].sighash_type = SIGHASH_NONE; });
    tamper([](auto& p) { p.inputs[0].sighash_type = SIGHASH_ALL | SIGHASH_ANYONECANPAY; });
    tamper([](auto& p) { p.inputs[0].hd_keypaths.clear(); });
    tamper([](auto& p) { p.inputs[0].hd_keypaths.begin()->second.path.back() += 1; });
    tamper([](auto& p) { p.inputs[0].redeem_script = CScript{} << OP_TRUE; });
    tamper([](auto& p) { p.inputs[0].witness_script = CScript{} << OP_TRUE; });
    tamper([](auto& p) { p.tx->vout[0].nValue = COIN + 1; });
    tamper([](auto& p) { p.tx->vout[0].nValue = -1; });
    tamper([](auto& p) { p.tx->vout[0].nValue = MAX_MONEY + 1; });
    tamper([](auto& p) {
        CMutableTransaction previous{*p.inputs[0].non_witness_utxo};
        previous.vout[0].nValue = -2;
        p.inputs[0].non_witness_utxo = MakeTransactionRef(previous);
        p.inputs[0].witness_utxo = previous.vout[0];
        p.tx->vin[0].prevout.hash = p.inputs[0].non_witness_utxo->GetHash();
    });
    tamper([](auto& p) {
        CMutableTransaction previous{*p.inputs[0].non_witness_utxo};
        previous.vout[0].nValue = MAX_MONEY;
        previous.vout.push_back(previous.vout[0]);
        p.inputs[0].non_witness_utxo = MakeTransactionRef(previous);
        p.inputs[0].witness_utxo = previous.vout[0];
        p.tx->vin[0].prevout.hash = p.inputs[0].non_witness_utxo->GetHash();
        p.tx->vin.emplace_back(COutPoint{p.inputs[0].non_witness_utxo->GetHash(), 1});
        p.inputs.push_back(p.inputs[0]);
    });
    tamper([](auto& p) { p.tx->vin.push_back(p.tx->vin[0]); p.inputs.push_back(p.inputs[0]); });
    tamper([](auto& p) { p.inputs[0].partial_sigs[CKeyID{}] = {CPubKey{}, {}}; });
    tamper([](auto& p) {
        const auto pubkey = p.inputs[0].hd_keypaths.begin()->first;
        p.inputs[0].partial_sigs[pubkey.GetID()] = {pubkey, std::vector<unsigned char>(80, 1)};
    });
    tamper([](auto& p) { p.inputs[0].final_script_witness.stack = {{0x01}}; });
    Reject([&] { td::ReviewedTransaction bad(wallet.Make(), bob, tag, Serialize(psbt)); });
    auto raw = Serialize(psbt);
    auto truncated = raw;
    truncated.pop_back();
    Reject([&] { td::ReviewedTransaction bad(wallet.Make(), alice, tag, truncated); });
    const std::vector<std::byte> oversized(td::ReviewedTransaction::MAX_PSBT_BYTES + 1);
    Reject([&] { td::ReviewedTransaction bad(wallet.Make(), alice, tag, oversized); });

    // A copied "change" hint must not hide an external destination.
    auto false_change = psbt;
    false_change.outputs[0] = psbt.outputs[1];
    auto honest = prepare(false_change);
    Check(!honest.Review().outputs[0].position, "False change was hidden");
    auto signed_false_change = honest.Sign(alice, [](const auto& facts) {
        return !facts.outputs[0].position && !facts.outputs[0].address.empty();
    });
    Verify(*signed_false_change);

    auto misleading_signature = psbt;
    const auto pubkey = psbt.inputs[0].hd_keypaths.begin()->first;
    // A short, correctly encoded but invalid ECDSA signature must not shrink
    // the displayed size estimate relative to Core's dummy-signature estimate.
    misleading_signature.inputs[0].partial_sigs[pubkey.GetID()] = {
        pubkey, {0x30, 0x06, 0x02, 0x01, 1, 0x02, 0x01, 1, SIGHASH_ALL}};
    auto size_review = prepare(misleading_signature);
    Check(size_review.Review().estimated_vsize == prepare(psbt).Review().estimated_vsize,
        "Untrusted signature influenced fee-rate estimate");

    auto self_payment = psbt;
    self_payment.tx->vout[0].scriptPubKey = wallet.Make().Script(0, 9);
    const auto receive_provider = wallet.Make().PublicProvider({0, 9});
    UpdatePSBTOutput(receive_provider, self_payment, 0);
    auto self_review = prepare(self_payment);
    Check(self_review.Review().outputs[0].position == td::Position{0, 9}, "Receive output mislabeled as change");
    Verify(*self_review.Sign(alice, [](const auto&) { return true; }));

    auto data_output = psbt;
    data_output.tx->vout[0] = CTxOut{0, CScript{} << OP_RETURN << std::vector<unsigned char>{1, 2, 3}};
    data_output.tx->vout[1].nValue = COIN - 1000;
    auto data_review = prepare(data_output);
    Check(data_review.Review().outputs[0].address.empty()
        && data_review.Review().outputs[0].script == data_output.tx->vout[0].scriptPubKey,
        "Raw output script missing from review");
    Verify(*data_review.Sign(alice, [](const auto&) { return true; }));

    // The caller's request buffer can change after construction without changing
    // either the reviewed fields or the transaction subsequently signed.
    td::ReviewedTransaction frozen(wallet.Make(), alice, tag, raw);
    std::fill(raw.begin(), raw.end(), std::byte{0});
    auto signed_frozen = frozen.Sign(alice, [](const auto& facts) { return facts.fee == 1000; });
    Check(CTransaction{*Decode(signed_frozen->psbt).tx}.GetHash() == CTransaction{*psbt.tx}.GetHash(), "Review was not immutable");
    Reject([&] { frozen.Sign(bob, [](const auto&) { return true; }); });
    Reject([&] { frozen.Sign(alice, {}); });
    Reject([&] {
        frozen.Sign(alice, [](const auto&) { SelectParams(ChainType::MAIN); return true; });
    });
    SelectParams(ChainType::REGTEST);
    td::ReviewedTransaction repeated(wallet.Make(), alice, tag, signed_frozen->psbt);
    Reject([&] { repeated.Sign(alice, [](const auto&) { return true; }); });

    Wallet taproot{"Tree", "tr(@0/**,pk(@1/**))", {Key(bob, 48), Key(alice, 48)}};
    auto tap = Fixture(taproot, destination);
    tap.inputs[0].non_witness_utxo.reset(); // Taproot DEFAULT binds all input amounts/scripts.
    td::ReviewedTransaction witness_only(taproot.Make(), alice, taproot.Tag(alice), Serialize(tap));
    auto result = witness_only.Sign(alice, [](const auto&) { return true; });
    Check(result && result->complete, "Witness-only Taproot spend failed");
    Verify(*result);
    auto wrong_rule = tap;
    wrong_rule.inputs[0].sighash_type = SIGHASH_ALL;
    Reject([&] { td::ReviewedTransaction bad(taproot.Make(), alice, taproot.Tag(alice), Serialize(wrong_rule)); });
    auto wrong_internal = tap;
    wrong_internal.inputs[0].m_tap_internal_key = XOnlyPubKey{alice.Derive({}).key.GetPubKey()};
    Reject([&] { td::ReviewedTransaction bad(taproot.Make(), alice, taproot.Tag(alice), Serialize(wrong_internal)); });
    auto wrong_tree = tap;
    wrong_tree.inputs[0].m_tap_merkle_root.begin()[0] ^= 1;
    Reject([&] { td::ReviewedTransaction bad(taproot.Make(), alice, taproot.Tag(alice), Serialize(wrong_tree)); });
    auto wrong_leaf = tap;
    Check(!wrong_leaf.inputs[0].m_tap_scripts.empty(), "Missing Taproot fixture leaves");
    auto node = wrong_leaf.inputs[0].m_tap_scripts.extract(wrong_leaf.inputs[0].m_tap_scripts.begin());
    node.key().first.push_back(OP_TRUE);
    wrong_leaf.inputs[0].m_tap_scripts.insert(std::move(node));
    Reject([&] { td::ReviewedTransaction bad(taproot.Make(), alice, taproot.Tag(alice), Serialize(wrong_leaf)); });
    auto wrong_control = tap;
    auto& controls = wrong_control.inputs[0].m_tap_scripts.begin()->second;
    auto control = *controls.begin();
    controls.clear();
    control[1] ^= 1;
    controls.insert(control);
    Reject([&] { td::ReviewedTransaction bad(taproot.Make(), alice, taproot.Tag(alice), Serialize(wrong_control)); });
    std::puts("PASS: malformed/tampered PSBTs, false change, immutable approval and Taproot commitment checks");
}

void ExternalInputs(const td::Keys& alice, const td::Keys& bob, const CScript& destination)
{
    Wallet first{"", "wpkh(@0/**)", {Key(alice, 84)}};
    Wallet second{"", "wpkh(@0/**)", {Key(bob, 84)}};
    auto psbt = Fixture(first, destination);
    auto previous = Funding(second.Make().Script(0, 8), COIN / 2);
    psbt.tx->vin.emplace_back(COutPoint{previous->GetHash(), 0});
    psbt.inputs.emplace_back();
    psbt.inputs[1].non_witness_utxo = previous;
    psbt.tx->vout[0].nValue += COIN / 2;
    FillInput(psbt, 1, second.Make(), {0, 8});
    td::ReviewedTransaction reviewed(first.Make(), alice, first.Tag(alice), Serialize(psbt));
    Check(reviewed.Review().unrecognized_inputs == 1 && reviewed.Review().recognized_inputs == COIN,
        "External input was claimed as owned");
    Check(reviewed.Review().fee == 1000, "External input fee mismatch");
    Check(!reviewed.Review().estimated_vsize, "Estimated an unfinished external input");
    auto partial = reviewed.Sign(alice, [](const auto&) { return true; });
    Check(partial && !partial->complete, "External input unexpectedly signed");
    Check(Decode(partial->psbt).inputs[1].partial_sigs.empty(), "External signature added");
    td::ReviewedTransaction other(second.Make(), bob, second.Tag(bob), partial->psbt);
    auto complete = other.Sign(bob, [](const auto&) { return true; });
    Check(complete && complete->complete, "External participant failed to complete");
    Verify(*complete);
    // The other participant may finalize their own input before ours is signed.
    td::ReviewedTransaction bob_first(second.Make(), bob, second.Tag(bob), Serialize(psbt));
    auto bob_partial = Decode(bob_first.Sign(bob, [](const auto&) { return true; })->psbt);
    Check(!FinalizePSBT(bob_partial) && PSBTInputSigned(bob_partial.inputs[1]), "Fixture did not finalize external input");
    td::ReviewedTransaction alice_last(first.Make(), alice, first.Tag(alice), Serialize(bob_partial));
    auto last = alice_last.Sign(alice, [](const auto&) { return true; });
    Check(last && last->complete, "Finalized external input was not preserved");
    Verify(*last);
    // Even another input's value must be authenticated in a SegWit-v0 spend.
    psbt.inputs[1].non_witness_utxo.reset();
    Reject([&] { td::ReviewedTransaction bad(first.Make(), alice, first.Tag(alice), Serialize(psbt)); });
    std::puts("PASS: external input disclosure, partial signing and authenticated fee accounting");
}
}

int main()
{
    try {
        ECC_Context context;
        SelectParams(ChainType::REGTEST);
        td::Keys alice(Bytes(MNEMONIC), Bytes("TREZOR")), bob(Bytes(OTHER), {});
        Wallet recipient{"", "wpkh(@0/**)", {Key(bob, 84, 2)}};
        const auto destination = recipient.Make().Script(0, 0);
        ScriptFamilies(alice, bob, destination);
        Adversarial(alice, bob, destination);
        ExternalInputs(alice, bob, destination);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Transaction test failed: %s\n", error.what());
        return 1;
    }
}
