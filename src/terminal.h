#pragma once
#include "review.h"
#include <termios.h>

namespace td {
struct Cancelled {};

// Owns a local tty, never a pipe or QR-controlled input stream.
class Terminal {
    int fd_;
    termios saved_;
    void Write(std::string_view text);
public:
    Terminal();
    explicit Terminal(int owned_fd);
    ~Terminal();
    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;
    int FD() const { return fd_; }
    int Key(int timeout_ms = -1);
    void Flush();
    void Screen(std::string_view title, const ReviewLines& lines);
    SecretBytes Input(std::string_view prompt, size_t limit, bool hidden, SecretBytes initial = {});
    SecretBytes Mnemonic();
    bool Approve(std::string_view title, const ReviewLines& lines, std::string_view confirmation);
    void Notice(std::string_view title, const ReviewLines& lines);
};

ReviewLines Wrap(const ReviewLines& lines, size_t columns);
}
