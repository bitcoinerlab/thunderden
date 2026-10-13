#include "address_check.h"
#include "camera.h"
#include "multisig_fixture.h"
#include "qr.h"
#include <key_io.h>
#include <secp256k1.h>
#include <secp256k1_schnorrsig.h>
#include <univalue.h>
#include <algorithm>
#include <chrono>
#include <cstdio>

extern "C" int __wrap_secp256k1_ecdsa_sign(const secp256k1_context*, secp256k1_ecdsa_signature*,
    const unsigned char*, const unsigned char*, secp256k1_nonce_function, const void*)
{ throw std::runtime_error("Address verification attempted ECDSA signing"); }
extern "C" int __wrap_secp256k1_schnorrsig_sign32(const secp256k1_context*, unsigned char*,
    const unsigned char*, const secp256k1_keypair*, const unsigned char*)
{ throw std::runtime_error("Address verification attempted Schnorr signing"); }

namespace {
auto Bytes(std::string_view s) { return std::span(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Reject(const std::function<void()>& action)
{
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid address check accepted");
}
std::string Address(const td::Policy& policy, unsigned branch, uint32_t index)
{
    CTxDestination destination;
    Check(ExtractDestination(policy.Script(branch, index), destination), "Fixture has no address");
    return EncodeDestination(destination);
}

void Fixtures(const td::Keys& keys, const td::Keys& other)
{
    SelectParams(ChainType::REGTEST);
    UniValue out(UniValue::VOBJ), cases(UniValue::VARR);
    out.pushKV("network", "Regtest"); out.pushKV("words", fixture::WORDS); out.pushKV("passphrase", "");
    out.pushKV("fingerprint", HexStr(keys.RootFingerprint()));
    out.pushKV("cosigner_fingerprint", HexStr(other.RootFingerprint()));
    out.pushKV("cosigner_passphrase", "cosigner");
    td::QRScanner scanner;
    const auto add = [&](const char* id, const char* title, const char* expected, const std::string& payload) {
        td::QRImage qr(payload);
        const unsigned width = (qr.width + 8) * 5;
        std::vector<uint8_t> pixels(width * width, 255);
        UniValue row(UniValue::VOBJ), modules(UniValue::VARR);
        for (int y = 0; y < qr.width; ++y) {
            std::string bits;
            for (int x = 0; x < qr.width; ++x) {
                const bool black = qr.modules[y * qr.width + x]; bits += black ? '1' : '0';
                for (int dy = 0; dy < 5; ++dy) for (int dx = 0; dx < 5; ++dx)
                    pixels[((y + 4) * 5 + dy) * width + (x + 4) * 5 + dx] = black ? 0 : 255;
            }
            modules.push_back(bits);
        }
        const auto decoded = scanner.Scan(pixels, width, width);
        Check(decoded.size() == 1 && decoded[0] == payload, "Device fixture QR failed independent read-back");
        row.pushKV("id", id); row.pushKV("title", title); row.pushKV("expected", expected);
        row.pushKV("payload", payload); row.pushKV("modules", modules); cases.push_back(row);
    };
    const auto single = td::DefaultPolicy(keys, 84, 0);
    add("01-receiving", "Native SegWit receiving address", "Use account 0. Expect Receiving address - number 7.", Address(single, 0, 7));
    add("02-change-uri", "Change address in a payment URI", "Use account 0. Expect Change address - number 5. The amount and label are not reviewed.",
        "bitcoin:" + Address(single, 1, 5) + "?amount=0.001&label=Public%20test");
    add("03-account-seven", "Another account", "Try account 0 to see No match: 0-999, then choose another account and enter 7. Expect receiving number 0.",
        Address(td::DefaultPolicy(keys, 84, 7), 0, 0));
    add("04-later-address", "Continue or enter an exact number", "Use account 0. No match in 0-999; continue or enter 1007. Expect receiving number 1007.", Address(single, 0, 1007));
    add("05-other-wallet", "A valid address from a different seed", "Use account 0. Expect no match. Stop, resume, or check number 7; it must never show a successful match.",
        Address(td::DefaultPolicy(other, 84, 0), 0, 7));
    for (unsigned kind : {2, 1}) {
        const auto setup = fixture::Setup(kind, 2, {&keys, &other}, {fixture::Path(kind), fixture::Path(kind)});
        const auto policy = td::ImportMultisig(setup);
        td::URSender sender(setup, 2080); Check(sender.Parts() == 1, "Setup fixture needs animation");
        add(kind == 2 ? "06-native-multisig-setup" : "08-nested-multisig-setup", kind == 2 ? "Load native SegWit 2-of-2" : "Load nested SegWit 2-of-2",
            "Scan, review both test cosigner keys and type REGISTER. This loads the wallet for this session.", sender.Next());
        add(kind == 2 ? "07-native-multisig-address" : "09-nested-multisig-address", kind == 2 ? "Native multisig address" : "Ambiguous-format multisig address",
            kind == 2 ? "Before loading setup: Wallet setup needed. After setup: expect Receiving address - number 7."
                : "Choose the loaded multisig wallet. Expect Change address - number 7. Choosing single signature must not match.", Address(policy, kind == 2 ? 0 : 1, 7));
        if (kind == 2) {
            UniValue public_keys(UniValue::VARR);
            for (const auto& key : policy.KeyInformation()) public_keys.push_back(key.text);
            out.pushKV("native_multisig_keys", public_keys);
        }
    }
    add("10-nested-single", "Single signature while multisig is loaded", "With nested multisig loaded, choose Single signature and account 0. Expect receiving number 3.",
        Address(td::DefaultPolicy(keys, 49, 0), 0, 3));
    add("11-taproot", "Standard Taproot account", "Use account 0. Expect Receiving address - number 0.", Address(td::DefaultPolicy(keys, 86, 0), 0, 0));
    add("12-legacy", "Legacy single-signature account", "Use account 0. Expect Receiving address - number 0.", Address(td::DefaultPolicy(keys, 44, 0), 0, 0));
    SelectParams(ChainType::MAIN);
    add("13-wrong-network", "Wrong network", "Keep Thunder Den on Regtest. This mainnet address must be rejected for the selected network.",
        Address(td::DefaultPolicy(keys, 84, 0), 0, 0));
    SelectParams(ChainType::REGTEST);
    out.pushKV("cases", cases);
    std::puts(out.write().c_str());
}
void Tests(const td::Keys& keys, const td::Keys& other)
{
    for (const auto network : {ChainType::MAIN, ChainType::TESTNET, ChainType::TESTNET4, ChainType::SIGNET, ChainType::REGTEST}) {
        SelectParams(network);
        for (unsigned purpose : {44, 49, 84, 86}) for (unsigned account : {0, 7}) {
            const auto policy = td::DefaultPolicy(keys, purpose, account);
            for (unsigned branch : {0, 1}) for (uint32_t index : {0U, 7U, 1000U, 0x7fffffffU}) {
                const auto address = Address(policy, branch, index);
                const auto target = td::ReadAddress(Bytes(address));
                Check(td::AddressPurpose(target) == purpose, "Address chose the wrong standard script");
                Check(GetScriptForDestination(target) == policy.Script(branch, index), "Address changed its destination");
                uint32_t next = index;
                Check(td::FindAddress(policy, GetScriptForDestination(target), next, index + 1, [](auto) { return true; })
                    == td::Position{branch, index}, "Exact-index check lost a receive/change address");
                const auto uri = "BITCOIN:" + address + "?amount=0.1&label=Test%20payment&message=hello=world";
                Check(td::ReadAddress(Bytes(uri)) == target, "Payment URI changed the destination");
                td::URReceiver receiver; receiver.Receive(address);
                Check(receiver.Result()->type == "address" && td::UnwrapBytes(receiver.Result()->cbor) == std::vector<uint8_t>(address.begin(), address.end()),
                    "Scanner changed a case-sensitive address");
            }
        }
    }
    SelectParams(ChainType::MAIN);
    const auto main = td::DefaultPolicy(keys, 84, 0);
    const std::string golden = "bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu"; // Published BIP84 vector.
    Check(Address(main, 0, 0) == golden, "Independent BIP84 address vector changed");
    auto upper = golden;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](auto c) { return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c; });
    Check(td::ReadAddress(Bytes(upper)) == td::ReadAddress(Bytes(golden)), "Uppercase Bech32 address was not canonicalized");
    upper[0] = 'b'; Reject([&] { td::ReadAddress(Bytes(upper)); });
    for (const auto& text : {std::string{}, golden + "\n", golden + " ", "bitcoin://" + golden,
        "bitcoin:" + golden + "?req-test=1", "bitcoin:" + golden + "?%72eq-test=1",
        "bitcoin:" + golden + "?label=hi#fragment", "bitcoin:" + golden + "?=1",
        "bitcoin:" + golden + "?label=hi&&message=test", "bitcoin:" + golden + "?label=\033[2J",
        "https://example.org/" + golden, "bitcoin:" + golden + "?label=" + std::string(512, 'a')})
        Reject([&] { td::ReadAddress(Bytes(text)); });
    const auto prefix = "bitcoin:" + golden + "?label=";
    const auto longest = prefix + std::string(td::MAX_ADDRESS_QR - prefix.size(), 'a');
    Check(td::ReadAddress(Bytes(longest)) == td::ReadAddress(Bytes(golden)), "Maximum-size address QR was rejected");
    SelectParams(ChainType::REGTEST);
    Reject([&] { td::ReadAddress(Bytes(golden)); });
    auto policy = td::DefaultPolicy(keys, 84, 0);
    uint32_t next = 0;
    unsigned calls = 0;
    auto result = td::FindAddress(policy, policy.Script(1, 7), next, 10, [&](auto) { return ++calls != 2; });
    Check(!result && next == 0, "Stopping between branches skipped the unchecked change address");
    result = td::FindAddress(policy, policy.Script(1, 7), next, 10, [](auto) { return true; });
    Check(result == td::Position{1, 7}, "Resume did not find change");
    for (auto wrong : {td::DefaultPolicy(keys, 84, 1), td::DefaultPolicy(keys, 49, 0), td::DefaultPolicy(other, 84, 0)}) {
        next = 0;
        Check(!td::FindAddress(policy, wrong.Script(0, 7), next, 20, [](auto) { return true; }) && next == 20,
            "Search switched account, script or seed");
    }
    next = 0;
    const auto started = std::chrono::steady_clock::now();
    Check(!td::FindAddress(policy, policy.Script(0, 1007), next, 1000, [](auto) { return true; }) && next == 1000,
        "Search crossed its range boundary");
    std::printf("Single-signature 1000-index search: %.3f seconds\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
    Check(td::FindAddress(policy, policy.Script(0, 1007), next, 2000, [](auto) { return true; }) == td::Position{0, 1007},
        "Continuation skipped a matching address");
    for (const auto& [begin, end] : {std::pair{2U, 1U}, {0U, 1001U}, {0x7fffffffU, 0x80000001U}}) {
        next = begin; Reject([&] { td::FindAddress(policy, policy.Script(0, 0), next, end, [](auto) { return true; }); });
    }
    for (unsigned kind : {0, 1, 2}) {
        auto multi = td::ImportMultisig(fixture::Setup(kind, 2, {&keys, &other}, {fixture::Path(kind), fixture::Path(kind, 7)}));
        for (unsigned branch : {0, 1}) {
            const auto target = td::ReadAddress(Bytes(Address(multi, branch, 7)));
            Check(td::AddressPurpose(target) == (kind == 2 ? 0U : 49U), "Multisig script classification failed");
            next = 0;
            Check(td::FindAddress(multi, GetScriptForDestination(target), next, 10, [](auto) { return true; }) == td::Position{branch, 7},
                "Loaded multisig receive/change verification failed");
        }
    }
    std::puts("PASS: bounded address/URI parsing, script/account isolation, receive/change search, stop/resume and index boundaries");
}
}

int main(int argc, char** argv)
{
    try {
        ECC_Context context;
        td::Keys keys(Bytes(fixture::WORDS), {}), other(Bytes(fixture::WORDS), Bytes("cosigner"));
        if (argc == 2 && std::string_view(argv[1]) == "--fixtures") { Fixtures(keys, other); return 0; }
        Check(argc == 1, "Unknown address test option");
        Tests(keys, other);
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
