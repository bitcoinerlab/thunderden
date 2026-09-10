#include "qr.h"
#include "transport.h"

#include <qrencode.h>
#include <memory>

namespace td {
QRImage::QRImage(const std::string& text, int version)
{
    Require(!text.empty() && text.size() <= MAX_QR_TEXT && text.find('\0') == text.npos, "Invalid QR payload");
    Require(version >= 0 && version <= 40, "Invalid QR version");
    const std::unique_ptr<QRcode, decltype(&QRcode_free)> code(
        QRcode_encodeString(text.c_str(), version, QR_ECLEVEL_L, QR_MODE_8, 1), QRcode_free);
    Require(bool(code), "QR encoding failed");
    Require(!version || code->version == version, "QR exceeded fixed display size");
    width = code->width;
    modules.assign(code->data, code->data + width * width);
    for (auto& module : modules) module &= 1;
}

}
