#!/usr/bin/env python3
"""Prepare a checked native decimal header for i18n dependency investigation.

ICU already compiles out decimal signal raising. Remove its unused signal.h
include; no signal API or altered trapping behavior is supplied.
"""
import argparse,hashlib,json,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__);p.add_argument('--repository',type=pathlib.Path,required=True);p.add_argument('--out',type=pathlib.Path,required=True);o=p.parse_args();commit=json.loads((root/'docs/dependencies/icu-common.json').read_text())['commit']
def original(name):return subprocess.check_output(['git','-C',str(o.repository),'show',commit+':icu4c/source/i18n/'+name])
header=original('decContext.h');source=original('decContext.cpp')
assert b'#if 0  /* ICU: Do not raise signals. */\n  if (status & context->traps) raise(SIGFPE);\n#endif' in source
old=b'  #include <signal.h>              /* for traps                       */'
assert header.count(old)==1
patched=header.replace(old,b'  /* CHOS: unused signal.h removed; ICU signal raising is already disabled. */')
assert not o.out.exists(),'refuse an existing probe output';o.out.mkdir(parents=True)
(o.out/'decContext.h').write_bytes(patched)
m={'schema':1,'source_commit':commit,'scope':'checked removal of an unused include only; no native signal support claim','original_header_sha256':hashlib.sha256(header).hexdigest(),'original_source_sha256':hashlib.sha256(source).hexdigest(),'patched_header_sha256':hashlib.sha256(patched).hexdigest(),'method':'force-include the checked replacement using the original DECCONTEXT guard; all other original sources are unchanged'}
(o.out/'patch.json').write_text(json.dumps(m,indent=2)+'\n');print('Prepared checked decimal header; upstream signal raising remains disabled')
