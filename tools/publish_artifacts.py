#!/usr/bin/env python3
"""Stage versioned GitHub-downloadable artifacts; commit/push remains explicit.

Run --iteration once per completed implementation iteration, or --publish-now for
an explicitly requested publication. Source pushes retain their separate cadence.
"""
import argparse
import gzip
import hashlib
import json
import pathlib
import shutil
import subprocess
import tempfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
INTERVAL = 2
MAX_BYTES = 95 * 1024 * 1024  # Leave room below GitHub's 100 MiB blob limit.


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def publication_due(state, source, force=False):
    if not force and state.get('last_counted_source_commit') == source:
        return False, state.get('iterations_since_artifacts', 0), True
    count = state.get('iterations_since_artifacts', 0) + (not force)
    if not isinstance(count, int) or not 0 <= count <= INTERVAL:
        raise ValueError('Invalid artifact iteration counter')
    return force or count >= INTERVAL, count, False


def compressed(source, destination):
    with source.open('rb') as incoming, destination.open('wb') as outgoing:
        with gzip.GzipFile(filename='', mode='wb', fileobj=outgoing, mtime=0) as archive:
            shutil.copyfileobj(incoming, archive)


def verify(directory):
    manifest = json.loads((directory / 'manifest.json').read_text())
    if not manifest.get('assets'):
        raise ValueError('Artifact manifest has no assets')
    for entry in manifest['assets']:
        path = directory / entry['name']
        if pathlib.Path(entry['name']).name != entry['name'] or not path.is_file():
            raise ValueError('Missing or invalid asset: ' + entry['name'])
        if path.stat().st_size != entry['bytes'] or digest(path) != entry['sha256']:
            raise ValueError('Artifact manifest mismatch: ' + entry['name'])
    verified = set()
    for line in (directory / 'SHA256SUMS').read_text().splitlines():
        expected, name = line.split('  ', 1)
        path = directory / name
        if pathlib.Path(name).name != name or name in verified or digest(path) != expected:
            raise ValueError('Artifact checksum mismatch: ' + name)
        verified.add(name)
    if verified != {path.name for path in directory.iterdir() if path.is_file() and path.name != 'SHA256SUMS'}:
        raise ValueError('SHA256SUMS does not cover every staged file')
    return manifest


def contains_bytes(path, needle):
    """Find a contiguous boot file without loading a whole prepared disk."""
    tail = b''
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(8 * 1024 * 1024), b''):
            window = tail + chunk
            if needle in window:
                return True
            tail = window[-(len(needle) - 1):] if len(needle) > 1 else b''
    return False


def stage(root, build, source, handoff=None):
    short = source[:7]
    directory = root / 'artifacts' / short
    iso = build / 'chos-0.30.0.iso'
    direct = build / 'chos.elf'
    uefi = build / 'uefi-kernel' / 'chos.elf'
    loader = build / 'uefi' / 'BOOTAA64.EFI'
    disk = build / 'chos-0.30.0-installed.img'
    for kernel in (direct, uefi):
        data = kernel.read_bytes()
        if ('[BUILD] ' + source[:12] + '\n\0').encode() not in data or (source[:12] + '-dirty').encode() in data:
            raise ValueError('Kernel is not a clean build of ' + source + ': ' + str(kernel))
    iso_bytes = iso.read_bytes()
    if loader.read_bytes() not in iso_bytes or uefi.read_bytes() not in iso_bytes:
        raise ValueError('ISO does not contain the staged EFI loader and UEFI kernel')
    if not contains_bytes(disk, loader.read_bytes()) or not contains_bytes(disk, uefi.read_bytes()):
        raise ValueError('Prepared disk does not contain the staged EFI loader and UEFI kernel')
    if directory.exists():
        manifest = verify(directory)
        if manifest['source_commit'] != source or manifest['inputs']['iso_sha256'] != digest(iso):
            raise ValueError('Refusing to replace an existing artifact publication')
        return directory
    directory.parent.mkdir(parents=True, exist_ok=True)
    temporary = pathlib.Path(tempfile.mkdtemp(prefix='.stage-', dir=directory.parent))
    try:
        assets = []

        def add(original, name, pack=False):
            target = temporary / name
            if pack:
                compressed(original, target)
            else:
                shutil.copyfile(original, target)
            if target.stat().st_size >= MAX_BYTES:
                raise ValueError('Asset exceeds the GitHub repository size budget: ' + name)
            entry = {'name': name, 'sha256': digest(target), 'bytes': target.stat().st_size}
            if pack:
                entry['uncompressed_sha256'] = digest(original)
                entry['uncompressed_bytes'] = original.stat().st_size
            assets.append(entry)

        add(iso, 'CHOS-Fusion-' + short + '.iso')
        add(disk, 'CHOS-' + short + '-prepared.img.gz', True)
        add(direct, 'CHOS-' + short + '-kernel-direct.elf.gz', True)
        add(uefi, 'CHOS-' + short + '-kernel-uefi.elf.gz', True)
        add(loader, 'BOOTAA64.EFI')
        source_zip = temporary / ('CHOS-' + short + '-source.zip')
        subprocess.run(['git', 'archive', '--worktree-attributes', '--format=zip',
                        '--prefix=CHOS-Second-Variant/', '--output=' + str(source_zip), source],
                       cwd=root, check=True)
        if source_zip.stat().st_size >= MAX_BYTES:
            raise ValueError('Source archive exceeds the repository size budget')
        assets.append({'name': source_zip.name, 'sha256': digest(source_zip), 'bytes': source_zip.stat().st_size})
        if handoff:
            with zipfile.ZipFile(handoff) as bundle:
                notes = [name for name in bundle.namelist() if name.endswith('/README.txt') and name.count('/') == 1]
                if len(notes) != 1 or source.encode() not in bundle.read(notes[0]):
                    raise ValueError('Handoff bundle does not identify this source commit')
            add(handoff, 'CHOS-Fusion-' + short + '-Handoff.zip')
        manifest = {'schema': 1, 'source_commit': source, 'tag': 'build-' + short,
                    'inputs': {'iso_sha256': digest(iso)}, 'assets': assets}
        (temporary / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        links = '\n'.join('- [' + item['name'] + '](' + item['name'] + '?raw=1)' for item in assets)
        (temporary / 'README.md').write_text(
            '# CHOS build ' + short + '\n\nSource commit: `' + source + '`.\n\n' + links + '\n\n'
            'Verify downloads with [SHA256SUMS](SHA256SUMS). Decompress `.gz` files before use.\n'
            'The prepared image is a distribution image, not a disk modified by acceptance tests.\n\n'
            'QEMU validation and target limitations are recorded in the source reports and optional handoff bundle. '
            'Actual Fusion validation of this build remains pending. Native NVMe is admitted through explicit read-only discovery or validated installed-boot discovery; destructive installation requires confirmation.\n')
        checksums = [digest(path) + '  ' + path.name for path in sorted(temporary.iterdir())]
        (temporary / 'SHA256SUMS').write_text('\n'.join(checksums) + '\n')
        verify(temporary)
        temporary.rename(directory)
        return directory
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)


def write_download_index(root, latest):
    short = latest['source_commit'][:7]
    names = [('Bootable ARM64 ISO', 'CHOS-Fusion-' + short + '.iso'),
             ('Prepared disk image (gzip)', 'CHOS-' + short + '-prepared.img.gz'),
             ('Source archive', 'CHOS-' + short + '-source.zip'),
             ('Direct kernel (gzip)', 'CHOS-' + short + '-kernel-direct.elf.gz'),
             ('UEFI kernel (gzip)', 'CHOS-' + short + '-kernel-uefi.elf.gz'),
             ('EFI loader', 'BOOTAA64.EFI'), ('Checksums', 'SHA256SUMS'),
             ('Manifest', 'manifest.json')]
    bundle = 'CHOS-Fusion-' + short + '-Handoff.zip'
    if (root / latest['directory'] / bundle).exists():
        names.insert(1, ('Complete source/build/test handoff', bundle))
    links = '\n'.join('- [' + title + '](' + short + '/' + name + '?raw=1)' for title, name in names)
    (root / 'artifacts' / 'README.md').write_text(
        '# Current CHOS build downloads\n\nTested source: `' + latest['source_commit'] + '`.\n'
        'Publication tag: `' + latest['tag'] + '`.\n\n' + links + '\n\n'
        'Source is pushed each iteration. Binary downloads are published for selected verified '
        'milestones that are useful for user testing. Stage 3 completion remains separately gated.\n\n'
        'See [publication instructions](../docs/ARTIFACT_PUBLICATION.md) and '
        '[installation guide](../docs/INSTALL_GUIDE.md) and [validation report](../docs/STAGE2_VALIDATION.md). '
        'Actual Fusion validation of this build remains pending. NVMe discovery is read-only; '
        'destructive installation requires explicit target selection and confirmation.\n')


def run(root, build, source, force=False, handoff=None):
    state_path = root / 'artifacts' / 'publication.json'
    state = json.loads(state_path.read_text()) if state_path.exists() else {}
    hold = state.get('release_hold', {})
    if hold.get('active') or (state.get('manual_milestones') and not force):
        if force:
            raise ValueError('Binary publication is held: ' + hold.get('reason', 'Stage 3 acceptance required'))
        if state.get('last_counted_source_commit') == source:
            return {'status': 'already-recorded', 'source_commit': source, 'release_hold': hold}
        # Keep the prior cadence checkpoint; count held iterations separately.
        counter = 'held_source_iterations' if hold.get('active') else 'milestone_source_iterations'
        state[counter] = state.get(counter, 0) + 1
        state['last_counted_source_commit'] = source
        temporary = state_path.with_suffix('.json.tmp')
        temporary.write_text(json.dumps(state, indent=2) + '\n')
        temporary.replace(state_path)
        return {'status': 'publication-held-source-only' if hold.get('active') else 'milestone-source-only', **state}
    due, count, duplicate = publication_due(state, source, force)
    if duplicate:
        return {'status': 'already-recorded', 'source_commit': source}
    next_state = dict(state)
    if due:
        directory = stage(root, build, source, handoff)
        next_state['latest'] = {'source_commit': source, 'directory': str(directory.relative_to(root)),
                                'tag': 'build-' + source[:7]}
        write_download_index(root, next_state['latest'])
        count = 0
    next_state.update(schema=1, artifact_interval=INTERVAL, iterations_since_artifacts=count,
                      last_counted_source_commit=source)
    state_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = state_path.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(next_state, indent=2) + '\n')
    temporary.replace(state_path)
    return {'status': 'artifacts-staged' if due else 'source-only-iteration', **next_state}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--iteration', action='store_true')
    mode.add_argument('--publish-now', action='store_true')
    parser.add_argument('--source-ref', default='HEAD')
    parser.add_argument('--build-dir', type=pathlib.Path, default=ROOT / 'build')
    parser.add_argument('--handoff', type=pathlib.Path)
    args = parser.parse_args()
    source = subprocess.check_output(['git', 'rev-parse', '--verify', '--end-of-options',
                                      args.source_ref + '^{commit}'], cwd=ROOT, text=True).strip()
    if len(source) != 40 or any(c not in '0123456789abcdef' for c in source):
        raise ValueError('Expected a full source commit')
    print(json.dumps(run(ROOT, args.build_dir.resolve(), source, args.publish_now, args.handoff), indent=2))


if __name__ == '__main__':
    main()
