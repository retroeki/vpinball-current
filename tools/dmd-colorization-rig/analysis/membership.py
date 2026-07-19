# Membership test: which captured identify-frame CRCs exist in the .cROMc hash table?
# usage: python3 membership.py smb3_hashes.txt frames.log
import sys
hashes = {}          # crc -> (shape, mask)
for line in open(sys.argv[1]):
    p = line.split()
    if len(p) >= 4:
        hashes[int(p[1], 16)] = (p[2], p[3])
maskless = {h for h, (s, m) in hashes.items() if m == 'mask=255' or m == 'mask=0'}
total = seen = hit = 0
byres = {}
uniq = {}
for line in open(sys.argv[2]):
    p = line.split()
    if len(p) != 3: continue
    res, vals, crc = p[0], p[1], int(p[2], 16)
    total += 1
    r = byres.setdefault(res, [0, 0])
    r[0] += 1
    if crc in hashes:
        hit += 1
        r[1] += 1
    uniq.setdefault(crc, [res, 0])[1] += 1
print(f"file: {len(hashes)} hashes ({len(maskless)} maskless-ish)")
print(f"captured: {total} frames, {len(uniq)} unique CRCs, direct-hash hits {hit}")
for res, (n, h) in sorted(byres.items()):
    print(f"  {res}: {n} frames, {h} direct hash hits")
