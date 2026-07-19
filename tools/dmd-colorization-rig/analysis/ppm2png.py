# Convert dumped PPM frames to 4x-upscaled PNGs (minimal encoder, zlib only).
# usage: python ppm2png.py frames/*.ppm
import sys, zlib, struct, glob, os

def png_chunk(tag, data):
    c = tag + data
    return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)

def convert(path, scale=4):
    with open(path, 'rb') as f:
        assert f.readline().strip() == b'P6'
        w, h = map(int, f.readline().split())
        f.readline()
        rgb = f.read(w * h * 3)
    W, H = w * scale, h * scale
    raw = bytearray()
    for y in range(H):
        raw.append(0)
        sy = y // scale
        row = rgb[sy * w * 3:(sy + 1) * w * 3]
        for x in range(W):
            sx = x // scale
            raw += row[sx * 3:sx * 3 + 3]
    png = (b'\x89PNG\r\n\x1a\n'
           + png_chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0))
           + png_chunk(b'IDAT', zlib.compress(bytes(raw), 6))
           + png_chunk(b'IEND', b''))
    out = os.path.splitext(path)[0] + '.png'
    open(out, 'wb').write(png)
    return out

files = []
for pat in sys.argv[1:]:
    files.extend(glob.glob(pat))
for p in sorted(files):
    convert(p)
print(f"converted {len(files)}")
