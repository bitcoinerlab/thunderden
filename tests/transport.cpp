#include "transport.h"
#include "qr.h"

#include <bytewords.hpp>
#include <cbor-lite.hpp>
#include <ur-decoder.hpp>
#include <util/strencodings.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <stdexcept>

namespace {
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Reject(const std::function<void()>& operation)
{
    try { operation(); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid transport input accepted");
}

std::string Upper(std::string value)
{
    for (auto& c : value) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    return value;
}

// Published bc-ur vectors, also used by independent UR implementations.
// Source: pinned bc-ur/test/test.cpp, test_single_part_ur/test_ur_encoder.
const std::string SINGLE = "ur:bytes/hdeymejtswhhylkepmykhhtsytsnoyoyaxaedsuttydmmhhpktpmsrjtgwdpfnsboxgwlbaawzuefywkdplrsrjynbvygabwjldapfcsdwkbrkch";
const std::vector<std::string> GOLDEN{
    "ur:bytes/1-9/lpadascfadaxcywenbpljkhdcahkadaemejtswhhylkepmykhhtsytsnoyoyaxaedsuttydmmhhpktpmsrjtdkgslpgh",
    "ur:bytes/2-9/lpaoascfadaxcywenbpljkhdcagwdpfnsboxgwlbaawzuefywkdplrsrjynbvygabwjldapfcsgmghhkhstlrdcxaefz",
    "ur:bytes/3-9/lpaxascfadaxcywenbpljkhdcahelbknlkuejnbadmssfhfrdpsbiegecpasvssovlgeykssjykklronvsjksopdzmol",
    "ur:bytes/4-9/lpaaascfadaxcywenbpljkhdcasotkhemthydawydtaxneurlkosgwcekonertkbrlwmplssjtammdplolsbrdzcrtas",
    "ur:bytes/5-9/lpahascfadaxcywenbpljkhdcatbbdfmssrkzmcwnezelennjpfzbgmuktrhtejscktelgfpdlrkfyfwdajldejokbwf",
    "ur:bytes/6-9/lpamascfadaxcywenbpljkhdcackjlhkhybssklbwefectpfnbbectrljectpavyrolkzczcpkmwidmwoxkilghdsowp",
    "ur:bytes/7-9/lpatascfadaxcywenbpljkhdcavszmwnjkwtclrtvaynhpahrtoxmwvwatmedibkaegdosftvandiodagdhthtrlnnhy",
    "ur:bytes/8-9/lpayascfadaxcywenbpljkhdcadmsponkkbbhgsoltjntegepmttmoonftnbuoiyrehfrtsabzsttorodklubbuyaetk",
    "ur:bytes/9-9/lpasascfadaxcywenbpljkhdcajskecpmdckihdyhphfotjojtfmlnwmadspaxrkytbztpbauotbgtgtaeaevtgavtny",
    "ur:bytes/10-9/lpbkascfadaxcywenbpljkhdcahkadaemejtswhhylkepmykhhtsytsnoyoyaxaedsuttydmmhhpktpmsrjtwdkiplzs",
    "ur:bytes/11-9/lpbdascfadaxcywenbpljkhdcahelbknlkuejnbadmssfhfrdpsbiegecpasvssovlgeykssjykklronvsjkvetiiapk",
    "ur:bytes/12-9/lpbnascfadaxcywenbpljkhdcarllaluzmdmgstospeyiefmwejlwtpedamktksrvlcygmzemovovllarodtmtbnptrs",
    "ur:bytes/13-9/lpbtascfadaxcywenbpljkhdcamtkgtpknghchchyketwsvwgwfdhpgmgtylctotzopdrpayoschcmhplffziachrfgd",
    "ur:bytes/14-9/lpbaascfadaxcywenbpljkhdcapazewnvonnvdnsbyleynwtnsjkjndeoldydkbkdslgjkbbkortbelomueekgvstegt",
    "ur:bytes/15-9/lpbsascfadaxcywenbpljkhdcaynmhpddpzmversbdqdfyrehnqzlugmjzmnmtwmrouohtstgsbsahpawkditkckynwt",
    "ur:bytes/16-9/lpbeascfadaxcywenbpljkhdcawygekobamwtlihsnpalnsghenskkiynthdzotsimtojetprsttmukirlrsbtamjtpd",
    "ur:bytes/17-9/lpbyascfadaxcywenbpljkhdcamklgftaxykpewyrtqzhydntpnytyisincxmhtbceaykolduortotiaiaiafhiaoyce",
    "ur:bytes/18-9/lpbgascfadaxcywenbpljkhdcahkadaemejtswhhylkepmykhhtsytsnoyoyaxaedsuttydmmhhpktpmsrjtntwkbkwy",
    "ur:bytes/19-9/lpbwascfadaxcywenbpljkhdcadekicpaajootjzpsdrbalpeywllbdsnbinaerkurspbncxgslgftvtsrjtksplcpeo",
    "ur:bytes/20-9/lpbbascfadaxcywenbpljkhdcayapmrleeleaxpasfrtrdkncffwjyjzgyetdmlewtkpktgllepfrltataztksmhkbot",
};

void Vectors()
{
    td::URReceiver single;
    Check(single.Progress() == 0 && single.Receive(SINGLE), "Single frame failed");
    auto data = td::UnwrapBytes(single.Result()->cbor);
    Check(data.size() == 50 && HexStr(std::span(data).first(10)) == "916ec65cf77cadf55cd7", "Published payload differs");
    td::URSender one(*single.Result());
    Check(one.Parts() == 1 && one.Next() == Upper(SINGLE), "Single frame encoding differs");

    td::URReceiver receiver;
    for (int i = 8; i >= 0; --i) receiver.Receive(Upper(GOLDEN[i]));
    Check(receiver.Result() && td::UnwrapBytes(receiver.Result()->cbor).size() == 256, "Reordered golden frames failed");
    td::URSender encoder(*receiver.Result(), 30);
    for (const auto& frame : GOLDEN) Check(encoder.Next() == Upper(frame), "Published fountain encoding differs");
    td::URReceiver missing;
    for (size_t i = 0; i < GOLDEN.size() && !missing.Result(); ++i) {
        if (i == 1 || i == 5) continue; // Recover two missing original fragments.
        missing.Receive(GOLDEN[i]);
    }
    Check(missing.Result() && missing.Result()->cbor == receiver.Result()->cbor, "Mixed-frame recovery failed");
    std::puts("PASS: published UR single/multipart vectors, reordered frames and fountain recovery");
}

std::string Frame(const std::vector<uint8_t>& cbor, std::string prefix = "ur:bytes/")
{
    return prefix + ur::Bytewords::encode(ur::Bytewords::style::minimal, cbor);
}

void Adversarial()
{
    for (const auto& bad : std::vector<std::string>{"", "B$2P0100AAAA", "p1of2 aaaa", "ur:crypto-seed/aaaa",
         "ur:bytes/", "ur:bytes/a", "ur:bytes/0-9/aaaa", "ur:bytes/4294967296-9/aaaa",
         "ur:bytes/1-4294967295/aaaa", "ur:bytes/1-9/aa/aa", "ur:bytes/aa\n"}) {
        Reject([&] { td::URReceiver{}.Receive(bad); });
    }
    Reject([&] { td::URReceiver{}.Receive(std::string(td::MAX_QR_TEXT + 1, 'a')); });
    auto corrupt = SINGLE;
    corrupt.back() = corrupt.back() == 'a' ? 'b' : 'a';
    Reject([&] { td::URReceiver{}.Receive(corrupt); });
    for (const auto& bytes : std::vector<std::vector<uint8_t>>{
        {}, {0x5b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
        {0x59, 0x10, 0x00, 1}, {0x41, 1, 2}, {0x58, 0x01, 1}, {0x9f, 0xff}, {0x61, 'x'},
    }) {
        Reject([&] { td::UnwrapBytes(bytes); });
        Reject([&] { td::URReceiver{}.Receive(Frame(bytes)); });
    }
    td::URReceiver active;
    active.Receive(GOLDEN[0]);
    const auto progress = active.Progress();
    for (int i = 0; i < 5000; ++i) Check(!active.Receive(GOLDEN[0]), "Duplicate consumed work");
    Check(active.Progress() == progress, "Duplicate changed progress");
    Reject([&] { active.Receive(SINGLE); });
    auto part = ur::Bytewords::decode(ur::Bytewords::style::minimal, GOLDEN[0].substr(GOLDEN[0].rfind('/') + 1));
    part.back() ^= 1;
    Reject([&] { active.Receive(Frame(part, "ur:bytes/1-9/")); });
    Reject([&] { active.Receive(Frame(part, "ur:crypto-psbt/1-9/")); });
    Reject([&] { td::URReceiver{}.Receive(Frame(part, "ur:bytes/2-9/")); });
    auto huge_length = ParseHex("8501091901031a010203045bffffffffffffffff");
    Reject([&] { td::URReceiver{}.Receive(Frame(huge_length, "ur:bytes/1-9/")); });
    auto enormous_count = ParseHex("85011affffffff1901031a010203044100");
    Reject([&] { td::URReceiver{}.Receive(Frame(enormous_count, "ur:bytes/1-4294967295/")); });
    for (size_t i = 1; i < 9; ++i) active.Receive(GOLDEN[i]);
    Check(active.Result().has_value(), "Invalid frames poisoned valid scan");
    std::puts("PASS: size/count/CBOR bounds, checksum failures, duplicate conflicts and BBQR rejection");
}

void Images()
{
    td::QRScanner scanner;
    std::vector<uint8_t> payload(4096);
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = (i * 41 + i / 7) & 0xff;
    td::URSender sender({"crypto-psbt", td::CborBytes(payload)});
    td::URReceiver receiver;
    ur::URDecoder reference;
    for (size_t part = 0; part < sender.Parts(); ++part) {
        const auto text = sender.Next();
        const td::QRImage qr(text);
        const int scale = 4, width = (qr.width + 8) * scale;
        std::vector<uint8_t> image(width * width, 224), rotated(width * width);
        for (int y = 0; y < qr.width; ++y) for (int x = 0; x < qr.width; ++x) {
            for (int dy = 0; dy < scale; ++dy) for (int dx = 0; dx < scale; ++dx) {
                image[((y + 4) * scale + dy) * width + (x + 4) * scale + dx] = qr.modules[y * qr.width + x] ? 32 : 224;
            }
        }
        for (int y = 0; y < width; ++y) for (int x = 0; x < width; ++x) rotated[x * width + width - 1 - y] = image[y * width + x];
        const auto found = scanner.Scan(rotated, width, width);
        Check(found.size() == 1 && found[0] == text, "QR optical round trip failed");
        receiver.Receive(found[0]);
        reference.receive_part(found[0]);
    }
    Check(receiver.Result() && td::UnwrapBytes(receiver.Result()->cbor) == payload, "Optical multipart payload differs");
    Check(reference.is_success() && reference.result_ur().cbor() == receiver.Result()->cbor, "Reference decoder disagrees");
    Reject([&] { scanner.Scan({}, 640, 480); });
    Reject([&] { scanner.Scan({}, 0xffffffff, 0xffffffff); });
    std::puts("PASS: libqrencode -> rotated low-contrast image -> ZBar -> UR payload, reference-decoder cross-check");
}
}

int main()
{
    try { Vectors(); Adversarial(); Images(); return 0; }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
