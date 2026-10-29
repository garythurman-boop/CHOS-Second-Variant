#!/usr/bin/env python3
"""Import original libc++ dependencies after validating the complete selection.

The default mode follows private includes conservatively. --native-header-closure
asks the real native preprocessor which headers are active. --native-source-closure
also follows quoted implementation includes, so disabled locale,
foreign-OS and experimental backends do not create fictitious dependencies.
"""
import argparse
import hashlib
import json
import pathlib
import re
import shlex
import subprocess
import sys
import tarfile
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--archive', type=pathlib.Path, required=True)
p.add_argument('--header', action='append', default=[])
p.add_argument('--source-file', action='append', default=[])
p.add_argument('--native-header-closure', action='store_true')
p.add_argument('--native-source-closure', action='store_true')
p.add_argument('--compiler', default='clang++')
p.add_argument('--check-only', action='store_true')
o = p.parse_args()
if not o.header and not o.source_file:
    p.error('select at least one original header/source')
if o.native_header_closure and (not o.header or o.source_file):
    p.error('native-header-closure requires headers only')
if o.native_source_closure and (not o.source_file or o.header):
    p.error('native-source-closure requires sources only')
native_closure = o.native_header_closure or o.native_source_closure
manifest = root/'docs/dependencies/libcxx-native.json'
m = json.loads(manifest.read_text())
assert hashlib.sha256(o.archive.read_bytes()).hexdigest() == m['release_archive_sha256']['libcxx.tar.xz']
subprocess.run(['python3', str(root/'tools/verify_libcxx.py')], check=True)
base = root/'third_party/libcxx'
generated = {'__config_site', '__assertion_handler', '__external_threading'}
selected = ['include/'+name for name in o.header] + ['src/'+name for name in o.source_file]
for name in selected:
    assert not pathlib.PurePosixPath(name).is_absolute() and '..' not in pathlib.PurePosixPath(name).parts
    assert '\n' not in name and '\r' not in name and '>' not in name
planned = {}
closure_profile = None
with tarfile.open(o.archive) as archive:
    members = {member.name.removeprefix('libcxx-'+m['version']+'.src/'): member
               for member in archive if member.isfile()}
    for name in selected:
        assert name in members, 'missing original header/source: '+name
    if native_closure:
        config = root/'build/userland/libcxx/include'
        math = root/'build/userland/libm/include'
        for name in ('__config_site', '__assertion_handler'):
            assert (config/name).is_file(), 'generate native C++ configuration first: '+name
        assert (math/'math.h').is_file(), 'generate native math header first'
        with tempfile.TemporaryDirectory(prefix='chos-libcxx-import-') as work:
            work = pathlib.Path(work)
            headers = work/'include'
            sources = work/'src'
            # Only private and explicitly selected public headers are exposed.
            # Do not incidentally shadow native C wrappers with a complete sysroot.
            for name, member in members.items():
                if not name.startswith('include/'):
                    continue
                relative = pathlib.PurePosixPath(name).relative_to('include')
                assert '..' not in relative.parts and not relative.is_absolute()
                if str(relative) in generated or not (str(relative).startswith('__') or name in selected):
                    continue
                target = headers/str(relative)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.extractfile(member).read())
            if o.native_source_closure:
                # Quoted implementation dependencies live under src/include.
                # Expose original source files for preprocessing only; import
                # just the selected units and dependencies the compiler uses.
                for name, member in members.items():
                    if not name.startswith('src/'):
                        continue
                    relative = pathlib.PurePosixPath(name).relative_to('src')
                    assert '..' not in relative.parts and not relative.is_absolute()
                    target = sources/str(relative)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(archive.extractfile(member).read())
            command = [o.compiler, '--target=aarch64-none-elf', '-std=c++23', '-D__CHOS__=1',
                       '-fno-exceptions', '-fno-rtti', '-femulated-tls', '-nostdinc++', '-nostdlibinc']
            for directory in (config, root/'userland/libcxx/include', base/'include', headers,
                              root/'userland/libc/include', math):
                command += ['-isystem', str(directory)]
            command += ['-I'+str(root/'include'), '-I'+str(root/'userland/libchos/include'),
                        '-I'+str(sources), '-M', '-MT', 'chos_import', '-x', 'c++']
            if o.native_source_closure:
                units = [str(sources/name) for name in o.source_file if name.endswith('.cpp')]
                assert units, 'native source closure requires a C++ implementation unit'
                scans = [(command+[unit], None) for unit in units]
            else:
                source = ''.join('#include <'+name+'>\n' for name in o.header)
                scans = [(command+['-'], source)]
            active = set(selected)
            for scan, source in scans:
                completed = subprocess.run(scan, input=source, text=True, capture_output=True)
                if completed.returncode:
                    # Preserve the actual missing-dependency diagnostic; discovery
                    # is still read-only and cannot leave a partly imported closure.
                    sys.stderr.write(completed.stderr)
                    completed.check_returncode()
                dependencies = shlex.split(completed.stdout.replace('\\\n', ' '))
                assert dependencies.pop(0) == 'chos_import:'
                for dependency in dependencies:
                    path = pathlib.Path(dependency)
                    if path.is_relative_to(headers):
                        active.add('include/'+str(path.relative_to(headers)))
                    elif path.is_relative_to(base/'include'):
                        # Existing originals are part of the same active closure.
                        # Retain them so repeated scans have identical provenance
                        # after newly imported dependencies move into the tree.
                        active.add('include/'+str(path.relative_to(base/'include')))
                    elif path.is_relative_to(sources):
                        active.add('src/'+str(path.relative_to(sources)))
            closure_profile = {
                'target': 'aarch64-none-elf', 'language': 'c++23',
                'configuration_sha256': hashlib.sha256((config/'__config_site').read_bytes()).hexdigest(),
                'selected_headers': o.header, 'active_original_files': sorted(active),
                'policy': 'real native preprocessing; original private and explicitly selected public headers only'
            }
            if o.native_source_closure:
                closure_profile['selected_sources'] = o.source_file
            pending = sorted(active)
    else:
        pending = selected.copy()
    seen = set()
    while pending:
        name = pending.pop()
        if name in seen:
            continue
        seen.add(name)
        assert name in members, 'missing original header/source: '+name
        data = archive.extractfile(members[name]).read()
        target = base/name
        if target.exists():
            assert target.read_bytes() == data, 'modified original file: '+name
        else:
            planned[name] = data
        if native_closure:
            continue
        for include in re.findall(rb'^\s*#\s*include\s*<([^>]+)>', data, re.M):
            dependency = include.decode()
            path = 'include/'+dependency
            if dependency in generated:
                continue
            if dependency.startswith('__') or path in m['files']:
                assert path in members, 'missing private header: '+path
                pending.append(path)

# All discovery, preprocessing and identity checks precede source-tree mutation.
for name, data in planned.items():
    m['files'][name] = hashlib.sha256(data).hexdigest()
    if name.startswith('src/') and name.endswith('.cpp'):
        m['sources'].append(name)
if closure_profile:
    records = m.setdefault('native_source_imports' if o.native_source_closure else 'native_header_imports', [])
    if closure_profile not in records:
        records.append(closure_profile)
if not o.check_only:
    created = []
    try:
        for name, data in planned.items():
            target = base/name
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open('xb') as output:
                created.append(target)
                output.write(data)
        with tempfile.NamedTemporaryFile(mode='w', dir=manifest.parent, delete=False) as temporary:
            temporary.write(json.dumps(m, indent=2)+'\n')
            replacement = pathlib.Path(temporary.name)
        replacement.replace(manifest)
    except BaseException:
        for target in reversed(created):
            target.unlink()
        raise
print(('Would import' if o.check_only else 'Imported'), len(planned), 'original libc++ files:',
      ', '.join(sorted(planned)))
