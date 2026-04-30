# Harvest Moon 64 Decomp - Dev

## Overview

This is a development branch set up to support modding out-of-the-box.

## Docker Build

The recommended build path is Docker, so every host uses the same Debian
toolchain:

```bash
bash tools/docker.sh make extract
bash tools/docker.sh make -j4
```

Put your legally dumped US ROM at `baserom.us.z64` in the repo root before
running `make extract`. The built ROM is `hm64.z64`. The build writes the final
ROM only after `makemask.py` pads it and updates the N64 header checksum; if
that step fails, fix the Python dependency error and rebuild before testing in
an emulator.

The Docker image uses Debian Bookworm, the original KMC GCC 2.7.2 toolchain,
MIPS binutils, and the repo's Python requirements in an image-local virtualenv.
It also builds as your host UID/GID so generated files stay editable outside
the container.

For an interactive container shell:

```bash
bash tools/docker.sh
```

Inside that shell, the image already sets `PYTHON`, `KMC_PATH`, and
`MODERN_GCC=0`, so the normal commands are:

```bash
make extract
make -j4
```

Without Docker, run `./tools/setup.sh`, `make extract`, and `make -j4`.

## Mod API

Python mod files can set build-time mod API values. For example:

```python
api.time.speed = 20
```

Build with:

```bash
bash tools/docker.sh make MOD=mods/example_time_speed.py MOD_PATCHES=1
```

The Python file is converted into generated C during the build, then compiled
into the ROM. The modloader, generated patch settings, and live mod menu are
only active when the ROM is compiled with `MOD_PATCHES=1`; without that option,
the game uses its default behavior. The first API value controls the clock
speed used by `src/game/time.c`:

```c
#include "modding/modApi.h"

api.config.time.speed = 20;
```

The default is `10`, matching the original game behavior. Set it to `0` to
pause time, or a higher value to advance the clock faster.

The C API is split by lifetime: `api.config` stores stable mod configuration,
while `api.runtime` is reserved for state that should reset on scoped hooks
such as new game, new day, or map load.

## Live Mod Menu

When compiled with `MOD_PATCHES=1`, hold `L` and press `R` on the pause screen
to open the live mod menu. Use `L/R` to change categories, up/down to change
items, left/right to edit the selected value, hold `A` while pressing
left/right for the fast step, press `A` to apply manual entries, `Z` to reset
that value, and `B` or `Start` to close the menu.

The first menu bindings live in `src/modding/modMenu.c`: `WORLD` edits weather
and forecast, `TIME` edits clock and calendar values, `PLAYER` edits gold and
stamina/fatigue-style values, `FARM` edits lumber and fodder, and `NPCS` edits
named NPC affection levels. Bindings also declare when a value is applied: live,
manual, on map load, or on new day, plus the normal and fast step increments.
Add new fields by adding one binding entry and, when needed, an `onChange`
callback in that file.

To add another Python API value, add its Python path and generated C target to
`API_FIELDS` in `tools/modding/build_mod.py`, then add the matching C field to
the appropriate config or runtime struct in `src/modding/modApi.h`.

Currently supported modding workflows:
- All code changes
- Text editing and new texts (simply edit an existing text and rebuild the game)
- Cutscene editing and new cutscenes
- Dialogue logic editing and additional dialogues
- Audio sequence replacement

Replacing other assets (maps, sprites, animations, fonts) is supported in theory, but tooling to create/modify these assets is still in progress.

For this branch, `make extract` should only be run once. If needed, `make clean-all-dangerous` can be run, which will remove all texts (losing any edits). Note that the original dialogue bytecode relies on hardocded text indexes, so removing or changing existing texts may result in breaking the game. This also applies to hardcoded references to texts in the game code itself as well as the cutscene DSL.

Basic changes to the development branch:
- Removing unued assets (sprites, maps, texts) and unused files
- Commenting out unused functions
- Fixing of fake matches to enchance readability

Note that you may run into performance issues when emulating non-matching builds. This is because many emulators hardcode game-specific graphics and overclocking settings that get applied when a game matches a known hash. Check your emulator's settings for vanilla Harvest Moon 64 and make sure those are applied when your mod is loaded.

### Music

See the README at `tools/modding/music` for more details for converting and inserting MIDI into existing song slots.
