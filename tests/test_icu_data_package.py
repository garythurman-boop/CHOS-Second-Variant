#!/usr/bin/env python3
"""Actual generated package integrity, bounds, overlap and path rejection."""
import copy, json, pathlib, struct, sys, unittest
root = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root/'tools'))
from icu_data_package import resources
directory = pathlib.Path(sys.argv.pop(1)) if len(sys.argv)>1 else root/'build/icu-data'

class DataPackage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data=(directory/'icudt79l.dat').read_bytes()
        cls.inventory=json.loads((directory/'resources.json').read_text())
        cls.header=struct.unpack_from('<H',cls.data)[0]
    def rejected(self, mutate):
        data=bytearray(self.data);mutate(data)
        with self.assertRaises(ValueError):resources(data,self.inventory)
    def test_all_original_resources_and_required_contents(self):
        items=resources(self.data,self.inventory)
        self.assertEqual(len(items),4321)
        for name in ('cnvalias.icu','root.res','en.res','coll/root.res','zoneinfo64.res','supplementalData.res'):
            self.assertEqual(bytes(items[name][2:4]),b'\xda\x27')
    def test_short_and_invalid_headers(self):
        for size in range(24):
            with self.subTest(size=size),self.assertRaises(ValueError):resources(self.data[:size],self.inventory)
        self.rejected(lambda d:d.__setitem__(8,1))
        self.rejected(lambda d:d.__setitem__(12,0))
        self.rejected(lambda d:struct.pack_into('<H',d,0,65535))
    def test_count_and_table_bounds(self):
        self.rejected(lambda d:struct.pack_into('<I',d,self.header,0xffffffff))
        self.rejected(lambda d:struct.pack_into('<I',d,self.header+4,0))
    def test_overlapping_names(self):
        first=struct.unpack_from('<I',self.data,self.header+4)[0]
        self.rejected(lambda d:struct.pack_into('<I',d,self.header+12,first))
    def test_payload_alignment_overlap_and_truncation(self):
        first=struct.unpack_from('<I',self.data,self.header+8)[0]
        second=struct.unpack_from('<I',self.data,self.header+16)[0]
        self.rejected(lambda d:struct.pack_into('<I',d,self.header+8,first+1))
        self.rejected(lambda d:struct.pack_into('<I',d,self.header+8,second))
        last_name=list(self.inventory)[-1]
        last=struct.unpack_from('<I',self.data,self.header+8*len(self.inventory))[0]
        with self.assertRaises(ValueError):resources(self.data[:self.header+last+self.inventory[last_name]['bytes']-1],self.inventory)
    def test_corrupt_payload(self):
        first=struct.unpack_from('<I',self.data,self.header+8)[0]
        self.rejected(lambda d:d.__setitem__(self.header+first+10,d[self.header+first+10]^1))
    def test_unsafe_path(self):
        first=struct.unpack_from('<I',self.data,self.header+4)[0]
        def mutate(d):d[self.header+first:self.header+first+13]=b'icudt79l/../x\0'
        self.rejected(mutate)
    def test_inventory_membership_and_digest(self):
        inventory=copy.deepcopy(self.inventory);inventory.pop(next(iter(inventory)))
        with self.assertRaises(ValueError):resources(self.data,inventory)
        inventory=copy.deepcopy(self.inventory);inventory['root.res']['sha256']='0'*64
        with self.assertRaises(ValueError):resources(self.data,inventory)
if __name__=='__main__':unittest.main()
