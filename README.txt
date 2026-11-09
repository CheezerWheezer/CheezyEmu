LICHII AMD SUPERIOR - READ ME
=============================

A short guide for players:
  1. The switches file (turning options on and off)
  2. Shaders and shader packs
  3. AMD FSR upscaling
  4. AMD frame generation
  5. If something looks wrong
  6. Other switches

What changed in each build, and who made it, is in AMD-SUPERIOR.md (open it with Notepad).


1. THE SWITCHES FILE (nx2_switches.txt)
---------------------------------------

Some options have no button in the settings yet. They live in a small text file:

  - nx2_switches.txt next to lichii.exe, or
  - %APPDATA%\eden\nx2_switches.txt
    (paste that into the File Explorer address bar). This one is kept when you update Lichii
    and wins over the one next to lichii.exe, so put your own changes here.

Each line is one switch, NAME=VALUE. A line that starts with # is OFF (a comment).

  - To turn a switch ON:  remove the # at the start of its line, save, restart Lichii.
  - To turn a switch OFF: put a # back at the start of its line (or delete the line), save,
                          restart Lichii.

For example, this switch is off:

    #EDEN_NX2_PRELOAD_ALL_PIPELINES=1

and this one is on:

    EDEN_NX2_PRELOAD_ALL_PIPELINES=1

Open the file with Notepad. Lichii reads it only when it starts.


2. SHADERS AND SHADER PACKS
---------------------------

The first time the game shows something new, Lichii has to build a shader for it, which can
stutter. Built shaders are saved, so each one is built only once. A shader pack is someone
else's saved shaders: with it, those stutters never happen.

Where shaders are kept
  Each game has its own folder. For Ocarina of Time:

    %APPDATA%\eden\cache\shader\0400486024dec000\

    vulkan.fork-v54.bin            The shaders themselves (the pack)
    vulkan_used.fork-v54.bin       Which shaders the game used lately; these are built at boot
    vulkan_binaries.fork-v54.bin   Your GPU's compiled shaders (made on your PC, not shared)
    vulkan_pipelines.fork-v54.bin  Your GPU driver's cache (made on your PC, not shared)

  "fork-v54" is the cache version. A pack must have the same version as your build.

Installing a shader pack
  1. Close Lichii.
  2. Back up your own vulkan.fork-v54.bin (copy it somewhere safe) in case you want it back.
  3. Copy the pack's vulkan.fork-v54.bin into the folder above, replacing yours. If the pack
     has a vulkan_used.fork-v54.bin, copy that too.
  4. Start the game. The first boot builds the pack's shaders for your GPU and saves them;
     later boots are faster.

  Packs made on any GPU work, AMD or NVIDIA. Lichii also removes duplicate entries from the
  cache on its own, so a big pack shrinks after the first boot.

Moving or sharing your shaders
  To use your shaders on another PC or give them to someone, copy vulkan.fork-v54.bin (and
  vulkan_used.fork-v54.bin) from the folder above into the same folder there. Don't copy the
  "binaries" and "pipelines" files: they only work with your GPU and driver.

Precaching shaders on the loading screen
  Emulation > Configure > Graphics > Advanced > "Precache shaders before game start":
  - Always: shaders you haven't used lately are built on the loading screen, a batch per boot
    (about a minute each), until all of them are done. After that boots are quick again.
  - Off: they are built the first time they appear in the game, which can stutter.

Building every shader at once (optional)
  1. In nx2_switches.txt, turn on (remove the #):
         EDEN_NX2_PRELOAD_ALL_PIPELINES=1
  2. Start the game and wait on the loading screen. With a big pack this takes a long time the
     first time (about 12 minutes for 20,000 shaders on an i5-12600K), only once.
  3. Close Lichii. Every shader is now saved.
  4. Put the # back in front of the line to return to normal boots. Leaving it on keeps every
     shader in memory (about 10 GB of RAM for Ocarina).

Fewer stutters while playing
  In Emulation > Configure > Graphics > Advanced, turn on "Enable asynchronous shader
  compilation". An object whose shader is still being built is skipped for a frame instead of
  the game freezing.


3. AMD FSR UPSCALING
--------------------

In Ocarina of Time, AMD's FSR replaces the game's own upscaler: sharper, steadier edges.

  1. Emulation > Configure > Graphics: turn on "Use AMD FSR 4 in place of a game's DLSS"
     (on by default).
  2. Start the game.

Which FSR you get:
  - FSR 4 (machine learning, best): Radeon RX 9000, and RX 7000 / RX 6000 with a recent AMD
    driver. If your RX 6000 or 7000 gets FSR 3.1, update to the latest AMD Adrenalin driver.
  - FSR 3.1: every other GPU (older Radeon, NVIDIA, Intel), or with the FSR 4 option off while
    frame generation is on.

The log (%APPDATA%\eden\log\eden_log.txt) says which one runs:
    AMD FSR: upscaler 4.x.x running   (FSR 4)
    AMD FSR: upscaler 3.1.x running   (FSR 3.1)

  - Windows only.
  - At the game's native resolution it works as anti-aliasing; when the game lowers its
    resolution under load, the game's own upscaler takes those frames.
  - If you see a problem only with it, turn the option off to compare.


4. AMD FRAME GENERATION
-----------------------

Frame generation shows one extra, generated frame after each real one: 30 FPS on screen
becomes 60. It is AMD's FSR frame generation, the same as in PC games. It works on any GPU on
Windows: FSR 4 frame generation on Radeon RX 9000, FSR 3.1 frame generation on the others.

Turning it on
  1. Emulation > Configure > Graphics: turn on "Enable Frame Generation".
  2. Set "Frame Generation Type" to "AMD FSR Frame Generation (Ocarina, restart)".
  3. Close and restart Lichii, then start the game.

  When it runs, the bottom bar shows "FG AMD FSR 2X" and the frame rate as
  "Game: 60 FPS (30 + FG)": the real frames plus the generated ones.

FSR 4 or FSR 3.1 frame generation
  "AMD Frame Generation Version" (under the frame generation options, restart Lichii after
  changing it):
  - Auto: the newest AMD offers for your GPU. FSR 4 frame generation on Radeon RX 9000
    (best looking), FSR 3.1 on every other GPU.
  - FSR 3.1 (faster): lighter on the GPU. Pick it on an RX 9000 if FSR 4 frame generation
    costs too much performance. On other GPUs it changes nothing.

Good to know
  - Ocarina of Time only for now. It needs the game's motion data, which Lichii reads from
    Ocarina. In other games the frame generator pauses (nothing generated).
  - The HUD is kept still (hearts, items, minimap), so it doesn't smear when you turn the
    camera.
  - Leave the frame rate limit at 60. Capping the game at 30 makes the generated frames land
    at the wrong time and it looks like 30.
  - Fast movement can look soft. The generator guesses the in-between frame; the higher the
    real frame rate, the better it looks. Below about 30 real FPS, fast motion (Link running,
    swaying grass) smears a little.
  - Stopping a game and starting another without closing Lichii: frame generation is reloaded
    for the next game. If the bar says it isn't generating after that, close and reopen
    Lichii.
  - To turn it off, untick "Enable Frame Generation" and restart Lichii.

Seeing what it does (for bug reports)
  Turn on EDEN_NX2_AMD_FG_DEBUG=2 in nx2_switches.txt to replace the picture with AMD's debug
  view (the motion it was given, its own motion estimate and the generated frame). Put the #
  back to return to the normal picture.


5. IF SOMETHING LOOKS WRONG
---------------------------

Ocarina of Time's own settings (right-click the game > Properties) override the global ones.

  - Frame generation or FSR doesn't seem to work: look in the log for "AMD FSR: upscaler ...
    running" and "AMD frame generation: generating frames". Send the log
    (%APPDATA%\eden\log\eden_log.txt) with your report.
  - Crash soon after starting, with 16 GB of RAM or less (the log ends with "error 1455" or
    images "over the memory budget"): the PC ran out of memory. Set Graphics > Advanced >
    VRAM Usage Mode to Conservative, let Windows manage the page file size (or make it at least
    32 GB, on an SSD), and close browsers, Discord and recording software while playing.
  - Objects flash black for a moment: Graphics > ASTC decoding method is "CPU Asynchronous"
    (textures draw black until they are decoded). Use GPU (fastest) or CPU.
  - Stutter when entering a new area: set ASTC decoding to GPU, and turn on "Enable
    asynchronous shader compilation" (section 2).
  - Frame generation looks like 30 or stutters: check Graphics > VSync Mode in the game's
    properties; Immediate (no VSync) spaces frames unevenly. Use FIFO, and FreeSync if your
    monitor has it.
  - Thin grid lines on distant ground (AMD): turn on EDEN_NX2_KEEP_ASTC=1 (below).


6. OTHER SWITCHES
-----------------

  EDEN_NX2_KEEP_ASTC=1              Removes the grid lines on distant terrain on some AMD
                                    cards (uses more video memory)
  EDEN_NX2_FLUSH_DRAWS=512          Sends GPU work in bigger batches; steadier frame rate on AMD
  EDEN_NX2_PRELOAD_ALL_PIPELINES=1  Builds every saved shader at boot (see section 2)
  EDEN_NX2_AMD_FG_DEBUG=2           AMD frame generation's debug view
  EDEN_NX2_FSR_VERSION=3            Uses FSR 3.1 even where FSR 4 is offered (for comparing)
