#include "application.h"
#include "review.h"

#include <chainparams.h>
#include <cbor-lite.hpp>
#include <crypto/common.h>
#include <util/strencodings.h>

namespace td {
std::string PublicKeyText(const Keys& keys, const Path& path)
{
    Require(path.size() <= 32, "Export path is too deep");
    // SeedSigner's static [origin]xpub format fills fingerprint/path in both
    // Sparrow's watch-only scanner and its SeedSigner/Specter importers. The
    // standalone ur:hdkey route drops that metadata in Sparrow 2.3.1/2.5.5.
    // Keep PublicHDKey's CBOR for the compact descriptor, not a second UI format.
    return "[" + HexStr(keys.RootFingerprint()) + PathText(path).substr(1) + "]"
        + EncodePublic(keys.PublicAt(path), Params().GetChainType() == ChainType::MAIN);
}

std::optional<Digest> ApproveWallet(const Policy& policy, const Keys& keys, const WalletApproval& approve)
{
    Require(!policy.Name().empty() && !policy.OwnedKeys(keys).empty(), "This wallet does not contain a key from your recovery words");
    Require(bool(approve), "Missing local approval");
    const auto id = policy.ID();
    if (!approve(PolicyReview(policy, keys), {"Wallet ID: " + HexStr(id), "", "Full public descriptor:", policy.DescriptorText()})) return {};
    return keys.RegistrationTag(id);
}

Policy DefaultPolicy(const Keys& keys, unsigned purpose, unsigned account)
{
    const bool mainnet = Params().GetChainType() == ChainType::MAIN;
    Require(account <= 100, "Default account exceeds 100");
    std::string text;
    if (purpose == 44) text = "pkh(@0/**)";
    else if (purpose == 49) text = "sh(wpkh(@0/**))";
    else if (purpose == 84) text = "wpkh(@0/**)";
    else if (purpose == 86) text = "tr(@0/**)";
    else throw std::invalid_argument("Unsupported default account type");
    const Path path{purpose | 0x80000000U, mainnet ? 0x80000000U : 0x80000001U, account | 0x80000000U};
    const auto pub = keys.PublicAt(path);
    const auto key_text = "[" + HexStr(keys.RootFingerprint()) + PathText(path).substr(1) + "]" + EncodePublic(pub, mainnet);
    return Policy("", text, {key_text}, mainnet);
}

QRMessage PublicDescriptor(const Policy& policy, const Keys& keys)
{
    Require(policy.IsDefault(keys), "Descriptor export requires a standard local account");
    // BCR-2023-010 permits full text too, but Sparrow assumes keys is present.
    // Keep the receive/change suffix in source for its key substitution.
    auto source = policy.Template();
    source.replace(source.find("/**"), 3, "/<0;1>/*");
    const auto hdkey = PublicHDKey(keys, policy.KeyInformation()[0].origin);
    std::vector<uint8_t> cbor;
    CborLite::encodeMapSize(cbor, size_t{2});
    CborLite::encodeUnsigned(cbor, uint64_t{1});
    CborLite::encodeText(cbor, source);
    CborLite::encodeUnsigned(cbor, uint64_t{2});
    CborLite::encodeArraySize(cbor, size_t{1});
    CborLite::encodeTagAndValue(cbor, CborLite::Major::semantic, uint64_t{40303});
    cbor.insert(cbor.end(), hdkey.cbor.begin(), hdkey.cbor.end());
    return {"output-descriptor", std::move(cbor)};
}

QRMessage PublicHDKey(const Keys& keys, const Path& path)
{
    Require(path.size() <= 32, "Export path is too deep");
    const auto pub = keys.PublicAt(path);
    const bool mainnet = Params().GetChainType() == ChainType::MAIN;
    std::vector<uint8_t> cbor;
    const auto number = [&](uint64_t n) { CborLite::encodeUnsigned(cbor, n); };
    const auto tag = [&](uint64_t n) { CborLite::encodeTagAndValue(cbor, CborLite::Major::semantic, n); };
    const auto parent = ReadBE32(pub.vchFingerprint);
    CborLite::encodeMapSize(cbor, size_t{3U + !mainnet + bool(parent)});
    number(3); CborLite::encodeBytes(cbor, pub.pubkey);
    number(4); CborLite::encodeBytes(cbor, pub.chaincode);
    if (!mainnet) {
        number(5); tag(40305); // coin-info
        CborLite::encodeMapSize(cbor, size_t{2});
        number(1); number(0); number(2); number(1); // Bitcoin, test network
    }
    number(6); tag(40304); // keypath
    CborLite::encodeMapSize(cbor, size_t{3});
    number(1); CborLite::encodeArraySize(cbor, path.size() * 2);
    for (const auto index : path) {
        number(index & 0x7fffffffU);
        CborLite::encodeBool(cbor, bool(index & 0x80000000U));
    }
    number(2); number(ReadBE32(keys.RootFingerprint().data()));
    number(3); number(path.size());
    if (parent) { number(8); number(parent); }
    return {"hdkey", std::move(cbor)};
}

}
