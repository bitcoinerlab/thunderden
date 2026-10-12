#include "ocr_engine.h"
#include "ocr_ctc.h"
#include "isolation.h"
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <cstdio>
#include <limits>

void Ranking()
{
    std::vector<unsigned char> alphabet{0};
    for (char c = 'a'; c <= 'z'; ++c) alphabet.push_back(c);
    for (char c = 'A'; c <= 'Z'; ++c) alphabet.push_back(c);
    const auto index = [&](char c) { return std::find(alphabet.begin(), alphabet.end(), c) - alphabet.begin(); };
    for (std::string_view word : {"all", "aLl", "ZOO", "letter"}) {
        std::vector<float> probabilities(word.size() * 2 * alphabet.size());
        for (size_t i = 0; i < word.size(); ++i) {
            probabilities[(2 * i) * alphabet.size() + index(word[i])] = 1;
            probabilities[(2 * i + 1) * alphabet.size()] = 1;
        }
        const auto ranking = td::RankSeedWords(probabilities, alphabet);
        std::string expected(word);
        for (auto& c : expected) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        td::Require(ranking.confident && std::string(ranking.words.front().begin(), ranking.words.front().end()) == expected,
            "CTC case/repeated-letter alignment failed");
    }
    std::vector<float> mixed(3 * alphabet.size());
    mixed[index('a')] = mixed[alphabet.size() + index('L')] = mixed[2 * alphabet.size() + index('l')] = 1;
    auto ranking = td::RankSeedWords(mixed, alphabet);
    td::Require(ranking.confident && std::string(ranking.words.front().begin(), ranking.words.front().end()) == "all",
        "Case folding before CTC collapse lost repeated letters");
    const std::string raw = "CARBNIV";
    std::vector<float> probabilities(raw.size() * 2 * alphabet.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        probabilities[(2 * i) * alphabet.size() + index(raw[i])] = 1;
        probabilities[(2 * i + 1) * alphabet.size()] = 1;
    }
    for (const auto& [step, alternative] : {std::pair{4U, 'O'}, {5U, 'N'}, {6U, char(0)}}) {
        probabilities[2 * step * alphabet.size() + index(raw[step])] = .51;
        probabilities[2 * step * alphabet.size() + index(alternative)] = .49;
    }
    ranking = td::RankSeedWords(probabilities, alphabet);
    td::Require(ranking.confident && std::string(ranking.words.front().begin(), ranking.words.front().end()) == "carbon",
        "Vocabulary decoding lost alternatives behind the greedy CARBNIV reading");
    // Greedy decoding spells the valid word CAN. Summing the alternative R
    // alignments instead supports CAR, so dictionary words must also be scored.
    std::vector<float> valid_greedy(14 * alphabet.size());
    valid_greedy[index('c')] = valid_greedy[alphabet.size() + index('a')] = 1;
    for (size_t t = 2; t < 14; ++t) {
        valid_greedy[t * alphabet.size()] = .51;
        valid_greedy[t * alphabet.size() + index('r')] = .49;
    }
    valid_greedy[7 * alphabet.size()] = 0;
    valid_greedy[7 * alphabet.size() + index('n')] = .52;
    valid_greedy[7 * alphabet.size() + index('r')] = .48;
    ranking = td::RankSeedWords(valid_greedy, alphabet);
    td::Require(ranking.confident && std::string(ranking.words.front().begin(), ranking.words.front().end()) == "car",
        "CTC alignments failed to improve an already-valid greedy reading");
    std::fill(probabilities.begin(), probabilities.end(), 1.0f / alphabet.size());
    ranking = td::RankSeedWords(probabilities, alphabet);
    td::Require(!ranking.confident && ranking.words.size() == td::MAX_WORD_SUGGESTIONS, "Ambiguous CTC evidence was overconfident or unbounded");
    std::fill(probabilities.begin(), probabilities.end(), 0);
    for (size_t t = 0; t < probabilities.size() / alphabet.size(); ++t) probabilities[t * alphabet.size()] = 1;
    td::Require(td::RankSeedWords(probabilities, alphabet).words.empty(), "Blank CTC output invented a dictionary word");
    probabilities[0] = std::numeric_limits<float>::quiet_NaN();
    bool rejected = false;
    try { td::RankSeedWords(probabilities, alphabet); } catch (const std::invalid_argument&) { rejected = true; }
    td::Require(rejected, "Non-finite CTC probabilities were accepted");
}

int main()
{
    try {
        td::OCREngine engine(TD_OCR_MODEL_DIR);
        td::LockProcess();
        td::ConfineScanner();
        Ranking();
        {
            cv::Mat blank(960, 1280, CV_8U, cv::Scalar(255));
            td::Require(engine.SuggestArea({blank.data, blank.total()}, blank.cols, blank.rows) == td::CaptureArea{0, 0, 1280, 960},
                "Empty frame did not offer the full capture area");
            const auto square = blank(cv::Rect(0, 0, 960, 960)).clone();
            td::Require(engine.Read({square.data, square.total()}, square.cols, square.rows).empty(), "Blank selected area invented text");
        }
        for (const unsigned count : {12, 24}) for (unsigned columns : {1, 2, 3}) for (const bool numbered : {false, true}) {
            std::vector<std::string> words{"legal", "winner", "thank", "year", "wave", "sausage", "worth", "useful", "legal", "winner", "thank", "yellow"};
            if (count == 24) { words.assign(24, "abandon"); words.back() = "art"; }
            cv::Mat image(960, 1280, CV_8U, cv::Scalar(255));
            const unsigned rows = words.size() / columns;
            for (unsigned i = 0; i < words.size(); ++i) {
                const auto label = (numbered ? std::to_string(i + 1) + ". " : "") + words[i];
                cv::putText(image, label, {int(60 + (i / rows) * (1200 / columns)), int(60 + (i % rows) * std::min(65U, 840 / rows))},
                    cv::FONT_HERSHEY_SIMPLEX, 1.1, cv::Scalar(0), 2, cv::LINE_AA);
            }
            const auto start = std::chrono::steady_clock::now();
            const auto tokens = engine.Read({image.data, image.total()}, image.cols, image.rows);
            auto draft = td::ReadSeedTokens(tokens);
            td::SuggestSeedChecksum(draft);
            td::Require(draft.words.size() == words.size(), "OCR lost fixture words");
            for (size_t i = 0; i < words.size(); ++i)
                td::Require(std::string(draft.words[i].value.begin(), draft.words[i].value.end()) == words[i], "OCR fixture word/order mismatch");
            for (size_t i = 0; i < words.size(); ++i)
                td::Require(std::any_of(draft.words[i].alternatives.begin(), draft.words[i].alternatives.end(), [&](const auto& value) {
                    return std::string(value.begin(), value.end()) == words[i];
                }), "CTC shortlist lost a printed fixture word");
            td::Require(td::DraftChecksum(draft), "OCR fixture checksum mismatch");
            std::printf("PASS: native OCR public printed fixture, %u words, %u columns, numbers=%d, %.3f seconds\n", count, columns, numbered,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
        }
        {
            const std::vector<std::string> words{"legal", "winner", "thank", "year", "wave", "sausage", "worth", "useful", "legal", "winner", "thank", "yellow"};
            const std::array<const char*, 4> separators{".", ")", "/", "-"};
            cv::Mat image(960, 1280, CV_8U, cv::Scalar(255));
            for (unsigned i = 0; i < words.size(); ++i) {
                const auto label = std::to_string(i + 1) + separators[i % separators.size()] + words[i];
                cv::putText(image, label, {int(60 + (i / 6) * 600), int(100 + (i % 6) * 130)},
                    cv::FONT_HERSHEY_SIMPLEX, 1.1, cv::Scalar(0), 2, cv::LINE_AA);
            }
            const auto tokens = engine.Read({image.data, image.total()}, image.cols, image.rows);
            td::Require(std::any_of(tokens.begin(), tokens.end(), [](const auto& token) {
                return token.prefix_width && token.prefix_width <= token.word_offset && token.word_offset < token.width;
            }), "Attached prefixes lost their character-aligned geometry");
            auto draft = td::ReadSeedTokens(tokens);
            td::Require(draft.words.size() == words.size(), "Mixed number separators created extra slots");
            for (size_t i = 0; i < words.size(); ++i)
                td::Require(std::string(draft.words[i].value.begin(), draft.words[i].value.end()) == words[i], "Mixed-prefix column order was lost");
            td::Require(td::DraftChecksum(draft), "Mixed-prefix phrase checksum mismatch");
            std::puts("PASS: native OCR columns with mixed dot, bracket, slash and dash prefixes");
        }
        for (unsigned rows : {2, 6}) {
            const std::vector<std::string> words{"legal", "winner", "thank", "year", "wave", "sausage", "worth", "useful", "legal", "winner", "thank", "yellow"};
            cv::Mat image(960, 1280, CV_8U, cv::Scalar(255));
            for (unsigned i = 0; i < words.size(); ++i) {
                const auto text = (rows == 6 ? std::to_string(i + 1) + ". " : "") + words[i];
                cv::putText(image, text, {int(20 + (i % (12 / rows)) * (1200 / (12 / rows))), int(150 + (i / (12 / rows)) * 100)},
                    cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0), 2, cv::LINE_AA);
            }
            const auto draft = td::ReadSeedTokens(engine.Read({image.data, image.total()}, image.cols, image.rows));
            td::Require(draft.words.size() == words.size(), "Row-first OCR lost words");
            for (size_t i = 0; i < words.size(); ++i)
                td::Require(std::string(draft.words[i].value.begin(), draft.words[i].value.end()) == words[i], "Row-first OCR order mismatch");
            std::puts("PASS: native OCR row-first numbering / unnumbered two-line phrase");
        }
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
