#include "wallet_qr.h"
#include "cbor.h"

#include <chainparams.h>
#include <crypto/common.h>
#include <util/bip32.h>
#include <util/strencodings.h>
#include <algorithm>
#include <bitset>
#include <charconv>

namespace td {
namespace {
std::vector<uint8_t> HDKeyCBOR(const Keys& keys, const Path& path, bool legacy_tags)
{
    Require(path.size() <= 32, "Export path is too deep");
    const auto pub = keys.PublicAt(path);
    const bool mainnet = Params().GetChainType() == ChainType::MAIN;
    const auto parent = ReadBE32(pub.vchFingerprint);
    CborWriter out;
    out.Map(3 + !mainnet + bool(parent));
    out.UInt(3); out.Bytes(pub.pubkey);
    out.UInt(4); out.Bytes(pub.chaincode);
    if (!mainnet) {
        out.UInt(5); out.Tag(legacy_tags ? 305 : 40305); // coin-info
        out.Map(2); out.UInt(1); out.UInt(0); out.UInt(2); out.UInt(1); // Bitcoin, test network
    }
    out.UInt(6); out.Tag(legacy_tags ? 304 : 40304); // keypath
    out.Map(3); out.UInt(1); out.Array(path.size() * 2);
    for (const auto index : path) { out.UInt(index & 0x7fffffffU); out.Bool(bool(index & 0x80000000U)); }
    out.UInt(2); out.UInt(ReadBE32(keys.RootFingerprint().data()));
    out.UInt(3); out.UInt(path.size());
    if (parent) { out.UInt(8); out.UInt(parent); }
    return std::move(out.data);
}

unsigned Field(CborReader& in, std::bitset<11>& seen, unsigned maximum)
{
    const auto field = in.UInt(maximum);
    Require(field && !seen[field], "Unknown or repeated wallet field");
    seen.set(field);
    return field;
}

std::string ReadAccount(CborReader& in, unsigned script_type, bool mainnet)
{
    Require(in.Tag() == 303, "Expected a public account key");
    CExtPubKey key;
    Fingerprint fingerprint{};
    Path path;
    std::bitset<11> seen;
    unsigned network = 0;
    for (size_t left = in.Map(10); left; --left) {
        switch (Field(in, seen, 10)) {
        case 1: case 2:
            Require(!in.Bool(), "Wallet setup must contain public keys only"); break;
        case 3: {
            const auto bytes = in.Bytes(33);
            Require(bytes.size() == 33, "Expected a compressed public key");
            key.pubkey.Set(bytes.begin(), bytes.end());
            Require(key.pubkey.IsFullyValid(), "Invalid public key");
            break;
        }
        case 4: {
            const auto bytes = in.Bytes(32);
            Require(bytes.size() == 32, "Missing account chain code");
            std::copy(bytes.begin(), bytes.end(), key.chaincode.begin());
            break;
        }
        case 5: {
            Require(in.Tag() == 305, "Invalid key network information");
            std::bitset<11> fields;
            for (size_t n = in.Map(2); n; --n) {
                if (Field(in, fields, 2) == 1) Require(in.UInt() == 0, "Expected a Bitcoin key");
                else network = in.UInt(1);
            }
            break;
        }
        case 6: {
            Require(in.Tag() == 304, "Invalid key origin");
            std::bitset<11> fields;
            unsigned depth = 0;
            for (size_t n = in.Map(3); n; --n) {
                switch (Field(in, fields, 3)) {
                case 1: {
                    const auto count = in.Array(8);
                    Require(count && count % 2 == 0, "Incomplete account path");
                    for (size_t i = 0; i < count / 2; ++i) {
                        const auto index = in.UInt(0x7fffffff);
                        path.push_back(index | (in.Bool() ? 0x80000000U : 0));
                    }
                    break;
                }
                case 2: WriteBE32(fingerprint.data(), in.UInt()); break;
                case 3: depth = in.UInt(255); break;
                }
            }
            Require(fields[1] && fields[2] && (!fields[3] || depth == path.size()), "Incomplete or inconsistent key origin");
            break;
        }
        case 7:
            // Sparrow omits children. This importer explicitly constructs its
            // standard /0/*, /1/* account, not an arbitrary crypto-output script.
            throw std::invalid_argument("Explicit child paths are not supported by this wallet import");
        case 8: WriteBE32(key.vchFingerprint, in.UInt()); break;
        case 9: case 10:
            // Labels/notes do not authorize keys. Bound and ignore them without
            // displaying untrusted text or making it part of wallet identity.
            in.IgnoreText(256); break;
        }
    }
    Require(seen[3] && seen[4] && seen[6] && network == unsigned(!mainnet), "Incomplete public key or wrong network");
    Require(MultisigAccountPath(path, script_type, mainnet), "Expected the standard account path for this multisig type");
    if (!seen[8]) {
        Require(path.size() == 1, "Missing parent fingerprint");
        std::copy(fingerprint.begin(), fingerprint.end(), key.vchFingerprint);
    }
    if (path.size() == 1) Require(ReadBE32(key.vchFingerprint) == ReadBE32(fingerprint.data()), "Conflicting parent fingerprint");
    key.nDepth = path.size();
    key.nChild = path.back();
    return "[" + HexStr(fingerprint) + PathText(path).substr(1) + "]" + EncodePublic(key, mainnet);
}

std::string_view Trim(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r");
    return first == text.npos ? std::string_view{} : text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

unsigned KeyCount(std::string_view text)
{
    unsigned value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    Require(error == std::errc{} && end == text.data() + text.size() && value > 0 && value <= 20,
        "Invalid multisig threshold or key count");
    return value;
}

Policy ReadMultisigText(std::string_view text, bool mainnet)
{
    for (const unsigned char c : text) Require((c >= 0x20 && c <= 0x7e) || c == '\n' || c == '\r' || c == '\t',
        "Invalid wallet setup text character");
    unsigned threshold = 0, count = 0;
    bool name = false, format = false;
    Path origin;
    std::vector<std::string> keys;
    while (!text.empty()) {
        const auto newline = text.find('\n');
        auto line = text.substr(0, newline);
        Require(line.size() <= 512, "Wallet setup line is too long");
        text = newline == text.npos ? std::string_view{} : text.substr(newline + 1);
        line = Trim(line);
        if (line.empty() || line.front() == '#') continue;
        const auto colon = line.find(':');
        Require(colon != line.npos, "Expected a wallet setup field");
        const auto field = Trim(line.substr(0, colon)), value = Trim(line.substr(colon + 1));
        if (field == "Name") {
            Require(!name && value.size() <= 256, "Invalid or repeated wallet name");
            name = true; // Labels do not establish wallet identity or authorize keys.
        } else if (field == "Policy") {
            Require(!count, "Repeated multisig policy");
            const auto separator = value.find(" of ");
            Require(separator != value.npos, "Expected a multisig M of N policy");
            threshold = KeyCount(value.substr(0, separator));
            count = KeyCount(value.substr(separator + 4));
        } else if (field == "Format") {
            Require(!format && value == "P2WSH", "Expected one P2WSH wallet format");
            format = true;
        } else if (field == "Derivation") {
            Require(origin.empty(), "Missing key after derivation");
            std::string path(value);
            std::replace(path.begin(), path.end(), 'h', '\'');
            Require(value.starts_with("m/") && ParseHDKeypath(path, origin) && MultisigAccountPath(origin, 2, mainnet),
                "Expected a standard native SegWit multisig account path");
        } else {
            Require(field.size() == 8 && IsHex(field), "Unknown wallet setup field or invalid fingerprint");
            Require(!origin.empty() && keys.size() < 20, "Missing derivation or too many wallet keys");
            keys.push_back("[" + HexStr(ParseHex(field)) + PathText(origin).substr(1) + "]" + std::string(value));
            origin.clear();
        }
    }
    Require(count && format && origin.empty() && keys.size() == count, "Incomplete multisig wallet setup");
    return MultisigPolicy(2, threshold, std::move(keys), mainnet);
}
}

bool IsMultisigSetup(const QRMessage& message)
{
    if (message.type == "crypto-output") return true;
    if (message.type != "bytes" || message.cbor.empty() || message.cbor.size() > MAX_WALLET_SETUP) return false;
    CborReader in(message.cbor);
    const auto bytes = in.Bytes(MAX_WALLET_SETUP);
    in.End();
    std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == text.npos) return false;
    text.remove_prefix(first);
    // Classify only. The full importer must validate before any wallet approval.
    // Thunder Den commands start with a binary CBOR array, never a text header.
    if (text.front() == '#') return true;
    const auto line = text.substr(0, text.find('\n'));
    const auto colon = line.find(':');
    if (colon == line.npos) return false;
    const auto field = Trim(line.substr(0, colon));
    return field == "Name" || field == "Policy" || field == "Format" || field == "Derivation";
}

Policy ImportMultisig(const QRMessage& message)
{
    Require((message.type == "crypto-output" || message.type == "bytes")
        && !message.cbor.empty() && message.cbor.size() <= MAX_WALLET_SETUP,
        "Scan a multisig wallet setup or signer-registration QR");
    const bool mainnet = Params().GetChainType() == ChainType::MAIN;
    CborReader in(message.cbor);
    if (message.type == "bytes") {
        const auto bytes = in.Bytes(MAX_WALLET_SETUP);
        in.End();
        return ReadMultisigText({reinterpret_cast<const char*>(bytes.data()), bytes.size()}, mainnet);
    }
    const auto wrapper = in.Tag();
    Require(wrapper == 400 || wrapper == 401, "Expected legacy, nested or native SegWit multisig");
    unsigned script_type = wrapper == 401 ? 2 : 0;
    auto inner = in.Tag();
    if (wrapper == 400 && inner == 401) { script_type = 1; inner = in.Tag(); }
    Require(inner == 407, "Only sorted multisig wallet setups are supported");
    std::bitset<11> seen;
    unsigned threshold = 0;
    std::vector<std::string> keys;
    for (size_t left = in.Map(2); left; --left) {
        if (Field(in, seen, 2) == 1) threshold = in.UInt(20);
        else {
            const auto count = in.Array(script_type == 0 ? 15 : 20);
            for (size_t i = 0; i < count; ++i) keys.push_back(ReadAccount(in, script_type, mainnet));
        }
    }
    in.End();
    Require(seen[1] && seen[2], "Incomplete multisig setup");
    return MultisigPolicy(script_type, threshold, std::move(keys), mainnet);
}

std::string PublicKeyText(const Keys& keys, const Path& path)
{
    Require(path.size() <= 32, "Export path is too deep");
    // The origin prefix retains fingerprint/path in Sparrow's text scanner.
    return "[" + HexStr(keys.RootFingerprint()) + PathText(path).substr(1) + "]"
        + EncodePublic(keys.PublicAt(path), Params().GetChainType() == ChainType::MAIN);
}

QRMessage PublicHDKey(const Keys& keys, const Path& path)
{
    return {"hdkey", HDKeyCBOR(keys, path, false)};
}

QRMessage PublicAccount(const Keys& keys, const Path& path)
{
    const bool mainnet = Params().GetChainType() == ChainType::MAIN;
    std::vector<unsigned> script_tags;
    if (path.size() == 3) {
        Require(path[1] == (mainnet ? 0x80000000U : 0x80000001U)
            && path[2] >= 0x80000000U && path[2] <= 0x80000064U, "Account export requires a standard account path");
        if (path[0] == 0x8000002cU) script_tags = {403}; // pkh
        else if (path[0] == 0x80000031U) script_tags = {400, 404}; // sh(wpkh)
        else if (path[0] == 0x80000054U) script_tags = {404}; // wpkh
        else if (path[0] == 0x80000056U) script_tags = {409}; // tr
        else throw std::invalid_argument("Unsupported account export path");
    } else {
        const auto script_type = path.size() == 4 ? path.back() & 0x7fffffffU : 0U;
        Require(MultisigAccountPath(path, script_type, mainnet), "Account export requires a standard multisig path");
        if (script_type != 2) script_tags.push_back(400); // sh
        if (script_type != 0) script_tags.push_back(401); // wsh
    }
    // SeedSigner's deployed account format carries a script-typed account key.
    // Sparrow preserves its full origin in both airgapped/watch-only imports;
    // standalone hdkey follows a lossy UI path. This does not approve a quorum.
    CborWriter out;
    out.Map(2); out.UInt(1); out.UInt(ReadBE32(keys.RootFingerprint().data()));
    out.UInt(2); out.Array(1);
    for (const auto tag : script_tags) out.Tag(tag);
    out.Tag(303);
    const auto key = HDKeyCBOR(keys, path, true);
    out.data.insert(out.data.end(), key.begin(), key.end());
    return {"crypto-account", std::move(out.data)};
}
}
