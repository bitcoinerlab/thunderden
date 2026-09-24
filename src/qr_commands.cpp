#include "qr_commands.h"
#include "cbor.h"

#include <chainparams.h>
#include <crypto/sha256.h>
#include <key_io.h>
#include <util/strencodings.h>

namespace td {
namespace {
Digest Hash(std::span<const uint8_t> data)
{
    Digest result;
    CSHA256().Write(data.data(), data.size()).Finalize(result.data());
    return result;
}

Policy ReadPolicy(CborReader& in)
{
    in.Tuple(3);
    auto name = in.Text(64);
    auto text = in.Text(8192);
    const auto count = in.Array(32);
    std::vector<std::string> keys;
    for (size_t i = 0; i < count; ++i) keys.push_back(in.Text(512));
    return Policy(std::move(name), std::move(text), std::move(keys), Params().GetChainType() == ChainType::MAIN);
}

bool Approve(const std::function<bool(const ReviewLines&)>& callback, const ReviewLines& lines)
{
    Require(bool(callback), "Missing local approval");
    return callback(lines);
}
}

Digest KeyIdentity(const Keys& keys)
{
    const auto pub = keys.PublicAt({});
    Digest result;
    CSHA256().Write(pub.pubkey.data(), pub.pubkey.size()).Write(pub.chaincode.data(), pub.chaincode.size()).Finalize(result.data());
    return result;
}

QRMessage HandleQRRequest(const QRMessage& message, const Keys& keys, const QRApproval& approve)
{
    Require(message.type == "bytes", "Expected a Thunder Den QR command");
    const auto raw = UnwrapBytes(message.cbor);
    Require(raw.size() <= ReviewedTransaction::MAX_PSBT_BYTES + 65536, "Request too large");
    CborReader in(raw);
    in.Tuple(6);
    Require(in.UInt() == 2, "Unsupported Thunder Den command version");
    const auto id = in.Bytes(16);
    Require(id.size() == 16, "Invalid request ID");
    const auto network = in.Text(16);
    const auto expected = in.Bytes(32);
    Require(expected.empty() || expected.size() == 32, "Invalid key identity");
    const auto operation = in.UInt();
    const auto chain = Params().GetChainType();
    const auto identity = KeyIdentity(keys);
    unsigned status = 0;
    CborWriter body;
    // Error replies contain fixed codes only, never exception strings or input data.
    if (network != ChainTypeToString(chain)) status = 4;
    else if (operation > 4) status = 5;
    else if ((!expected.empty() && !std::equal(expected.begin(), expected.end(), identity.begin()))
        || (operation >= 2 && expected.empty())) status = 3;
    else try {
        if (operation == 0) {
            in.Tuple(0); in.End();
            body.Array(0);
        } else if (operation == 1) {
            in.Tuple(2);
            const auto count = in.Array(32);
            Path path;
            for (size_t i = 0; i < count; ++i) path.push_back(in.UInt());
            in.UInt(1); // display flag: this signer always asks before sharing a key.
            in.End();
            const auto xpub = EncodePublic(keys.PublicAt(path), chain == ChainType::MAIN);
            const ReviewLines lines{"Network: " + network, "Fingerprint: " + HexStr(keys.RootFingerprint()),
                "Path: " + PathText(path), "This public key can reveal account activity.", "No private keys are shared.", xpub};
            if (!Approve(approve.export_key, lines)) status = 1;
            body.Array(2); body.Array(path.size());
            for (const auto index : path) body.UInt(index);
            body.Text(xpub);
        } else if (operation >= 2 && operation <= 4) {
            in.Tuple(operation == 2 ? 1 : operation == 3 ? 4 : 3);
            auto policy = ReadPolicy(in);
            if (operation == 2) {
                in.End();
                Require(!policy.Name().empty() && !policy.OwnedKeys(keys).empty(), "Wallet is not owned");
                const auto wallet_id = policy.ID();
                if (!Approve(approve.register_wallet, PolicyReview(policy, keys))) status = 1;
                if (!status) {
                    body.Array(2); body.Bytes(wallet_id); body.Bytes(keys.RegistrationTag(wallet_id));
                }
            } else {
                const auto proof = in.Bytes(32);
                Require(policy.Authorized(keys, proof), "Invalid registration proof");
                if (operation == 3) {
                    const auto branch = in.UInt(1), index = in.UInt(0x7fffffff);
                    in.End();
                    CTxDestination destination;
                    Require(ExtractDestination(policy.Script(branch, index), destination), "Invalid address");
                    const auto address = EncodeDestination(destination);
                    if (!Approve(approve.address, {"Network: " + network, "Wallet: " + policy.Name(),
                        "Wallet ID: " + HexStr(policy.ID()), branch ? "Change address" : "Receive address",
                        "Index: " + std::to_string(index), address})) status = 1;
                    body.Array(1); body.Text(address);
                } else {
                    const auto psbt = in.Bytes(ReviewedTransaction::MAX_PSBT_BYTES);
                    in.End();
                    const ReviewedTransaction reviewed(std::move(policy), keys, proof, std::as_bytes(psbt));
                    const auto result = reviewed.Sign(keys, approve.transaction);
                    if (!result) status = 1;
                    else {
                        body.Array(3);
                        body.Bytes({reinterpret_cast<const uint8_t*>(result->psbt.data()), result->psbt.size()});
                        body.UInt(result->added_signatures); body.UInt(result->complete);
                    }
                }
            }
        } else status = 5;
    } catch (const std::invalid_argument&) { status = 2; }
    Require(Params().GetChainType() == chain, "Network changed during operation");
    CborWriter out;
    out.Array(10); out.UInt(2); out.Bytes(id); out.Text(ChainTypeToString(chain));
    out.Bytes(identity); out.Bytes(keys.RootFingerprint()); out.Text("2.0"); out.UInt(operation);
    out.Bytes(Hash(raw)); out.UInt(status);
    if (status) out.Array(0);
    else out.data.insert(out.data.end(), body.data.begin(), body.data.end());
    return {"bytes", CborBytes(out.data)};
}
}
