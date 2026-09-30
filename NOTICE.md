# Notices

This repository is licensed under the **GNU General Public License v3** (see
`LICENSE`). Copyright (c) 2026 Wetrix Enjoyers.

## Why GPL v3

`wetrix_game`, the library built here, links the **N64ModernRuntime**
(`ultramodern` and `librecomp`), whose licence is the GPL v3 (the `COPYING` file at
its root). A program that links it is a combined work under the GPL v3, so this
project is released under the same licence. `tools/regen.py` fetches the runtime at
a pinned commit; it is not part of this repository.

## Source

- Recompilation (this repository): <https://github.com/wetrix-enjoyers/wetrix-n64-recompilation>
- Disassembly (the ELF it recompiles): <https://github.com/wetrix-enjoyers/wetrix-n64-disassembly>
- Ports (the programs that use it): <https://github.com/wetrix-enjoyers/wetrix-n64-ports>
- Wetter: <https://github.com/wetrix-enjoyers/wetter>

## What else it uses

- **N64Recomp** (MIT, Copyright (c) 2024 Wiseguy): the recompiler. It generates the
  C in `build/recomp/` from your ROM.
- Libraries the runtime bundles, each under its own licence in the runtime's
  `thirdparty/` folder: xxHash (BSD 2-clause), sse2neon (MIT), miniz, o1heap,
  concurrentqueue, json.

## The game

Nothing here contains game data. The recompiled C is generated from your own ROM
and must not be published. Wetrix is the work of its authors and publishers, not
of this project.

This is not legal advice.
