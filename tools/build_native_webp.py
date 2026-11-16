#!/usr/bin/env python3
"""Build original static WebP memory codecs/demux with genuine native probes.

Host fixture generation is separate from target compilation and linking. A
library build or executable link is never reported as native execution.
"""
import argparse
import contextlib
import hashlib
import json
import pathlib
import shutil
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=pathlib.Path, required=True)
    parser.add_argument('--native-sdk', type=pathlib.Path)
    parser.add_argument('--host-build', type=pathlib.Path,
                        help='Verified original host WebP build used only to generate fixture bytes')
    parser.add_argument('--host', action='store_true')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    options = parser.parse_args()
    if options.host == bool(options.native_sdk):
        parser.error('Select exactly one of --host or --native-sdk')
    if options.sanitize and not options.host:
        parser.error('--sanitize requires --host')
    if options.host and options.host_build:
        parser.error('--host-build applies only to native builds')
    verifier = ['python3', str(ROOT / 'tools/verify_native_webp_sources.py')]
    subprocess.run(verifier, check=True)
    sdk = options.native_sdk.resolve() if options.native_sdk else None
    if sdk:
        subprocess.run(['python3', str(ROOT / 'tools/verify_native_sdk.py'), str(sdk)], check=True)
    out = options.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    prefix = out / 'prefix'
    source = out / 'source'
    shutil.copytree(ROOT / 'third_party/libwebp', source)
    fixture = ROOT / 'tests/native_webp'
    fixture_files = ['main.c', 'CMakeLists.txt', 'generate_cases.c', 'generate_cases.py']
    fixture_present = all((fixture / name).is_file() for name in fixture_files)
    inputs = ['tools/build_native_webp.py', 'tools/verify_native_webp_sources.py',
              'ports/webp/CMakeLists.txt']
    if fixture_present:
        inputs += ['tests/native_webp/' + name for name in fixture_files]
    report = {
        'status': 'BUILDING', 'host_reference': options.host,
        'host_sanitizers': options.sanitize, 'executed': False, 'stage3_complete': False,
        'source_manifest_sha256': sha(ROOT / 'docs/dependencies/native-webp.json'),
        'SDK_manifest_sha256': sha(sdk / 'chos-sdk.json') if sdk else None,
        'sources_sha256': {name: sha(ROOT / name) for name in inputs}, 'commands': [],
        'fixture_sources_available': fixture_present,
        'limits': ['Original WebP memory encoder/decoder,SharpYUV and demux dependency;not WebCore/page/browser integration',
                   'No command-line image/file transport,GL,codec plugin or mux target in this profile',
                   'Genuine upstream compiler/Threads probes retained;native AArch64 NEON required',
                   'Selected codec fixtures do not establish full upstream conformance,production security or physical-platform qualification']}
    started = time.monotonic()

    def save():
        report['seconds'] = time.monotonic() - started
        (out / 'build.json').write_text(json.dumps(report, indent=2) + '\n')

    def run(name, argv):
        report['commands'].append(argv)
        save()
        with (out / (name + '.log')).open('wb') as log:
            code = subprocess.run(argv, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT).returncode
        if code:
            report.update(status='BUILD_FAILED', failed_step=name, returncode=code)
            save()
            raise SystemExit(code)

    @contextlib.contextmanager
    def record_failures():
        try:
            yield
        except BaseException as error:
            if report['status'] != 'BUILD_FAILED':
                report.update(status='BUILD_FAILED', error=str(error),
                              returncode=error.code if isinstance(error, SystemExit) else 1)
                save()
            raise

    with record_failures():
        cmake = shutil.which(options.cmake) or options.cmake
        ninja = shutil.which(options.ninja) or options.ninja
        base = [cmake, '-G', 'Ninja', '-DCMAKE_MAKE_PROGRAM=' + str(ninja),
                '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_INSTALL_PREFIX=' + str(prefix),
                '-DCMAKE_INSTALL_LIBDIR=lib']
        if sdk:
            base += ['-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT / 'ports/native/chos-toolchain.cmake'),
                     '-DCHOS_SDK_ROOT=' + str(sdk), '-DCHOS_APPLICATION_PROFILE=engine']
        if options.sanitize:
            base += ['-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer',
                     '-DCMAKE_C_FLAGS_RELEASE=-O1 -DNDEBUG',
                     '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
        run('webp-setup', [*base, '-S', str(ROOT / 'ports/webp'), '-B', str(out / 'webp'),
                           '-DWEBP_SOURCE=' + str(source)])
        config = out / 'webp/upstream/src/webp/config.h'
        config_text = config.read_text()
        report['generated_configuration'] = {'path': str(config), 'sha256': sha(config),
                                             'upstream_NEON': '#define WEBP_HAVE_NEON\n' in config_text,
                                             'upstream_threads': '#define WEBP_USE_THREAD 1' in config_text}
        run('webp-build', [cmake, '--build', str(out / 'webp'), '-j4'])
        run('webp-install', [cmake, '--install', str(out / 'webp')])
        archives = {}
        for name in ['libwebp.a', 'libwebpdecoder.a', 'libwebpdemux.a', 'libsharpyuv.a']:
            archive = prefix / 'lib' / name
            members = subprocess.check_output(['llvm-ar', 't', str(archive)], text=True).splitlines()
            if sdk:
                headers = subprocess.check_output(['llvm-readelf', '-h', str(archive)], text=True)
                assert headers.count('Machine:                           AArch64') == len(members)
            archives[name] = {'bytes': archive.stat().st_size, 'members': len(members), 'sha256': sha(archive)}
        report['archives'] = archives
        report['installed_files_sha256'] = {str(path.relative_to(prefix)): sha(path)
                                            for path in sorted(prefix.rglob('*')) if path.is_file()}
        report['status'] = 'LIBRARIES_BUILT_NOT_LINKED'
        save()
        if fixture_present:
            if sdk and not options.host_build:
                raise RuntimeError('Native fixture generation requires --host-build with a verified original host build')
            host_prefix = prefix
            host_sanitizers = options.sanitize
            if sdk:
                host = options.host_build.resolve()
                metadata_path = host / 'build.json'
                metadata = json.loads(metadata_path.read_text())
                assert metadata['host_reference'] is True
                assert metadata['source_manifest_sha256'] == report['source_manifest_sha256']
                assert metadata['installed_files_sha256']
                host_prefix = host / 'prefix'
                for name, expected in metadata['installed_files_sha256'].items():
                    path = host_prefix / name
                    assert path.resolve().is_relative_to(host_prefix) and sha(path) == expected, str(path)
                report['fixture_host_build_report_sha256'] = sha(metadata_path)
                host_sanitizers = metadata['host_sanitizers']
            compiler = shutil.which('cc')
            assert compiler, 'Actual host C compiler required for fixture generation'
            generator = out / 'generate_cases'
            argv = [compiler, '-std=c11', '-O1', '-I' + str(host_prefix / 'include'),
                    str(fixture / 'generate_cases.c'), str(host_prefix / 'lib/libwebp.a'),
                    str(host_prefix / 'lib/libsharpyuv.a'), '-lm', '-pthread', '-o', str(generator)]
            if host_sanitizers:
                argv += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            run('generator-build', argv)
            cases = out / 'cases'
            run('generate-cases', ['python3', str(fixture / 'generate_cases.py'),
                                   '--out', str(cases), '--encoder', str(generator)])
            report['generated_cases_sha256'] = {str(path.relative_to(cases)): sha(path)
                                               for path in sorted(cases.rglob('*')) if path.is_file()}
            run('application-setup', [*base, '-S', str(fixture), '-B', str(out / 'application'),
                                      '-DWEBP_PREFIX=' + str(prefix), '-DWEBP_CASES=' + str(cases)])
            run('application-build', [cmake, '--build', str(out / 'application'), '-j4'])
            executable = out / 'application/native_webp'
            if sdk:
                assert not subprocess.check_output(['llvm-nm', '-u', str(executable)], text=True).strip()
                assert 'AArch64' in subprocess.check_output(['llvm-readelf', '-h', str(executable)], text=True)
            report.update(status='LINKED_NOT_EXECUTED', application={'path': str(executable),
                          'bytes': executable.stat().st_size, 'sha256': sha(executable)})
        subprocess.run(verifier, check=True)
        for name, expected in report['sources_sha256'].items():
            assert sha(ROOT / name) == expected, 'Source changed during build: ' + name
        assert sha(ROOT / 'docs/dependencies/native-webp.json') == report['source_manifest_sha256']
        if sdk:
            assert sha(sdk / 'chos-sdk.json') == report['SDK_manifest_sha256']
            subprocess.run(['python3', str(ROOT / 'tools/verify_native_sdk.py'), str(sdk)], check=True)
        report['returncode'] = 0
        save()
        print(json.dumps({key: report[key] for key in ['status', 'archives']}, indent=2))
        return 0


if __name__ == '__main__':
    raise SystemExit(main())
