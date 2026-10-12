#include "ocr_engine.h"
#include "camera.h"
#include "cbor.h"
#include "isolation.h"
#include "scan.h"
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
volatile sig_atomic_t capture = 0;
void Capture(int) { capture = 1; }
}

int main()
{
    struct stat output{};
    if (fstat(1, &output) || !S_ISFIFO(output.st_mode)) return 1;
    struct sigaction action{};
    action.sa_handler = Capture; action.sa_flags = SA_RESTART;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGUSR1, &action, nullptr)) return 1;
    auto failure = td::ScanFailure::Startup;
    try {
        td::LockProcess();
        failure = td::ScanFailure::OCR;
        td::OCREngine engine(TD_OCR_MODEL_DIR);
        failure = td::ScanFailure::Camera;
        td::Camera camera(1280, 960);
        failure = td::ScanFailure::Confinement;
        td::ConfineScanner();
        while (true) {
            failure = td::ScanFailure::Capture;
            if (!camera.Capture()) continue;
            if (capture) {
                failure = td::ScanFailure::OCR;
                const auto suggested = engine.SuggestArea(camera.Gray(), camera.Width(), camera.Height());
                td::SendSeedFrame(camera.Gray(), camera.Width(), camera.Height(), suggested);
                const auto area = td::ReceiveSeedArea(camera.Width(), camera.Height());
                const cv::Mat frame(camera.Height(), camera.Width(), CV_8U, const_cast<uint8_t*>(camera.Gray().data()));
                const auto selected = frame(cv::Rect(area.x, area.y, area.width, area.height)).clone();
                const auto tokens = engine.Read({selected.data, selected.total()}, selected.cols, selected.rows);
                td::CborWriter result;
                result.Array(tokens.size());
                for (const auto& token : tokens) {
                    result.Array(10); result.UInt(token.x); result.UInt(token.y);
                    result.UInt(token.width); result.UInt(token.height); result.UInt(token.line); result.Bytes(token.text);
                    result.Array(token.candidates.size());
                    for (const auto& candidate : token.candidates) result.Bytes(candidate);
                    result.Bool(token.confident);
                    result.UInt(token.prefix_width); result.UInt(token.word_offset);
                }
                td::SendSeedScanResult(result.data);
                return 0;
            }
            td::SendPreview(camera.Gray(), camera.Width(), camera.Height(), 0);
        }
    } catch (...) { td::SendScanFailure(failure); }
    return 1;
}
