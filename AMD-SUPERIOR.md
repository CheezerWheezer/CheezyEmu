# Lichii AMD Superior

A Lichii build for **AMD graphics cards**. It combines the community's AMD performance work with a
new feature: **compiled shaders are saved to disk**, so each shader is compiled once instead of
every time you start the game.

It runs and behaves like normal Lichii on every other GPU. Built on
[zortos293/Lichii](https://github.com/zortos293/Lichii).

## What's different from Lichii builds 95 / 115

### New in this build

- **Compiled shaders are saved (`VK_KHR_pipeline_binary`).** AMD's Windows driver does not keep
  compiled shaders between sessions, so plain Lichii recompiles every cached shader on every
  boot and stutters while doing it. This build stores what the driver compiles and hands it
  back on later runs.
  - Measured on an RX 6750 XT (driver 26.10.1), Ocarina of Time: boot to the title screen went
    from **513 s to 14 s**; building all ~20,000 pipelines of a shared shader pack went from
    **10-18 minutes on every boot to about 30 seconds**. A saved shader takes 0.2-0.4 ms
    instead of 2.5-5 ms (often seconds at boot).
  - Works for **every game**, one file per game:
    `%APPDATA%\eden\cache\shader\<title id>\vulkan_binaries.fork-v54.bin`
  - On by default. Setting: *Graphics > Advanced > Save compiled shaders*. It is skipped on
    NVIDIA (its driver already caches) and on drivers without `VK_KHR_pipeline_binary`.
  - After a driver update the old file is replaced, not kept. Removing the game's pipeline
    cache from the game list (right-click > Remove) deletes it too.
- **Shader packs made on any GPU now work.** Lichii skipped every cached pipeline built on a
  different driver, so a pack made on NVIDIA showed only a fraction of its shaders on AMD.
  Those pipelines are now accepted and compiled for your GPU.

### From [conongnang/Lichii `oot-amd-perf`](https://github.com/conongnang/Lichii/releases/tag/oot-amd-perf-2)

This build is based on conongnang's branch. Their release notes credit each port and its author;
the main changes are:

- **No more 11 ms GPU stall per frame on AMD** from occlusion query reads (port by Matt Artist /
  mja00, from chuppatricky's AMD testbuild).
- **Hyrule Field's white sky fixed** (zortos293/Lichii#422, port of #351 by jpkto); out-of-bounds
  storage reads return zeros on AMD (`robustBufferAccess2`, mja00).
- Uniform banks no longer break render passes; precise split-front macro DMA reads (#345,
  Celmax); Ocarina's CPU reads of GPU-written pages are preflushed; faster software HMMA (#174,
  mja00).
- About 30 open Lichii PRs merged, including #415 (daniel-lynch), #434 (jpkto), **#463 stale
  texture uploads (madebyjordan)**, #376 (Moraeszz2), #446 (ne1i), #481 / #478 / #488
  (mstampfli), **#448 AMD terrain lines (persluffy-cl)**, #250 / #264 / #288 (tibzejoker,
  Kajcsi), #296 (SmileyTatsu), #300, #245, #333, #427, #436, #440, #495, #437.
- From Eglantine: Switch 2 font service (Alleexxi), puller fence release and macro query flushes
  (kraibse), buffer cache and memory layout speedups (zortos293), guest RAM release on stop
  (SmileyTatsu), transform feedback offset fix (CloudP4bbs), ports of PreTenD's TLD4 register
  limit and Jonas's partial-Preflushable flush guard.
- AMD fixes from mja00's and chuppatricky's branches: depth-only sampler comparison and blend
  state rebuilds (sharkiix), a null texture for handles past the pool (chuppatricky), a fresh
  start when the driver rejects the pipeline cache (Erick).
- Lichii #337 is reverted (cost ~9 fps in Castle Town).

### Also merged

- [zortos293/Lichii#555](https://github.com/zortos293/Lichii/pull/555) by ne1i: shaders built by
  the "build every shader at boot" switch are no longer thrown away a few minutes later.

### Build

- Windows release is compiled like the official one: clang-cl 22.1.8, ThinLTO and
  profile-guided optimization with the Ocarina of Time profile (`lichii-windows.profdata`).
- sirit is built from source with conongnang's atomic-float-add patch
  (`YUZU_USE_BUNDLED_SIRIT=OFF`), which also lets GitHub Actions build Linux.

## Setup

1. Extract the whole archive and run `lichii.exe` (keys, firmware and games are not included).
2. Your existing saves, settings and caches in `%APPDATA%\eden` are used as they are.

### Shader packs

Shader packs work the same way as before; now they work no matter which GPU they were made on.

1. Close Lichii.
2. Copy the pack's files into the game's shader folder, for Ocarina of Time:
   `%APPDATA%\eden\cache\shader\0400486024dec000\`
   - `vulkan.fork-v54.bin` (the shaders)
   - `vulkan_used.fork-v54.bin` (which ones the game uses first), if the pack has it
3. Start the game. Shaders compile once on your PC and are saved; later boots load them.

The pack's shaders must match the build's cache version (`fork-v54` here).

### Precompile everything once (optional)

By default the game builds the shaders it used recently at boot and the rest the first time they
appear. To build **all** of a pack's shaders once, so nothing ever compiles while you play:

1. Open `nx2_switches.txt` next to `lichii.exe` in Notepad.
2. Add this line and save:
   ```
   EDEN_NX2_PRELOAD_ALL_PIPELINES=1
   ```
3. Start the game and wait on the loading screen until the game starts (about 12 minutes for
   Ocarina's 20,000 shaders on an i5-12600K; it only has to happen once).
4. Close Lichii. Every shader is now saved.
5. Keep the line to build everything at every boot (about 30 seconds once saved, but it keeps
   every shader in memory: about 10 GB of RAM for Ocarina), or put `#` in front of it to go back
   to building only the recent ones.

### Fix the grid lines on distant terrain (AMD)

Some AMD cards draw thin dark lines in a grid on far-away ground and grass. Turning off GPU
texture re-compression fixes it (found in zortos293/Lichii#448 by persluffy-cl). It uses more
video memory, so it is off by default:

1. Open `nx2_switches.txt` next to `lichii.exe` in Notepad.
2. Find this line and remove the `#` in front of it (or add the line):
   ```
   EDEN_NX2_KEEP_ASTC=1
   ```
3. Save and restart Lichii.

### Optional: steadier frame rate on AMD

Sending GPU work in larger batches suits AMD's driver. On an RX 6750 XT in Hyrule Field it gave
about +1 fps and a steadier frame rate (31-33 fps instead of 27-33). Add this line to
`nx2_switches.txt` next to `lichii.exe`:

```
EDEN_NX2_FLUSH_DRAWS=512
```

## Credits

All of the performance and rendering work above belongs to its authors, listed in the release
notes of [conongnang/Lichii](https://github.com/conongnang/Lichii/releases) and in each commit.
Lichii is by [zortos293](https://github.com/zortos293/Lichii), built on Eglantine, Eden, yuzu and
Sudachi.
