# Wetrix (N64): the recompilation

Wetrix's original MIPS code statically recompiled to C (N64Recomp), its audio
microcode to C++ (RSPRecomp), and the game-side shims they need, built as one
library, `wetrix_game`, on N64ModernRuntime. The input is the ELF that
`wetrix-n64-disassembly` produces. Ports (`wetrix-n64-ports/`) add this with
`add_subdirectory` and bring the window, input and renderer.

**You supply your own ROM.** Nothing here contains or produces any part of the
game. See [The ROM](#the-rom).

## Layout

| Path | Contents |
|---|---|
| `CMakeLists.txt` | the `wetrix_game` library and the runtime (`ultramodern`, `librecomp`) |
| `src/` | game-side code the recompiled functions call: overlay tables, runtime-replaced functions, libultra shims, trace rings, ROM adoption |
| `include/` | `wetrix_shims.h`, `trace.h`, `rom_adopt.h` |
| `wetrix.toml` | N64Recomp config: functions, names, runtime-replaced functions, instruction patches |
| `wetrix_rsp.toml` | RSPRecomp config for the audio microcode |
| `tools/` | ROM provisioning (`wetrix_provision`), fetching the ELF, the TOML checker |
| `build/` | generated and fetched: recompiled C, RSP C++, the runtime checkout, the N64Recomp build (not committed) |

## Requirements

- A US Wetrix ROM (`.z64`, `.n64` or `.v64`, any filename).
- `wetrix-n64-disassembly`, which produces the ELF the recompiler reads. `tools/deps.py`
  uses the folder `WETRIX_DISASM_DIR` names, or a sibling folder next to this
  repository, or else clones it from GitHub into `deps/`. `tools/regen.py` builds
  its ELF (`tools/fetch_elf.py` copies it to `build/wetrix.elf`).
- Windows: **MSYS2 UCRT64** (`mingw-w64-ucrt-x86_64-gcc`, `cmake`, `ninja`,
  `git`). From Git Bash:

  ```
  MSYSTEM=UCRT64 CHERE_INVOKING=1 /c/msys64/usr/bin/bash -lc 'cd <dir> && <command>'
  ```

## Build

The short version (steps 1 to 4 below are what it does; you never need to run them by hand):

```
python tools/regen.py
```

It brings everything up to date from the disassembly down, doing only what
changed. First it sets up what a fresh clone lacks: the runtime checkout (cloned
at the commit pinned in `tools/regen.py`) and the game image (`wetrix.z64`,
adopted from the disassembly's `baserom.z64`). Then the disassembly's
`tools/build.py` (Python environment, split, ELF), then step 3, then step 4
(building N64Recomp and RSPRecomp, and running each only when its inputs
changed). The generated files are written to a scratch folder and only
those whose content differs are copied over, so the compiler rebuilds only them.
`--no-disasm` leaves the disassembly alone; `--force` regenerates everything.
Every port's build runs it first.

Each output folder keeps its inputs' hashes in `.inputs`. `CMakeLists.txt`
re-hashes them at configure time, re-configures when one changes, and stops
with an error when they no longer match, so a plain `cmake --build` can't
compile stale sources.

### 1. The runtime

```
git clone --recurse-submodules https://github.com/N64Recomp/N64ModernRuntime.git build/n64modernruntime
git -C build/n64modernruntime checkout cdf5abbd5026fef5c364c676e4667c45e42b6863
git -C build/n64modernruntime submodule update --init --recursive
```

### 2. Game image

```
cmake -S tools -B build/tools -G Ninja
cmake --build build/tools --parallel
build/tools/wetrix_provision.exe
```

Uses the file `WETRIX_ROM` names, else the disassembly's `baserom.z64`, else finds a ROM (beside itself, in `data/`, up to four directories up), checks it, and
writes `wetrix.z64` (8MB big-endian) here.

### 3. The ELF

```
python tools/fetch_elf.py
```

Copies `../wetrix-n64-disassembly/build/wetrix.elf` (build and verify it there
first: its `tools/build_elf.py` and `tools/verify_elf.py`) to `build/wetrix.elf`,
only if it changed, and says whether `build/recomp` needs regenerating.
`--disasm DIR` (or `WETRIX_DISASM_DIR`) points elsewhere; `--check` copies
nothing and exits 1 if anything is out of date.

### 4. Recompile

```
cmake -S build/n64modernruntime/N64Recomp -B build/n64recomp-build -G Ninja
cmake --build build/n64recomp-build --parallel
./build/n64recomp-build/N64Recomp.exe wetrix.toml
./build/n64recomp-build/RSPRecomp.exe wetrix_rsp.toml
```

Output: `build/recomp/funcs_*.c` and `build/rsp/aspMain.cpp`, used by every port.
Repeat whenever the ELF changes. `wetrix.toml` currently has `trace_mode = true`
(every recompiled function records into the trace rings; slower).

The library itself is built by a port; see `wetrix-n64-ports/`.

## The ROM

`wetrix.z64` is the only ROM-derived file. A port copies it beside its executable,
where librecomp loads it whole (`config_path/wetrix.z64`, from the game id) and
serves every PI DMA from it.

On launch the executable checks that image (one read, one hash). If it is missing
or wrong, it searches beside itself, in `data/`, and up to four directories up.
Every `*.z64`, `*.n64` and `*.v64` it finds is a candidate, and a rejected one gets
a reason (length, internal name, or hash). A match is normalised to big-endian and
is written out as `wetrix.z64`; the original is never moved or changed. The same code (`src/rom_adopt.c`) backs
`tools/provision_rom.c`.

```
0x000000 - 0x07CF80   header, IPL3, main segment
0x07CF80 - 0x47DE80   level, texture and table data
0x47DE80 - 0x800000   audio samples
```

## Keeping the configuration honest

```
python tools/check_toml_against_elf.py [--toml wetrix.toml] [--elf build/wetrix.elf] [--write]
```

Re-points instruction patches at their functions and reports names in
`function_sizes`, `ignored` and `manual_funcs` that no longer exist. `--write`
fixes the mechanical ones.

## Known gaps

- `osYieldThread` does not yield (the runtime's version is an assert compiled
  out). See `include/wetrix_shims.h`.
- The recompiled C is not committed, and an ELF newer than `build/recomp`
  silently recompiles the wrong functions: regenerate whenever the ELF changes.

## Licence

GPL v3 (`LICENSE`, Copyright (c) 2026 Wetrix Enjoyers), because it links the GPL v3
N64ModernRuntime; see `NOTICE.md`.
