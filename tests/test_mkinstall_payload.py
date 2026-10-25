#!/usr/bin/env python3
"""Sparse install payload roundtrip and output safety using disposable files only."""
import importlib.util, pathlib, struct, subprocess, sys, tempfile, unittest, zlib
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('payload',ROOT/'tools/mkinstall_payload.py')
payload=importlib.util.module_from_spec(spec);spec.loader.exec_module(payload)
class PayloadTests(unittest.TestCase):
 def test_sparse_roundtrip_and_symlink_guard(self):
  with tempfile.TemporaryDirectory() as temporary:
   folder=pathlib.Path(temporary);image=folder/'fixture.img'
   with image.open('wb') as f:
    f.truncate((2048+131072)*512);f.seek(2048*512+510);f.write(b'\x55\xaa');f.seek((2048+17)*512);f.write(b'nonzero-sector')
   data=payload.encode(image);magic,size,sectors,runs,crc=struct.unpack_from('<8sIIII',data)
   self.assertEqual((magic,size,sectors),(b'CHOSINS1',512,131072));self.assertEqual(zlib.crc32(data[24:]),crc)
   restored=bytearray(sectors*512);off=24
   for _ in range(runs):
    start,count=struct.unpack_from('<II',data,off);off+=8
    restored[start*512:(start+count)*512]=data[off:off+count*512];off+=count*512
   self.assertEqual(off,len(data))
   with image.open('rb') as f:f.seek(2048*512);self.assertEqual(restored,f.read(131072*512))
   target=folder/'preserved';target.write_bytes(b'preserve-this-file');link=folder/'unsafe.payload';link.symlink_to(target)
   result=subprocess.run([sys.executable,str(ROOT/'tools/mkinstall_payload.py'),'--image',str(image),'--output',str(link),'--force'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=20)
   self.assertNotEqual(result.returncode,0);self.assertEqual(target.read_bytes(),b'preserve-this-file')
 def test_short_input(self):
  with tempfile.TemporaryDirectory() as temporary:
   image=pathlib.Path(temporary)/'short.img';image.write_bytes(bytes(512))
   with self.assertRaises(ValueError):payload.encode(image)
if __name__=='__main__':unittest.main()
