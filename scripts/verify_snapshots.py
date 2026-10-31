#!/usr/bin/env python3
"""Check copied file inventory and exact bytes against recorded upstream hashes."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
manifest = json.loads((root / "provenance.json").read_text())
expected = {entry["path"] for entry in manifest["files"]}
actual = {str(p.relative_to(root)) for p in (root / "sources").rglob("*") if p.is_file()}
if actual != expected:
    raise SystemExit(f"Inventory mismatch: missing={expected-actual}, extra={actual-expected}")
for entry in manifest["files"]:
    data = (root / entry["path"]).read_bytes()
    data.decode("utf-8")
    if b"\0" in data:
        raise SystemExit(f"Unexpected binary content: {entry['path']}")
    blob = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
    if blob != entry["git_blob"] or hashlib.sha256(data).hexdigest() != entry["sha256"]:
        raise SystemExit(f"Hash mismatch: {entry['path']}")
print(f"Verified {len(expected)} text-only upstream snapshots; inventory and hashes match.")
