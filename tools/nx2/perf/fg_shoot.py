"""fg_shoot.py <outdir>: while Link runs / the camera turns, arms the frame generation capture
(debug knob 1) a few times, then converts the raw files to PNG (and a motion-vector picture)."""
import glob, json, os, re, socket, struct, sys, threading, time
import zlib

ADDR = ("127.0.0.1", 6545)
out = sys.argv[1]


def call(method, params=None, timeout=120):
    with socket.create_connection(ADDR, timeout=timeout) as s:
        s.sendall((json.dumps({"id": 1, "method": method, "params": params or {}}) + "\n").encode())
        buf = b""
        while not buf.endswith(b"\n"):
            chunk = s.recv(1 << 20)
            if not chunk:
                break
            buf += chunk
    reply = json.loads(buf)
    if "error" in reply:
        raise RuntimeError(reply["error"])
    return reply["result"]


scenarios = [
    ("turn", [{"right_stick": [1, 0], "frames": 200}]),
    ("run", [{"left_stick": [0, 1], "frames": 200}]),
    ("run_turn", [{"left_stick": [0, 1], "right_stick": [0.8, 0], "frames": 200}]),
    ("strafe", [{"left_stick": [1, 0], "frames": 200}]),
]
if len(sys.argv) > 2 and sys.argv[2] == "--convert-only":
    scenarios = []
for name, steps in scenarios:
    t = threading.Thread(target=lambda: call("input_sequence", {"steps": steps, "timeout_s": 60}, 90))
    t.start()
    time.sleep(1.5)
    call("knobs", {"value": 2})
    time.sleep(1.2)
    call("knobs", {"value": 0})
    t.join()
    time.sleep(1.0)
    print("shot", name)
time.sleep(1.0)


def write_png(path, w, h, rgb):
    """rgb: bytes of w*h*3."""
    stride = w * 3
    raw = b"".join(b"\x00" + rgb[y * stride:(y + 1) * stride] for y in range(h))

    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff)

    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(raw, 3)) + chunk(b"IEND", b""))


def clamp8(x):
    return 0 if x < 0 else 255 if x > 255 else int(x)


for path in sorted(glob.glob(os.path.join(out, "*.raw"))):
    m = re.match(r"(\d+)_(\w+?)_(\d+)x(\d+)_(\d+)_id(\d+)\.raw", os.path.basename(path))
    if not m:
        continue
    n, what, w, h, fmt, fid = m.group(1), m.group(2), int(m.group(3)), int(m.group(4)), int(m.group(5)), m.group(6)
    data = open(path, "rb").read()
    png = os.path.join(out, f"{n}_{what}.png")
    if what == "motion":
        values = struct.unpack(f"<{w * h * 2}e", data)
        xs = values[0::2]
        ys = values[1::2]
        mags = sorted((x * x + y * y) ** 0.5 for x, y in zip(xs, ys))
        zero = sum(1 for v in mags if v == 0.0)
        print(f"{n} motion id{fid}: {w}x{h} x mean {sum(xs) / len(xs):+.2f} y mean {sum(ys) / len(ys):+.2f} "
              f"|mv| median {mags[len(mags) // 2]:.2f} p99 {mags[int(len(mags) * 0.99)]:.2f} max {mags[-1]:.1f} "
              f"zero {100 * zero / len(mags):.1f}%")
        rgb = bytearray(w * h * 3)
        rgb[0::3] = bytes(clamp8((x / 32.0 + 0.5) * 255) for x in xs)
        rgb[1::3] = bytes(clamp8((y / 32.0 + 0.5) * 255) for y in ys)
        rgb[2::3] = bytes([128]) * (w * h)
        write_png(png, w, h, bytes(rgb))
    elif what == "depth":
        values = struct.unpack(f"<{w * h}f", data)
        ordered = sorted(values)
        far = sum(1 for v in values if v >= 1.0)
        print(f"{n} depth id{fid}: min {ordered[0]:.5f} median {ordered[len(ordered) // 2]:.5f} max {ordered[-1]:.5f} "
              f"at 1.0: {100 * far / len(values):.1f}%")
        grey = bytes(clamp8((min(max(v, 0.0), 1.0) ** 40) * 255) for v in values)
        rgb = bytearray(w * h * 3)
        rgb[0::3] = grey
        rgb[1::3] = grey
        rgb[2::3] = grey
        write_png(png, w, h, bytes(rgb))
    else:
        rgb = bytearray(w * h * 3)
        if fmt in (44, 50):      # B8G8R8A8 unorm / srgb
            rgb[0::3] = data[2::4]
            rgb[1::3] = data[1::4]
            rgb[2::3] = data[0::4]
        elif fmt in (37, 43):    # R8G8B8A8
            rgb[0::3] = data[0::4]
            rgb[1::3] = data[1::4]
            rgb[2::3] = data[2::4]
        else:
            words = struct.unpack(f"<{w * h}I", data)   # A2B10G10R10 (64) or A2R10G10B10 (58)
            c0 = bytes(((v >> 0) & 1023) >> 2 for v in words)
            c1 = bytes(((v >> 10) & 1023) >> 2 for v in words)
            c2 = bytes(((v >> 20) & 1023) >> 2 for v in words)
            rgb[0::3] = c0 if fmt == 64 else c2
            rgb[1::3] = c1
            rgb[2::3] = c2 if fmt == 64 else c0
        write_png(png, w, h, bytes(rgb))
        print(f"{n} {what} id{fid}: {w}x{h} format {fmt}")
