#!/usr/bin/env python3
# license:GPLv3+
"""Every F12 option with a list of choices that a corpus of table scripts registers
(<table>.Option(name, min, max, step, default, unit, Array("a", "b", ...))), one per line:

    python3 extract_options.py SCRIPTS_DIR [SCRIPTS_DIR ...] > options.tsv

Columns: script, option name, min, default, choices (separated by \\x1f). Feed the file to
tests/test-tableoptiondefault.cpp to list every table whose phone default changes.
"""
import os
import re
import sys

CALL = re.compile(r'\b\w+\.Option\s*\(', re.I)


def args_of(script, start):
    """Arguments of the call whose '(' is at start-1, joined across `_` line continuations."""
    depth, i, cur, args, in_str = 1, start, [], [], False
    while i < len(script) and depth > 0:
        c = script[i]
        if in_str:
            cur.append(c)
            if c == '"':
                if i + 1 < len(script) and script[i + 1] == '"':
                    cur.append('"')
                    i += 1
                else:
                    in_str = False
        elif c == '"':
            in_str = True
            cur.append(c)
        elif c == "'":  # comment to end of line
            while i < len(script) and script[i] not in '\r\n':
                i += 1
            continue
        elif c == '_' and re.match(r'_[ \t]*(\r\n|\r|\n)', script[i:]) and (not cur or not (cur[-1].isalnum() or cur[-1] == '_')):
            i += re.match(r'_[ \t]*(\r\n|\r|\n)', script[i:]).end()
            continue
        elif c in '\r\n':
            return None  # statement ended without closing the call
        elif c == '(':
            depth += 1
            cur.append(c)
        elif c == ')':
            depth -= 1
            if depth > 0:
                cur.append(c)
        elif c == ',' and depth == 1:
            args.append(''.join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    args.append(''.join(cur).strip())
    return args


def choices(arg):
    m = re.match(r'(?i)Array\s*\((.*)\)\s*$', arg, re.S)
    if not m:
        return None
    return [s.replace('""', '"') for s in re.findall(r'"((?:[^"]|"")*)"', m.group(1))]


def number(s):
    try:
        return float(s)
    except ValueError:
        return None


def main():
    for d in sys.argv[1:]:
        for name in sorted(os.listdir(d)):
            if not name.lower().endswith('.vbs'):
                continue
            with open(os.path.join(d, name), 'rb') as f:
                script = f.read().decode('latin-1')
            for m in CALL.finditer(script):
                line_start = script.rfind('\n', 0, m.start()) + 1
                if script[line_start:m.start()].lstrip().startswith("'"):
                    continue
                a = args_of(script, m.end())
                if not a or len(a) < 7:
                    continue
                lits = choices(a[6])
                lo, default = number(a[1]), number(a[4])
                if lits is None or lo is None or default is None or not a[0].startswith('"'):
                    continue
                print('\t'.join([name[:-4], a[0].strip('"'), str(int(lo)), str(int(default)), '\x1f'.join(lits)]))


if __name__ == '__main__':
    main()
