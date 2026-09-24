#include "terminal.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <stdexcept>

namespace td {
ReviewLines Wrap(const ReviewLines& lines, size_t columns)
{
    Require(columns >= 20, "Display is too narrow");
    ReviewLines result;
    for (const auto& line : lines) {
        Require(std::all_of(line.begin(), line.end(), [](unsigned char c) { return c >= 32 && c <= 126; }),
            "Unprintable review text");
        if (line.empty()) result.emplace_back();
        for (size_t offset = 0; offset < line.size(); offset += columns) result.push_back(line.substr(offset, columns));
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
    const char clear[] = "\033[2J\033[H\033[?25h";
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

int Terminal::Key(int timeout_ms)
{
    pollfd descriptor{fd_, POLLIN, 0};
    int status;
    do { status = poll(&descriptor, 1, timeout_ms); } while (status < 0 && errno == EINTR);
    if (status == 0) return 0;
    if (status < 0 || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))) throw std::runtime_error("Terminal disconnected");
    unsigned char key;
    if (read(fd_, &key, 1) != 1) throw std::runtime_error("Terminal disconnected");
    return key;
}

void Terminal::Flush() { tcflush(fd_, TCIFLUSH); }

void Terminal::Screen(std::string_view title, const ReviewLines& lines)
{
    winsize size{};
    Require(ioctl(fd_, TIOCGWINSZ, &size) == 0 && size.ws_col >= 40 && size.ws_row >= 12,
        "Display needs at least 40 columns and 12 rows");
    const auto safe_title = Wrap({std::string(title)}, size.ws_col - 1);
    const auto safe_lines = Wrap(lines, size.ws_col - 1);
    Require(safe_title.size() + safe_lines.size() + 1 < size.ws_row, "Text does not fit the display");
    Write("\033[2J\033[H\033[?25l");
    for (const auto& line : safe_title) { Write(line); Write("\r\n"); }
    Write("\r\n");
    for (const auto& line : safe_lines) { Write(line); Write("\r\n"); }
}

SecretBytes Terminal::Input(std::string_view prompt, size_t limit, bool hidden, SecretBytes result)
{
    Wrap({std::string(prompt)}, 80);
    Require(result.size() <= limit && std::all_of(result.begin(), result.end(), [](auto c) {
        return c >= 32 && c <= 126;
    }), "Invalid initial input");
    Flush();
    Write(prompt);
    Write("\033[?25h");
    result.reserve(limit);
    const auto echo = [&](unsigned char value) {
        char output = hidden ? '*' : value;
        Write(std::string_view(&output, 1));
        memory_cleanse(&output, 1);
    };
    for (const auto value : result) echo(value);
    while (true) {
        const int key = Key();
        if (key == 27 || key == 3) throw Cancelled{};
        if (key == '\r' || key == '\n') { Write("\r\n\033[?25l"); return result; }
        if (key == 127 || key == 8) {
            if (!result.empty()) {
                result.back() = 0;
                result.pop_back();
                Write("\b \b");
            }
        } else if (key >= 32 && key <= 126) {
            Require(result.size() < limit, "Input is too long");
            result.push_back(key);
            echo(key);
        } else if (key > 126) {
            throw std::invalid_argument("Only printable ASCII is supported");
        }
    }
}

SecretBytes Terminal::Mnemonic()
{
    Flush();
    Screen("Recovery word count", {"1: 12 words   2: 15 words   3: 18 words",
        "4: 21 words   5: 24 words", "Enter: 12 words   Esc: Cancel"});
    int choice;
    do {
        choice = Key();
        if (choice == 27 || choice == 3) throw Cancelled{};
        if (choice == '\r' || choice == '\n') choice = '1';
    } while (choice < '1' || choice > '5');
    const size_t count = 12 + (choice - '1') * 3;
    std::string error;
    while (true) {
        std::vector<SecretBytes> words(count);
        size_t index = 0;
        while (index < count) {
            Screen("Enter recovery words", {"Words are visible on this screen.",
                "Enter: Accept   Backspace: Edit", "Empty entry: Previous word   Esc: Cancel", error});
            try {
                auto word = Input("Word " + std::to_string(index + 1) + "/" + std::to_string(count) + ": ",
                    8, false, words[index]);
                error.clear();
                if (word.empty()) { if (index > 0) --index; continue; }
                MnemonicWordIndex({reinterpret_cast<const char*>(word.data()), word.size()});
                words[index++] = std::move(word);
            } catch (const std::invalid_argument& invalid) { error = invalid.what(); }
        }
        SecretBytes mnemonic;
        mnemonic.reserve(256);
        for (const auto& word : words) {
            if (!mnemonic.empty()) mnemonic.push_back(' ');
            mnemonic.insert(mnemonic.end(), word.begin(), word.end());
        }
        try { ValidateMnemonic(mnemonic); return mnemonic; }
        catch (const std::invalid_argument&) {
            error = "Invalid recovery phrase. Enter all " + std::to_string(count) + " words again.";
        }
    }
}

bool Terminal::Approve(std::string_view title, const ReviewLines& lines, std::string_view confirmation)
{
    winsize size{};
    Require(ioctl(fd_, TIOCGWINSZ, &size) == 0 && size.ws_col >= 40 && size.ws_row >= 12, "Display needs at least 40 columns and 12 rows");
    const auto wrapped = Wrap(lines, std::min<unsigned>(size.ws_col - 1, 100));
    const size_t height = size.ws_row - 7;
    const size_t pages = std::max<size_t>(1, (wrapped.size() + height - 1) / height);
    size_t page = 0;
    while (true) {
        winsize current{};
        Require(ioctl(fd_, TIOCGWINSZ, &current) == 0 && current.ws_col == size.ws_col
            && current.ws_row == size.ws_row, "Display changed during review; restart review");
        const auto first = std::min(page * height, wrapped.size());
        const auto end = std::min(first + height, wrapped.size());
        ReviewLines visible(wrapped.begin() + first, wrapped.begin() + end);
        visible.push_back("");
        visible.push_back("Page " + std::to_string(page + 1) + "/" + std::to_string(pages));
        visible.push_back("n: Next   b: Back   Esc/q: Cancel");
        Flush();
        Screen(title, visible);
        int key = Key();
        if (key == 27) {
            if (Key(30) == '[') {
                const auto direction = Key(30);
                if (direction == 'C') key = 'n';
                if (direction == 'D') key = 'b';
            }
        }
        if (key == 27 || key == 3 || key == 'q') return false;
        if (key == 'b' && page > 0) --page;
        if (key == 'n') {
            if (++page == pages) break;
        }
    }
    if (confirmation.empty()) return true;
    Screen(title, {"Review complete.", "Approval applies only to the request just reviewed.", "Esc: Cancel"});
    try {
        const auto answer = Input("Type " + std::string(confirmation) + " then Enter: ", 32, false);
        return std::string_view(reinterpret_cast<const char*>(answer.data()), answer.size()) == confirmation;
    } catch (const Cancelled&) { return false; }
}

void Terminal::Notice(std::string_view title, const ReviewLines& lines)
{
    Approve(title, lines, {});
}

bool Terminal::Confirm(std::string_view title, const ReviewLines& lines, std::string_view action, const ReviewLines& details)
{
    winsize size{};
    Require(ioctl(fd_, TIOCGWINSZ, &size) == 0 && size.ws_col >= 40 && size.ws_row >= 12, "Display is too small");
    const auto wrapped = Wrap(lines, std::min<unsigned>(size.ws_col - 1, 100));
    const size_t height = size.ws_row - 7;
    const size_t pages = std::max<size_t>(1, (wrapped.size() + height - 1) / height);
    size_t page = 0;
    while (true) {
        winsize current{};
        Require(ioctl(fd_, TIOCGWINSZ, &current) == 0 && current.ws_col == size.ws_col
            && current.ws_row == size.ws_row, "Display changed; start again");
        const auto first = std::min(page * height, wrapped.size());
        const auto end = std::min(first + height, wrapped.size());
        ReviewLines visible(wrapped.begin() + first, wrapped.begin() + end);
        visible.push_back("");
        if (pages > 1) visible.push_back("Page " + std::to_string(page + 1) + "/" + std::to_string(pages));
        if (!details.empty()) visible.push_back("d: Details");
        visible.push_back((page ? std::string("b: Previous page   ") : "")
            + (page + 1 < pages ? "n: Next page" : "Enter: " + std::string(action)) + "   Esc: Cancel");
        Flush(); Screen(title, visible);
        const int key = Key();
        if (key == 27 || key == 3 || key == 'q') return false;
        if (key == 'd' && !details.empty()) Confirm(title, details, "Back to summary");
        if (key == 'b' && page) --page;
        if (key == 'n' && page + 1 < pages) ++page;
        if ((key == '\r' || key == '\n') && page + 1 == pages) return true;
    }
}
}
