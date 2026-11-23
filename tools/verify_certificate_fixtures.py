#!/usr/bin/env python3
"""Verify pinned public synthetic DER and independent metadata, never trust roots."""
import hashlib
import ipaddress
import json
import pathlib
from cryptography import x509
from cryptography.hazmat.primitives import serialization
from cryptography.x509.oid import NameOID

ROOT = pathlib.Path(__file__).resolve().parents[1]
manifest = json.loads((ROOT / 'tests/native_certificate_parser/fixtures-manifest.json').read_text())
base = ROOT / manifest['root']
assert not base.is_symlink()
assert {f.name for f in base.iterdir()} == set(manifest['files'])
for name, wanted in manifest['files'].items():
    f = base / name
    assert f.is_file() and not f.is_symlink() and f.resolve().is_relative_to(base.resolve())
    assert hashlib.sha256(f.read_bytes()).hexdigest() == wanted, name
vectors = json.loads((base / 'vectors.json').read_text())
assert vectors['private_keys_retained'] is False
for row in vectors['cases']:
    der = (base / (row['name'] + '.der')).read_bytes()
    cert = x509.load_der_x509_certificate(der)
    assert cert.public_bytes(serialization.Encoding.DER) == der
    assert len(der) == row['bytes'] and hashlib.sha256(der).hexdigest() == row['sha256']
    subject = None
    for oid in (NameOID.COMMON_NAME, NameOID.ORGANIZATION_NAME):
        values = cert.subject.get_attributes_for_oid(oid)
        if values:
            subject = values[0].value
            break
    if subject is None:
        subject = list(cert.subject)[0].value
    assert subject.encode('utf-8').hex() == row['subject_utf8_hex']
    assert int(cert.not_valid_before_utc.timestamp()) == row['valid_from']
    assert int(cert.not_valid_after_utc.timestamp()) == row['valid_until']
    digest = cert.signature_hash_algorithm
    assert bool(digest and digest.name == 'sha1') == row['sha1']
    sans = []
    try:
        for san in cert.extensions.get_extension_for_class(x509.SubjectAlternativeName).value:
            if isinstance(san, x509.DNSName):
                sans.append({'bytes_hex': san.value.encode('ascii').hex(), 'type': 2})
            elif isinstance(san, x509.IPAddress):
                sans.append({'bytes_hex': ipaddress.ip_address(san.value).packed.hex(), 'type': 7})
    except x509.ExtensionNotFound:
        pass
    assert sans == row['sans']
print('Certificate fixtures: 7 exact public DER cases and independent metadata verified')
