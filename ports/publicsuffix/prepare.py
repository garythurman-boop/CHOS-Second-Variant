"""Verify the pinned public suffix inputs and generate the builtin table."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('out', type=Path)
out = parser.parse_args().out.resolve()
out.mkdir(parents=True, exist_ok=True)
manifest = json.loads((ROOT / 'docs/dependencies/native-publicsuffix.json').read_text())
for name, digest in manifest['files'].items():
    if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != digest:
        raise ValueError(f'Source verification failed for {name}')
source = ROOT / 'third_party/libpsl/src'
rules = out / 'public_suffix_list.dat'
shutil.copyfile(ROOT / 'third_party/publicsuffix' / rules.name, rules)
stamp = manifest['rules']['commit_timestamp']
os.utime(rules, (stamp, stamp))
subprocess.run([sys.executable, str(source / 'psl-make-dafsa'), '--output-format=cxx+',
                rules.name, 'suffixes_dafsa.h'], cwd=out, check=True)
shutil.copyfile(source / 'psl.c', out / 'psl.c')
subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i',
                str(ROOT / 'ports/publicsuffix/builtin-only.patch')], cwd=out, check=True)
