# DMD Colorization Test Rig

A headless PC replica of the Android DMD colorization pipeline. It runs the real
PinMAME emulation (this fork's vendored source, including all local fixes), feeds the
real identify-frame stream to the same-generation libserum the device ships, mirrors
the serum plugin's publication logic, and lets you SEE the colorized output as PNG
images and MEASURE match rates — all without touching a device.

Built 2026-07-19 during the colour-DMD stutter investigation. Bugs found and fixed
with it, each proven on PC before a single device build:

| Case | Symptom | Root cause found with the rig |
|---|---|---|
| smb3 (GTS3 SMB) | colorized DMD stuttered at ~0.25fps | plugin treated `Serum_Colorize()` return as a plain ms delay; low word is the delay, high bits are flags — a scene-start (`FLAG_RETURNED_V2_SCENE`=0x40000) scheduled the next `Serum_Rotate()` 262s out, so authored scenes never played (`[serum-scene fix]`) |
| sttng_l7 | (false alarm) green stars | device was rendering a `.pal` via the VNI plugin; Serum path verified color-correct end-to-end here |
| trek_201 (DE 128x16) | blank DMD with .cROMc | packs for 128x16 hardware are authored 128x32 with content rows CENTERED (rows 8..23); feeding the raw 16-high frame made libserum hash past the buffer — nothing identified (`[serum-16high fix]`, later + render-crop of the bar rows) |

Also established (negative results worth keeping): the smb3 pack's maskless hashes
match NOTHING derivable from the emulated stream in any domain (identify 5-level,
luminance 16-level under trunc/round/shift, block sums at periods 6..48 at every
phase, monotone-LUT oracle) — that pack is authored from real-hardware captures and
works through masks + scenes only.

## Layout

```
harness.cpp        the rig: boots a ROM via libpinmame, receives DMD frames on the
                   legacy OnDisplayUpdated callback (= vblank-sampled identify, same
                   stream classic VPinMAME feeds colorizers), runs Serum_Colorize /
                   Serum_Rotate exactly like the plugin, counts new/same/nof/gray/
                   scene/rot per second, dumps colorized frames as PPM
pluginsim.h        faithful port of the plugin's ColorizationState publication
                   pipeline (double buffers, synthesis, scene/rotate handling) so
                   dumps show exactly what the plugin would hand the VPX renderer
dumper2.cpp        loads a .cROMc/.cRZ via libserum's own loader and prints every
                   frame's hashcode + shapecompmode + compmaskID (the hash oracle)
extra.cpp          per-pack facts: nframes, extra(HD)-frame count, geometry,
                   nocolors; per-frame isextraframe/triggerID/mask on request
analysis/
  ppm2png.py       PPM -> 4x-upscaled PNG (view frames directly)
  membership.py    captured CRCs vs pack hash table
  membership3.py   3 luminance quantizations vs hash table (trunc/round/>>4)
  phasescan.py     brute-force PWM block period x phase x level-mapping vs hashes
  transform16.py   brute-force 128x16 -> 128x32 layout (rowdouble/center/top/...)
```

## One-time setup (WSL)

Needs: gcc/g++, cmake, make, zlib1g-dev, python3 + numpy (analysis scripts run fine
under Windows Python too).

```bash
RIG=~/rig && mkdir -p $RIG && cd $RIG

# 1. libpinmame (linux, static) from the vendored fork source - OUT OF SOURCE so the
#    android tree stays untouched. Rebuild after editing vendored pinmame sources.
cmake -S /mnt/c/vpinball-master_060726/vpinball-master/external/android-arm64-v8a/Release/pinmame/pinmame \
      -B $RIG/pmbuild -DPLATFORM=linux -DCMAKE_BUILD_TYPE=Release
make -C $RIG/pmbuild -j8 pinmame_static

# 2. libserum, same generation as the device .so (third-party/runtime-libs is built
#    from PPUC/libserum_concentrate; master reads all concentrate versions)
curl -sL https://github.com/PPUC/libserum_concentrate/archive/refs/heads/master.tar.gz -o serum.tar.gz
mkdir newlib && tar xzf serum.tar.gz -C newlib --strip-components=1
gcc -O2 -w -c newlib/third-party/include/miniz/miniz.c newlib/third-party/include/lz4/lz4.c newlib/third-party/include/lz4/lz4hc.c
g++ -O2 -std=c++17 -w -I newlib/src -I newlib/third-party/include \
    -c newlib/src/serum-decode.cpp newlib/src/SerumData.cpp newlib/src/SceneGenerator.cpp

# 3. the rig binaries
SRC=/mnt/c/vpinball-master_060726/vpinball-master/tools/dmd-colorization-rig
PM=/mnt/c/vpinball-master_060726/vpinball-master/external/android-arm64-v8a/Release/pinmame/pinmame/src/libpinmame
g++ -O2 -std=c++17 -w -I $PM -I newlib/src -I $SRC \
    $SRC/harness.cpp serum-decode.o SerumData.o SceneGenerator.o miniz.o lz4.o lz4hc.o \
    $RIG/pmbuild/libpinmame.a -lz -lpthread -ldl -o harness
# dumper2/extra #include serum-decode.cpp directly (they need the static g_serumData):
g++ -O2 -std=c++17 -w -I newlib/src -I newlib/third-party/include \
    $SRC/dumper2.cpp newlib/src/SerumData.cpp newlib/src/SceneGenerator.cpp miniz.o lz4.o lz4hc.o -o dumper2
g++ -O2 -std=c++17 -w -I newlib/src -I newlib/third-party/include \
    $SRC/extra.cpp   newlib/src/SerumData.cpp newlib/src/SceneGenerator.cpp miniz.o lz4.o lz4hc.o -o extra

# 4. game data
mkdir -p vpm/roms vpm/nvram altcolor frames
# copy <rom>.zip into vpm/roms/  (library: G:\Roms\VPX Roms and Tables\Roms\roms)
# copy <rom>.nv into vpm/nvram/  (optional but skips boot/setup - pull from device)
# copy the pack into altcolor/<rom>/<rom>.cROMc (folder name must equal the ROM name)
```

Note: the harness includes the raw-PWM-ring sampler which needs the
`core_dmd_peek_raw_ring` diagnostic accessor in the vendored core.c (part of
`patches/vendored-pinmame-android-fixes.patch`). If the vendored tree was re-fetched,
apply that patch first.

## Running

```
./harness <vpmPath> <altcolorPath> <rom> <seconds> [mode] [skip] [timeoutMs]
./harness vpm altcolor smb3 75                 # identify-domain feed (device behavior)
./harness vpm altcolor smb3 75 lum             # trunc(15*lum) 16-level luminance feed
./harness vpm altcolor trek_201 75 dbl         # 128x16 centered into 128x32 (the fix)
./harness vpm altcolor trek_201 75 hex16       # log raw 128x16 frames as hex lines
./harness vpm altcolor smb3 75 lum 8 500       # + unknown-frame fallback knobs
```

Per-second stderr counters: `frames` (distinct DMD frames), `new` (fresh colorized),
`same` (matched, unchanged), `nof` (unidentified -> display holds), `gray`
(monochrome fallback fired), `scene`/`rot` (updates delivered by the Serum_Rotate
pump — scene playback and palette rotations).

Outputs in the working dir:
- `frames.log` — per distinct frame: `ms result valmask crc [crcRound crcShift]`
  (valmask = bitmask of pixel values 0-15 present; crc = zlib CRC32 of the frame fed
  to Serum, computed BEFORE the call - libserum mutates its input)
- `frames/pNNNN_{32,64}.ppm` — the PLANES THE PLUGIN WOULD PUBLISH (via pluginsim),
  twice a second; convert and view: `python analysis/ppm2png.py "frames/*.ppm"`
- `subframes.bin` — every raw PWM sub-frame with its submission index (from the ring
  sampler), for offline combining experiments

## Recipes

**Which frames does a pack actually contain?**
`./dumper2 altcolor <rom> > hashes.txt` then membership-test any capture:
`python analysis/membership.py hashes.txt frames.log`. Direct hits only occur for
maskless (`mask=255`), non-shape frames — masked frames match only through Serum.

**Is the frame DOMAIN wrong (nothing matches)?** Run identify + lum modes, compare
`new+same` vs `nof`; run `membership3.py` for the three luminance quantizations;
run `phasescan.py hashes.txt subframes.bin` to brute-force block combining.

**Unknown geometry/layout transform?** Capture with `hex16` (or extend for other
sizes), then `transform16.py hashes.txt frames.log` — the pack's hash table is a
perfect oracle: the correct transform scores hundreds of direct hits, wrong ones
score ~a dozen (blank/boot frames).

**Is a bug in libserum or in our plugin?** The `frames/fNNNN_*.ppm`-style dumps in
harness.cpp can snapshot `pSerum->frame32/64` directly (pure libserum), while the
`pNNNN_*` dumps go through pluginsim. If pure output is right and published output is
wrong, the bug is ours.

**Scene packs** (`./extra <rom>` shows trigger counts): scene playback is driven by
`Serum_Rotate()` — if scenes stall, check the delay-word handling first (see the
smb3 case above).

## On-device instrumentation

The serum plugin (plugins/serum/serum.cpp) has a `SERUM_DIAG` compile gate (default
1 — zero it for release): per-second `[SerumDiag]` counter lines and per-frame
`[SerumFrame] res/vals/crc` lines in logcat. The CRCs are computed pre-colorize and
are directly comparable with `dumper2` hash tables and rig captures — that is how a
device log and a PC run were proven to be watching the same frames.
