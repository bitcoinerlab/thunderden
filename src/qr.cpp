#include "qr.h"
#include "transport.h"

#include <qrencode.h>
#include <memory>

namespace td {
namespace {
struct EraseQR {
    void operator()(QRcode* code) const {
        if (code) memory_cleanse(code->data, code->width * code->width);
        QRcode_free(code);
    }
};
}
QRImage::QRImage(const std::string& text, int version)
{
    Require(!text.empty() && text.size() <= MAX_QR_TEXT && text.find('\0') == text.npos, "Invalid QR payload");
    Require(version >= 0 && version <= 40, "Invalid QR version");
    const std::unique_ptr<QRcode, EraseQR> code(
        QRcode_encodeString(text.c_str(), version, QR_ECLEVEL_L, QR_MODE_8, 1));
    Require(bool(code), "QR encoding failed");
    Require(!version || code->version == version, "QR exceeded fixed display size");
    width = code->width;
    modules.assign(code->data, code->data + width * width);
    for (auto& module : modules) module &= 1;
}

QRImage::QRImage(std::span<const uint8_t> data, bool numeric)
{
    Require(!data.empty() && data.size() <= MAX_QR_TEXT, "Invalid QR payload");
    const std::unique_ptr<QRinput, decltype(&QRinput_free)> input(QRinput_new2(0, QR_ECLEVEL_L), QRinput_free);
    Require(bool(input) && QRinput_append(input.get(), numeric ? QR_MODE_NUM : QR_MODE_8, data.size(), data.data()) == 0,
        "QR encoding failed");
    const std::unique_ptr<QRcode, EraseQR> code(QRcode_encodeInput(input.get()));
    Require(bool(code), "QR encoding failed");
    width = code->width;
    modules.assign(code->data, code->data + width * width);
    for (auto& module : modules) module &= 1;
}

}
