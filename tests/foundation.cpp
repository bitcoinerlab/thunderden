#include "keys.h"
#include "policy.h"

#include <base58.h>
#include <chainparams.h>
#include <crypto/sha256.h>
#include <script/signingprovider.h>
#include <util/strencodings.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>

namespace {
const std::string MNEMONIC = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
const std::string OTHER = "all all all all all all all all all all all all";

auto Bytes(std::string_view text)
{
    return std::span{reinterpret_cast<const unsigned char*>(text.data()), text.size()};
}

void Check(bool condition, const char* label)
{
    if (!condition) throw std::runtime_error(label);
}

void Reject(const std::function<void()>& operation)
{
    try { operation(); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid input was accepted");
}

std::string Key(const td::Keys& session, const td::Path& origin, bool mainnet = false)
{
    std::string result = "[" + HexStr(session.RootFingerprint());
    for (auto child : origin) {
        result += "/" + std::to_string(child & 0x7fffffff);
        if (child >> 31) result += 'h';
    }
    return result + "]" + td::EncodePublic(session.Derive(origin).Neuter(), mainnet);
}

void Seeds()
{
    Check(td::MnemonicWordIndex("abandon") == 0 && td::MnemonicWordIndex("zoo") == 2047,
        "BIP39 word-list endpoints");
    for (const auto& word : {std::string(""), std::string("Abandon"), std::string("aban"),
            std::string("abandon "), std::string("abandon\0", 8)}) {
        Reject([&] { td::MnemonicWordIndex(word); });
    }
    td::ValidateMnemonic(Bytes(MNEMONIC));
    const auto seed = td::MnemonicSeed(Bytes(MNEMONIC), Bytes("TREZOR"));
    Check(HexStr(seed) == "c55257c360c07c72029aebc1b53c05ed0362ada38ead3e3e9efa3708e53495531f09a6987599d18264c1e1c92f2cf141630c7a3c4ab7c81b2f001698e7463b04", "BIP39 seed vector");
    const std::vector<std::pair<std::string, std::string>> sizes{
        {"address", "fa08713f46bf5cb48728ceb70e3aae1bc53c5cb7b4e29c5610261d1cbb7be3bed4d805256fec515754d2be35974fc5da678168e9d9bb0cb70948026923b0def3"},
        {"agent", "035895f2f481b1b0f01fcf8c289c794660b289981a78f8106447707fdd9666ca06da5a9a565181599b79f53b844d8a71dd9f439c52a3d7b3e8a79c906ac845fa"},
        {"admit", "e7dadc189d2e8d07ac278d9ec98a1d2d327e4a6b7df494c00cbf2cbf2d3543dac7000fc72d4ada8d9997dc8db388ff22c6d79f604a7455f2df5534a28eee04c6"},
        {"art", "bda85446c68413707090a52022edd26a1c9462295029f2e60cd7c4f2bbd3097170af7a4d73245cafa9c3cca8d561a7c3de6f5d4a10be8ed2a5e608d68f92fcc8"}};
    unsigned count = 15;
    for (const auto& [last, expected] : sizes) {
        std::string words;
        for (unsigned i = 1; i < count; ++i) words += "abandon ";
        words += last;
        Check(HexStr(td::MnemonicSeed(Bytes(words), Bytes("TREZOR"))) == expected, "BIP39 word-count vector");
        count += 3;
    }
    Check(td::MnemonicSeed(Bytes(MNEMONIC), Bytes(" password ")) != td::MnemonicSeed(Bytes(MNEMONIC), Bytes("password")), "Passphrase spaces must survive");
    Check(td::MnemonicSeed(Bytes(MNEMONIC), Bytes("PASSWORD")) != td::MnemonicSeed(Bytes(MNEMONIC), Bytes("password")), "Passphrase case must survive");
    for (const auto& bad : std::vector<std::string>{"", "abandon", " " + MNEMONIC, MNEMONIC + " ", "abandon " + MNEMONIC,
            "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon"}) {
        Reject([&] { td::ValidateMnemonic(Bytes(bad)); });
        Reject([&] { td::MnemonicSeed(Bytes(bad), {}); });
    }
    for (const auto& bad : {std::string("caf\xc3\xa9"), std::string("tab\t"), std::string("x\0y", 3), std::string(129, 'x')}) {
        Reject([&] { td::MnemonicSeed(Bytes(MNEMONIC), Bytes(bad)); });
    }
    std::puts("PASS: BIP39 known seed, invalid input, ASCII limits and exact passphrase preservation");
}

void Policies()
{
    td::Keys session(Bytes(MNEMONIC), Bytes("TREZOR")), outsider(Bytes(OTHER), {}), wrong(Bytes(MNEMONIC), Bytes("wrong"));
    const td::Digest zero{};
    const std::array<std::pair<unsigned, std::string>, 4> defaults{{
        {44, "pkh(@0/**)"}, {49, "sh(wpkh(@0/**))"}, {84, "wpkh(@0/**)"}, {86, "tr(@0/**)"}}};
    for (const auto& [purpose, text] : defaults) {
        const td::Path path{purpose | 0x80000000U, 0x80000001U, 0x80000000U};
        const auto key = Key(session, path);
        td::Policy policy("", text, {key}, false);
        Check(policy.IsDefault(session) && policy.Authorized(session, zero), "Default authorization");
        Check(!policy.Authorized(wrong, zero), "Wrong seed authorized default");
        Check(policy.Script(0, 0) != policy.Script(1, 0), "Receive and change must differ");
        Check(!policy.Script(0, 50001).empty(), "Existing high-index input must remain derivable");
        td::Policy named("Named", text, {key}, false);
        Check(!named.IsDefault(session) && !named.Authorized(session, zero), "Named default bypass");
        Check(named.Authorized(session, session.RegistrationTag(named.ID())), "Named policy proof");
        const auto unusual = Key(session, {purpose | 0x80000000U, 0x80000001U, 0x80000065U});
        Check(!td::Policy("", text, {unusual}, false).Authorized(session, zero), "Non-default account bypass");
    }

    const auto a = Key(session, {0x80000030U, 0x80000001U, 0x80000000U, 0x80000002U});
    const auto b = Key(outsider, {0x80000030U, 0x80000001U, 0x80000000U, 0x80000002U});
    const auto c = Key(outsider, {0x80000030U, 0x80000001U, 0x80000001U, 0x80000002U});
    const std::vector<std::string> keys{a, b, c};
    const std::vector<std::pair<std::string, size_t>> valid{
        {"sh(sortedmulti(2,@0/**,@1/**))", 2},
        {"sh(wsh(sortedmulti(2,@0/**,@1/**)))", 2},
        {"wsh(sortedmulti(2,@0/**,@1/**))", 2},
        {"wsh(and_v(v:pk(@0/**),or_i(pk(@1/**),older(2))))", 2},
        {"wsh(and_v(v:pk(@0/**),older(2)))", 1},
        {"wsh(and_v(v:pk(@0/**),after(150)))", 1},
        {"tr(@0/**,{pk(@1/**),pk(@2/**)})", 3},
        {"tr(@0/**,and_v(v:pk(@1/**),pk(@2/**)))", 3},
        {"wsh(multi(2,@0/<0;1>/*,@0/<2;3>/*))", 1},
        {"wsh(and_v(v:pk(@0/**),sha256(" + std::string(64, '1') + ")))", 1},
        {"wsh(and_v(v:pk(@0/**),hash256(" + std::string(64, '1') + ")))", 1},
        {"wsh(and_v(v:pk(@0/**),ripemd160(" + std::string(40, '1') + ")))", 1},
        {"wsh(and_v(v:pk(@0/**),hash160(" + std::string(40, '1') + ")))", 1},
    };
    for (const auto& [text, count] : valid) {
        td::Policy policy("Policy", text, {keys.begin(), keys.begin() + count}, false);
        Check(policy.OwnedKeys(session) == std::vector<size_t>{0}, "Local key detection");
        const auto proof = session.RegistrationTag(policy.ID());
        Check(policy.Authorized(session, proof), "Registered policy rejected");
        auto bad_proof = proof;
        bad_proof[0] ^= 1;
        Check(!policy.Authorized(session, bad_proof), "Tampered proof accepted");
        Check(!policy.Authorized(wrong, proof), "Wrong seed proof accepted");
        td::Policy renamed("Other", text, {keys.begin(), keys.begin() + count}, false);
        Check(!renamed.Authorized(session, proof), "Renamed policy accepted");
        for (unsigned branch : {0, 1}) {
            FlatSigningProvider provider;
            std::string error;
            const auto parsed = Parse(policy.DescriptorText(branch), provider, error);
            std::vector<CScript> scripts;
            Check(parsed.size() == 1 && parsed[0]->Expand(7, provider, scripts, provider), "Descriptor expansion");
            Check(scripts.size() == 1 && scripts[0] == policy.Script(branch, 7), "Policy script mismatch");
        }
    }
    td::Policy real("Policy", "wsh(sortedmulti(2,@0/**,@1/**))", {a, b}, false);
    td::Policy replaced("Policy", "wsh(sortedmulti(2,@0/**,@1/**))", {a, c}, false);
    Check(!replaced.Authorized(session, session.RegistrationTag(real.ID())), "Replaced cosigner accepted");
    td::Policy foreign("Foreign", "wpkh(@0/**)", {b}, false);
    Check(!foreign.Authorized(session, session.RegistrationTag(foreign.ID())), "HMAC without ownership accepted");
    const auto spoofed = "[" + HexStr(session.RootFingerprint()) + b.substr(9);
    Check(!td::Policy("Foreign", "wpkh(@0/**)", {spoofed}, false).Authorized(session, zero), "Fingerprint-only ownership");

    for (const auto& bad : {"wsh(multi(1,@1/**,@0/**))", "wsh(multi(1,@0/**,@2/**))",
            "wsh(multi(2,@0/**,@0/**))", "wsh(and_v(v:pk(@0/<0;1>/*),pk(@0/<2;1>/*)))",
            "wpkh(@00/**)", "wpkh(@0/<0;2147483648>/*)", "wpkh(@0/<0;0>/*)",
            "wsh(and_v(v:pk(@0/**),older(65536)))", "tr(musig(@0/**,@1/**))",
            "sh(pk(@0/**))", "wpkh([deadbeef]@0/**)", "wpkh(@0/**))", "wpkh(@0/**/0)"}) {
        Reject([&] { td::Policy("Invalid", bad, {a}, false); });
    }
    Reject([&] { td::Policy("Invalid", "wpkh(@0/**)", {a, b}, false); });
    Reject([&] { td::Policy("Invalid", "wsh(multi(2,@0/**,@1/**))", {a, a}, false); });
    Reject([&] { td::Policy("Invalid", "wsh(multi(2,@0/**," + a + "))", {a}, false); });
    Reject([&] { td::Policy("Invalid", "tr(" + std::string(64, '1') + ",pk(@0/**))", {a}, false); });
    Reject([&] { td::Policy("Invalid", "wsh(" + std::string(65, 'n') + ":pk(@0/**))", {a}, false); });
    Reject([&] { td::Policy("Invalid", "wpkh(@0/**)", {a + "\n"}, false); });
    Reject([&] { td::Policy("Invalid", "wpkh(@0/**)", {a + "/0/*),pk(" + b}, false); });
    Reject([&] { td::Policy("\x1b[2J", "wpkh(@0/**)", {a}, false); });
    std::vector<unsigned char> encoded;
    const auto public_text = a.substr(a.find(']') + 1);
    Check(DecodeBase58Check(public_text, encoded, 78), "Test key decode");
    auto changed = encoded;
    changed[13] ^= 1; // Different chain code, same public point: still forbidden.
    Reject([&] { td::Policy("Invalid", "wsh(multi(2,@0/**,@1/**))", {a, EncodeBase58Check(changed)}, false); });
    std::fill(changed.begin() + 46, changed.end(), 0xff); // Invalid curve coordinate.
    Reject([&] { td::Policy("Invalid", "wpkh(@0/**)", {EncodeBase58Check(changed)}, false); });
    Check(!real.Authorized(session, std::span{zero}.first(31)), "Truncated proof accepted");
    std::puts("PASS: defaults, ownership, registration authorization, policy families and hostile inputs");
}

void Identifiers()
{
    SelectParams(ChainType::MAIN);
    // Public key from BIP388's BIP44 example. IDs computed independently from
    // Ledger v2 serialization; the long template exercises CompactSize >= 253.
    const std::string key = "[6738736c/44'/0'/0']xpub6Br37sWxruYfT8ASpCjVHKGwgdnYFEn98DwiN76i2oyY6fgH1LAPmmDcF46xjxJr22gw4jmVjTE2E3URMnRPEPYyo1zoPSUba563ESMXCeb";
    td::Policy golden("Golden wallet", "pkh(@0/**)", {key}, true);
    Check(HexStr(golden.ID()) == "7854337da8c8dee4ba29460c81d1b696cbd4257c7935b47d0b7c79ea2cee66e8", "Wallet ID vector");
    td::Keys session(Bytes(MNEMONIC), Bytes("TREZOR"));
    Check(HexStr(session.RegistrationTag(golden.ID())) == "1d24dc59fddb8ebfb3d8a9e1f4fe0c5928efe8d037537d874f388c0cc07801b9", "Registration HMAC vector");
    std::string long_template = "wsh(multi(20";
    for (unsigned index = 2; index < 42; index += 2) long_template += ",@0/<" + std::to_string(index) + ";" + std::to_string(index + 1) + ">/*";
    long_template += "))";
    td::Policy long_policy("Golden wallet", long_template, {key}, true);
    Check(HexStr(long_policy.ID()) == "49a4773d70e608b1b8acfb9cd7a3b265b6ac222bced3e145e06d683202b97135", "Long wallet ID vector");
    const std::string key2 = "xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8";
    const std::string key3 = "xpub661MyMwAqRbcFW31YEwpkMuc5THy2PSt5bDMsktWQcFF8syAmRUapSCGu8ED9W6oDMSgv6Zz8idoc4a6mr8BDzTJY47LJhkJ8UB7WEGuduB";
    td::Policy two("Golden wallet", "wsh(sortedmulti(2,@0/**,@1/**))", {key, key2}, true);
    td::Policy three("Golden wallet", "wsh(sortedmulti(2,@0/**,@1/**,@2/**))", {key, key2, key3}, true);
    Check(HexStr(two.ID()) == "5dea6f4b41905d5d2625e9f790357ccdb351d0877ef29b010a34109c01beae8a", "Two-key Merkle vector");
    Check(HexStr(three.ID()) == "a9aaeb802f07f42fcc2b4debd802595e99098fbab8fc4bb8b98f2162bd4063eb", "Unbalanced Merkle vector");
    std::puts("PASS: exact wallet-ID and HMAC vectors, including CompactSize boundary");
}
}

int main()
{
    try {
        ECC_Context context;
        SelectParams(ChainType::REGTEST);
        Seeds();
        Policies();
        Identifiers();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Foundation test failed: %s\n", error.what());
        return 1;
    }
}
