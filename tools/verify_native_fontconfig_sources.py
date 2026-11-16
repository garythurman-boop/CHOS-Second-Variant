#!/usr/bin/env python3
"""Verify original Fontconfig files and the retained authentication receipts.

The signed-index-to-original-archive comparison was performed during import and
is recorded in source-chain-verification.json. This offline inventory check does
not claim to download or reauthenticate an upstream release on every build.
"""
from pathlib import Path
import hashlib
import json


ROOT = Path(__file__).resolve().parents[1]
manifest = json.loads((ROOT / "docs/dependencies/native-fontconfig.json").read_text())
base = ROOT / manifest["root"]
assert not base.is_symlink() and base.resolve().is_relative_to(ROOT)
entries = list(base.rglob("*"))
assert not any(path.is_symlink() for path in entries), "Fontconfig symlink added"
assert {path.relative_to(base).as_posix() for path in entries if path.is_file()} == set(
    manifest["files"]
), "Original Fontconfig inventory changed"
for name, expected in manifest["files"].items():
    path = base / name
    assert path.resolve().is_relative_to(base.resolve()), name
    assert hashlib.sha256(path.read_bytes()).hexdigest() == expected, name
for name, expected in manifest["authentication_material"].items():
    path = ROOT / name
    assert not path.is_symlink() and path.resolve().is_relative_to(ROOT), name
    assert hashlib.sha256(path.read_bytes()).hexdigest() == expected, name
print("Fontconfig", manifest["version"], len(manifest["files"]), "original files verified")
print("Fontconfig retained source-authentication receipts verified")
