#include "application.h"
#include "camera.h"
#include "hardware.h"
#include "qr_commands.h"
#include "cbor.h"
#include "multisig_fixture.h"

#include <chainparams.h>
#include <key_io.h>
#include <linux/videodev2.h>
#include <script/solver.h>
#include <streams.h>
#include <univalue.h>
#include <util/strencodings.h>

#include <cstdio>
#include <stdexcept>

namespace {
const std::string MNEMONIC = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
auto Bytes(std::string_view s) { return std::span(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool Contains(const td::ReviewLines& lines, std::string_view text)
{
    return std::find(lines.begin(), lines.end(), text) != lines.end();
}
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
    out.UInt(operation); out.Array(operation == 2 ? 1 : operation == 3 ? 4 : 3);
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

unsigned Status(const td::QRReply& reply)
{
    const auto raw = td::UnwrapBytes(reply.message.cbor);
    td::CborReader in(raw);
    const auto status = Header(in);
    Check(reply.status == status, "Local result status differs from the QR reply");
    return status;
}

void Registration(const td::Keys& keys)
{
    for (const unsigned account : {0, 7}) {
        const auto standard = td::DefaultPolicy(keys, 84, account);
        const auto lines = td::PolicyReview(standard, keys);
        Check(std::find(lines.begin(), lines.end(), "Account: " + std::to_string(account)) != lines.end(),
            "Standard account review lost the actual account number");
        Check(std::find(lines.begin(), lines.end(), "Master fingerprint: " + HexStr(keys.RootFingerprint())) != lines.end(),
            "Review did not identify the master key");
        auto request = Envelope(standard, 3);
        request.Bytes(td::Digest{}); request.UInt(0); request.UInt(7);
        bool displayed = false;
        Check(Status(td::HandleQRRequest(Message(request.data), keys, {{}, {}, [&](const auto& address, const auto& details) {
            displayed = std::find(address.begin(), address.end(), "Account: " + std::to_string(account)) != address.end();
            Check(!Contains(address, "Wallet ID: " + HexStr(standard.ID())), "Address summary exposes the wallet ID");
            Check(Contains(details, "Wallet ID: " + HexStr(standard.ID())) && Contains(details, standard.DescriptorText()),
                "Address details lost the wallet ID or complete descriptor");
            return true;
        }, {}})) == 0 && displayed, "Address review lost the standard account number");
    }
    auto base = td::DefaultPolicy(keys, 84, 0);
    td::Keys cosigner(Bytes(MNEMONIC), Bytes("cosigner"));
    Reject([&] { td::PublicDescriptor(base, cosigner); });
    const auto cosigner_key = td::DefaultPolicy(cosigner, 84, 0).KeyInformation()[0].text;
    td::Policy policy("Savings", "wsh(sortedmulti(2,@0/**,@1/**))", {base.KeyInformation()[0].text, cosigner_key}, false);
    auto root = Envelope(policy, 2);
    bool called = false;
    Check(Status(td::HandleQRRequest(Message(root.data), keys, {{}, [&](const auto& lines, const auto&) { called = !lines.empty(); return false; }, {}, {}})) == 1,
        "Declined registration exported proof");
    Check(called, "Approval was not requested");
    const auto id = policy.ID();
    auto response = td::HandleQRRequest(Message(root.data), keys, {{}, [&](const auto& lines, const auto& details) {
        Check(std::find(lines.begin(), lines.end(), "Wallet: Savings") != lines.end(), "Wrong displayed policy");
        Check(!Contains(lines, "Wallet ID: " + HexStr(id)), "Registration summary exposes the wallet ID");
        Check(Contains(lines, policy.Template()), "Registration rules became optional");
        for (const auto& key : policy.KeyInformation()) Check(Contains(lines, key.text), "Registration key became optional");
        Check(Contains(lines, "Key 0 - verified local key:") && Contains(lines, "Key 1 - external key:"),
            "Registration lost verified key ownership");
        Check(Contains(details, "Wallet ID: " + HexStr(id)) && Contains(details, policy.DescriptorText()),
            "Registration details lost the wallet ID or complete descriptor");
        return true;
    }, {}, {}});
    const auto payload = td::UnwrapBytes(response.message.cbor);
    td::CborReader reply(payload);
    Check(Header(reply) == 0, "Registration failed"); reply.Tuple(2);
    Check(HexStr(reply.Bytes(32)) == HexStr(id) && HexStr(reply.Bytes(32)) == HexStr(keys.RegistrationTag(id)),
        "Proof did not bind to approved policy");
    reply.End();
    CTxDestination destination;
    Check(ExtractDestination(policy.Script(1, 7), destination), "Address fixture has no destination");
    auto address_request = Envelope(policy, 3);
    address_request.Bytes(keys.RegistrationTag(id)); address_request.UInt(1); address_request.UInt(7);
    for (const bool accept : {false, true}) {
        Check(Status(td::HandleQRRequest(Message(address_request.data), keys, {{}, {}, [&](const auto& lines, const auto& details) {
            Check(Contains(lines, "Wallet: Savings") && Contains(lines, "Network: Regtest")
                && Contains(lines, "Change address (index 7)") && Contains(lines, EncodeDestination(destination)),
                "Address summary lost the wallet, network, position or full address");
            Check(!Contains(lines, "Wallet ID: " + HexStr(id)) && !Contains(lines, policy.DescriptorText()),
                "Technical wallet identity belongs in address details");
            Check(Contains(details, "Wallet ID: " + HexStr(id)) && Contains(details, policy.DescriptorText())
                && Contains(details, policy.Template()), "Incomplete address policy details");
            for (const auto& key : policy.KeyInformation()) Check(Contains(details, key.text), "Address details lost a policy key");
            return accept;
        }, {}})) == (accept ? 0U : 1U), "Address consent was not honored");
    }
    td::Keys other(Bytes(MNEMONIC), Bytes("another seed"));
    const auto unexpected = [](const auto&...) { throw std::runtime_error("Unexpected approval"); return true; };
    const td::QRApproval never{{}, unexpected, unexpected, {}};
    address_request = Envelope(policy, 3);
    address_request.Bytes(td::Digest{}); address_request.UInt(1); address_request.UInt(7);
    Check(Status(td::HandleQRRequest(Message(address_request.data), keys, never)) == 2, "Invalid proof reached address approval");
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
    const std::string address(171, 'x');
    const auto pieces = td::Wrap({address}, 38);
    std::string restored;
    for (const auto& piece : pieces) { Check(piece.size() <= 38, "Wrapped value exceeds width"); restored += piece; }
    Check(restored == address, "Wrapping lost part of a public value");
    Check(td::Wrap({"Check the complete address before continuing."}, 20)
        == td::ReviewLines({"Check the complete", "address before", "continuing."}), "Prose was split mid-word");
    std::puts("PASS: strict request schema, registration consent and matching wallet ID/proof");
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
        const auto details = td::TransactionDetails(review);
        Check(!Contains(lines, "Wallet ID: " + HexStr(policy.ID())) && !Contains(lines, "Policy: " + policy.Template()),
            "Technical wallet identity belongs in signing details");
        Check(Contains(lines, "Wallet: Savings") && Contains(lines, "Network: Regtest"), "Signing lost wallet context");
        Check(Contains(details, "Wallet ID: " + HexStr(policy.ID())) && Contains(details, policy.Template())
            && Contains(details, policy.DescriptorText()), "Signing details lost the authenticated wallet definition");
        Check(Contains(lines, "Fee: 0.00001 BTC (1000 sats)"), "Fee missing from review");
        Check(Contains(lines, "Verified change: 0.00099 BTC (99000 sats) (1 output)"), "Change total missing");
        Check(Contains(lines, "Wallet decrease: 0.00001 BTC (1000 sats)"), "Internal transfer hid its fee");
        Check(Contains(lines, "All outputs belong to this wallet."), "Internal transfer was not identified");
        Check(!Contains(lines, review.outputs[0].address) && Contains(details, review.outputs[0].address), "Change address was not moved to Details");
        Check(Contains(details, "Change address (index 1)") && Contains(details, "Receiving address (index 0)"), "Detailed positions missing");
        Check(!Contains(lines, previous->GetHash().ToString() + ":0") && Contains(details, previous->GetHash().ToString() + ":0"), "Input outpoint misplaced or truncated");
        Check(Contains(details, "Signing rule: ALL"), "Signing rule missing from Details");
        return true;
    }});
    Check(signed_message.message.type == "bytes" && signed_message.status == 0, "Wrong signed response type or status");
    const auto payload = td::UnwrapBytes(signed_message.message.cbor);
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

UniValue Addresses(const td::Policy& policy)
{
    UniValue addresses(UniValue::VARR);
    for (unsigned branch : {0, 1}) for (unsigned index : {0, 1, 7, 1000}) {
        CTxDestination destination;
        Check(ExtractDestination(policy.Script(branch, index), destination), "No imported address");
        addresses.push_back(EncodeDestination(destination));
    }
    return addresses;
}

void PublicKeyImages(const td::Keys& keys)
{
    td::QRScanner scanner;
    for (const auto& path : std::vector<td::Path>{{}, fixture::Path(0), fixture::Path(1, 7), fixture::Path(2, 7), td::Path(32, 0xffffffffU)}) {
        const auto text = td::PublicKeyText(keys, path);
        const td::QRImage qr(text);
        const int width = (qr.width + 8) * 4;
        std::vector<uint8_t> image(width * width, 255);
        for (int y = 0; y < qr.width; ++y) for (int x = 0; x < qr.width; ++x)
            for (int dy = 0; dy < 4; ++dy) for (int dx = 0; dx < 4; ++dx)
                image[((y + 4) * 4 + dy) * width + (x + 4) * 4 + dx] = qr.modules[y * qr.width + x] ? 0 : 255;
        const auto decoded = scanner.Scan(image, width, width);
        Check(decoded == std::vector<std::string>{text}, "Static xpub QR lost case or origin information");
    }
    std::puts("PASS: case-sensitive public-key QR optical round trips, including root and maximum-depth keys");
}

void Multisig(const td::Keys& alice, bool vectors = false)
{
    td::Keys bob(Bytes(MNEMONIC), Bytes("cosigner")), carol(Bytes(MNEMONIC), Bytes("third"));
    UniValue rows(UniValue::VARR);
    for (auto network : {ChainType::MAIN, ChainType::REGTEST}) {
        SelectParams(network);
        for (unsigned kind : {0, 1, 2}) for (size_t count : {2, 3}) {
            std::vector<const td::Keys*> signers{&alice, &bob};
            if (count == 3) signers.push_back(&carol);
            std::vector<td::Path> paths;
            for (size_t i = 0; i < count; ++i) paths.push_back(fixture::Path(kind, i * 7));
            const auto qr = fixture::Setup(kind, 2, signers, paths);
            const auto policy = td::ImportMultisig(qr);
            Check(policy.OwnedKeys(alice) == std::vector<size_t>{0}, "Imported key ownership changed");
            for (size_t i = 0; i < count; ++i)
                Check(policy.KeyInformation()[i].text == td::PublicKeyText(*signers[i], paths[i]), "Import changed an origin or xpub");
            for (unsigned branch : {0, 1}) for (unsigned index : {0, 1, 7, 1000}) {
                std::vector<CPubKey> pubs;
                for (size_t i = 0; i < count; ++i) {
                    auto path = paths[i]; path.push_back(branch); path.push_back(index);
                    pubs.push_back(signers[i]->PublicAt(path).pubkey);
                }
                std::sort(pubs.begin(), pubs.end());
                auto script = GetScriptForMultisig(2, pubs);
                if (kind) script = GetScriptForDestination(WitnessV0ScriptHash(script));
                if (kind != 2) script = GetScriptForDestination(ScriptHash(script));
                Check(policy.Script(branch, index) == script, "Import changed threshold, branches or per-index key sorting");
            }
            Check(!td::ApproveWallet(policy, alice, [](const auto&, const auto&) { return false; }), "Declined setup produced a proof");
            const auto proof = td::ApproveWallet(policy, alice, [](const auto&, const auto&) { return true; });
            Check(proof && policy.Authorized(alice, *proof), "Approved setup could not authorize signing");
            auto changed = td::ImportMultisig(fixture::Setup(kind, 1, signers, paths));
            Check(!changed.Authorized(alice, *proof), "Changed threshold reused approval");
            auto bad = qr; bad.cbor.push_back(0);
            Reject([&] { td::ImportMultisig(bad); });
            bad = qr; bad.cbor.pop_back();
            Reject([&] { td::ImportMultisig(bad); });
            bad = qr; bad.cbor[2] = 0x99;
            Reject([&] { td::ImportMultisig(bad); });
            bad = qr;
            const size_t map = kind == 1 ? 9 : 6;
            bad.cbor[map] = 0xa3;
            bad.cbor.insert(bad.cbor.end(), {1, 2});
            Reject([&] { td::ImportMultisig(bad); });
            bad = qr;
            const std::array<uint8_t, 2> private_flag{2, 0xf4};
            auto flag = std::search(bad.cbor.begin(), bad.cbor.end(), private_flag.begin(), private_flag.end());
            Check(flag != bad.cbor.end(), "No private flag in fixture");
            flag[1] = 0xf5;
            Reject([&] { td::ImportMultisig(bad); });
            Reject([&] { td::ImportMultisig(fixture::Setup(kind, count + 1, signers, paths)); });
            auto wrong_paths = paths; wrong_paths[0].back() ^= 1;
            Reject([&] { td::ImportMultisig(fixture::Setup(kind, 2, signers, wrong_paths)); });
            Reject([&] { td::ImportMultisig(fixture::Setup(kind, 2, {&alice, &alice}, {paths[0], paths[0]})); });
            SelectParams(network == ChainType::MAIN ? ChainType::REGTEST : ChainType::MAIN);
            Reject([&] { td::ImportMultisig(qr); });
            SelectParams(network);
            UniValue row(UniValue::VOBJ), public_keys(UniValue::VARR), accounts(UniValue::VARR);
            for (const auto& info : policy.KeyInformation()) public_keys.push_back(info.text);
            for (size_t i = 0; i < count; ++i) {
                const auto exported = td::PublicAccount(*signers[i], paths[i]);
                td::URSender sender(exported);
                Check(sender.Parts() == 1, "Standard account export no longer fits one QR");
                UniValue account(UniValue::VOBJ);
                account.pushKV("cbor", HexStr(exported.cbor)); account.pushKV("ur", sender.Next());
                accounts.push_back(account);
            }
            row.pushKV("network", ChainTypeToString(network)); row.pushKV("kind", kind);
            row.pushKV("accounts", accounts);
            row.pushKV("keys", public_keys); row.pushKV("descriptor", policy.DescriptorText());
            row.pushKV("cbor", HexStr(qr.cbor)); row.pushKV("addresses", Addresses(policy));
            row.pushKV("psbt", HexStr(fixture::Serialize(fixture::Spend({&policy, &policy}))));
            row.pushKV("single_psbt", HexStr(fixture::Serialize(fixture::Spend({&policy}))));
            rows.push_back(row);
        }
    }
    auto spoof = fixture::Setup(2, 2, {&bob, &carol}, {fixture::Path(2), fixture::Path(2, 7)});
    td::CborWriter old_fp, new_fp;
    old_fp.UInt(ReadBE32(bob.RootFingerprint().data())); new_fp.UInt(ReadBE32(alice.RootFingerprint().data()));
    Check(old_fp.data.size() == new_fp.data.size(), "Fingerprint fixture sizes changed");
    auto fp = std::search(spoof.cbor.begin(), spoof.cbor.end(), old_fp.data.begin(), old_fp.data.end());
    Check(fp != spoof.cbor.end(), "No fingerprint in fixture");
    std::copy(new_fp.data.begin(), new_fp.data.end(), fp);
    auto foreign = td::ImportMultisig(spoof);
    Check(foreign.OwnedKeys(alice).empty(), "Spoofed QR fingerprint established key ownership");
    Reject([&] { td::ApproveWallet(foreign, alice, [](const auto&, const auto&) -> bool {
        throw std::runtime_error("Unowned QR setup reached approval");
    }); });
    for (const auto& path : std::vector<td::Path>{{}, {0x80000054U, 0x80000001U, 0x80000000U},
            {0x80000030U, 0x80000000U, 0x80000000U, 0x80000002U}, {0x80000030U, 0x80000001U, 0, 0x80000002U}})
        Reject([&] { td::PublicAccount(alice, path); });
    if (vectors) std::puts(rows.write().c_str());
    else std::puts("PASS: multisig setup origins, three script types, both branches, approval and malformed setup rejection");
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
            row.pushKV("public_key_text", td::PublicKeyText(keys, info.origin));
            row.pushKV("cbor", HexStr(td::PublicHDKey(keys, info.origin).cbor));
            const auto exported = td::PublicDescriptor(policy, keys);
            row.pushKV("descriptor_cbor", HexStr(exported.cbor));
            row.pushKV("descriptor", policy.DescriptorText());
            td::URSender sender(exported);
            Check(sender.Parts() == 1, "Standard account export no longer fits one QR");
            row.pushKV("descriptor_ur", sender.Next());
            UniValue addresses(UniValue::VARR);
            for (unsigned branch : {0, 1}) for (unsigned index : {0, 1, 7}) {
                CTxDestination destination;
                Check(ExtractDestination(policy.Script(branch, index), destination), "Export fixture has no address");
                addresses.push_back(EncodeDestination(destination));
            }
            row.pushKV("addresses", addresses);
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
        if (argc == 2 && std::string_view(argv[1]) == "--wallet-vectors") { Multisig(keys, true); return 0; }
        if (argc == 4 && std::string_view(argv[1]) == "--import") {
            SelectParams(std::string_view(argv[2]) == "main" ? ChainType::MAIN : ChainType::REGTEST);
            auto policy = td::ImportMultisig({"crypto-output", ParseHex(argv[3])});
            Check(!policy.OwnedKeys(keys).empty(), "Sparrow fixture lost local key");
            std::puts(Addresses(policy).write().c_str());
            return 0;
        }
        if (argc == 6 && std::string_view(argv[1]) == "--sign") {
            SelectParams(std::string_view(argv[2]) == "main" ? ChainType::MAIN : ChainType::REGTEST);
            Check(std::string_view(argv[5]) == "alice" || std::string_view(argv[5]) == "bob", "Unknown public fixture signer");
            td::Keys signer(Bytes(MNEMONIC), Bytes(std::string_view(argv[5]) == "bob" ? "cosigner" : ""));
            auto policy = td::ImportMultisig({"crypto-output", ParseHex(argv[3])});
            td::ApprovedWallet loaded{policy.Copy(), *td::ApproveWallet(policy, signer, [](const auto&, const auto&) { return true; })};
            const auto psbt = ParseHex(argv[4]);
            auto options = td::FindSigningWallets(std::as_bytes(std::span(psbt)), signer, &loaded);
            Check(options.wallets.size() == 1, "Sparrow PSBT did not match exactly one approved wallet");
            const auto signed_psbt = options.wallets[0].transaction->Sign(signer, [](const auto&) { return true; });
            Check(signed_psbt.has_value(), "Fixture signing declined");
            UniValue result(UniValue::VOBJ);
            result.pushKV("psbt", HexStr(signed_psbt->psbt));
            result.pushKV("complete", signed_psbt->complete);
            result.pushKV("fee_unverified", options.wallets[0].transaction->Review().fee_unverified);
            std::puts(result.write().c_str());
            return 0;
        }
        Registration(keys); Signing(keys); CameraFrames(); Multisig(keys); PublicKeyImages(keys);
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
