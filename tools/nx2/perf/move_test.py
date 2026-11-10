"""move_test.py [--label TEXT]: in the running test instance, runs and turns for about 75 s
(the same path every time), then prints the frame times of that stretch: stutter shows while
moving, not standing."""
import argparse, json, socket, time

ADDR = ("127.0.0.1", 6545)


def call(method, params=None, timeout=300):
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
ap.add_argument("--label", default="move")
ap.add_argument("--route", type=int, default=1, help="2: a longer run the other way (new ground)")
args = ap.parse_args()
steps = []
if args.route == 2:
    steps += [
        {"right_stick": [-1, 0], "frames": 90},                             # turn the camera away
        {"left_stick": [0, 1], "frames": 1500},                             # long run forward
        {"left_stick": [0, 1], "right_stick": [-0.7, 0], "frames": 300},
        {"left_stick": [0, 1], "frames": 1200},
        {"left_stick": [-1, 0], "right_stick": [0.7, 0], "frames": 600},
        {"frames": 30},
    ]
for lap in range(3 if args.route == 1 else 0):
    steps += [
        {"left_stick": [0, 1], "frames": 360},                              # run forward
        {"left_stick": [0, 1], "right_stick": [1, 0], "frames": 120},       # run, camera right
        {"right_stick": [-1, 0], "frames": 150},                            # stand, camera left
        {"left_stick": [1, 0], "frames": 240},                              # run right
        {"left_stick": [0, -1], "right_stick": [0.6, 0], "frames": 240},    # run back, turning
        {"frames": 30},
    ]
begin = time.time()
call("input_sequence", {"steps": steps, "timeout_s": 240}, timeout=300)
seconds = time.time() - begin
ft = call("frame_times", {"past": min(seconds, 590)})
iv = ft.get("intervals_ms", [])
print(f"{args.label}: {seconds:.0f} s, {ft.get('fps', 0):.1f} fps, mean {ft.get('mean_ms', 0):.1f} ms, "
      f"p50 {ft.get('p50_ms', 0):.1f}, p90 {ft.get('p90_ms', 0):.1f}, p99 {ft.get('p99_ms', 0):.1f}, "
      f"max {ft.get('max_ms', 0):.1f}, stutters {ft.get('stutters')}, frames {ft.get('frames')}, "
      f"truncated {ft.get('truncated')}")
