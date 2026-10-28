#!/usr/bin/env python3
"""Generate default ICU data with original host tools, then a real ARM64 archive.

Host tools are build dependencies only. This does not execute ICU or an engine
inside CHOS, and does not widen the native loader's existing resource limits.
"""
import argparse, hashlib, json, os, pathlib, shutil, subprocess, sys, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=pathlib.Path, required=True)
p.add_argument('--work-dir', type=pathlib.Path)
p.add_argument('--jobs', type=int, default=4)
p.add_argument('--host-cc', default='gcc'); p.add_argument('--host-cxx', default='g++')
p.add_argument('--native-cc', default='clang'); p.add_argument('--native-ar', default='llvm-ar')
o = p.parse_args()
if not 1 <= o.jobs <= 16: p.error('jobs must be between1 and16')
out = o.out.resolve()
if out == root or out == root/'build' or out.is_relative_to(root/'.git'):
    p.error('unsafe output')
if out.exists():
    m = out/'build-report.json'
    if not m.is_file(): p.error('output exists without an ownership report')
    old = json.loads(m.read_text())
    if {str(f.relative_to(out)) for f in out.rglob('*') if f.is_file() and f != m} != set(old['files']):
        p.error('output has added/removed files')
    for name, wanted in old['files'].items():
        if '..' in pathlib.Path(name).parts or hashlib.sha256((out/name).read_bytes()).hexdigest() != wanted:
            p.error('output was modified: '+name)
tools = {}
for key, name in [('host_cc', o.host_cc), ('host_cxx', o.host_cxx),
                  ('native_cc', o.native_cc), ('native_ar', o.native_ar),
                  ('make', 'make'), ('objcopy', 'llvm-objcopy'), ('nm', 'llvm-nm')]:
    found = shutil.which(name)
    if not found: p.error('missing build tool: '+name)
    tools[key] = str(pathlib.Path(found).resolve())
if o.work_dir:
    work = o.work_dir.resolve(); work.mkdir(parents=True, exist_ok=False)
else:
    work = pathlib.Path(tempfile.mkdtemp(prefix='chos-icu-data-'))
print('Retained data build directory:', work, flush=True)
source_manifest = json.loads((root/'docs/dependencies/icu-data-source.json').read_text())
env = dict(os.environ, CC=tools['host_cc'], CXX=tools['host_cxx'],
           SOURCE_DATE_EPOCH=str(source_manifest['source_date_epoch']), PYTHONHASHSEED='0', TZ='UTC', LC_ALL='C.UTF-8')
# Do not inherit native target flags or search paths into host build tools.
for key in ('CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'LIBS', 'ICU_DATA'):
    env.pop(key, None)
commands = []
def run(command, log, cwd=work):
    command = list(map(str, command)); commands.append(command)
    with (work/log).open('wb') as f:
        subprocess.run(command, cwd=cwd, env=env, stdout=f, stderr=subprocess.STDOUT,
                       check=True, timeout=1800)
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
try:
    source = work/'source'; host = work/'host-build'; host.mkdir()
    run([sys.executable, root/'tools/verify_icu_data_source.py', '--extract', source], 'source-verification.log')
    run([source/'icu4c/source/configure', '--disable-shared', '--enable-static',
         '--disable-extras', '--disable-icuio', '--disable-tests', '--disable-samples',
         '--with-data-packaging=archive'], 'configure.log', host)
    run([tools['make'], '-j'+str(o.jobs)], 'host-build.log', host)
    package = host/'data/out/icudt79l.dat'
    data = package.read_bytes()
    assert len(data) > 16*1024*1024 and data[2:4] == b'\xda\x27'
    assert data[8:12] == b'\0\0\2\0' and data[12:16] == b'CmnD'
    expectation_path = root/'docs/dependencies/icu-generated-data.json'
    expectation = json.loads(expectation_path.read_text()) if expectation_path.exists() else None
    if expectation:
        assert expectation['source_archive_sha256']==source_manifest['archive']['sha256']
        assert len(data)==expectation['package_bytes'] and sha(package)==expectation['package_sha256']
    run([host/'bin/icupkg', '-l', package], 'package-entries.txt')
    entries = (work/'package-entries.txt').read_text().splitlines()
    assert len(entries) == len(set(entries)) and len(entries) > 4000
    for required in ('cnvalias.icu', 'root.res', 'en.res', 'de.res', 'fr.res',
                     'supplementalData.res', 'zoneinfo64.res', 'coll/root.res',
                     'nfkc.nrm', 'uts46.nrm', 'plurals.res', 'res_index.res'):
        assert required in entries, required
    native = work/'native'; native.mkdir()
    run([host/'bin/genccode', '-a', 'gcc', '-e', 'icudt79', '-d', native, package], 'genccode.log')
    assembly = list(native.glob('*.S')); assert len(assembly) == 1
    obj = native/'icudata.o'
    run([tools['native_cc'], '--target=aarch64-none-elf', '-c', assembly[0], '-o', obj], 'native-build.log')
    run([tools['objcopy'], '--dump-section', '.rodata='+str(native/'payload.dat'), obj], 'native-payload.log')
    assert (native/'payload.dat').read_bytes() == data
    run([tools['nm'], '--defined-only', obj], 'native-symbols.log')
    assert (work/'native-symbols.log').read_text().split() == ['0000000000000000', 'R', 'icudt79_dat']
    # ELF64, little-endian, relocatable AArch64, with no host object substitution.
    elf = obj.read_bytes(); assert elf[:6] == b'\x7fELF\x02\x01' and elf[16:20] == b'\x01\0\xb7\0'
    out.parent.mkdir(parents=True, exist_ok=True)
    stage = pathlib.Path(tempfile.mkdtemp(prefix=out.name+'.stage-', dir=out.parent))
    archive = stage/'libicudata.a'
    run([tools['native_ar'], 'rcsD', archive, obj], 'native-archive.log')
    shutil.copyfile(package, stage/'icudt79l.dat')
    shutil.copyfile(work/'package-entries.txt', stage/'package-entries.txt')
    resources = {}
    for name in entries:
        resource = host/'data/out/build/icudt79l'/name
        resources[name] = {'bytes': resource.stat().st_size, 'sha256': sha(resource)}
    (stage/'resources.json').write_text(json.dumps(resources, indent=2, sort_keys=True)+'\n')
    if expectation:
        assert sha(stage/'resources.json')==expectation['resources_sha256']
    versions = {name: subprocess.check_output([tool, '--version'], text=True).splitlines()[0]
                for name, tool in tools.items()}
    report = {'schema': 1, 'status': 'DATA_GENERATED_NATIVE_ARCHIVE_BUILT',
              'stage3_complete': False, 'native_data_executed': False, 'engine_built': False,
              'source_commit': 'ddc6b66926bfac6036144ca9902992a0d6d50af9',
              'source_archive_sha256': json.loads((root/'docs/dependencies/icu-data-source.json').read_text())['archive']['sha256'],
              'resource_count': len(entries), 'package_bytes': len(data), 'package_sha256': sha(package),
              'native_object_sha256': sha(obj), 'native_payload_matches_package': True,
              'work_directory': str(work), 'commands': commands, 'tool_versions': versions,
              'logs': {f.name: sha(f) for f in work.glob('*.log')},
              'configure_warnings': [line for line in (work/'configure.log').read_text().splitlines() if 'Warning:' in line],
              'limits': ['host executables are data generators only',
                         'upstream default dataset,not full ICU conformance or every optional neural break model',
                         'full32MiB data execution needs a separately qualified larger native runtime profile',
                         'no JavaScriptCore/WebKit/browser or live WiFi success inferred'],
              'files': {f.name: sha(f) for f in stage.iterdir()}}
    (stage/'build-report.json').write_text(json.dumps(report, indent=2)+'\n')
    backup = out.with_name(out.name+'.previous-'+str(os.getpid())); assert not backup.exists()
    if out.exists(): out.rename(backup)
    try: stage.rename(out)
    except BaseException:
        if backup.exists(): backup.rename(out)
        raise
    if backup.exists(): shutil.rmtree(backup)
    print(json.dumps({k: report[k] for k in ('status', 'resource_count', 'package_bytes', 'package_sha256')}), flush=True)
except BaseException as error:
    (work/'failure.json').write_text(json.dumps({'error': str(error), 'commands': commands}, indent=2)+'\n')
    raise
