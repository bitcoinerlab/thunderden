#include "scan.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <bit>
#include <cerrno>
#include <stdexcept>

namespace td {
namespace {
static_assert(std::endian::native == std::endian::little);
// Five uint32s: kind, width, height, progress, payload length.
constexpr uint32_t PREVIEW = 1, PSBT = 2, REQUEST = 3, FAILURE = 4;
void Write(std::span<const uint8_t> bytes)
{
    while (!bytes.empty()) {
        const auto count = write(STDOUT_FILENO, bytes.data(), bytes.size());
        if (count < 0 && errno == EINTR) continue;
        Require(count > 0, "Scanner pipe closed");
        bytes = bytes.subspan(count);
    }
}
void Send(const std::array<uint32_t, 5>& header, std::span<const uint8_t> bytes)
{
    Write({reinterpret_cast<const uint8_t*>(header.data()), sizeof(header)});
    Write(bytes);
}
}

std::string ScannerExecutable()
{
    std::array<char, 4096> path;
    const auto size = readlink("/proc/self/exe", path.data(), path.size());
    Require(size > 0 && size < ssize_t(path.size()), "Cannot locate scanner executable");
    const std::string executable(path.data(), size);
    return executable.substr(0, executable.rfind('/') + 1) + "thunderden-scanner";
}

ScanProcess::ScanProcess(const std::string& executable)
{
    int pipefd[2];
    Require(pipe2(pipefd, O_CLOEXEC) == 0, "Cannot create scanner pipe");
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    const int action_status = posix_spawn_file_actions_init(&actions);
    const int attr_status = posix_spawnattr_init(&attributes);
    const int flags = fcntl(pipefd[0], F_GETFL);
    bool configured = !action_status && !attr_status && flags >= 0
        && fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK) == 0;
    if (configured) {
        configured = posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO) == 0
            && posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0) == 0
            && posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0) == 0
            && posix_spawn_file_actions_addclosefrom_np(&actions, 3) == 0
            && posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSID) == 0;
    }
    char* argv[]{const_cast<char*>(executable.c_str()), nullptr};
    char* env[]{const_cast<char*>("LANG=C"), const_cast<char*>("LC_ALL=C"), nullptr};
    const int status = configured ? posix_spawn(&child_, executable.c_str(), &actions, &attributes, argv, env) : -1;
    if (!action_status) posix_spawn_file_actions_destroy(&actions);
    if (!attr_status) posix_spawnattr_destroy(&attributes);
    close(pipefd[1]);
    if (status != 0) { close(pipefd[0]); throw std::invalid_argument("Cannot start scanner"); }
    fd_ = pipefd[0];
}

ScanProcess::~ScanProcess()
{
    close(fd_);
    // Reap before returning to review. A compromised/hung worker cannot retain
    // devices or continue running behind an approval screen.
    kill(child_, SIGKILL);
    while (waitpid(child_, nullptr, 0) < 0 && errno == EINTR) {}
}

bool ScanProcess::Read(std::span<uint8_t> bytes, size_t& received)
{
    const auto count = read(fd_, bytes.data() + received, bytes.size() - received);
    if (count < 0 && (errno == EAGAIN || errno == EINTR)) return false;
    Require(count > 0, "Camera/QR worker exited unexpectedly; retry the scan");
    received += count;
    return received == bytes.size();
}

std::optional<ScanUpdate> ScanProcess::Poll()
{
    Require(!complete_, "Scan already complete");
    Require(std::chrono::steady_clock::now() < deadline_, preview_seen_
        ? "Camera stopped producing frames for 10 seconds; retry the scan"
        : "Camera startup timed out after 10 seconds; retry the scan");
    pollfd descriptor{fd_, POLLIN, 0};
    const int status = poll(&descriptor, 1, 50);
    if (!status || (status < 0 && errno == EINTR)) return {};
    Require(status > 0, "Scanner pipe failed");
    if (header_bytes_ != sizeof(header_) && !Read({reinterpret_cast<uint8_t*>(header_.data()), sizeof(header_)}, header_bytes_)) return {};
    const auto [kind, width, height, progress, size] = header_;
    if (!checked_) {
        if (kind == FAILURE) {
            Require(width == 0 && height == 0 && size == 0, "Invalid scanner failure record");
            switch (static_cast<ScanFailure>(progress)) {
            case ScanFailure::Startup: throw std::invalid_argument("Scanner initialization failed");
            case ScanFailure::Camera: throw std::invalid_argument("Cannot open or configure a webcam");
            case ScanFailure::CameraAccess: throw std::invalid_argument("Camera access denied; try a new session");
            case ScanFailure::Confinement: throw std::invalid_argument("Scanner isolation setup failed");
            case ScanFailure::Capture: throw std::invalid_argument("Camera frame capture failed; retry the scan");
            case ScanFailure::Decode: throw std::invalid_argument("QR image decoding failed");
            case ScanFailure::Request: throw std::invalid_argument("Invalid or unsupported UR v2 request");
            }
            throw std::invalid_argument("Invalid scanner failure code");
        }
        if (kind == PREVIEW) {
            Require(width > 0 && width <= 1920 && height > 0 && height <= 1080
                && size == width * height && progress <= 100, "Invalid scanner preview");
        } else {
            Require((kind == PSBT || kind == REQUEST) && width == 0 && height == 0 && progress == 0
                && size > 0 && size <= MAX_UR_MESSAGE, "Invalid scanner result");
        }
        payload_.resize(size);
        checked_ = true;
    }
    if (!Read(payload_, payload_bytes_)) return {};
    ScanUpdate update;
    if (kind == PREVIEW) {
        update = {width, height, progress, std::move(payload_), {}};
        preview_seen_ = true;
        deadline_ = std::chrono::steady_clock::now() + SCAN_FRAME_TIMEOUT;
    } else {
        update.message = QRMessage{kind == PSBT ? "crypto-psbt" : "bytes", std::move(payload_)};
        complete_ = true;
    }
    header_bytes_ = payload_bytes_ = 0;
    checked_ = false;
    return update;
}

void SendPreview(std::span<const uint8_t> gray, unsigned width, unsigned height, unsigned progress)
{
    Send({PREVIEW, width, height, progress, static_cast<uint32_t>(gray.size())}, gray);
}

void SendScanResult(const QRMessage& message)
{
    Require(message.type == "crypto-psbt" || message.type == "bytes", "Unsupported scanner result");
    Send({message.type == "crypto-psbt" ? PSBT : REQUEST, 0, 0, 0, static_cast<uint32_t>(message.cbor.size())}, message.cbor);
}

void SendScanFailure(ScanFailure failure) noexcept
{
    const std::array<uint32_t, 5> header{FAILURE, 0, 0, static_cast<uint32_t>(failure), 0};
    // A fixed-size diagnostic only; a broken pipe is handled as an unexpected exit.
    const auto ignored = write(STDOUT_FILENO, header.data(), sizeof(header));
    (void)ignored;
}
}
