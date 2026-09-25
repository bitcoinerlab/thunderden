#include "application.h"
#include "camera.h"
#include "hardware.h"
#include "qr_commands.h"
#include "cbor.h"

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
td::QRMessage Message(std::span<const uint8_t> bytes) { return {"bytes", td::CborBytes(bytes)}; }

td::CborWriter Envelope(const td::Policy& policy, unsigned operation, const std::string& network = "regtest")
{
    td::CborWriter out;
    out.Array(5); out.UInt(3); out.Bytes(std::array<uint8_t, 16>{}); out.Text(network);
    out.UInt(operation); out.Array(operation == 2 ? 1 : 3);
    out.Array(3); out.Text(policy.Name()); out.Text(policy.Template());
    out.Array(policy.KeyInformation().size());
    for (const auto& key : policy.KeyInformation()) out.Text(key.text);
    return out;
}

unsigned Header(td::CborReader& in)
{
    in.Tuple(8); Check(in.UInt() == 3, "Wrong reply version"); in.Bytes(16); in.Text(16);
    in.Bytes(4); Check(in.Text(32) == "0.0.1", "Wrong application version"); in.UInt();
    return in.UInt();
}

unsigned Status(const td::QRMessage& message)
{
    const auto raw = td::UnwrapBytes(message.cbor);
    td::CborReader in(raw); return Header(in);
}

void Registration(const td::Keys& keys)
{
    auto base = td::DefaultPolicy(keys, 84, 0);
    td::Policy policy("Savings", base.Template(), {base.KeyInformation()[0].text}, false);
    auto root = Envelope(policy, 2);
    bool called = false;
    Check(Status(td::HandleQRRequest(Message(root.data), keys, {{}, [&](const auto& lines) { called = !lines.empty(); return false; }, {}, {}})) == 1,
        "Declined registration exported proof");
    Check(called, "Approval was not requested");
    const auto id = policy.ID();
    auto response = td::HandleQRRequest(Message(root.data), keys, {{}, [&](const auto& lines) {
        Check(std::find(lines.begin(), lines.end(), "Wallet: Savings") != lines.end(), "Wrong displayed policy");
        // The caller can replace its policy object; approval must still bind to
        // the original reviewed ID rather than recompute it after the callback.
        policy = td::Policy("Changed", base.Template(), {base.KeyInformation()[0].text}, false);
        return true;
    }, {}, {}});
    const auto payload = td::UnwrapBytes(response.cbor);
    td::CborReader reply(payload);
    Check(Header(reply) == 0, "Registration failed"); reply.Tuple(2);
    Check(HexStr(reply.Bytes(32)) == HexStr(id) && HexStr(reply.Bytes(32)) == HexStr(keys.RegistrationTag(id)),
        "Proof did not bind to approved policy");
    reply.End();
    td::Keys other(Bytes(MNEMONIC), Bytes("another seed"));
    const td::QRApproval never{{}, [](const auto&) { throw std::runtime_error("Unexpected approval"); return true; }, {}, {}};
    Check(Status(td::HandleQRRequest(Message(Envelope(policy, 2).data), other, never)) == 2, "Unowned wallet approved");
    // A claimed matching fingerprint is not ownership: the derived xpub must match.
    auto foreign_key = td::DefaultPolicy(other, 84, 0).KeyInformation()[0].text;
    foreign_key.replace(1, 8, HexStr(keys.RootFingerprint()));
    td::Policy spoofed("Spoofed origin", base.Template(), {foreign_key}, false);
    Check(spoofed.OwnedKeys(keys).empty(), "Fingerprint alone established ownership");
    Check(Status(td::HandleQRRequest(Message(Envelope(spoofed, 2).data), keys, never)) == 2, "Spoofed origin approved");
    Check(Status(td::HandleQRRequest(Message(root.data), keys, {})) == 2, "Missing approval accepted");
    auto bad = root.data; bad[1] = 2;
    Reject([&] { td::HandleQRRequest(Message(bad), keys, never); });
    Check(Status(td::HandleQRRequest(Message(Envelope(policy, 2, "main").data), keys, never)) == 4, "Wrong network accepted");
    Check(Status(td::HandleQRRequest(Message(Envelope(policy, 99).data), keys, never)) == 5, "Unknown operation accepted");
    bad = root.data; bad.push_back(0);
    Check(Status(td::HandleQRRequest(Message(bad), keys, never)) == 2, "Trailing field accepted");
    bad = root.data; bad.insert(bad.begin() + 1, 0x18);
    Reject([&] { td::HandleQRRequest(Message(bad), keys, never); });
    Reject([&] { td::HandleQRRequest(Message(Bytes("{\"version\":1}")), keys, never); });
    Reject([&] { td::HandleQRRequest(Message(std::vector<uint8_t>(1024 * 1024 + 65537)), keys, never); });
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
    auto root = Envelope(policy, 4);
    root.Bytes(keys.RegistrationTag(policy.ID()));
    root.Bytes({reinterpret_cast<const uint8_t*>(stream.data()), stream.size()});
    const auto message = Message(root.data);
    Check(Status(td::HandleQRRequest(message, keys, {{}, {}, {}, [](const auto&) { return false; }})) == 1, "Declined transaction exported signature");
    const auto signed_message = td::HandleQRRequest(message, keys, {{}, {}, {}, [&](const auto& review) {
        const auto lines = td::TransactionLines(review);
        Check(std::find(lines.begin(), lines.end(), "Transaction fee: 0.00001 BTC (1000 sats)") != lines.end(), "Fee missing from review");
        Check(std::find(lines.begin(), lines.end(), "VERIFIED CHANGE") != lines.end(), "Change classification missing");
        Check(std::find(lines.begin(), lines.end(), review.outputs[0].address) != lines.end(), "Complete output address missing");
        Check(std::find(lines.begin(), lines.end(), previous->GetHash().ToString() + ":0") != lines.end(), "Input txid was truncated");
        Check(std::find(lines.begin(), lines.end(), "Signing rule: ALL") != lines.end(), "Signing rule missing");
        return true;
    }});
    Check(signed_message.type == "bytes", "Wrong signed response type");
    const auto payload = td::UnwrapBytes(signed_message.cbor);
    td::CborReader reply(payload);
    Check(Header(reply) == 0, "Signing failed"); reply.Tuple(3);
    const auto raw = reply.Bytes(2 * 1024 * 1024);
    PartiallySignedTransaction result;
    std::string error;
    Check(DecodeRawPSBT(result, std::as_bytes(std::span(raw)), error) && FinalizePSBT(result), "Signed response cannot finalize");
    txdata = PrecomputePSBTData(result);
    Check(PSBTInputSignedAndVerified(result, 0, &txdata), "Signed response is invalid");
    Check(reply.UInt() == 1 && reply.UInt() == 1, "Wrong signature count/completion"); reply.End();
    root = Envelope(policy, 4); root.Bytes(td::Digest{});
    root.Bytes({reinterpret_cast<const uint8_t*>(stream.data()), stream.size()});
    Check(Status(td::HandleQRRequest(Message(root.data), keys, {{}, {}, {}, [](const auto&) {
        throw std::runtime_error("Unexpected approval"); return true;
    }})) == 2, "Wrong proof accepted");
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
            row.pushKV("cbor", HexStr(td::PublicHDKey(keys, info.origin).cbor));
            row.pushKV("descriptor_cbor", HexStr(td::PublicDescriptor(policy).cbor));
            row.pushKV("descriptor", policy.DescriptorText());
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
