import pathlib,sys,json,tempfile,shutil,unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import prepare_test262 as p
class TestInputs(unittest.TestCase):
    def test_original_selection(self):
        data,cases=p.inputs(p.ROOT/'third_party/test262-subset',json.loads(p.MANIFEST.read_text()))
        self.assertEqual(len(cases),88)
        self.assertEqual(sum(2 if c['modes']==3 else 1 for c in cases),172)
        self.assertEqual(sum(bool(c['negative']) for c in cases),9)
        self.assertEqual(sum(c['asynchronous'] for c in cases),6)
        generated=p.header(data,cases)
        for case in cases:self.assertIn(data[case['path']],generated)
        for name in ['sta.js','assert.js','doneprintHandle.js']:self.assertIn(data['harness/'+name],generated)
    def test_changed_source_refuses(self):
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)/'sources';shutil.copytree(p.ROOT/'third_party/test262-subset',root)
            manifest=json.loads(p.MANIFEST.read_text());target=root/manifest['selected_test_paths'][0]
            target.write_bytes(target.read_bytes()+b'\n')
            with self.assertRaisesRegex(ValueError,'Changed pinned'):p.inputs(root,manifest)
    def test_metadata_never_silently_skips(self):
        for front in ['flags: [module]','flags: [noStrict, onlyStrict]','flags: [async, async]','flags: unsupported','flags: []\nflags: []','negative:\n  phase: runtime\n  type: Error']:
            with self.subTest(front=front),self.assertRaises(ValueError):p.metadata('/*---\n'+front+'\n---*/\n')
        with self.assertRaises(ValueError):p.metadata('no frontmatter')
    def test_path_and_literal_boundaries(self):
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)
            for name in ['/tmp/escape','../escape','test/../../escape','test/./escape','unreviewed.js']:
                with self.subTest(name=name),self.assertRaises(ValueError):p.checked_path(root,name)
        with self.assertRaises(ValueError):p.raw_literal('a\0b')
        self.assertTrue(p.raw_literal(')TC262_0"').startswith('R"TC262_1('))
if __name__=='__main__':unittest.main()
