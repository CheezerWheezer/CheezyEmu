CHEEZYEMU - READ ME
===================

A short guide for players:
  1. The switches file (turning options on and off)
  2. Shaders and shader packs
  3. AMD FSR upscaling
  4. AMD frame generation
  5. If something looks wrong
  6. Other switches

What changed in each build, and who made it, is in CHEEZYEMU.md (open it with Notepad).

Help, bug reports and new builds: the CheezyEmu Discord, https://discord.gg/vEK4ug6JD


1. THE SWITCHES FILE (nx2_switches.txt)
---------------------------------------

The most used switches have checkboxes: Tools > Switch 2 Switches (restart CheezyEmu after
saving). It writes the file below for you. The rest live only in the text file:

  - nx2_switches.txt next to cheezyemu.exe, or
  - %APPDATA%\cheezyemu\nx2_switches.txt
    (paste that into the File Explorer address bar). This one is kept when you update CheezyEmu
    and wins over the one next to cheezyemu.exe, so put your own changes here.

Each line is one switch, NAME=VALUE. A line that starts with # is OFF (a comment).

  - To turn a switch ON:  remove the # at the start of its line, save, restart CheezyEmu.
  - To turn a switch OFF: put a # back at the start of its line (or delete the line), save,
                          restart CheezyEmu.

For example, this switch is off:

    #EDEN_NX2_PRELOAD_ALL_PIPELINES=1

and this one is on:

    EDEN_NX2_PRELOAD_ALL_PIPELINES=1

Open the file with Notepad. CheezyEmu reads it only when it starts.


2. SHADERS AND SHADER PACKS
---------------------------

The first time the game shows something new, CheezyEmu has to build a shader for it, which can
stutter. Built shaders are saved, so each one is built only once.

Since build 12 Ocarina of Time needs no shader pack: CheezyEmu comes with a shader recipe for
it (the "recipes" folder next to cheezyemu.exe). The first time you start the game, the loading
screen gathers the shader programs from your own copy of the game (about a minute) and builds
them all, showing "Building shaders N / M (once)" with the time left. This happens once per GPU
driver; updating CheezyEmu does not start it over. It can take a long time on a PC with no
saved shaders: the Skip button starts the game right away, and what was not built is built on
the next start or while you play, as in earlier builds.

A shader pack is someone else's saved shaders. Packs still work and are used together with the
recipe.

Where shaders are kept
  Each game has its own folder. For Ocarina of Time:

    %APPDATA%\cheezyemu\cache\shader\0400486024dec000\

    vulkan.fork-v54.bin            The shaders themselves (the pack)
    vulkan_used.fork-v54.bin       Which shaders the game used lately; these are built at boot
    vulkan_binaries.fork-v54.bin   Your GPU's compiled shaders (made on your PC, not shared)
    vulkan_pipelines.fork-v54.bin  Your GPU driver's cache (made on your PC, not shared)

  "fork-v54" is the cache version. A pack must have the same version as your build.

Installing a shader pack
  1. Close CheezyEmu.
  2. Back up your own vulkan.fork-v54.bin (copy it somewhere safe) in case you want it back.
  3. Copy the pack's vulkan.fork-v54.bin into the folder above, replacing yours. If the pack
     has a vulkan_used.fork-v54.bin, copy that too.
  4. Start the game. The first boot builds the pack's shaders for your GPU and saves them;
     later boots are faster.

  Packs made on any GPU work, AMD or NVIDIA. CheezyEmu also removes duplicate entries from the
  cache on its own, so a big pack shrinks after the first boot.

Moving or sharing your shaders
  To use your shaders on another PC or give them to someone, copy vulkan.fork-v54.bin (and
  vulkan_used.fork-v54.bin) from the folder above into the same folder there. Don't copy the
  "binaries" and "pipelines" files: they only work with your GPU and driver.

Building shaders on the loading screen
  Emulation > Configure > Graphics > Advanced > "Precache shaders before game start":
  - Always (the default since build 12): the game's recipe, the shaders you haven't used
    lately and the compute programs found in the game files are built on the loading screen
    before the game starts. Once everything is built, boots are quick again.
  - Ask: asks before doing that for a game the first time.
  - Off: shaders are built the first time they appear in the game, which can stutter.

Building every shader at once (optional)
  1. In nx2_switches.txt, turn on (remove the #):
         EDEN_NX2_PRELOAD_ALL_PIPELINES=1
  2. Start the game and wait on the loading screen. With a big pack this takes a long time the
     first time (about 12 minutes for 20,000 shaders on an i5-12600K), only once.
  3. Close CheezyEmu. Every shader is now saved.
  4. Put the # back in front of the line to return to normal boots. Leaving it on keeps every
     shader in memory (about 10 GB of RAM for Ocarina).

Fewer stutters while playing
  In Emulation > Configure > Graphics > Advanced, turn on "Enable asynchronous shader
  compilation". An object whose shader is still being built is skipped for a frame instead of
  the game freezing.
  Keep "Switch 2 draws with unbuilt pipelines" on "Skip only redrawn targets" (the default since
  build 9). "Skip" can drop something the game draws only once, such as the lighting of an area,
  which then stays wrong (white, overexposed) until you reset the game.


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

The log (%APPDATA%\cheezyemu\log\eden_log.txt) says which one runs:
    AMD FSR: upscaler 4.x.x running   (FSR 4)
    AMD FSR: upscaler 3.1.x running   (FSR 3.1)

  - Windows only.
  - Resolution must be 1X (Graphics > Resolution). At 0.75X, 2X and so on, FSR and AMD frame
    generation turn off and the game's own upscaler is used.
  - At the game's native resolution it works as anti-aliasing; when the game lowers its
    resolution under load, the game's own upscaler takes those frames.
  - If you see a problem only with it, turn the option off to compare.

Low frame rate? (FSR 4 on RX 6000 / RX 7000)
  FSR 4's network is heavy on Radeon RX 6000 and RX 7000 (they don't have the RX 9000's AI
  hardware). If your frame rate is low with it on:
  1. Turn "Use AMD FSR 4 in place of a game's DLSS" OFF. You get FSR 3.1, which costs far less.
     AMD frame generation keeps working.
  2. If your PC isn't strong, turn the "Max Resolution (built-in)" add-on OFF (right-click the
     game > Properties > Add-Ons). It keeps the game at its highest resolution; without it the
     game lowers its resolution when the GPU falls behind.
  3. Advanced, at your own risk: OptiScaler (github.com/optiscaler/OptiScaler) can replace
     AMD's FSR DLLs next to cheezyemu.exe with its own, including a lighter FSR 4 model for
     RDNA 2. It isn't part of CheezyEmu; if anything breaks, delete what it added and put back
     the three amd_fidelityfx_*.dll files from the CheezyEmu zip.


4. AMD FRAME GENERATION
-----------------------

Frame generation shows one extra, generated frame after each real one: 30 FPS on screen
becomes 60. It is AMD's FSR frame generation, the same as in PC games. It works on any GPU on
Windows: FSR 4 frame generation on Radeon RX 9000, FSR 3.1 frame generation on every other GPU.
Not on Linux or macOS.

Turning it on
  1. Emulation > Configure > Graphics: turn on "Enable Frame Generation".
  2. Set "Frame Generation Type" to "AMD FSR Frame Generation (Ocarina, restart)".
  3. Close and restart CheezyEmu, then start the game.

  When it runs, the bottom bar shows "FG AMD FSR 2X" and the frame rate as
  "Game: 60 FPS (30 + FG)": the real frames plus the generated ones.

  When it's on but not generating, the bar says why, for example:
    (RES 1X)        set Resolution to 1X
    (RESTART)       close and reopen CheezyEmu after changing the Frame Generation Type
    (OOT ONLY)      it works in Ocarina of Time only
    (WAITING)       menus, cutscenes and loading screens have no motion data (normal)
    (WINDOWS ONLY)  AMD frame generation doesn't run on Linux or macOS
  Hover over it for the full reason. Check "Frame Generation Type" too: "Optical Flow
  Interpolation (built-in)" is a different frame generator, not AMD's.

The FPS counter and a locked 60
  The number on screen is always the real frame rate x 2. With "FPS Unlock (built-in)" (or
  another FPS mod) on, the game runs above 30, so you see more than 60 (for example 45 real =
  90). FPS Limit and Target Frame Rate cap the game's own frames, not AMD's generated ones.
  For a steady 60 on screen:
    1. Turn the "FPS Unlock (built-in)" add-on OFF (the game runs at its normal 30).
    2. Set "Frame rate limit (Switch 2)" to 60 Hz.
    3. Set VSync Mode to FIFO.

FSR 4 or FSR 3.1 frame generation
  "AMD Frame Generation Version" (under the frame generation options, restart CheezyEmu after
  changing it):
  - Auto: FSR 4 frame generation on RX 9000 (it needs their FP8 support), FSR 3.1 on every
    other GPU, picked for you.
  - FSR 3.1 (faster): lighter on the GPU. Pick it on an RX 9000 if FSR 4 frame generation
    costs too much performance. On other GPUs it's what Auto already uses.

Good to know
  - Ocarina of Time only for now. It needs the game's motion data, which CheezyEmu reads from
    Ocarina. In other games the frame generator pauses (nothing generated).
  - The HUD is kept still (hearts, items, minimap), so it doesn't smear when you turn the
    camera.
  - Don't cap the game at 30 with FPS Limit: the generated frames then land at the wrong time
    and it looks like 30. Turn FPS Unlock off instead (see above).
  - Fast movement can look soft. The generator guesses the in-between frame; the higher the
    real frame rate, the better it looks. Below about 30 real FPS, fast motion (Link running,
    swaying grass) smears a little.
  - Stopping a game and starting another without closing CheezyEmu: frame generation is reloaded
    for the next game. If the bar says it isn't generating after that, close and reopen
    CheezyEmu.
  - To turn it off, untick "Enable Frame Generation" and restart CheezyEmu.

Seeing what it does (for bug reports)
  Turn on EDEN_NX2_AMD_FG_DEBUG=2 in nx2_switches.txt to replace the picture with AMD's debug
  view (the motion it was given, its own motion estimate and the generated frame). Put the #
  back to return to the normal picture.


5. IF SOMETHING LOOKS WRONG
---------------------------

Ocarina of Time's own settings (right-click the game > Properties) override the global ones.

  - Frame generation or FSR doesn't seem to work: look in the log for "AMD FSR: upscaler ...
    running" and "AMD frame generation: generating frames". Send the log
    (%APPDATA%\cheezyemu\log\eden_log.txt) with your report.
  - Crash soon after starting or while loading a save, with 16 GB of RAM or less (the log
    shows "error 1455" or images "over the memory budget"): the PC ran out of memory. Build 9
    uses much less of it at boot. If it still happens: make the page file bigger (System >
    About > Advanced system settings > Performance > Advanced > Virtual memory: 32768 MB
    initial, 49152 MB maximum, on an SSD), set VRAM Usage Mode to Conservative, and close
    browsers, Discord and recording software while playing. ("Available VRAM: 7.00 GiB" in the
    log is just Conservative mode's cap, not other programs.)
  - After a crash, if the next launch misbehaves: check Task Manager for a leftover cheezyemu.exe
    and end it.
  - Picture white / overexposed (fog and sky blown out) until you reset the game: keep
    "Switch 2 draws with unbuilt pipelines" on "Skip only redrawn targets" (section 2), and
    remove mods that turn off occlusion queries (such as "OcclusionOff..."): the game uses those
    queries to decide how bright the sun and fog are.
  - Black picture with only a few bright dots while FSR 4 is on (RX 9000): fixed in build 9.
    If FSR 4 still draws black, build 10 notices within a few seconds and switches to FSR 3.1
    by itself (the log says "drew a black picture"). Update your graphics driver to get FSR 4
    back. You can also turn FSR 4 off or use EDEN_NX2_FSR_VERSION=3 (below).
  - White fog (Hyrule Field drawbridge, Kokiri Forest): fixed in build 10.
  - HUD (hearts, minimap) jitters with frame generation when the game runs below its full
    resolution (without the Max Resolution add-on): fixed in build 9.
  - Objects flash black for a moment: Graphics > ASTC decoding method is "CPU Asynchronous"
    (textures draw black until they are decoded). Use GPU (fastest) or CPU.
  - Stutter when entering a new area: set ASTC decoding to GPU, and turn on "Enable
    asynchronous shader compilation" (section 2).
  - Frame generation looks like 30 or stutters: check Graphics > VSync Mode in the game's
    properties; Immediate (no VSync) spaces frames unevenly. Use FIFO, and FreeSync if your
    monitor has it.
  - Thin grid lines on distant ground (AMD): turn on EDEN_NX2_KEEP_ASTC=1 (below).
  - The GPU hangs or resets, or the screen goes black after the opening cutscene (AMD):
    on Linux and on laptops / handhelds with built-in Radeon graphics CheezyEmu now avoids
    the cause by itself. On a desktop Radeon on Windows, turn on
    EDEN_NX2_FORCE_MESH_FALLBACK=1 (below). The first start after that builds shaders for
    several minutes.
  - A white flash across the whole picture, or huge glowing triangles, after playing a while:
    restarting the game clears it. The cause is known and a fix is being worked on. To help,
    set EDEN_NX2_FLASH_WATCH to a folder (below), play until it happens, then send that folder
    and the log.


6. OTHER SWITCHES
-----------------

  EDEN_NX2_KEEP_ASTC=1              Removes the grid lines on distant terrain on some AMD
                                    cards (uses more video memory)
  EDEN_NX2_FLUSH_DRAWS=512          Sends GPU work in bigger batches; steadier frame rate on AMD
  EDEN_NX2_PRELOAD_ALL_PIPELINES=1  Builds every saved shader at boot (see section 2)
  EDEN_NX2_AMD_FG_DEBUG=2           AMD frame generation's debug view
  EDEN_NX2_FSR_VERSION=3            Uses FSR 3.1 even where FSR 4 is offered (for comparing)
  EDEN_NX2_FORCE_MESH_FALLBACK=1    Draws mesh shader geometry the safe way on any GPU (fixes
                                    AMD hangs; =0 never uses it, even where it is automatic)
  EDEN_NX2_FSR_BUILTIN=1            Uses the FSR 3.1 built into CheezyEmu even where AMD's own
                                    runtime is there (what Linux and macOS run; to compare)
  EDEN_NX2_BUFFER_GC_TOTAL=1        Brings back the old buffer clean-up rule (more hitching in
                                    Ocarina; only to compare)
  EDEN_NX2_MESH_EMULATION=0         On a GPU without mesh shaders: leaves grass and props out
                                    instead of drawing them through compute (if loading a
                                    save freezes there)
  EDEN_NX2_DEPTH_ALWAYS_MODIFIED=1  Brings back the old depth buffer rule, if water, particles
                                    or decals look wrong in build 12
  EDEN_NX2_TRANSIENT_TARGETS=0      Brings back the older rule for draws whose shader is still
                                    building, if something stays missing in a new area
  EDEN_NX2_FLASH_WATCH=C:\flash     Records what caused a white flash into that folder
                                    (costs some speed; leave it off otherwise)
  EDEN_NX2_SEC_RESERVE=0            Commits all of the console's memory at boot again (the
                                    build 8 behaviour), if build 9's lower memory use causes
                                    trouble
