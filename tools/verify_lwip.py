#!/usr/bin/env python3
"""Check exact pinned vendor inventory and hashes; no network or implicit update."""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[1]
manifest=json.loads((root/'docs/dependencies/lwip.json').read_text())
vendor=root/'third_party/lwip'
actual={str(p.relative_to(vendor)):hashlib.sha256(p.read_bytes()).hexdigest()
        for p in vendor.rglob('*') if p.is_file()}
if actual!=manifest['files_sha256']:
    raise SystemExit('Vendored lwIP inventory/hash mismatch; review upstream update and manifest together')
print(f"lwIP {manifest['version']} pinned {manifest['commit']}: {len(actual)} original files verified")
