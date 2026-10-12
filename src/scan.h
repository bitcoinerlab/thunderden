#pragma once
#include "transport.h"
#include "capture_area.h"

#include <array>
#include <chrono>
#include <sys/types.h>

namespace td {
inline constexpr auto SCAN_FRAME_TIMEOUT = std::chrono::seconds(10);
enum class ScanFailure : uint32_t {
    Startup = 1, Camera, CameraAccess, Confinement, Capture, Decode, Request, OCR
};

struct ScanUpdate {
    // progress is UR completion percent; OCR previews always use zero.
    unsigned width{}, height{}, progress{};
    std::vector<uint8_t> gray;
    std::optional<QRMessage> message;
    std::optional<SecretBytes> seed;
    std::optional<CaptureArea> area{}; // Frozen OCR frame and initial selection.
};

// The executable path is trusted application configuration, never request data.
std::string ScannerExecutable(std::string_view program = "thunderden-scanner");
class ScanProcess {
    pid_t child_{-1};
    int fd_{-1};
    std::array<int, 2> input_{-1, -1};
    bool seed_scan_{false};
    bool seed_captured_{false}, frame_seen_{false}, area_sent_{false};
    unsigned frame_width_{}, frame_height_{};
    std::array<uint32_t, 5> header_{};
    std::vector<uint8_t> payload_;
    size_t header_bytes_{0}, payload_bytes_{0};
    bool checked_{false}, complete_{false};
    bool preview_seen_{false};
    std::chrono::steady_clock::time_point deadline_{std::chrono::steady_clock::now() + SCAN_FRAME_TIMEOUT};
    bool Read(std::span<uint8_t> bytes, size_t& received);
public:
    explicit ScanProcess(const std::string& executable, bool seed_scan = false);
    ~ScanProcess();
    ScanProcess(const ScanProcess&) = delete;
    ScanProcess& operator=(const ScanProcess&) = delete;
    // Wait at most 50 ms; partial/stalled packets never prevent local cancellation.
    std::optional<ScanUpdate> Poll();
    void CaptureSeed();
    void ReadSeedArea(const CaptureArea& area);
};

// Internal, same-build, little-endian pipe protocol. No keys or approval bits.
void SendPreview(std::span<const uint8_t> gray, unsigned width, unsigned height, unsigned progress);
void SendScanResult(const QRMessage& message);
void SendSeedScanResult(std::span<const uint8_t> bytes);
void SendSeedFrame(std::span<const uint8_t> gray, unsigned width, unsigned height, const CaptureArea& area);
CaptureArea ReceiveSeedArea(unsigned width, unsigned height);
void SendScanFailure(ScanFailure failure) noexcept;
}
