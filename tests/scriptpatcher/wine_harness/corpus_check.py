#!/usr/bin/env python3
"""Compile (or run) a corpus of table scripts with the shipped Wine engine, before and after patching.

  corpus_check.py --raw RAW_DIR [--patched PATCHED_DIR] [--out results.tsv] [--baseline old.tsv]
                  [--host SCRIPTS_DIR [--items ITEMS_DIR] [--companions DIR] [--relaunch]]
                  [--sample N [--seed S]]

--host runs each script against vbs_check's stand-in table (top-level code, then
<table>_Init, with GetTextFile serving SCRIPTS_DIR) instead of compiling only.
--items gives it each table's real element names (extract_vpx_items.py), so a
name the .vpx lacks is undefined, as on device.
--companions DIR: DIR/<table>/ holds the files shipped next to the .vpx; GetTextFile
serves them after SCRIPTS_DIR, as on device (extract_vpx_zips.py writes them).
--relaunch also runs <table>_Exit (high score save) and starts the table a second
time on the same UserDirectory; an error code like 62@relaunch-init is from that
second start (or @exit from the save).
--sample picks N scripts at random (reproducible with --seed).

RAW_DIR holds the unpatched scripts (tests/scriptpatcher/extract_vpx_scripts.py),
PATCHED_DIR the same file names after the real patcher (corpus_harness/patch_corpus).
Every script gets the device's final text pass (non-ASCII -> space, CRLF) by vbs_check.

Reports:
  patcher broke   compiles raw, fails patched     (a patcher regression, always fix)
  patcher fixed   fails raw, compiles patched     (rules that earn their keep)
  wine rejects    fails both                      (engine strictness or a broken table)
With --baseline, also lists every table whose raw or patched result got worse.
Exit status 1 when anything is in "patcher broke" or worse than the baseline.
"""
import argparse
import collections
import random
import concurrent.futures
import csv
import os
import subprocess
import sys
import tempfile


def compile_one(binary, path, host=None, items=None, companions=None, relaunch=False):
    if not os.path.exists(path):
        return ('missing', '', '')
    cmd = [binary] + ([f'--host={host}'] if host else []) + ([f'--items={items}'] if items else [])
    if relaunch:
        cmd.append('--relaunch')
    if companions:
        table_dir = os.path.join(companions, os.path.splitext(os.path.basename(path))[0])
        if os.path.isdir(table_dir):
            cmd.append(f'--table-dir={table_dir}')
    cmd.append(path)
    try:
        # Table scripts write files relative to the working directory too.
        with tempfile.TemporaryDirectory() as cwd:
            out = subprocess.run(cmd, capture_output=True, text=True, timeout=60, cwd=cwd).stdout
    except subprocess.TimeoutExpired:
        return ('timeout', '', '')
    line = next((l for l in out.splitlines() if l.startswith(('OK\t', 'FAIL\t'))), '')
    if line.startswith('OK'):
        return ('ok', '', '')
    if line.startswith('FAIL'):
        f = line.split('\t')
        return ('fail', f[4] if f[1] in ('compile', 'run', 'init') else f'{f[4]}@{f[1]}', f[2])
    return ('crash', '', '')


def load_tsv(path):
    with open(path, newline='', encoding='utf-8') as f:
        return {r['name']: r for r in csv.DictReader(f, delimiter='\t')}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--raw', required=True)
    ap.add_argument('--patched')
    ap.add_argument('--out', default='corpus_results.tsv')
    ap.add_argument('--baseline')
    ap.add_argument('--bin', default=os.environ.get('VBS_CHECK', os.path.expanduser('~/vbs_check_build/vbs_check')))
    ap.add_argument('-j', type=int, default=os.cpu_count() or 4)
    ap.add_argument('--host')
    ap.add_argument('--items')
    ap.add_argument('--companions')
    ap.add_argument('--relaunch', action='store_true')
    ap.add_argument('--sample', type=int)
    ap.add_argument('--seed', type=int, default=0)
    args = ap.parse_args()
    # vbs_check runs in a temporary working directory: make every path absolute.
    for key in ('raw', 'patched', 'bin', 'host', 'items', 'companions'):
        if getattr(args, key):
            setattr(args, key, os.path.abspath(getattr(args, key)))

    names = sorted(n for n in os.listdir(args.raw) if n.lower().endswith('.vbs'))
    if args.sample:
        names = sorted(random.Random(args.seed).sample(names, min(args.sample, len(names))))
    jobs = {}
    with concurrent.futures.ThreadPoolExecutor(args.j) as pool:
        for n in names:
            jobs[(n, 'raw')] = pool.submit(compile_one, args.bin, os.path.join(args.raw, n), args.host, args.items,
                                       args.companions, args.relaunch)
            if args.patched:
                jobs[(n, 'patched')] = pool.submit(compile_one, args.bin, os.path.join(args.patched, n), args.host,
                                               args.items, args.companions, args.relaunch)
        rows = []
        for n in names:
            r = jobs[(n, 'raw')].result()
            p = jobs[(n, 'patched')].result() if args.patched else ('', '', '')
            rows.append({'name': n, 'raw': r[0], 'raw_code': r[1], 'raw_line': r[2],
                         'patched': p[0], 'patched_code': p[1], 'patched_line': p[2]})

    with open(args.out, 'w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()), delimiter='\t')
        w.writeheader()
        w.writerows(rows)

    raw_ok = sum(r['raw'] == 'ok' for r in rows)
    verb = 'runs' if args.host else 'compiles'
    print(f'{len(rows)} scripts: raw {verb} {raw_ok}, fails {len(rows) - raw_ok}', end='')
    if args.patched:
        pat_ok = sum(r['patched'] == 'ok' for r in rows)
        print(f'; patched {verb} {pat_ok}, fails {len(rows) - pat_ok}')
    else:
        print()

    bad = 0
    if args.patched:
        broke = [r for r in rows if r['raw'] == 'ok' and r['patched'] != 'ok']
        fixed = [r for r in rows if r['raw'] != 'ok' and r['patched'] == 'ok']
        both = [r for r in rows if r['raw'] != 'ok' and r['patched'] != 'ok']
        print(f'\npatcher broke ({len(broke)}):')
        for r in broke:
            print(f"   {r['name']}  -> {r['patched']} {r['patched_code']} line {r['patched_line']}")
        print(f'\npatcher fixed ({len(fixed)}):')
        for r in fixed:
            print(f"   {r['name']}  (raw {r['raw']} {r['raw_code']} line {r['raw_line']})")
        print(f'\nwine rejects both ({len(both)}), by patched error:')
        for code, n in collections.Counter(r['patched_code'] or r['patched'] for r in both).most_common():
            print(f'   {code:>8}  {n}')
        bad += len(broke)
    else:
        print('\nraw failures by error:')
        for code, n in collections.Counter(r['raw_code'] or r['raw'] for r in rows if r['raw'] != 'ok').most_common():
            print(f'   {code:>8}  {n}')

    if args.baseline:
        base = load_tsv(args.baseline)
        worse = []
        for r in rows:
            b = base.get(r['name'])
            if not b:
                continue
            for col in ('raw', 'patched'):
                if b.get(col) == 'ok' and r[col] not in ('ok', ''):
                    worse.append((r['name'], col, r[col], r[col + '_code'], r[col + '_line']))
        print(f'\nworse than baseline ({len(worse)}):')
        for name, col, st, code, line in worse:
            print(f'   {name}  [{col}] {st} {code} line {line}')
        bad += len(worse)

    print(f'\nresults: {args.out}')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
