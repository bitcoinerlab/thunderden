#include "policy.h"

#include <base58.h>
#include <crypto/common.h>
#include <crypto/sha256.h>
#include <streams.h>
#include <util/strencodings.h>
#include <util/translation.h>

#include <algorithm>
#include <bit>
#include <charconv>
#include <set>

const TranslateFn G_TRANSLATION_FUN{};

namespace td {
namespace {
uint32_t Number(std::string_view text, size_t& pos)
{
    const size_t start = pos;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') ++pos;
    Require(pos > start && (pos == start + 1 || text[start] != '0'), "Invalid path or key number");
    uint32_t value{0};
    const auto parsed = std::from_chars(text.data() + start, text.data() + pos, value);
    Require(parsed.ec == std::errc{} && value < 0x80000000U, "Path or key number out of range");
    return value;
}

void Take(std::string_view text, size_t& pos, std::string_view token)
{
    Require(text.substr(pos, token.size()) == token, "Invalid key expression");
    pos += token.size();
}

KeyInfo ReadKey(std::string text, bool mainnet)
{
    Require(!text.empty() && text.size() <= 512, "Invalid key information length");
    KeyInfo result;
    result.text = text;
    size_t pos = 0;
    if (text[0] == '[') {
        Require(text.size() >= 10 && IsHex(std::string_view(text).substr(1, 8)), "Invalid key fingerprint");
        const auto bytes = ParseHex(std::string_view(text).substr(1, 8));
        std::copy(bytes.begin(), bytes.end(), result.fingerprint.begin());
        result.has_origin = true;
        pos = 9;
        while (pos < text.size() && text[pos] == '/') {
            ++pos;
            uint32_t index = Number(text, pos);
            if (pos < text.size() && (text[pos] == 'h' || text[pos] == '\'')) {
                index |= 0x80000000U;
                ++pos;
            }
            result.origin.push_back(index);
            Require(result.origin.size() <= 32, "Key origin exceeds 32 steps");
        }
        Take(text, pos, "]");
    }
    const auto encoded = text.substr(pos);
    Require(encoded.starts_with(mainnet ? "xpub" : "tpub"), "Wrong network or non-public key");
    Require(std::all_of(encoded.begin(), encoded.end(), [](char c) {
        return std::string_view("123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz").find(c) != std::string_view::npos;
    }), "Invalid extended-key characters");
    std::vector<unsigned char> bytes;
    Require(DecodeBase58Check(encoded, bytes, 78) && bytes.size() == 78, "Invalid extended public key");
    Require(ReadBE32(bytes.data()) == (mainnet ? 0x0488B21EU : 0x043587CFU), "Wrong key version");
    result.key.DecodeWithVersion(bytes.data());
    Require(result.key.pubkey.IsFullyValid(), "Invalid public key point or metadata");
    return result;
}

Digest Sha256(std::span<const unsigned char> data)
{
    Digest result;
    CSHA256().Write(data.data(), data.size()).Finalize(result.data());
    return result;
}

Digest Merkle(std::span<const KeyInfo> keys)
{
    CSHA256 hash;
    if (keys.size() == 1) {
        const unsigned char prefix = 0;
        const auto& key = keys[0].text;
        hash.Write(&prefix, 1).Write(reinterpret_cast<const unsigned char*>(key.data()), key.size());
    } else {
        const size_t split = std::bit_floor(keys.size() - 1);
        const auto left = Merkle(keys.first(split)), right = Merkle(keys.subspan(split));
        const unsigned char prefix = 1;
        hash.Write(&prefix, 1).Write(left.data(), left.size()).Write(right.data(), right.size());
    }
    Digest result;
    hash.Finalize(result.data());
    return result;
}
}

Policy::Policy(std::string name, std::string text, std::vector<std::string> keys, bool mainnet)
    : name_(std::move(name)), text_(std::move(text)), mainnet_(mainnet)
{
    Require(name_.size() <= 64 && std::all_of(name_.begin(), name_.end(), [](unsigned char c) {
        return c >= 0x20 && c <= 0x7e;
    }), "Invalid wallet name");
    Require(name_.empty() || (name_.front() != ' ' && name_.back() != ' '), "Invalid wallet name spacing");
    Require(!text_.empty() && text_.size() <= 8192, "Invalid policy length");
    Require(!keys.empty() && keys.size() <= 32, "Policy requires 1 through 32 keys");
    Require(text_.starts_with("pkh(") || text_.starts_with("wpkh(") || text_.starts_with("sh(")
        || text_.starts_with("wsh(") || text_.starts_with("tr("), "Unsupported account wrapper");
    Require(!text_.starts_with("sh(pk("), "Bare pk inside sh is not a BIP388 account");
    Require(text_.find("musig(") == text_.npos, "MuSig2 is not supported");
    std::set<CPubKey> public_keys;
    std::set<CExtPubKey> expected_keys;
    for (auto& encoded : keys) {
        auto key = ReadKey(std::move(encoded), mainnet);
        Require(public_keys.insert(key.key.pubkey).second, "Duplicate policy public key");
        expected_keys.insert(key.key);
        keys_.push_back(std::move(key));
    }

    // Parse only BIP388 references and their path suffixes. Core parses the
    // surrounding descriptor/Miniscript language and exposes all resulting keys.
    std::string expanded, nesting;
    std::set<size_t> seen;
    std::set<std::pair<size_t, uint32_t>> paths;
    size_t references = 0, wrappers = 0;
    for (size_t pos = 0; pos < text_.size();) {
        if (text_[pos] == '@') {
            ++pos;
            const size_t index = Number(text_, pos);
            Require(index < keys_.size(), "Unknown policy key");
            Require(seen.contains(index) || index == seen.size(), "Policy keys must appear in order");
            seen.insert(index);
            Take(text_, pos, "/");
            uint32_t receive = 0, change = 1;
            if (text_.substr(pos, 2) == "**") {
                pos += 2;
            } else {
                Take(text_, pos, "<");
                receive = Number(text_, pos);
                Take(text_, pos, ";");
                change = Number(text_, pos);
                Take(text_, pos, ">/*");
            }
            Require(paths.emplace(index, receive).second && paths.emplace(index, change).second,
                "Overlapping receive/change paths");
            Require(++references <= 128, "Too many key references");
            references_.push_back({index, receive, change});
            expanded += keys_[index].text + "/<" + std::to_string(receive) + ";" + std::to_string(change) + ">/*";
            Require(expanded.size() <= 65536, "Expanded policy is too large");
        } else {
            const char c = text_[pos++];
            Require((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                || std::string_view("_():{},").find(c) != std::string_view::npos, "Invalid policy character");
            if (c == '(' || c == '{') {
                nesting += c;
                Require(nesting.size() <= 64, "Policy nesting is too deep");
            } else if (c == ')' || c == '}') {
                Require(!nesting.empty() && nesting.back() == (c == ')' ? '(' : '{'), "Unbalanced policy");
                nesting.pop_back();
            } else if (c == ':') {
                size_t start = pos - 1;
                while (start > 0 && text_[start - 1] >= 'a' && text_[start - 1] <= 'z') --start;
                wrappers += pos - 1 - start;
                Require(wrappers <= 64, "Too many Miniscript wrappers");
            }
            expanded += c;
        }
    }
    Require(nesting.empty() && seen.size() == keys_.size(), "Incomplete policy or unused keys");
    Require(expanded.size() <= 65536, "Expanded policy is too large");
    FlatSigningProvider provider;
    std::string error;
    descriptors_ = Parse(expanded, provider, error);
    Require(descriptors_.size() == 2 && provider.keys.empty(), "Invalid public receive/change policy");
    for (const auto& descriptor : descriptors_) {
        Require(descriptor->IsRange() && descriptor->IsSolvable() && descriptor->IsSingleType(), "Invalid account descriptor");
        Require(descriptor->Warnings().empty(), "Core reported an unsafe descriptor");
        Require(descriptor->GetKeyCount() == references, "Every key must use a policy reference");
        std::set<CPubKey> bare;
        std::set<CExtPubKey> extended;
        descriptor->GetPubKeys(bare, extended);
        Require(bare.empty() && extended == expected_keys, "Unexpected descriptor key");
    }
    public_text_ = std::move(expanded);
}

Digest Policy::ID() const
{
    DataStream stream;
    stream << uint8_t{2} << static_cast<uint8_t>(name_.size());
    stream.write(std::as_bytes(std::span{name_}));
    WriteCompactSize(stream, text_.size());
    const auto descriptor_hash = Sha256({reinterpret_cast<const unsigned char*>(text_.data()), text_.size()});
    stream.write(std::as_bytes(std::span{descriptor_hash}));
    WriteCompactSize(stream, keys_.size());
    const auto root = Merkle(keys_);
    stream.write(std::as_bytes(std::span{root}));
    return Sha256({reinterpret_cast<const unsigned char*>(stream.data()), stream.size()});
}

std::vector<size_t> Policy::OwnedKeys(const Keys& session) const
{
    std::vector<size_t> owned;
    for (size_t index = 0; index < keys_.size(); ++index) {
        const auto& key = keys_[index];
        if (key.has_origin && key.fingerprint == session.RootFingerprint()
            && session.PublicAt(key.origin) == key.key) owned.push_back(index);
    }
    return owned;
}

bool Policy::IsDefault(const Keys& session) const
{
    if (!name_.empty() || keys_.size() != 1 || OwnedKeys(session).size() != 1) return false;
    unsigned purpose = 0;
    if (text_ == "pkh(@0/**)") purpose = 44;
    else if (text_ == "sh(wpkh(@0/**))") purpose = 49;
    else if (text_ == "wpkh(@0/**)") purpose = 84;
    else if (text_ == "tr(@0/**)") purpose = 86;
    else return false;
    const auto& path = keys_[0].origin;
    return path.size() == 3 && path[0] == (0x80000000U | purpose)
        && path[1] == (mainnet_ ? 0x80000000U : 0x80000001U)
        && path[2] >= 0x80000000U && path[2] <= 0x80000064U;
}

bool Policy::Authorized(const Keys& session, std::span<const unsigned char> tag) const
{
    if (tag.size() != 32 || OwnedKeys(session).empty()) return false;
    if (IsDefault(session)) return std::all_of(tag.begin(), tag.end(), [](auto c) { return c == 0; });
    return !name_.empty() && session.VerifyTag(ID(), tag);
}

CScript Policy::Script(unsigned branch, uint32_t index) const
{
    Require(branch < 2 && index < 0x80000000U, "Invalid address position");
    std::vector<CScript> scripts;
    FlatSigningProvider provider;
    Require(descriptors_[branch]->Expand(index, provider, scripts, provider) && scripts.size() == 1,
        "Address derivation failed");
    return scripts[0];
}

std::string Policy::DescriptorText(unsigned branch) const
{
    Require(branch < 2, "Invalid address branch");
    return descriptors_[branch]->ToString();
}

std::vector<Position> Policy::Positions(const KeyOriginInfo& hint) const
{
    // Hints suggest coordinates only. Callers must compare the actual output
    // script with Script() before claiming ownership or hiding change.
    std::vector<Position> positions;
    if (hint.path.size() < 2 || hint.path.back() >= 0x80000000U) return positions;
    for (const auto& ref : references_) {
        const auto& key = keys_[ref.key];
        if (!key.has_origin || !std::equal(key.fingerprint.begin(), key.fingerprint.end(), hint.fingerprint)
            || hint.path.size() != key.origin.size() + 2
            || !std::equal(key.origin.begin(), key.origin.end(), hint.path.begin())) continue;
        const auto branch = hint.path[hint.path.size() - 2];
        if (branch == ref.receive) positions.push_back({0, hint.path.back()});
        if (branch == ref.change) positions.push_back({1, hint.path.back()});
    }
    return positions;
}

FlatSigningProvider Policy::PublicProvider(Position position) const
{
    Require(position.branch < 2 && position.index < 0x80000000U, "Invalid address position");
    FlatSigningProvider provider;
    std::vector<CScript> scripts;
    Require(descriptors_[position.branch]->Expand(position.index, provider, scripts, provider)
        && scripts.size() == 1, "Descriptor expansion failed");
    return provider;
}

FlatSigningProvider Policy::PrivateProvider(Position position, const Keys& session) const
{
    auto provider = PublicProvider(position);
    FlatSigningProvider accounts;
    for (const auto index : OwnedKeys(session)) {
        auto account = session.Derive(keys_[index].origin);
        accounts.keys.emplace(keys_[index].key.pubkey.GetID(), account.key);
        memory_cleanse(account.chaincode.begin(), account.chaincode.size());
    }
    descriptors_[position.branch]->ExpandPrivate(position.index, accounts, provider);
    Require(!provider.keys.empty(), "No private policy keys available");
    return provider;
}
}
