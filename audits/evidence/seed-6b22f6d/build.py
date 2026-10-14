"""Build public audit probes against the reviewed native Docker image."""
from pathlib import Path
import shlex
import subprocess

sources = Path(__file__).resolve().parent
text = Path('/build/build.ninja').read_text()
block = text.split('build entropy-tests: ', 1)[1].split('\n\n', 1)[0]
libraries = shlex.split(next(line.split(' = ', 1)[1] for line in block.splitlines() if line.startswith('  LINK_LIBRARIES')))
libraries = [str(Path('/build') / item) if not item.startswith(('/', '-')) else item for item in libraries]
common = ['c++', '-std=c++20', '-Os', '-fstack-reuse=none', '-fstack-protector-all',
          '-ffunction-sections', '-fdata-sections', '-I/work/src', '-I/opt/bitcoin/src',
          '-I/opt/bitcoin/src/secp256k1/include', '-I/build/core/src', '-I/build', '-Wl,--gc-sections']
for name in ('json_boundary', 'memfd_budget', 'residue_probe'):
    flags = ['-Wl,--wrap=_ZN7CSHA2568FinalizeEPh', '-Wl,--wrap=_Z14memory_cleansePvm'] if name == 'residue_probe' else []
    subprocess.run(common + [str(sources / (name + '.cpp')), '-o', '/audit/' + name] + flags + libraries, check=True)
subprocess.run(['c++', '-std=c++20', '-Os', '-rdynamic', str(sources / 'native_read.cpp'),
                '-L/usr/local/lib', '-lv4l2', '-ldl', '-o', '/audit/native_read'], check=True)
