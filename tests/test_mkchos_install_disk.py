#!/usr/bin/env python3
"""End-to-end tests for the safe CHOS installed-disk image builder."""
from __future__ import annotations
import pathlib
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import mkchos_install_disk as installer  # noqa: E402
from tools.fat32_builder import short_name  # noqa: E402
from tests.test_mkuefi_iso import fake_efi, fake_elf  # noqa: E402


def fat_read_file(fp, partition_start: int, path_parts: list[str]) -> bytes:
    fp.seek(partition_start * 512)
    boot = fp.read(512)
    assert boot[82:90] == b'FAT32   '
    spc = boot[13]
    reserved = struct.unpack_from('<H', boot, 14)[0]
    fat_count = boot[16]
    spf = struct.unpack_from('<I', boot, 36)[0]
    root_cluster = struct.unpack_from('<I', boot, 44)[0]
    data_start = reserved + fat_count * spf
    def cluster_offset(cluster: int) -> int:
        return (partition_start + data_start + (cluster - 2) * spc) * 512
    cluster = root_cluster
    ent = None
    for wanted in path_parts:
        fp.seek(cluster_offset(cluster))
        raw = fp.read(spc * 512)
        target = short_name(wanted)
        ent = None
        for pos in range(0, len(raw), 32):
            row = raw[pos:pos + 32]
            if len(row) < 32 or row[0] == 0:
                break
            if row[0] == 0xe5 or row[11] == 0x0f:
                continue
            if row[:11] == target:
                ent = row
                break
        assert ent is not None, f'FAT32 entry not found: {wanted}'
        cluster = (struct.unpack_from('<H', ent, 20)[0] << 16) | struct.unpack_from('<H', ent, 26)[0]
    size = struct.unpack_from('<I', ent, 28)[0]
    if ent[11] & 0x10:
        raise AssertionError('requested path refers to directory, not file')
    out = bytearray()
    while cluster < 0x0ffffff8 and len(out) < size:
        if cluster < 2:
            raise AssertionError('invalid FAT32 cluster in chain')
        fp.seek(cluster_offset(cluster)); out.extend(fp.read(spc * 512))
        fp.seek((partition_start + reserved) * 512 + cluster * 4)
        raw_next = fp.read(4)
        if len(raw_next) != 4:
            raise AssertionError('short FAT32 cluster chain')
        cluster = struct.unpack('<I', raw_next)[0] & 0x0fffffff
    assert len(out) >= size
    return bytes(out[:size])


def main() -> None:
    with tempfile.TemporaryDirectory(prefix='chos-install-test-') as td:
        root = pathlib.Path(td)
        kernel = root / 'kernel.elf'; kernel.write_bytes(fake_elf())
        efi = root / 'BOOTAA64.EFI'; efi.write_bytes(fake_efi())
        app = root / 'BROWSER.CHX'; app.write_bytes(b'test-CHCX-payload')
        notes = root / 'NOTES.TXT'; notes.write_text('CHOS install test\n', encoding='ascii')
        image = root / 'chos-installed.img'
        cmd = [sys.executable, str(ROOT / 'tools/mkchos_install_disk.py'),
               '--kernel', str(kernel), '--efi', str(efi), '--readme', str(notes),
               '--app', f'/BIN/BROWSER.CHX={app}', '--size-mib', '96', '--output', str(image)]
        result = subprocess.run(cmd, check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if result.returncode != 0:
            raise AssertionError(f'installer builder failed: {result.stdout}\n{result.stderr}')
        info = installer.verify_image(image)
        assert info['size_bytes'] == 96 * 1024 * 1024
        with image.open('rb') as fp:
            esp_kernel = fat_read_file(fp, int(info['esp_first_lba']), ['EFI', 'CHOS', 'CHOS.ELF'])
            esp_loader = fat_read_file(fp, int(info['esp_first_lba']), ['EFI', 'BOOT', 'BOOTAA64.EFI'])
            assert esp_kernel == kernel.read_bytes()
            assert esp_loader == efi.read_bytes()
        # Ensure the installed GPT data partition has a valid CHFS image with the app.
        import tools.mkchfs as mkchfs
        with image.open('rb') as fp:
            fp.seek(int(info['data_first_lba']) * 512)
            superblock = fp.read(512)
        layout = mkchfs.parse_super(superblock, int(info['data_last_lba']) - int(info['data_first_lba']) + 1)
        assert layout['version'] == 3

        # Image creation must never follow a symlink or allow --force on a device node.
        sentinel = root / 'sentinel.img'; sentinel.write_bytes(b'unchanged')
        link = root / 'alias.img'; link.symlink_to(sentinel)
        link_cmd = [sys.executable, str(ROOT / 'tools/mkchos_install_disk.py'),
                    '--kernel', str(kernel), '--efi', str(efi), '--output', str(link), '--size-mib', '96', '--force']
        rejected = subprocess.run(link_cmd, check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        assert rejected.returncode != 0
        assert sentinel.read_bytes() == b'unchanged'
        if pathlib.Path('/dev/null').exists():
            device_cmd = [sys.executable, str(ROOT / 'tools/mkchos_install_disk.py'),
                          '--kernel', str(kernel), '--efi', str(efi), '--output', '/dev/null', '--size-mib', '96', '--force']
            device_rejected = subprocess.run(device_cmd, check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            assert device_rejected.returncode != 0
        print('CHOS install image: protective MBR, primary/backup GPT CRCs, FAT32 ESP, EFI boot files, CHFS partition/app payload, symlink refusal and device-path refusal passed')


if __name__ == '__main__':
    main()
