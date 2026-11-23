#!/usr/bin/env python3
"""Isolate all original MbedTLS identifier symbols in the inspection profile."""
import argparse
import pathlib
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', type=pathlib.Path, required=True)
p.add_argument('--out', type=pathlib.Path, required=True)
o = p.parse_args()
text = '\n'.join(f.read_text() for f in sorted(o.source.rglob('*')) if f.suffix in ('.h', '.c'))
names = set(re.findall(r'\b(?:mbedtls|psa)_[A-Za-z0-9_]+\b', text))
macros = set(re.findall(r'^\s*#\s*define\s+((?:mbedtls|psa)_[A-Za-z0-9_]+)\b', text, re.M))
o.out.write_text('/* Private parser namespace; original source bytes remain unchanged. */\n' +
    '\n'.join('#define ' + n + ' chos_certificate_' + n for n in sorted(names - macros)) + '\n')
