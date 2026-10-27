#!/usr/bin/env python3
"""Publication cadence and integrity regressions; no network or Git writes."""
import importlib.util
import json
import pathlib
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('publisher', pathlib.Path(__file__).resolve().parents[1] / 'tools/publish_artifacts.py')
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)


class PublicationTests(unittest.TestCase):
    def test_stage3_hold_prevents_staging_and_force(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp);folder=root/'artifacts';folder.mkdir()
            path=folder/'publication.json'
            original={'latest':{'source_commit':'previous'},'iterations_since_artifacts':1,
                      'release_hold':{'active':True,'reason':'Complete and thoroughly test revised Stage 3'}}
            path.write_text(json.dumps(original))
            with mock.patch.object(publisher,'stage',side_effect=AssertionError('must not stage')):
                for i in range(4):
                    result=publisher.run(root,root/'build',str(i)*40)
                    self.assertEqual(result['status'],'publication-held-source-only')
                    self.assertEqual(result['latest'],original['latest'])
                self.assertEqual(result['held_source_iterations'],4)
                before=path.read_bytes()
                with self.assertRaisesRegex(ValueError,'publication is held'):
                    publisher.run(root,root/'build','f'*40,force=True)
                self.assertEqual(path.read_bytes(),before)
                publisher.run(root,root/'build','3'*40)
                self.assertEqual(path.read_bytes(),before)

    def test_manual_milestones_require_explicit_publication(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp); folder=root/'artifacts'; folder.mkdir()
            state={'manual_milestones':True,'release_hold':{'active':False},
                   'latest':{'source_commit':'previous'},'iterations_since_artifacts':1}
            path=folder/'publication.json'; path.write_text(json.dumps(state))
            with mock.patch.object(publisher,'stage',side_effect=AssertionError('source work must not auto-publish')):
                for i in range(4):
                    result=publisher.run(root,root/'build',str(i)*40)
                    self.assertEqual(result['status'],'milestone-source-only')
                    self.assertEqual(result['latest'],state['latest'])
                self.assertEqual(result['milestone_source_iterations'],4)
            with mock.patch.object(publisher,'stage',return_value=folder/('a'*7)) as stage:
                result=publisher.run(root,root/'build','a'*40,force=True)
            stage.assert_called_once()
            self.assertEqual(result['status'],'artifacts-staged')
            self.assertTrue(result['manual_milestones'])

    def test_every_other_iteration_and_duplicate(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            state = {'latest': {'source_commit': 'baseline'}, 'iterations_since_artifacts': 0,
                     'last_counted_source_commit': 'baseline'}
            folder = root / 'artifacts'; folder.mkdir()
            (folder / 'publication.json').write_text(json.dumps(state))
            first = publisher.run(root, root / 'build', 'a' * 40)
            self.assertEqual(first['status'], 'source-only-iteration')
            self.assertEqual(first['iterations_since_artifacts'], 1)
            self.assertEqual(publisher.run(root, root / 'build', 'a' * 40)['status'], 'already-recorded')
            with mock.patch.object(publisher, 'stage', return_value=folder / ('b' * 7)) as stage:
                second = publisher.run(root, root / 'build', 'b' * 40)
            stage.assert_called_once()
            self.assertEqual(second['iterations_since_artifacts'], 0)
            self.assertEqual(second['latest']['source_commit'], 'b' * 40)
            third = publisher.run(root, root / 'build', 'c' * 40)
            self.assertEqual(third['status'], 'source-only-iteration')

    def test_failed_staging_does_not_advance_counter(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp); folder = root / 'artifacts'; folder.mkdir()
            path = folder / 'publication.json'
            path.write_text(json.dumps({'iterations_since_artifacts': 1, 'last_counted_source_commit': 'a' * 40}))
            before = path.read_bytes()
            with mock.patch.object(publisher, 'stage', side_effect=ValueError('invalid build')):
                with self.assertRaises(ValueError): publisher.run(root, root / 'build', 'b' * 40)
            self.assertEqual(path.read_bytes(), before)

    def test_compression_preserves_exact_bytes(self):
        import gzip
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp); data = bytes(range(256)) * 100
            source = root / 'source'; source.write_bytes(data)
            one, two = root / 'one.gz', root / 'two.gz'
            publisher.compressed(source, one); publisher.compressed(source, two)
            self.assertEqual(one.read_bytes(), two.read_bytes())
            self.assertEqual(gzip.decompress(one.read_bytes()), data)

    def test_dirty_build_rejected_before_artifact_creation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp); build = root / 'build'
            (build / 'uefi-kernel').mkdir(parents=True)
            source = 'a' * 40
            dirty = ('[BUILD] ' + source[:12] + '-dirty\n\0').encode()
            (build / 'chos.elf').write_bytes(dirty)
            (build / 'uefi-kernel/chos.elf').write_bytes(dirty)
            with self.assertRaisesRegex(ValueError, 'not a clean build'):
                publisher.stage(root, build, source)
            self.assertFalse((root / 'artifacts').exists())

    def test_stale_prepared_disk_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp); build = root / 'build'
            (build / 'uefi-kernel').mkdir(parents=True); (build / 'uefi').mkdir()
            source = 'a' * 40; kernel = ('[BUILD] ' + source[:12] + '\n\0').encode()
            (build / 'chos.elf').write_bytes(kernel); (build / 'uefi-kernel/chos.elf').write_bytes(kernel)
            (build / 'uefi/BOOTAA64.EFI').write_bytes(b'current-EFI')
            (build / 'chos-0.30.0.iso').write_bytes(b'current-EFI' + kernel)
            (build / 'chos-0.30.0-installed.img').write_bytes(b'stale prepared disk')
            with self.assertRaisesRegex(ValueError, 'Prepared disk does not contain'):
                publisher.stage(root, build, source)
            self.assertFalse((root / 'artifacts').exists())

    def test_checksum_mismatch_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            asset = root / 'asset.iso'; asset.write_bytes(b'original')
            manifest = root / 'manifest.json'
            manifest.write_text(json.dumps({'assets': [{'name': asset.name, 'sha256': publisher.digest(asset), 'bytes': asset.stat().st_size}]}))
            (root / 'SHA256SUMS').write_text(publisher.digest(asset) + '  asset.iso\n' + publisher.digest(manifest) + '  manifest.json\n')
            publisher.verify(root); asset.write_bytes(b'changed')
            with self.assertRaises(ValueError): publisher.verify(root)


if __name__ == '__main__':
    unittest.main()
