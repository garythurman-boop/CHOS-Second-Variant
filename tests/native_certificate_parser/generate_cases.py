#!/usr/bin/env python3
"""Generate disposable DER metadata vectors with an independent host library.

Keys remain in temporary storage; generated DER is test input, never trust roots.
SHA1 is deliberately used in one negative-inspection fixture, not TLS policy.
"""
import argparse
import datetime
import hashlib
import ipaddress
import json
import pathlib
import subprocess
import tempfile
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, rsa, ed25519
from cryptography.x509.oid import NameOID

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=pathlib.Path, required=True)
o = p.parse_args()
o.out.mkdir(parents=True, exist_ok=True)
UTC = datetime.timezone.utc
before = datetime.datetime(2020, 2, 29, 12, 34, 56, tzinfo=UTC)
after = datetime.datetime(2051, 1, 2, 3, 4, 5, tzinfo=UTC)
name = lambda attrs: x509.Name([x509.NameAttribute(oid, text) for oid, text in attrs])
rsa_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
ec_key = ec.generate_private_key(ec.SECP384R1())


def certificate(subject, key, algorithm, san=None):
    b = (x509.CertificateBuilder().subject_name(subject).issuer_name(subject)
         .public_key(key.public_key()).serial_number(x509.random_serial_number())
         .not_valid_before(before).not_valid_after(after)
         .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True))
    if san is not None:
        b = b.add_extension(x509.SubjectAlternativeName(san), critical=False)
    return b.sign(key, algorithm)


items = []
def add(label, cert, subject, expected=True):
    items.append((label, cert, subject, expected))

add('rsa256', certificate(name([(NameOID.COUNTRY_NAME, 'US'), (NameOID.ORGANIZATION_NAME, 'Example Org'),
    (NameOID.COMMON_NAME, 'metadata.chos.test')]), rsa_key, hashes.SHA256(), [
    x509.DNSName('metadata.chos.test'), x509.DNSName('*.example.test'),
    x509.IPAddress(ipaddress.ip_address('127.0.0.1')),
    x509.IPAddress(ipaddress.ip_address('2001:db8::7')),
    x509.RFC822Name('ignored@example.test')]), 'metadata.chos.test')
add('ec384_utf8', certificate(name([(NameOID.COMMON_NAME, 'Café Ω')]), ec_key, hashes.SHA384(), [
    x509.DNSName('xn--caf-dma.example'), x509.IPAddress(ipaddress.ip_address('::1'))]), 'Café Ω')
add('organization', certificate(name([(NameOID.COUNTRY_NAME, 'GB'), (NameOID.ORGANIZATION_NAME, 'CHOS Org')]),
    ec_key, hashes.SHA256()), 'CHOS Org')
add('country_fallback', certificate(name([(NameOID.COUNTRY_NAME, 'US')]), rsa_key, hashes.SHA256()), 'US')
add('embedded_nul_subject', certificate(name([(NameOID.COMMON_NAME, 'A\x00B')]), ec_key, hashes.SHA256()), 'A\x00B')
with tempfile.TemporaryDirectory(prefix='chos-cert-sha1-') as temp:
    d = pathlib.Path(temp)
    (d/'key.pem').write_bytes(rsa_key.private_bytes(serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    req = x509.CertificateSigningRequestBuilder().subject_name(name([(NameOID.COMMON_NAME, 'old-signature.test')])).sign(rsa_key, hashes.SHA256())
    (d/'request.pem').write_bytes(req.public_bytes(serialization.Encoding.PEM))
    subprocess.run(['openssl', 'x509', '-req', '-in', str(d/'request.pem'), '-signkey', str(d/'key.pem'),
        '-sha1', '-days', '30', '-set_serial', '7', '-outform', 'DER', '-out', str(d/'sha1.der')], check=True,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    add('rsa_sha1', x509.load_der_x509_certificate((d/'sha1.der').read_bytes()), 'old-signature.test')
ed_key = ed25519.Ed25519PrivateKey.generate()
add('unsupported_ed25519', certificate(name([(NameOID.COMMON_NAME, 'unsupported.test')]), ed_key, None),
    'unsupported.test', False)

lines = ['/* Independently generated test DER/metadata; contains no private keys. */',
    '#ifndef CHOS_CERTIFICATE_CASES_H', '#define CHOS_CERTIFICATE_CASES_H', '#include <stdint.h>', '#include <stddef.h>',
    'struct CertificateSAN {const unsigned char *data;size_t size;int type;};',
    'struct CertificateCase {const char *name;const unsigned char *der;size_t size;const unsigned char *subject;size_t subject_size;int64_t from,until;int sha1,accepted;const struct CertificateSAN *sans;size_t san_count;};']
records = []
report = []
def data_array(label, data):
    lines.append('static const unsigned char '+label+'[] = {' + ','.join(str(x) for x in data) + '};')
for index, (label, cert, subject, accepted) in enumerate(items):
    der = cert.public_bytes(serialization.Encoding.DER)
    (o.out/(label+'.der')).write_bytes(der)
    data_array('der_'+str(index), der)
    encoded_subject = subject.encode('utf-8')
    data_array('subject_'+str(index), encoded_subject)
    sans = []
    try:
        for san in cert.extensions.get_extension_for_class(x509.SubjectAlternativeName).value:
            if isinstance(san, x509.DNSName): sans.append((san.value.encode('ascii'), 2))
            elif isinstance(san, x509.IPAddress): sans.append((san.value.packed, 7))
    except x509.ExtensionNotFound:
        pass
    for n, (data, kind) in enumerate(sans): data_array('san_'+str(index)+'_'+str(n), data)
    if sans:
        lines.append('static const struct CertificateSAN sans_'+str(index)+'[] = {' +
            ','.join('{san_%d_%d,%d,%d}'%(index,n,len(data),kind) for n,(data,kind) in enumerate(sans)) + '};')
    start = int(cert.not_valid_before_utc.timestamp()); end = int(cert.not_valid_after_utc.timestamp())
    sha1 = int(cert.signature_hash_algorithm is not None and cert.signature_hash_algorithm.name == 'sha1')
    records.append('{"%s",der_%d,sizeof(der_%d),subject_%d,sizeof(subject_%d),INT64_C(%d),INT64_C(%d),%d,%d,%s,%d}' %
        (label,index,index,index,index,start,end,sha1,int(accepted),'sans_'+str(index) if sans else 'NULL',len(sans)))
    report.append({'name':label,'sha256':hashlib.sha256(der).hexdigest(),'bytes':len(der),'subject_utf8_hex':encoded_subject.hex(),
        'valid_from':start,'valid_until':end,'sha1':bool(sha1),'accepted_profile':accepted,
        'sans':[{'bytes_hex':data.hex(),'type':kind} for data,kind in sans]})
pem = items[0][1].public_bytes(serialization.Encoding.PEM)
data_array('certificate_pem', pem)
lines.extend(['static const struct CertificateCase certificate_cases[] = {'+',\n'.join(records)+'};',
    '#define CERTIFICATE_CASE_COUNT (sizeof(certificate_cases)/sizeof(*certificate_cases))', '#endif', ''])
(o.out/'certificate-cases.h').write_text('\n'.join(lines))
(o.out/'vectors.json').write_text(json.dumps({'generator':'cryptography X509 metadata plus actual OpenSSL SHA1-signed fixture',
    'cases':report,'private_keys_retained':False},indent=2)+'\n')
print('Generated', len(items), 'certificate cases without retained private keys')
