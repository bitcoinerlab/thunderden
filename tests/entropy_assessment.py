"""Actual isolated NIST entry point with public fixtures; no wallet-generation data."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import signal
import time

SEALS = fcntl.F_SEAL_WRITE | fcntl.F_SEAL_GROW | fcntl.F_SEAL_SHRINK | fcntl.F_SEAL_SEAL


def assess(binary, data, *, sealed=True, threads=1, official=False, timeout=130):
    source = os.memfd_create('public-entropy-fixture', os.MFD_CLOEXEC | os.MFD_ALLOW_SEALING)
    report = os.memfd_create('public-assessment', os.MFD_CLOEXEC | os.MFD_ALLOW_SEALING)
    safe_in = safe_out = -1
    pid = None
    try:
        assert os.write(source, data) == len(data)
        if sealed:
            fcntl.fcntl(source, fcntl.F_ADD_SEALS, SEALS)
        safe_in = fcntl.fcntl(source, fcntl.F_DUPFD_CLOEXEC, 5)
        safe_out = fcntl.fcntl(report, fcntl.F_DUPFD_CLOEXEC, 5)
        actions = [(os.POSIX_SPAWN_DUP2, safe_in, 3), (os.POSIX_SPAWN_DUP2, safe_out, 4)]
        actions += [(os.POSIX_SPAWN_OPEN, fd, '/dev/null', os.O_RDONLY if fd == 0 else os.O_WRONLY, 0) for fd in range(3)]
        actions.append((os.POSIX_SPAWN_CLOSEFROM, 5))
        command = [str(binary)] + (['-i', '-a', '-q', '-o', '/proc/self/fd/4', '/proc/self/fd/3', '8'] if official else [])
        env = {'LANG': 'C', 'LC_ALL': 'C', 'TZ': 'UTC0', 'OMP_NUM_THREADS': str(threads), 'OMP_THREAD_LIMIT': str(threads), 'OMP_DYNAMIC': 'FALSE'}
        start = time.monotonic()
        pid = os.posix_spawn(str(binary), command, env, file_actions=actions, setsid=True)
        timed_out = False
        while True:
            found, status, usage = os.wait4(pid, os.WNOHANG)
            if found:
                break
            if time.monotonic() - start > timeout:
                os.kill(pid, signal.SIGKILL)
                _, status, usage = os.wait4(pid, 0)
                timed_out = True
                break
            time.sleep(0.02)
        pid = None
        elapsed = time.monotonic() - start
        size = os.fstat(report).st_size
        assert size <= 65536, 'Assessment output exceeded its budget'
        result = json.loads(os.pread(report, size, 0)) if size else {}
        score = next((case['hAssessed'] for case in result.get('testCases') or [] if case.get('testCaseDesc') == 'Overall'), None)
        summary = {'samples': len(data), 'threads': threads, 'seconds': round(elapsed, 3), 'peak_rss_kib': usage.ru_maxrss,
                   'cpu_seconds': round(usage.ru_utime + usage.ru_stime, 3), 'exit_code': os.waitstatus_to_exitcode(status),
                   'timeout': timed_out, 'bits_per_sample': score}
        return summary, result
    finally:
        if pid is not None:
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
        for fd in (source, report, safe_in, safe_out):
            if fd >= 0:
                os.close(fd)


def tests(binary):
    for count, sealed in ((1048575, True), (2097153, True), (1048576, False)):
        summary, report = assess(binary, bytes(count), sealed=sealed)
        assert summary['exit_code'] != 0 and not report, 'Invalid sample descriptor accepted'
    summary, report = assess(binary, bytes(1048576))
    assert summary['exit_code'] == 255 and report['errorLevel'] == -1
    assert report['errorMessage'] == 'Symbol alphabet consists of 1 symbol. No entropy awarded...'
    assert summary['bits_per_sample'] is None, 'Constant block pretended to complete the battery'
    data = hashlib.shake_256(b'PUBLIC reproducible fixture, NOT secret entropy').digest(1048576)
    summary, report = assess(binary, data)
    assert summary['exit_code'] == 0 and not summary['timeout'], summary
    assert len(report['testCases']) == 11 and report['IID'] is False and report['errorLevel'] == 0
    assert report['sha256'] == hashlib.sha256(data).hexdigest(), 'NIST did not receive the immutable sample bytes'
    assert 0 <= summary['bits_per_sample'] <= 8
    print('PASS: sealed RAM-only input, invalid descriptors, constant-data status, complete official battery and exact sample bytes')
    print(json.dumps(summary, sort_keys=True))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    tests(parser.parse_args().binary)
