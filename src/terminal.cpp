#include "terminal.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <optional>
#include <stdexcept>

namespace td {
namespace {
void Printable(std::string_view text)
{
    Require(std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= 32 && c <= 126; }),
        "Unprintable review text");
}
}

ReviewLines Wrap(const ReviewLines& lines, size_t columns)
{
    Require(columns >= 20, "Display is too narrow");
    ReviewLines result;
    for (const auto& line : lines) {
        Printable(line);
        if (line.empty()) result.emplace_back();
        for (size_t offset = 0; offset < line.size();) {
            size_t count = std::min(columns, line.size() - offset);
            if (offset + count < line.size()) {
                const auto space = std::string_view(line).substr(offset, count + 1).rfind(' ');
                if (space != std::string_view::npos && space > 0) count = space;
            }
            result.push_back(line.substr(offset, count));
            offset += count;
            // Long values are split without losing characters. Only a separating
            // space in prose is consumed at a line break.
            if (offset < line.size() && line[offset] == ' ') ++offset;
        }
    }
    return result;
}

Terminal::Terminal() : Terminal(open("/dev/tty", O_RDWR | O_CLOEXEC)) {}

Terminal::Terminal(int fd) : fd_(fd)
{
    if (fd_ < 0) throw std::runtime_error("A local terminal is required");
    if (tcgetattr(fd_, &saved_) != 0) {
        close(fd_);
        throw std::runtime_error("Input must be a terminal");
    }
    auto raw = saved_;
    cfmakeraw(&raw);
    if (tcsetattr(fd_, TCSAFLUSH, &raw) != 0) {
        close(fd_);
        throw std::runtime_error("Cannot configure local terminal");
    }
}

Terminal::~Terminal()
{
    const char clear[] = "\033[0;37;40m\033[2J\033[3J\033[H\033[?25h";
    const auto ignored = write(fd_, clear, sizeof(clear) - 1);
    (void)ignored;
    tcsetattr(fd_, TCSAFLUSH, &saved_);
    close(fd_);
}

void Terminal::Write(std::string_view text)
{
    while (!text.empty()) {
        const auto count = write(fd_, text.data(), text.size());
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) throw std::runtime_error("Terminal disconnected");
        text.remove_prefix(count);
    }
}

int Terminal::ReadKey(int timeout_ms)
{
    if (pending_key_) { const int key = pending_key_; pending_key_ = 0; return key; }
    pollfd descriptor{fd_, POLLIN, 0};
    int status;
    do { status = poll(&descriptor, 1, timeout_ms); } while (status < 0 && errno == EINTR);
    if (status == 0) return 0;
    if (status < 0 || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))) throw std::runtime_error("Terminal disconnected");
    unsigned char key;
    if (read(fd_, &key, 1) != 1) throw std::runtime_error("Terminal disconnected");
    return key;
}

int Terminal::Key(int timeout_ms)
{
    int key = ReadKey(timeout_ms);
    if (key == 27) {
        const int prefix = ReadKey(30);
        if (prefix == '[' || prefix == 'O') {
            // Consume one bounded escape sequence, never its bytes as input text.
            for (unsigned i = 0; i < 8; ++i) {
                const int part = ReadKey(30);
                if (!part) return 0;
                if (i == 0 && prefix == '[' && part == '[') { ReadKey(30); return 0; } // Linux F1-F5.
                if (part >= '@' && part <= '~') {
                    if (i == 0 && part >= 'A' && part <= 'D') { last_cancel_ = {}; return KEY_UP + part - 'A'; }
                    return 0;
                }
            }
            return 0;
        }
        pending_key_ = prefix;
    }
    if (key == 27 || key == 3) {
        const auto now = std::chrono::steady_clock::now();
        // A tty supplies repeats, not key-up events. Keep this latch across
        // screens: held Escape/Ctrl-C must not cancel the screen behind this one.
        const bool repeated = now - last_cancel_ < std::chrono::milliseconds(1000);
        last_cancel_ = now;
        if (repeated) return 0;
    } else if (key) {
        last_cancel_ = {};
    }
    return key;
}

void Terminal::Flush() { pending_key_ = 0; tcflush(fd_, TCIFLUSH); }

Terminal::Layout Terminal::View() const
{
    winsize size{};
    Require(ioctl(fd_, TIOCGWINSZ, &size) == 0 && size.ws_col >= 40 && size.ws_row >= 12,
        "The screen is too small. At least 40 columns and 12 rows are needed.");
    const size_t width = std::min<size_t>(80, size.ws_col - 2), body = size.ws_row >= 20 ? 6 : 5;
    return {size.ws_col, size.ws_row, width, (size.ws_col - width) / 2 + 1, body, size.ws_row - body - 2};
}

void Terminal::At(size_t row, size_t column)
{
    Write("\033[" + std::to_string(row) + ";" + std::to_string(column) + "H");
}

void Terminal::Row(const Layout& view, size_t index, std::string_view text, Tone tone)
{
    Require(index < view.height && text.size() <= view.width, "Text row does not fit");
    Printable(text);
    // Only public screen text uses this combined write. Secret input is rendered
    // directly from its secure buffer, never copied into a screen cache.
    Write("\033[" + std::to_string(view.body + index) + ";" + std::to_string(view.left) + "H\033[2K"
        + (tone == Tone::Selected ? "\033[30;43m" : tone == Tone::Error ? "\033[0;31;40m" : "\033[0;37;40m")
        + std::string(text) + "\033[0;37;40m");
}

void Terminal::Draw(const Layout& view, std::string_view title, const ReviewLines& rows,
    std::string_view footer, int selected, std::string_view pager, bool warning)
{
    // Navigation prepares body rows and owns the layout. Drawing never reads keys
    // or repaginates a review; every emitted field is still checked as plain text.
    for (const auto text : {title, std::string_view(network_), pager}) Printable(text);
    const auto safe_footer = Wrap({std::string(footer)}, view.width);
    Require(title.size() <= view.width && rows.size() <= view.height && safe_footer.size() <= 2,
        "The text does not fit this screen.");
    Require(pager.size() + 2 <= view.width, "Page counter does not fit");
    Write("\033[0;37;40m\033[2J\033[3J\033[H\033[?25l");
    At(view.body - 4, view.left); Write("\033[1mTHUNDER DEN\033[0;37;40m");
    if (!network_.empty()) {
        Require(network_.size() + 14 <= view.width, "Network label does not fit");
        Write(std::string(view.width - 11 - network_.size(), ' ')); Write("\033[33m"); Write(network_);
    }
    At(view.body - 3, view.left); Write("\033[90m"); Write(std::string(view.width, '-'));
    At(view.body - 2, view.left); Write(warning ? "\033[0;31;40m" : "\033[1;37m");
    Write(title); Write("\033[0;37;40m");
    for (size_t i = 0; i < rows.size(); ++i) Row(view, i, rows[i], int(i) == selected ? Tone::Selected : Tone::Plain);
    At(view.rows - 2, view.left); Write("\033[90m"); Write(std::string(view.width, '-'));
    if (!pager.empty()) {
        At(view.rows - 2, view.left + view.width - pager.size() - 2);
        Write(" " + std::string(pager) + " ");
    }
    for (size_t i = 0; i < safe_footer.size(); ++i) { At(view.rows - 1 + i, view.left); Write(safe_footer[i]); }
    Write("\033[0;37;40m");
}

void Terminal::Screen(std::string_view title, const ReviewLines& lines, std::string_view footer)
{
    const auto view = View();
    Draw(view, title, Wrap(lines, view.width), footer);
}

int Terminal::Menu(std::string_view title, const ReviewLines& choices, std::string_view introduction,
    bool cancellable, std::string_view status)
{
    Require(!choices.empty() && choices.size() <= 9, "Invalid menu");
    Printable(status);
    size_t selected = 0, first = 0, previous_highlight = 0, previous_count = 0;
    std::optional<Layout> previous_view;
    ReviewLines previous_lines;
    Flush();
    while (true) {
        const auto view = View();
        const auto instruction = Wrap({"Use the arrow keys to choose, then press Enter."}, view.width);
        const size_t available = view.height - instruction.size() - 1;
        std::vector<ReviewLines> entries;
        size_t entry_rows = 0;
        for (const auto& choice : choices) {
            entries.push_back(Wrap({choice}, view.width - 5));
            entry_rows += entries.back().size();
        }
        auto lines = introduction.empty() ? ReviewLines{} : Wrap({std::string(introduction), ""}, view.width);
        if (lines.size() + entry_rows > available) lines.clear();
        const size_t start = lines.size(), slots = available - start;
        Require(entries[selected].size() <= slots, "Menu option does not fit this screen");
        if (!previous_view || *previous_view != view) first = 0;
        if (selected < first) first = selected;
        size_t visible = 0;
        for (size_t i = first; i <= selected; ++i) visible += entries[i].size();
        while (visible > slots) visible -= entries[first++].size();
        size_t highlight = 0;
        for (size_t i = first; i < choices.size(); ++i) {
            if (lines.size() + entries[i].size() > available) break;
            if (i == selected) highlight = lines.size();
            for (size_t row = 0; row < entries[i].size(); ++row)
                lines.push_back((row ? std::string(5, ' ') : std::string(i == selected ? "> " : "  ") + std::to_string(i + 1) + ": ")
                    + entries[i][row]);
        }
        lines.emplace_back(""); lines.insert(lines.end(), instruction.begin(), instruction.end());
        if (!previous_view || *previous_view != view) {
            Draw(view, title, lines, std::string("Up/Down: Choose   Enter/1-") + std::to_string(choices.size())
                + ": Select" + (cancellable ? "   Esc: Back" : ""), highlight);
            // Status occupies the otherwise blank header row, so it is not
            // discarded when a small screen needs to scroll the menu options.
            if (!status.empty()) {
                auto label = std::string(status);
                if (label.size() > view.width) label = label.substr(0, view.width - 3) + "...";
                At(view.body - 1, view.left); Write("\033[33m" + label + "\033[0;37;40m");
            }
            for (size_t row = 1; row < entries[selected].size(); ++row) Row(view, highlight + row, lines[highlight + row], Tone::Selected);
        } else {
            for (size_t row = 0; row < std::max(lines.size(), previous_lines.size()); ++row) {
                const bool active = row >= highlight && row < highlight + entries[selected].size();
                const bool was_active = row >= previous_highlight && row < previous_highlight + previous_count;
                if (row >= lines.size()) Row(view, row, "");
                else if (row >= previous_lines.size() || lines[row] != previous_lines[row] || active != was_active)
                    Row(view, row, lines[row], active ? Tone::Selected : Tone::Plain);
            }
        }
        previous_view = view; previous_lines = lines; previous_highlight = highlight; previous_count = entries[selected].size();
        const int key = Key();
        if ((key == 27 || key == 3) && cancellable) throw Cancelled{};
        if (key == KEY_UP && selected) --selected;
        if (key == KEY_DOWN && selected + 1 < choices.size()) ++selected;
        if (key == '\r' || key == '\n') return selected;
        if (key >= '1' && size_t(key - '1') < choices.size()) return key - '1';
    }
}

SecretBytes Terminal::Input(std::string_view title, const ReviewLines& introduction, std::string_view prompt,
    size_t limit, SecretInput* secret, SecretBytes result, std::string error, bool warning)
{
    const auto view = View();
    Printable(prompt);
    Require(prompt.size() + 2 < view.width, "Input prompt does not fit");
    Require(result.size() <= limit && std::all_of(result.begin(), result.end(), [](auto c) {
        return c >= 32 && c <= 126;
    }), "Invalid initial input");
    result.reserve(limit);
    const bool word = secret && secret->word_number;
    if (secret) secret->previous = false;
    const auto hidden_hint = !secret ? ReviewLines{} : Wrap({word
        ? "Your words are hidden as you type." : "Your passphrase is hidden as you type.", word
        ? "Press TAB if you want to see the word you are entering."
        : "Press TAB if you want to see what you are entering."}, view.width);
    const auto visible_hint = !secret ? ReviewLines{} : Wrap({word
        ? "You can see the word you are entering." : "Your passphrase is visible as you type.",
        "Press TAB to hide it."}, view.width);
    const size_t hint_rows = std::max(hidden_hint.size(), visible_hint.size());
    size_t hint_row{}, input_row{}, overflow{};
    const auto erase = [&]() { At(input_row, view.left); Write("\033[2K\033[?25l"); };
    const auto draw = [&]() {
        auto text = Wrap(error.empty() ? introduction : ReviewLines{error}, view.width);
        if (hint_rows + (secret && !text.empty() ? 1 : 0) + text.size() + 2 > view.height) {
            if (!Pages(title, error.empty() ? introduction : ReviewLines{error}, "continue", {},
                error.empty() ? PageMode::Review : PageMode::Error, warning)) throw Cancelled{};
            text.clear();
        }
        auto lines = text;
        if (!text.empty() && secret) lines.emplace_back("");
        hint_row = lines.size();
        const auto& hint = secret && secret->visible ? visible_hint : hidden_hint;
        lines.insert(lines.end(), hint.begin(), hint.end());
        lines.resize(hint_row + hint_rows);
        lines.emplace_back(""); // A blank row always separates instructions from input.
        Require(lines.size() < view.height, "Not enough room for the input field");
        Draw(view, title, lines, word ? std::string("Enter: Next   Backspace: Edit")
            + (secret->word_number > 1 ? "   Up: Previous word" : "") + "   Esc: Cancel" : "Enter: Continue   Esc: Cancel", -1, {}, warning);
        if (!error.empty()) for (size_t i = 0; i < text.size(); ++i) Row(view, i, text[i], Tone::Error);
        input_row = view.body + lines.size();
        Flush();
    };
    const auto render = [&]() {
        // Keep editing on one row. Long entries show their tail; the secure
        // buffer retains every byte, including leading/trailing spaces.
        const size_t count = std::min(result.size(), view.width - prompt.size() - 2);
        At(input_row, view.left); Write("\033[2K"); Write(prompt);
        Write(count < result.size() ? "<" : " ");
        if (secret && !secret->visible) Write(std::string(count, '*'));
        else if (count) Write({reinterpret_cast<const char*>(result.data() + result.size() - count), count});
        if (overflow) Write("+");
        Write("\033[?25h");
    };
    draw();
    render();
    while (true) {
        const int key = Key();
        Require(View() == view, "The screen changed. Start this entry again.");
        if (key == 27 || key == 3) { erase(); throw Cancelled{}; }
        if (key == 9 && secret) {
            secret->visible = !secret->visible;
            const auto& hint = secret->visible ? visible_hint : hidden_hint;
            for (size_t i = 0; i < hint_rows; ++i) Row(view, hint_row + i, i < hint.size() ? hint[i] : "");
        } else {
            const bool previous = word && key == KEY_UP && secret->word_number > 1;
            if (key == '\r' || key == '\n' || previous) {
                if (word && result.empty() && !previous) continue;
                try {
                    Require(overflow == 0, "This entry is too long. Please enter it again.");
                    // Going back preserves a draft, but only Enter can validate it
                    // and move forwards. Overlong drafts must never be truncated.
                    if (word && !previous) MnemonicWordIndex({reinterpret_cast<const char*>(result.data()), result.size()});
                    if (secret) secret->previous = previous;
                    erase(); return result;
                } catch (const std::invalid_argument& invalid) {
                    error = word ? "Invalid word. Check your backup and try again." : invalid.what();
                    memory_cleanse(result.data(), result.size());
                    result.clear(); overflow = 0;
                    draw();
                }
            } else if (key == 127 || key == 8) {
                if (overflow) --overflow;
                else if (!result.empty()) { result.back() = 0; result.pop_back(); }
            } else if (key >= 32 && key <= 126) {
                if (result.size() == limit) ++overflow;
                else result.push_back(key);
            } else if (key > 126 && key < KEY_UP) {
                erase(); throw std::invalid_argument("Use English letters, numbers, spaces and punctuation.");
            }
        }
        render();
    }
}

SecretBytes Terminal::Mnemonic()
{
    const size_t count = 12 + 3 * Menu("Your recovery phrase",
        {"12 words", "15 words", "18 words", "21 words", "24 words"},
        "How many words are in your wallet backup?");
    SecretInput visibility;
    std::string error;
    while (true) {
        std::vector<SecretBytes> words(count);
        size_t index = 0;
        while (index < count) {
            // Move, rather than copy, a previously accepted word into its editor.
            // Rejection cannot restore an old value from this slot.
            visibility.word_number = index + 1;
            words[index] = Input("Enter your recovery words", {},
                "Word " + std::to_string(index + 1) + " of " + std::to_string(count) + ": ",
                8, &visibility, std::move(words[index]), error);
            error.clear();
            if (visibility.previous) --index;
            else ++index;
        }
        SecretBytes mnemonic;
        mnemonic.reserve(256);
        for (const auto& word : words) {
            if (!mnemonic.empty()) mnemonic.push_back(' ');
            mnemonic.insert(mnemonic.end(), word.begin(), word.end());
        }
        try { ValidateMnemonic(mnemonic); return mnemonic; }
        catch (const std::invalid_argument&) {
            error = "These words do not make a valid recovery phrase. Check your backup and enter the " + std::to_string(count) + " words again.";
        }
    }
}

bool Terminal::Pages(std::string_view title, const ReviewLines& lines, std::string_view action,
    const ReviewLines& details, PageMode mode, bool warning)
{
    const auto view = View();
    const auto wrapped = Wrap(lines, view.width);
    const bool separate = mode == PageMode::Review || mode == PageMode::Completed;
    auto expanded = details;
    if (!expanded.empty()) {
        if (!separate) {
            expanded.insert(expanded.begin(), "");
            expanded.insert(expanded.begin(), lines.begin(), lines.end());
        }
        expanded = Wrap(expanded, view.width);
    }
    const auto next = Wrap({"Press Enter to read the next page."}, view.width);
    const auto finish = Wrap({"Press Enter to " + std::string(action) + "."}, view.width);
    const auto return_to_review = Wrap({"Press Enter to return to the review."}, view.width);
    const size_t reserved = std::max({next.size(), finish.size(), separate ? return_to_review.size() : size_t{0}}) + 1;
    Require(view.height > reserved, "Not enough room for this review");
    const size_t height = view.height - reserved;
    size_t page_by_view[2]{};
    bool detailed = false;
    while (true) {
        Require(View() == view, "Display changed during review. Start the review again.");
        const auto& content = detailed ? expanded : wrapped;
        auto& page = page_by_view[detailed];
        const size_t pages = std::max<size_t>(1, (content.size() + height - 1) / height);
        const auto first = std::min(page * height, content.size());
        const auto end = std::min(first + height, content.size());
        ReviewLines visible(content.begin() + first, content.begin() + end);
        const bool last = page + 1 == pages;
        const bool inspecting = detailed && separate;
        visible.emplace_back("");
        const auto& instruction = last ? (inspecting ? return_to_review : finish) : next;
        visible.insert(visible.end(), instruction.begin(), instruction.end());
        const std::string pager = pages > 1 ? "Page " + std::to_string(page + 1) + "/" + std::to_string(pages) : "";
        std::string footer = (last ? "Enter: " + (inspecting ? std::string("Return to review") : std::string(action)) : "Right/n/Enter: Next")
            + (page ? "  Left/b: Back" : "") + (details.empty() ? "" : detailed ? "  d: Summary" : "  d: Details")
            + (mode == PageMode::Idle ? "" : mode == PageMode::Completed ? "  Esc: Finish" : "  Esc: Cancel");
        Flush();
        Draw(view, title, visible, footer, -1, pager, warning);
        if (mode == PageMode::Error || (warning && mode == PageMode::Confirm))
            for (size_t i = 0; i < end - first; ++i) Row(view, i, visible[i], Tone::Error);
        const int key = Key();
        Require(View() == view, "Display changed during review. Start the review again.");
        if (mode == PageMode::Idle) {
            if (key == 4) return false; // EOF stops the launcher rather than starting a session.
        } else if (key == 27 || key == 3 || key == 'q') return false;
        if (key == 'd' && !details.empty()) { detailed = !detailed; continue; }
        if ((key == 'b' || key == KEY_LEFT) && page) --page;
        if (key == 'n' || key == KEY_RIGHT || key == '\r' || key == '\n') {
            if (!last) ++page;
            else if (inspecting) {
                if (key == '\r' || key == '\n') detailed = false;
            }
            else if (mode == PageMode::Review || mode == PageMode::Error || key == '\r' || key == '\n') return true;
        }
    }
}

bool Terminal::Approve(std::string_view title, const ReviewLines& lines, std::string_view confirmation, const ReviewLines& details,
    bool warning, std::string_view confirmation_note)
{
    if (!Pages(title, lines, "continue", details, PageMode::Review, warning)) return false;
    try {
        const auto answer = Input(title, {"You have reached the end of this review.",
            confirmation_note.empty() ? warning ? "The fee uses unverified input amounts. Confirm only if you accept this risk."
                : "Confirm only if the details match what you intended." : std::string(confirmation_note)},
            "Type " + std::string(confirmation) + " then Enter: ", 32,
            nullptr, {}, {}, warning);
        return std::string_view(reinterpret_cast<const char*>(answer.data()), answer.size()) == confirmation;
    } catch (const Cancelled&) { return false; }
}

void Terminal::Notice(std::string_view title, const ReviewLines& lines)
{
    Pages(title, lines, "continue", {}, PageMode::Error);
}

bool Terminal::Confirm(std::string_view title, const ReviewLines& lines, std::string_view action, const ReviewLines& details, bool warning)
{
    return Pages(title, lines, action, details, PageMode::Confirm, warning);
}

bool Terminal::Revisit(const ReviewScreen& review)
{
    return Pages(review.title, review.summary, "show QR again", review.details, PageMode::Completed, review.warning);
}

bool Terminal::SessionEnded()
{
    return Pages("Session ended", {
        "Thunder Den has cleared the recovery words, passphrase and private keys it was holding in memory.", "",
        "If you have finished, turn off the laptop completely rather than leaving it asleep.", "",
        "You will need your recovery words again to start a new session."}, "start a new session", {}, PageMode::Idle);
}
}
