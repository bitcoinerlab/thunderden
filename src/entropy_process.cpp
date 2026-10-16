#include "entropy_process.h"
#include "scan.h"
#include <univalue.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cmath>

namespace td {
EntropyFile::EntropyFile() : fd_(memfd_create("thunderden-entropy", MFD_CLOEXEC | MFD_ALLOW_SEALING))
{ Require(fd_ >= 0, "Cannot create anonymous entropy buffer"); }
EntropyFile::~EntropyFile() { close(fd_); }
void EntropyFile::Append(std::span<const unsigned char> bytes)
{
    while (!bytes.empty()) {
        const auto n = write(fd_, bytes.data(), bytes.size());
        if (n < 0 && errno == EINTR) continue;
        Require(n > 0, "Cannot fill entropy buffer"); bytes = bytes.subspan(n);
    }
}
void EntropyFile::Seal()
{
    Require(fcntl(fd_, F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL) == 0, "Cannot freeze entropy buffer");
}
SecretBytes EntropyFile::Read() const
{
    struct stat info{};
    Require(fstat(fd_, &info) == 0 && info.st_size > 0 && info.st_size <= 65536, "Invalid assessment output size");
    SecretBytes result(info.st_size);
    size_t received = 0;
    while (received < result.size()) {
        const auto n = pread(fd_, result.data() + received, result.size() - received, received);
        if (n < 0 && errno == EINTR) continue;
        Require(n > 0, "Incomplete assessment output"); received += n;
    }
    return result;
}

std::string EntropyExecutable(bool assessment)
{
    auto path = ScannerExecutable();
    path.replace(path.rfind('/') + 1, path.npos, assessment ? "thunderden-assess" : "thunderden-camera-entropy");
    return path;
}

EntropyWorker::EntropyWorker(const std::string& executable, int input, int output)
{
    int pipefd[2]{-1, -1};
    const bool assessment = input >= 0;
    Require(pipe2(pipefd, O_CLOEXEC | O_NONBLOCK) == 0, "Cannot create entropy pipe");
    // Reserve 3/4 for memfds and 5 for progress; remapping must not overwrite a source.
    const int in = assessment ? fcntl(input, F_DUPFD_CLOEXEC, 6) : -1;
    const int out = assessment ? fcntl(output, F_DUPFD_CLOEXEC, 6) : -1;
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    const int a = posix_spawn_file_actions_init(&actions), b = posix_spawnattr_init(&attributes);
    bool ok = !a && !b && (!assessment || (in >= 0 && out >= 0));
    if (ok) {
        // The camera writes blocking chunks; only the signer's read end is nonblocking.
        if (!assessment) ok = fcntl(pipefd[1], F_SETFL, 0) == 0;
        ok = ok && posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0) == 0
            && posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0) == 0
            && (assessment ? posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0) == 0
                && posix_spawn_file_actions_adddup2(&actions, pipefd[1], 5) == 0
                && posix_spawn_file_actions_adddup2(&actions, in, 3) == 0
                && posix_spawn_file_actions_adddup2(&actions, out, 4) == 0
                : posix_spawn_file_actions_adddup2(&actions, pipefd[1], 1) == 0)
            && posix_spawn_file_actions_addclosefrom_np(&actions, assessment ? 6 : 3) == 0
            && posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSID) == 0;
    }
    const char* env[]{"LANG=C", "LC_ALL=C", "TZ=UTC0", "OMP_NUM_THREADS=1", "OMP_THREAD_LIMIT=1", "OMP_DYNAMIC=FALSE", nullptr};
    const char* argv[]{executable.c_str(), nullptr};
    const int status = ok ? posix_spawn(&pid_, executable.c_str(), &actions, &attributes,
        const_cast<char**>(argv), const_cast<char**>(env)) : -1;
    if (!a) posix_spawn_file_actions_destroy(&actions);
    if (!b) posix_spawnattr_destroy(&attributes);
    if (in >= 0) close(in);
    if (out >= 0) close(out);
    if (pipefd[1] >= 0) close(pipefd[1]);
    if (status) { if (pipefd[0] >= 0) close(pipefd[0]); throw std::invalid_argument("Cannot start entropy worker"); }
    fd_ = pipefd[0];
}
EntropyWorker::~EntropyWorker()
{
    Stop();
    if (fd_ >= 0) close(fd_);
}
void EntropyWorker::Stop()
{
    if (pid_ > 0) {
        kill(pid_, SIGKILL);
        while (waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {}
        pid_ = -1; exit_ = 256;
    }
}
std::optional<int> EntropyWorker::PollExit()
{
    if (exit_) return exit_;
    int status;
    const auto result = waitpid(pid_, &status, WNOHANG);
    if (!result || (result < 0 && errno == EINTR)) return {};
    Require(result == pid_, "Cannot wait for entropy worker");
    pid_ = -1;
    exit_ = WIFEXITED(status) ? WEXITSTATUS(status) : 256;
    return exit_;
}
size_t EntropyWorker::Read(std::span<unsigned char> bytes)
{
    pollfd ready{fd_, POLLIN, 0};
    const auto state = poll(&ready, 1, 50);
    if (!state || (state < 0 && errno == EINTR)) return 0;
    Require(state > 0, "Entropy pipe failed");
    const auto n = read(fd_, bytes.data(), bytes.size());
    if (n < 0 && (errno == EAGAIN || errno == EINTR)) return 0;
    Require(n > 0, "Camera capture stopped before completion");
    return n;
}
size_t EntropyWorker::ReadAvailable(std::span<unsigned char> bytes)
{
    ssize_t n;
    do { n = read(fd_, bytes.data(), bytes.size()); } while (n < 0 && errno == EINTR);
    if (n < 0 && errno == EAGAIN) return 0;
    Require(n >= 0, "Camera check progress pipe failed");
    return n;
}

EntropyAssessment ParseAssessment(std::span<const unsigned char> json, int exit_code)
{
    Require(exit_code == 0 || exit_code == 255, "Camera assessment failed");
    Require(!json.empty() && json.size() <= 65536, "Invalid assessment report size");
    // Pinned UniValue has token lookahead beyond the logical end. Keep zero
    // padding for its longest lookahead, without extending the parsed span.
    SecretBytes padded(json.size() + 5);
    std::copy(json.begin(), json.end(), padded.begin());
    UniValue report;
    Require(report.read(std::string_view(reinterpret_cast<const char*>(padded.data()), json.size())) && report.isObject()
        && report["IID"].isBool() && !report["IID"].get_bool() && report["errorLevel"].isNum(), "Invalid assessment report");
    const auto& level = report["errorLevel"].getValStr();
    const auto& error = report["errorMessage"];
    Require(error.isNull() || (error.isStr() && error.get_str().size() <= 256), "Invalid assessment diagnostic");
    if (exit_code == 255 && level == "-1" && error.isStr()
        && error.get_str() == "Symbol alphabet consists of 1 symbol. No entropy awarded...") return {0, true, true};
    Require(exit_code == 0 && level == "0" && report["testCases"].isArray(), "Camera assessment failed");
    const auto& cases = report["testCases"].getValues();
    constexpr std::array<std::string_view, 11> names{"Most Common Value", "Collision Test (for bit strings only)",
        "Markov Test (for bit strings only)", "Compression Test (for bit strings only)", "T-Tuple Test", "LRS Test",
        "Multi Most Common in Window Test", "Lag Prediction Test", "Multi Markov Model with Counting Test (MultiMMC)", "LZ78Y Test", "Overall"};
    Require(cases.size() == names.size(), "Incomplete assessment cases");
    const auto& overall = cases.back();
    Require(overall.isObject() && overall["dataWordSize"].isNum() && overall["dataWordSize"].get_real() == 8
        && overall["hAssessed"].isNum(), "Invalid overall assessment");
    const bool bitstring = overall.exists("hBitstring");
    bool warning = !error.isNull();
    for (size_t i = 0; i < cases.size(); ++i) {
        const auto& item = cases[i];
        Require(item.isObject() && item["testCaseDesc"].isStr() && item["testCaseDesc"].get_str() == names[i], "Unexpected assessment case");
        for (bool bits : {false, true}) {
            const char* key = i == 4 ? (bits ? "binTTupleRes" : "tTupleRes")
                : i == 5 ? (bits ? "binLrsRes" : "lrsRes") : bits ? "hBitstring" : "hOriginal";
            const bool expected = bits ? bitstring : !bitstring || i == 0 || i >= 4;
            Require(expected || !item.exists(key), "Unexpected assessment result");
            if (!expected) continue;
            if (!item.exists(key)) {
                // Upstream may omit an unavailable compression/tuple/predictor estimate.
                Require(i > 2 && i < 10, "Missing assessment result");
                warning = true; continue;
            }
            Require(item[key].isNum(), "Invalid assessment result");
            const double value = item[key].get_real();
            Require(std::isfinite(value) && value >= 0 && value <= (bits || !bitstring ? 1 : 8), "Invalid assessment estimate");
        }
    }
    const double estimate = overall["hAssessed"].get_real();
    Require(std::isfinite(estimate) && estimate >= 0 && estimate <= (bitstring ? 8 : 1), "Invalid entropy estimate");
    return {estimate, warning, false};
}
}
