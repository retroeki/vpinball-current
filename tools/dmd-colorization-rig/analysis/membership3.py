# Compare three luminance quantizations against the .cROMc hash table.
# frames.log: ms res vals crcTrunc crcRound crcShift
# usage: python3 membership3.py smb3_hashes.txt frames.log
import sys
hashes = set()
maskless = set()
for line in open(sys.argv[1]):
    p = line.split()
    if len(p) >= 4:
        h = int(p[1], 16)
        hashes.add(h)
        if p[3] in ('mask=255',):
            maskless.add(h)
names = ['trunc(15*lum)', 'round(15*lum)', 'trunc(255*lum)>>4']
hits = [0, 0, 0]
mhits = [0, 0, 0]
total = 0
uniq = [set(), set(), set()]
uhits = [set(), set(), set()]
for line in open(sys.argv[2]):
    p = line.split()
    if len(p) != 6:
        continue
    total += 1
    for k in range(3):
        c = int(p[3 + k], 16)
        uniq[k].add(c)
        if c in hashes:
            hits[k] += 1
            uhits[k].add(c)
        if c in maskless:
            mhits[k] += 1
print(f"file: {len(hashes)} hashes, {len(maskless)} maskless; captured {total} frames")
for k in range(3):
    print(f"{names[k]:22s}: frame-hits {hits[k]:5d} (maskless {mhits[k]:5d})  unique-hits {len(uhits[k])}/{len(uniq[k])}")
