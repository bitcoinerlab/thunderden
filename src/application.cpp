#include "application.h"

#include <chainparams.h>
#include <cbor-lite.hpp>
#include <crypto/common.h>
#include <univalue.h>
#include <util/strencodings.h>

#include <algorithm>
#include <set>

namespace td {
namespace {
void Fields(const UniValue& object, const std::set<std::string>& expected)
{
    Require(object.isObject() && object.size() == expected.size(), "Invalid request fields");
    const auto& keys = object.getKeys();
    Require(std::set<std::string>(keys.begin(), keys.end()) == expected, "Duplicate or unknown request field");
}

std::string String(const UniValue& value)
{
    Require(value.isStr(), "Expected a request string");
    return value.get_str();
}

QRMessage Json(const UniValue& value)
{
    const auto text = value.write();
    return {"bytes", CborBytes({reinterpret_cast<const uint8_t*>(text.data()), text.size()})};
}
}

Request ParseRequest(const QRMessage& message)
{
    Require(message.type == "bytes", "Expected a wallet-policy request");
    const auto bytes = UnwrapBytes(message.cbor);
    Require(!bytes.empty() && bytes.size() <= 1536 * 1024, "Application request size limit exceeded");
    UniValue root;
    Require(root.read(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())) && root.isObject(), "Invalid request JSON");
    Require(root["version"].isNum() && root["version"].getValStr() == "1", "Unsupported protocol version");
    const auto command = String(root["command"]);
    const bool registration = command == "REGISTER_WALLET";
    Require(registration || command == "SIGN_PSBT", "Unsupported request command");
    auto fields = std::set<std::string>{"version", "command", "network", "wallet"};
    if (!registration) { fields.insert("wallet_hmac"); fields.insert("psbt"); }
    Fields(root, fields);
    Require(String(root["network"]) == ChainTypeToString(Params().GetChainType()), "Request network differs from local selection");
    const auto& wallet = root["wallet"];
    Fields(wallet, {"name", "template", "keys"});
    Require(wallet["keys"].isArray() && wallet["keys"].size() <= 32, "Invalid wallet key vector");
    std::vector<std::string> key_text;
    for (const auto& key : wallet["keys"].getValues()) key_text.push_back(String(key));
    Request result{registration, Policy(String(wallet["name"]), String(wallet["template"]), std::move(key_text),
        Params().GetChainType() == ChainType::MAIN), {}, {}};
    if (registration) {
        Require(!result.policy.Name().empty(), "Registration requires a named wallet");
    } else {
        const auto tag = String(root["wallet_hmac"]);
        Require(tag.size() == 64 && IsHex(tag), "Invalid registration proof");
        const auto decoded = ParseHex(tag);
        std::copy(decoded.begin(), decoded.end(), result.tag.begin());
        const auto base64 = String(root["psbt"]);
        const auto psbt = DecodeBase64(base64);
        Require(psbt && psbt->size() <= ReviewedTransaction::MAX_PSBT_BYTES && EncodeBase64(*psbt) == base64, "Invalid base64 PSBT");
        const auto raw = std::as_bytes(std::span(*psbt));
        result.psbt.assign(raw.begin(), raw.end());
    }
    return result;
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
    auto key = keys.Derive(path);
    const auto pub = key.Neuter();
    memory_cleanse(key.chaincode.begin(), key.chaincode.size());
    const auto key_text = "[" + HexStr(keys.RootFingerprint()) + PathText(path).substr(1) + "]" + EncodePublic(pub, mainnet);
    return Policy("", text, {key_text}, mainnet);
}

QRMessage PublicAccount(const Policy& policy, const Keys& keys)
{
    Require(policy.IsDefault(keys), "Account export requires a verified default policy");
    const auto& info = policy.KeyInformation()[0];
    const bool mainnet = Params().GetChainType() == ChainType::MAIN;
    std::vector<uint8_t> cbor;
    const auto number = [&](uint64_t n) { CborLite::encodeUnsigned(cbor, n); };
    const auto tag = [&](uint64_t n) { CborLite::encodeTagAndValue(cbor, CborLite::Major::semantic, n); };
    CborLite::encodeMapSize(cbor, size_t{2});
    number(1); number(ReadBE32(info.fingerprint.data()));
    number(2); CborLite::encodeArraySize(cbor, size_t{1});
    const auto purpose = info.origin[0] & 0x7fffffffU;
    if (purpose == 49) tag(400);
    tag(purpose == 44 ? 403 : purpose == 86 ? 409 : 404);
    tag(303); // crypto-hdkey
    CborLite::encodeMapSize(cbor, size_t{mainnet ? 4U : 5U});
    number(3); CborLite::encodeBytes(cbor, info.key.pubkey);
    number(4); CborLite::encodeBytes(cbor, info.key.chaincode);
    if (!mainnet) {
        number(5); tag(305); // crypto-coin-info
        CborLite::encodeMapSize(cbor, size_t{2});
        number(1); number(0); number(2); number(1); // Bitcoin, test network
    }
    number(6); tag(304); // crypto-keypath
    CborLite::encodeMapSize(cbor, size_t{3});
    number(1); CborLite::encodeArraySize(cbor, info.origin.size() * 2);
    for (const auto index : info.origin) {
        number(index & 0x7fffffffU);
        CborLite::encodeBool(cbor, bool(index & 0x80000000U));
    }
    number(2); number(ReadBE32(info.fingerprint.data()));
    number(3); number(info.origin.size());
    number(8); number(ReadBE32(info.key.vchFingerprint));
    return {"crypto-account", std::move(cbor)};
}

std::optional<QRMessage> Register(const Policy& policy, const Keys& keys,
    const std::function<bool(const ReviewLines&)>& approve)
{
    Require(!policy.Name().empty() && !policy.OwnedKeys(keys).empty(), "Wallet is not registerable with this seed");
    Require(bool(approve), "Missing registration approval callback");
    const auto network = Params().GetChainType();
    const auto id = policy.ID();
    if (!approve(PolicyReview(policy, keys))) return {};
    Require(Params().GetChainType() == network, "Network changed during registration");
    UniValue response(UniValue::VOBJ);
    response.pushKV("version", 1);
    response.pushKV("command", "WALLET_REGISTERED");
    response.pushKV("wallet_id", HexStr(id));
    response.pushKV("wallet_hmac", HexStr(keys.RegistrationTag(id)));
    return Json(response);
}

std::optional<QRMessage> Sign(Request request, const Keys& keys,
    const std::function<bool(const TransactionReview&)>& approve)
{
    Require(!request.registration, "Registration request cannot sign");
    const ReviewedTransaction reviewed(std::move(request.policy), keys, request.tag, request.psbt);
    const auto result = reviewed.Sign(keys, approve);
    if (!result) return {};
    return QRMessage{"crypto-psbt", CborBytes({reinterpret_cast<const uint8_t*>(result->psbt.data()), result->psbt.size()})};
}
}
