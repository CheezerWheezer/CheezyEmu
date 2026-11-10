"""mv_crop.py <dir> <session> <x0> <y0> <x1> <y1> [gain_px]: motion minus its median (camera), x red y green,
grey = same as the camera motion; prints stats for the crop. Coordinates in the motion image."""
import glob, os, re, struct, sys, zlib
d, n, x0, y0, x1, y1 = sys.argv[1], sys.argv[2], *map(int, sys.argv[3:7])
gain = float(sys.argv[7]) if len(sys.argv) > 7 else 16.0
path = glob.glob(os.path.join(d, f"{n}_motion_*.raw"))[0]
m = re.search(r"_(\d+)x(\d+)_(\d+)_id", path)
w, h = int(m.group(1)), int(m.group(2))
data = open(path, "rb").read()
rows = []
xs_all = []; ys_all = []
for y in range(y0, y1):
    vals = struct.unpack(f"<{(x1 - x0) * 2}e", data[(y * w + x0) * 4:(y * w + x1) * 4])
    rows.append(vals); xs_all += vals[0::2]; ys_all += vals[1::2]
mx = sorted(xs_all)[len(xs_all) // 2]; my = sorted(ys_all)[len(ys_all) // 2]
print(f"crop median motion ({mx:+.2f}, {my:+.2f}) px; x range {min(xs_all):+.1f}..{max(xs_all):+.1f}, y range {min(ys_all):+.1f}..{max(ys_all):+.1f}")
def c8(v):
    v = (v / gain + 0.5) * 255
    return 0 if v < 0 else 255 if v > 255 else int(v)
raw = bytearray()
for vals in rows:
    raw += b"\x00"
    line = bytearray((x1 - x0) * 3)
    line[0::3] = bytes(c8(v - mx) for v in vals[0::2])
    line[1::3] = bytes(c8(v - my) for v in vals[1::2])
    line[2::3] = bytes([128]) * (x1 - x0)
    raw += line
def chunk(kind, payload):
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff)
out = os.path.join(d, f"{n}_mvcrop_{x0}_{y0}.png")
open(out, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", x1 - x0, y1 - y0, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(bytes(raw), 3)) + chunk(b"IEND", b""))
print(out)
