#include "qr.h"
#include "camera.h"
#include "application.h"

#include <chainparams.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv)
{
    try {
        td::Require(argc == 2, "Expected a PPM screenshot");
        std::ifstream stream(argv[1], std::ios::binary);
        std::string magic;
        unsigned width{}, height{}, maximum{};
        stream >> magic >> width >> height >> maximum;
        td::Require(magic == "P6" && width > 0 && width <= 4096 && height > 0 && height <= 4096 && maximum == 255,
            "Invalid screenshot header");
        td::Require(stream.get() == '\n', "Invalid screenshot separator");
        std::vector<uint8_t> rgb(width * height * 3);
        td::Require(bool(stream.read(reinterpret_cast<char*>(rgb.data()), rgb.size())), "Truncated screenshot");
        // Large framebuffer screenshots are reduced to the camera decoder's bounds.
        const unsigned scale = std::max((width + 1919) / 1920, (height + 1079) / 1080);
        const unsigned scan_width = width / scale, scan_height = height / scale;
        std::vector<uint8_t> gray(scan_width * scan_height);
        for (unsigned y = 0; y < scan_height; ++y) for (unsigned x = 0; x < scan_width; ++x) {
            const auto i = 3 * ((y * scale) * width + x * scale);
            gray[y * scan_width + x] = (77U * rgb[i] + 150U * rgb[i + 1] + 29U * rgb[i + 2]) >> 8;
        }
        td::QRScanner scanner;
        const auto codes = scanner.Scan(gray, scan_width, scan_height);
        td::Require(codes.size() == 1, "Expected exactly one QR in the framebuffer screenshot");
        ECC_Context context;
        SelectParams(ChainType::REGTEST);
        const std::string mnemonic = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
        td::Keys keys(std::span(reinterpret_cast<const uint8_t*>(mnemonic.data()), mnemonic.size()), {});
        const auto expected = td::PublicDescriptor(td::DefaultPolicy(keys, 84, 0));
        td::URSender encoder(expected);
        td::Require(encoder.Parts() == 1 && codes[0] == encoder.Next(),
            "Framebuffer QR differs from the expected public fixture account");
        std::cout << "PASS: booted framebuffer QR matches the expected BIP84 regtest account\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
