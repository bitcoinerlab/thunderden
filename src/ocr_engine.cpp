#include "ocr_engine.h"
#include "ocr_ctc.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <fstream>

namespace td {
namespace {
cv::Mat Tensor(const cv::Mat& gray, cv::Size size)
{
    cv::Mat image, rgb;
    cv::resize(gray, image, size);
    cv::cvtColor(image, rgb, cv::COLOR_GRAY2BGR);
    return cv::dnn::blobFromImage(rgb, 1.0 / 127.5, {}, cv::Scalar(127.5, 127.5, 127.5));
}
}

OCREngine::OCREngine(const std::string& directory)
{
    cv::setNumThreads(1);
    detector_ = cv::dnn::readNetFromONNX(directory + "/det.onnx");
    recognizer_ = cv::dnn::readNetFromONNX(directory + "/rec.onnx");
    std::ifstream dictionary(directory + "/characters.txt");
    Require(bool(dictionary), "OCR character data unavailable");
    characters_.push_back(0); // CTC blank
    for (std::string line; std::getline(dictionary, line);) {
        Require(characters_.size() < 1024 && line.size() <= 8, "Invalid OCR character data");
        characters_.push_back(line == "\xe2\x80\xa2" || line == "\xc2\xb7" ? '*' : line == "\xe2\x80\x94" ? '-'
            : line.size() == 1 && line[0] >= 32 && line[0] <= 126 ? line[0] : '?');
    }
    characters_.push_back(' ');
}

std::vector<OCREngine::Region> OCREngine::Detect(std::span<const uint8_t> gray, unsigned width, unsigned height)
{
    Require(width && height && width <= 1920 && height <= 1080 && gray.size() == size_t(width) * height, "Invalid OCR image");
    const cv::Mat source(height, width, CV_8U, const_cast<uint8_t*>(gray.data()));
    // Detect at a bounded size, but recognize text regions from the full camera frame.
    const double scale = std::min(1.0, 768.0 / std::max(width, height));
    const cv::Size size(std::max(32, int(width * scale / 32 + 0.5) * 32),
        std::max(32, int(height * scale / 32 + 0.5) * 32));
    detector_.setInput(Tensor(source, size));
    const auto detected = detector_.forward();
    Require(detected.type() == CV_32F && detected.dims == 4 && detected.size[0] == 1 && detected.size[1] == 1
        && detected.size[2] == size.height && detected.size[3] == size.width, "Unexpected OCR detection result");
    const cv::Mat scores(size, CV_32F, const_cast<float*>(detected.ptr<float>()));
    const cv::Mat mask = scores > 0.3;
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    Require(contours.size() <= 1024, "Too many image regions");
    std::vector<Region> regions;
    for (const auto& contour : contours) {
        auto rect = cv::minAreaRect(contour);
        if (std::min(rect.size.width, rect.size.height) < 3) continue;
        const auto bounds = cv::boundingRect(contour) & cv::Rect({}, size);
        cv::Mat selected(bounds.size(), CV_8U, cv::Scalar(0));
        cv::drawContours(selected, std::vector<std::vector<cv::Point>>{contour}, 0, cv::Scalar(255), cv::FILLED, 8, {}, 0, -bounds.tl());
        if (cv::mean(scores(bounds), selected)[0] < 0.5) continue;
        // The detector shrinks text regions. Expand the fitted rectangle, avoiding a polygon-offset dependency.
        const float pad = rect.size.area() * 1.5f / (2 * (rect.size.width + rect.size.height));
        rect.size.width += 2 * pad; rect.size.height += 2 * pad;
        std::array<cv::Point2f, 4> points;
        rect.points(points.data());
        std::sort(points.begin(), points.end(), [](auto a, auto b) { return a.x < b.x; });
        if (points[0].y > points[1].y) std::swap(points[0], points[1]);
        if (points[2].y > points[3].y) std::swap(points[2], points[3]);
        std::swap(points[1], points[2]); // top-left, top-right, bottom-left, bottom-right
        for (auto& p : points) {
            p.x = std::clamp(p.x * width / size.width, 0.0f, float(width - 1));
            p.y = std::clamp(p.y * height / size.height, 0.0f, float(height - 1));
        }
        regions.push_back({points, cv::boundingRect(std::vector<cv::Point2f>(points.begin(), points.end()))});
    }
    // Coarse line grouping retains separately detected numeric prefixes next to their words.
    std::sort(regions.begin(), regions.end(), [](const auto& a, const auto& b) { return a.bounds.y < b.bounds.y; });
    for (size_t first = 0; first < regions.size();) {
        size_t end = first + 1;
        while (end < regions.size() && regions[end].bounds.y - regions[first].bounds.y <= regions[first].bounds.height / 2) ++end;
        std::sort(regions.begin() + first, regions.begin() + end, [](const auto& a, const auto& b) { return a.bounds.x < b.bounds.x; });
        first = end;
    }
    return regions;
}

CaptureArea OCREngine::SuggestArea(std::span<const uint8_t> gray, unsigned width, unsigned height)
{
    const auto regions = Detect(gray, width, height);
    cv::Rect bounds;
    for (const auto& region : regions) bounds |= region.bounds;
    if (bounds.empty()) return {0, 0, width, height};
    const int pad = std::max(8U, std::min(width, height) / 100);
    bounds = cv::Rect(bounds.x - pad, bounds.y - pad, bounds.width + 2 * pad, bounds.height + 2 * pad)
        & cv::Rect(0, 0, width, height);
    return {unsigned(bounds.x), unsigned(bounds.y), unsigned(bounds.width), unsigned(bounds.height)};
}

std::vector<OCRToken> OCREngine::Read(std::span<const uint8_t> gray, unsigned width, unsigned height)
{
    const auto regions = Detect(gray, width, height);
    Require(regions.size() <= 64, "Too much text detected. Select only the recovery words.");
    const cv::Mat source(height, width, CV_8U, const_cast<uint8_t*>(gray.data()));
    std::vector<OCRToken> tokens;
    for (size_t line = 0; line < regions.size(); ++line) {
        const auto& p = regions[line].points;
        const float crop_width = std::max(cv::norm(p[1] - p[0]), cv::norm(p[3] - p[2]));
        const float crop_height = std::max(cv::norm(p[2] - p[0]), cv::norm(p[3] - p[1]));
        if (crop_width < 3 || crop_height < 3) continue;
        const int resized_width = std::clamp(int(48 * crop_width / crop_height + 0.5), 1, 640);
        const std::array<cv::Point2f, 4> destination{{{0, 0}, {float(resized_width - 1), 0}, {0, 47}, {float(resized_width - 1), 47}}};
        cv::Mat crop;
        cv::warpPerspective(source, crop, cv::getPerspectiveTransform(p.data(), destination.data()), {resized_width, 48}, cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        cv::Mat input = cv::Mat::zeros(4, std::array<int, 4>{1, 3, 48, 640}.data(), CV_32F);
        auto normalized = Tensor(crop, {resized_width, 48});
        for (unsigned channel = 0; channel < 3; ++channel) for (unsigned row = 0; row < 48; ++row)
            std::copy_n(normalized.ptr<float>() + (channel * 48 + row) * resized_width, resized_width,
                input.ptr<float>() + (channel * 48 + row) * 640);
        recognizer_.setInput(input);
        const auto recognized = recognizer_.forward();
        Require(recognized.type() == CV_32F && recognized.dims == 3 && recognized.size[0] == 1
            && recognized.size[1] <= 256 && recognized.size[2] == int(characters_.size()), "Unexpected OCR recognition result");
        const int steps = recognized.size[1], alphabet = recognized.size[2];
        // Character-cell cards can produce "w i nner" inside one detected
        // region. Join short fragments with isolated characters, never two
        // dictionary words. A valid prefix such as WIN needs multiple isolated
        // letters and a complete dictionary spelling before it can be joined.
        SecretBytes decoded;
        int prior = -1;
        for (int step = 0; step < steps; ++step) {
            const auto* row = recognized.ptr<float>() + step * alphabet;
            const int index = std::max_element(row, row + alphabet) - row;
            if (index && index != prior) decoded.push_back(characters_[index]);
            prior = index;
        }
        auto numbering = ReadOCRNumber(decoded);
        if (!std::any_of(decoded.begin(), decoded.begin() + numbering.length, [](auto c) { return c != ' ' && (c < '0' || c > '9'); }))
            numbering = {}; // A lone "1" in a letter grid can be a misread L.
        unsigned parts = 0, singles = 0, known = 0;
        bool spaced = true;
        SecretBytes joined;
        for (size_t i = numbering.length; i < decoded.size();) {
            if (decoded[i] == ' ') { ++i; continue; }
            SecretBytes part;
            while (i < decoded.size() && decoded[i] != ' ') {
                auto c = decoded[i++];
                if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
                spaced &= (c >= 'a' && c <= 'z') || c == '0' || c == '1' || c == '5' || c == '8' || c == '|';
                part.push_back(c); joined.push_back(c);
            }
            ++parts; singles += part.size() == 1;
            try { MnemonicWordIndex({reinterpret_cast<const char*>(part.data()), part.size()}); ++known; }
            catch (const std::invalid_argument&) {}
        }
        bool whole_word = false;
        if (known == 1 && singles >= 2) {
            try { MnemonicWordIndex({reinterpret_cast<const char*>(joined.data()), joined.size()}); whole_word = true; }
            catch (const std::invalid_argument&) {}
        }
        spaced &= singles && parts >= 2 && joined.size() >= 3 && joined.size() <= 8
            && (known == 0 || (known == 1 && whole_word));
        bool separate_number = spaced && numbering.length && decoded[numbering.length - 1] == ' ';
        auto scoring_alphabet = characters_;
        if (spaced) for (auto& c : scoring_alphabet) if (c == ' ') c = 0;
        const auto before = tokens.size();
        SecretBytes word;
        std::vector<int> positions;
        int first = 0, last = 0, previous = -1, begin = 0;
        const auto emit = [&](int end) {
            if (word.empty()) return;
            Require(tokens.size() < MAX_OCR_TOKENS, "Too many OCR tokens");
            const float start = std::clamp(float(first * 640) / (steps * resized_width), 0.0f, 1.0f);
            const float finish = std::clamp(float((last + 1) * 640) / (steps * resized_width), start, 1.0f);
            const auto origin = p[0] + (p[1] - p[0]) * start;
            tokens.push_back({unsigned(std::max(0.0f, origin.x)), unsigned(std::max(0.0f, origin.y)),
                unsigned(std::max(1.0f, crop_width * (finish - start))), unsigned(std::max(1.0f, crop_height)), unsigned(line), std::move(word)});
            auto& token = tokens.back();
            // Exclude an attached numeric prefix, but retain it in the raw token
            // for layout. Spaces split multiword lines into separate CTC spans.
            const auto prefix = ReadOCRNumber(token.text).length;
            if (prefix && prefix < positions.size()) {
                const float unit = crop_width * 640 / (steps * resized_width);
                const unsigned prefix_width = std::max(1U, unsigned((positions[prefix - 1] + 1 - first) * unit));
                const unsigned word_offset = unsigned((positions[prefix] - first) * unit);
                if (ReadOCRNumber(token.text).value && prefix_width <= word_offset && word_offset < token.width) {
                    token.prefix_width = prefix_width; token.word_offset = word_offset;
                }
            }
            if (std::any_of(token.text.begin() + prefix, token.text.end(), [](auto c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            })) {
                const auto from = prefix ? positions[prefix - 1] + 1 : begin;
                if (from < end) {
                    auto ranking = RankSeedWords({recognized.ptr<float>() + from * alphabet, size_t(end - from) * alphabet}, scoring_alphabet);
                    token.candidates = std::move(ranking.words);
                    token.confident = ranking.confident;
                }
            }
            word.clear();
            positions.clear();
        };
        for (int step = 0; step < steps; ++step) {
            const auto* row = recognized.ptr<float>() + step * alphabet;
            const int index = std::max_element(row, row + alphabet) - row;
            if (index && characters_[index] == ' ') {
                if (!spaced || separate_number) { emit(step); begin = step + 1; separate_number = false; }
            }
            else if (index != previous && index) {
                const auto c = characters_[index];
                Require(word.size() < 32, "OCR token too long");
                if (word.empty()) first = step;
                word.push_back(c); positions.push_back(step); last = step;
            }
            previous = index;
        }
        emit(steps);
        if (tokens.size() == before) {
            Require(tokens.size() < MAX_OCR_TOKENS, "Too many OCR tokens");
            const auto& box = regions[line].bounds;
            tokens.push_back({unsigned(box.x), unsigned(box.y), unsigned(box.width), unsigned(box.height), unsigned(line), {}});
        }
    }
    return tokens;
}
}
