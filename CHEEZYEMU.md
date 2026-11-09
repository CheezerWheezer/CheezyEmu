# CheezyEmu

CheezyEmu is a Switch and Switch 2 emulator for **AMD graphics cards**, and runs on every other GPU
too. It started as the "AMD Superior" fork of [zortos293/Lichii](https://github.com/zortos293/Lichii)
build 117 (`89440b2`) and is now its own emulator. Its main additions: compiled shaders saved to disk
(each shader is compiled once instead of every time you start the game), AMD FSR 4 and FSR 3.1
upscaling, AMD frame generation, and the community's AMD performance work.

## New in build 10

How to use all of this: **[README.txt](README.txt)**, also next to `cheezyemu.exe` in every download.

- **Now its own emulator: CheezyEmu.** New name, new cheese icon, `cheezyemu.exe`. Saves, settings
  and shader caches stay where they were. In-app updates from build 9 can't find build 10 (the
  files have new names): download it once from the releases page.
- **New look**: the CheezyEmu theme (warm charcoal and cheese yellow, rounded buttons, yellow
  headers, menu and status bar lines) is the default. Players on the old default theme move to it
  once; any theme picked in Configure > General > UI sticks.
- **Game pictures for every Switch 2 game, automatically**: CheezyEmu downloads a picture list
  (dist/game_pictures.json, one SteamGridDB picture per Switch 2 title ID) and shows each game's
  picture with nothing to set up. Matching is by title ID, so it's the right game. The list is
  rebuilt every Monday by the game-pictures workflow from switchbrew's title list; a wrong
  picture is fixed in tools/game_pictures/overrides.json. A player's own SteamGridDB key (right-click
  a game > Game Picture > Find Picture Online) also finds pictures for games the list lacks.
  Right-click > Game Picture picks another picture, uses your own image or resets it.
- **Updates install themselves from now on**: when a newer build is out, CheezyEmu offers it at
  startup, downloads it, closes, installs and restarts (Windows zip and Linux AppImage).
- **White fog fixed** (Hyrule Field drawbridge solid white, lines in Kokiri Forest's fog): reverts
  the perspective-correct interpolation of zortos293/Lichii#820, as Fritchii v5.1 did. It was also
  suspected in an RX 6600 crash at boot. The faint red line in the Royal Family's Tomb may return.
- **No more stale vertices** (glowing or misplaced geometry in Hyrule Field and the graveyard):
  reverts the 32-byte rounding half of zortos293/Lichii#700, as Fritchii v5.1 did.
- **FSR 4 that draws black switches to FSR 3.1 by itself.** For a few seconds after a game starts,
  CheezyEmu checks FSR 4's output against the game's picture; if FSR 4 keeps drawing black (seen on
  an RX 9070 XT with driver 2.0.406), it changes to FSR 3.1 for the session and says so in the log.
  Updating the graphics driver should bring FSR 4 back.
- **Crash fix while shaders compile**: a data race in the SPIR-V emitter (zortos293/Lichii#902).
- **Less memory while shaders precache**: a compute shader's IR is freed before the driver compiles
  it (zortos293/Lichii#878).
- **Faster NUP loading**: NUP archives are decoded only as far as the game reads
  (zortos293/Lichii#885).
- **Microphone**: a microphone that can't open no longer breaks the game; it hears silence
  (zortos293/Lichii#881).
- **In-app updates retry** a download up to three times on a GitHub server error (from Fritchii).
- The "Available VRAM" log line names Conservative mode's cap only when it lowered the figure.
- The game list no longer offers "Submit compatibility report" (that website is the upstream
  project's).

## New in build 9

How to use all of this: **[README.txt](README.txt)**, also next to `cheezyemu.exe` in every download.

- **FSR 4 black picture on Radeon RX 9000 fixed.** RDNA 4 runs FSR 4's FP8 network, which needs
  `VK_EXT_shader_float8`; CheezyEmu never enabled it, so the scene came out black with a few bright
  dots. It's enabled now when the driver offers it.
- **HUD jitter with frame generation fixed** when the game renders below its output size (without
  the Max Resolution add-on, e.g. 1440x810 upscaled to 1080p, or 1080p to 1440p): the HUD mask was
  built but never applied there.
- **Much less memory at boot** (port of Frizti/Fritchii's reserved guest memory): the console's
  ~14 GB of memory is committed as the game uses it instead of all at boot, for PCs with 16 GB of
  RAM that ran out (error 1455) loading a save. `EDEN_NX2_SEC_RESERVE=0` turns it off.
- **No more lighting stuck white until a reset**: "Switch 2 draws with unbuilt pipelines" now
  defaults to *Skip only redrawn targets*, so a draw the game makes only once (an area's lighting)
  waits for its shader instead of being dropped for good (as in Fritchii v5).
- **The bar says why AMD frame generation is idle**: `(RES 1X)`, `(RESTART)`, `(OOT ONLY)`,
  `(WAITING)` in menus and cutscenes, `(WINDOWS ONLY)`; hover for the full reason.
- **Crash dumps can be read**: Windows releases include `cheezyemu-windows-x86_64[-pgo]-map.zip`, the
  link maps that match a player's `cheezyemu.exe.*.dmp` to functions.
- **Release builds retry their downloads** (GitHub's 504s failed several build 8 runs).
- README.txt: Resolution 1X, the FPS counter (real x 2) and how to lock 60, white overexposure
  and occlusion-off mods, low-RAM crashes; "Available VRAM: 7.00 GiB" in the log now says it is
  Conservative mode's cap.

## New in build 8

- **Frame generation and upscaling without FSR 4.** When AMD's runtime doesn't offer FSR 4 for a
  GPU (RX 6000/7000 on an older driver, older Radeons, NVIDIA, Intel), Ocarina now uses AMD's
  FSR 3.1 instead of turning off. In build 7 both FSR and AMD frame generation stopped on those
  GPUs. The log names the version that runs (`AMD FSR: upscaler ... running`);
  `EDEN_NX2_FSR_VERSION=3` forces FSR 3.1 to compare.
- **AMD Frame Generation Version** setting: *Auto* (FSR 4 frame generation on Radeon RX 9000,
  FSR 3.1 elsewhere) or *FSR 3.1 (faster)*, for RX 9000 owners who want the lighter one.
- **The HUD stays still in Docked (4K) mode.** With Docked mode and the Max Resolution add-on,
  Ocarina draws at 1440p and FSR outputs 4K; frame generation there had no HUD mask, so the HUD
  moved with the camera in generated frames. The 4K mode now gets its own mask.
- **Running out of video memory no longer crashes AMD drivers** (port of zortos293/Lichii#880,
  via Frizti/Fritchii): a texture that doesn't fit is left out until memory is freed, instead of
  becoming an empty image the game draws into.
- **Frame generation with FSR 4 turned off** no longer runs AMD's FSR 3.1 without the GPU's
  float16 feature it needs.
- **README.txt** also has tips for a low frame rate with FSR 4 on RX 6000/7000 (FSR 3.1, the Max
  Resolution add-on, OptiScaler) and for crashes on PCs with 16 GB of RAM.
- **README.txt** replaces HOW-TO-USE.md: plain text that opens in Notepad, next to `cheezyemu.exe`,
  and named at the top of every release's notes. It now also covers which FSR you get, shader
  precaching and low-memory crashes.

## New in build 7

- **AMD FSR 4 in Ocarina of Time.** The game's own upscaler is replaced by AMD's FSR 4
  (machine-learning upscaling and anti-aliasing) on the GPUs AMD's FSR runtime supports. Frames
  where the game lowers its resolution go back to the game's upscaler.
- **AMD FSR frame generation (Ocarina of Time).** AMD's real frame generation, the one PC games
  ship, through AMD's frame generation swapchain: one generated frame after each real one (30 FPS
  shows as 60). Settings: *Enable Frame Generation*, type *AMD FSR Frame Generation*. It works on
  AMD and other GPUs (Windows, Vulkan). What it is given:
  - the camera's motion for the whole scene, and the motion of **every animated character**
    (Link and each of his pieces, enemies, NPCs) from their bones, read in each of the ~20 vertex
    layouts the game uses;
  - a **per-pixel HUD mask**: the pixels the game's HUD draws are told apart and kept still, so
    the hearts, items and minimap don't smear when the camera turns;
  - the game's depth and camera.
  The frame counter shows real and generated frames (`Game: 60 FPS (30 + FG)`).
- **Crash fixes for frame generation:** going fullscreen (AMD's context is destroyed before its
  swapchain), stopping a game and starting another (AMD's runtime is unloaded and loaded again).
- **White fog blobs** (Kokiri Forest and elsewhere, until the area was entered again): the stale
  texture upload skip (#463) is off by default; `EDEN_NX2_TC_SKIP_STALE=1` turns it on.
- **Smoother camera turns:** the texture collector no longer waits on the GPU to save an image it
  evicts unless video memory is critical.
- **Shader cache:** a damaged record no longer throws the whole cache away and duplicate records
  are removed (zortos293/Lichii#815, adapted: the newest copy of a key is kept, as this build
  tries the newest first). A 6.9 GB Ocarina cache went to 4.6 GB. Used shaders are kept loaded
  (up to 4,000) instead of being thrown away after a few minutes.
- **Ocarina's freeze fix and RX 580 shader threads** from Spikol02/Nyamu, and upstream #817
  (no upload for a texture a copy overwrites whole); see "Also merged" below.

## What's different from CheezyEmu (builds 95 / 115 / 117)

### New in build 6

- **Compiled shaders are saved (`VK_KHR_pipeline_binary`).** AMD's Windows driver does not keep
  compiled shaders between sessions, so plain CheezyEmu recompiles every cached shader on every
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
- **Shader packs made on any GPU now work.** CheezyEmu skipped every cached pipeline built on a
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
- About 30 open CheezyEmu PRs merged, including #415 (daniel-lynch), #434 (jpkto), **#463 stale
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
- zortos293/Lichii#337 is reverted (cost ~9 fps in Castle Town).

### Also merged

- **Updates come from this fork**: the built-in updater checks the repository that built the
  release, so it offers CheezyEmu updates instead of replacing it with upstream CheezyEmu.
- Linux build fix for `vk_flash_watch` (X11 `Success` macro clash).

- [zortos293/Lichii#555](https://github.com/zortos293/Lichii/pull/555) by ne1i: shaders built by
  the "build every shader at boot" switch are no longer thrown away a few minutes later.
- [zortos293/Lichii#817](https://github.com/zortos293/Lichii/pull/817) (upstream build 119):
  textures that a copy overwrites whole are no longer uploaded first (Kokiri Forest shadow maps
  took up to 17% of the GPU back thread).
- [zortos293/Lichii#815](https://github.com/zortos293/Lichii/pull/815) (upstream build 119): a
  damaged shader cache record no longer throws the whole cache away, the cache is locked while
  written, and records the load never uses are removed. Adapted here: of graphics records with the
  same stored key the newest is kept, as this build tries them newest first.
- [Spikol02/Nyamu `ec73605`](https://github.com/Spikol02/Nyamu/commit/ec7360576f78b4181cd12908a4afbeb68d2bcfa0):
  Ocarina of Time's unaligned transform feedback outputs (46 and 89) are written again, fixing a
  freeze; RX 580 and older AMD cards build at most 2 shaders at a time so the old driver doesn't
  run out of RAM (`EDEN_NX2_PIPELINE_WORKERS=<n>` sets the count on any GPU).

### Build

- Windows release is compiled like the official one: clang-cl 22.1.8, ThinLTO and
  profile-guided optimization with the Ocarina of Time profile (`cheezyemu-windows.profdata`).
- sirit is built from source with conongnang's atomic-float-add patch
  (`YUZU_USE_BUNDLED_SIRIT=OFF`), which also lets GitHub Actions build Linux.

## Setup

1. Extract the whole archive and run `cheezyemu.exe` (keys, firmware and games are not included).
2. Your existing saves, settings and caches in `%APPDATA%\eden` are used as they are.

### Shader packs

Shader packs work the same way as before; now they work no matter which GPU they were made on.

1. Close CheezyEmu.
2. Copy the pack's files into the game's shader folder, for Ocarina of Time:
   `%APPDATA%\eden\cache\shader\0400486024dec000\`
   - `vulkan.fork-v54.bin` (the shaders)
   - `vulkan_used.fork-v54.bin` (which ones the game uses first), if the pack has it
3. Start the game. Shaders compile once on your PC and are saved; later boots load them.

The pack's shaders must match the build's cache version (`fork-v54` here).

### Precompile everything once (optional)

By default the game builds the shaders it used recently at boot and the rest the first time they
appear. To build **all** of a pack's shaders once, so nothing ever compiles while you play:

1. Open `nx2_switches.txt` next to `cheezyemu.exe` in Notepad.
2. Add this line and save:
   ```
   EDEN_NX2_PRELOAD_ALL_PIPELINES=1
   ```
3. Start the game and wait on the loading screen until the game starts (about 12 minutes for
   Ocarina's 20,000 shaders on an i5-12600K; it only has to happen once).
4. Close CheezyEmu. Every shader is now saved.
5. Keep the line to build everything at every boot (about 30 seconds once saved, but it keeps
   every shader in memory: about 10 GB of RAM for Ocarina), or put `#` in front of it to go back
   to building only the recent ones.

### Fix the grid lines on distant terrain (AMD)

Some AMD cards draw thin dark lines in a grid on far-away ground and grass. Turning off GPU
texture re-compression fixes it (found in zortos293/Lichii#448 by persluffy-cl). It uses more
video memory, so it is off by default:

1. Open `nx2_switches.txt` next to `cheezyemu.exe` in Notepad.
2. Find this line and remove the `#` in front of it (or add the line):
   ```
   EDEN_NX2_KEEP_ASTC=1
   ```
3. Save and restart CheezyEmu.

### Optional: steadier frame rate on AMD

Sending GPU work in larger batches suits AMD's driver. On an RX 6750 XT in Hyrule Field it gave
about +1 fps and a steadier frame rate (31-33 fps instead of 27-33). Add this line to
`nx2_switches.txt` next to `cheezyemu.exe`:

```
EDEN_NX2_FLUSH_DRAWS=512
```

## Credits

All of the performance and rendering work above belongs to its authors, listed in the release
notes of [conongnang/Lichii](https://github.com/conongnang/Lichii/releases) and in each commit.
FSR 4 and FSR frame generation are AMD's
[FidelityFX SDK 2.4](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) (MIT); the
shader cache repair is zortos293/Lichii#815 by zortos293 and jpkto; the Ocarina freeze fix and
the RX 580 thread limit are from [Spikol02/Nyamu](https://github.com/Spikol02/Nyamu); turning
off the stale upload skip follows [Frizti/Fritchii](https://github.com/Frizti/Fritchii).
CheezyEmu started from [Lichii](https://github.com/zortos293/Lichii) by zortos293 and contributors,
itself built on Eglantine, Eden, yuzu and Sudachi.
