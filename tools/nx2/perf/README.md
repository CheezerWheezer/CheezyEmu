# In-game performance testing (CheezyEmu, Windows)

Scripts used to measure changes in the real game instead of guessing. They drive the debug
server (`EDEN_NX2_DEBUG_SERVER`, docs/nx2/debug-mcp.md): no desktop screenshots, no keystrokes.

The test instance is the dev build (`build/dev2/bin/cheezyemu.exe`) with a portable `user`
folder next to it (keys, config, nand/system, saves and the game's shader cache copied from the
real data folder), so the player's own saves and settings are never touched. Paths at the top of
the scripts are this PC's: edit them elsewhere.

- `run_game.py [--env NAME=VALUE]... [--label TEXT]`: restarts the test instance, loads the
  first save (keeps pressing A until the log shows gameplay), settles, prints frame times.
  `--measure-only` measures the running instance, `--stop` ends it (only the process it started).
- `setcfg.py name=value ...`: per-game settings of the test instance (clears the "default" flag,
  which otherwise makes the emulator write the default back).
- `mapsym.py <exe.map> <samples.raw> [top]`: names the sampler's `cheezyemu.exe+0x...` frames
  with the linker map (the debug server's `sample` tool writes the .raw next to its report).
- `leaf.py <map> <raw> <thread>`: for time spent outside the exe (waits, driver, CRT), which of
  our functions called it. `stacks.py <map> <raw> <function>`: the call chains into a function.

- `ab.py [--knob N]`: flips a debug knob of the running game on and off and pairs the frame
  times (whole runs move by 10% or more with what else the PC is doing).
- `move_test.py [--route 2]`: runs and turns for about 75 s along a fixed path and prints the
  frame times of that stretch. Stutter shows while moving, not standing: start the game with
  `--env EDEN_NX2_STUTTER_TRACE=1` and read the `Nx2Stutter` lines for what made each long frame.
  The second pass over a route is warm (its pipelines are saved): to compare two builds on fresh
  ground use a route neither has run, and do not copy the shader folder to reset it (the copy
  changes a file time and the next boot precompiles compute programs for eight minutes).

Also useful: `EDEN_NX2_GPU_PROFILE=1` (per-second zones of the GPU thread, and why the split's
front waited for its back), `EDEN_NX2_MME_DMA_STATS=1` (semaphore acquire statistics).

## Baseline (2026-11-09, i5-12600K + RX 6750 XT, handheld, Max Resolution on, Hyrule Field)

| Setup | Frame | FPS |
|---|---|---|
| No FSR, no frame generation | 17.4 ms | 57.3 |
| FSR 3.1 (1080p -> 1080p) | 23.1 -> 21.9 ms | 43.4 -> 45.6 |
| FSR 4.1.2 | 25.2 ms | 39.7 |
| FSR 4.1.2 + AMD frame generation, DMA Safe | 33.5 -> 28.9 ms | 29.8 -> 34.6 |

The graphics card is at about 65% load in every row: the frame is paced by the GPU-emulation
threads. Per second of wall time the split's front waits for its back about 650 ms (semaphore
acquires 277 ms, command list writers 210 ms, macro memory reads 163 ms) while the back is busy
78% and starved 22%: the two mostly take turns. Overlapping them fully is worth up to ~28%.
