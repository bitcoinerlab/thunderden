#include "camera_entropy.h"
#include "entropy_process.h"
#include "isolation.h"
#include "nist_progress.h"
#include <crypto/common.h>
#include <linux/videodev2.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <chrono>
#include <thread>
#include <algorithm>

namespace {
void Send(unsigned type, std::span<const unsigned char> data)
{
    unsigned char header[8]; WriteBE32(header, type); WriteBE32(header + 4, data.size());
    for (auto bytes : {std::span<const unsigned char>(header), data}) while (!bytes.empty()) {
        const auto n = write(1, bytes.data(), bytes.size());
        td::Require(n > 0, "Test pipe closed"); bytes = bytes.subspan(n);
    }
}
}
int main(int argc, char** argv)
{
    try {
        const std::string mode(argv[0]);
        td::LockProcess();
        if (argc == 2) {
            td::EntropyWorker child(argv[1]);
            unsigned char ok{};
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!ok && std::chrono::steady_clock::now() < end) child.Read({&ok, 1});
            td::Require(ok == 1, "Confinement probe did not complete");
            std::puts("PASS: assessment file/terminal/process isolation and resource limits");
            return 0;
        }
        if (mode.find("confined") != mode.npos) {
            rlimit cpu_before{}, cpu_after{};
            td::Require(getrlimit(RLIMIT_CPU, &cpu_before) == 0, "Cannot read inherited CPU limit");
            td::ConfineAssessment();
            td::Require(getrlimit(RLIMIT_CPU, &cpu_after) == 0 && cpu_before.rlim_cur == cpu_after.rlim_cur
                && cpu_before.rlim_max == cpu_after.rlim_max, "Assessment imposed a CPU timeout");
            for (const auto* path : {"/etc/passwd", "/dev/tty", "/proc/self/exe"}) td::Require(open(path, O_RDONLY) < 0, "Worker opened filesystem/terminal");
            const auto parent = "/proc/" + std::to_string(getppid()) + "/mem";
            td::Require(open(parent.c_str(), O_RDONLY) < 0 && getenv("ENTROPY_TEST_SECRET") == nullptr, "Worker inherited parent access");
            for (int fd = 3; fd < 64; ++fd) td::Require(fcntl(fd, F_GETFD) < 0, "Worker inherited an unintended descriptor");
            td::Require(fork() == -1 && errno == EAGAIN, "Worker created another process");
            bool thread_denied = false;
            try { std::thread extra([] {}); extra.join(); } catch (const std::system_error&) { thread_denied = true; }
            td::Require(thread_denied, "Worker created another thread");
            td::Require(mmap(nullptr, 513UL * 1024 * 1024, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED, "Worker escaped memory budget");
            for (const auto& [kind, bound] : {std::pair{RLIMIT_AS, 512UL * 1024 * 1024}, {RLIMIT_NPROC, 0UL}, {RLIMIT_FSIZE, 65536UL}}) {
                rlimit value{}; td::Require(getrlimit(kind, &value) == 0 && value.rlim_max == bound, "Worker resource budget changed");
            }
            const unsigned char ok = 1; td::Require(write(1, &ok, 1) == 1, "No confinement reply");
            return 0;
        }
        if (mode.find("thunderden-assess") != mode.npos) {
            if (mode.find("bad-progress") != mode.npos) {
                const unsigned char bad[20]{255};
                td::Require(write(5, bad, sizeof(bad)) == sizeof(bad), "Bad progress fixture write");
            } else {
                td::NistInput(256, td::ENTROPY_TEST_SAMPLES, 1000000);
                for (unsigned test = 1; test <= 8; ++test) for (unsigned branch : {2, 1}) {
                    if (branch == 1 && test >= 2 && test <= 4) continue;
                    td::NistStart(test, branch, branch == 2 ? 1000000 : td::ENTROPY_TEST_SAMPLES);
                    td::NistResult(test, test == 1 ? 0 : 0.5); // A low partial result is not a capture fault.
                }
                td::NistStart(9, 1, td::ENTROPY_TEST_SAMPLES);
                std::this_thread::sleep_for(std::chrono::milliseconds(1100));
                td::NistUpdate(655360);
            }
            while (true) pause();
        }
        td::ConfineScanner();
        const auto profile = td::CameraProfile(128, 128, V4L2_PIX_FMT_GREY, 64);
        if (mode.find("bad-header") != mode.npos) { unsigned char bad[8]{}; WriteBE32(bad, 3); WriteBE32(bad + 4, UINT32_MAX); td::Require(write(1, bad, 8) == 8, "Test write"); return 0; }
        Send(1, profile);
        if (mode.find("stalled-capture") != mode.npos) { while (true) pause(); }
        unsigned char frames[4]; WriteBE32(frames, 136); Send(2, frames);
        std::vector<unsigned char> bytes(td::ENTROPY_CHUNK);
        for (unsigned chunk = 0; chunk < 32; ++chunk) {
            for (size_t n = 0; n < bytes.size(); n += 32) {
                unsigned char index[8]; WriteBE64(index, chunk * bytes.size() + n);
                CSHA256().Write(index, sizeof(index)).Finalize(bytes.data() + n);
            }
            if (mode.find("constant") != mode.npos) std::fill(bytes.begin(), bytes.end(), 0);
            Send(3, bytes);
        }
        if (mode.find("partial-capture") != mode.npos) { while (true) pause(); }
        unsigned char flags[4]{};
        if (mode.find("frozen") != mode.npos) WriteBE32(flags, 1);
        Send(4, flags);
        if (mode.find("stalled-exit") != mode.npos) { while (true) pause(); }
        return 0;
    } catch (...) { return 1; }
}
