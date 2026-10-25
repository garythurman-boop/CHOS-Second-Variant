#!/usr/bin/env python3
"""Local relocation fixture: constrain EFI loader allocation above 2 GiB.

The production loader remains unchanged. Only its minimum allocation address is
changed in a copied source; kernel/runtime bytes are the actual current build.
This is a relocation/translation test input, never a publishable distribution.
"""
import argparse,hashlib,json,pathlib,subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',required=True,type=pathlib.Path)
a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
original=pathlib.Path('boot/uefi/bootaa64.c').read_bytes()
old=b'#define KERNEL_MIN_PHYSICAL_ADDRESS ((uint64_t)0x01000000u)'
assert original.count(old)==1
source=out/'bootaa64-test.c';source.write_bytes(original.replace(old,b'#define KERNEL_MIN_PHYSICAL_ADDRESS ((uint64_t)0x80000000u)'))
commands=[['clang','--target=aarch64-pc-windows-msvc','-Iinclude','-fshort-wchar','-ffreestanding','-fno-builtin','-fno-stack-protector','-mno-stack-arg-probe','-Wall','-Wextra','-Werror','-O2','-c',str(source),'-o',str(out/'bootaa64.obj')],
 ['lld-link','/machine:arm64','/subsystem:efi_application','/entry:efi_main','/nodefaultlib','/out:'+str(out/'BOOTAA64.EFI'),'/base:0x100000','/dynamicbase','/filealign:512',str(out/'bootaa64.obj'),'build/uefi/display_modes.obj'],
 ['python3','tools/mkuefi_iso.py','--kernel','build/uefi-kernel/chos.elf','--efi',str(out/'BOOTAA64.EFI'),'--readme','docs/RELEASE_NOTES_0.30.0.md','--output',str(out/'relocation-test.iso')]]
for c in commands:subprocess.run(c,check=True)
files=[source,out/'BOOTAA64.EFI',out/'relocation-test.iso',pathlib.Path('build/uefi-kernel/chos.elf')]
(out/'fixture.json').write_text(json.dumps({'purpose':'local test only, production loader unchanged','minimum_kernel_address':'0x80000000','commands':commands,'production_loader_source_sha256':hashlib.sha256(original).hexdigest(),'sha256':{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in files}},indent=2))
