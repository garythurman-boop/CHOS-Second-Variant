#!/usr/bin/env python3
"""Build original native JPEG API and real corpus consumer using verified SDK bytes.

Host builds additionally run the original upstream core regression tests before
producing corpus input. Neither build is WebCore/browser qualification.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import time

root = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=pathlib.Path, required=True)
p.add_argument('--native-sdk', type=pathlib.Path)
p.add_argument('--host', action='store_true')
p.add_argument('--sanitize', action='store_true')
p.add_argument('--reference-build', type=pathlib.Path,
               help='Completed upstream host build with successful original regression output')
p.add_argument('--cmake', default='cmake')
p.add_argument('--ninja', default='ninja')
o = p.parse_args()
if o.host == bool(o.native_sdk):
    p.error('Select exactly one of --host or --native-sdk')
if o.sanitize and not o.host:
    p.error('--sanitize requires --host')
if not o.host and not o.reference_build:
    p.error('Native corpus requires --reference-build')
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
verify = ['python3', str(root / 'tools/verify_native_jpeg_sources.py')]
subprocess.run(verify, check=True)
sdk = o.native_sdk.resolve() if o.native_sdk else None
if sdk:
    subprocess.run(['python3', str(root / 'tools/verify_native_sdk.py'), str(sdk)], check=True)
out = o.out.resolve()
out.mkdir(parents=True, exist_ok=False)
prefix = out / 'prefix'
cmake = shutil.which(o.cmake) or o.cmake
ninja = shutil.which(o.ninja) or o.ninja
ctest = str(pathlib.Path(cmake).with_name('ctest'))
inputs = ['tools/build_native_jpeg.py', 'tools/verify_native_jpeg_sources.py',
          'tools/generate_jpeg_cases.py', 'tests/native_jpeg/main.c',
          'tests/native_jpeg/CMakeLists.txt']
report = {'status': 'BUILDING', 'executed': False, 'stage3_complete': False,
          'host_reference': o.host, 'host_sanitizers': o.sanitize,
          'source_manifest_sha256': sha(root / 'docs/dependencies/native-jpeg.json'),
          'SDK_manifest_sha256': sha(sdk / 'chos-sdk.json') if sdk else None,
          'port_and_fixture_sources': {name: sha(root / name) for name in inputs},
          'commands': [], 'limits': [
              'Original libjpeg API prerequisite;not WebCore or browser integration',
              'Target TurboJPEG,command-line tools and mutable environment helper excluded',
              'Selected8-bit decode tests;not full native upstream conformance,production hostile-content isolation or color management']}
started = time.monotonic()

def save():
    (out / 'build.json').write_text(json.dumps(report, indent=2) + '\n')

def run(name, args):
    report['commands'].append(args)
    save()
    with (out / (name + '.log')).open('wb') as log:
        try:
            code = subprocess.run(args, cwd=root, stdout=log, stderr=subprocess.STDOUT).returncode
        except OSError as error:
            log.write((str(error) + '\n').encode())
            code = 127
    if code:
        report.update(status='BUILD_FAILED', failed_step=name, returncode=code,
                      seconds=time.monotonic()-started)
        save()
        raise SystemExit(code)

base = [cmake, '-G', 'Ninja', '-DCMAKE_MAKE_PROGRAM=' + str(ninja),
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_INSTALL_PREFIX=' + str(prefix)]
if sdk:
    base += ['-DCMAKE_TOOLCHAIN_FILE=' + str(root / 'ports/native/chos-toolchain.cmake'),
             '-DCHOS_SDK_ROOT=' + str(sdk), '-DCHOS_APPLICATION_PROFILE=engine']
# Upstream NO_PUTENV excludes an unused inline setter for the selected libjpeg
# API. CHOS does not declare or pretend to implement setenv. getenv is real.
flags = '-DNO_PUTENV'
if o.sanitize:
    flags += ' -fsanitize=address,undefined -fno-omit-frame-pointer'
    base += ['-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
base += ['-DCMAKE_C_FLAGS=' + flags]
codec = out / 'jpeg'
run('jpeg-setup', [*base, '-S', str(root / 'third_party/libjpeg-turbo'), '-B', str(codec),
    '-DENABLE_SHARED=OFF', '-DENABLE_STATIC=ON', '-DWITH_TURBOJPEG=OFF',
    '-DWITH_TOOLS=' + ('ON' if o.host else 'OFF'),
    '-DWITH_TESTS=' + ('ON' if o.host else 'OFF'),
    '-DWITH_SIMD=' + ('OFF' if o.host else 'ON'),
    '-DREQUIRE_SIMD=' + ('OFF' if o.host else 'ON')])
run('jpeg-build', [cmake, '--build', str(codec), '-j4'])
run('jpeg-install', [cmake, '--install', str(codec)])
if o.host:
    run('upstream-tests', [ctest, '--test-dir', str(codec), '--output-on-failure', '-j1'])
    report['upstream_host_tests'] = {'status': 'PASS', 'log_sha256': sha(out/'upstream-tests.log')}
reference = o.reference_build.resolve() if o.reference_build else codec
run('cases', ['python3', str(root / 'tools/generate_jpeg_cases.py'),
              '--host-build', str(reference), '--out', str(out / 'cases')])
run('application-setup', [*base, '-S', str(root / 'tests/native_jpeg'),
    '-B', str(out / 'application'), '-DJPEG_PREFIX=' + str(prefix),
    '-DJPEG_CASES=' + str(out / 'cases')])
run('application-build', [cmake, '--build', str(out / 'application'), '-j4'])
for name, expected in report['port_and_fixture_sources'].items():
    assert sha(root / name) == expected, 'Source changed during build: ' + name
assert sha(root / 'docs/dependencies/native-jpeg.json') == report['source_manifest_sha256']
subprocess.run(verify, check=True)
if sdk:
    assert sha(sdk / 'chos-sdk.json') == report['SDK_manifest_sha256']
    subprocess.run(['python3', str(root / 'tools/verify_native_sdk.py'), str(sdk)], check=True)
archive = prefix / 'lib/libjpeg.a'
members = subprocess.check_output(['llvm-ar', 't', str(archive)], text=True).splitlines()
elf = out / 'application/native_jpeg'
if sdk:
    assert subprocess.check_output(['llvm-readelf', '-h', str(archive)], text=True).count(
        'Machine:                           AArch64') == len(members)
    assert not subprocess.check_output(['llvm-nm', '-u', str(elf)], text=True).strip()
report.update(status='LINKED_NOT_EXECUTED', returncode=0, seconds=time.monotonic()-started,
              application={'path': str(elf), 'bytes': elf.stat().st_size, 'sha256': sha(elf)},
              archive={'members': len(members), 'bytes': archive.stat().st_size, 'sha256': sha(archive)},
              cases_manifest_sha256=sha(out/'cases/cases.json'),
              installed_files_sha256={str(f.relative_to(prefix)): sha(f)
                  for f in sorted(prefix.rglob('*')) if f.is_file()})
save()
print(json.dumps({name: report[name] for name in ['status', 'application', 'archive']}, indent=2))
