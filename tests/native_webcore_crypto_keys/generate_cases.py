#!/usr/bin/env python3
"""Generate disposable OpenSSL keys and independent key-number test oracles.

The output contains TEST-ONLY PRIVATE KEYS. Keep it in a fresh evidence/build
directory, never a production key store. No random seed or crypto is invented.
For the generated input files, C header emission is deterministic. Consumers
should reuse the same generated header/input inventory for host and target runs.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess

import cryptography
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec, rsa
from cryptography.hazmat.backends.openssl.backend import backend


FAMILY_RSA = 1
FAMILY_EC = 2
FORMAT_SPKI = 1
FORMAT_PKCS8 = 2
FIELD_NAMES = ("spki", "pkcs8", "traditional_private", "pkcs1_public",
               "raw_public", "n", "e", "d", "p", "q", "dp", "dq", "qi", "x", "y")


def sha(data):
    return hashlib.sha256(data).hexdigest()


def integer_bytes(value, width=None):
    return value.to_bytes(width or max(1, (value.bit_length() + 7) // 8), "big")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=pathlib.Path, required=True,
                        help="A new test-only evidence directory; existing paths are refused")
    parser.add_argument("--openssl", default="openssl")
    options = parser.parse_args()
    out = options.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    openssl = shutil.which(options.openssl)
    if not openssl:
        parser.error("A real OpenSSL executable is required")
    commands = []

    def run(label, args):
        command = [openssl, *map(str, args)]
        result = subprocess.run(command, capture_output=True)
        (out / (label + ".log")).write_bytes(result.stdout + result.stderr)
        commands.append({"label": label, "argv": command, "exit_code": result.returncode})
        if result.returncode:
            raise RuntimeError("OpenSSL command failed: " + label)
        return result.stdout

    version = run("openssl-version", ["version", "-a"]).decode("utf-8")
    rows = []
    source_files = []
    for label, family, curve_bits, bits, parameters in (
        ("rsa2048", FAMILY_RSA, 0, 2048,
         ["-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048",
          "-pkeyopt", "rsa_keygen_pubexp:65537"]),
        ("p256", FAMILY_EC, 256, 256,
         ["-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:prime256v1",
          "-pkeyopt", "ec_param_enc:named_curve"]),
        ("p384", FAMILY_EC, 384, 384,
         ["-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:secp384r1",
          "-pkeyopt", "ec_param_enc:named_curve"]),
        ("p521", FAMILY_EC, 521, 521,
         ["-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:secp521r1",
          "-pkeyopt", "ec_param_enc:named_curve"]),
    ):
        pem = out / (label + ".test-private.pem")
        spki = out / (label + ".spki.der")
        pkcs8 = out / (label + ".pkcs8.der")
        traditional = out / (label + (".pkcs1-private.der" if family == FAMILY_RSA else ".sec1-private.der"))
        run(label + "-generate", ["genpkey", *parameters, "-out", pem])
        pem.chmod(0o600)
        run(label + "-spki", ["pkey", "-in", pem, "-pubout", "-outform", "DER", "-out", spki])
        run(label + "-pkcs8", ["pkcs8", "-topk8", "-nocrypt", "-in", pem,
                               "-outform", "DER", "-out", pkcs8])
        command = ["rsa", "-traditional"] if family == FAMILY_RSA else ["ec"]
        run(label + "-traditional", [*command, "-in", pem, "-outform", "DER", "-out", traditional])
        pkcs8.chmod(0o600)
        traditional.chmod(0o600)
        run(label + "-spki-asn1", ["asn1parse", "-inform", "DER", "-in", spki])
        run(label + "-pkcs8-asn1", ["asn1parse", "-inform", "DER", "-in", pkcs8])
        key = serialization.load_pem_private_key(pem.read_bytes(), password=None)
        loaded_private = serialization.load_der_private_key(pkcs8.read_bytes(), password=None)
        loaded_public = serialization.load_der_public_key(spki.read_bytes())
        assert key.key_size == loaded_private.key_size == loaded_public.key_size == bits
        assert key.public_key().public_numbers() == loaded_private.public_key().public_numbers() == loaded_public.public_numbers()
        assert loaded_public.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo) == spki.read_bytes()
        assert key.private_bytes(serialization.Encoding.DER, serialization.PrivateFormat.PKCS8,
                                 serialization.NoEncryption()) == pkcs8.read_bytes()
        assert key.private_bytes(serialization.Encoding.DER, serialization.PrivateFormat.TraditionalOpenSSL,
                                 serialization.NoEncryption()) == traditional.read_bytes()
        fields = {name: b"" for name in FIELD_NAMES}
        fields.update(spki=spki.read_bytes(), pkcs8=pkcs8.read_bytes(),
                      traditional_private=traditional.read_bytes())
        if family == FAMILY_RSA:
            assert isinstance(key, rsa.RSAPrivateKey)
            private = key.private_numbers()
            public = private.public_numbers
            for name, value in (("n", public.n), ("e", public.e), ("d", private.d),
                                ("p", private.p), ("q", private.q), ("dp", private.dmp1),
                                ("dq", private.dmq1), ("qi", private.iqmp)):
                fields[name] = integer_bytes(value)
            public_pkcs1 = out / (label + ".pkcs1-public.der")
            run(label + "-pkcs1-public", ["rsa", "-in", pem, "-RSAPublicKey_out",
                                          "-outform", "DER", "-out", public_pkcs1])
            fields["pkcs1_public"] = public_pkcs1.read_bytes()
            assert loaded_public.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.PKCS1) == fields["pkcs1_public"]
            source_files.append(public_pkcs1.name)
        else:
            assert isinstance(key, ec.EllipticCurvePrivateKey)
            private = key.private_numbers()
            public = private.public_numbers
            width = (bits + 7) // 8
            fields["x"] = integer_bytes(public.x, width)
            fields["y"] = integer_bytes(public.y, width)
            fields["d"] = integer_bytes(private.private_value, width)
            fields["raw_public"] = loaded_public.public_bytes(serialization.Encoding.X962,
                                                               serialization.PublicFormat.UncompressedPoint)
            assert fields["raw_public"] == b"\x04" + fields["x"] + fields["y"]
        rows.append({"name": label, "family": family, "curve_bits": curve_bits,
                     "bits": bits, "fields": fields})
        source_files.extend([pem.name, spki.name, pkcs8.name, traditional.name])

    lines = ["/* TEST-ONLY PRIVATE KEY VECTORS. Never use these as production keys. */",
             "#ifndef CHOS_CRYPTO_KEY_CASES_H", "#define CHOS_CRYPTO_KEY_CASES_H",
             "#include <stddef.h>", "#include <stdint.h>",
             "enum { KEY_FIXTURE_RSA=1, KEY_FIXTURE_EC=2, KEY_FIXTURE_SPKI=1, KEY_FIXTURE_PKCS8=2 };",
             "struct KeyBytes { const uint8_t *data; size_t size; };",
             "struct CryptoKeyCase { const char *name; unsigned family,curve_bits,bits; " +
             "; ".join("struct KeyBytes " + name for name in FIELD_NAMES) + "; };",
             "struct RejectedKeyCase { const char *name; struct KeyBytes input; unsigned format,family,curve_bits; };" ]
    arrays = {}

    def array(label, data):
        if not data:
            return "{NULL,0}"
        # Reuse exact duplicate input arrays. Fixed bytes determine output.
        if data not in arrays:
            arrays[data] = label
            lines.append("static const uint8_t " + label + "[] = {" +
                         ",".join(str(byte) for byte in data) + "};")
        name = arrays[data]
        return "{" + name + ",sizeof(" + name + ")}"

    case_records = []
    report_cases = []
    for index, row in enumerate(rows):
        case_records.append('{"%s",%u,%u,%u,%s}' % (row["name"], row["family"], row["curve_bits"], row["bits"],
            ",".join(array("key_%u_%s" % (index, name), row["fields"][name]) for name in FIELD_NAMES)))
        report_cases.append({"name": row["name"], "family": row["family"], "curve_bits": row["curve_bits"],
            "bits": row["bits"], "fields": {name: {"bytes": len(data), "sha256": sha(data),
                **({"hex": data.hex()} if name in ("n", "e", "d", "p", "q", "dp", "dq", "qi", "x", "y") else {})}
                for name, data in row["fields"].items()}})

    bad = []
    for row in rows:
        label, fields, family, curve = row["name"], row["fields"], row["family"], row["curve_bits"]
        for field, fmt in (("spki", FORMAT_SPKI), ("pkcs8", FORMAT_PKCS8)):
            data = fields[field]
            bad.extend([(label + "_" + field + "_empty", b"", fmt, family, curve),
                        (label + "_" + field + "_truncated", data[:-1], fmt, family, curve),
                        (label + "_" + field + "_trailing", data + b"\x00", fmt, family, curve),
                        (label + "_" + field + "_wrong_outer_tag", b"\x31" + data[1:], fmt, family, curve)])
        bad.extend([(label + "_private_as_public", fields["pkcs8"], FORMAT_SPKI, family, curve),
                    (label + "_public_as_private", fields["spki"], FORMAT_PKCS8, family, curve),
                    (label + "_traditional_is_not_pkcs8", fields["traditional_private"], FORMAT_PKCS8, family, curve)])
        if family == FAMILY_RSA:
            bad.extend([(label + "_pkcs1_is_not_spki", fields["pkcs1_public"], FORMAT_SPKI, family, curve),
                        (label + "_wrong_family", fields["spki"], FORMAT_SPKI, FAMILY_EC, 256)])
        else:
            bad.extend([(label + "_wrong_family", fields["spki"], FORMAT_SPKI, FAMILY_RSA, 0),
                        (label + "_wrong_curve", fields["spki"], FORMAT_SPKI, family, 384 if curve != 384 else 256)])
    rejected_records = []
    report_bad = []
    for index, (label, data, fmt, family, curve) in enumerate(bad):
        rejected_records.append('{"%s",%s,%u,%u,%u}' % (label, array("bad_key_%u" % index, data), fmt, family, curve))
        report_bad.append({"name": label, "bytes": len(data), "sha256": sha(data),
                           "format": fmt, "family": family, "curve_bits": curve})
    lines.extend(["static const struct CryptoKeyCase key_cases[] = {" + ",\n".join(case_records) + "};",
                  "static const struct RejectedKeyCase rejected_key_cases[] = {" + ",\n".join(rejected_records) + "};",
                  "#define KEY_CASE_COUNT (sizeof(key_cases)/sizeof(*key_cases))",
                  "#define REJECTED_KEY_CASE_COUNT (sizeof(rejected_key_cases)/sizeof(*rejected_key_cases))",
                  "#endif", ""])
    header = out / "key-cases.h"
    header.write_text("\n".join(lines))
    header.chmod(0o600)
    manifest = {"schema": 1, "test_only_private_keys": True, "production_use": False,
        "generator_sha256": sha(pathlib.Path(__file__).read_bytes()), "openssl_executable": openssl,
        "openssl_version": version, "cryptography_version": cryptography.__version__,
        "cryptography_provider": backend.openssl_version_text(), "commands": commands,
        "cases": report_cases, "rejected_cases": report_bad,
        "input_files_sha256": {name: sha((out / name).read_bytes()) for name in sorted(source_files)},
        "generated_header": {"name": header.name, "sha256": sha(header.read_bytes()), "bytes": header.stat().st_size},
        "oracle": "Keys generated and DER converted by OpenSSL CLI; all accepted formats parsed and public/private numbers independently checked by Python cryptography. Integer fields are big-endian, RSA minimally sized, EC fixed to curve width.",
        "export_comparison": "Canonical SPKI is byte comparable. A valid PKCS8 export can use a different valid inner SEC1 parameter representation; independently decode its key fields before requiring byte identity.",
        "determinism": "For these generated input files, array and metadata emission is deterministic. Reuse this exact generated header and inventory for host and native consumers; fresh --out creates fresh random test keys."}
    (out / "vectors.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Generated", len(rows), "independently checked key cases and", len(bad), "refusal vectors; TEST-ONLY private keys remain in", out)


if __name__ == "__main__":
    main()
