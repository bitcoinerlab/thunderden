#pragma once
#include "transport.h"

#include <array>
#include <chrono>
#include <sys/types.h>

namespace td {
inline constexpr auto SCAN_FRAME_TIMEOUT = std::chrono::seconds(10);
enum class ScanFailure : uint32_t {
    Startup = 1, Camera, CameraAccess, Confinement, Capture, Decode, Request
};

struct ScanUpdate {
    unsigned width{}, height{}, progress{};
    std::vector<uint8_t> gray;
    std::optional<QRMessage> message;
};

// The executable path is trusted application configuration, never request data.
std::string ScannerExecutable();
class ScanProcess {
    pid_t child_{-1};
    int fd_{-1};
    std::array<uint32_t, 5> header_{};
    std::vector<uint8_t> payload_;
    size_t header_bytes_{0}, payload_bytes_{0};
    bool checked_{false}, complete_{false};
    bool preview_seen_{false};
    std::chrono::steady_clock::time_point deadline_{std::chrono::steady_clock::now() + SCAN_FRAME_TIMEOUT};
    bool Read(std::span<uint8_t> bytes, size_t& received);
public:
    explicit ScanProcess(const std::string& executable);
    ~ScanProcess();
    ScanProcess(const ScanProcess&) = delete;
    ScanProcess& operator=(const ScanProcess&) = delete;
    // Wait at most 50 ms; partial/stalled packets never prevent local cancellation.
    std::optional<ScanUpdate> Poll();
};

// Internal, same-build, little-endian pipe protocol. No commands or approval bits.
void SendPreview(std::span<const uint8_t> gray, unsigned width, unsigned height, unsigned progress);
void SendScanResult(const QRMessage& message);
void SendScanFailure(ScanFailure failure) noexcept;
}
