#!/usr/bin/env python3
"""Generate isolated, disposable keys and run the real certificate verifier."""
import pathlib,subprocess,sys,tempfile
from tls_fixture_certs import fixture_roots,certificate,ec,serialization
with tempfile.TemporaryDirectory(prefix='chos-x509-test-') as d:
 p=pathlib.Path(d);key=fixture_roots(p);leafkey=ec.generate_private_key(ec.SECP256R1())
 leaf=certificate('fixture.chos.test',leafkey,'CHOS ephemeral test CA',key,2029,2031)
 (p/'leaf.der').write_bytes(leaf.public_bytes(serialization.Encoding.DER))
 subprocess.run([sys.argv[1],str(p/'ca.der'),str(p/'other.der'),str(p/'leaf.der')],check=True)
