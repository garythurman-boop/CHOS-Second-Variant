#!/usr/bin/env python3
"""The SDK installer must refuse to replace unowned or user-modified output."""
import hashlib,json,pathlib,subprocess,sys,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class InstallerGuards(unittest.TestCase):
 def test_unowned_added_and_modified_files_are_retained(self):
  for kind in ['unowned','added','modified']:
   with self.subTest(kind=kind),tempfile.TemporaryDirectory() as temporary:
    out=pathlib.Path(temporary)/'sdk';out.mkdir();data=b'owned bytes';(out/'payload').write_bytes(data)
    if kind!='unowned':
     (out/'chos-sdk.json').write_text(json.dumps({'files':{'payload':hashlib.sha256(data).hexdigest()}}))
     (out/('user-file' if kind=='added' else 'payload')).write_bytes(b'user bytes')
    before={str(p.relative_to(out)):p.read_bytes() for p in out.rglob('*') if p.is_file()}
    result=subprocess.run([sys.executable,str(ROOT/'tools/install_native_sdk.py'),'--out',str(out),'--libc-object',str(ROOT/'build/userland/libc/allocator.o')],capture_output=True,text=True)
    self.assertNotEqual(result.returncode,0);self.assertIn('error:',result.stderr)
    self.assertEqual(before,{str(p.relative_to(out)):p.read_bytes() for p in out.rglob('*') if p.is_file()})
    self.assertEqual([out],list(out.parent.iterdir()))
if __name__=='__main__':unittest.main()
