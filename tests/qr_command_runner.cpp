// Development only. These fixed BIP39 test vectors are public, never real wallets.
// This executable is not installed and production has no auto-approval switch.
#include "qr_commands.h"
#include "cbor.h"

#include <chainparams.h>
#include <crypto/sha256.h>
#include <key_io.h>
#include <streams.h>
#include <univalue.h>
#include <util/strencodings.h>

#include <iostream>
#include <tuple>

namespace {
const std::string ALICE = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
const std::string BOB = "all all all all all all all all all all all all";
auto Bytes(std::string_view s) { return std::span(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }
std::vector<std::byte> Serialize(const PartiallySignedTransaction& psbt)
{
    DataStream stream; stream << psbt; return {stream.begin(), stream.end()};
}
PartiallySignedTransaction Decode(std::span<const std::byte> raw)
{
    PartiallySignedTransaction psbt;
    std::string error;
    td::Require(DecodeRawPSBT(psbt, raw, error), "Invalid fixture PSBT");
    return psbt;
}
std::string Key(const td::Keys& keys)
{
    const td::Path path{0x80000030, 0x80000001, 0x80000000, 0x80000002};
    return "[" + HexStr(keys.RootFingerprint()) + td::PathText(path).substr(1) + "]"
        + td::EncodePublic(keys.Derive(path).Neuter(), false);
}
td::Policy Wallet(const td::Keys& alice, const td::Keys& bob)
{
    // BIP341's NUMS point: no known discrete logarithm. Public child derivation
    // does not make the private scalar known. No signer owns the internal key.
    const auto point = ParseHex("0250929b74c1a04954b78b4b6035e97a5e078a5a0f28ec96d547bfee9ace803ac0");
    CExtPubKey nums{};
    nums.pubkey.Set(point.begin(), point.end());
    std::fill(nums.chaincode.begin(), nums.chaincode.end(), 0x42);
    const std::vector<uint8_t> preimage(32, 0x42);
    td::Digest hash;
    CSHA256().Write(preimage.data(), preimage.size()).Finalize(hash.data());
    return td::Policy("HTLC test", "tr(@0/**,{and_v(v:multi_a(2,@1/**,@2/**),sha256(" + HexStr(hash)
        + ")),and_v(v:pk(@1/<2;3>/*),older(144))})", {td::EncodePublic(nums, false), Key(alice), Key(bob)}, false);
}
PartiallySignedTransaction Fixture(const td::Policy& policy, unsigned count, bool refund = false)
{
    CMutableTransaction tx;
    std::vector<CTxOut> coins;
    for (unsigned i = 0; i < count; ++i) {
        uint256 id; id.begin()[0] = i + 1;
        tx.vin.emplace_back(COutPoint(Txid::FromUint256(id), 0), CScript{}, refund ? 144 : 0xfffffffd);
        coins.emplace_back(100000, policy.Script(0, i));
    }
    tx.vout.emplace_back(80000 * count, CScript{} << OP_0 << std::vector<uint8_t>(20, 0x42));
    tx.vout.emplace_back(20000 * count - 1000, policy.Script(1, 3));
    PartiallySignedTransaction psbt(tx);
    uint256 hash;
    const std::vector<uint8_t> preimage(32, 0x42);
    CSHA256().Write(preimage.data(), preimage.size()).Finalize(hash.begin());
    for (unsigned i = 0; i < count; ++i) {
        psbt.inputs[i].witness_utxo = coins[i];
        psbt.inputs[i].sha256_preimages[hash] = preimage;
    }
    const auto data = PrecomputePSBTData(psbt);
    for (unsigned i = 0; i < count; ++i)
        td::Require(SignPSBTInput(policy.PublicProvider({0, i}), psbt, i, &data, std::nullopt, nullptr, false)
            == PSBTError::INCOMPLETE, "Fixture unexpectedly signed");
    UpdatePSBTOutput(policy.PublicProvider({1, 3}), psbt, 1);
    return psbt;
}
void Verify(PartiallySignedTransaction psbt)
{
    td::Require(FinalizePSBT(psbt), "Fixture could not finalize");
    const auto data = PrecomputePSBTData(psbt);
    for (size_t i = 0; i < psbt.inputs.size(); ++i)
        td::Require(PSBTInputSignedAndVerified(psbt, i, &data), "Core rejected the signed fixture");
}
td::CborWriter Request(unsigned operation)
{
    td::CborWriter out;
    out.Array(5); out.UInt(3); out.Bytes(std::array<uint8_t, 16>{}); out.Text("regtest");
    out.UInt(operation);
    return out;
}
void Policy(td::CborWriter& out, const td::Policy& policy)
{
    out.Array(3); out.Text(policy.Name()); out.Text(policy.Template());
    out.Array(policy.KeyInformation().size());
    for (const auto& key : policy.KeyInformation()) out.Text(key.text);
}
unsigned Status(const td::QRMessage& response)
{
    const auto bytes = td::UnwrapBytes(response.cbor);
    td::CborReader in(bytes);
    in.Tuple(8); in.UInt(); in.Bytes(16); in.Text(16); in.Bytes(4); in.Text(32); in.UInt();
    return in.UInt();
}
void Tests(const td::Keys& alice, const td::Keys& bob)
{
    const auto allow = [](const auto&) { return true; };
    const auto deny = [](const auto&) { return false; };
    auto policy = Wallet(alice, bob);
    td::Require(HexStr(policy.ID()) == "d4a3cc4c6b38a893fa0d41c51edfd6503e41354ded8739bfa308ef7e63ca573b", "Wallet ID vector changed");
    td::Require(HexStr(alice.RegistrationTag(policy.ID())) == "7015c6bbdd62f432537186226d3cdc9370248052db92fb02574145a40c7d7f3f", "Alice proof vector changed");
    td::Require(HexStr(bob.RegistrationTag(policy.ID())) == "e82489f38c231f88cd956770b85604d51ec185f92b9203b9f2f7d5d043cbf26c", "Bob proof vector changed");
    for (unsigned n : {1, 5, 10}) {
        auto psbt = Fixture(policy, n);
        const auto raw = Serialize(psbt);
        auto req = Request(4); req.Array(3); Policy(req, policy);
        req.Bytes(alice.RegistrationTag(policy.ID()));
        req.Bytes({reinterpret_cast<const uint8_t*>(raw.data()), raw.size()});
        td::Require(Status(td::HandleQRRequest({"bytes", td::CborBytes(req.data)}, alice, {deny, deny, deny, deny})) == 1,
            "Refusal did not return a refusal");
        const auto unexpected = [](const auto&) { throw std::runtime_error("Wrong seed reached approval"); return true; };
        td::Require(Status(td::HandleQRRequest({"bytes", td::CborBytes(req.data)}, bob, {unexpected, unexpected, unexpected, unexpected})) == 2,
            "Another cosigner's proof accepted");
        auto first = td::ReviewedTransaction(Wallet(alice, bob), alice, alice.RegistrationTag(policy.ID()), raw).Sign(alice, allow);
        td::Require(first && !first->complete && first->added_signatures > 0, "Expected partial signature");
        auto second = td::ReviewedTransaction(Wallet(alice, bob), bob, bob.RegistrationTag(policy.ID()), first->psbt).Sign(bob, allow);
        td::Require(second && second->complete, "Expected complete claim");
        Verify(Decode(second->psbt));
        psbt.inputs[0].sha256_preimages.clear();
        auto missing = td::ReviewedTransaction(Wallet(alice, bob), alice, alice.RegistrationTag(policy.ID()), Serialize(psbt)).Sign(alice, allow);
        auto partial = td::ReviewedTransaction(Wallet(alice, bob), bob, bob.RegistrationTag(policy.ID()), missing->psbt).Sign(bob, allow);
        td::Require(partial && !partial->complete, "Missing preimage completed claim");
        auto restored = Decode(partial->psbt);
        restored.inputs[0].sha256_preimages = Fixture(policy, n).inputs[0].sha256_preimages;
        Verify(restored); // No new signatures needed when the preimage arrives.
    }
    auto refund = Fixture(policy, 1, true);
    refund.inputs[0].sha256_preimages.clear();
    const auto signed_refund = td::ReviewedTransaction(Wallet(alice, bob), alice, alice.RegistrationTag(policy.ID()), Serialize(refund)).Sign(alice, allow);
    td::Require(signed_refund && signed_refund->complete, "Refund did not complete");
    Verify(Decode(signed_refund->psbt));
    auto req = Request(2); req.Array(1); Policy(req, policy);
    size_t approvals = 0;
    const auto spy = [&](const auto&) { ++approvals; return true; };
    const td::QRApproval callbacks{spy, spy, spy, spy};
    for (size_t length = 0; length < req.data.size(); ++length) {
        auto raw = req.data; raw.resize(length);
        std::optional<unsigned> status;
        try { status = Status(td::HandleQRRequest({"bytes", td::CborBytes(raw)}, alice, callbacks)); }
        catch (const std::invalid_argument&) {}
        td::Require(!status || *status == 2, "Truncation accepted");
    }
    td::Require(approvals == 0, "Malformed request reached approval");
    req.data.push_back(0);
    td::Require(Status(td::HandleQRRequest({"bytes", td::CborBytes(req.data)}, alice, callbacks)) == 2 && approvals == 0, "Trailing data accepted");
    std::cout << "PASS: CBOR truncation, seed-bound proofs, refusal, two-signer HTLC claim/refund and delayed preimages\n";
}
void Fixtures(const td::Keys& alice, const td::Keys& bob)
{
    const auto policy = Wallet(alice, bob);
    UniValue result(UniValue::VOBJ), keys(UniValue::VARR), rows(UniValue::VARR);
    result.pushKV("name", policy.Name()); result.pushKV("template", policy.Template());
    result.pushKV("descriptor", policy.DescriptorText()); result.pushKV("wallet_id", HexStr(policy.ID()));
    for (const auto& key : policy.KeyInformation()) keys.push_back(key.text);
    result.pushKV("keys", keys);
    result.pushKV("alice_fingerprint", HexStr(alice.RootFingerprint())); result.pushKV("bob_fingerprint", HexStr(bob.RootFingerprint()));
    result.pushKV("alice_proof", HexStr(alice.RegistrationTag(policy.ID()))); result.pushKV("bob_proof", HexStr(bob.RegistrationTag(policy.ID())));
    for (unsigned n : {1, 5, 10}) {
        const auto raw = Serialize(Fixture(policy, n));
        const auto first = td::ReviewedTransaction(Wallet(alice, bob), alice, alice.RegistrationTag(policy.ID()), raw).Sign(alice, [](const auto&) { return true; });
        const auto second = td::ReviewedTransaction(Wallet(alice, bob), bob, bob.RegistrationTag(policy.ID()), first->psbt).Sign(bob, [](const auto&) { return true; });
        UniValue row(UniValue::VOBJ);
        row.pushKV("inputs", n); row.pushKV("psbt_hex", HexStr(raw));
        row.pushKV("bytes", raw.size()); row.pushKV("partial_bytes", first->psbt.size()); row.pushKV("signed_bytes", second->psbt.size());
        row.pushKV("alice_signatures", first->added_signatures); row.pushKV("bob_signatures", second->added_signatures);
        for (const auto& [name, signer, input] : {std::tuple{"alice", &alice, &raw}, std::tuple{"bob", &bob, &first->psbt}}) {
            auto request = Request(4); request.Array(3); Policy(request, policy);
            request.Bytes(signer->RegistrationTag(policy.ID()));
            request.Bytes({reinterpret_cast<const uint8_t*>(input->data()), input->size()});
            const td::QRMessage message{"bytes", td::CborBytes(request.data)};
            const auto allow = [](const auto&) { return true; };
            const auto reply = td::HandleQRRequest(message, *signer, {allow, allow, allow, allow});
            row.pushKV(std::string(name) + "_request_bytes", message.cbor.size());
            row.pushKV(std::string(name) + "_reply_bytes", reply.cbor.size());
            row.pushKV(std::string(name) + "_request_frames", td::URSender(message).Parts());
            row.pushKV(std::string(name) + "_reply_frames", td::URSender(reply).Parts());
        }
        rows.push_back(row);
    }
    result.pushKV("transactions", rows);
    std::cout << result.write() << '\n';
}
}

int main(int argc, char** argv)
{
    try {
        ECC_Context context; SelectParams(ChainType::REGTEST);
        td::Keys alice(Bytes(ALICE), {}), bob(Bytes(BOB), {});
        const std::string mode = argc == 2 ? argv[1] : "--test";
        if (mode == "--test") { Tests(alice, bob); return 0; }
        if (mode == "--fixtures") { Fixtures(alice, bob); return 0; }
        if (mode == "--verify" || mode == "--preimage") {
            std::string line; std::getline(std::cin, line);
            td::Require(line.size() <= 4 * 1024 * 1024 && IsHex(line), "Invalid test PSBT");
            const auto raw = ParseHex(line);
            auto psbt = Decode(std::as_bytes(std::span(raw)));
            if (mode == "--preimage") {
                const std::vector<uint8_t> preimage(32, 0x42);
                uint256 hash; CSHA256().Write(preimage.data(), preimage.size()).Finalize(hash.begin());
                for (auto& input : psbt.inputs) input.sha256_preimages[hash] = preimage;
                std::cout << HexStr(Serialize(psbt)) << '\n'; return 0;
            }
            Verify(psbt);
            std::cout << "Core verified every input\n"; return 0;
        }
        const bool qr = mode == "--qr-alice" || mode == "--qr-bob";
        td::Require(qr || mode == "--alice" || mode == "--bob" || mode == "--decline", "Unknown public-fixture mode");
        const auto& keys = mode == "--bob" || mode == "--qr-bob" ? bob : alice;
        const auto approve = [&](const auto&) { return mode != "--decline"; };
        td::URReceiver receiver;
        std::string line;
        while (std::getline(std::cin, line)) {
            if (qr) {
                receiver.Receive(line);
                if (!receiver.Result()) continue;
                const auto reply = td::HandleQRRequest(*receiver.Result(), keys, {approve, approve, approve, approve});
                td::URSender sender(reply);
                for (size_t i = 0; i < sender.Parts(); ++i) std::cout << sender.Next() << '\n';
                std::cout << std::endl; return 0;
            }
            td::Require(line.size() <= 2 * (1024 * 1024 + 65536) && IsHex(line), "Invalid test request");
            const auto reply = td::HandleQRRequest({"bytes", td::CborBytes(ParseHex(line))}, keys, {approve, approve, approve, approve});
            std::cout << HexStr(td::UnwrapBytes(reply.cbor)) << std::endl;
        }
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
