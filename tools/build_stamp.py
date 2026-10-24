#!/usr/bin/env python3
"""Write a reproducible source-revision header, preserving mtime if unchanged."""
import pathlib,re,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
try:
    revision=subprocess.check_output(['git','rev-parse','--verify','HEAD'],cwd=root,text=True,stderr=subprocess.DEVNULL).strip()
    if not re.fullmatch('[0-9a-f]{40}',revision):raise ValueError('Invalid revision')
    dirty=any(subprocess.run(cmd,cwd=root,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode for cmd in
              [['git','diff','--quiet'],['git','diff','--cached','--quiet']])
    label=revision[:12]+('-dirty' if dirty else '')
except (OSError,subprocess.SubprocessError,ValueError):label='source-export'
p=pathlib.Path(sys.argv[1]);p.parent.mkdir(parents=True,exist_ok=True)
contents='#ifndef CHOS_BUILD_STAMP_H\n#define CHOS_BUILD_STAMP_H\n#define CHOS_BUILD_REVISION "'+label+'"\n#endif\n'
if not p.exists() or p.read_text()!=contents:p.write_text(contents)
