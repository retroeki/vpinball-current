#!/usr/bin/env python3
"""
Extract scripts, element names and companion .vbs files from a folder of zipped
tables (one or more .vpx per zip), without unpacking the archives to disk.

Usage:
    python tests/scriptpatcher/extract_vpx_zips.py <zip_dir> <out_dir> [-j N]

Writes, per .vpx found (searched recursively under <zip_dir>):
    <out_dir>/scripts_raw/<table>.vbs          as extract_vpx_scripts.py
    <out_dir>/items/<table>.items.tsv          as extract_vpx_items.py
    <out_dir>/companions/<table>/...           every .vbs in the zip, at its path
                                               relative to the .vpx's folder
    <out_dir>/manifest.tsv                     table, zip, member, script sha1
<table> is the .vpx file name; a name already taken gets " [2]", " [3]", ...
Feed the folders to wine_harness/corpus_check.py (--raw, --items, --companions).
Requires `olefile` (pip install olefile).
"""
import concurrent.futures
import hashlib
import os
import posixpath
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_vpx_items import extract_items  # noqa: E402
from extract_vpx_scripts import extract_code  # noqa: E402


def scan_zip(path):
    """[(member, code, item lines, {relative companion path: bytes})] for each .vpx in one zip."""
    out = []
    with zipfile.ZipFile(path) as z:
        names = [i.filename for i in z.infolist() if not i.is_dir()]
        for member in (n for n in names if n.lower().endswith(".vpx")):
            data = z.read(member)
            code = extract_code(data)
            items = extract_items(data)
            folder = posixpath.dirname(member)
            companions = {}
            for n in names:
                if n.lower().endswith(".vbs") and (not folder or n.startswith(folder + "/")):
                    companions[posixpath.relpath(n, folder or ".")] = z.read(n)
            out.append((member, code, items, companions))
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-j")]
    jobs = next((int(a[2:]) for a in sys.argv[1:] if a.startswith("-j")), 4)
    if len(args) != 2:
        print(__doc__)
        return 2
    src, out = args
    for sub in ("scripts_raw", "items", "companions"):
        os.makedirs(os.path.join(out, sub), exist_ok=True)
    zips = sorted(os.path.join(d, f) for d, _, fs in os.walk(src) for f in fs if f.lower().endswith(".zip"))
    taken, manifest, skipped = set(), [], []
    with concurrent.futures.ProcessPoolExecutor(jobs) as pool:
        for zpath, fut in [(z, pool.submit(scan_zip, z)) for z in zips]:
            try:
                found = fut.result()
            except Exception as exc:  # noqa: BLE001 - report and continue
                skipped.append((zpath, f"error: {exc}"))
                continue
            if not found:
                skipped.append((zpath, "no .vpx"))
            for member, code, items, companions in found:
                stem = os.path.splitext(posixpath.basename(member))[0]
                table, n = stem, 1
                while table.lower() in taken:
                    n += 1
                    table = f"{stem} [{n}]"
                taken.add(table.lower())
                if not code:
                    skipped.append((f"{zpath} :: {member}", "no CODE record"))
                    continue
                # newline="" keeps the script's own CRLFs (see extract_vpx_scripts.py).
                with open(os.path.join(out, "scripts_raw", table + ".vbs"), "w", encoding="latin-1", newline="") as o:
                    o.write(code)
                with open(os.path.join(out, "items", table + ".items.tsv"), "w", encoding="utf-8", newline="\n") as o:
                    o.write("\n".join(items) + "\n")
                for rel, data in companions.items():
                    dest = os.path.join(out, "companions", table, *rel.split("/"))
                    os.makedirs(os.path.dirname(dest), exist_ok=True)
                    with open(dest, "wb") as o:
                        o.write(data)
                sha = hashlib.sha1(code.encode("latin-1")).hexdigest()
                manifest.append((table, os.path.relpath(zpath, src), member, sha, str(len(companions))))
    with open(os.path.join(out, "manifest.tsv"), "w", encoding="utf-8", newline="\n") as o:
        o.write("table\tzip\tmember\tscript_sha1\tcompanion_vbs\n")
        o.writelines("\t".join(r) + "\n" for r in manifest)
    print(f"Extracted {len(manifest)} tables from {len(zips)} zips to {out}")
    for what, why in skipped:
        print(f"  SKIP {what}: {why}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
