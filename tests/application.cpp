#include "application.h"
#include "hardware.h"

#include <chainparams.h>
#include <key_io.h>
#include <linux/videodev2.h>
#include <streams.h>
#include <univalue.h>
#include <util/strencodings.h>

#include <cstdio>
#include <stdexcept>

namespace {
const std::string MNEMONIC = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
auto Bytes(std::string_view s) { return std::span(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Reject(const std::function<void()>& operation)
{
    try { operation(); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid application request accepted");
}
td::QRMessage Message(const std::string& json) { return {"bytes", td::CborBytes(Bytes(json))}; }

UniValue Envelope(const td::Policy& policy)
{
    UniValue wallet(UniValue::VOBJ), keys(UniValue::VARR), root(UniValue::VOBJ);
    wallet.pushKV("name", policy.Name());
    wallet.pushKV("template", policy.Template());
    for (const auto& key : policy.KeyInformation()) keys.push_back(key.text);
    wallet.pushKV("keys", keys);
    root.pushKV("version", 1);
    root.pushKV("command", "REGISTER_WALLET");
    root.pushKV("network", "regtest");
    root.pushKV("wallet", wallet);
    return root;
}

void Registration(const td::Keys& keys)
{
    auto base = td::DefaultPolicy(keys, 84, 0);
    td::Policy policy("Savings", base.Template(), {base.KeyInformation()[0].text}, false);
    auto root = Envelope(policy);
    auto request = td::ParseRequest(Message(root.write()));
    Check(request.registration && request.policy.ID() == policy.ID(), "Policy request changed identity");
    bool called = false;
    Check(!td::Register(policy, keys, [&](const auto& lines) { called = !lines.empty(); return false; }), "Declined registration exported proof");
    Check(called, "Approval was not requested");
    const auto id = policy.ID();
    auto response = td::Register(policy, keys, [&](const auto& lines) {
        Check(std::find(lines.begin(), lines.end(), "Wallet: Savings") != lines.end(), "Wrong displayed policy");
        // The caller can replace its policy object; approval must still bind to
        // the original reviewed ID rather than recompute it after the callback.
        policy = td::Policy("Changed", base.Template(), {base.KeyInformation()[0].text}, false);
        return true;
    });
    const auto payload = td::UnwrapBytes(response->cbor);
    UniValue reply;
    Check(reply.read(std::string_view(reinterpret_cast<const char*>(payload.data()), payload.size())), "Invalid registration response");
    Check(reply["wallet_id"].get_str() == HexStr(id)
        && reply["wallet_hmac"].get_str() == HexStr(keys.RegistrationTag(id)), "Proof did not bind to approved policy");
    td::Keys other(Bytes(MNEMONIC), Bytes("another seed"));
    Reject([&] { td::Register(policy, other, [](const auto&) { throw std::runtime_error("Unexpected approval"); return true; }); });
    Reject([&] { td::Register(policy, keys, {}); });
    auto bad = root;
    bad.pushKV("version", 2);
    Reject([&] { td::ParseRequest(Message(bad.write())); });
    bad = root; bad.pushKV("network", "main");
    Reject([&] { td::ParseRequest(Message(bad.write())); });
    bad = root; bad.pushKV("unexpected", true);
    Reject([&] { td::ParseRequest(Message(bad.write())); });
    bad = root; bad.pushKV("command", "EXPORT_SEED");
    Reject([&] { td::ParseRequest(Message(bad.write())); });
    auto duplicate = root.write(); duplicate.insert(1, "\"version\":1,");
    Reject([&] { td::ParseRequest(Message(duplicate)); });
    Reject([&] { td::ParseRequest(Message("{\"version\":1e0}")); });
    Reject([&] { td::ParseRequest(Message(std::string(1536 * 1024 + 1, ' '))); });
    Reject([&] { td::Wrap({"Wallet\033[2Jhidden"}, 79); });
    std::puts("PASS: strict request schema, registration consent and immutable approved wallet ID");
}

void Signing(const td::Keys& keys)
{
    auto base = td::DefaultPolicy(keys, 84, 0);
    td::Policy policy("Savings", base.Template(), {base.KeyInformation()[0].text}, false);
    CMutableTransaction funding;
    uint256 prev_hash; prev_hash.begin()[0] = 1;
    funding.vin.emplace_back(COutPoint(Txid::FromUint256(prev_hash), 0));
    funding.vout.emplace_back(100000, policy.Script(0, 0));
    auto previous = MakeTransactionRef(funding);
    CMutableTransaction tx;
    tx.vin.emplace_back(COutPoint(previous->GetHash(), 0));
    tx.vout.emplace_back(99000, policy.Script(1, 1));
    PartiallySignedTransaction psbt(tx);
    psbt.inputs[0].non_witness_utxo = previous;
    auto txdata = PrecomputePSBTData(psbt);
    Check(SignPSBTInput(policy.PublicProvider({0, 0}), psbt, 0, &txdata, SIGHASH_ALL, nullptr, false)
        == PSBTError::INCOMPLETE, "Public fixture provider unexpectedly signed");
    UpdatePSBTOutput(policy.PublicProvider({1, 1}), psbt, 0);
    DataStream stream; stream << psbt;
    auto root = Envelope(policy);
    root.pushKV("command", "SIGN_PSBT");
    root.pushKV("wallet_hmac", HexStr(keys.RegistrationTag(policy.ID())));
    root.pushKV("psbt", EncodeBase64(std::span(reinterpret_cast<const uint8_t*>(stream.data()), stream.size())));
    const auto message = Message(root.write());
    Check(!td::Sign(td::ParseRequest(message), keys, [](const auto&) { return false; }), "Declined transaction exported signature");
    const auto signed_message = td::Sign(td::ParseRequest(message), keys, [&](const auto& review) {
        const auto lines = td::TransactionLines(review);
        Check(std::find(lines.begin(), lines.end(), "Transaction fee: 0.00001 BTC (1000 sats)") != lines.end(), "Fee missing from review");
        Check(std::find(lines.begin(), lines.end(), "VERIFIED CHANGE") != lines.end(), "Change classification missing");
        Check(std::find(lines.begin(), lines.end(), review.outputs[0].address) != lines.end(), "Complete output address missing");
        Check(std::find(lines.begin(), lines.end(), previous->GetHash().ToString() + ":0") != lines.end(), "Input txid was truncated");
        Check(std::find(lines.begin(), lines.end(), "Signing rule: ALL") != lines.end(), "Signing rule missing");
        return true;
    });
    Check(signed_message && signed_message->type == "crypto-psbt", "Wrong signed response type");
    const auto raw = td::UnwrapBytes(signed_message->cbor);
    PartiallySignedTransaction result;
    std::string error;
    Check(DecodeRawPSBT(result, std::as_bytes(std::span(raw)), error) && FinalizePSBT(result), "Signed response cannot finalize");
    txdata = PrecomputePSBTData(result);
    Check(PSBTInputSignedAndVerified(result, 0, &txdata), "Signed response is invalid");
    root.pushKV("wallet_hmac", std::string(64, '0'));
    Reject([&] { td::Sign(td::ParseRequest(Message(root.write())), keys, [](const auto&) { throw std::runtime_error("Unexpected approval"); return true; }); });
    root.pushKV("psbt", "cHNidP8= ");
    Reject([&] { td::ParseRequest(Message(root.write())); });
    std::puts("PASS: policy request -> complete review -> approval -> independently verified signed PSBT response");
}

void CameraFrames()
{
    const std::vector<uint8_t> rgb{255, 0, 0, 0, 255, 0, 17, 19, 0, 0, 255, 255, 255, 255};
    Check(td::Grayscale(rgb, 2, 2, 8, V4L2_PIX_FMT_RGB24) == std::vector<uint8_t>({76, 149, 28, 255}), "RGB padding was interpreted as pixels");
    Check(td::Grayscale(rgb, 2, 2, 8, V4L2_PIX_FMT_BGR24) == std::vector<uint8_t>({28, 149, 76, 255}), "BGR channels swapped");
    const std::vector<uint8_t> yuyv{30, 128, 240, 128, 17, 19, 50, 128, 100, 128};
    Check(td::Grayscale(yuyv, 2, 2, 6, V4L2_PIX_FMT_YUYV) == std::vector<uint8_t>({30, 240, 50, 100}), "YUYV luminance extraction failed");
    Reject([&] { td::Grayscale(rgb, 2, 2, 5, V4L2_PIX_FMT_RGB24); });
    Reject([&] { td::Grayscale(std::span(rgb).first(13), 2, 2, 8, V4L2_PIX_FMT_RGB24); });
    Reject([&] { td::Grayscale(yuyv, 3, 1, 6, V4L2_PIX_FMT_YUYV); });
    Reject([&] { td::Grayscale(rgb, 0xffffffff, 0xffffffff, 0xffffffff, V4L2_PIX_FMT_RGB24); });
    std::puts("PASS: webcam row padding, RGB/BGR/YUYV conversion and malformed frame bounds");
}

void ExportVectors(const td::Keys& keys)
{
    UniValue vectors(UniValue::VARR);
    for (const auto network : {ChainType::MAIN, ChainType::REGTEST}) {
        SelectParams(network);
        for (const unsigned purpose : {44, 49, 84, 86}) {
            auto policy = td::DefaultPolicy(keys, purpose, 7);
            const auto& info = policy.KeyInformation()[0];
            UniValue row(UniValue::VOBJ);
            row.pushKV("network", ChainTypeToString(network));
            row.pushKV("purpose", purpose);
            row.pushKV("key", td::EncodePublic(info.key, network == ChainType::MAIN));
            row.pushKV("fingerprint", HexStr(info.fingerprint));
            row.pushKV("path", td::PathText(info.origin));
            row.pushKV("cbor", HexStr(td::PublicAccount(policy, keys).cbor));
            vectors.push_back(row);
        }
    }
    std::puts(vectors.write().c_str());
}
}

int main(int argc, char** argv)
{
    try {
        ECC_Context context;
        SelectParams(ChainType::REGTEST);
        td::Keys keys(Bytes(MNEMONIC), {});
        if (argc == 2 && std::string_view(argv[1]) == "--export-vectors") { ExportVectors(keys); return 0; }
        Registration(keys); Signing(keys); CameraFrames();
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
