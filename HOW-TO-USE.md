# How to use Lichii AMD Superior

A short guide for players: shader packs, the switches file, AMD FSR 4 upscaling and AMD frame
generation. What changed in this build and who made it is in [AMD-SUPERIOR.md](AMD-SUPERIOR.md).

## 1. The switches file (`nx2_switches.txt`)

Some options have no button in the settings yet. They live in a small text file:

- `nx2_switches.txt` next to `lichii.exe`, or
- `%APPDATA%\eden\nx2_switches.txt` (paste that into the File Explorer address bar). This one is
  kept when you update Lichii and wins over the one next to `lichii.exe`, so put your own changes
  here.

Each line is one switch, `NAME=VALUE`. A line that starts with `#` is **off** (a comment).

- **To turn a switch on:** remove the `#` at the start of its line, save the file, restart Lichii.
- **To turn a switch off:** put a `#` back at the start of its line (or delete the line), save,
  restart Lichii.

For example, this switch is off:

```
#EDEN_NX2_PRELOAD_ALL_PIPELINES=1
```

and this one is on:

```
EDEN_NX2_PRELOAD_ALL_PIPELINES=1
```

Open the file with Notepad. Lichii reads it only when it starts.

## 2. Shaders and shader packs

The first time the game shows something new, Lichii has to build a shader for it, which can
stutter. Built shaders are saved, so each one is built only once. A shader pack is someone else's
saved shaders: with it, those stutters never happen.

### Where shaders are kept

Each game has its own folder. For Ocarina of Time:

```
%APPDATA%\eden\cache\shader\0400486024dec000\
```

| File | What it is |
| --- | --- |
| `vulkan.fork-v54.bin` | The shaders themselves (the pack) |
| `vulkan_used.fork-v54.bin` | Which shaders the game used lately; these are built at boot |
| `vulkan_binaries.fork-v54.bin` | Your GPU's compiled shaders (made on your PC, not shared) |
| `vulkan_pipelines.fork-v54.bin` | Your GPU driver's cache (made on your PC, not shared) |

`fork-v54` is the cache version. A pack must have the same version as your build.

### Installing a shader pack

1. Close Lichii.
2. Back up your own `vulkan.fork-v54.bin` (copy it somewhere safe) in case you want it back.
3. Copy the pack's `vulkan.fork-v54.bin` into the folder above, replacing yours. If the pack has a
   `vulkan_used.fork-v54.bin`, copy that too.
4. Start the game. The first boot builds the pack's shaders for your GPU and saves them; later
   boots are faster.

Packs made on any GPU work, AMD or NVIDIA. Lichii also removes duplicate entries from the cache on
its own, so a big pack shrinks after the first boot.

### Moving or sharing your shaders

To use your shaders on another PC or give them to someone, copy `vulkan.fork-v54.bin` (and
`vulkan_used.fork-v54.bin`) from the folder above into the same folder there. Don't copy the
`binaries` and `pipelines` files: they only work with your GPU and driver.

### Building every shader at once (optional)

Normally the game builds the shaders it used recently at boot and the others the first time they
appear. To build **all** of them once, so nothing ever compiles while you play:

1. In `nx2_switches.txt`, turn on (remove the `#`):
   ```
   EDEN_NX2_PRELOAD_ALL_PIPELINES=1
   ```
2. Start the game and wait on the loading screen. With a big pack this takes a long time the first
   time (about 12 minutes for 20,000 shaders on an i5-12600K), only once.
3. Close Lichii. Every shader is now saved.
4. Put the `#` back in front of the line to return to normal boots. Leaving it on keeps every
   shader in memory (about 10 GB of RAM for Ocarina).

### Fewer stutters while playing

In **Emulation > Configure > Graphics > Advanced**, turn on **Enable asynchronous shader
compilation**. An object whose shader is still being built is skipped for a frame instead of the
game freezing.

## 3. AMD FSR 4 upscaling

FSR 4 is AMD's machine-learning upscaler and anti-aliasing. In Ocarina of Time it replaces the
game's own upscaler: sharper, steadier edges.

1. **Emulation > Configure > Graphics**: turn on **Use AMD FSR 4 in place of a game's DLSS** (on by
   default).
2. Start the game. The log says `FSR 4: upscaler ... running` when it works.

- It needs a GPU AMD's FSR runtime supports (Radeon RX 6000 and newer here) and Windows.
- At the game's native resolution it works as anti-aliasing; when the game lowers its resolution
  under load, the game's own upscaler takes those frames.
- If you see a problem only with it, turn the option off to compare.

## 4. AMD frame generation

Frame generation shows one extra, generated frame after each real one: 30 FPS on screen becomes
60. It is AMD's FSR frame generation, the same as in PC games, and it works on AMD and other GPUs
(Windows, Vulkan).

### Turning it on

1. **Emulation > Configure > Graphics**: turn on **Enable Frame Generation**.
2. Set **Frame Generation Type** to **AMD FSR Frame Generation (Ocarina, restart)**.
3. Close and restart Lichii, then start the game.

When it runs, the bottom bar shows **FG AMD FSR 2X** and the frame rate as
`Game: 60 FPS (30 + FG)`: the real frames plus the generated ones.

### Good to know

- **Ocarina of Time only for now.** It needs the game's motion data, which Lichii reads from
  Ocarina. In other games the frame generator pauses (nothing generated).
- **The HUD is kept still** (hearts, items, minimap), so it doesn't smear when you turn the camera.
- **Leave the frame rate limit at 60.** Capping the game at 30 makes the generated frames land at
  the wrong time and it looks like 30.
- **Fast movement can look soft.** The generator guesses the in-between frame; the higher the real
  frame rate, the better it looks. Below about 30 real FPS, fast motion (Link running, swaying
  grass) smears a little.
- **Stopping a game and starting another without closing Lichii:** frame generation is reloaded
  for the next game. If the bar says it isn't generating after that, close and reopen Lichii.
- To turn it off, untick **Enable Frame Generation** and restart Lichii.

### Seeing what it does (for bug reports)

Turn on `EDEN_NX2_AMD_FG_DEBUG=2` in `nx2_switches.txt` to replace the picture with AMD's debug
view (the motion it was given, its own motion estimate and the generated frame). Put the `#` back
to return to the normal picture.

## 5. If something looks wrong

Ocarina of Time's own settings (right-click the game > **Properties**) override the global ones.

- **Objects flash black for a moment:** *Graphics > ASTC decoding method* is *CPU Asynchronous*
  (textures draw black until they are decoded). Use **GPU** (fastest, fixed for AMD in this
  build) or **CPU**.
- **Stutter when entering a new area:** set ASTC decoding to **GPU**, and turn on
  *Enable asynchronous shader compilation* (section 2).
- **Frame generation looks like 30 or stutters:** check *Graphics > VSync Mode* in the game's
  properties; *Immediate* (no VSync) spaces frames unevenly. Use **FIFO**, and FreeSync if your
  monitor has it.
- **Thin grid lines on distant ground (AMD):** turn on `EDEN_NX2_KEEP_ASTC=1` (below).

## 6. Other switches

| Switch | What it does |
| --- | --- |
| `EDEN_NX2_KEEP_ASTC=1` | Removes the grid lines on distant terrain on some AMD cards (uses more video memory) |
| `EDEN_NX2_FLUSH_DRAWS=512` | Sends GPU work in bigger batches; steadier frame rate on AMD |
| `EDEN_NX2_PRELOAD_ALL_PIPELINES=1` | Builds every saved shader at boot (see above) |
| `EDEN_NX2_AMD_FG_DEBUG=2` | AMD frame generation's debug view |
