#include "seed_qr.h"
#include "qr.h"
#include "camera.h"
#include <base58.h>
#include <util/strencodings.h>
#include <algorithm>
#include <cstdio>
#include <functional>

namespace {
void Check(bool ok, const char* error) { if (!ok) throw std::runtime_error(error); }
void Reject(const std::function<void()>& operation)
{
    try { operation(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid seed QR accepted");
}
td::SecretBytes Bytes(std::string_view text) { return {text.begin(), text.end()}; }

// SeedSigner's published SeedQR vectors, including binary NUL, LF, CR and CRLF.
// https://github.com/SeedSigner/seedsigner/blob/dev/docs/seed_qr/README.md
struct Vector { const char* words; const char* indices; const char* entropy; };
const Vector VECTORS[]{
    {"attack pizza motion avocado network gather crop fresh patrol unusual wild holiday candy pony ranch winter theme error hybrid van cereal salon goddess expire",
        "011513251154012711900771041507421289190620080870026613431420201617920614089619290300152408010643",
        "0e74b64107f94cc0ccfae6a13dcbec3662154fec67e0e00999c07892597d190a"},
    {"atom solve joy ugly ankle message setup typical bean era cactus various odor refuse element afraid meadow quick medal plate wisdom swap noble shallow",
        "011416550964188800731119157218870156061002561932122514430573003611011405110613292018175411971576",
        "0e59dde276009317f1275f1389888078c99368d1e82489b5f629531fc5b6a56e"},
    {"sound federal bonus bleak light raise false engage round stock update render quote truck quality fringe palace foot recipe labor glow tortoise potato still",
        "166206750203018810361417065805941507171219081456140818651401074412730727143709940798183613501710",
        "cfca8c658bc81962549252bc7ac3ba5b0b01d26bcae89f2b5ecebe263dcb2a36"},
    {"forum undo fragile fade shy sign arrest garment culture tube off merit",
        "073318950739065415961602009907670428187212261116", "5bbd9d71a8ec7990831aff359d426545"},
    {"good battle boil exact add seed angle hurry success glad carbon whisper",
        "080301540200062600251559007008931730078802752004", "6462686427203385c2337dd84c5089fd"},
    {"approve fruit lens brass ring actual stool coin doll boss strong rate",
        "008607501025021714880023171503630517020917211425", "0acbba008d9ba005f5996b40a3475cd9"},
    {"dignity utility vacant shiver thought canoe feel multiply item youth actor coyote",
        "049619221923158517990268067811630950204300210397", "3e1e0bc1e31e0e4315348b76dfec0a98"},
    {"corn voice scrap arrow original diamond trial property benefit choose junk lock",
        "038719631547010112530489185713790169032209701051", "307eaf058659ca7a7a0d63152509e541"},
    {"vocal tray giggle tool duck letter category pattern train magnet excite swamp",
        "196218530783182905421028028912901848107106301753", "f55cf587f2543d01090d0ae710bd3b6d"},
};

void Vectors()
{
    td::QRScanner scanner;
    for (const auto& test : VECTORS) for (const auto format : {td::SeedQRFormat::Standard, td::SeedQRFormat::Compact}) {
        const auto words = Bytes(test.words);
        const auto raw = ParseHex(test.entropy);
        const auto expected = format == td::SeedQRFormat::Standard ? Bytes(test.indices) : td::SecretBytes(raw.begin(), raw.end());
        Check(td::EncodeSeedQR(words, format) == expected, "Published seed QR encoding differs");
        td::QRMessage message{format == td::SeedQRFormat::Standard ? "seedqr" : "compact-seedqr", {}, expected};
        Check(td::DecodeSeedQR(message) == words, "Published seed QR decoding differs");
        const td::QRImage qr(expected, format == td::SeedQRFormat::Standard);
        const int modules = (format == td::SeedQRFormat::Standard ? 25 : 21) + (raw.size() == 32 ? 4 : 0);
        Check(qr.width == modules, "Seed QR used the wrong encoding mode or size");
        const unsigned width = (qr.width + 8) * 5;
        std::vector<uint8_t> gray(width * width, 255);
        for (int y = 0; y < qr.width; ++y) for (int x = 0; x < qr.width; ++x)
            for (unsigned dy = 0; dy < 5; ++dy) for (unsigned dx = 0; dx < 5; ++dx)
                gray[((y + 4) * 5 + dy) * width + (x + 4) * 5 + dx] = qr.modules[y * qr.width + x] ? 0 : 255;
        const auto frames = scanner.Scan(gray, width, width);
        Check(frames.size() == 1, "Seed QR was not optically readable");
        td::URReceiver receiver;
        receiver.Receive(frames[0]);
        Check(receiver.Result() && receiver.Result()->secret == expected && receiver.Result()->cbor.empty()
            && td::DecodeSeedQR(*receiver.Result()) == words, "Optical seed QR detection changed binary data");
        Reject([&] { td::URSender sender(message); });
    }
    std::puts("PASS: nine published 12/24-word vectors, exact QR sizes, numeric/binary optics, NUL/CR/LF and no public seed replies");
}

void Invalid()
{
    for (const auto type : {"seedqr", "compact-seedqr", "bytes"}) for (unsigned length : {0, 15, 17, 31, 33, 47, 49, 95, 97})
        Reject([&] { td::DecodeSeedQR({type, {}, td::SecretBytes(length, '0')}); });
    for (const auto& text : {std::string(48, '0'), "2048" + std::string(44, '0'), "9999" + std::string(92, '0'),
            std::string(47, '0') + "x", std::string(48, '0') + "\n"})
        Reject([&] { td::DecodeSeedQR({"seedqr", {}, Bytes(text)}); });
    Reject([&] { td::DecodeSeedQR({"compact-seedqr", {1}, td::SecretBytes(16)}); });
    Reject([&] { td::EncodeSeedQR(Bytes("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon address"), td::SeedQRFormat::Standard); });
    td::URReceiver active;
    td::URSender sender({"bytes", td::CborBytes(std::vector<uint8_t>(600))});
    active.Receive(sender.Next());
    const auto progress = active.Progress();
    Reject([&] { active.Receive(VECTORS[3].indices); });
    Reject([&] { active.Receive(std::string(16, '\0')); });
    Check(!active.Result() && active.Progress() == progress, "Seed QR replaced a multipart request");
    // Some valid Base58 addresses are exactly 32 bytes: never reinterpret one as entropy.
    bool found = false;
    for (unsigned i = 0; i < 20; ++i) {
        std::vector<uint8_t> payload(21); payload[i + 1] = 1;
        const auto address = EncodeBase58Check(payload);
        if (address.size() != 32) continue;
        td::URReceiver receiver; receiver.Receive(address);
        Check(receiver.Result()->type == "address" && receiver.Result()->secret.empty(), "CompactSeedQR swallowed an address");
        found = true;
    }
    Check(found, "Missing ambiguous-address fixture");
    // Base58 decoding tolerates surrounding whitespace; QR address routing must
    // not apply that tolerance to CompactSeedQR's exact binary payload.
    const auto short_address = EncodeBase58Check(std::vector<uint8_t>(21));
    Check(short_address.size() < 32, "Missing short-address fixture");
    for (const char padding : {'\0', ' ', '\r', '\n'}) {
        const auto binary = short_address + std::string(32 - short_address.size(), padding);
        td::URReceiver receiver; receiver.Receive(binary);
        Check(receiver.Result()->type == "compact-seedqr" && receiver.Result()->secret == Bytes(binary), "Address-like binary entropy was lost");
    }
    // Zero entropy is valid and must not be rejected as an empty C string.
    Check(td::DecodeSeedQR({"compact-seedqr", {}, td::SecretBytes(16)}) == td::EntropyMnemonic(td::SecretBytes(16)), "Zero entropy was lost");
    std::puts("PASS: strict shape/index/checksum checks, multipart isolation and 32-character address precedence");
}
}

int main()
{
    try { Vectors(); Invalid(); return 0; }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
