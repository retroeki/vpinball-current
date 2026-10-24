# ScoreView layout choice

The in-game score panel (`plugins/scoreview`) shows one layout: a bundled `.scv` file whose
displays all exist with the right digit count, the redraw of a table's own score reels, or,
since `ScoreViewLayoutChoice.h`, a layout generated from the ROM's segment displays when no
bundled file fits. About one segment ROM in six needed it: Taito do Brasil (four 6-digit
players with single-digit credit and ball displays), Zaccaria (five 8-digit displays),
Playmatic, LTD, Juegos Populares, MAC, early Williams and others. JPSalas has 31 such tables.

## The test

```bash
g++ -std=c++20 -Iplugins/scoreview tests/scoreview/test-layoutchoice.cpp -o /tmp/tlc
/tmp/tlc plugins/scoreview/layouts tests/scoreview
```

For every ROM in `rom_displays.tsv`, with and without a reel image on the table and for a
landscape and a portrait panel:

- a ROM a bundled layout fits keeps exactly that layout (`expected_layouts.tsv`, worked out
  by `rom_displays.py` independently of the C++ code);
- any other ROM gets a generated layout with each of its displays once, inside the layout,
  without overlaps, at a sane aspect ratio;
- a real DMD, an EM reel table and a table with no source never get a generated layout.

Result on the reference set: 1,399 segment ROMs, 1,159 keep their layout, 240 get a
generated one.

## The fixtures

`rom_displays.tsv` comes from running every ROM of a PinMAME set through PinMAME itself:
`probe_displays.cpp` prints what `OnDisplayAvailable` reports, and `rom_displays.py` groups
it into displays the way `libpinmame.cpp` does for the ScoreView. To rebuild (WSL):

```bash
PM=external/android-arm64-v8a/Release/pinmame/pinmame   # the vendored PinMAME source
cmake -S $PM -B ~/pmhost -DPLATFORM=linux -DARCH=x64 -DBUILD_SHARED=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build ~/pmhost -j
g++ -O2 -std=c++17 -I$PM/src/libpinmame tests/scoreview/probe_displays.cpp ~/pmhost/libpinmame.a -lpthread -lz -o ~/probe_displays
mkdir -p ~/vpm && ln -sfn /path/to/roms ~/vpm/roms
ls ~/vpm/roms | sed 's/\.zip$//' | xargs -P12 -I{} sh -c 'timeout 12 ~/probe_displays ~/vpm/ {}' > probe.tsv
python3 tests/scoreview/rom_displays.py probe.tsv plugins/scoreview/layouts tests/scoreview
```

Only ROMs that reported all their displays within the timeout are kept (DMD games boot too
slowly for it; the generated layout never applies to them).
