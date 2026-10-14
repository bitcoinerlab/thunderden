#include "entropy.h"
#include "camera_entropy.h"
#include "entropy_process.h"
#include <linux/videodev2.h>
#include <univalue.h>
#include <util/strencodings.h>
#include <cstdio>
#include <numeric>

namespace {
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<typename F> void Reject(F action) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid entropy input accepted");
}
std::string Text(const td::SecretBytes& bytes) { return {bytes.begin(), bytes.end()}; }
}
int main()
{
    try {
        using td::ManualSource;
        const auto zero12 = td::EntropyMnemonic(td::SecretBytes(16));
        Check(Text(zero12) == "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about", "BIP39 128-bit vector");
        const auto zero24 = td::EntropyMnemonic(td::SecretBytes(32));
        Check(Text(zero24) == std::string("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon ")
            + "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon art", "BIP39 256-bit vector");
        Check(Text(td::EntropyMnemonic(td::SecretBytes(16, 255))) == "zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo wrong", "BIP39 all-ones vector");
        Check(Text(td::EntropyMnemonic(td::SecretBytes(16, 127))) == "legal winner thank year wave sausage worth useful legal winner thank yellow", "BIP39 nonzero reference vector");
        for (unsigned size : {16,32}) for (unsigned bit = 0; bit < size * 8; ++bit) {
            td::SecretBytes original(size), decoded(size); original[bit / 8] = 1U << (bit % 8);
            const auto text = Text(td::EntropyMnemonic(original));
            size_t begin = 0, position = 0;
            while (begin < text.size()) {
                const auto end = text.find(' ', begin);
                const auto index = td::MnemonicWordIndex(std::string_view(text).substr(begin, end == text.npos ? end : end - begin));
                for (unsigned n = 0; n < 11 && position < size * 8; ++n, ++position)
                    decoded[position / 8] |= ((index >> (10 - n)) & 1U) << (7 - position % 8);
                if (end == text.npos) break;
                begin = end + 1;
            }
            Check(decoded == original, "BIP39 entropy round trip changed a bit");
        }
        for (unsigned size : {0, 15, 17, 20, 31, 33}) Reject([&] { td::EntropyMnemonic(td::SecretBytes(size)); });
        td::SecretBytes random(32); std::iota(random.begin(), random.end(), 0);
        Check(HexStr(td::MixEntropy(12, {}, ManualSource::None, {}, random)) == "3516466a8e1c9b59d942448a9de9b95b", "Independent 12-word mixing vector");
        Check(HexStr(td::MixEntropy(24, {}, ManualSource::None, {}, random)) == "d7faee9d2c34fa852449bfc28948df7c84fdb761031b2e7d4e879079b4e7ce8b", "Independent 24-word mixing vector");
        td::ValidateMnemonic(td::EntropyMnemonic(td::MixEntropy(12, {}, ManualSource::None, {}, td::SecretBytes(32))));
        Reject([&] { td::ManualContribution(ManualSource::Coins, td::SecretBytes{2}); });
        Reject([&] { td::ManualContribution(ManualSource::Dice, td::SecretBytes{6}); });
        for (unsigned invalid : {0,2,4,255}) {
            Reject([&] { td::ManualTarget(static_cast<ManualSource>(invalid), 12); });
            Reject([&] { td::ManualContribution(static_cast<ManualSource>(invalid), td::SecretBytes{0,1}); });
            Reject([&] { td::MixEntropy(12, {}, static_cast<ManualSource>(invalid), td::SecretBytes(32), random); });
        }
        for (auto source : {ManualSource::Coins, ManualSource::Dice}) {
            const auto maximum = td::ManualTarget(source, 24);
            td::ManualContribution(source, td::SecretBytes(maximum));
            Reject([&] { td::ManualContribution(source, td::SecretBytes(maximum + 1)); });
            Reject([&] { td::ManualContribution(source, {}); });
        }
        for (unsigned words : {12,24}) {
            Check(td::ManualTarget(ManualSource::Coins, words) == 150 * words / 12, "Coin target");
            Check(td::ManualTarget(ManualSource::Dice, words) == 64 * words / 12, "Dice target");
            for (auto source : {ManualSource::Coins, ManualSource::Dice}) {
                const auto contribution = td::ManualContribution(source, td::SecretBytes{0,1});
                for (auto camera : {td::SecretBytes{}, td::SecretBytes(32)}) {
                    const auto entropy = td::MixEntropy(words, camera, source, contribution, random);
                    td::ValidateMnemonic(td::EntropyMnemonic(entropy));
                    Check(entropy != td::MixEntropy(words, camera, ManualSource::None, {}, random), "Manual source not included");
                }
            }
            Check(td::MixEntropy(words, {}, ManualSource::None, {}, random)
                != td::MixEntropy(words, td::SecretBytes(32), ManualSource::None, {}, random), "Absent camera confused with zero digest");
        }
        Reject([&] { td::MixEntropy(15, {}, ManualSource::None, {}, random); });
        Reject([&] { td::MixEntropy(12, {}, ManualSource::Coins, {}, random); });
        Reject([&] { td::MixEntropy(12, {}, ManualSource::None, {}, {}); });
        const std::vector<unsigned char> frames{10,100,200,10,101,200,10,100,200};
        std::vector<unsigned char> temporal(9), fragmented(9);
        td::TemporalSamples(frames, 3, 3, 0, temporal);
        Check(HexStr(temporal) == "0a0a0a646564c8c8c8", "Temporal serialization vector");
        td::TemporalSamples(frames, 3, 3, 0, std::span(fragmented).first(4));
        td::TemporalSamples(frames, 3, 3, 4, std::span(fragmented).subspan(4));
        Check(temporal == fragmented, "IPC fragmentation changed sample order");
        td::Digest digest; CSHA256().Write(temporal.data(), temporal.size()).Finalize(digest.data());
        Check(HexStr(digest) == "c6403665e2afe61b755d4807cc94205acd540d165444e8cfea4008c2d5a33c03", "Independent sample hash");
        const auto profile = td::CameraProfile(3, 1, V4L2_PIX_FMT_GREY, 3);
        Check(HexStr(profile) == "0159455247000000030000000100010003", "Camera profile encoding");
        const auto camera_digest = td::CameraContribution(profile, 9, digest);
        Check(HexStr(camera_digest) == "cd5b2fd8586a061df8b79693322f5fcd15a9ceddc2191e8206229b94bd4e6f4b", "Independent camera envelope vector");
        Check(HexStr(td::ManualContribution(ManualSource::Coins, td::SecretBytes{0,1}))
            == "80b27529b068505788b154c3bb40168b03108754e7a069ace8413a6b8c6da4e4", "Independent coin envelope vector");
        Check(HexStr(td::ManualContribution(ManualSource::Dice, td::SecretBytes{0,1}))
            == "dd388c612799ccdd0a6af198e19a0d8a4e09f997a0980d1cd855bf4d383dec44", "Independent dice envelope vector");
        const auto manual_digest = td::ManualContribution(ManualSource::Dice, td::SecretBytes{0,1});
        Check(HexStr(td::MixEntropy(12, camera_digest, ManualSource::Dice, manual_digest, random)) == "e9596b99edb5fc6c99d7dce5fae54b84", "Combined 12-word vector");
        Check(HexStr(td::MixEntropy(24, camera_digest, ManualSource::Dice, manual_digest, random)) == "f9188a457909e17abaf78a30bfd47e8eb6635941ddf168f5c5b971ee8018e552", "Combined 24-word vector");
        const std::vector<unsigned char> padded{10,80,20,90,99,99,30,81,40,91};
        Check(td::CameraSamples(padded, 2, 2, 6, V4L2_PIX_FMT_YUYV) == std::vector<unsigned char>({10,20,30,40}), "Y/padding selection");
        Check(td::CameraSamples(padded, 2, 2, 6, V4L2_PIX_FMT_UYVY) == std::vector<unsigned char>({80,90,81,91}), "UYVY selection");
        Check(td::CameraSamples(std::span(padded).first(6), 2, 1, 6, V4L2_PIX_FMT_BGR24)
            == std::vector<unsigned char>({20,80,10,99,99,90}), "BGR component order");
        Reject([&] { td::CameraSamples(std::span(padded).first(9), 2, 2, 6, V4L2_PIX_FMT_YUYV); });
        Reject([&] { td::CameraSamples(padded, 3, 1, 6, V4L2_PIX_FMT_YUYV); });
        Reject([&] { td::CameraSamples(padded, UINT32_MAX, 1, 6, V4L2_PIX_FMT_GREY); });
        Reject([&] { td::CameraSamples(padded, 2, 1, UINT32_MAX, V4L2_PIX_FMT_GREY); });
        Reject([&] { td::CameraProfile(640, 480, V4L2_PIX_FMT_MJPEG, 64); });
        Reject([&] { td::TemporalSamples(frames, SIZE_MAX, 64, 0, temporal); });
        const auto parse = [&](const std::string& text, int status) {
            return td::ParseAssessment({reinterpret_cast<const unsigned char*>(text.data()), text.size()}, status);
        };
        UniValue report(UniValue::VOBJ), cases(UniValue::VARR);
        report.pushKV("IID", false); report.pushKV("errorLevel", 0);
        for (unsigned i = 0; i < 10; ++i) { UniValue item(UniValue::VOBJ); item.pushKV("testCaseDesc", "Fixture method"); cases.push_back(item); }
        UniValue overall(UniValue::VOBJ); overall.pushKV("testCaseDesc", "Overall"); overall.pushKV("dataWordSize", 8); overall.pushKV("hAssessed", 0.25);
        cases.push_back(overall); report.pushKV("testCases", cases);
        Check(parse(report.write(), 0).bits_per_sample == 0.25, "Overall result was rescaled");
        Reject([&] { parse(report.write(), 1); });
        for (const auto& bad : {std::string{}, std::string("{}"), report.write().substr(0, 40), std::string(65537, ' ')})
            Reject([&] { parse(bad, 0); });
        for (const auto value : {"-1", "8.1", "NaN", "null"}) {
            auto text = report.write(); const auto at = text.find("0.25"); Check(at != text.npos, "Missing test estimate");
            text.replace(at, 4, value); Reject([&] { parse(text, 0); });
        }
        auto overflowing = report.write(); overflowing.replace(overflowing.find("0.25"), 4, "1e999");
        bool rejected = false;
        try { parse(overflowing, 0); } catch (const std::exception&) { rejected = true; }
        Check(rejected, "Non-finite estimate accepted");
        report.pushKV("errorMessage", "Public diagnostic fixture");
        Check(parse(report.write(), 0).warning, "Assessment warning lost");
        Check(parse("{\"IID\":false,\"errorLevel\":-1,\"errorMessage\":\"Symbol alphabet consists of 1 symbol. No entropy awarded...\"}", 255).constant,
            "Constant-data exit was not distinguished");
        std::puts("PASS: BIP39 reference vectors, versioned mixing, source presence, manual alphabets and limits");
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
