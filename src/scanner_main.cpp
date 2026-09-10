#include "camera.h"
#include "isolation.h"
#include "scan.h"

#include <sys/stat.h>
#include <unistd.h>

int main()
{
    try {
        td::LockProcess();
        struct stat output{};
        td::Require(fstat(STDOUT_FILENO, &output) == 0 && S_ISFIFO(output.st_mode), "Scanner requires a pipe");
        td::Camera camera;
        td::QRScanner scanner;
        td::URReceiver receiver;
        td::ConfineScanner();
        while (!receiver.Result()) {
            if (!camera.Capture()) continue;
            for (const auto& frame : scanner.Scan(camera.Gray(), camera.Width(), camera.Height())) {
                receiver.Receive(frame);
                if (receiver.Result()) break;
            }
            if (!receiver.Result()) td::SendPreview(camera.Gray(), camera.Width(), camera.Height(), receiver.Progress() * 100);
        }
        td::SendScanResult(*receiver.Result());
        return 0;
    } catch (...) { return 1; } // Never forward decoder diagnostics to the terminal.
}
