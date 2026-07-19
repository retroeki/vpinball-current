# Brute-force which 128x16 -> 128x32 transform reproduces trek_201's hashed frames.
# frames.log lines: 2048 hex chars (one 128x16 frame, values 0-15)
# usage: python transform16.py trek_hashes.txt frames.log
import sys, zlib
import numpy as np

hashes = set()
maskless = set()
for line in open(sys.argv[1]):
    p = line.split()
    if len(p) >= 4:
        h = int(p[1], 16)
        hashes.add(h)
        if p[3] == 'mask=255':
            maskless.add(h)

frames = []
for line in open(sys.argv[2]):
    line = line.strip()
    if len(line) == 2048:
        frames.append(np.frombuffer(bytes.fromhex(''.join(
            '0' + c for c in line)), dtype=np.uint8).reshape(16, 128))
print(f"captured {len(frames)} raw 16-high frames; pack: {len(hashes)} hashes ({len(maskless)} maskless)")

Z8 = np.zeros((8, 128), dtype=np.uint8)
Z16 = np.zeros((16, 128), dtype=np.uint8)

def transforms(f):
    yield 'rowdouble', np.repeat(f, 2, axis=0)
    yield 'center', np.vstack([Z8, f, Z8])
    yield 'top', np.vstack([f, Z16])
    yield 'bottom', np.vstack([Z16, f])
    yield 'interleave-below', np.vstack([f, Z16]).reshape(2, 16, 128).transpose(1, 0, 2).reshape(32, 128)   # row, blank, row, blank...
    yield 'interleave-above', np.vstack([Z16, f]).reshape(2, 16, 128).transpose(1, 0, 2).reshape(32, 128)   # blank, row, blank, row...
    yield 'colwise-256', None  # placeholder, handled separately

counts = {}
for f in frames:
    for name, t in transforms(f):
        if t is None:
            continue
        c = zlib.crc32(t.tobytes()) & 0xffffffff
        m, h = counts.get(name, (0, 0))
        counts[name] = (m + (1 if c in maskless else 0), h + (1 if c in hashes else 0))

print("transform            maskless-hits  full-hits")
for name, (m, h) in sorted(counts.items(), key=lambda kv: -kv[1][0]):
    print(f"{name:20s} {m:12d} {h:10d}")
