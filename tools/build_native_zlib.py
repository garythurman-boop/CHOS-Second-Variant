#!/usr/bin/env python3
"""Build the pinned zlib memory API and a real native or sanitized host consumer."""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', type=pathlib.Path, required=True)
parser.add_argument('--native-sdk', type=pathlib.Path)
parser.add_argument('--host', action='store_true')
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--host-cc', default='cc', help='Host C compiler, including its sanitizer runtime')
args = parser.parse_args()
if args.host == bool(args.native_sdk):
    parser.error('Select exactly one of --host or --native-sdk')
if args.sanitize and not args.host:
    parser.error('--sanitize requires --host')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


subprocess.run(['python3', str(ROOT/'tools/verify_native_png_sources.py')], check=True)
sdk = args.native_sdk.resolve() if args.native_sdk else None
if sdk:
    subprocess.run(['python3', str(ROOT/'tools/verify_native_sdk.py'), str(sdk)], check=True)
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=False)
prefix = out/'prefix'
inputs = [ROOT/'tools/build_native_zlib.py', ROOT/'tools/verify_native_png_sources.py']
inputs += sorted(path for directory in ['ports/zlib', 'tests/native_zlib']
                 for path in (ROOT/directory).rglob('*')
                 if path.is_file() and '__pycache__' not in path.parts)
report = {
    'status': 'BUILDING', 'executed': False, 'stage3_complete': False,
    'host_reference': args.host, 'host_sanitizers': args.sanitize,
    'SDK_manifest_sha256': sha(sdk/'chos-sdk.json') if sdk else None,
    'source_manifest_sha256': sha(ROOT/'docs/dependencies/native-png.json'),
    'port_and_fixture_sources': {str(path.relative_to(ROOT)): sha(path) for path in inputs},
    'profile': {
        'upstream_version': '1.4', 'translation_units': 11,
        'memory_formats': ['raw deflate', 'zlib', 'gzip'],
        'default_allocators': 'Original zcalloc/zcfree use real malloc/calloc/free',
        'utilities': ['compress', 'compress2', 'compressBound', 'uncompress', 'uncompress2',
                      'upstream size_t variants'],
        'excluded': ['gzFile and filesystem entry points'],
        'private_definition': 'CHOS_ZLIB_MEMORY_API=1',
        'forbidden_definitions': ['Z_SOLO', 'NO_GZIP', 'NO_GZCOMPRESS'],
    },
    'commands': [],
    'limits': [
        'Memory API prerequisite; not WebCore integration or a full browser',
        'gzFile declarations remain in original public header; entry points intentionally do not link',
        'Selected streams and failures; not the complete upstream conformance suite or a security audit',
    ],
}
started = time.monotonic()


def save():
    (out/'build.json').write_text(json.dumps(report, indent=2)+'\n')


def run(name, command, cwd=ROOT):
    report['commands'].append(command)
    save()
    with (out/(name+'.log')).open('wb') as log:
        try:
            code = subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT).returncode
        except OSError as error:
            log.write((str(error)+'\n').encode())
            code = 127
    if code:
        report.update(status='BUILD_FAILED', failed_step=name, returncode=code,
                      seconds=time.monotonic()-started)
        save()
        raise SystemExit(code)


source = out/'zlib-source'
shutil.copytree(ROOT/'third_party/zlib', source)
run('memory-api-patch', ['patch', '--batch', '--fuzz=0', '--no-backup-if-mismatch',
                        '-p1', '-i', str(ROOT/'ports/zlib/memory-api.patch')], source)
original_files = {path.relative_to(ROOT/'third_party/zlib'): path
                  for path in (ROOT/'third_party/zlib').rglob('*') if path.is_file()}
copied_files = {path.relative_to(source): path for path in source.rglob('*') if path.is_file()}
assert copied_files.keys() == original_files.keys(), 'Patched source inventory differs'
changed = [str(name) for name in original_files if sha(original_files[name]) != sha(copied_files[name])]
assert changed == ['zutil.c'], changed
report['fresh_copy_modifications'] = {
    name: {'original_sha256': sha(original_files[pathlib.Path(name)]),
           'adapted_sha256': sha(source/name)} for name in changed}
cmake = shutil.which('cmake') or 'cmake'
ninja = shutil.which('ninja') or 'ninja'
base = [cmake, '-G', 'Ninja', '-DCMAKE_MAKE_PROGRAM='+ninja,
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_INSTALL_PREFIX='+str(prefix),
        '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
if sdk:
    base += ['-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'ports/native/chos-toolchain.cmake'),
             '-DCHOS_SDK_ROOT='+str(sdk), '-DCHOS_APPLICATION_PROFILE=engine']
else:
    base += ['-DCMAKE_C_COMPILER='+args.host_cc]
if args.sanitize:
    base += ['-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer',
             '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
run('zlib-setup', [*base, '-S', str(ROOT/'ports/zlib'), '-B', str(out/'zlib'),
                   '-DCHOS_ZLIB_SOURCE_DIR='+str(source)])
run('zlib-build', [cmake, '--build', str(out/'zlib'), '-j4'])
run('zlib-install', [cmake, '--install', str(out/'zlib')])
run('application-setup', [*base, '-S', str(ROOT/'tests/native_zlib'),
                          '-B', str(out/'application'), '-DZLIB_PREFIX='+str(prefix),
                          '-DCMAKE_PREFIX_PATH='+str(prefix)])
run('application-build', [cmake, '--build', str(out/'application'), '-j4'])
for name, wanted in report['port_and_fixture_sources'].items():
    assert sha(ROOT/name) == wanted, 'Source changed during build: '+name
assert sha(ROOT/'docs/dependencies/native-png.json') == report['source_manifest_sha256']
run('original-source-verification', ['python3', str(ROOT/'tools/verify_native_png_sources.py')])
if sdk:
    assert sha(sdk/'chos-sdk.json') == report['SDK_manifest_sha256'], 'SDK manifest changed'
    run('SDK-verification', ['python3', str(ROOT/'tools/verify_native_sdk.py'), str(sdk)])
archive = prefix/'lib/libz.a'
members = subprocess.check_output(['llvm-ar', 't', str(archive)], text=True).splitlines()
assert len(members) == 11
if sdk:
    headers = subprocess.check_output(['llvm-readelf', '-h', str(archive)], text=True)
    assert headers.count('Machine:                           AArch64') == len(members)
symbols = subprocess.check_output(['llvm-nm', '--defined-only', '--extern-only', str(archive)], text=True)
defined = {line.split()[-1] for line in symbols.splitlines() if len(line.split()) >= 3}
required = {'compress', 'compress2', 'compressBound', 'uncompress', 'uncompress2',
            'deflate', 'deflateInit2_', 'inflate', 'inflateInit2_', 'zcalloc', 'zcfree'}
assert required <= defined, sorted(required-defined)
assert not any(name.startswith('gz') for name in defined), 'Unexpected gzFile implementation'
elf = out/'application/native_zlib'
if sdk:
    assert not subprocess.check_output(['llvm-nm', '-u', str(elf)], text=True).strip()
report.update(
    status='LINKED_NOT_EXECUTED', returncode=0, seconds=time.monotonic()-started,
    archive={'path': str(archive), 'members': len(members), 'bytes': archive.stat().st_size,
             'sha256': sha(archive), 'gzFile_entrypoints': [],
             'required_memory_symbols_present': sorted(required)},
    application={'path': str(elf), 'bytes': elf.stat().st_size, 'sha256': sha(elf)},
    cases_manifest_sha256=sha(out/'application/cases/cases.json'),
    cases=json.loads((out/'application/cases/cases.json').read_text()),
    compile_commands_sha256=sha(out/'zlib/compile_commands.json'),
    installed_files_sha256={str(path.relative_to(prefix)): sha(path)
                            for path in sorted(prefix.rglob('*')) if path.is_file()},
)
save()
print(json.dumps({key: report[key] for key in ['status', 'application', 'archive']}, indent=2))
