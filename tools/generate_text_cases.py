#!/usr/bin/env python3
"""Generate literal native vectors from one complete pinned upstream shaper file."""
import pathlib,json,hashlib,argparse
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);o=p.parse_args()
m=json.loads((root/'docs/dependencies/text-fixtures.json').read_text());base=root/'third_party/text-fixtures'
for name,wanted in m['files'].items():assert hashlib.sha256((base/name).read_bytes()).hexdigest()==wanted,name
lines=(base/m['original_harfbuzz_tests']['path']).read_text().splitlines();cases=[]
for line in lines:
 if not line or line.startswith(('#','@')):continue
 name,options,sequence,expected=line.split(';');assert not options
 font=next(f['index'] for f in m['fonts'] if f['path']=='fonts/'+pathlib.Path(name).name)
 cps=[int(c.removeprefix('U+'),16) for c in sequence.split(',')];assert len(cps)<=128 and len(expected)<8192
 cases.append('{'+str(font)+','+str(len(cps))+',{'+','.join(hex(c) for c in cps)+'},'+json.dumps(expected)+'}')
assert len(cases)==m['original_harfbuzz_tests']['cases']==64
text='// Generated from the complete pinned original Arabic normalization corpus.\nstruct TextCase {unsigned font,length;uint32_t codepoints[128];const char *expected;};\nstatic const TextCase textCases[]={\n'+',\n'.join(cases)+'\n};\n'
o.out.parent.mkdir(parents=True,exist_ok=True);o.out.write_text(text)
