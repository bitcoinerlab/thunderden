#pragma once
#include "seed_ocr.h"
#include "capture_area.h"
#include <opencv2/dnn.hpp>

namespace td {
// CPU-only OCR. Construct before confinement; models are fixed, local build inputs.
class OCREngine {
    cv::dnn::Net detector_, recognizer_;
    std::vector<unsigned char> characters_;
    struct Region { std::array<cv::Point2f, 4> points; cv::Rect bounds; };
    std::vector<Region> Detect(std::span<const uint8_t> gray, unsigned width, unsigned height);
public:
    explicit OCREngine(const std::string& directory);
    CaptureArea SuggestArea(std::span<const uint8_t> gray, unsigned width, unsigned height);
    std::vector<OCRToken> Read(std::span<const uint8_t> gray, unsigned width, unsigned height);
};
}
