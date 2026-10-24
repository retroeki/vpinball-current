# PuP plugin tests (Android additions)

Two pieces of `plugins/pup` decide where a PuP table's score ends up on the device, and both
are header-only so they run on the host:

- `PUPScreenType.h`: what a table's `ScreenType.txt` read gives. On Windows the user runs one
  of the pack's layout `.bat` files, which copies a layout and writes the number the table
  maps to its score screen. On the device the number is worked out from the layout the pack
  is set to. SimpleScriptPatcher replaces `ObjFile.ReadLine` with `PuPlayer.ReadScreenType`.
- `PUPScoreTracker.h`: the ScoreView shows one PuP screen; it moves to the screen holding the
  score label when the first guess from `screens.pup` does not show the score.

## Unit tests

```bash
g++ -std=c++20 -Iplugins/pup tests/pup/test-pup.cpp -o /tmp/test_pup && /tmp/test_pup tests/pup/fixtures
```

`fixtures/` holds the config files (no media) of real packs: Darkest Dungeon 3.06 and
Terrifier 1.0.1 as their zips ship, BTILC and Batman 66 as configured on a PC, Harry Potter's
single layout, plus two variants (`dd_slim`: Slim DMD chosen; `btilc_unset`: no
`ScreenType.txt`). Each pack is a `pupvideos/<pack>` folder, so a fixture root can be passed
as a table folder.

## The tables, end to end

The Wine harness answers `PuPlayer.ReadScreenType` with the same code and, with
`VBS_HARNESS_LOG_PUP=1`, prints the screen each `LabelInit` names, so a patched table run
against a fixture shows which screen gets its score:

```bash
VBS_HARNESS_LOG_PUP=1 ~/vbs_check_build/vbs_check --host=SCRIPTS --items=ITEMS \
    --table-dir=tests/pup/fixtures/default --relaunch DarkestDungeon_3.06.vbs
```

| Table | Pack in `fixtures/default` | Score screen (`LabelInit`) | Without the pack |
|---|---|---|---|
| Darkest Dungeon 3.06 | file 99, FullDMD layout | 13 (over 5) | 1 |
| Terrifier 1.02 | file 2, Desktop-VR layout | 5 | 12 |
| BTILC 1.0.0 | file 0 | 5 | 5 |

## Patcher changes

`../scriptpatcher/corpus_harness/patcher_diff.sh REV WORK RAW... -- --expect NAME...`
builds the patcher of a git revision and of the working tree, patches whole corpora with
both and fails if a table outside `--expect` changed. The ScreenType rule changed exactly
two lines in each of the five tables that read `ScreenType.txt` (1,337 scripts compared).

## Which tables can the ScoreView move?

`score_screen_survey.py SCRIPTS_DIR...` lists, from the scripts alone, the PuP tables that
write their score only to the backglass (the ScoreView moves there once the score passes
1,000 if screen 1 was the guess) and those that write it to both. It reads source text, so
it over-counts: Harry Potter's DMD score routine is listed although the table never shows
that page.

## On a device

The two other PuP changes are rendering behaviour and need a device:

- a stop on a screen returns it to its background video instead of leaving it black;
- the ScoreView draws a screen's PuPOverlays/PuPAlphas images (the score frame).

Check with the packs above: Harry Potter (castle video and frame behind the score after a
game starts), Darkest Dungeon (score in the FullDMD panel), Terrifier, BTILC.
