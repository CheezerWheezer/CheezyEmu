"""fg_crop.py <dir> <session> <x0> <y0> <x1> <y1> [scale] : real0 | generated | real1 crops side by side."""
import glob, os, re, struct, sys, zlib
d, n, x0, y0, x1, y1 = sys.argv[1], sys.argv[2], *map(int, sys.argv[3:7])
scale = int(sys.argv[7]) if len(sys.argv) > 7 else 2

def load(what):
    path = glob.glob(os.path.join(d, f"{n}_{what}_*.raw"))[0]
    m = re.search(r"_(\d+)x(\d+)_(\d+)_id", path)
    w, h = int(m.group(1)), int(m.group(2))
    return w, h, open(path, "rb").read()

rows = [bytearray() for _ in range((y1 - y0) * scale)]
for what in ("real0", "generated", "real1"):
    w, h, data = load(what)
    for y in range(y0, y1):
        line = data[(y * w + x0) * 4:(y * w + x1) * 4]
        rgb = bytearray((x1 - x0) * 3)
        rgb[0::3] = line[2::4]; rgb[1::3] = line[1::4]; rgb[2::3] = line[0::4]
        if scale > 1:
            wide = bytearray()
            for i in range(0, len(rgb), 3):
                wide += rgb[i:i + 3] * scale
            rgb = wide
        for k in range(scale):
            rows[(y - y0) * scale + k] += rgb + b"\xff\x00\xff" * 2
W = len(rows[0]) // 3
raw = b"".join(b"\x00" + bytes(r) for r in rows)
def chunk(kind, payload):
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff)
out = os.path.join(d, f"{n}_crop_{x0}_{y0}.png")
open(out, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, len(rows), 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 3)) + chunk(b"IEND", b""))
print(out, W, len(rows))
