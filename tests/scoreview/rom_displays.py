#!/usr/bin/env python3
# license:GPLv3+
"""Builds the fixtures of test-layoutchoice.cpp from probe_displays output.

    python3 rom_displays.py PROBE_OUTPUT.tsv LAYOUT_DIR OUT_DIR

rom_displays.tsv: per ROM, its segment displays (digit counts) the way libpinmame groups them
for the ScoreView (libpinmame.cpp: the elements of the layout entries, in declaration order,
split where the line changes or a gap appears) and its DMD sizes.

expected_layouts.tsv: the layout ScoreView::Select picked for each ROM before generated
layouts existed, worked out independently of the C++ code: a layout fits when every display
it draws exists with its digit count (DMDs: same aspect ratio); the fitting layout with the
most displays wins; NONE when none fits.
"""
import collections
import os
import re
import sys

DMD, VIDEO, NODISP = 14, 15, 0x200


def seg_displays(entries):
    elems = []
    for idx, count, typ, top, left, length, w, h in sorted(entries):
        if (typ & 0x3f) in (DMD, VIDEO) or typ & NODISP:
            continue
        for j in range(length):
            elems.append((top, left + j * 2))
    out, start = [], 0
    for i in range(len(elems)):
        if i == len(elems) - 1 or elems[i][0] != elems[i + 1][0] or elems[i][1] + 2 != elems[i + 1][1]:
            out.append(i + 1 - start)
            start = i + 1
    return out


def read_layouts(folder):
    layouts = {}
    for name in sorted(os.listdir(folder)):
        if not name.endswith('.scv'):
            continue
        visuals, cur = [], None
        for line in open(os.path.join(folder, name), encoding='utf-8'):
            s = line.strip()
            if s.startswith('#'):
                continue
            if s.startswith('- '):
                cur = {'type': s[2:].rstrip(':'), 'src': None, 'n': None, 'size': None}
                visuals.append(cur)
            elif cur is not None and s.startswith('Src:'):
                cur['src'] = s[4:].strip()
            elif cur is not None and s.startswith('XPos:'):
                cur['n'] = len(re.findall(r'-?[\d.]+', s[5:]))
            elif cur is not None and s.startswith('NElements:'):
                cur['n'] = int(s[10:])
            elif cur is not None and s.startswith('Size:'):
                cur['size'] = tuple(int(float(v)) for v in re.findall(r'[\d.]+', s[5:]))
        layouts[name[:-4]] = visuals
    return layouts


def expected(segs, dmds, layouts):
    best, best_score = 'NONE', -1
    for name, visuals in layouts.items():
        score, ok = 0, bool(visuals)
        for v in visuals:
            if v['type'] == 'SegDisplay':
                m = re.search(r'id=(\d+)', v['src'] or '')
                i = int(m.group(1)) if m else 0
                if i < len(segs) and segs[i] == v['n']:
                    score += 1
                else:
                    ok = False
            elif v['type'] == 'DMD':
                if dmds and dmds[0][0] * v['size'][1] == v['size'][0] * dmds[0][1]:
                    score += 10
                else:
                    ok = False
            else:  # Image: the reel image, absent on these ROMs
                ok = False
        if ok and score > best_score:
            best, best_score = name, score
    return best


def main():
    probe, layout_dir, out = sys.argv[1:4]
    games, disp, done = {}, collections.defaultdict(list), set()
    for line in open(probe, encoding='utf-8', errors='replace'):
        p = line.rstrip('\n').split('\t')
        if p[0] == 'GAME' and len(p) >= 4:
            games[p[1]] = (p[2], p[3])
        elif p[0] == 'DISPLAY':
            disp[p[1]].append(tuple(map(int, p[2:])))
        elif p[0] == 'DONE':
            done.add(p[1])
    layouts = read_layouts(layout_dir)
    with open(os.path.join(out, 'rom_displays.tsv'), 'w', encoding='utf-8', newline='\n') as f, \
         open(os.path.join(out, 'expected_layouts.tsv'), 'w', encoding='utf-8', newline='\n') as e:
        f.write('# rom\tmanufacturer\tyear\tsegment displays (digits)\tDMD sizes\n')
        e.write('# rom\tlayout picked before generated layouts (NONE: panel off)\n')
        for rom in sorted(done):
            segs = seg_displays(disp[rom])
            dmds = [(w, h) for idx, count, typ, top, left, length, w, h in disp[rom] if (typ & 0x3f) == DMD]
            man, year = games.get(rom, ('?', '?'))
            f.write(f"{rom}\t{man}\t{year}\t{','.join(map(str, segs))}\t{','.join(f'{w}x{h}' for w, h in dmds)}\n")
            e.write(f'{rom}\t{expected(segs, dmds, layouts)}\n')


if __name__ == '__main__':
    main()
