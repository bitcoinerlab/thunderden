#include "wallet_qr.h"
#include "cbor.h"

#include <chainparams.h>
#include <crypto/common.h>
#include <util/strencodings.h>
#include <bitset>

namespace td {
namespace {
// 0 is Sparrow's legacy layout; 1/2 are BIP48's nested/native script types.
bool AccountPath(const Path& path, unsigned script_type, bool mainnet)
{
    if (script_type == 0) return path == Path{0x8000002dU};
    return path.size() == 4 && path[0] == 0x80000030U
        && path[1] == (mainnet ? 0x80000000U : 0x80000001U)
        && path[2] >= 0x80000000U && path[3] == (0x80000000U | script_type);
}

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
    Require(AccountPath(path, script_type, mainnet), "Expected the standard account path for this multisig type");
    if (!seen[8]) {
        Require(path.size() == 1, "Missing parent fingerprint");
        std::copy(fingerprint.begin(), fingerprint.end(), key.vchFingerprint);
    }
    if (path.size() == 1) Require(ReadBE32(key.vchFingerprint) == ReadBE32(fingerprint.data()), "Conflicting parent fingerprint");
    key.nDepth = path.size();
    key.nChild = path.back();
    return "[" + HexStr(fingerprint) + PathText(path).substr(1) + "]" + EncodePublic(key, mainnet);
}
}

Policy ImportMultisig(const QRMessage& message)
{
    Require(message.type == "crypto-output" && !message.cbor.empty() && message.cbor.size() <= MAX_WALLET_SETUP,
        "Scan a multisig wallet setup QR from your wallet's settings");
    CborReader in(message.cbor);
    const auto wrapper = in.Tag();
    Require(wrapper == 400 || wrapper == 401, "Expected legacy, nested or native SegWit multisig");
    unsigned script_type = wrapper == 401 ? 2 : 0;
    auto inner = in.Tag();
    if (wrapper == 400 && inner == 401) { script_type = 1; inner = in.Tag(); }
    Require(inner == 407, "Only sorted multisig wallet setups are supported");
    std::bitset<11> seen;
    unsigned threshold = 0;
    std::vector<std::string> keys;
    const bool mainnet = Params().GetChainType() == ChainType::MAIN;
    for (size_t left = in.Map(2); left; --left) {
        if (Field(in, seen, 2) == 1) threshold = in.UInt(20);
        else {
            const auto count = in.Array(script_type == 0 ? 15 : 20);
            for (size_t i = 0; i < count; ++i) keys.push_back(ReadAccount(in, script_type, mainnet));
        }
    }
    in.End();
    Require(seen[1] && seen[2] && keys.size() >= 2 && threshold && threshold <= keys.size(), "Invalid multisig threshold or key count");
    const std::string wrappers[]{"sh(", "sh(wsh(", "wsh("};
    const std::string names[]{"Legacy", "Nested SegWit", "Native SegWit"};
    const auto name = names[script_type] + " multisig (" + std::to_string(threshold) + " of " + std::to_string(keys.size()) + ")";
    auto text = wrappers[script_type] + "sortedmulti(" + std::to_string(threshold);
    for (size_t i = 0; i < keys.size(); ++i) text += ",@" + std::to_string(i) + "/**";
    text += script_type == 1 ? ")))" : "))";
    return Policy(name, text, std::move(keys), mainnet);
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

QRMessage PublicDescriptor(const Policy& policy, const Keys& keys)
{
    Require(policy.IsDefault(keys), "Descriptor export requires a standard local account");
    // Full-text descriptors are valid too, but Sparrow expects a key list.
    auto source = policy.Template();
    source.replace(source.find("/**"), 3, "/<0;1>/*");
    CborWriter out;
    out.Map(2); out.UInt(1); out.Text(source);
    out.UInt(2); out.Array(1); out.Tag(40303);
    const auto key = HDKeyCBOR(keys, policy.KeyInformation()[0].origin, false);
    out.data.insert(out.data.end(), key.begin(), key.end());
    return {"output-descriptor", std::move(out.data)};
}

QRMessage PublicAccount(const Keys& keys, const Path& path)
{
    const bool mainnet = Params().GetChainType() == ChainType::MAIN;
    const auto script_type = path.size() == 4 ? path.back() & 0x7fffffffU : 0U;
    Require(script_type <= 2 && AccountPath(path, script_type, mainnet), "Account export requires a standard multisig path");
    // SeedSigner's deployed account format carries a script-typed cosigner key.
    // Sparrow preserves its full origin in both airgapped/watch-only imports;
    // standalone hdkey follows a lossy UI path. This does not approve a quorum.
    CborWriter out;
    out.Map(2); out.UInt(1); out.UInt(ReadBE32(keys.RootFingerprint().data()));
    out.UInt(2); out.Array(1);
    if (script_type != 2) out.Tag(400); // sh
    if (script_type != 0) out.Tag(401); // wsh
    out.Tag(303);
    const auto key = HDKeyCBOR(keys, path, true);
    out.data.insert(out.data.end(), key.begin(), key.end());
    return {"crypto-account", std::move(out.data)};
}
}
