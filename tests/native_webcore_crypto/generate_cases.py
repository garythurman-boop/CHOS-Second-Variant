#!/usr/bin/env python3
"""Independent host hashlib references, including standard empty/abc vectors."""
import argparse, hashlib, json, pathlib
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);o=p.parse_args()
o.out.mkdir(parents=True,exist_ok=False)
algorithms=['sha1','sha224','sha256','sha384','sha512']
known_abc=['a9993e364706816aba3e25717850c26c9cd0d89d','23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7','ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad','cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7','ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f']
known_empty=['da39a3ee5e6b4b0d3255bfef95601890afd80709','d14a028c2a3a2bc9476102bb288234c415a2b01f828ea62ac5b3e42f','e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855','38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b','cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e']
rows=[]
for algorithm,name in enumerate(algorithms,1):
    assert hashlib.new(name,b'abc').hexdigest()==known_abc[algorithm-1]
    assert hashlib.new(name,b'').hexdigest()==known_empty[algorithm-1]
    for kind,length in [(0,n) for n in [0,1,55,56,63,64,65,111,112,127,128,129,1024,65537]]+[(1,3),(2,1000000)]:
        data=(bytes((i*73+(i>>8)*19+31)&255 for i in range(length)) if not kind else b'abc' if kind==1 else b'a'*length)
        digest=hashlib.new(name,data).digest()
        rows.append({'algorithm':algorithm,'name':name,'kind':kind,'length':length,'digest_bytes':len(digest),'expected_hex':digest.hex()})
header=['/* Generated independently by Python hashlib; not linked into the tested target. */','#ifndef CHOS_DIGEST_CASES_H','#define CHOS_DIGEST_CASES_H','#include <stddef.h>','#include <stdint.h>','struct digest_case {unsigned algorithm,kind;size_t length,digest_bytes;uint8_t expected[64];};','static const struct digest_case digest_cases[]={']
for r in rows:
    values=','.join('0x'+r['expected_hex'][i:i+2] for i in range(0,len(r['expected_hex']),2))
    header.append('{%u,%u,%u,%u,{%s}},'%(r['algorithm'],r['kind'],r['length'],r['digest_bytes'],values))
header+=['};','#define DIGEST_CASE_COUNT (sizeof(digest_cases)/sizeof(*digest_cases))','#endif']
(o.out/'cases.h').write_text('\n'.join(header)+'\n')
(o.out/'cases.json').write_text(json.dumps({'oracle':'Python hashlib/OpenSSL host only','algorithms':algorithms,'known_empty_and_abc_checked':True,'cases':rows},indent=2)+'\n')
