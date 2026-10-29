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
                     'src/hash.cpp': b'// original source\n',
                     'src/real.cpp': b'#include "include/detail.h"\n',
                     'src/include/detail.h': b'// selected private source header\n',
                     'src/include/unused.h': b'// must remain unimported\n'}
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

    def test_native_scan_failure_reports_cause_without_import(self):
        for name in ('__config_site', '__assertion_handler'):
            p = self.root/'build/userland/libcxx/include'/name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text('// configuration fixture\n')
        p = self.root/'build/userland/libm/include/math.h'
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text('// math fixture\n')
        compiler = self.root/'compiler'
        compiler.write_text('#!/usr/bin/env python3\nimport sys\nsys.stderr.write("missing native dependency fixture\\n")\nraise SystemExit(9)\n')
        compiler.chmod(0o755)
        before = self.snapshot()
        result = self.run_import('--native-header-closure', '--header', 'plain',
                                 '--compiler', str(compiler))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('missing native dependency fixture', result.stderr)
        self.assertEqual(before, self.snapshot())

    def test_native_source_scan_imports_only_reported_quoted_dependency(self):
        for name in ('__config_site', '__assertion_handler'):
            p = self.root/'build/userland/libcxx/include'/name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text('// configuration fixture\n')
        p = self.root/'build/userland/libm/include/math.h'
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text('// math fixture\n')
        compiler = self.root/'compiler'
        compiler.write_text('#!/usr/bin/env python3\nimport sys,pathlib\np=pathlib.Path(sys.argv[-1])\nprint("chos_import: "+str(p)+" "+str(p.parent/"include/detail.h"))\n')
        compiler.chmod(0o755)
        arguments = ('--native-source-closure', '--source-file', 'real.cpp',
                     '--compiler', str(compiler))
        before = self.snapshot()
        result = self.run_import(*arguments, '--check-only')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(before, self.snapshot())
        result = self.run_import(*arguments)
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads(self.manifest.read_text())
        self.assertEqual(set(manifest['files']), {'src/real.cpp', 'src/include/detail.h'})
        self.assertEqual(manifest['sources'], ['src/real.cpp'])
        self.assertEqual((self.base/'src/include/detail.h').read_bytes(),
                         b'// selected private source header\n')
        before = self.snapshot()
        result = self.run_import(*arguments)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(before, self.snapshot())

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
