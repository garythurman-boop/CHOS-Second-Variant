#!/usr/bin/env python3
"""Prepare ICU's decimal header without its unused signal dependency."""
import argparse,hashlib,json,pathlib
root=pathlib.Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);o=p.parse_args();base=root/'third_party/icu-i18n';manifest=json.loads((root/'docs/dependencies/icu-i18n.json').read_text())
def checked(name):
 data=(base/'source/i18n'/name).read_bytes();assert hashlib.sha256(data).hexdigest()==manifest['files']['source/i18n/'+name];return data
header=checked('decContext.h');source=checked('decContext.cpp');assert b'#if 0  /* ICU: Do not raise signals. */\n  if (status & context->traps) raise(SIGFPE);\n#endif' in source
old=b'  #include <signal.h>              /* for traps                       */';assert header.count(old)==1
patched=header.replace(old,b'  /* CHOS: unused include removed; ICU signal raising is already disabled. */')
o.out.parent.mkdir(parents=True,exist_ok=True);o.out.write_bytes(patched)
print('Checked decimal header prepared; native signal handling remains unsupported')
