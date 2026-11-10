"""ab.py [--knob N] [--rounds R] [--seconds S]: flips debug knob N of the running test instance
on and off R times and compares the frame times of both, in the same scene and process.
Machine noise (other programs) moves whole runs by 10% or more; alternating windows cancel it."""
import argparse, json, socket, statistics, time

ADDR = ("127.0.0.1", 6545)


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


ap = argparse.ArgumentParser()
ap.add_argument("--knob", type=int, default=0)
ap.add_argument("--rounds", type=int, default=8)
ap.add_argument("--seconds", type=float, default=5)
args = ap.parse_args()
arms = {0: [], 1: []}
for index in range(args.rounds * 2):
    on = index % 2
    call("knobs", {"value": (1 << args.knob) if on else 0})
    time.sleep(1.0)
    ft = call("frame_times", {"seconds": args.seconds}, timeout=args.seconds + 60)
    arms[on].append(ft.get("mean_ms", 0))
call("knobs", {"value": 0})
off, on = arms[0], arms[1]
diffs = [b - a for a, b in zip(off, on)]
print(f"knob {args.knob} off: {statistics.mean(off):.2f} ms ({1000 / statistics.mean(off):.1f} fps)   "
      f"on: {statistics.mean(on):.2f} ms ({1000 / statistics.mean(on):.1f} fps)")
print(f"on - off per pair: mean {statistics.mean(diffs):+.2f} ms, sd {statistics.pstdev(diffs):.2f}; "
      f"pairs {['%+.2f' % d for d in diffs]}")
