#pragma once
#include "application.h"
#include "wallet_qr.h"
#include "cbor.h"
#include <chainparams.h>
#include <crypto/common.h>
#include <streams.h>

namespace fixture {
inline auto Bytes(std::string_view text) { return std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()); }
inline const std::string WORDS = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
inline td::Path Path(unsigned kind, unsigned account = 0)
{
    if (!kind) return {0x8000002dU};
    return {0x80000030U, Params().GetChainType() == ChainType::MAIN ? 0x80000000U : 0x80000001U,
        0x80000000U | account, 0x80000000U | kind};
}

// Independent test producer for Sparrow's legacy registry schema. Deliberately
// writes origin map fields in a different order; map order is not identity.
inline td::QRMessage Setup(unsigned kind, unsigned threshold, const std::vector<const td::Keys*>& signers,
                           const std::vector<td::Path>& paths)
{
    td::CborWriter out;
    const auto tag = [&](unsigned n) { CborLite::encodeTagAndValue(out.data, CborLite::Major::semantic, n); };
    const auto map = [&](size_t n) { CborLite::encodeMapSize(out.data, n); };
    tag(kind == 2 ? 401 : 400);
    if (kind == 1) tag(401);
    tag(407); map(2); out.UInt(1); out.UInt(threshold); out.UInt(2); out.Array(signers.size());
    for (size_t i = 0; i < signers.size(); ++i) {
        const auto pub = signers[i]->PublicAt(paths[i]);
        tag(303); map(7);
        out.UInt(2); CborLite::encodeBool(out.data, false);
        out.UInt(3); out.Bytes(pub.pubkey);
        out.UInt(4); out.Bytes(pub.chaincode);
        out.UInt(5); tag(305); map(2); out.UInt(1); out.UInt(0);
        out.UInt(2); out.UInt(Params().GetChainType() == ChainType::MAIN ? 0 : 1);
        out.UInt(6); tag(304); map(3);
        out.UInt(3); out.UInt(paths[i].size());
        out.UInt(2); out.UInt(ReadBE32(signers[i]->RootFingerprint().data()));
        out.UInt(1); out.Array(paths[i].size() * 2);
        for (const auto child : paths[i]) { out.UInt(child & 0x7fffffffU); CborLite::encodeBool(out.data, bool(child & 0x80000000U)); }
        out.UInt(8); out.UInt(ReadBE32(pub.vchFingerprint));
        out.UInt(9); out.Text("Cosigner");
    }
    return {"crypto-output", std::move(out.data)};
}

inline std::vector<std::byte> Serialize(const PartiallySignedTransaction& psbt)
{
    DataStream stream; stream << psbt;
    return {stream.begin(), stream.end()};
}

inline PartiallySignedTransaction Spend(const std::vector<const td::Policy*>& policies, bool compact = false)
{
    CMutableTransaction tx;
    std::vector<CTransactionRef> previous;
    for (size_t i = 0; i < policies.size(); ++i) {
        CMutableTransaction funding;
        uint256 hash; hash.begin()[0] = i + 1;
        funding.vin.emplace_back(COutPoint(Txid::FromUint256(hash), 0));
        funding.vout.emplace_back(100000, policies[i]->Script(i % 2, i + 7));
        previous.push_back(MakeTransactionRef(funding));
        tx.vin.emplace_back(COutPoint(previous.back()->GetHash(), 0));
    }
    tx.vout.emplace_back(100000 * policies.size() - 1000, policies[0]->Script(1, 5));
    PartiallySignedTransaction psbt(tx);
    for (size_t i = 0; i < policies.size(); ++i) psbt.inputs[i].non_witness_utxo = previous[i];
    const auto data = PrecomputePSBTData(psbt);
    for (size_t i = 0; i < policies.size(); ++i) {
        const auto status = SignPSBTInput(policies[i]->PublicProvider({unsigned(i % 2), uint32_t(i + 7)}), psbt, i, &data, std::nullopt, nullptr, false);
        td::Require(status == PSBTError::OK || status == PSBTError::INCOMPLETE, "Fixture metadata failed");
        if (compact) psbt.inputs[i].non_witness_utxo.reset();
    }
    UpdatePSBTOutput(policies[0]->PublicProvider({1, 5}), psbt, 0);
    return psbt;
}
}
