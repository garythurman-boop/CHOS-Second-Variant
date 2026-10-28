#!/usr/bin/env python3
"""Create the checked native printf copy without modifying original musl bytes."""
import argparse,hashlib,json,pathlib
root=pathlib.Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);o=p.parse_args()
original=root/'third_party/musl/src/stdio/vfprintf.c';data=original.read_bytes();m=json.loads((root/'docs/dependencies/musl-format.json').read_text());assert hashlib.sha256(data).hexdigest()==m['files']['src/stdio/vfprintf.c'];s=data.decode()
old='\t\t\tif (w<0) fl|=LEFT_ADJ, w=-w;';new='\t\t\tif (w<0) { if (w==INT_MIN) goto overflow; fl|=LEFT_ADJ; w=-w; }';assert s.count(old)==1;s=s.replace(old,new)
# POSIX argument selectors start at one. The original algorithm reserves slot0
# and must not consume an uninitialized union from that slot for malformed input.
for pattern,index in [("if (isdigit(s[1]) && s[2]=='$') {",1),("if (isdigit(s[2]) && s[3]=='$') {",2)]:
 assert s.count(pattern)==(2 if index==1 else 1);s=s.replace(pattern,pattern+f"\n\t\t\tif (s[{index}]=='0') goto inval;")
o.out.parent.mkdir(parents=True,exist_ok=True);o.out.write_text(s)
print('Checked printf copy: INT_MIN width overflow and invalid positional slot0 rejected')
