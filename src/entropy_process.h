#pragma once
#include "entropy.h"
#include <optional>
#include <sys/types.h>

namespace td {
class EntropyFile {
    int fd_;
public:
    EntropyFile();
    ~EntropyFile();
    EntropyFile(const EntropyFile&) = delete;
    EntropyFile& operator=(const EntropyFile&) = delete;
    int FD() const { return fd_; }
    void Append(std::span<const unsigned char> bytes);
    void Seal();
    SecretBytes Read() const;
};

// Fresh exec with a fixed environment and only the intended descriptors.
class EntropyWorker {
    pid_t pid_{-1};
    int fd_{-1};
    std::optional<int> exit_;
public:
    EntropyWorker(const std::string& executable, int input = -1, int output = -1);
    ~EntropyWorker();
    void Stop();
    EntropyWorker(const EntropyWorker&) = delete;
    EntropyWorker& operator=(const EntropyWorker&) = delete;
    size_t Read(std::span<unsigned char> bytes);
    size_t ReadAvailable(std::span<unsigned char> bytes);
    std::optional<int> PollExit();
};
std::string EntropyExecutable(bool assessment);
struct EntropyAssessment { double bits_per_sample; bool warning; bool constant; };
EntropyAssessment ParseAssessment(std::span<const unsigned char> json, int exit_code);
}
