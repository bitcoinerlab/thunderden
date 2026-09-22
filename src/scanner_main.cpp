#include "camera.h"
#include "isolation.h"
#include "scan.h"

#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

int main()
{
    struct stat output{};
    if (fstat(STDOUT_FILENO, &output) != 0 || !S_ISFIFO(output.st_mode)) return 1;
    auto failure = td::ScanFailure::Startup;
    try {
        td::LockProcess();
        failure = td::ScanFailure::Camera;
        td::Camera camera;
        failure = td::ScanFailure::Startup;
        td::QRScanner scanner;
        td::URReceiver receiver;
        failure = td::ScanFailure::Confinement;
        td::ConfineScanner();
        while (!receiver.Result()) {
            failure = td::ScanFailure::Capture;
            if (!camera.Capture()) continue;
            failure = td::ScanFailure::Decode;
            const auto frames = scanner.Scan(camera.Gray(), camera.Width(), camera.Height());
            failure = td::ScanFailure::Request;
            for (const auto& frame : frames) {
                receiver.Receive(frame);
                if (receiver.Result()) break;
            }
            if (!receiver.Result()) td::SendPreview(camera.Gray(), camera.Width(), camera.Height(), receiver.Progress() * 100);
        }
        td::SendScanResult(*receiver.Result());
        return 0;
    } catch (const std::system_error& error) {
        if (failure == td::ScanFailure::Camera && error.code() == std::errc::permission_denied)
            failure = td::ScanFailure::CameraAccess;
    } catch (...) {}
    td::SendScanFailure(failure); // Never forward decoder-provided text to the terminal.
    return 1;
}
