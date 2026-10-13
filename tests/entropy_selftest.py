"""Run the pinned upstream non-IID self-test corpus and original comparator."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
source = Path(sys.argv[2] if len(sys.argv) > 2 else '/opt/nist-entropy')
environment = {**os.environ, 'OMP_NUM_THREADS': '1', 'OMP_THREAD_LIMIT': '1', 'LC_ALL': 'C'}
with tempfile.TemporaryDirectory(prefix='nist-public-selftest-') as directory:
    samples = sorted((source / 'bin').glob('*.bin'))
    assert len(samples) == 11, 'Pinned upstream self-test corpus is missing'
    for sample in samples:
        output = Path(directory) / (sample.stem + '.res')
        with output.open('wb') as stream:
            subprocess.run([str(binary), '-vv', str(sample)], stdout=stream, check=True, timeout=300, env=environment)
        comparison = subprocess.run(['perl', str(source / 'cpp/selftest/compareresults.pl'), str(output),
                                     str(source / 'cpp/selftest/refdata' / output.name)], capture_output=True, text=True)
        assert 'No corresponding value' not in comparison.stdout and not comparison.stderr, comparison
        delta = float(re.search(r'Maximum delta: ([^\s]+)', comparison.stdout)[1])
        assert delta < 1e-6, comparison.stdout  # Published README tolerance; comparator itself uses 1e-10.
        print(f'PASS: {sample.name}; max delta {delta}; strict comparator exit {comparison.returncode}', flush=True)
print('PASS: all 11 upstream non-IID self-test datasets')
