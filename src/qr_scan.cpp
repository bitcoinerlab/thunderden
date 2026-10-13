#include "camera.h"
#include "transport.h"
#include <memory>

namespace td {
// Separate QR decoding so the native entropy worker does not load ZBar.
QRScanner::QRScanner() : scanner_(zbar::zbar_image_scanner_create())
{
    Require(scanner_ != nullptr, "QR scanner allocation failed");
    zbar::zbar_image_scanner_set_config(scanner_, zbar::ZBAR_NONE, zbar::ZBAR_CFG_ENABLE, 0);
    zbar::zbar_image_scanner_set_config(scanner_, zbar::ZBAR_QRCODE, zbar::ZBAR_CFG_ENABLE, 1);
    zbar::zbar_image_scanner_set_config(scanner_, zbar::ZBAR_QRCODE, zbar::ZBAR_CFG_BINARY, 1);
}

QRScanner::~QRScanner() { zbar::zbar_image_scanner_destroy(scanner_); }

std::vector<std::string> QRScanner::Scan(std::span<const uint8_t> gray, unsigned width, unsigned height)
{
    Require(width > 0 && width <= 1920 && height > 0 && height <= 1080
        && gray.size() == width * height, "Invalid camera image geometry");
    using namespace zbar;
    const std::unique_ptr<zbar_image_t, decltype(&zbar_image_destroy)> image(zbar_image_create(), zbar_image_destroy);
    Require(bool(image), "QR image allocation failed");
    zbar_image_set_format(image.get(), zbar_fourcc('Y', '8', '0', '0'));
    zbar_image_set_size(image.get(), width, height);
    zbar_image_set_data(image.get(), gray.data(), gray.size(), nullptr);
    Require(zbar_scan_image(scanner_, image.get()) >= 0, "QR image scan failed");
    std::vector<std::string> result;
    for (auto symbol = zbar_image_first_symbol(image.get()); symbol; symbol = zbar_symbol_next(symbol)) {
        const auto size = zbar_symbol_get_data_length(symbol);
        if (size > 0 && size <= MAX_QR_TEXT && zbar_symbol_get_type(symbol) == ZBAR_QRCODE) {
            result.emplace_back(zbar_symbol_get_data(symbol), size);
        }
    }
    return result;
}
}
