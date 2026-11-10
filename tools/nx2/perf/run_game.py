"""run_game.py [--env NAME=VALUE ...] [--settle S] [--measure S] [--label TEXT] [--keep]
Restarts the test instance of the dev build (its own portable 'user' folder), loads the first
save through the debug server, and prints frame times. Only ever stops the process it started."""
import argparse
import json
import os
import socket
import subprocess
import sys
import time

# The dev build's folder (its portable "user" folder is the test instance).
BIN = os.environ.get("CHEEZYEMU_TEST_BIN") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "build", "dev2", "bin")
# The game file to boot: set CHEEZYEMU_TEST_GAME to its path.
GAME = os.environ.get("CHEEZYEMU_TEST_GAME", "")
PIDFILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "test_instance.pid")
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


def stop():
    if not os.path.exists(PIDFILE):
        return
    pid = open(PIDFILE).read().strip()
    subprocess.run(["taskkill", "/PID", pid, "/F"], capture_output=True)
    os.remove(PIDFILE)
    time.sleep(3)


def wait_presents(minimum, timeout):
    end = time.time() + timeout
    while time.time() < end:
        try:
            st = call("status", {"fps_window_ms": 1000}, timeout=10)
            if st.get("presents", 0) >= minimum:
                return st
        except OSError:
            pass
        except RuntimeError:
            pass
        time.sleep(2)
    raise SystemExit("the game did not start presenting in time")


LOG = os.path.join(BIN, "user", "log", "eden_log.txt")


def in_gameplay():
    import re
    try:
        with open(LOG, "rb") as f:
            f.seek(0, 2)
            f.seek(max(0, f.tell() - 200000))
            tail = f.read().decode("utf-8", "replace")
    except OSError:
        return False
    counts = re.findall(r"Buffer cache: (\d+) buffers", tail)
    return bool(counts) and int(counts[-1]) > 100


def press(button, frames=6):
    return call("input", {"buttons": [button], "frames": frames})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--settle", type=float, default=25)
    ap.add_argument("--measure", type=float, default=10)
    ap.add_argument("--label", default="")
    ap.add_argument("--stop", action="store_true", help="only stop the test instance")
    ap.add_argument("--measure-only", action="store_true")
    args = ap.parse_args()
    if args.stop:
        stop()
        return
    if not args.measure_only:
        stop()
        env = dict(os.environ)
        env["EDEN_NX2_DEBUG_SERVER"] = "127.0.0.1:6545"
        for pair in args.env:
            name, _, value = pair.partition("=")
            env[name] = value
        flags = 0x00000008 | 0x00000200  # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP
        proc = subprocess.Popen([os.path.join(BIN, "cheezyemu.exe"), GAME], cwd=BIN, env=env,
                                creationflags=flags, stdin=subprocess.DEVNULL,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        open(PIDFILE, "w").write(str(proc.pid))
        st = wait_presents(300, 300)
        time.sleep(8)      # title screen fade-in
        # Continue > first save > Load. A press can be dropped while a menu animates, so keep
        # pressing A until the log shows gameplay (hundreds of buffers; menus have about ten).
        # A press can be dropped while a menu animates: press, then give the load a minute to show
        # in the log, and press again if it did not.
        for attempt in range(10):
            press("A", 8)
            end = time.time() + (6 if attempt < 3 else 45)
            while time.time() < end and not in_gameplay():
                time.sleep(2)
            if in_gameplay():
                break
        else:
            raise SystemExit("never reached gameplay")
        time.sleep(args.settle)
    ft = call("frame_times", {"seconds": args.measure}, timeout=args.measure + 60)
    st = call("status", {"fps_window_ms": 2000})
    print(f"{args.label or 'run'}: {ft.get('fps', 0):.1f} fps, mean {ft.get('mean_ms', 0):.1f} ms, "
          f"p50 {ft.get('p50_ms', 0):.1f}, p99 {ft.get('p99_ms', 0):.1f}, max {ft.get('max_ms', 0):.1f}, "
          f"stutters {ft.get('stutters')}, presents {st.get('presents')}")


if __name__ == "__main__":
    main()
