#include "isolation.h"
#include "keys.h"

#include <grp.h>
#include <linux/capability.h>
#include <linux/landlock.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>

namespace td {
void LockProcess()
{
    Require(getuid() != 0 && geteuid() == getuid() && getgid() != 0 && getegid() == getgid(), "Signer requires an unprivileged identity");
    __user_cap_header_struct header{_LINUX_CAPABILITY_VERSION_3, 0};
    __user_cap_data_struct capabilities[2]{};
    const rlimit no_core{0, 0};
    Require(syscall(SYS_capset, &header, capabilities) == 0 && setrlimit(RLIMIT_CORE, &no_core) == 0
        && prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0 && prctl(PR_SET_DUMPABLE, 0) == 0,
        "Cannot restrict process privileges");
}

void PrepareSigner()
{
    if (geteuid() == 0) {
        // /dev is root-owned devtmpfs. Only fixed camera/framebuffer nodes are
        // shared with the signer group; the keyboard remains on its open tty.
        const auto grant = [](const std::string& path) {
            struct stat info{};
            if (lstat(path.c_str(), &info) != 0) {
                Require(errno == ENOENT, "Cannot inspect input/display device");
                return;
            }
            Require(S_ISCHR(info.st_mode) && chown(path.c_str(), 0, 1000) == 0
                && chmod(path.c_str(), 0660) == 0, "Cannot prepare input/display device");
        };
        grant("/dev/fb0");
        for (unsigned i = 0; i < 32; ++i) grant("/dev/video" + std::to_string(i));
        Require(setgroups(0, nullptr) == 0 && setresgid(1000, 1000, 1000) == 0
            && setresuid(1000, 1000, 1000) == 0, "Cannot drop root identity");
    }
    LockProcess();
}

void ConfineScanner()
{
    Require(syscall(SYS_landlock_create_ruleset, nullptr, 0, LANDLOCK_CREATE_RULESET_VERSION) >= 6,
        "Scanner requires Landlock ABI 6 or later");
    const rlimit memory{256 * 1024 * 1024, 256 * 1024 * 1024}, cpu{60, 60}, files{64, 64}, children{0, 0};
    Require(setrlimit(RLIMIT_AS, &memory) == 0 && setrlimit(RLIMIT_CPU, &cpu) == 0
        && setrlimit(RLIMIT_NOFILE, &files) == 0 && setrlimit(RLIMIT_NPROC, &children) == 0,
        "Cannot bound scanner resources");
    // Deny new file-content access and filesystem execution. Open camera and
    // pipe descriptors keep their rights. Ptrace is confined to this domain;
    // signals cannot target the parent. No networking exists in the image.
    const landlock_ruleset_attr policy{
        .handled_access_fs = LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_WRITE_FILE
            | LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR | LANDLOCK_ACCESS_FS_REMOVE_DIR
            | LANDLOCK_ACCESS_FS_REMOVE_FILE | LANDLOCK_ACCESS_FS_MAKE_CHAR | LANDLOCK_ACCESS_FS_MAKE_DIR
            | LANDLOCK_ACCESS_FS_MAKE_REG | LANDLOCK_ACCESS_FS_MAKE_SOCK | LANDLOCK_ACCESS_FS_MAKE_FIFO
            | LANDLOCK_ACCESS_FS_MAKE_BLOCK | LANDLOCK_ACCESS_FS_MAKE_SYM | LANDLOCK_ACCESS_FS_REFER
            | LANDLOCK_ACCESS_FS_TRUNCATE | LANDLOCK_ACCESS_FS_IOCTL_DEV,
        .handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP | LANDLOCK_ACCESS_NET_CONNECT_TCP,
        .scoped = LANDLOCK_SCOPE_SIGNAL | LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET,
    };
    const int fd = syscall(SYS_landlock_create_ruleset, &policy, sizeof(policy), 0);
    Require(fd >= 0, "Cannot create scanner restriction domain");
    const int status = syscall(SYS_landlock_restrict_self, fd, 0);
    close(fd);
    Require(status == 0, "Cannot confine scanner");
}
}
