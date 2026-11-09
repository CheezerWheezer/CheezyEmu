<!--
# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
-->

<p align="center">
  <img src="dist/branding/cheezyemu_logo.png" alt="CheezyEmu" width="600">
</p>

# CheezyEmu

CheezyEmu is an experimental, open-source Nintendo Switch and Switch 2 emulator written in C++,
tuned for **AMD graphics cards**: AMD performance fixes, saved compiled shaders, AMD FSR 4 and AMD
frame generation. It runs on NVIDIA and Intel GPUs too.

> 💬 **Join the CheezyEmu Discord:** https://discord.gg/vEK4ug6JD (help, bug reports, new builds)
>
> **[README.txt](README.txt)** (also next to `cheezyemu.exe` in every download) explains shader
> packs, the switches file, FSR and frame generation. [CHEEZYEMU.md](CHEEZYEMU.md) lists every
> change and its credits.
>
> **New in build 10**
> - Fog no longer turns solid white (Hyrule Field drawbridge, Kokiri Forest).
> - No more stale or glowing geometry from old vertex data (Hyrule Field, the graveyard).
> - FSR 4 that draws a black picture switches itself to FSR 3.1 instead.
> - Fixes a crash while shaders compile, less memory used when shaders precache, faster NUP loading.
> - A missing or broken microphone no longer breaks games.
> - In-app updates retry when GitHub has a hiccup.
> - Now its own emulator: CheezyEmu, with a new name and icon.

CheezyEmu is not affiliated with Nintendo.

## Downloads and setup

Download CheezyEmu only from this repository's [releases page](https://github.com/CheezerWheezer/CheezyEmu/releases).

On Windows, extract the entire build archive and run **cheezyemu.exe**. Only use games and system
files you have legally obtained. Games, keys and firmware are not included.

### Existing saves and settings

CheezyEmu uses the same data folders as the emulators it builds on (Eden/Eglantine), so saves,
settings and shader caches carry over. Back up your saves before testing experimental builds.

## Reporting a problem

Post in the [Discord](https://discord.gg/vEK4ug6JD) or open an issue, and include:

- The exact build, game version, CPU, GPU and driver version.
- Relevant settings and steps to reproduce the problem.
- A screenshot and the log from the affected session, after checking it for personal information.
- For a crash, the `cheezyemu.exe.*.dmp` file and which zip you used (standard or PGO).

Do not upload game files, keys, firmware or private account information.

## Building and contributing

See the [build guide](docs/Build.md), [user documentation](docs/user/README.md) and
[coding guidelines](docs/Coding.md). Some inherited documentation and internal build target names
still use upstream names. The GUI build target remains `yuzu`; its output is `cheezyemu.exe` on
Windows and `cheezyemu` on other desktop platforms.

## Credits and license

CheezyEmu is built on the work of many projects and their contributors, and keeps their copyright
notices and licenses:

- [Lichii](https://github.com/zortos293/Lichii) by zortos293 and contributors, the emulator
  CheezyEmu started from;
- Eglantine (original developer Camille LaVey), Eden, yuzu and Sudachi;
- [conongnang](https://github.com/conongnang/Lichii)'s AMD performance work,
  [Fritchii](https://github.com/Frizti/Fritchii) and [Nyamu](https://github.com/Spikol02/Nyamu);
- AMD's [FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) for FSR and
  frame generation.

[CHEEZYEMU.md](CHEEZYEMU.md) credits each change. See [LICENSE.txt](LICENSE.txt) and the source
files for the license terms (GPL-3.0-or-later).
