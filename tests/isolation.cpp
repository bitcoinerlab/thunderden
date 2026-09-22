#include "camera.h"
#include "isolation.h"
#include "qr.h"
#include "scan.h"

#include <linux/capability.h>
#include <linux/landlock.h>
#include <linux/videodev2.h>
#include <libv4l-plugin.h>
#include <libv4lconvert.h>
#include <jpeglib.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>

namespace {
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Header(std::array<uint32_t, 5> header)
{
    Check(write(1, header.data(), sizeof(header)) == sizeof(header), "Test header write failed");
}
td::QRMessage Message() { return {"bytes", td::CborBytes(std::vector<uint8_t>{1, 2, 3})}; }

int CameraControl(void*, int, unsigned long request, void* argument)
{
    if (request == VIDIOC_QUERYCAP) {
        auto& cap = *static_cast<v4l2_capability*>(argument);
        cap = {};
        std::strcpy(reinterpret_cast<char*>(cap.driver), "uvcvideo");
        cap.capabilities = V4L2_CAP_VIDEO_CAPTURE;
        return 0;
    }
    if (request == VIDIOC_ENUM_FMT) {
        auto& format = *static_cast<v4l2_fmtdesc*>(argument);
        if (format.index < 2) {
            format.pixelformat = format.index ? V4L2_PIX_FMT_MJPEG : V4L2_PIX_FMT_YUYV;
            return 0;
        }
    }
    errno = EINVAL;
    return -1;
}

int Worker(const std::string& mode)
{
    td::LockProcess();
    Check(!getenv("TD_SECRET_CANARY"), "Parent environment leaked");
    Check(fcntl(100, F_GETFD) == -1 && errno == EBADF, "Parent descriptor leaked");
    Check(getsid(0) == getpid(), "Worker retained controlling session");
    __user_cap_header_struct cap_header{_LINUX_CAPABILITY_VERSION_3, 0};
    __user_cap_data_struct caps[2]{};
    Check(syscall(SYS_capget, &cap_header, caps) == 0 && !caps[0].effective && !caps[1].effective
        && !caps[0].permitted && !caps[1].permitted && !caps[0].inheritable && !caps[1].inheritable
        && prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) == 1 && prctl(PR_GET_DUMPABLE) == 0,
        "Worker privileges were not reduced");
    if (mode == "oversize") { Header({2, 0, 0, 0, UINT32_MAX}); return 0; }
    if (mode == "geometry") { Header({1, UINT32_MAX, 2, 0, 1}); return 0; }
    if (mode == "progress") { Header({1, 1, 1, 101, 1}); return 0; }
    if (mode == "command") { Header({99, 0, 0, 0, 4}); return 0; }
    if (mode == "bad-failure-code") { Header({4, 0, 0, UINT32_MAX, 0}); return 0; }
    if (mode == "bad-failure-zero") { Header({4, 0, 0, 0, 0}); return 0; }
    if (mode == "bad-failure-geometry") { Header({4, 1, 0, 1, 0}); return 0; }
    if (mode == "bad-failure-payload") { Header({4, 0, 0, 1, 4}); return 0; }
    if (mode.starts_with("failure-")) {
        td::ConfineScanner();
        td::SendScanFailure(static_cast<td::ScanFailure>(mode.back() - '0'));
        return 1;
    }
    if (mode == "unexpected-exit") return 1;
    if (mode == "truncated") { Header({2, 0, 0, 0, 4}); return 0; }
    if (mode == "partial-header") { const char data = 2; return write(1, &data, 1) == 1 ? 0 : 1; }
    if (mode == "stall" || mode == "partial-timeout") { Header({2, 0, 0, 0, 4}); for (;;) pause(); }
    if (mode == "silent-timeout") { for (;;) pause(); }
    if (mode == "drip-timeout") {
        Header({1, 16, 16, 0, 256});
        const char byte = 0;
        for (;;) { Check(write(1, &byte, 1) == 1, "Drip write failed"); usleep(100000); }
    }
    if (mode == "streaming" || mode == "preview-timeout") {
        td::ConfineScanner();
        const std::array<uint8_t, 4> black{};
        for (;;) {
            td::SendPreview(black, 2, 2, 0);
            if (mode == "preview-timeout") { for (;;) pause(); }
            usleep(100000);
        }
    }
    if (mode == "bytewise") {
        const auto message = Message();
        const std::array<uint32_t, 5> header{3, 0, 0, 0, static_cast<uint32_t>(message.cbor.size())};
        std::vector<uint8_t> bytes(reinterpret_cast<const uint8_t*>(header.data()), reinterpret_cast<const uint8_t*>(header.data()) + sizeof(header));
        bytes.insert(bytes.end(), message.cbor.begin(), message.cbor.end());
        for (const auto byte : bytes) { Check(write(1, &byte, 1) == 1, "Byte write failed"); usleep(1000); }
        return 0;
    }

    // Generate public image data before confinement, then exercise the actual
    // ZBar + UR decode and pipe-write path under the production policy.
    td::URSender sender(Message());
    td::QRImage qr(sender.Next());
    const unsigned width = (qr.width + 8) * 4;
    std::vector<uint8_t> image(width * width, 255);
    for (int y = 0; y < qr.width; ++y) for (int x = 0; x < qr.width; ++x) {
        for (int dy = 0; dy < 4; ++dy) for (int dx = 0; dx < 4; ++dx) {
            image[((y + 4) * 4 + dy) * width + (x + 4) * 4 + dx] = qr.modules[y * qr.width + x] ? 0 : 255;
        }
    }
    td::QRScanner scanner;
    td::URReceiver receiver;
    libv4l_dev_ops operations{};
    operations.ioctl = CameraControl;
    v4lconvert_data* converter = nullptr;
    unsigned char* jpeg = nullptr;
    unsigned long jpeg_size = 0;
    v4l2_format source{}, destination{};
    std::vector<uint8_t> rgb;
    if (mode == "mjpeg") {
        converter = v4lconvert_create_with_dev_ops(-1, nullptr, &operations);
        Check(converter, "Camera conversion setup failed");
        jpeg_compress_struct compressor{};
        jpeg_error_mgr error;
        compressor.err = jpeg_std_error(&error);
        jpeg_create_compress(&compressor);
        jpeg_mem_dest(&compressor, &jpeg, &jpeg_size);
        compressor.image_width = compressor.image_height = width;
        compressor.input_components = 3;
        compressor.in_color_space = JCS_RGB;
        rgb.resize(width * width * 3);
        for (size_t i = 0; i < image.size(); ++i) rgb[3 * i] = rgb[3 * i + 1] = rgb[3 * i + 2] = image[i];
        jpeg_set_defaults(&compressor);
        jpeg_set_quality(&compressor, 90, TRUE);
        jpeg_start_compress(&compressor, TRUE);
        while (compressor.next_scanline < width) {
            JSAMPROW row = rgb.data() + compressor.next_scanline * width * 3;
            jpeg_write_scanlines(&compressor, &row, 1);
        }
        jpeg_finish_compress(&compressor);
        jpeg_destroy_compress(&compressor);
        source.type = destination.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        source.fmt.pix.width = destination.fmt.pix.width = width;
        source.fmt.pix.height = destination.fmt.pix.height = width;
        source.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        destination.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
        destination.fmt.pix.bytesperline = width * 3;
        destination.fmt.pix.sizeimage = width * width * 3;
        rgb.resize(destination.fmt.pix.sizeimage);
    }
    const pid_t parent = getppid();
    const int camera = open("/dev/null", O_RDWR | O_CLOEXEC);
    Check(camera >= 3, "Mock camera open failed");
    rlimit cpu_before{}, cpu_after{};
    Check(getrlimit(RLIMIT_CPU, &cpu_before) == 0, "Cannot read inherited CPU limit");
    td::ConfineScanner();
    Check(getrlimit(RLIMIT_CPU, &cpu_after) == 0 && cpu_before.rlim_cur == cpu_after.rlim_cur
        && cpu_before.rlim_max == cpu_after.rlim_max, "Confinement imposed a scanner lifetime CPU limit");
    long result;
    int expected_errno = EPERM;
    if (mode == "open") { result = syscall(SYS_openat, AT_FDCWD, "/proc/self/maps", O_RDONLY, 0); expected_errno = EACCES; }
    else if (mode == "write-file") { result = syscall(SYS_openat, AT_FDCWD, "/tmp/td-forbidden-write", O_CREAT | O_WRONLY, 0600); expected_errno = EACCES; }
    else if (mode == "exec") { char* argv[]{const_cast<char*>("/bin/false"), nullptr}; result = syscall(SYS_execve, argv[0], argv, nullptr); expected_errno = EACCES; }
    else if (mode == "ptrace") result = syscall(SYS_ptrace, PTRACE_ATTACH, parent, nullptr, nullptr);
    else if (mode == "read-parent") {
        char value;
        iovec local{&value, 1}, remote{reinterpret_cast<void*>(0x1000), 1};
        result = syscall(SYS_process_vm_readv, parent, &local, 1, &remote, 1, 0);
    } else if (mode == "signal") result = syscall(SYS_kill, parent, SIGUSR1);
    else if (mode == "fork") { result = syscall(SYS_clone, SIGCHLD, nullptr, nullptr, nullptr, 0); expected_errno = EAGAIN; }
    else if (mode == "raise-limit") { rlimit unlimited{RLIM_INFINITY, RLIM_INFINITY}; result = syscall(SYS_prlimit64, 0, RLIMIT_AS, &unlimited, nullptr); }
    else {
        Check(mode == "decode" || mode == "limit" || mode == "mjpeg", "Unknown probe mode");
        char value;
        Check(read(camera, &value, 1) == 0, "Previously opened device became unreadable");
        Check(ioctl(camera, FIONREAD, &result) == -1 && errno == ENOTTY, "Previously opened device ioctl was blocked");
        if (mode == "limit") Check(mmap(nullptr, 512 * 1024 * 1024, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED, "Memory budget not enforced");
        if (mode == "mjpeg") {
            Check(v4lconvert_convert(converter, &source, &destination, jpeg, jpeg_size, rgb.data(), rgb.size()) == int(rgb.size()),
                v4lconvert_get_error_message(converter));
            image = td::Grayscale(rgb, width, width, width * 3, V4L2_PIX_FMT_RGB24);
            v4lconvert_destroy(converter);
            free(jpeg);
        }
        const auto results = scanner.Scan(image, width, width);
        Check(results.size() == 1, "Confined image decode failed");
        receiver.Receive(results[0]);
        Check(receiver.Result().has_value(), "Confined UR decode failed");
        td::SendPreview(image, width, width, 100);
        td::SendScanResult(*receiver.Result());
        return 0;
    }
    Check(result == -1 && errno == expected_errno, "Forbidden operation was not denied");
    return 0;
}

void Parent(const std::string& executable)
{
    td::LockProcess();
    Check(setuid(0) == -1 && errno == EPERM, "Process can regain root");
    const auto signer = std::filesystem::path(executable).parent_path() / "thunderden-signer";
    const int code = open(signer.c_str(), O_WRONLY);
    Check(code == -1 && (errno == EACCES || errno == EROFS), "Application files are writable");
    signal(SIGUSR1, SIG_IGN); // A broken signal rule must fail, not terminate the runner.
    Check(setenv("TD_SECRET_CANARY", "PUBLIC-PARENT-ONLY-FIXTURE", 1) == 0, "Canary setup failed");
    const int original = open("/dev/null", O_RDONLY);
    Check(original >= 0 && dup2(original, 100) == 100, "Descriptor canary setup failed");
    close(original);
    char pattern[] = "/tmp/thunderden-isolation-XXXXXX";
    const char* directory = mkdtemp(pattern);
    Check(directory, "Temporary directory failed");
    const std::filesystem::path root(directory);
    try {
        const auto path = [&](const std::string& mode) {
            const auto link = root / mode;
            std::filesystem::create_symlink(executable, link);
            return link.string();
        };
        for (const auto mode : {"decode", "limit", "bytewise", "mjpeg"}) {
            td::ScanProcess process(path(mode));
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            bool complete = false;
            while (!complete && std::chrono::steady_clock::now() < deadline) {
                if (auto update = process.Poll()) {
                    if (update->message) {
                        Check(update->message->type == "bytes" && update->message->cbor == Message().cbor, "Result changed across process boundary");
                        complete = true;
                    } else {
                        Check(update->progress == 100 && update->gray.size() == update->width * update->height, "Invalid preview accepted");
                    }
                }
            }
            Check(complete, "Worker failed to return a result");
        }
        for (const auto mode : {"oversize", "geometry", "progress", "command", "truncated", "partial-header",
                "bad-failure-code", "bad-failure-zero", "bad-failure-geometry", "bad-failure-payload"}) {
            bool rejected = false;
            td::ScanProcess process(path(mode));
            for (int i = 0; i < 100 && !rejected; ++i) {
                try { Check(!process.Poll(), "Malformed worker output escaped validation"); }
                catch (const std::invalid_argument&) { rejected = true; }
            }
            Check(rejected, "Malformed worker output not rejected");
        }
        for (const auto& [mode, expected] : std::array<std::pair<const char*, const char*>, 8>{{
                {"failure-1", "Scanner initialization failed"},
                {"failure-2", "Cannot open or configure a webcam"},
                {"failure-3", "Camera access denied; try a new session"},
                {"failure-4", "Scanner isolation setup failed"},
                {"failure-5", "Camera frame capture failed; retry the scan"},
                {"failure-6", "QR image decoding failed"},
                {"failure-7", "Invalid or unsupported UR v2 request"},
                {"unexpected-exit", "Camera/QR worker exited unexpectedly; retry the scan"}}}) {
            td::ScanProcess process(path(mode));
            bool reported = false;
            for (int i = 0; i < 100 && !reported; ++i) {
                try { Check(!process.Poll(), "Failure produced a successful result"); }
                catch (const std::invalid_argument& error) {
                    Check(std::strcmp(error.what(), expected) == 0, "Wrong scanner failure message");
                    reported = true;
                }
            }
            Check(reported, "Scanner failure was not reported");
        }
        {
            const auto started = std::chrono::steady_clock::now();
            {
                td::ScanProcess process(path("stall"));
                for (int i = 0; i < 3; ++i) Check(!process.Poll(), "Partial result was accepted");
            }
            Check(std::chrono::steady_clock::now() - started < std::chrono::seconds(1), "Worker prevented cancellation");
        }
        {
            const auto started = std::chrono::steady_clock::now();
            std::vector<std::unique_ptr<td::ScanProcess>> stalled;
            for (const auto mode : {"silent-timeout", "partial-timeout", "drip-timeout", "preview-timeout"})
                stalled.push_back(std::make_unique<td::ScanProcess>(path(mode)));
            td::ScanProcess streaming(path("streaming"));
            unsigned previews = 0;
            while (std::chrono::steady_clock::now() - started < td::SCAN_FRAME_TIMEOUT + std::chrono::seconds(1)) {
                for (size_t i = 0; i < stalled.size(); ++i) {
                    if (!stalled[i]) continue;
                    try { stalled[i]->Poll(); }
                    catch (const std::invalid_argument& error) {
                        Check(std::chrono::steady_clock::now() - started >= td::SCAN_FRAME_TIMEOUT,
                            "Worker timed out too early");
                        Check(std::string_view(error.what()).starts_with(i == 3
                            ? "Camera stopped producing frames" : "Camera startup timed out"), "Wrong timeout reason");
                        stalled[i].reset();
                    }
                }
                if (auto update = streaming.Poll()) {
                    Check(!update->message && update->gray == std::vector<uint8_t>(4, 0), "Invalid streaming preview");
                    ++previews;
                }
            }
            for (const auto& process : stalled) Check(!process, "Partial traffic prevented the frame timeout");
            Check(previews > 1, "Healthy frames did not keep the scanner alive");
        }
        Check(waitpid(-1, nullptr, WNOHANG) == -1 && errno == ECHILD, "Worker was not reaped");
        for (const auto mode : {"open", "write-file", "exec", "ptrace", "read-parent", "signal", "fork", "raise-limit"}) {
            const pid_t pid = fork();
            Check(pid >= 0, "Probe fork failed");
            if (!pid) {
                unsetenv("TD_SECRET_CANARY");
                close(100);
                setsid();
                _exit(Worker(mode));
            }
            int status;
            Check(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "Forbidden operation escaped confinement");
        }
        std::filesystem::remove_all(root);
        close(100);
    } catch (...) { std::filesystem::remove_all(root); close(100); throw; }
    std::puts("PASS: fresh worker environment/FDs, confined decoding, resource bounds, hostile IPC, cancellation/reaping and filesystem/process access denial");
}
}

int main(int argc, char** argv)
{
    try {
        const auto name = std::filesystem::path(argv[0]).filename().string();
        if (name != "isolation-tests") return Worker(name);
        if (argc == 2 && std::string_view(argv[1]) == "--guest") td::PrepareSigner();
        else Check(argc == 1, "Unexpected test arguments");
        if (syscall(SYS_landlock_create_ruleset, nullptr, 0, LANDLOCK_CREATE_RULESET_VERSION) < 6) {
            bool rejected = false;
            try { td::ConfineScanner(); } catch (const std::invalid_argument&) { rejected = true; }
            Check(rejected, "Missing Landlock did not fail closed");
            std::puts("SKIP: confinement tests require a host kernel with Landlock ABI 6; scanner refuses unconfined operation");
            return 77;
        }
        Parent(std::filesystem::canonical(argv[0]));
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
