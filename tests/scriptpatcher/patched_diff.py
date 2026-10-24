#!/usr/bin/env python3
# license:GPLv3+
"""Which tables does a patcher change touch? Compares two patch_corpus outputs of the same raw
scripts (a baseline patcher and the one under test) and lists every table whose patched
script differs, with the changed lines.

    python3 patched_diff.py BASE_DIR NEW_DIR [--expect TEXT ...] [--lines N]

--expect names the tables the change is meant to touch (case-insensitive substrings of the
file name). Any other table that changed is UNEXPECTED and the exit status is 1, so a rule
meant for five tables cannot silently rewrite a sixth. corpus_harness/patcher_diff.sh builds
the baseline from a git revision and runs this.
"""
import argparse
import difflib
import os
import sys


def read_lines(path):
    with open(path, 'rb') as f:
        return f.read().decode('latin-1').replace('\r\n', '\n').replace('\r', '\n').split('\n')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('base')
    ap.add_argument('new')
    ap.add_argument('--expect', action='append', default=[])
    ap.add_argument('--lines', type=int, default=6, help='changed lines shown per table')
    args = ap.parse_args()

    base = {f for f in os.listdir(args.base) if f.lower().endswith('.vbs')}
    new = {f for f in os.listdir(args.new) if f.lower().endswith('.vbs')}
    expect = [e.lower() for e in args.expect]

    changed, unexpected = [], []
    for name in sorted(base & new):
        a = read_lines(os.path.join(args.base, name))
        b = read_lines(os.path.join(args.new, name))
        if a == b:
            continue
        diff = [l for l in difflib.unified_diff(a, b, lineterm='', n=0) if l[:1] in '+-' and l[:3] not in ('+++', '---')]
        changed.append(name)
        is_expected = any(e in name.lower() for e in expect)
        if not is_expected:
            unexpected.append(name)
        print(f"{'changed ' if is_expected else 'UNEXPECTED'}  {name}  ({len(diff)} lines)")
        for line in diff[:args.lines]:
            print('      ' + line[:160])
        if len(diff) > args.lines:
            print(f'      ... {len(diff) - args.lines} more')

    for name in sorted(base - new):
        print(f'missing in new: {name}')
    for name in sorted(new - base):
        print(f'missing in base: {name}')
    missed = [e for e in expect if not any(e in n.lower() for n in changed)]
    for e in missed:
        print(f'expected to change but did not: {e}')

    print(f'\n{len(base & new)} tables compared, {len(changed)} changed, {len(unexpected)} unexpected')
    return 1 if unexpected or base != new else 0


if __name__ == '__main__':
    sys.exit(main())
