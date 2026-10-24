#!/usr/bin/env python3
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import mkchfs
import sync_system_apps


class SyncSystemAppsTests(unittest.TestCase):
    def test_replaces_stale_apps_and_preserves_user_files(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            image_path = root / 'disk.img'
            stale = b'old app build'
            fresh = b'new app build with different syscall usage'
            user_data = b'my notes must survive update'
            image = mkchfs.format_image(2048)
            mkchfs.put(image, '/BIN/CONSOLE.CHX', stale)
            mkchfs.put(image, '/DOCS/USER.TXT', user_data)
            image_path.write_bytes(image)
            app_path = root / 'console.chcx'
            app_path.write_bytes(fresh)

            self.assertEqual(sync_system_apps.sync_apps(image_path, [f'/BIN/CONSOLE.CHX={app_path}']), 1)
            synced = bytearray(image_path.read_bytes())
            _, _, entries, _ = mkchfs.active_dir(synced)
            def read_entry(path):
                entry = next(e for e in entries if e and e['path'].casefold() == path.casefold())
                start = entry['start'] * mkchfs.SECTOR
                data = bytes(synced[start:start + entry['size']])
                self.assertEqual(mkchfs.crc(data), entry['crc'])
                return data
            self.assertEqual(read_entry('/BIN/CONSOLE.CHX'), fresh)
            self.assertEqual(read_entry('/DOCS/USER.TXT'), user_data)

            self.assertEqual(sync_system_apps.sync_apps(image_path, [f'/BIN/CONSOLE.CHX={app_path}']), 0)

    def test_rejects_missing_mapping(self):
        with tempfile.TemporaryDirectory() as td:
            image_path = pathlib.Path(td) / 'missing.img'
            with self.assertRaises((OSError, ValueError)):
                sync_system_apps.sync_apps(image_path, ['/BIN/CONSOLE.CHX=/missing/file'])


if __name__ == '__main__':
    unittest.main()
