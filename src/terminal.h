#pragma once
#include "review.h"
#include <chrono>
#include <termios.h>

namespace td {
struct Cancelled {};
enum NavigationKey { KEY_UP = 256, KEY_DOWN, KEY_RIGHT, KEY_LEFT };
struct SecretInput {
    unsigned word_number{0}; // One-based recovery-word position; zero for passphrases.
    bool visible{false};
    bool previous{false}; // Up returns the current draft without accepting it.
};

// Owns a local tty, never a pipe or QR-controlled input stream.
class Terminal {
    int fd_;
    termios saved_;
    std::string network_;
    int pending_key_{};
    std::chrono::steady_clock::time_point last_cancel_{};
    void Write(std::string_view text);
    int ReadKey(int timeout_ms);
    struct Layout {
        size_t columns, rows, width, left, body, height;
        bool operator==(const Layout&) const = default;
    };
    Layout View() const;
    enum class Tone { Plain, Selected, Error };
    enum class PageMode { Review, Confirm, Error, Idle };
    void At(size_t row, size_t column);
    void Row(const Layout& view, size_t index, std::string_view text, Tone tone = Tone::Plain);
    void Draw(const Layout& view, std::string_view title, const ReviewLines& rows,
        std::string_view footer, int selected = -1, std::string_view pager = {});
    bool Pages(std::string_view title, const ReviewLines& lines, std::string_view action,
        const ReviewLines& details, PageMode mode);
public:
    Terminal();
    explicit Terminal(int owned_fd);
    ~Terminal();
    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;
    int FD() const { return fd_; }
    int Key(int timeout_ms = -1);
    void Flush();
    void SetNetwork(std::string_view network) { network_ = network; }
    void Screen(std::string_view title, const ReviewLines& lines, std::string_view footer);
    int Menu(std::string_view title, const ReviewLines& choices, std::string_view introduction = {},
        bool cancellable = true);
    SecretBytes Input(std::string_view title, const ReviewLines& introduction, std::string_view prompt,
        size_t limit, SecretInput* secret = nullptr, SecretBytes initial = {}, std::string error = {});
    SecretBytes Mnemonic();
    bool Approve(std::string_view title, const ReviewLines& lines, std::string_view confirmation);
    bool Confirm(std::string_view title, const ReviewLines& lines, std::string_view action, const ReviewLines& details = {});
    void Notice(std::string_view title, const ReviewLines& lines);
    bool SessionEnded();
};

ReviewLines Wrap(const ReviewLines& lines, size_t columns);
}
