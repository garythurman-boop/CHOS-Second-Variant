#!/usr/bin/env python3
"""Build selected original WebCore local-buffer ownership, not a page or IPC."""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
PORT = pathlib.Path(__file__).resolve().parent
PIN = '32717fabcb9dcad9aa00a54ef1f23e41ee64e849'
KIT_SHA = 'c5f2aabcf448415a9567cb7f0d2d1ad11ace0c2ef44731f1c8ff0a810b1b4757'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ['out', 'native-sdk', 'engine-kit', 'webkit-source']:
        p.add_argument('--' + name, type=pathlib.Path, required=True)
    a = p.parse_args()
    out = a.out.resolve(); out.mkdir(parents=True, exist_ok=False)
    sdk = a.native_sdk.resolve(); kit = a.engine_kit.resolve(); up = a.webkit_source.resolve()
    report = {'status': 'VALIDATING', 'stage3_complete': False, 'executed': False,
              'scope': 'Original SharedBuffer/SharedMemory plus genuine CHOS in-process ReadWrite backing',
              'limits': ['ReadOnly,CopyOnWrite and cross-process memory refused',
                         'Selected existing WTF component configuration; full page generated ABI remains unqualified'],
              'commands': [], 'builder_sha256': sha(pathlib.Path(__file__))}
    started = time.monotonic()

    def save():
        report['seconds'] = time.monotonic() - started
        (out / 'build.json').write_text(json.dumps(report, indent=2) + '\n')

    def run(name, cmd):
        report['commands'].append(cmd); save()
        with (out / (name + '.log')).open('wb') as log:
            subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=True)

    try:
        run('verify-sdk', ['python3', str(ROOT / 'tools/verify_native_sdk.py'), str(sdk)])
        run('verify-images', ['python3', str(ROOT / 'tools/verify_webcore_image_sources.py')])
        assert sha(kit / 'MANIFEST.json') == KIT_SHA
        manifest = json.loads((kit / 'MANIFEST.json').read_text())
        selected = {n: e for n, e in manifest['files'].items()
                    if n.startswith(('source/webkit/Source/WTF/', 'source/webkit/Source/bmalloc/'))
                    or n in ['engine/lib/libWTF.a', 'engine/lib/libbmalloc.a']}
        assert len(selected) == 2118
        for name, entry in selected.items():
            item = kit / name
            assert item.resolve().is_relative_to(kit) and item.stat().st_size == entry['bytes'] and sha(item) == entry['sha256'], name
        report.update(engine_manifest_sha256=KIT_SHA, engine_files_verified=len(selected),
                      SDK_manifest_sha256=sha(sdk / 'chos-sdk.json'),
                      component_profile_sha256=sha(ROOT / 'ports/webcore-images/source-list.json'),
                      component_sources_manifest_sha256=sha(ROOT / 'docs/dependencies/native-webcore-images.json'),
                      compiler_version=subprocess.check_output(['clang++', '--version'], text=True))
        assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=up, text=True).strip() == PIN
        source = out / 'source'
        shutil.copytree(ROOT / 'third_party/webcore-images', source)
        original = {}
        for name in ['Source/WebCore/platform/SharedMemory.h', 'Source/WebCore/platform/SharedMemory.cpp']:
            path = up / name; data = path.read_bytes()
            blob = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
            expected = subprocess.check_output(['git', 'rev-parse', PIN + ':' + name], cwd=up, text=True).strip()
            assert blob == expected and path.resolve().is_relative_to(up)
            (source / name).write_bytes(data)
            original[name] = {'git_blob': blob, 'sha256': sha(path), 'bytes': len(data)}
        report['additional_original_sources'] = original
        patch = PORT / 'shared-memory.patch'
        run('patch', ['patch', '--batch', '--fuzz=0', '--no-backup-if-mismatch',
                      '-d', str(source), '-p1', '-i', str(patch)])
        own = ['SharedMemoryCHOS.cpp', 'SharedMemoryCHOSBacking.h', 'tests/shared-memory.cpp', 'shared-memory.patch']
        snapshot = {n: sha(PORT / n) for n in own}
        report['port_sources_sha256'] = snapshot
        report['patched_source_files_sha256'] = {str(f.relative_to(source)): sha(f) for f in source.rglob('*') if f.is_file()}
        include = out / 'include'
        for project in ['WebCore', 'JavaScriptCore']:
            forward = include / project; forward.mkdir(parents=True)
            headers = {}
            for path in (source / 'Source' / project).rglob('*.h'):
                headers.setdefault(path.name, []).append(path)
            for name, paths in headers.items():
                if len(paths) == 1:
                    (forward / name).symlink_to(paths[0])
        wc = source / 'Source/WebCore'
        includes = [PORT, include, wc, wc / 'platform', wc / 'PAL',
                    kit / 'source/webkit/Source/WTF', kit / 'source/webkit/Source/bmalloc']
        flags = ['clang++', '--target=aarch64-none-elf', '--sysroot=' + str(sdk),
                 '-std=c++23', '-O2', '-DNDEBUG', '-fhosted', '-fno-builtin', '-fno-stack-protector',
                 '-fno-pic', '-fno-pie', '-femulated-tls', '-ffunction-sections', '-fdata-sections',
                 '-mcpu=cortex-a72', '-mstrict-align', '-fno-exceptions', '-frtti', '-nostdinc++',
                 '-isystem', str(sdk / 'include/c++/v1'), '-isystem', str(sdk / 'include'), '-D__CHOS__=1']
        defines = json.loads((ROOT / 'ports/webcore-images/source-list.json').read_text())['defines']
        del defines['CHOS_WEBCORE_IMAGE_COMPONENT']
        flags += ['-D' + n + '=' + str(v) for n, v in defines.items()]
        for path in includes:
            flags += ['-I', str(path)]
        units = {'shared': wc / 'platform/SharedBuffer.cpp', 'memory': wc / 'platform/SharedMemory.cpp',
                 'chos': PORT / 'SharedMemoryCHOS.cpp', 'main': PORT / 'tests/shared-memory.cpp'}
        objects = []
        for name, path in units.items():
            obj = out / (name + '.o'); objects.append(obj)
            run('compile-' + name, flags + ['-c', str(path), '-o', str(obj)])
        libraries = [kit / 'engine/lib/libWTF.a', kit / 'engine/lib/libbmalloc.a']
        libraries += [sdk / 'lib' / ('libchos-' + n + '.a') for n in ['c++', 'm', 'quad', 'complex', 'wide', 'stdio', 'format', 'floatparse', 'c']]
        libraries += [sdk / 'lib' / n for n in ['libicuuc.a', 'libicui18n.a', 'libicudata.a']]
        app = out / 'native_webcore_local_memory.elf'
        link = ['clang++', '--target=aarch64-none-elf', '-nostdlib', '-static', '-fuse-ld=lld',
                '-Wl,-T,' + str(sdk / 'lib/app-engine.ld'), '-Wl,--gc-sections', '-Wl,-z,max-page-size=4096',
                '-Wl,--error-limit=0', '-Wl,--strip-debug']
        link += [str(sdk / 'lib' / n) for n in ['crt0.o', 'crtinit.o', 'chos-engine-abi.o']]
        run('link', link + list(map(str, objects)) + ['-Wl,--start-group'] + list(map(str, libraries)) + ['-Wl,--end-group', '-o', str(app)])
        assert not subprocess.check_output(['llvm-nm', '-u', str(app)], text=True).strip()
        assert 'AArch64' in subprocess.check_output(['llvm-readelf', '-h', str(app)], text=True)
        assert snapshot == {n: sha(PORT / n) for n in own}, 'Port changed during build'
        assert report['patched_source_files_sha256'] == {str(f.relative_to(source)): sha(f) for f in source.rglob('*') if f.is_file()}, 'Copied source changed during build'
        for name, entry in selected.items():
            assert sha(kit / name) == entry['sha256'], 'Published source/archive changed during build'
        assert sha(sdk / 'chos-sdk.json') == report['SDK_manifest_sha256']
        assert sha(ROOT / 'ports/webcore-images/source-list.json') == report['component_profile_sha256']
        assert sha(ROOT / 'docs/dependencies/native-webcore-images.json') == report['component_sources_manifest_sha256']
        assert sha(pathlib.Path(__file__)) == report['builder_sha256']
        report.update(status='LINKED_NOT_EXECUTED', application_sha256=sha(app), application_bytes=app.stat().st_size,
                      libraries_sha256={str(n): sha(n) for n in libraries}, objects_sha256={n.name: sha(n) for n in objects})
        save(); print(json.dumps({'status': report['status'], 'application': str(app)}, indent=2))
    except Exception as error:
        report.update(status='BUILD_FAILED', error=str(error)); save(); raise


if __name__ == '__main__':
    main()
