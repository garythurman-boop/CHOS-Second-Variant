#!/usr/bin/env python3
"""Compile original WebCore image adapters for CHOS; never link or execute them."""
import argparse
import collections
import concurrent.futures
import hashlib
import json
import pathlib
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMIT = '32717fabcb9dcad9aa00a54ef1f23e41ee64e849'
UNITS = {
    'frame': 'platform/image-decoders/ScalableImageDecoderFrame.cpp',
    'png': 'platform/image-decoders/png/PNGImageDecoder.cpp',
    'cairo': 'platform/image-decoders/cairo/ImageBackingStoreCairo.cpp',
    'jpeg': 'platform/image-decoders/jpeg/JPEGImageDecoder.cpp',
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['out', 'webkit-source', 'native-sdk', 'engine-kit',
                 'graphics-prefix', 'font-prefix', 'png-prefix', 'jpeg-prefix']:
        parser.add_argument('--' + name, type=pathlib.Path, required=True)
    parser.add_argument('--cc', default='clang++')
    parser.add_argument('--jobs', type=int, choices=range(1, 5), default=2)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source = args.webkit_source.resolve()
    sdk = args.native_sdk.resolve()
    kit = args.engine_kit.resolve()
    report = {'status': 'VALIDATING', 'linked': False, 'executed': False,
              'stage3_complete': False, 'units': [], 'upstream_commit': COMMIT,
              'probe_script_sha256': sha(pathlib.Path(__file__)),
              'limits': [
                  'Compiler-only component probe;not a complete WebCore build,link or execution',
                  'Explicit Cairo/FreeType/native profile flags;matching generated engine configuration and ABI remain unqualified',
                  'No fabricated successful interfaces;export aliases point to unchanged original headers',
                  'Original WebCore decoder policies and complete browser/security/platform acceptance remain unqualified']}

    def save():
        (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')

    def verify_sdk():
        subprocess.run(['python3', str(ROOT / 'tools/verify_native_sdk.py'), str(sdk)], check=True)

    def verify_prefix(prefix):
        build = prefix.parent / 'build.json'
        metadata = json.loads(build.read_text())
        assert metadata['host_reference'] is False, 'Host prefix refused: ' + str(prefix)
        assert metadata['status'] == 'LINKED_NOT_EXECUTED', 'Unfinished dependency build'
        assert metadata['installed_files_sha256'], 'Missing installed inventory'
        for name, expected in metadata['installed_files_sha256'].items():
            path = prefix / name
            assert path.resolve().is_relative_to(prefix), 'Dependency escapes prefix'
            assert sha(path) == expected, 'Dependency changed: ' + str(path)
        return {'prefix': str(prefix), 'build_report_sha256': sha(build),
                'installed_files_verified': len(metadata['installed_files_sha256'])}

    try:
        actual_commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source, text=True).strip()
        assert actual_commit == COMMIT, 'Wrong upstream commit'
        subprocess.run(['git', 'diff', '--quiet', 'HEAD', '--', 'Source/WebCore',
                        'Source/JavaScriptCore'], cwd=source, check=True)
        verify_sdk()
        report['native_SDK_manifest_sha256'] = sha(sdk / 'chos-sdk.json')
        manifest_path = kit / 'MANIFEST.json'
        manifest = json.loads(manifest_path.read_text())
        verified = 0
        for name, entry in manifest['files'].items():
            if name.startswith(('source/webkit/Source/WTF/', 'source/webkit/Source/bmalloc/')):
                path = kit / name
                assert path.resolve().is_relative_to(kit), 'Engine source escapes kit'
                assert path.stat().st_size == entry['bytes'] and sha(path) == entry['sha256'], name
                verified += 1
        assert verified == 2116, 'Unexpected native WTF/bmalloc inventory'
        report.update(engine_kit=str(kit), engine_manifest_sha256=sha(manifest_path),
                      native_WTF_bmalloc_files_verified=verified)
        prefixes = {name: getattr(args, name + '_prefix').resolve()
                    for name in ['graphics', 'font', 'png', 'jpeg']}
        report['target_dependencies'] = {name: verify_prefix(path) for name, path in prefixes.items()}

        # Equivalent to exported framework headers, without editing source or
        # synthesizing declarations. Ambiguous basenames are not guessed.
        includes = out / 'include'
        forwarders = {}
        for project in ['WebCore', 'JavaScriptCore']:
            target = includes / project
            target.mkdir(parents=True)
            tracked = subprocess.check_output(['git', 'ls-tree', '-r', '--name-only', COMMIT,
                                               '--', 'Source/' + project], cwd=source, text=True)
            groups = collections.defaultdict(list)
            for name in tracked.splitlines():
                if name.endswith('.h') and (source / name).is_file():
                    groups[pathlib.Path(name).name].append(source / name)
            for name, paths in groups.items():
                if len(paths) == 1:
                    (target / name).symlink_to(paths[0])
            forwarders[project] = {'unique_headers': sum(len(paths) == 1 for paths in groups.values()),
                                   'ambiguous_names': {name: list(map(str, paths))
                                                       for name, paths in groups.items() if len(paths) > 1}}
        exports = out / 'header-forwarders.json'
        exports.write_text(json.dumps(forwarders, indent=2) + '\n')
        report['header_forwarders_sha256'] = sha(exports)
        wc = source / 'Source/WebCore'
        paths = [includes, wc, kit / 'source/webkit/Source/WTF',
                 kit / 'source/webkit/Source/bmalloc', wc / 'PAL']
        paths.extend(wc / name for name in [
            'platform', 'platform/text', 'platform/graphics', 'platform/graphics/cairo',
            'platform/graphics/transforms', 'platform/graphics/filters',
            'platform/graphics/freetype', 'platform/graphics/harfbuzz',
            'platform/graphics/opentype', 'platform/image-decoders',
            'platform/image-decoders/png', 'platform/image-decoders/jpeg',
            'platform/network', 'rendering', 'rendering/style', 'page', 'dom', 'loader'])
        paths.extend([prefixes['graphics'] / 'include/cairo',
                      prefixes['graphics'] / 'include/pixman-1',
                      prefixes['font'] / 'include/freetype2',
                      prefixes['font'] / 'include/harfbuzz',
                      prefixes['png'] / 'include', prefixes['jpeg'] / 'include'])
        assert all(path.is_dir() for path in paths), 'Missing actual target include directory'
        compiler = shutil.which(args.cc)
        assert compiler, 'Compiler unavailable'
        report['compiler'] = subprocess.check_output([compiler, '--version'], text=True)
        base = [compiler, '--target=aarch64-none-elf', '--sysroot=' + str(sdk),
                '-std=c++23', '-O2', '-DNDEBUG', '-fhosted', '-fno-builtin',
                '-fno-stack-protector', '-fno-pic', '-fno-pie', '-femulated-tls',
                '-ffunction-sections', '-fdata-sections', '-mcpu=cortex-a72',
                '-mstrict-align', '-fno-exceptions', '-frtti', '-nostdinc++',
                '-isystem', str(sdk / 'include/c++/v1'), '-isystem', str(sdk / 'include'),
                '-D__CHOS__=1', '-DUSE_SYSTEM_MALLOC=1', '-DUSE_CAIRO=1',
                '-DUSE_FREETYPE=1', '-DUSE_HARFBUZZ=1', '-DBUILDING_JSCONLY__=1',
                '-DBUILDING_WebCore=1', '-DSTATICALLY_LINKED_WITH_JavaScriptCore=1',
                '-DSTATICALLY_LINKED_WITH_WTF=1', '-DENABLE_JIT=0',
                '-DENABLE_DFG_JIT=0', '-DENABLE_FTL_JIT=0', '-DENABLE_WEBASSEMBLY=0']
        for path in paths:
            base.extend(['-I', str(path)])

        def compile_unit(item):
            name, relative = item
            unit = wc / relative
            original = subprocess.check_output(['git', 'show', COMMIT + ':Source/WebCore/' + relative], cwd=source)
            assert unit.read_bytes() == original, 'Modified original unit'
            obj = out / (name + '.o')
            argv = base + ['-MD', '-MF', str(out / (name + '.d')), '-c', str(unit), '-o', str(obj)]
            command = out / (name + '-command.json')
            command.write_text(json.dumps(argv, indent=2) + '\n')
            log = out / (name + '.log')
            with log.open('wb') as handle:
                code = subprocess.run(argv, stdout=handle, stderr=subprocess.STDOUT).returncode
            result = {'unit': relative, 'returncode': code, 'command': argv,
                      'source_sha256': sha(unit), 'command_sha256': sha(command), 'log_sha256': sha(log)}
            if not code:
                header = subprocess.check_output(['llvm-readelf', '-h', str(obj)], text=True)
                assert 'AArch64' in header, 'Host object refused'
                undefined = subprocess.check_output(['llvm-nm', '-u', '-C', str(obj)], text=True)
                (out / (name + '-elf-header.txt')).write_text(header)
                (out / (name + '-undefined.txt')).write_text(undefined)
                result.update(object_path=str(obj), object_sha256=sha(obj), object_bytes=obj.stat().st_size,
                              ELF_header=header, unresolved_symbols=undefined.splitlines(),
                              dependency_file_sha256=sha(out / (name + '.d')))
            return result

        report['status'] = 'COMPILING'
        save()
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            report['units'] = list(pool.map(compile_unit, UNITS.items()))
        verify_sdk()
        for path in prefixes.values():
            verify_prefix(path)
        report['status'] = ('PASS_NATIVE_COMPILATION_ONLY' if all(unit['returncode'] == 0
                            for unit in report['units']) else 'NATIVE_COMPILATION_FAILED')
        save()
        print(json.dumps({'status': report['status'], 'units': report['units']}, indent=2))
        return 0 if report['status'] == 'PASS_NATIVE_COMPILATION_ONLY' else 1
    except Exception as error:
        report.update(status='PROBE_FAILED', error=str(error))
        save()
        raise


if __name__ == '__main__':
    raise SystemExit(main())
