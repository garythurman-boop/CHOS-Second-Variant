#!/usr/bin/env python3
"""Apply the explicit native clock-error fix to a verified original putil copy."""
import argparse,hashlib,json,pathlib
root=pathlib.Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);o=p.parse_args()
original=root/'third_party/icu-common/source/common/putil.cpp';data=original.read_bytes();m=json.loads((root/'docs/dependencies/icu-common.json').read_text())
assert hashlib.sha256(data).hexdigest()==m['files']['source/common/putil.cpp']
s=data.decode();old='    gettimeofday(&posixTime, nullptr);';new='    if (gettimeofday(&posixTime, nullptr) != 0) { return uprv_getNaN(); }'
assert s.count(old)==2
# Both the normal clock and optional upstream fake-clock diagnostic must not
# consume uninitialized fields when the real native clock is unavailable.
s=s.replace(old,new);o.out.parent.mkdir(parents=True,exist_ok=True);o.out.write_text(s)
print('ICU platform copy:',o.out,'clock failures return NaN; original source unchanged')
