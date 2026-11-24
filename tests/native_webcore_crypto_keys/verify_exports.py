#!/usr/bin/env python3
"""Independently decode maintained-library test exports and emit native byte oracles."""
import argparse
import hashlib
import json
import pathlib
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec,rsa

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--vectors',type=pathlib.Path,required=True)
p.add_argument('--exports',type=pathlib.Path,required=True)
p.add_argument('--out',type=pathlib.Path,required=True)
o=p.parse_args()
out=o.out.resolve()
out.mkdir(parents=True,exist_ok=False)
m=json.loads((o.vectors/'vectors.json').read_text())
sha=lambda b:hashlib.sha256(b).hexdigest()
lines=['/* TEST-ONLY private DER, independently decoded with Python cryptography. */',
       '#ifndef CHOS_KEY_EXPORT_ORACLE_H','#define CHOS_KEY_EXPORT_ORACLE_H',
       '#include <stdint.h>','#include <stddef.h>',
       'struct KeyExportOracle { const uint8_t *data; size_t size; };']
records=[];reports=[]
for i,row in enumerate(m['cases']):
    file=o.exports/(row['name']+'.export.pkcs8.der');data=file.read_bytes()
    key=serialization.load_der_private_key(data,password=None)
    assert key.key_size==row['bits'],row['name']
    n=key.private_numbers()
    if isinstance(key,rsa.RSAPrivateKey):
        assert row['family']==1
        actual=dict(n=n.public_numbers.n,e=n.public_numbers.e,d=n.d,p=n.p,q=n.q,
                    dp=n.dmp1,dq=n.dmq1,qi=n.iqmp)
    else:
        assert isinstance(key,ec.EllipticCurvePrivateKey) and row['family']==2
        actual=dict(x=n.public_numbers.x,y=n.public_numbers.y,d=n.private_value)
    for name,value in actual.items():
        assert value==int(row['fields'][name]['hex'],16),(row['name'],name)
    spki=key.public_key().public_bytes(serialization.Encoding.DER,
                                      serialization.PublicFormat.SubjectPublicKeyInfo)
    assert sha(spki)==row['fields']['spki']['sha256'],row['name']
    name='key_private_export_'+str(i)
    lines.append('static const uint8_t '+name+'[] = {'+','.join(map(str,data))+'};')
    records.append('{'+name+',sizeof('+name+')}')
    reports.append({'name':row['name'],'bytes':len(data),'sha256':sha(data),
                    'independent_private_fields':list(actual),'public_spki_sha256':sha(spki)})
lines+=['static const struct KeyExportOracle key_export_oracles[] = {'+','.join(records)+'};',
        '#define KEY_EXPORT_ORACLE_COUNT (sizeof(key_export_oracles)/sizeof(*key_export_oracles))','#endif','']
header=out/'key-export-oracle.h';header.write_text('\n'.join(lines));header.chmod(0o600)
(out/'export-oracle.json').write_text(json.dumps({'schema':1,'status':'PASS',
    'test_only_private_keys':True,'production_use':False,
    'vectors_manifest_sha256':sha((o.vectors/'vectors.json').read_bytes()),
    'generator_sha256':sha(pathlib.Path(__file__).read_bytes()),
    'exports':reports,'generated_header_sha256':sha(header.read_bytes())},indent=2)+'\n')
print('Independently verified',len(records),'actual exported private key structures and SPKI identities')
