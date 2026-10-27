#!/usr/bin/env python3
"""Stage a real static native SDK; no host libraries or unsupported API stubs."""
import argparse,hashlib,json,pathlib,shutil,subprocess,tempfile,os
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=pathlib.Path,default=root/'build');p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--libc-object',type=pathlib.Path,action='append',required=True);o=p.parse_args();build=o.build.resolve();out=o.out.resolve()
# Only replace an intact tree previously produced by this installer. Refuse
# user additions/edits and build in a fresh sibling so stale files cannot persist.
if out==root or out==build or out.is_relative_to(root/'.git'):p.error('unsafe SDK output')
if out.exists():
 manifest_path=out/'chos-sdk.json'
 if not manifest_path.is_file():p.error('output exists without a native SDK manifest')
 old=json.loads(manifest_path.read_text())
 actual={str(p.relative_to(out)) for p in out.rglob('*') if p.is_file() and p!=manifest_path}
 if actual!=set(old['files']):p.error('SDK output contains added/removed files')
 for name,wanted in old['files'].items():
  if '..' in pathlib.Path(name).parts or hashlib.sha256((out/name).read_bytes()).hexdigest()!=wanted:p.error('SDK output was modified: '+name)
out.parent.mkdir(parents=True,exist_ok=True)
final=out;out=pathlib.Path(tempfile.mkdtemp(prefix=out.name+'.stage-',dir=out.parent))
inc=out/'include';cpp=inc/'c++/v1';lib=out/'lib';lib.mkdir()
for source,target in [(root/'userland/libc/include',inc),(build/'userland/libm/include',inc),(root/'include/chos',inc/'chos'),(root/'userland/libchos/include/chos',inc/'chos'),(root/'third_party/libcxx/include',cpp),(root/'userland/libcxx/include',cpp),(build/'userland/libcxx/include',cpp)]:
 shutil.copytree(source,target,dirs_exist_ok=True)
for source,name in [(build/'userland/start.o','crt0.o'),(build/'userland/libc/static_startup.o','crtinit.o'),(build/'userland/native_abi.o','chos-abi.o'),(build/'userland/libcxx/libchos-c++.a','libchos-c++.a'),(build/'userland/libm/libchos-m.a','libchos-m.a'),(build/'userland/libquad/libchos-quad.a','libchos-quad.a'),(build/'userland/libcomplex/libchos-complex.a','libchos-complex.a'),(build/'userland/libwide/libchos-wide.a','libchos-wide.a'),(build/'userland/libstdio/libchos-stdio.a','libchos-stdio.a'),(build/'userland/floatscan/libchos-floatparse.a','libchos-floatparse.a'),(root/'userland/app.ld','app.ld')]:shutil.copyfile(source,lib/name)
licenses=out/'licenses';licenses.mkdir()
for source,name in [(root/'third_party/libcxx/LICENSE.TXT','libcxx.txt'),(root/'third_party/libcxxabi/LICENSE.TXT','libcxxabi.txt'),(root/'third_party/compiler-rt/LICENSE.TXT','compiler-rt.txt'),(root/'third_party/musl/COPYRIGHT','musl.txt')]:shutil.copyfile(source,licenses/name)
objects=[p.resolve() for p in o.libc_object]
assert objects and len(set(objects))==len(objects)
assert all(p.is_file() and p.is_relative_to(build/'userland/libc') and p.name!='static_startup.o' for p in objects)
archive=lib/'libchos-c.a'
if archive.exists():archive.unlink()
subprocess.run(['llvm-ar','rcs',str(archive),*map(str,objects)],check=True)
files={str(p.relative_to(out)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out.rglob('*')) if p.is_file() and p.name!='chos-sdk.json'}
manifest={'schema':1,'target':'aarch64-none-elf','format':'CHOS native static ELF','profile':'bounded static C/C++20/23,normal main startup,opt-in RTTI,no exceptions/dynamic linking','source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'files':files,'limits':['not full hosted C/C++/POSIX; unsupported imports must fail to link','2MiB native image and existing tiny heap/thread budgets','no Linux or host library compatibility; real engine not yet built']}
(out/'chos-sdk.json').write_text(json.dumps(manifest,indent=2)+'\n')
backup=final.with_name(final.name+'.previous-'+str(os.getpid()))
assert not backup.exists()
if final.exists():final.rename(backup)
try:out.rename(final)
except BaseException:
 if backup.exists():backup.rename(final)
 raise
if backup.exists():shutil.rmtree(backup)
print('Native SDK:',final,'files:',len(files))
