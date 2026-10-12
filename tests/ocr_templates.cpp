#include "ocr_engine.h"
#include "isolation.h"
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <cstdio>

namespace {
// Public BIP39 test vector. Templates are generated here, not copies of vendor
// artwork or customer backups. See docs/OCR.md for sources and coverage limits.
const std::vector<std::string> WORDS{"legal", "winner", "thank", "year", "wave", "sausage", "worth", "useful",
    "legal", "winner", "thank", "year", "wave", "sausage", "worth", "useful", "legal", "winner", "thank", "year",
    "wave", "sausage", "worth", "title"};

cv::Mat Card(std::string_view kind)
{
    const unsigned columns = kind == "four-columns" ? 4 : 2, rows = WORDS.size() / columns;
    const bool grid = kind.starts_with("grid");
    cv::Mat image(960, 1280, CV_8U, cv::Scalar(255));
    if (kind == "header" || kind == "blank") {
        cv::putText(image, "MY 24-WORD RECOVERY PHRASE", {40, 45}, cv::FONT_HERSHEY_SIMPLEX, 1, 0, 2, cv::LINE_AA);
        cv::putText(image, "Write down each word shown on your device and keep this sheet safe.", {40, 85}, cv::FONT_HERSHEY_SIMPLEX, .65, 0, 1, cv::LINE_AA);
        cv::putText(image, "This is a backup of your wallet. Store securely and never share it.", {40, 120}, cv::FONT_HERSHEY_SIMPLEX, .65, 0, 1, cv::LINE_AA);
    }
    for (unsigned i = 0; i < WORDS.size(); ++i) {
        const int x = 40 + i / rows * (1200 / columns), y = 190 + i % rows * (680 / rows);
        const auto number = std::to_string(i + 1);
        const auto label = kind == "bullet" ? "* " : kind == "brackets" ? "[" + number + "] "
            : kind == "parentheses" ? "(" + number + ") " : kind == "zero-padded" ? (i < 9 ? "0" : "") + number + " "
            : number + (kind == "colon" ? ": " : ". ");
        if (kind == "ruled" || kind == "header" || kind == "touching" || kind == "blank")
            cv::line(image, {x + 60, y + (kind == "touching" ? 0 : 6)}, {x + int(1100 / columns), y + (kind == "touching" ? 0 : 6)}, cv::Scalar(150), 1, cv::LINE_AA);
        if (kind == "boxes") cv::rectangle(image, {x + 62, y - 33, int(1000 / columns), 46}, cv::Scalar(190), 1);
        if (grid) for (unsigned cell = 0; cell < 8; ++cell)
            cv::rectangle(image, {x + 70 + int(cell * 36), y - 32, 36, 46}, cv::Scalar(205), 1);
        cv::putText(image, label, {x, y}, cv::FONT_HERSHEY_SIMPLEX, .75, 0, 1, cv::LINE_AA);
        if (kind == "blank") continue;
        if (grid) for (unsigned c = 0; c < WORDS[i].size(); ++c)
            cv::putText(image, std::string(1, kind == "grid-upper" ? WORDS[i][c] - 'a' + 'A' : WORDS[i][c]),
                {x + 78 + int(c * 36), y}, cv::FONT_HERSHEY_SIMPLEX, .9, 0, 2, cv::LINE_AA);
        else cv::putText(image, WORDS[i], {x + 80, y}, cv::FONT_HERSHEY_SIMPLEX, .9, 0, 2, cv::LINE_AA);
    }
    return image;
}
}

int main()
{
    try {
        td::OCREngine engine(TD_OCR_MODEL_DIR);
        td::LockProcess(); td::ConfineScanner();
        for (const auto kind : {"colon", "brackets", "parentheses", "zero-padded", "ruled", "touching", "boxes", "four-columns", "grid", "grid-upper", "bullet", "header"}) {
            const auto frame = Card(kind);
            const auto image = std::string_view(kind) == "header" ? frame(cv::Rect(20, 145, 1240, 710)).clone() : frame;
            const auto start = std::chrono::steady_clock::now();
            const auto draft = td::ReadSeedTokens(engine.Read({image.data, image.total()}, image.cols, image.rows));
            td::Require(draft.words.size() == WORDS.size(), "Template changed the word count");
            for (size_t i = 0; i < WORDS.size(); ++i)
                td::Require(std::string(draft.words[i].value.begin(), draft.words[i].value.end()) == WORDS[i], "Template word/order mismatch");
            td::Require(td::DraftChecksum(draft), "Template checksum mismatch");
            std::printf("PASS: public %s template, exact 24-word sequence, %.3f seconds\n", kind,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
        }
        for (unsigned count : {12, 24}) {
            cv::Mat image(960, 1280, CV_8U, cv::Scalar(255));
            for (unsigned i = 0; i < count; ++i) {
                const auto word = i + 1 == count ? (count == 12 ? "about" : "art") : "abandon";
                cv::putText(image, std::to_string(i + 1) + ". " + word,
                    {int(100 + (i / (count / 2)) * 400), int(200 + (i % (count / 2)) * 50)},
                    cv::FONT_HERSHEY_SIMPLEX, .9, 0, 2, cv::LINE_AA);
            }
            for (const auto& position : {cv::Point(100, 60), cv::Point(950, 300), cv::Point(100, 900)})
                cv::putText(image, "safe account label", position, cv::FONT_HERSHEY_SIMPLEX, .65, 0, 1, cv::LINE_AA);
            const auto tokens = engine.Read({image.data, image.total()}, image.cols, image.rows);
            td::Require(std::any_of(tokens.begin(), tokens.end(), [](const auto& token) { return token.y < 100; })
                && std::any_of(tokens.begin(), tokens.end(), [](const auto& token) { return token.x > 900; })
                && std::any_of(tokens.begin(), tokens.end(), [](const auto& token) { return token.y > 850; }),
                "Context fixture did not exercise detected notes");
            const auto hint = engine.SuggestArea({image.data, image.total()}, image.cols, image.rows);
            td::Require(hint.Fits(image.cols, image.rows), "Invalid suggested capture area");
            const auto selected = image(cv::Rect(75, 150, 800, 650)).clone();
            const auto draft = td::ReadSeedTokens(engine.Read({selected.data, selected.total()}, selected.cols, selected.rows));
            td::Require(draft.words.size() == count && td::DraftChecksum(draft), "Selected word area included surrounding notes");
            for (unsigned i = 0; i < count; ++i) {
                const auto expected = i + 1 == count ? (count == 12 ? "about" : "art") : "abandon";
                td::Require(std::string(draft.words[i].value.begin(), draft.words[i].value.end()) == expected, "Context fixture changed a word");
            }
            std::printf("PASS: selected area with %u words excludes surrounding notes\n", count);
        }
        {
            cv::Mat image(960, 1280, CV_8U, cv::Scalar(255));
            for (unsigned side = 0; side < 2; ++side) for (unsigned i = 0; i < 12; ++i)
                cv::putText(image, std::to_string(i + 1) + ". " + (side == 0 && i == 11 ? "about" : "abandon"),
                    {int(100 + side * 600), int(150 + i * 55)}, cv::FONT_HERSHEY_SIMPLEX, .9, 0, 2, cv::LINE_AA);
            const auto selected = image(cv::Rect(75, 100, 420, 740)).clone();
            const auto draft = td::ReadSeedTokens(engine.Read({selected.data, selected.total()}, selected.cols, selected.rows));
            td::Require(draft.words.size() == 12 && td::DraftChecksum(draft), "Selected list was mixed with another list");
        }
        const auto blank = Card("blank");
        const auto empty = td::ReadSeedTokens(engine.Read({blank.data, blank.total()}, blank.cols, blank.rows));
        td::Require(!td::DraftChecksum(empty), "Blank recovery card became a usable phrase");
        cv::Mat pair(200, 640, CV_8U, cv::Scalar(255));
        cv::putText(pair, "car pet", {60, 110}, cv::FONT_HERSHEY_SIMPLEX, 1.5, 0, 2, cv::LINE_AA);
        const auto draft = td::ReadSeedTokens(engine.Read({pair.data, pair.total()}, pair.cols, pair.rows));
        td::Require(draft.words.size() == 2 && std::string(draft.words[0].value.begin(), draft.words[0].value.end()) == "car"
            && std::string(draft.words[1].value.begin(), draft.words[1].value.end()) == "pet", "Letter-spacing handling merged complete words");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
