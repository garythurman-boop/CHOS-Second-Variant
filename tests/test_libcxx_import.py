#!/usr/bin/env python3
"""Regression: failed dependency discovery must never leave a partial import."""
import hashlib
import io
import json
import pathlib
import shutil
import subprocess
import tarfile
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class ImportTransactions(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='chos-import-test-')
        self.root = pathlib.Path(self.temporary.name)
        (self.root/'tools').mkdir()
        for name in ('import_libcxx_headers.py', 'verify_libcxx.py'):
            shutil.copyfile(ROOT/'tools'/name, self.root/'tools'/name)
        self.base = self.root/'third_party/libcxx'
        self.base.mkdir(parents=True)
        self.archive = self.root/'original.tar.xz'
        originals = {'include/plain': b'// original public header\n',
                     'include/broken': b'#include <__private.h>\n',
                     'include/__private.h': b'#include <__absent.h>\n',
                     'src/hash.cpp': b'// original source\n'}
        with tarfile.open(self.archive, 'w:xz') as archive:
            for name, data in originals.items():
                entry = tarfile.TarInfo('libcxx-1.0.0.src/'+name)
                entry.size = len(data)
                archive.addfile(entry, io.BytesIO(data))
        self.manifest = self.root/'docs/dependencies/libcxx-native.json'
        self.manifest.parent.mkdir(parents=True)
        self.manifest.write_text(json.dumps({'version': '1.0.0', 'files': {}, 'sources': [],
            'release_archive_sha256': {'libcxx.tar.xz': hashlib.sha256(self.archive.read_bytes()).hexdigest()}}))

    def tearDown(self):
        self.temporary.cleanup()

    def snapshot(self):
        return {str(p.relative_to(self.root)): p.read_bytes()
                for p in [self.manifest, *self.base.rglob('*')] if p.is_file()}

    def run_import(self, *arguments):
        return subprocess.run(['python3', str(self.root/'tools/import_libcxx_headers.py'),
                               '--archive', str(self.archive), *arguments], capture_output=True, text=True)

    def test_late_missing_private_header_preserves_tree(self):
        before = self.snapshot()
        result = self.run_import('--header', 'broken')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('missing private header: include/__absent.h', result.stderr)
        self.assertEqual(before, self.snapshot())

    def test_dry_run_then_real_header_and_source_import(self):
        before = self.snapshot()
        arguments = ('--header', 'plain', '--source-file', 'hash.cpp')
        result = self.run_import(*arguments, '--check-only')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(before, self.snapshot())
        result = self.run_import(*arguments)
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads(self.manifest.read_text())
        self.assertEqual(manifest['sources'], ['src/hash.cpp'])
        self.assertEqual(set(manifest['files']), {'include/plain', 'src/hash.cpp'})
        self.assertEqual((self.base/'include/plain').read_bytes(), b'// original public header\n')
        self.assertEqual((self.base/'src/hash.cpp').read_bytes(), b'// original source\n')
        result = self.run_import(*arguments)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(self.manifest.read_text())['sources'], ['src/hash.cpp'])

    def test_archive_identity_failure_preserves_tree(self):
        before = self.snapshot()
        with self.archive.open('ab') as stream:
            stream.write(b'changed')
        self.assertNotEqual(self.run_import('--header', 'plain').returncode, 0)
        self.assertEqual(before, self.snapshot())

    def test_traversal_and_unregistered_source_are_refused(self):
        before = self.snapshot()
        self.assertNotEqual(self.run_import('--header', '../../escape').returncode, 0)
        self.assertEqual(before, self.snapshot())
        (self.base/'unregistered').write_text('preserve me')
        before = self.snapshot()
        self.assertNotEqual(self.run_import('--header', 'plain').returncode, 0)
        self.assertEqual(before, self.snapshot())

if __name__ == '__main__':
    unittest.main()
