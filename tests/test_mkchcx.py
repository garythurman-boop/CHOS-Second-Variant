#!/usr/bin/env python3
import importlib.util
import pathlib
import struct
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("mkchcx", ROOT / "tools" / "mkchcx.py")
mkchcx = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(mkchcx)

class ChcxPackerTests(unittest.TestCase):
    def test_header_and_round_trip(self):
        payload = bytes.fromhex("1f2003d5 1f2003d5")
        image = mkchcx.pack(payload, 0, 2)
        self.assertEqual(len(image), 24 + len(payload))
        self.assertEqual(image[:4], b"CHCX")
        self.assertEqual(mkchcx.validate(image), (8, 0, 2))
        self.assertEqual(image[24:], payload)

    def test_nonzero_entry(self):
        payload = bytes.fromhex("1f2003d5 1f2003d5")
        self.assertEqual(mkchcx.validate(mkchcx.pack(payload, 4, 1)), (8, 4, 1))

    def test_eight_page_stack_is_supported(self):
        payload = bytes.fromhex("1f2003d5")
        image = mkchcx.pack(payload, 0, 8)
        self.assertEqual(mkchcx.validate(image), (4, 0, 8))

    def test_reject_invalid_inputs(self):
        payload = bytes.fromhex("1f2003d5")
        for entry, stack in ((2, 1), (4, 1), (0, 0), (0, 9)):
            with self.subTest(entry=entry, stack=stack):
                with self.assertRaises(ValueError):
                    mkchcx.pack(payload, entry, stack)
        with self.assertRaises(ValueError):
            mkchcx.validate(b"not a CHCX image")
        damaged = bytearray(mkchcx.pack(payload))
        damaged[18] = 1
        with self.assertRaises(ValueError):
            mkchcx.validate(bytes(damaged))

    def test_size_limit(self):
        with self.assertRaises(ValueError):
            mkchcx.pack(b"\x00" * (mkchcx.MAX_IMAGE_SIZE + 1))

    def test_v2_segment_pack_and_validation(self):
        symbols = {
            "_start": 0,
            "__chos_code_start": 0,
            "__chos_code_end": 32,
            "__chos_rodata_start": 4096,
            "__chos_rodata_end": 4104,
            "__chos_data_start": 8192,
            "__chos_data_end": 8196,
            "__chos_bss_start": 8196,
            "__chos_bss_end": 8200,
            "__chos_virtual_end": 12288,
        }
        flat = bytearray(8196)
        flat[:4] = bytes.fromhex("1f2003d5")
        image = mkchcx.pack_v2(bytes(flat), symbols, stack_pages=2)
        self.assertEqual(len(image), mkchcx.V2_HEADER_SIZE + 8196)
        self.assertEqual(struct.unpack("<H", image[4:6])[0], 2)
        self.assertEqual(mkchcx.validate(image), (8196, 0, 2))
        self.assertEqual(struct.unpack("<I", image[52:56])[0], 12288)
        damaged = bytearray(image)
        damaged[29] = 0
        with self.assertRaises(ValueError):
            mkchcx.validate(bytes(damaged))

    def test_v2_rejects_out_of_order_segments(self):
        symbols = {
            "_start": 0, "__chos_code_start": 0, "__chos_code_end": 4097,
            "__chos_rodata_start": 8192, "__chos_rodata_end": 8200,
            "__chos_data_start": 8192, "__chos_data_end": 8196,
            "__chos_bss_start": 8196, "__chos_bss_end": 8200,
            "__chos_virtual_end": 12288,
        }
        with self.assertRaises(ValueError):
            mkchcx.pack_v2(bytes(12288), symbols)

if __name__ == "__main__":
    unittest.main()
