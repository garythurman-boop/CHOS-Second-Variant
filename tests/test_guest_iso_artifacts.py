#!/usr/bin/env python3
"""Catch stale-symbol and malformed-ISO fixture errors before starting a VM."""
import hashlib
import pathlib
import tempfile
import unittest
from guest_iso_artifacts import embedded_kernel_bytes,verify_iso_kernel,installed_kernel_bytes,verify_installed_kernel
from test_mkuefi_iso import fake_efi,fake_elf,iso
from tools.fat32_builder import Fat32Builder
from tools.mkchos_install_disk import make_gpt


class GuestIsoArtifactsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=tempfile.TemporaryDirectory()
        cls.root=pathlib.Path(cls.directory.name)
        cls.kernel_bytes=fake_elf()+b'guest-symbol-fixture\0'*15000
        fat=iso.Fat16Builder(sectors=32768)
        fat.add_file('EFI/BOOT/BOOTAA64.EFI',fake_efi())
        fat.add_file('EFI/CHOS/CHOS.ELF',cls.kernel_bytes)
        cls.iso_bytes=iso.build_iso(fat.build())
        cls.boot=cls.root/'boot.iso';cls.boot.write_bytes(cls.iso_bytes)
        cls.kernel=cls.root/'kernel.elf';cls.kernel.write_bytes(cls.kernel_bytes)
        cls.installed=cls.root/'installed.img'
        cls.disk_size=80*1024*1024
        esp_first=2048;esp_sectors=64*1024*1024//512
        mbr,header,entries,_,_,_=make_gpt(cls.disk_size//512,esp_first,esp_sectors,esp_first+esp_sectors)
        fat32=Fat32Builder(esp_sectors)
        fat32.add_file('EFI/BOOT/BOOTAA64.EFI',fake_efi())
        fat32.add_file('EFI/CHOS/CHOS.ELF',cls.kernel_bytes)
        with cls.installed.open('wb') as image:
            image.truncate(cls.disk_size)
            image.seek(0);image.write(mbr);image.write(header);image.write(entries)
            image.seek(esp_first*512);image.write(fat32.build())

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_extract_crosses_fat_clusters(self):
        self.assertEqual(embedded_kernel_bytes(self.boot),self.kernel_bytes)

    def test_matching_kernel_records_digest(self):
        result=verify_iso_kernel(self.boot,self.kernel)
        self.assertTrue(result['kernel_symbols_match_iso'])
        self.assertEqual(result['iso_embedded_kernel_sha256'],hashlib.sha256(self.kernel_bytes).hexdigest())

    def test_mismatched_symbol_image_fails_closed(self):
        stale=self.root/'stale.elf';stale.write_bytes(self.kernel_bytes[:-1]+b'x')
        with self.assertRaisesRegex(AssertionError,'differs from supplied symbols'):
            verify_iso_kernel(self.boot,stale)

    def test_truncated_iso_has_clear_failure(self):
        truncated=self.root/'truncated.iso';truncated.write_bytes(self.iso_bytes[:1024])
        with self.assertRaisesRegex(AssertionError,'Cannot inspect embedded CHOS kernel'):
            embedded_kernel_bytes(truncated)

    def test_installed_kernel_comes_from_esp_path(self):
        self.assertEqual(installed_kernel_bytes(self.installed),self.kernel_bytes)
        result=verify_installed_kernel(self.installed,self.kernel)
        self.assertTrue(result['kernel_symbols_match_installed_esp'])
        self.assertEqual(result['installed_esp_kernel_sha256'],hashlib.sha256(self.kernel_bytes).hexdigest())

    def test_stale_kernel_elsewhere_on_disk_is_not_accepted(self):
        stale_bytes=self.kernel_bytes[:-1]+b'x'
        stale=self.root/'unindexed-stale.elf';stale.write_bytes(stale_bytes)
        with self.installed.open('r+b') as image:
            image.seek(self.disk_size-1024*1024);image.write(stale_bytes)
        with self.assertRaisesRegex(AssertionError,'Installed ESP kernel differs'):
            verify_installed_kernel(self.installed,stale)

    def test_corrupt_gpt_is_refused_before_fat_access(self):
        corrupt=self.root/'corrupt-gpt.img'
        with corrupt.open('wb') as image:
            image.truncate(self.disk_size)
        with self.assertRaisesRegex(AssertionError,'missing GPT header'):
            installed_kernel_bytes(corrupt)


if __name__=='__main__':
    unittest.main()
