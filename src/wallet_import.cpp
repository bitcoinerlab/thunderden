#include "application.h"
#include "cbor.h"

#include <chainparams.h>
#include <crypto/common.h>
#include <util/strencodings.h>

namespace td {
namespace {
unsigned Field(CborReader& in, unsigned& seen, unsigned maximum)
{
    const auto field = in.UInt(maximum);
    Require(field && !(seen & (1U << field)), "Unknown or repeated wallet field");
    seen |= 1U << field;
    return field;
}

std::string ReadAccount(CborReader& in, unsigned kind, bool mainnet)
{
    Require(in.Tag() == 303, "Expected a public account key");
    CExtPubKey key;
    Fingerprint fingerprint{};
    Path path;
    unsigned seen = 0, network = 0;
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
            unsigned fields = 0;
            for (size_t n = in.Map(2); n; --n) {
                if (Field(in, fields, 2) == 1) Require(in.UInt() == 0, "Expected a Bitcoin key");
                else network = in.UInt(1);
            }
            break;
        }
        case 6: {
            Require(in.Tag() == 304, "Invalid key origin");
            unsigned fields = 0, depth = 0;
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
            Require((fields & 6) == 6 && (!(fields & 8) || depth == path.size()), "Incomplete or inconsistent key origin");
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
    Require((seen & 88) == 88 && network == unsigned(!mainnet), "Incomplete public key or wrong network");
    if (kind == 0) Require(path == Path{0x8000002dU}, "Legacy setup requires Sparrow's m/45h path");
    else Require(path.size() == 4 && path[0] == 0x80000030U
        && path[1] == (mainnet ? 0x80000000U : 0x80000001U)
        && path[2] >= 0x80000000U && path[3] == (0x80000000U | kind), "Expected a BIP48 account for this address type");
    if (!(seen & 256)) {
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
    unsigned kind = wrapper == 401 ? 2 : 0;
    auto inner = in.Tag();
    if (wrapper == 400 && inner == 401) { kind = 1; inner = in.Tag(); }
    Require(inner == 407, "Only sorted multisig wallet setups are supported");
    unsigned seen = 0, threshold = 0;
    std::vector<std::string> keys;
    const bool mainnet = Params().GetChainType() == ChainType::MAIN;
    for (size_t left = in.Map(2); left; --left) {
        if (Field(in, seen, 2) == 1) threshold = in.UInt(20);
        else {
            const auto count = in.Array(kind == 0 ? 15 : 20);
            for (size_t i = 0; i < count; ++i) keys.push_back(ReadAccount(in, kind, mainnet));
        }
    }
    in.End();
    Require(seen == 6 && keys.size() >= 2 && threshold && threshold <= keys.size(), "Invalid multisig threshold or key count");
    const std::string wrappers[]{"sh(", "sh(wsh(", "wsh("};
    const std::string names[]{"Legacy", "Nested SegWit", "Native SegWit"};
    const auto name = names[kind] + " multisig (" + std::to_string(threshold) + " of " + std::to_string(keys.size()) + ")";
    auto text = wrappers[kind] + "sortedmulti(" + std::to_string(threshold);
    for (size_t i = 0; i < keys.size(); ++i) text += ",@" + std::to_string(i) + "/**";
    text += kind == 1 ? ")))" : "))";
    return Policy(name, text, std::move(keys), mainnet);
}
}
