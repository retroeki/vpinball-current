# Brute-force the combining rule that reproduces the pack's hashed frames.
# subframes.bin: repeated [u32 submissionIndex][512-byte 128x32 bitplane]
# For each (period, phase, level-mapping): combine aligned blocks, CRC32, and
# count membership in the .cROMc hash table (maskless and full).
# usage: python3 phasescan.py smb3_hashes.txt subframes.bin
import sys, struct, zlib
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

data = open(sys.argv[2], 'rb').read()
REC = 4 + 512
n = len(data) // REC
idxs = np.empty(n, dtype=np.uint32)
planes = np.empty((n, 512), dtype=np.uint8)
for i in range(n):
    off = i * REC
    idxs[i] = struct.unpack_from('<I', data, off)[0]
    planes[i] = np.frombuffer(data, dtype=np.uint8, count=512, offset=off + 4)

# verify continuity
gaps = np.count_nonzero(np.diff(idxs.astype(np.int64)) != 1)
print(f"subframes: {n}, discontinuities: {gaps}")

bits = np.unpackbits(planes, axis=1)  # MSB-first == pixel order used by the identify combiner

level5 = np.array([0,3,3,7,7,7,11,11,11,11,11,11,15], dtype=np.uint8)

def crc_of(frame_u8):
    return zlib.crc32(frame_u8.tobytes()) & 0xffffffff

results = []
for period in (12, 24, 6, 8):
    mappings = {}
    if period == 12:
        mappings['level5'] = lambda s: level5[s]
        mappings['trunc15'] = lambda s: ((s * 15) // 12).astype(np.uint8)
        mappings['round15'] = lambda s: ((s * 15 + 6) // 12).astype(np.uint8)
    elif period == 24:
        mappings['trunc15'] = lambda s: ((s * 15) // 24).astype(np.uint8)
        mappings['round15'] = lambda s: ((s * 15 + 12) // 24).astype(np.uint8)
    elif period == 6:
        mappings['trunc15'] = lambda s: ((s * 15) // 6).astype(np.uint8)
    elif period == 8:
        mappings['trunc15'] = lambda s: ((s * 15) // 8).astype(np.uint8)
    for phase in range(period):
        # block starts at first index >= idxs[0] with (idx - phase) % period == 0
        start = (phase - int(idxs[0])) % period
        nblocks = (n - start) // period
        if nblocks <= 10:
            continue
        blocks = bits[start:start + nblocks * period].reshape(nblocks, period, 4096)
        sums = blocks.sum(axis=1)  # nblocks x 4096, values 0..period
        for mname, mfn in mappings.items():
            frames = mfn(sums)
            hitsm = hitsf = distinct = 0
            seen = set()
            last = None
            for b in range(nblocks):
                fb = frames[b]
                tb = fb.tobytes()
                if tb == last:
                    continue
                last = tb
                distinct += 1
                c = zlib.crc32(tb) & 0xffffffff
                if c in maskless:
                    hitsm += 1
                if c in hashes:
                    hitsf += 1
            results.append((hitsm, hitsf, distinct, period, phase, mname))

results.sort(reverse=True)
print("maskless  full  distinct  period phase mapping")
for r in results[:15]:
    print(f"{r[0]:8d} {r[1]:5d} {r[2]:9d} {r[3]:6d} {r[4]:5d} {r[5]}")
