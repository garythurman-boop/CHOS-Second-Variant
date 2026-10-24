#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile
import unittest
import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
import mkchfs

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL = ROOT / 'tools' / 'mkchfs.py'

class CHFSToolTests(unittest.TestCase):
    def run_tool(self, *args, ok=True):
        result = subprocess.run(['python3', str(TOOL), *map(str, args)], text=True, capture_output=True)
        if ok and result.returncode != 0:
            self.fail(result.stdout + result.stderr)
        if not ok and result.returncode == 0:
            self.fail('expected command to fail')
        return result

    def test_create_put_replace_remove_extract(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td); img = root / 'disk.img'; src = root / 'app.bin'; out = root / 'out.bin'
            src.write_bytes(b'initial image\x00payload')
            self.run_tool('create', img, '--size-mib', '1', '--put', f'/BIN/APP.CHX={src}')
            self.run_tool('list', img)
            self.run_tool('read', img, '/bin/app.chx', out)
            self.assertEqual(out.read_bytes(), src.read_bytes())
            src.write_bytes(b'replacement image and a different file payload')
            self.run_tool('put', img, '/BIN/APP.CHX', src)
            self.run_tool('read', img, '/BIN/APP.CHX', out)
            self.assertEqual(out.read_bytes(), src.read_bytes())
            self.run_tool('rm', img, '/BIN/APP.CHX')
            listing = self.run_tool('list', img).stdout
            self.assertNotIn('/BIN/APP.CHX', listing)
            self.assertIn('/BIN/', listing)


    def test_nested_directories_empty_files_and_empty_directory_removal(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td); img = root / 'disk.img'; src = root / 'empty'; out = root / 'out'
            src.write_bytes(b'')
            self.run_tool('create', img, '--size-mib', '1', '--put', f'/A/B/EMPTY.TXT={src}')
            listing = self.run_tool('list', img).stdout
            self.assertIn('/A/', listing)
            self.assertIn('/A/B/', listing)
            self.assertIn('/A/B/EMPTY.TXT', listing)
            self.run_tool('read', img, '/A/B/EMPTY.TXT', out)
            self.assertEqual(out.read_bytes(), b'')
            self.run_tool('rm', img, '/A/B', ok=False)
            self.run_tool('rm', img, '/A/B/EMPTY.TXT')
            self.run_tool('rmdir', img, '/A/B')
            self.run_tool('rmdir', img, '/A')

    def test_failed_commit_preserves_previous_generation_extents(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td); img = root / 'disk.img'; old_src = root / 'old'; new_src = root / 'new'; out = root / 'out'
            old_src.write_bytes(b'older generation must remain readable')
            new_src.write_bytes(b'new unrelated payload')
            self.run_tool('create', img, '--size-mib', '1', '--put', f'/DOCS/ROLLBACK.TXT={old_src}')
            old_image = bytearray(img.read_bytes())
            old_entry = next(e for e in mkchfs.active_dir(old_image)[2] if e is not None and e['path'] == '/DOCS/ROLLBACK.TXT')
            old_sector = old_entry['start']
            old_src.write_bytes(b'replaced contents occupy a separate extent')
            self.run_tool('put', img, '/DOCS/ROLLBACK.TXT', old_src)

            # Snapshot the valid older directory copy, then perform another put.
            # Restore that copy to model a failed metadata commit while leaving
            # the new data sectors written; corrupt the current copy to force
            # mount recovery to the older generation.
            before = bytearray(img.read_bytes())
            total, gen, entries, active_lba = mkchfs.active_dir(before)
            layout = mkchfs.parse_super(bytes(before[:mkchfs.SECTOR]), len(before) // mkchfs.SECTOR)
            backup_lba = layout['b'] if active_lba == layout['a'] else layout['a']
            backup_slice = slice(backup_lba * mkchfs.SECTOR, (backup_lba + layout['dir_sectors']) * mkchfs.SECTOR)
            backup_bytes = bytes(before[backup_slice])
            self.run_tool('put', img, '/DOCS/SECOND.TXT', new_src)
            after = bytearray(img.read_bytes())
            after[backup_slice] = backup_bytes
            # The previously active generation is now treated as torn.
            after[active_lba * mkchfs.SECTOR + 12] ^= 0x80
            img.write_bytes(after)
            self.run_tool('read', img, '/DOCS/ROLLBACK.TXT', out)
            self.assertEqual(out.read_bytes(), b'older generation must remain readable')


    def test_v1_tools_preserve_legacy_layout(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td); img = root / 'legacy.img'; src = root / 'note.txt'; out = root / 'out.txt'
            src.write_bytes(b'legacy CHFS data stays readable')
            image = mkchfs.format_image(2048, version=mkchfs.V1_VERSION)
            mkchfs.put(image, '/DOCS/README.TXT', src.read_bytes())
            img.write_bytes(image)
            listing = self.run_tool('list', img).stdout
            self.assertIn('CHFS v1:', listing)
            self.run_tool('read', img, '/docs/readme.txt', out)
            self.assertEqual(out.read_bytes(), src.read_bytes())
            self.run_tool('put', img, '/BIN/NEW.CHX', src)
            updated = bytearray(img.read_bytes())
            layout = mkchfs.parse_super(bytes(updated[:mkchfs.SECTOR]), len(updated) // mkchfs.SECTOR)
            self.assertEqual(layout['version'], mkchfs.V1_VERSION)
            self.assertEqual(layout['a'], mkchfs.DIR_A)
            self.assertEqual(layout['b'], mkchfs.DIR_B)

    def test_v3_can_use_more_than_v2_63_entries(self):
        image = mkchfs.format_image(2048)
        for index in range(400):
            mkchfs.mkdir(image, f'/DIR{index:03d}')
        total, generation, entries, active = mkchfs.active_dir(image)
        layout = mkchfs.parse_super(bytes(image[:mkchfs.SECTOR]), len(image) // mkchfs.SECTOR)
        self.assertEqual(layout['version'], mkchfs.VERSION)
        self.assertEqual(len(entries), mkchfs.MAX_FILES)
        self.assertIn('/DIR399', [e['path'] for e in entries if e is not None])
        self.assertGreater(generation, 0)
        self.assertIn(active, (layout['a'], layout['b']))

    def test_v2_layout_remains_readable_and_unchanged_by_host_tool(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td); img = root / 'v2.img'; src = root / 'a.txt'; out = root / 'out.txt'
            src.write_bytes(b'legacy v2 layout')
            image = mkchfs.format_image(2048, version=mkchfs.V2_VERSION)
            mkchfs.put(image, '/DOCS/A.TXT', src.read_bytes())
            img.write_bytes(image)
            listing = self.run_tool('list', img).stdout
            self.assertIn('CHFS v2:', listing)
            self.run_tool('read', img, '/DOCS/A.TXT', out)
            self.assertEqual(out.read_bytes(), src.read_bytes())
            updated = bytearray(img.read_bytes())
            layout = mkchfs.parse_super(bytes(updated[:mkchfs.SECTOR]), len(updated) // mkchfs.SECTOR)
            self.assertEqual(layout['version'], mkchfs.V2_VERSION)
            self.assertEqual(layout['data_start'], mkchfs.LEGACY_DATA_LBA)

    def test_reject_duplicate_invalid_path_and_corrupt_superblock(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td); img = root / 'disk.img'; src = root / 'file'
            src.write_bytes(b'x')
            self.run_tool('create', img, '--size-mib', '1')
            self.run_tool('put', img, '/OK.TXT', src)
            self.run_tool('put', img, '/bad path', src, ok=False)
            bad = bytearray(img.read_bytes()); bad[100] ^= 0x20; img.write_bytes(bad)
            self.run_tool('list', img, ok=False)

if __name__ == '__main__': unittest.main()
