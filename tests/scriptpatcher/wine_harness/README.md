# Wine VBScript harness (host build of the shipped engine)

`vbs_check` compiles the vendored Wine vbscript engine (`standalone/inc/wine`, the
same source list and defines as `make/CMakeLists_common_standalone.txt`) for the
host and runs VBScript through it the way `CodeViewer` does: `InitNew`,
`SetScriptSite`, `ParseScriptText` while INITIALIZED (compile only), then
`SCRIPTSTATE_STARTED` to run the queued top-level code (`--run`).

It answers "does Wine accept this script" without a device: the proof the
patcher rules need before they exist, and the check that an engine change did
not break anything.

It uses the in-tree `parser.tab.c`/`.h` that the engine build generated, so
the grammar is the one the device compiled. A host-only `<android/log.h>` shim
(`shim/`) stands in for the NDK header `interp.c` includes.

## Build and run the regression cases (WSL)

```bash
./run.sh            # builds ~/vbs_check_build/vbs_check, runs cases/*.vbs
```

Each `cases/*.vbs` declares what must happen:

```
' EXPECT: compile-ok | run-ok | compile-error <n> | run-error <n>
' WINDOWS: ok | compile-error | run-error      what Microsoft's engine does
' KNOWN-BUG: <id> <text>                        Wine still differs: reported XFAIL
' GLOBALS: Version,MusicVolume                  host object with global members
' ITEMS: Trim=Wall                              table elements (named items)
```

Results are PASS / FAIL / XFAIL (known bug still there) / XPASS (known bug now
fixed: delete its `KNOWN-BUG` line). FAIL or XPASS exits 1.

The `WINDOWS` lines are verified, not assumed. From Windows:

```
python run_cases.py --engine windows     # runs each case under cscript
```

## Corpus: does the patcher break anything Wine would have compiled?

```bash
# 1. pre-patch scripts (Windows, needs olefile)
python ../extract_vpx_scripts.py "G:\VPX Files" "C:\vpx_patcher_corpus\scripts_raw_<date>"
# 2. the real patcher (WSL)
~/patch_corpus_build/patch_corpus RAW PATCHED report.csv      # see ../corpus_harness
# 3. compile both through the shipped engine
python3 corpus_check.py --raw RAW --patched PATCHED --out results.tsv [--baseline old.tsv]
```

`patcher broke` (compiles raw, fails patched) is always a patcher bug. With
`--baseline`, any table whose raw or patched result got worse is listed too.

Baseline 2026-10-23 (`C:\vpx_patcher_corpus\wine_compile_2026-10-23.tsv`):
634/634 raw compile, 633/634 patched; the one break is Big Town Pedator 2.1 VR
line 206 (pSubOnlyPause on a commented Sub), matching its field crash report.

## Running table code: `--host` and `--items`

`vbs_check --host=SCRIPTS_DIR --items=ITEMS_DIR table.vbs` runs a table's
top-level code and then `<table>_Init` against a stand-in table (`table_host.c`):

- `GetTextFile` serves SCRIPTS_DIR (use the app's bundled
  `app/src/main/assets/vpinball/scripts`), so `LoadVPM` really runs
  controller.vbs, the system script and core.vbs.
- `--items` reads `ITEMS_DIR/<table>.items.tsv` from
  `../extract_vpx_items.py`. Each element is registered as its own named
  item, like CodeViewer does, with its real TypeName and its TimerInterval,
  Color, Material and Image. Collections enumerate their members. A name the
  .vpx lacks stays undefined, as on device.
- `CreateObject` works for the ProgIDs the device provides (PinMAME, B2S,
  FlexDMD/UltraDMD, PUP, WMP, ArrayList) and fails for the rest. WScript.Shell
  and Shell.Application answer exactly like the device stubs
  (`src/core/ScriptWScriptShell.cpp`, `ScriptShellApplication.cpp`): RegRead
  is Empty, unknown members are errors.
- `UserDirectory` is a fresh empty folder per table (removed afterwards), so
  high score and settings files go through the device's FSO code (Wine scrrun
  over `standalone/inc/wine/wine.c` file I/O).
- `--table-dir=DIR` (corpus: `--companions`) serves the `.vbs` files shipped
  next to the `.vpx` after SCRIPTS_DIR, in `ScriptGlobalTable::GetTextFile` order.
- `--relaunch` then calls `<table>_Exit` (where tables save) and starts the
  table again on the same UserDirectory; failures there are `relaunch-*`/`exit`.
- Other members are catch-alls: assignments are remembered (by value), unset
  reads give an object whose value is 1, `Is*` probes and ShowDT read False.

```bash
python ../extract_vpx_items.py "G:\VPX Files" "C:\vpx_patcher_corpus\items_<date>"
python3 corpus_check.py --raw RAW --patched PATCHED --host SCRIPTS --items ITEMS [--relaunch] [--sample 50 --seed 1]
```

Zipped libraries (one or more `.vpx` per zip) go through `../extract_vpx_zips.py`,
which writes `scripts_raw/`, `items/` and `companions/` without unpacking to disk:

```bash
python ../extract_vpx_zips.py "G:\Roms\To be sorted\Visual Pinball (2026-01-02)" C:\vpx_patcher_corpus\vp_2026-01-02 -j6
~/patch_corpus_build/patch_corpus $C/scripts_raw $C/scripts_patched $C/patch_report.csv
python3 corpus_check.py --raw $C/scripts_raw --patched $C/scripts_patched --host SCRIPTS \
    --items $C/items --companions $C/companions --relaunch
```

Baselines, with `--relaunch`, built with AddressSanitizer (no memory errors
anywhere). In `C:\vpx_patcher_corpus`:

| Library | Tables | Raw clean | Patched clean | Patcher broke | TSV |
|---|---|---|---|---|---|
| G:\VPX Files, before the crash-fixes-2026-10-23 branch | 634 | 621 | 622 | Big Town | `wine_relaunch_2026-10-23.tsv` |
| G:\VPX Files, after | 634 | 624 | 630 | none | `wine_final_2026-10-23.tsv` |
| Visual Pinball (2026-01-02) zips, before | 700 | 695 | 696 | none | `wine_relaunch_vp_2026-01-02.tsv` |
| Visual Pinball (2026-01-02) zips, after | 700 | 697 | 698 | none | `wine_final_vp_2026-01-02.tsv` |

"After" uses `scripts_patched_fix_2026-10-23` and `vp_2026-01-02/scripts_patched_fix`.
Remaining: 4X4 (500, cluster F: runtime error in top-level code, policy
undecided) and stand-in limits (Munsters, CyberRace, Space Station, Defender
`Controller.NVRAM`, Heavy Metal `Controller.Games().Settings` on exit).

### Memory errors

Build a second copy with AddressSanitizer and pass it as `--bin`; a memory error
aborts the run and shows as `crash`:

```bash
cmake -S . -B ~/vbs_check_asan -DCMAKE_C_FLAGS="-g -O1 -fsanitize=address -fno-omit-frame-pointer" \
      -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address && cmake --build ~/vbs_check_asan -j8
ASAN_OPTIONS=detect_leaks=0 python3 corpus_check.py --bin ~/vbs_check_asan/vbs_check ...
```

Check the stand-in first: a `crash` that turned out to be the stand-in keeping a
`VT_BYREF` argument (a pointer into a freed Sub frame) looked exactly like a Wine
use-after-free.

Cases can declare elements with `' ITEMS: Name=Type,...`; on Windows those run
through 32-bit `MSScriptControl.ScriptControl`, which can add named items.

## Limits

- Plain `--run` has no table at all; use `--host`/`--items` for table scripts.
- Only top-level code, `<table>_Init` and `<table>_Exit` run: no timers, no
  switch or hit events, no PinMAME (the Controller is a catch-all).
- Cases run in an empty temporary working directory; a case may write files.
- Built with gcc on x86-64; the device uses clang on arm64 with approximate-math
  flags. Parsing and compiling are unaffected; float formatting at runtime could
  differ.
