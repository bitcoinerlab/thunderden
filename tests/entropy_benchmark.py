"""Benchmark public synthetic samples, never material intended for a wallet."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import random
import time
from entropy_assessment import assess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', type=Path, default=Path('/build/ea_non_iid'))
parser.add_argument('--samples', nargs='+', type=int, default=[1048576, 2097152])
parser.add_argument('--threads', nargs='+', type=int, default=[1, 2])
parser.add_argument('--frames', nargs='+', type=int, default=[32, 64, 128])
parser.add_argument('--patterns', nargs='+', choices=['constant', 'periodic', 'loop', 'texture', 'markov', 'public-prng'],
                    default=['constant', 'periodic', 'loop', 'texture', 'markov', 'public-prng'])
parser.add_argument('--timeout', type=int, default=120)
args = parser.parse_args()
assert all(n in (1048576, 2097152) for n in args.samples)
assert all(n in (1, 2) for n in args.threads) and all(n in (32, 64, 128) for n in args.frames)
cpu = next((line.split(':', 1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines() if line.startswith('model name')), platform.machine())
memory = next(line for line in Path('/proc/meminfo').read_text().splitlines() if line.startswith('MemTotal:'))
print(json.dumps({'cpu': cpu, 'memory': memory, 'machine': platform.machine(), 'kernel': platform.release(),
                  'data': 'public synthetic fixtures; not camera entropy', 'timings': 'NIST process; Python rearrangement excluded'}), flush=True)
for n in args.samples:
    for pattern in args.patterns:
        noise = hashlib.shake_256(b'PUBLIC entropy benchmark fixture').digest(n)
        if pattern == 'constant':
            raw = bytes(n)
        elif pattern == 'periodic':
            raw = bytes(range(256)) * (n // 256)
        elif pattern == 'loop':
            raw = noise[:4096] * (n // 4096)
        elif pattern == 'markov':
            rng = random.Random(0)
            data, value = bytearray(n), 0
            for i in range(n):
                if rng.random() < 0.02:
                    value = rng.randrange(256)
                data[i] = value
            raw = bytes(data)
        else:
            raw = noise
        for frames in args.frames:
            frame_samples = n // frames
            source = noise[:frame_samples] * frames if pattern == 'texture' else raw
            for order in ('frame-major', 'temporal'):
                # Reference permutation only; production serialization is measured separately in C++.
                data = source if order == 'frame-major' else b''.join(source[i::frame_samples] for i in range(frame_samples))
                start = time.monotonic()
                hashlib.sha256(data).digest()
                hash_seconds = time.monotonic() - start
                for threads in args.threads:
                    result, _ = assess(args.binary, data, official=True, threads=threads, timeout=args.timeout)
                    result.update(pattern=pattern, order=order, frames=frames, positions=frame_samples,
                                  hash_seconds=round(hash_seconds, 6))
                    print(json.dumps(result, sort_keys=True), flush=True)
