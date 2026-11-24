#!/usr/bin/env python3
"""Isolate maintained key symbols from the independent TLS and parser profiles."""
import argparse
import pathlib
import re

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=pathlib.Path,action='append',required=True)
p.add_argument('--out',type=pathlib.Path,required=True)
o=p.parse_args()
sources='\n'.join(f.read_text() for base in o.source for f in sorted(base.rglob('*')) if f.suffix in ('.h','.c'))
names=set(re.findall(r'\b(?:mbedtls|psa)_[A-Za-z0-9_]+\b',sources))
macros=set(re.findall(r'^\s*#\s*define\s+((?:mbedtls|psa)_[A-Za-z0-9_]+)\b',sources,re.M))
o.out.write_text('/* Original source bytes remain unchanged; private key namespace. */\n'+
    '\n'.join('#define '+n+' chos_webcore_key_'+n for n in sorted(names-macros))+'\n')
