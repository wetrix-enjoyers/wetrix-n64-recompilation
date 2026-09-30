#!/usr/bin/env python3
"""Check this repo's configuration against whatever map the ELF now describes.

wetrix.toml names functions in four places, and every one of them is a claim
about function boundaries that lives in the *other* repository:

  * `manual_funcs`  -- an extra function N64Recomp compiles at an address the
    split does not call a function at all (a jump-table handler);
  * `function_sizes`-- widen a function so a target inside its body is contained;
  * `ignored`       -- the port or the runtime supplies the body;
  * `[[patches.instruction]]` -- rewrite one instruction, addressed by the
    function that contains it.

When the map changes, these go stale in ways that are silent until N64Recomp
aborts, and its complaints name one entry at a time. This reports all of them at
once, and with --write repairs the mechanical ones:

  * a patch whose vram now sits in a different function -- the rewrite is exactly
    the same, only the containing function moved, so the name is updated;
  * a patch whose function is now on N64Recomp's reimplemented or ignored list,
    which renames it in the generated code (N64Recomp appends `_recomp`);

For the name lists it does not invent entries: a name that no longer exists as a
function is reported as an error, because the fix depends on why it went away.
`manual_funcs` is the exception, and is checked differently. A name there is
*expected* to be missing from the map -- that is the whole reason the entry
exists -- so what has to hold is a geometric property instead: the address must
fall inside a real function's body, and the declared size must run exactly to
that body's end. Short of it, the copied body has no `jr $ra` of its own; past
it, the copy swallows whatever follows.

    python tools/check_toml_against_elf.py --elf build/wetrix.elf \
        --lists /path/to/n64recomp/src/symbol_lists.cpp
"""
from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

RE_PATCH = re.compile(r"\[\[patches\.instruction\]\]")
RE_KV = re.compile(r"^\s*(vram|func)\s*=\s*(.+?)\s*$")
RE_NAME = re.compile(r'"([A-Za-z_][A-Za-z0-9_]*)"')


def load_elf(path: Path):
    """(name, vram, size) for every function symbol, sorted by address."""
    d = path.read_bytes()
    if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 2:
        raise SystemExit(f"{path}: expected a big-endian ELF32")
    shoff = struct.unpack_from(">I", d, 0x20)[0]
    shentsize, shnum = struct.unpack_from(">HH", d, 0x2E)
    shstrndx = struct.unpack_from(">H", d, 0x32)[0]
    secs = []
    for i in range(shnum):
        o = shoff + i * shentsize
        nm, _t, _f, addr, off, size, link, _i, _a, _e = \
            struct.unpack_from(">10I", d, o)
        secs.append(dict(name=nm, addr=addr, off=off, size=size, link=link))
    shstr = secs[shstrndx]["off"]
    for s in secs:
        end = d.index(b"\0", shstr + s["name"])
        s["sname"] = d[shstr + s["name"]:end].decode()
    symsec = next(s for s in secs if s["sname"] == ".symtab")
    strings = d[secs[symsec["link"]]["off"]:]
    out = []
    for i in range(symsec["size"] // 16):
        o = symsec["off"] + i * 16
        nm, val, size, info, _o, shndx = struct.unpack_from(">IIIBBH", d, o)
        if shndx == 0 or nm == 0 or (info & 0xF) != 2 or size < 4:
            continue
        end = strings.index(b"\0", nm)
        out.append((strings[nm:end].decode(), val, size))
    out.sort(key=lambda f: f[1])
    return out


def load_lists(path: Path) -> set[str]:
    src = path.read_text()
    known: set[str] = set()
    for key in ("reimplemented_funcs", "ignored_funcs"):
        if key in src:
            body = src.split(key, 1)[1].split("};", 1)[0]
            known |= set(re.findall(r'"([A-Za-z0-9_]+)"', body))
    return known


RE_MANUAL = re.compile(
    r'\{\s*name\s*=\s*"([^"]+)"\s*,\s*section\s*=\s*"[^"]*"\s*,\s*'
    r'vram\s*=\s*(0x[0-9A-Fa-f]+)\s*,\s*size\s*=\s*(0x[0-9A-Fa-f]+)')


def block_names(text: str, key: str) -> list[str]:
    """The quoted names inside a top-level `key = [ ... ]` array."""
    m = re.search(rf"^{re.escape(key)}\s*=\s*\[", text, re.M)
    if not m:
        return []
    depth, i = 1, m.end()
    while i < len(text) and depth:
        depth += (text[i] == "[") - (text[i] == "]")
        i += 1
    return RE_NAME.findall(text[m.end():i])


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--toml", default="wetrix.toml")
    ap.add_argument("--elf", default="build/wetrix.elf")
    ap.add_argument("--lists", help="N64Recomp's src/symbol_lists.cpp")
    ap.add_argument("--write", action="store_true", help="repair what is mechanical")
    args = ap.parse_args()

    funcs = load_elf(Path(args.elf))
    by_name = {n: (v, s) for n, v, s in funcs}
    text = Path(args.toml).read_text(encoding="utf-8")

    # Widenings in this file change a function's extent, and two functions can
    # legitimately overlap once one is widened -- a patch then belongs to both,
    # which is why some entries here appear twice. So containment is judged
    # against the effective extents, not the ELF's own sizes.
    widened = {}
    for m in re.finditer(r'\{\s*name\s*=\s*"([^"]+)"\s*,\s*size\s*=\s*(0x[0-9A-Fa-f]+)', text):
        widened[m.group(1)] = int(m.group(2), 16)

    def extent(name):
        v, s = by_name[name]
        return v, widened.get(name, s)

    def owner(vram: int):
        best = None
        for n, v, s in funcs:
            if v <= vram:
                best = n
            else:
                break
        if best is None:
            return None
        v, s = extent(best)
        return (best, v, s) if vram < v + s else None

    problems = 0

    # ---- name lists -------------------------------------------------------
    for key in ("ignored", "function_sizes"):
        for name in block_names(text, key):
            if name not in by_name:
                if key == "ignored" and args.lists:
                    known = load_lists(Path(args.lists))
                    if name in known:
                        continue        # on N64Recomp's own list: no ELF body
                print(f"ERROR  {key}: {name} is not a function in the map")
                problems += 1

    # ---- manual functions --------------------------------------------------
    for m in RE_MANUAL.finditer(text):
        name, vram, size = m.group(1), int(m.group(2), 16), int(m.group(3), 16)

        if name in by_name:
            print(f"NOTE   manual_funcs: {name} is already a function in the map")
            continue

        holder = owner(vram)
        if holder is None:
            print(f"ERROR  manual_funcs: {name} 0x{vram:08X} is inside no function")
            problems += 1
            continue

        owner_name, owner_v, owner_size = holder
        end = owner_v + owner_size
        if vram + size != end:
            where = "past" if vram + size > end else f"0x{end - vram - size:X} short of"
            print(f"ERROR  manual_funcs: {name} 0x{vram:08X}+0x{size:X} ends "
                  f"0x{vram + size:08X}, {where} the end of {owner_name} "
                  f"(0x{end:08X})")
            problems += 1

    # ---- instruction patches-----------------------------------------------
    if args.lists:
        known = load_lists(Path(args.lists))
    else:
        known = set()

    lines = text.splitlines(True)
    out, i, rewrites = [], 0, []
    while i < len(lines):
        out.append(lines[i])
        if RE_PATCH.search(lines[i]):
            j, vram, name, func_line = i + 1, None, None, None
            while j < len(lines) and not RE_PATCH.search(lines[j]):
                kv = RE_KV.match(lines[j])
                if kv:
                    if kv.group(1) == "vram":
                        vram = int(kv.group(2).rstrip(","), 0)
                    else:
                        name = kv.group(2).strip().strip('"').rstrip(",")
                        func_line = len(out)
                out.append(lines[j])
                j += 1
            if vram is not None and name is not None and name != "recomp_entrypoint":
                # Already correct under the effective extents: leave it alone.
                if name in by_name:
                    v, s = extent(name)
                    if v <= vram < v + s:
                        i = j
                        continue
                o = owner(vram)
                want = o[0] if o else None
                if want and want in known:
                    want = want + "_recomp"
                if want and want != name:
                    rewrites.append((vram, name, want))
                    if args.write and func_line is not None:
                        out[func_line] = f'func = "{want}"\n'
                elif want is None:
                    print(f"ERROR  patch 0x{vram:08X}: inside no function at all")
                    problems += 1
            i = j
            continue
        i += 1

    for vram, was, now in rewrites:
        print(f"patch   0x{vram:08X}: {was} -> {now}")
    if args.write and rewrites:
        Path(args.toml).write_text("".join(out), encoding="utf-8")
        print(f"\nupdated {len(rewrites)} patch entries in {args.toml}")

    if not rewrites and not problems:
        print("configuration agrees with the map")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
