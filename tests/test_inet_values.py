#!/usr/bin/env python3
"""Compare CHOS address conversions and network byte order with the host socket library."""
import ctypes
from pathlib import Path
import random
import socket
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AddressValues(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        directory = Path(cls.temp.name)
        errno_source = directory / 'errno.c'
        errno_source.write_text('int *__chos_errno_location(void) { static _Thread_local int e; return &e; }\n')
        library = directory / 'inet.so'
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-shared', '-fPIC', '-Wl,-Bsymbolic', '-I', str(ROOT / 'include'), '-I', str(ROOT / 'userland/libc/include'),
                        str(ROOT / 'userland/libc/inet.c'), str(ROOT / 'userland/libc/inet/inet_pton.c'),
                        str(ROOT / 'userland/libc/inet/inet_ntop.c'), str(errno_source), '-o', str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.inet_pton.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_void_p]
        cls.lib.inet_ntop.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32]
        cls.lib.inet_ntop.restype = ctypes.c_void_p

    def test_address_roundtrips(self):
        gen = random.Random(0x43484f53)
        for family, size in ((socket.AF_INET, 4), (socket.AF_INET6, 16)):
            for iteration in range(2048):
                data = bytearray(gen.randbytes(size))
                if family == socket.AF_INET6 and iteration % 2:
                    start = gen.randrange(8)
                    end = gen.randrange(start + 1, 9)
                    data[2*start:2*end] = bytes(2*(end-start))
                packed = bytes(data)
                output = ctypes.create_string_buffer(46)
                self.assertTrue(self.lib.inet_ntop(family, packed, output, len(output)))
                # Valid equivalent IPv6 presentations may use different spellings.
                self.assertEqual(socket.inet_pton(family, output.value.decode()), packed)
                result = ctypes.create_string_buffer(size + 1)
                result[size] = b'Z'
                text = socket.inet_ntop(family, packed).encode()
                self.assertEqual(self.lib.inet_pton(family, text, result), 1)
                self.assertEqual(result.raw, packed + b'Z')

    def test_byte_order(self):
        for name, ctype, oracle, values in [
            ('htons', ctypes.c_uint16, socket.htons, range(65536)),
            ('htonl', ctypes.c_uint32, socket.htonl, [0, 1, 0x01020304, 0x7fffffff, 0x80000000, 0xffffffff]),
        ]:
            convert = getattr(self.lib, name)
            convert.argtypes = [ctype]
            convert.restype = ctype
            for value in values:
                self.assertEqual(convert(value), oracle(value))


if __name__ == '__main__':
    unittest.main()
