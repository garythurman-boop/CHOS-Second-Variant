"""Ephemeral test CA/leaf material, never a production trust bundle."""
import datetime
from cryptography import x509
from cryptography.hazmat.primitives import hashes,serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID

def certificate(subject,key,issuer,signer,begin,end,is_ca=False,server_auth=True):
 name=lambda s:x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,s)])
 year=lambda y:datetime.datetime(y,1,1)
 b=x509.CertificateBuilder().subject_name(name(subject)).issuer_name(name(issuer)).public_key(key.public_key()).serial_number(x509.random_serial_number()).not_valid_before(year(begin)).not_valid_after(year(end)).add_extension(x509.BasicConstraints(ca=is_ca,path_length=1 if is_ca else None),critical=True)
 if not is_ca:
  b=b.add_extension(x509.SubjectAlternativeName([x509.DNSName('fixture.chos.test')]),critical=False).add_extension(x509.ExtendedKeyUsage([x509.oid.ExtendedKeyUsageOID.SERVER_AUTH if server_auth else x509.oid.ExtendedKeyUsageOID.CLIENT_AUTH]),critical=False)
 return b.sign(signer,hashes.SHA256())

def fixture_roots(directory):
 rootkey=ec.generate_private_key(ec.SECP256R1())
 root=certificate('CHOS ephemeral test CA',rootkey,'CHOS ephemeral test CA',rootkey,2020,2040,True)
 (directory/'ca.der').write_bytes(root.public_bytes(serialization.Encoding.DER))
 otherkey=ec.generate_private_key(ec.SECP256R1())
 other=certificate('unrelated test CA',otherkey,'unrelated test CA',otherkey,2020,2040,True)
 (directory/'other.der').write_bytes(other.public_bytes(serialization.Encoding.DER))
 (directory/'bad.der').write_bytes(b'not a DER certificate')
 return rootkey
