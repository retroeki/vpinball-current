#!/usr/bin/env python3
# license:GPLv3+
"""Which PuP screen does each table write its score to? Static survey of table scripts, to
know which tables the ScoreView's score-label rule (plugins/pup/PUPScoreTracker.h) can move
off its first guess, and so which to try on a device.

    python3 score_screen_survey.py SCRIPTS_DIR [SCRIPTS_DIR ...] [--all]

A score write is a PuPlayer.LabelSet whose caption mentions Score( / FormatScore /
FormatNumber(Score... and not a high score. Its screen argument is resolved through the
script's Const/assignments when it is a name (pDMD, pBackglass). The guess the plugin makes
from screens.pup needs the pack, which most corpora lack, so this lists where the score goes:
- "backglass only": the score is written to screen 2 alone (Harry Potter): the ScoreView
  moves there once the score passes 1,000, if screen 1 was the guess.
- "dmd and backglass": both (Darkest Dungeon): it stays on the guess.
- everything else stays where it is.
"""
import argparse
import collections
import os
import re
import sys

LABELSET = re.compile(r'PuPlayer\.LabelSet\s*\(?\s*([\w.]+)\s*,\s*"([^"]*)"\s*,\s*([^\r\n]*)', re.I)
SCORE = re.compile(r'\b(FormatScore\w*|FormatNumber\s*\(\s*Score|Score\s*\(|PlayerScore|CurrentScore|anScore)', re.I)
HIGH = re.compile(r'high|hs|hiscore|best|champ', re.I)


def resolve(name, script):
    if name.isdigit():
        return [int(name)]
    values = set()
    for m in re.finditer(r'(?im)^[ \t]*(?:Const|Dim)?[ \t]*(?:\w+[ \t]*:[ \t]*)?' + re.escape(name) + r'[ \t]*=[ \t]*(\d+)\b', script):
        values.add(int(m.group(1)))
    for m in re.finditer(r'(?i)\b' + re.escape(name) + r'[ \t]*=[ \t]*(\d+)\b', script):
        values.add(int(m.group(1)))
    return sorted(values)


def survey(path):
    with open(path, 'rb') as f:
        script = f.read().decode('latin-1')
    if 'pinupplayer.pindisplay' not in script.lower():
        return None
    screens = collections.Counter()
    for m in LABELSET.finditer(script):
        target, label, rest = m.groups()
        caption = rest.split(',')[0]
        if not SCORE.search(caption) or HIGH.search(label) or HIGH.search(caption):
            continue
        line_start = script.rfind('\n', 0, m.start()) + 1
        if script[line_start:m.start()].lstrip().startswith("'"):
            continue
        for v in resolve(target, script) or [target]:
            screens[v] += 1
    return screens


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dirs', nargs='+')
    ap.add_argument('--all', action='store_true', help='list every PuP table, not just the backglass ones')
    args = ap.parse_args()
    groups = collections.defaultdict(list)
    seen = set()
    for d in args.dirs:
        for name in sorted(os.listdir(d)):
            if not name.lower().endswith('.vbs') or name.lower() in seen:
                continue
            seen.add(name.lower())
            screens = survey(os.path.join(d, name))
            if screens is None:
                continue
            nums = {s for s in screens if isinstance(s, int)}
            if not screens:
                kind = 'no score label (score elsewhere: FlexDMD, B2S, reels)'
            elif nums == {2}:
                kind = 'backglass only'
            elif 2 in nums:
                kind = 'dmd and backglass'
            else:
                kind = 'dmd screens ' + ','.join(str(s) for s in sorted(screens, key=str))
            groups[kind].append(name[:-4])
    for kind in sorted(groups, key=lambda k: (k != 'backglass only', k != 'dmd and backglass', k)):
        tables = groups[kind]
        print(f'{kind}: {len(tables)}')
        if args.all or kind in ('backglass only', 'dmd and backglass'):
            for t in tables:
                print(f'    {t}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
