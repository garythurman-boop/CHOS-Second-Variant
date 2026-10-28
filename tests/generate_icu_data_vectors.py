#!/usr/bin/env python3
"""Losslessly encode all official Unicode18 normalization and UTS46 rows.

No expected result is computed by ICU, Python Unicode APIs or the native code.
Only field inheritance, documented escapes and UTF16 encoding are applied.
"""
import argparse, hashlib, json, pathlib, re, struct, subprocess
root = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=pathlib.Path, required=True)
o = p.parse_args()
subprocess.run(['python3', str(root/'tools/verify_icu_normalization_data.py')], check=True)
base = root/'third_party/icu-normalization-data'
def utf16(s):
    return s.encode('utf-16-le', errors='surrogatepass')
def unescape(s):
    if s == '""': return ''
    return re.sub(r'\\u([0-9A-Fa-f]{4})|\\x\{([0-9A-Fa-f]+)\}',
                  lambda m: chr(int(m[1] or m[2], 16)), s)
norm = bytearray(); count = 0; part = None; excluded = bytearray(0x110000//8)
max_norm = 0
for line_no, line in enumerate((base/'source/data/unidata/NormalizationTest.txt').read_text().splitlines(), 1):
    line = line.split('#')[0].strip(' \t\r')
    if not line: continue
    if line.startswith('@Part'):
        part = int(line[5:]); continue
    columns = line.split(';')
    assert len(columns) == 6 and not columns[-1].strip()
    scalars = [[int(x, 16) for x in c.split()] for c in columns[:5]]
    if part == 1:
        assert len(scalars[0]) == 1
        cp = scalars[0][0]; excluded[cp//8] |= 1 << (cp%8)
    fields = [utf16(''.join(map(chr, c))) for c in scalars]
    lengths = [len(c)//2 for c in fields]
    assert max(lengths) <= 128
    max_norm = max(max_norm, *lengths)
    norm += struct.pack('<I5H', line_no, *lengths) + b''.join(fields); count += 1
norm = b'CHOSN18\0'+struct.pack('<I', count)+excluded+norm
idna = bytearray(); idna_count = 0; max_idna = 0; utf8_rows = 0; ill_formed_rows = 0
for line_no, line in enumerate((base/'source/test/testdata/IdnaTestV2.txt').read_text().splitlines(), 1):
    line = line.split('#')[0].strip(' \t\r')
    if not line: continue
    c = [s.strip(' \t') for s in line.split(';')]
    assert len(c) == 7, (line_no, len(c))
    source = unescape(c[0]); unicode = unescape(c[1]) if c[1] else source
    ascii_n = unescape(c[3]) if c[3] else unicode
    ascii_t = unescape(c[5]) if c[5] else ascii_n
    statuses = [c[2] or '[]', c[4] or c[2] or '[]', c[6] or c[4] or c[2] or '[]']
    assert all(re.fullmatch(r'\[(?:[A-Z][0-9]+(?:_[0-9]+)?(?:, )?)*\]', s) for s in statuses), (line_no, statuses)
    # Match the original ICU79 UTS46 conformance harness: error/no-error,
    # exact strings on success, and its explicitly documented ICU-22882
    # empty trailing-root-label exception for an otherwise valid ToASCII.
    flags = [0 if s == '[]' else 2 if s == '[A4_2]' else 1 for s in statuses]
    fields = [utf16(s) for s in (source, unicode, ascii_n, ascii_t)]
    try:
        fields[0].decode('utf-16-le'); utf8_rows += 1
    except UnicodeDecodeError:
        ill_formed_rows += 1
    lengths = [len(s)//2 for s in fields]
    assert max(lengths) <= 1024
    max_idna = max(max_idna, *lengths)
    idna += struct.pack('<I4H3Bx', line_no, *lengths, *flags)+b''.join(fields); idna_count += 1
idna = b'CHOSI18\0'+struct.pack('<I', idna_count)+idna
o.out.mkdir(parents=True, exist_ok=True)
files = {'NORM.BIN': norm, 'IDNA.BIN': idna}
for name, data in files.items():
    (o.out/name).write_bytes(data)
metadata = {'schema': 1, 'normalization_rows': count, 'normalization_max_utf16': max_norm,
            'idna_rows': idna_count, 'idna_max_utf16': max_idna,
            'idna_utf8_rows': utf8_rows, 'idna_ill_formed_utf16_rows': ill_formed_rows,
            'files': {n: {'bytes': len(d), 'sha256': hashlib.sha256(d).hexdigest()} for n, d in files.items()}}
(o.out/'vectors.json').write_text(json.dumps(metadata, indent=2)+'\n')
print(json.dumps(metadata))
