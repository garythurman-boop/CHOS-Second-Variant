#!/usr/bin/env python3
"""Build the actual LLVM bitset/stack qualification fixture with verified inputs.

Host mode uses the SDK's original libc++ headers, the existing native string
instantiations and the exact original verbose-abort source from the pinned LLVM
release archive. It is a selected host template fixture, not a full host libc++.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import tarfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=pathlib.Path, required=True)
p.add_argument('--sdk', type=pathlib.Path, required=True)
p.add_argument('--host', action='store_true')
p.add_argument('--sanitize', action='store_true')
p.add_argument('--llvm-archive', type=pathlib.Path)
p.add_argument('--host-cxx')
o = p.parse_args()
if o.sanitize and not o.host:
    p.error('--sanitize requires --host')
if o.host and not o.llvm_archive:
    p.error('--host requires the verified --llvm-archive')
sdk = o.sdk.resolve()
out = o.out.resolve()
subprocess.run(['python3', str(ROOT / 'tools/verify_native_sdk.py'), str(sdk)], check=True)
subprocess.run(['python3', str(ROOT / 'tools/verify_libcxx.py')], check=True)
manifest = json.loads((ROOT / 'docs/dependencies/libcxx-containers.json').read_text())
for name, entry in manifest['headers'].items():
    assert sha(ROOT / 'third_party/libcxx/include' / name) == entry['sha256'], name
    assert sha(sdk / 'include/c++/v1' / name) == entry['sha256'], name
out.mkdir(parents=True, exist_ok=False)
inputs = ['tools/build_native_stdlib_containers.py', 'tools/verify_native_sdk.py',
          'tools/verify_libcxx.py', 'docs/dependencies/libcxx-native.json',
          'docs/dependencies/libcxx-containers.json',
          'tests/native_stdlib_containers/main.cpp',
          'tests/native_stdlib_containers/CMakeLists.txt']
if o.host:
    inputs.append('userland/libcxx/string_instantiations.cpp')
report = {'schema': 1, 'status': 'BUILDING', 'executed': False,
          'stage3_complete': False, 'host_reference': o.host,
          'host_sanitizers': o.sanitize,
          'SDK_manifest_sha256': sha(sdk / 'chos-sdk.json'),
          'sources_sha256': {name: sha(ROOT / name) for name in inputs},
          'commands': [], 'limits': [
              'Selected original bitset/stack semantics, not complete libc++ conformance',
              'Native exception support and hostile-input hardening are not qualified',
              'Not WebCore execution, modern browser or physical-platform acceptance']}
start = time.monotonic()


def save():
    report['seconds'] = time.monotonic() - start
    (out / 'build.json').write_text(json.dumps(report, indent=2) + '\n')


def run(name, command):
    report['commands'].append(command)
    save()
    with (out / (name + '.log')).open('wb') as log:
        result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        report.update(status='BUILD_FAILED', failed_step=name, returncode=result.returncode)
        save()
        raise SystemExit(result.returncode)


if o.host:
    archive = o.llvm_archive.resolve()
    assert sha(archive) == manifest['source_archive_sha256'], 'Wrong LLVM release archive'
    member = 'libcxx-19.1.7.src/src/verbose_abort.cpp'
    with tarfile.open(archive) as source:
        entry = source.getmember(member)
        assert entry.isfile() and not entry.issym() and not entry.islnk()
        data = source.extractfile(entry).read()
    original = out / 'verbose_abort.cpp'
    original.write_bytes(data)
    report['original_host_support'] = {'archive_sha256': sha(archive),
        'member': member, 'sha256': sha(original), 'bytes': len(data),
        'purpose': 'Original diagnostic termination helper only; no replacement container code'}
    compiler = shutil.which(o.host_cxx or ('g++' if o.sanitize else 'clang++'))
    assert compiler
    flags = ['-std=c++23', '-fno-builtin', '-fno-exceptions', '-fno-rtti',
             '-nostdinc++', '-isystem', str(sdk / 'include/c++/v1'), '-pthread']
    flags += ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if o.sanitize else ['-O2']
    app = out / 'native_stdlib_containers'
    run('host-build', [compiler, *flags, str(ROOT / 'tests/native_stdlib_containers/main.cpp'),
                      str(ROOT / 'userland/libcxx/string_instantiations.cpp'), str(original), '-o', str(app)])
else:
    cmake = shutil.which('cmake')
    ninja = shutil.which('ninja')
    assert cmake and ninja
    run('setup', [cmake, '-S', str(ROOT / 'tests/native_stdlib_containers'),
                 '-B', str(out / 'application'), '-G', 'Ninja',
                 '-DCMAKE_MAKE_PROGRAM=' + ninja, '-DCMAKE_BUILD_TYPE=Release',
                 '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                 '-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT / 'ports/native/chos-toolchain.cmake'),
                 '-DCHOS_SDK_ROOT=' + str(sdk), '-DCHOS_APPLICATION_PROFILE=engine'])
    run('build', [cmake, '--build', str(out / 'application'), '-j4'])
    app = out / 'application/native_stdlib_containers'
    assert 'Machine:                           AArch64' in subprocess.check_output(['llvm-readelf', '-h', str(app)], text=True)
    assert not subprocess.check_output(['llvm-nm', '-u', str(app)], text=True).strip()
for name, expected in report['sources_sha256'].items():
    assert sha(ROOT / name) == expected, name
assert sha(sdk / 'chos-sdk.json') == report['SDK_manifest_sha256']
subprocess.run(['python3', str(ROOT / 'tools/verify_native_sdk.py'), str(sdk)], check=True)
subprocess.run(['python3', str(ROOT / 'tools/verify_libcxx.py')], check=True)
report.update(status='LINKED_NOT_EXECUTED', returncode=0,
              application={'path': str(app), 'sha256': sha(app), 'bytes': app.stat().st_size})
save()
print(json.dumps({key: report[key] for key in ['status', 'application']}, indent=2))
