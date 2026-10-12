#include "terminal.h"
#include "seed_ocr.h"
#include <algorithm>
#include <optional>

namespace td {
SeedAction Terminal::EditSeedWords(SeedDraft& draft, size_t& selected)
{
    Require(draft.words.size() <= MAX_DRAFT_WORDS, "Too many recovery-word slots");
    enum class Mode { Select, Edit, Delete };
    Mode mode = Mode::Select;
    bool inserting = false, replace = true;
    size_t first = 0, cursor = 0, restore_selection = 0, overflow = 0;
    int suggestion = -1;
    SecretBytes input;
    std::string error;
    std::array<bool, MAX_DRAFT_WORDS> seen{};
    std::array<bool, MAX_DRAFT_WORDS> before_insert{};
    std::optional<Layout> previous;
    Flush();
    const auto start_edit = [&] {
        const auto& word = draft.words[selected];
        input = word.value;
        suggestion = -1;
        cursor = input.size(); overflow = 0; replace = true;
        mode = Mode::Edit;
    };
    while (true) {
        auto& words = draft.words;
        if (selected >= words.size()) selected = words.empty() ? 0 : words.size() - 1;
        auto view = View();
        view.height += 3; // This editor uses the footer area too, without a separate pager strip.
        const bool editing = mode == Mode::Edit;
        const auto count = editing ? words[selected].alternatives.size() : 0;
        ReviewLines controls;
        const auto actions = [&](std::initializer_list<std::string_view> group) {
            std::string line;
            for (const auto label : group) {
                if (!line.empty() && ControlGlyphs(line).size() + 2 + ControlGlyphs(label).size() > view.width) {
                    controls.push_back(std::move(line)); line.clear();
                }
                if (!line.empty()) line += "  ";
                line += label;
            }
            controls.push_back(std::move(line));
        };
        if (editing) {
            if (count) actions({suggestion < 0 ? "Down: Choose suggestion" : "Up/Down: Choose or return to edit"});
            actions({suggestion < 0 ? "Enter: Save word" : "Enter: Use word", "Esc: Cancel edit"});
        } else if (mode == Mode::Delete) actions({"Enter: Remove word", "Esc: Keep word"});
        else {
            actions({"Up/Down: Select word", "Enter: Edit word"});
            actions({"D: Delete word", "A: Insert above", "B: Insert below"});
            actions({"R: Capture again", "T: Type words manually"});
            actions({"Esc: Back"});
        }
        const size_t available = view.height - controls.size();
        const size_t suggestions = std::min({size_t(3), count, available - 2});
        const size_t expansion = suggestions ? suggestions + 1 : mode == Mode::Delete ? 1 : 0;
        const size_t slots = available - expansion;
        if (selected < first) first = selected;
        if (selected >= first + slots) first = selected - slots + 1;
        first = std::min(first, words.empty() ? size_t(0) : words.size() - 1);
        if (!previous || *previous != view) Draw(view, "Review recovery words", {}, "");
        previous = view;

        const bool valid = DraftChecksum(draft);
        std::string status;
        if (words.size() > MAX_SEED_WORDS) status = "Too many words: delete extra text.";
        else if (!SeedWordCount(words.size())) status = "Invalid count: use 12/15/18/21/24 words";
        else {
            size_t invalid = 0;
            for (; invalid < words.size(); ++invalid) {
                const auto& value = words[invalid].value;
                try { MnemonicWordIndex({reinterpret_cast<const char*>(value.data()), value.size()}); }
                catch (const std::invalid_argument&) { break; }
            }
            status = invalid < words.size() ? "Enter word " + std::to_string(invalid + 1)
                : valid ? "Checksum matches" : "Checksum does not match";
        }
        if (editing) status = suggestion >= 0 ? "Choose a suggestion." : replace ? "Type to replace this word." : "Edit this word.";
        At(view.body - 2, view.left); Write("\033[2K\033[1mReview recovery words (" + std::to_string(words.size()) + ")\033[0;37;40m");
        At(view.body - 1, view.left); Write("\033[2K");
        if (editing && error.empty()) Write("\033[33m");
        Write(std::string_view(error.empty() ? status : error).substr(0, view.width));
        if (editing && error.empty()) Write("\033[0;37;40m");
        if (error.empty() && valid && mode == Mode::Select) Write(" - \033[1;33mPress C to continue\033[0;37;40m");
        // Render secrets directly from their secure buffers; retain no screen-text cache.
        for (size_t row = 0; row < view.height; ++row) Row(view, row, "");
        if (words.empty()) Row(view, 0, "No words yet.");
        size_t row = 0, edit_row = 0;
        for (size_t index = first; index < words.size() && index < first + slots; ++index) {
            const auto& word = words[index];
            const bool active = index == selected;
            At(view.body + row, view.left);
            if (active) Write(editing && suggestion >= 0 ? "\033[1m" : "\033[30;43m");
            const auto prefix = std::string(active ? "> " : "  ") + std::to_string(index + 1) + ": ";
            Write(prefix);
            const auto& text = active && editing ? input : word.value;
            if (active && editing) Write("[");
            if (!text.empty()) Write({reinterpret_cast<const char*>(text.data()), text.size()});
            else if (!editing || !active) Write("[Enter word]");
            if (active && editing) Write(overflow ? "+]" : "]");
            else if (!word.value.empty() && word.uncertain) Write(" ?");
            else if (!word.value.empty() && (word.original.size() != word.value.size() || !std::equal(word.value.begin(), word.value.end(), word.original.begin(),
                [](auto a, auto b) { return a == (b >= 'A' && b <= 'Z' ? b + 'a' - 'A' : b); }))) Write(" *");
            Write("\033[0;37;40m");
            seen[index] = true;
            if (active) edit_row = row;
            ++row;
            if (active && mode == Mode::Delete) Row(view, row++, "   Remove word " + std::to_string(selected + 1) + "?");
            if (active && suggestions) {
                const auto begin = suggestion < int(suggestions) ? size_t(0) : size_t(suggestion) - suggestions + 1;
                Row(view, row++, "    Suggestions (" + std::to_string(begin + 1) + "-" + std::to_string(begin + suggestions)
                    + " of " + std::to_string(count) + ")");
                for (size_t choice = begin; choice < begin + suggestions; ++choice) {
                    At(view.body + row++, view.left + 4); Write("| ");
                    if (int(choice) == suggestion) Write("\033[30;43m");
                    Write(int(choice) == suggestion ? "> " : "  ");
                    const auto& candidate = word.alternatives[choice];
                    Write({reinterpret_cast<const char*>(candidate.data()), candidate.size()}); Write("\033[0;37;40m");
                }
            }
        }
        for (size_t i = 0; i < controls.size(); ++i) Row(view, available + i, controls[i], Tone::Controls);
        if (editing && suggestion < 0) {
            At(view.body + edit_row, view.left + 4 + std::to_string(selected + 1).size() + cursor + 1);
            Write("\033[?25h");
        } else Write("\033[?25l");
        const int key = Key();
        error.clear();
        if (editing) {
            if (key == 27 || key == 3) {
                if (inserting) { words.erase(words.begin() + selected); selected = restore_selection; seen = before_insert; }
                inserting = false; input.clear(); mode = Mode::Select;
            } else if (key == '\r' || key == '\n') {
                try {
                    const auto& value = suggestion < 0 ? input : words[selected].alternatives[suggestion];
                    Require(suggestion >= 0 || !overflow, "Word is too long (max 8 letters).");
                    MnemonicWordIndex({reinterpret_cast<const char*>(value.data()), value.size()});
                    auto& word = words[selected]; word.value = value;
                    input.clear();
                    word.alternatives.clear(); word.uncertain = false;
                    draft.checksum_suggested = false;
                    if (inserting) { draft.numbered = false; seen.fill(false); }
                    inserting = false; mode = Mode::Select;
                } catch (const std::invalid_argument&) { error = "Enter a BIP39 word (max 8 letters)."; }
            } else if ((key == KEY_UP || key == KEY_DOWN) && count) {
                suggestion = std::clamp(suggestion + (key == KEY_UP ? -1 : 1), -1, int(count) - 1);
            } else if (key == KEY_LEFT || key == KEY_RIGHT) {
                suggestion = -1;
                if (replace) cursor = key == KEY_LEFT ? 0 : input.size();
                else if (key == KEY_LEFT && cursor) --cursor;
                else if (key == KEY_RIGHT && cursor < input.size()) ++cursor;
                replace = false;
            } else if (key == 127 || key == 8 || key == KEY_DELETE) {
                if (replace) { input.clear(); cursor = overflow = 0; replace = false; }
                else if (overflow) --overflow;
                else if (key == KEY_DELETE && cursor < input.size()) input.erase(input.begin() + cursor);
                else if (key != KEY_DELETE && cursor) input.erase(input.begin() + --cursor);
                suggestion = -1;
            } else if ((key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z')) {
                if (replace) { input.clear(); cursor = overflow = 0; replace = false; }
                if (input.size() < 8) input.insert(input.begin() + cursor++, key >= 'A' && key <= 'Z' ? key + 'a' - 'A' : key);
                else ++overflow;
                suggestion = -1;
            }
            continue;
        }
        if (mode == Mode::Delete) {
            if (key == 27 || key == 3) mode = Mode::Select;
            else if (key == '\r' || key == '\n') {
                words.erase(words.begin() + selected); seen.fill(false);
                draft.numbered = draft.checksum_suggested = false;
                mode = Mode::Select;
            }
            continue;
        }
        if (key == 27 || key == 3) throw Cancelled{};
        if (key == KEY_UP && selected) --selected;
        if (key == KEY_DOWN && selected + 1 < words.size()) ++selected;
        if ((key == '\r' || key == '\n') && !words.empty()) start_edit();
        if ((key == 'd' || key == 'D' || key == KEY_DELETE) && !words.empty()) mode = Mode::Delete;
        if (key == 'a' || key == 'A' || key == 'b' || key == 'B') {
            if (words.size() == MAX_DRAFT_WORDS) { error = "At most 30 slots. Remove extras first."; continue; }
            restore_selection = selected;
            before_insert = seen;
            if ((key == 'b' || key == 'B') && !words.empty()) ++selected;
            words.insert(words.begin() + selected, SeedWord{});
            inserting = true; start_edit();
        }
        if (key == 'r' || key == 'R') return SeedAction::Recapture;
        if (key == 't' || key == 'T') return SeedAction::Manual;
        if (key == 'c' || key == 'C') {
            if (!valid) error = SeedWordCount(words.size()) ? "Fill missing words or fix the checksum." : "Use 12, 15, 18, 21 or 24 words.";
            else {
                const auto unread = std::find(seen.begin(), seen.begin() + words.size(), false);
                if (unread == seen.begin() + words.size()) return SeedAction::Use;
                selected = first = unread - seen.begin();
            }
        }
    }
}
}
