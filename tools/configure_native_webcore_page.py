#!/usr/bin/env python3
"""Prepare and configure the explicit native CHOS full-WebCore port prototype.

A successful configure is not compilation, linking, page rendering or a browser.
Required target libraries and real compiler checks are never substituted.
"""
import argparse
import hashlib
import json
import pathlib
import os
import shlex
import shutil
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMIT = '32717fabcb9dcad9aa00a54ef1f23e41ee64e849'
KIT_MANIFEST = 'c5f2aabcf448415a9567cb7f0d2d1ad11ace0c2ef44731f1c8ff0a810b1b4757'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_standard_header_overlay(directory):
    """Accept exact maintained C++ headers, never C include_next wrappers."""
    assert not directory.is_symlink()
    directory = directory.resolve()
    inventory_path = ROOT / 'docs/dependencies/libcxx-native.json'
    inventory = json.loads(inventory_path.read_text())
    assert inventory['version'] == '19.1.7'
    assert inventory['commit'] == 'cd708029e0b2869e80abe31ddb175f7c35361f90'
    assert directory.is_dir() and not directory.is_symlink()
    headers = {}
    for path in sorted(directory.rglob('*')):
        assert not path.is_symlink() and path.resolve().is_relative_to(directory), path
        if not path.is_file():
            continue
        relative = path.relative_to(directory).as_posix()
        # Importing a top-level C compatibility wrapper ahead of the SDK could
        # intercept libc++ include_next. Internal maintained .h files are valid.
        assert not relative.endswith('.h') or relative.startswith('__'), relative
        expected = inventory['files'].get('include/' + relative)
        assert expected and sha(path) == expected, 'Unverified standard header: ' + relative
        assert sha(ROOT / 'third_party/libcxx/include' / relative) == expected, relative
        headers[relative] = expected
    assert headers, 'The standard-header overlay must not be empty'
    return headers, inventory_path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ['out', 'webkit-source', 'native-sdk', 'engine-kit']:
        p.add_argument('--' + name, type=pathlib.Path, required=True)
    p.add_argument('--prefix', type=pathlib.Path, action='append', default=[],
                   help='Completed verified target dependency prefix; may be repeated')
    p.add_argument('--cmake', default='cmake')
    p.add_argument('--ninja', default='ninja')
    p.add_argument('--ruby', default='ruby3.3')
    p.add_argument('--ruby-library-dir', type=pathlib.Path, action='append', default=[],
                   help='Installed host Ruby standard-library or extension directory; may be repeated')
    p.add_argument('--gperf', type=pathlib.Path)
    p.add_argument('--standard-header-overlay', type=pathlib.Path,
                   help='Explicit verified LLVM19 C++ header overlay for an older immutable SDK')
    o = p.parse_args()
    out = o.out.resolve(); out.mkdir(parents=True, exist_ok=False)
    upstream = o.webkit_source.resolve(); sdk = o.native_sdk.resolve(); kit = o.engine_kit.resolve()
    report = {'status': 'VALIDATING', 'compiled': False, 'linked': False, 'executed': False,
              'stage3_complete': False, 'upstream_commit': COMMIT, 'commands': [],
              'tool_sha256': sha(pathlib.Path(__file__)),
              'limits': ['This report covers configuration only',
                         'Page linking and rendering require separate runtime evidence',
                         'Required dependencies and real executable probes remain mandatory']}
    started = time.monotonic()

    def save():
        report['seconds'] = time.monotonic() - started
        (out / 'configure.json').write_text(json.dumps(report, indent=2) + '\n')

    def verify():
        subprocess.run(['python3', str(ROOT / 'tools/verify_native_sdk.py'), str(sdk)], check=True)
        assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=upstream, text=True).strip() == COMMIT
        subprocess.run(['git', 'diff', '--quiet', 'HEAD', '--', 'Source/WebCore'], cwd=upstream, check=True)
        manifest_path = kit / 'MANIFEST.json'
        assert sha(manifest_path) == KIT_MANIFEST
        manifest = json.loads(manifest_path.read_text())
        files = {n: v for n, v in manifest['files'].items() if n.startswith('source/webkit/')}
        for name, entry in files.items():
            path = kit / name
            assert path.resolve().is_relative_to(kit) and path.stat().st_size == entry['bytes'] and sha(path) == entry['sha256'], name
        return files

    def stage_webcore(destination):
        # A clean tracked diff does not exclude untracked files in an external
        # checkout. Read the pinned tree and check each copied Git blob instead
        # of importing generated or otherwise unreviewed files with copytree.
        listing = subprocess.check_output(
            ['git', 'ls-tree', '-rz', '--full-tree', COMMIT, '--', 'Source/WebCore'], cwd=upstream)
        inventory = {}
        for record in listing.split(b'\0'):
            if not record:
                continue
            metadata, encoded_name = record.split(b'\t', 1)
            mode, kind, expected_blob = metadata.decode().split()
            name = encoded_name.decode()
            assert mode in ['100644', '100755'] and kind == 'blob', name
            source = upstream / name
            assert source.resolve().is_relative_to(upstream) and not source.is_symlink(), name
            data = source.read_bytes()
            blob = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
            assert blob == expected_blob, 'Upstream WebCore blob differs from pin: ' + name
            target = destination / pathlib.Path(name).relative_to('Source/WebCore')
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            target.chmod(0o755 if mode == '100755' else 0o644)
            inventory[name] = {'git_blob': blob, 'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)}
        assert inventory, 'Pinned WebCore source tree is empty'
        manifest = out / 'upstream-webcore-files.json'
        manifest.write_text(json.dumps(inventory, indent=2) + '\n')
        report.update(upstream_WebCore_files_verified=len(inventory),
                      upstream_WebCore_inventory_sha256=sha(manifest))

    try:
        source_files = verify()
        report.update(native_SDK_manifest_sha256=sha(sdk / 'chos-sdk.json'),
                      engine_manifest_sha256=KIT_MANIFEST, published_source_files_verified=len(source_files))
        prefixes = [path.resolve() for path in o.prefix]
        report['target_prefixes'] = []
        for prefix in prefixes:
            metadata_path = prefix.parent / 'build.json'; metadata = json.loads(metadata_path.read_text())
            assert metadata.get('host_reference') is False, 'Host prefix refused: ' + str(prefix)
            assert metadata.get('status') == 'LINKED_NOT_EXECUTED', 'Incomplete target prefix: ' + str(prefix)
            inventory = metadata['installed_files_sha256']; assert inventory
            for name, expected in inventory.items():
                path = prefix / name
                assert path.resolve().is_relative_to(prefix) and sha(path) == expected, str(path)
            report['target_prefixes'].append({'path': str(prefix), 'build_report_sha256': sha(metadata_path),
                                              'compilation_SDK_manifest_sha256': metadata.get('SDK_manifest_sha256'),
                                              'files_verified': len(inventory)})
        staged = out / 'source'; staged.mkdir()
        kit_source = kit / 'source/webkit'
        # Copy only published, verified source files. No generated binary/cache
        # source directories or unverified external kit additions are inherited.
        for name in source_files:
            relative = pathlib.Path(name).relative_to('source/webkit')
            destination = staged / relative; destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(kit / name, destination)
        wc = staged / 'Source/WebCore'
        if wc.exists():
            raise RuntimeError('Unexpected WebCore in the selected published JSC source kit')
        # Full upstream tree is required for real DOM/CSS generated sources.
        # The earlier 95-file image subset cannot stand in for it.
        stage_webcore(wc)
        # Explicit, recorded changes apply only to a fresh verified source copy.
        patches = [ROOT / 'ports/webcore-page' / name for name in ['shared-memory.patch', 'software-only.patch', 'native-prefix.patch', 'screen-properties.patch', 'accessibility-owned.patch', 'network-calendar.patch', 'dns-queue.patch']]
        patches += [ROOT / 'ports/webcore-network-types' / name for name in
                    ['resource-request-isolation.patch', 'resource-response-initialization.patch', 'form-data-isolation.patch']]
        patches += [ROOT / 'ports/webcore-crypto-keys/key-types.patch']
        report['source_patches_sha256'] = {patch.name: sha(patch) for patch in patches}
        for patch in patches:
            patch_argv = ['patch', '--batch', '--fuzz=0', '--no-backup-if-mismatch',
                          '-d', str(staged), '-p1', '-i', str(patch)]
            report['commands'].append(patch_argv)
            with (out / ('patch-' + patch.stem + '.log')).open('wb') as log:
                subprocess.run(patch_argv, stdout=log, stderr=subprocess.STDOUT, check=True)
        common = staged / 'Source/cmake/WebKitCommon.cmake'
        text = common.read_text(); marker = '    set(ALL_PORTS\n'
        assert text.count(marker) == 1 and '        CHOS\n' not in text
        common.write_text(text.replace(marker, marker + '        CHOS\n'))
        port = ROOT / 'ports/webcore-page'
        destinations = {'OptionsCHOS.cmake': staged / 'Source/cmake/OptionsCHOS.cmake',
                        'WebCorePlatformCHOS.cmake': wc / 'PlatformCHOS.cmake',
                        'PALPlatformCHOS.cmake': wc / 'PAL/pal/PlatformCHOS.cmake',
                        'WTFPlatformCHOS.cmake': staged / 'Source/WTF/wtf/PlatformCHOS.cmake',
                        'BmallocPlatformCHOS.cmake': staged / 'Source/bmalloc/PlatformCHOS.cmake',
                        'JavaScriptCorePlatformCHOS.cmake': staged / 'Source/JavaScriptCore/PlatformCHOS.cmake',
                        'SharedMemoryCHOS.cpp': wc / 'platform/chos/SharedMemoryCHOS.cpp',
                        'SharedMemoryCHOSBacking.h': wc / 'platform/chos/SharedMemoryCHOSBacking.h'}
        report['port_files_sha256'] = {}
        for name, destination in destinations.items():
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(port / name, destination)
            report['port_files_sha256'][name] = sha(port / name)
        fixture = ROOT / 'tests/native_webcore_page/main.cpp'
        shutil.copyfile(fixture, wc / 'platform/chos/LocalPageDiagnostic.cpp')
        report['local_page_fixture_sha256'] = sha(fixture)
        for name, destination in [('main.cpp', 'NetworkValueDiagnostic.cpp'), ('Target.cmake', 'network-values/CMakeLists.txt')]:
            source = ROOT / 'tests/native_webcore_network_values' / name
            target = wc / 'platform/chos' / destination
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
            report.setdefault('network_fixture_files_sha256', {})[name] = sha(source)
        external_sources = {}
        for directory in ['ports/webcore-network-types', 'ports/webcore-platform', 'ports/webcore-crypto', 'ports/webcore-crypto-keys', 'ports/publicsuffix']:
            for path in sorted((ROOT / directory).rglob('*')):
                if path.is_file() and path.suffix in ['.h', '.c', '.cpp', '.cmake', '.py', '.json', '.patch'] and '__pycache__' not in path.parts:
                    assert not path.is_symlink() and path.resolve().is_relative_to(ROOT)
                    external_sources[str(path.relative_to(ROOT))] = sha(path)
        external_sources['docs/dependencies/native-publicsuffix.json'] = sha(ROOT / 'docs/dependencies/native-publicsuffix.json')
        report['external_port_files_sha256'] = external_sources
        # Target roots only; host executable generators remain normal host tools.
        toolchain = out / 'chos-page-toolchain.cmake'
        roots = [sdk, *prefixes]
        toolchain.write_text('include(' + json.dumps(str(ROOT / 'ports/native/chos-toolchain.cmake')) + ')\n'
                             + 'set(CMAKE_FIND_ROOT_PATH ' + ' '.join(json.dumps(str(path)) for path in roots) + ')\n'
                             + 'set(CMAKE_PREFIX_PATH ' + ' '.join(json.dumps(str(path)) for path in prefixes) + ')\n'
                             # Installed dependency .pc files contain absolute
                             # verified prefixes outside the SDK. Applying the
                             # SDK sysroot to these paths would corrupt them.
                             + 'set(ENV{PKG_CONFIG_SYSROOT_DIR} "")\n'
                             + 'set(ENV{PKG_CONFIG_LIBDIR} ' + json.dumps(':'.join(
                                 str(path / directory) for path in roots
                                 for directory in ['lib/pkgconfig', 'share/pkgconfig'])) + ')\n'
                             + 'set(ENV{PKG_CONFIG_PATH} "")\n')
        cmake = shutil.which(o.cmake); ninja = shutil.which(o.ninja); ruby = shutil.which(o.ruby)
        if not all([cmake, ninja, ruby]):
            raise RuntimeError('Actual host CMake,Ninja and Ruby interpreters are required')
        ruby_dirs = [path.resolve() for path in o.ruby_library_dir]
        assert all(path.is_dir() for path in ruby_dirs), 'Missing installed host Ruby library directory'
        ruby_env = os.environ.copy()
        if ruby_dirs:
            ruby_env['RUBYLIB'] = ':'.join(map(str, ruby_dirs))
        else:
            ruby_env.pop('RUBYLIB', None)
        ruby_probe = [ruby, '--disable-gems', '-e',
                      'require "date"; require "fileutils"; require "erb"; require "yaml"; '
                      'require "json"; require "pathname"; puts "Ruby generator modules: PASS #{RUBY_VERSION}"']
        report['commands'].append(ruby_probe)
        with (out / 'ruby-modules.log').open('wb') as log:
            subprocess.run(ruby_probe, env=ruby_env, stdout=log, stderr=subprocess.STDOUT, check=True)
        # The relocated host interpreter does not automatically discover its
        # installed standard library. A real wrapper preserves the tested host
        # module path in later Ninja invocations; no generated source is faked.
        ruby_wrapper = out / 'host-ruby'
        ruby_wrapper.write_text('#!/bin/sh\n'
            + ('export RUBYLIB=' + shlex.quote(':'.join(map(str, ruby_dirs))) + '\n' if ruby_dirs else 'unset RUBYLIB\n')
            + 'exec ' + shlex.quote(ruby) + ' --disable-gems "$@"\n')
        ruby_wrapper.chmod(0o755)
        report['host_ruby'] = {'executable': ruby, 'sha256': sha(pathlib.Path(ruby)),
                              'library_directories': list(map(str, ruby_dirs)),
                              'module_probe_sha256': sha(out / 'ruby-modules.log'),
                              'wrapper_sha256': sha(ruby_wrapper)}
        argv = [cmake, '-G', 'Ninja', '-S', str(staged), '-B', str(out / 'build'),
                '-DCMAKE_MAKE_PROGRAM=' + ninja, '-DCMAKE_TOOLCHAIN_FILE=' + str(toolchain),
                '-DCHOS_SDK_ROOT=' + str(sdk), '-DCHOS_APPLICATION_PROFILE=engine',
                '-DCHOS_WTF_PLATFORM_DIR=' + str(ROOT / 'ports/webkit/chos'),
                '-DCHOS_PAL_DIGEST_DIR=' + str(ROOT / 'ports/webcore-crypto'),
                '-DCHOS_WEBCORE_RESOURCE_TYPES_DIR=' + str(ROOT / 'ports/webcore-network-types'),
                '-DCHOS_WEBCORE_CRYPTO_KEYS_DIR=' + str(ROOT / 'ports/webcore-crypto-keys'),
                '-DCHOS_WEBCORE_PLATFORM_DIR=' + str(ROOT / 'ports/webcore-platform'),
                '-DPORT=CHOS', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DDEVELOPER_MODE=OFF',
                '-DENABLE_WEBCORE=ON', '-DENABLE_WEBKIT=OFF', '-DENABLE_WEBKIT_LEGACY=OFF',
                '-DENABLE_TOOLS=OFF', '-DRuby_EXECUTABLE=' + str(ruby_wrapper),
                '-DICU_INCLUDE_DIR=' + str(sdk / 'include')]
        for component, library in [('UC', 'libicuuc.a'), ('I18N', 'libicui18n.a'), ('DATA', 'libicudata.a')]:
            assert (sdk / 'lib' / library).is_file()
            argv.append('-DICU_' + component + '_LIBRARY_RELEASE=' + str(sdk / 'lib' / library))
        overlay_headers = None
        if o.standard_header_overlay:
            overlay = o.standard_header_overlay.resolve()
            overlay_headers, overlay_inventory = verify_standard_header_overlay(overlay)
            report['native_standard_header_overlay'] = {'path': str(overlay), 'files_sha256': overlay_headers,
                'inventory_path': str(overlay_inventory), 'inventory_sha256': sha(overlay_inventory),
                'scope': 'Exact maintained C++ headers only; no C wrappers, SDK mutation or host include directories'}
            argv.append('-DCHOS_CXX_STANDARD_HEADER_OVERLAY=' + str(overlay))
            argv.append('-DCHOS_CXX_STANDARD_HEADERS_INVENTORY=' + str(overlay_inventory))
        if o.gperf:
            assert o.gperf.resolve().is_file()
            argv.append('-DGPERF_EXECUTABLE=' + str(o.gperf.resolve()))
        report['commands'].append(argv); report['status'] = 'CONFIGURING'; save()
        with (out / 'configure.log').open('wb') as log:
            code = subprocess.run(argv, stdout=log, stderr=subprocess.STDOUT).returncode
        report.update(returncode=code, status='CONFIGURED_NOT_COMPILED' if not code else 'CONFIGURE_FAILED',
                      configure_log_sha256=sha(out / 'configure.log'))
        verify()
        for name, expected in report['port_files_sha256'].items():
            assert sha(port / name) == expected, 'Port changed during configure'
        assert sha(fixture) == report['local_page_fixture_sha256']
        for name, expected in report['network_fixture_files_sha256'].items():
            assert sha(ROOT / 'tests/native_webcore_network_values' / name) == expected, name
        for name, expected in external_sources.items():
            assert sha(ROOT / name) == expected, 'External port changed during configure: ' + name
        assert all(sha(patch) == report['source_patches_sha256'][patch.name] for patch in patches), 'Patch changed during configure'
        assert sha(sdk / 'chos-sdk.json') == report['native_SDK_manifest_sha256']
        if overlay_headers:
            checked, inventory = verify_standard_header_overlay(overlay)
            assert checked == overlay_headers
            assert sha(inventory) == report['native_standard_header_overlay']['inventory_sha256']
        save()
        print(json.dumps({'status': report['status'], 'returncode': code, 'log': str(out / 'configure.log')}, indent=2))
        return code
    except Exception as error:
        report.update(status='PREPARATION_FAILED', error=str(error)); save(); raise


if __name__ == '__main__':
    raise SystemExit(main())
