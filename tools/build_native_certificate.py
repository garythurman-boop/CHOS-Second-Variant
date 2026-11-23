#!/usr/bin/env python3
"""Build genuine DER inspection and optional full-config WebCore certificate values.

No execution/trust/browser qualification is implied by successful linkage.
The optional native value target uses actual full-port compiler options and WTF.
"""
import argparse
import hashlib
import json
import pathlib
import re
import shlex
import shutil
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
PORT = ROOT / 'ports/webcore-network-types'
FIXTURE = ROOT / 'tests/native_certificate_parser'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=pathlib.Path, required=True)
    p.add_argument('--native-sdk', type=pathlib.Path)
    p.add_argument('--webcore-build', type=pathlib.Path)
    p.add_argument('--cases', type=pathlib.Path, help='Reuse exact generated vector files')
    p.add_argument('--generate-cases', action='store_true', help='Generate new disposable vectors instead of pinned public inputs')
    p.add_argument('--host', action='store_true')
    p.add_argument('--sanitize', action='store_true')
    o = p.parse_args()
    if o.host == bool(o.native_sdk):
        p.error('Select --host or --native-sdk')
    if o.sanitize and not o.host:
        p.error('--sanitize requires --host')
    if o.webcore_build and o.host:
        p.error('The actual C++ value fixture is native-only')
    if o.cases and o.generate_cases:
        p.error('Choose --cases or --generate-cases')
    out = o.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    report = {'schema': 1, 'status': 'VALIDATING', 'executed': False,
              'stage3_complete': False, 'host_reference': o.host,
              'host_sanitizers': o.sanitize, 'commands': []}
    started = time.monotonic()

    def save():
        report['seconds'] = time.monotonic() - started
        (out / 'build.json').write_text(json.dumps(report, indent=2) + '\n')

    def run(name, command, cwd=ROOT):
        report['commands'].append({'name': name, 'argv': command, 'cwd': str(cwd)})
        save()
        with (out / (name + '.log')).open('wb') as log:
            code = subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT).returncode
        if code:
            raise RuntimeError(name + ' failed with status ' + str(code))

    inputs = [pathlib.Path(__file__).resolve(), PORT / 'CertificateInfo.h',
        PORT / 'CertificateInfoCHOS.cpp', PORT / 'CertificateBackend.h',
        PORT / 'CertificateBackend.c', PORT / 'CertificateParser.cmake',
        PORT / 'chos_certificate_config.h', PORT / 'generate_certificate_namespace.py',
        ROOT / 'tools/verify_certificate_fixtures.py', FIXTURE / 'fixtures-manifest.json',
        *[f for f in FIXTURE.iterdir() if f.suffix in ('.c', '.cpp', '.h', '.py', '.txt')]]
    snapshot = {str(f.relative_to(ROOT)): sha(f) for f in inputs}
    report['sources_sha256'] = snapshot
    manifest = ROOT / 'docs/dependencies/mbedtls.json'
    tls_config = ROOT / 'userland/tls/include/chos_tls_config.h'
    report['mbedtls_manifest_sha256'] = sha(manifest)
    report['separate_TLS_config_sha256'] = sha(tls_config)
    assert sha(tls_config) == json.loads(manifest.read_text())['native_tls_profile']['configuration_sha256']
    try:
        run('verify-mbedtls', ['python3', str(ROOT / 'tools/verify_mbedtls.py')])
        cases = out / 'cases'
        if o.generate_cases:
            run('generate-cases', ['python3', str(FIXTURE / 'generate_cases.py'), '--out', str(cases)])
        else:
            if not o.cases:
                run('verify-pinned-cases', ['python3', str(ROOT / 'tools/verify_certificate_fixtures.py')])
            shutil.copytree((o.cases or FIXTURE / 'fixtures').resolve(), cases)
        report['case_files_sha256'] = {str(f.relative_to(cases)): sha(f) for f in sorted(cases.iterdir()) if f.is_file()}
        assert 'vectors.json' in report['case_files_sha256'] and 'certificate-cases.h' in report['case_files_sha256']
        vectors = json.loads((cases / 'vectors.json').read_text())
        expected_cases = {'vectors.json', 'certificate-cases.h'}
        for row in vectors['cases']:
            assert re.fullmatch('[A-Za-z0-9_]+', row['name']), 'Invalid case filename'
            expected_cases.add(row['name'] + '.der')
            assert sha(cases / (row['name'] + '.der')) == row['sha256']
        assert not any(f.is_symlink() or not f.is_file() for f in cases.iterdir())
        assert set(report['case_files_sha256']) == expected_cases, 'Unexpected vector files'
        sdk = o.native_sdk.resolve() if o.native_sdk else None
        if sdk:
            run('verify-sdk', ['python3', str(ROOT / 'tools/verify_native_sdk.py'), str(sdk)])
            report['SDK_manifest_sha256'] = sha(sdk / 'chos-sdk.json')
        cmake = shutil.which('cmake')
        ninja = shutil.which('ninja')
        assert cmake and ninja
        build = out / 'parser'
        command = [cmake, '-G', 'Ninja', '-DCMAKE_MAKE_PROGRAM=' + ninja,
            '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
            '-S', str(FIXTURE), '-B', str(build), '-DCHOS_ROOT=' + str(ROOT),
            '-DCERTIFICATE_CASES=' + str(cases)]
        if sdk:
            command += ['-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT / 'ports/native/chos-toolchain.cmake'),
                        '-DCHOS_SDK_ROOT=' + str(sdk), '-DCHOS_APPLICATION_PROFILE=engine']
        if o.sanitize:
            command += ['-DCMAKE_C_COMPILER=gcc', '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer',
                '-DCMAKE_C_FLAGS_RELEASE=-O1 -DNDEBUG', '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
        run('parser-setup', command)
        run('parser-build', [cmake, '--build', str(build), '-j4'])
        report['parser_compile_database_sha256'] = sha(build / 'compile_commands.json')
        report['private_namespace_header_sha256'] = sha(build / 'chos-certificate-namespace.h')
        report['compiled_parser_sources_sha256'] = {
            row['file']: sha(pathlib.Path(row['file']))
            for row in json.loads((build / 'compile_commands.json').read_text())}
        archive = build / 'libchos_certificate_parser.a'
        app = build / 'native_certificate_parser'
        symbols = subprocess.check_output(['llvm-nm', '-g', '--defined-only', str(archive)], text=True)
        assert not any(row.split()[-1].startswith(('mbedtls_', 'psa_')) for row in symbols.splitlines() if len(row.split()) >= 3)
        report['private_symbols_sha256'] = hashlib.sha256(symbols.encode()).hexdigest()
        report['parser_archive'] = {'path': str(archive), 'sha256': sha(archive), 'bytes': archive.stat().st_size}
        report['parser_application'] = {'path': str(app), 'sha256': sha(app), 'bytes': app.stat().st_size}
        dependencies = {}
        if o.webcore_build:
            full = o.webcore_build.resolve()
            database = full / 'compile_commands.json'
            rows = json.loads(database.read_text())
            row = next(r for r in rows if r['file'].endswith('/CertificateInfoCHOS.cpp'))
            args = shlex.split(row['command'])
            old_sdk = next(a.split('=', 1)[1] for a in args if a.startswith('--sysroot='))
            flags = []
            i = 1
            while i < len(args):
                a = args[i]
                if a in ('-o', '-c', '-MF', '-MT', '-MQ'):
                    i += 2
                    continue
                if a in ('-MD', '-MMD'):
                    i += 1
                    continue
                if a == '-Xclang' and i + 3 < len(args) and args[i + 1] in ('-include-pch', '-include') and args[i + 2] == '-Xclang':
                    i += 4
                    continue
                flags.append(a.replace(old_sdk, str(sdk)))
                i += 1
            # Identical full generated feature configuration; only its SDK
            # prefix and the source/output/PCH transport arguments differ.
            flags += ['-I' + str(PORT), '-I' + str(cases)]
            for f in [full / 'cmakeconfig.h', full / 'lib/libWTF.a',
                full / 'Source/bmalloc/CMakeFiles/bmalloc.dir/libpas/src/libpas/pas_process.c.obj']:
                dependencies[str(f)] = sha(f)
            report['full_port'] = {'compile_database_path': str(database),
                'compile_database_sha256_at_start': sha(database),
                'selected_compile_record': row, 'WTF_compilation_SDK_root': old_sdk,
                'consuming_SDK_root': str(sdk), 'PCH_transport_omitted': True}
            objects = out / 'objects'
            objects.mkdir()
            for name, source in [('certificate', PORT / 'CertificateInfoCHOS.cpp'), ('main', FIXTURE / 'value-main.cpp')]:
                run('compile-' + name, [args[0], *flags, '-MD', '-MF', str(objects / (name + '.d')),
                    '-c', str(source), '-o', str(objects / (name + '.o'))], pathlib.Path(row['directory']))
                text = (objects / (name + '.d')).read_text().replace('\\\n', ' ')
                for item in shlex.split(text.split(':', 1)[1]):
                    path = pathlib.Path(item)
                    if not path.is_absolute():
                        path = pathlib.Path(row['directory']) / path
                    if path.is_file():
                        dependencies[str(path.resolve())] = sha(path)
            libraries = [full / 'lib/libWTF.a']
            libraries += [sdk / 'lib' / ('libchos-' + n + '.a') for n in ['c++', 'm', 'quad', 'complex', 'wide', 'stdio', 'format', 'floatparse', 'c']]
            value_app = out / 'native_certificate_value'
            link = [args[0], '--target=aarch64-none-elf', '-nostdlib', '-static', '-fuse-ld=lld',
                '-Wl,-T,' + str(sdk / 'lib/app-engine.ld'), '-Wl,--gc-sections', '-Wl,-z,max-page-size=4096',
                '-Wl,--error-limit=0', '-Wl,--strip-debug', '-Wl,-Map,' + str(out / 'application.map')]
            link += [str(sdk / 'lib' / n) for n in ['crt0.o', 'crtinit.o', 'chos-engine-abi.o']]
            link += [str(objects / 'main.o'), str(objects / 'certificate.o'), str(archive),
                str(full / 'Source/bmalloc/CMakeFiles/bmalloc.dir/libpas/src/libpas/pas_process.c.obj'),
                '-Wl,--start-group', *map(str, libraries), '-Wl,--end-group', '-o', str(value_app)]
            run('link-value', link)
            assert not subprocess.check_output(['llvm-nm', '-u', str(value_app)], text=True).strip()
            report['value_application'] = {'path': str(value_app), 'sha256': sha(value_app), 'bytes': value_app.stat().st_size}
            for f in libraries:
                dependencies[str(f)] = sha(f)
            # A frozen full-port forwarding tree may outlive a platform edit.
            # Its class declaration must match the actual CHOS implementation.
            for name in dependencies:
                path = pathlib.Path(name)
                if path.name in ('CertificateInfo.h', 'CertificateBackend.h'):
                    assert sha(path) == sha(PORT / path.name), 'Stale certificate declaration: ' + name
            report['value_objects_sha256'] = {str(f.name): sha(f) for f in objects.glob('*.o')}
        if sdk:
            assert not subprocess.check_output(['llvm-nm', '-u', str(app)], text=True).strip()
            headers = subprocess.check_output(['llvm-readelf', '-h', str(archive)], text=True)
            assert headers.count('Machine:                           AArch64') == len(subprocess.check_output(['llvm-ar', 't', str(archive)], text=True).splitlines())
            run('reverify-sdk', ['python3', str(ROOT / 'tools/verify_native_sdk.py'), str(sdk)])
        for name, wanted in snapshot.items():
            assert sha(ROOT / name) == wanted, 'Source changed during build: ' + name
        for name, wanted in dependencies.items():
            assert sha(pathlib.Path(name)) == wanted, 'Dependency changed during build: ' + name
        for name, wanted in report['compiled_parser_sources_sha256'].items():
            assert sha(pathlib.Path(name)) == wanted, 'Parser source changed during build: ' + name
        assert sha(manifest) == report['mbedtls_manifest_sha256']
        assert sha(tls_config) == report['separate_TLS_config_sha256']
        report['dependency_files_sha256'] = dependencies
        run('reverify-mbedtls', ['python3', str(ROOT / 'tools/verify_mbedtls.py')])
        report['status'] = 'LINKED_NOT_EXECUTED'
        save()
        print(json.dumps({k: report[k] for k in ('status', 'parser_application', 'value_application') if k in report}, indent=2))
    except Exception as error:
        report.update(status='BUILD_FAILED', error=str(error))
        save()
        raise


if __name__ == '__main__':
    main()
