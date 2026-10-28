#!/usr/bin/env python3
"""Independent full case-fold oracle from Python's Unicode15 database."""
import argparse,pathlib,unicodedata
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--check',action='store_true');o=p.parse_args()
assert unicodedata.unidata_version=='15.0.0','reproduction requires Python Unicode15.0.0'
rows=[];units=[]
for scalar in range(0x110000):
 if 0xd800<=scalar<=0xdfff:continue
 c=chr(scalar);fold=c.casefold()
 if fold==c:continue
 encoded=fold.encode('utf-16-le');value=[int.from_bytes(encoded[i:i+2],'little') for i in range(0,len(encoded),2)]
 rows.append((scalar,len(units),len(value)));units.extend(value)
s='/* Python Unicode15.0.0 full case-fold mappings; independent of ICU source. */\n'
s+='static const char16_t icu_fold_units[]={'+','.join(hex(x) for x in units)+'};\n'
s+='struct icu_fold_vector {uint32_t scalar,offset,length;};\nstatic const icu_fold_vector icu_fold_vectors[]={\n'
s+=''.join('{0x%x,%u,%u},\n'%v for v in rows)+'};\n'
target=pathlib.Path(__file__).with_name('icu_fold_vectors.h')
if o.check:assert target.read_text()==s,'case-fold corpus changed'
else:target.write_text(s)
print(len(rows),'independent Unicode15 full case-fold mappings')
