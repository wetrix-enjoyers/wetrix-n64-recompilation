#!/usr/bin/env python3
"""Bring the generated sources up to date, from the disassembly down.

    python tools/regen.py [--no-disasm] [--force]

Every port's build runs this first, so a change anywhere upstream reaches the
binary; each step is skipped when its inputs are unchanged:

  0. setup    what a fresh clone lacks, each only if absent:
              - build/n64modernruntime: cloned at the pinned commit below
              - wetrix.z64: adopted from the disassembly's baserom.z64 (or a
                ROM found by the search wetrix_provision does), building
                wetrix_provision first
  1. disasm   ../wetrix-n64-disassembly/tools/build.py: venv, split and ELF,
              each only if its own inputs changed (--no-disasm skips it)
  2. elf      tools/fetch_elf.py: copy the ELF here if it differs
  3. tools    build N64Recomp and RSPRecomp from the runtime checkout
              (incremental; a no-op when built)
  4. recomp   N64Recomp  -> build/recomp/   inputs: the ELF, wetrix.toml,
                                                    wetrix.z64, N64Recomp
  5. rsp      RSPRecomp  -> build/rsp/      inputs: wetrix_rsp.toml, wetrix.z64,
                                                    RSPRecomp

Steps 4 and 5 generate into a scratch folder and copy over only the files whose
content changed, so the compiler rebuilds only those. Each records its inputs'
hashes in `.inputs` in its output folder; CMakeLists.txt re-hashes them at
configure time (and re-configures when they change) and stops the build if
they no longer match, so a raw `cmake --build` can't use stale sources either.

The disassembly is found through tools/deps.py: WETRIX_DISASM_DIR, a sibling
folder, or deps/, and cloned into deps/ from GitHub when it is not there.
The ROM: WETRIX_ROM, the disassembly's baserom.z64, or (wetrix_provision's own
search) beside the tools folder, in data/, or up to four folders above. Once
wetrix.z64 exists it is copied to the disassembly's baserom.z64 if that is missing.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import deps  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
ROM_ENV = "WETRIX_ROM"


def disasm_dir(clone: bool = True) -> Path:
    return deps.resolve(ROOT, "wetrix-n64-disassembly", "WETRIX_DISASM_DIR", clone=clone)
RUNTIME = BUILD / "n64modernruntime"
TOOLS = BUILD / "n64recomp-build"
EXE = ".exe" if os.name == "nt" else ""
IMAGE = ROOT / "wetrix.z64"

# The runtime every part of this project builds against, pinned.
RUNTIME_URL = "https://github.com/N64Recomp/N64ModernRuntime.git"
RUNTIME_COMMIT = "cdf5abbd5026fef5c364c676e4667c45e42b6863"
MSYS_BIN = Path(os.environ.get("MSYS2_ROOT", "C:/msys64")) / "ucrt64" / "bin"


def env() -> dict:
    e = dict(os.environ)
    if os.name == "nt" and MSYS_BIN.is_dir():  # the UCRT64 toolchain and its runtime DLLs
        e["PATH"] = str(MSYS_BIN) + os.pathsep + e["PATH"]
    return e


def run(cmd: list, cwd: Path = ROOT) -> None:
    print("regen:", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], cwd=cwd, check=True, env=env())


def sha256(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def record(inputs: list[Path]) -> str:
    """The `.inputs` text: one `<sha256> <path relative to here>` line per input.
    CMakeLists.txt reads the same format back."""
    return "".join(f"{sha256(p)} {p.relative_to(ROOT).as_posix()}\n" for p in inputs)


def sync(src: Path, dst: Path) -> int:
    """Make dst hold exactly src's files, touching only those that differ."""
    dst.mkdir(parents=True, exist_ok=True)
    changed = 0
    names = {f.name for f in src.iterdir() if f.is_file()}
    for name in sorted(names):
        s, d = src / name, dst / name
        if not d.is_file() or d.read_bytes() != s.read_bytes():
            shutil.copyfile(s, d)
            changed += 1
    for d in dst.iterdir():
        if d.is_file() and d.name not in names and d.name != ".inputs":
            d.unlink()
            changed += 1
    shutil.rmtree(src)
    return changed


def generate(name: str, tool: Path, toml: Path, key: str, out: Path, inputs: list[Path], force: bool) -> None:
    """Run `tool` on a copy of `toml` whose output `key` points at a scratch
    folder, then sync the scratch folder into `out`."""
    rec = out / ".inputs"
    want = record(inputs)
    if not force and rec.is_file() and rec.read_text() == want:
        print(f"regen: {out.relative_to(ROOT).as_posix()} is current")
        return
    scratch = BUILD / f"{out.name}.new"
    shutil.rmtree(scratch, ignore_errors=True)
    scratch.mkdir(parents=True)
    text = toml.read_text(encoding="utf-8")
    old = re.search(rf'^{key}\s*=\s*"([^"]+)"', text, re.M)
    if not old:
        sys.exit(f"regen: no {key} in {toml.name}")
    target = (scratch / Path(old.group(1)).name) if key == "output_file_path" else scratch
    text = text[:old.start(1)] + target.relative_to(ROOT).as_posix() + text[old.end(1):]
    tmp = ROOT / f".regen.{toml.name}"  # beside the original, so its other relative paths still resolve
    tmp.write_text(text, encoding="utf-8")
    try:
        rec.unlink(missing_ok=True)
        run([tool, tmp.name])
    finally:
        tmp.unlink(missing_ok=True)
    n = sync(scratch, out)
    rec.write_bytes(want.encode())  # LF only: CMake reads it back on Linux too
    print(f"regen: {name}: {n} file(s) changed in {out.relative_to(ROOT).as_posix()}")


def ensure_runtime() -> None:
    if not (RUNTIME / "CMakeLists.txt").is_file():
        run(["git", "clone", "--recurse-submodules", RUNTIME_URL, RUNTIME])
        run(["git", "-C", RUNTIME, "checkout", RUNTIME_COMMIT])
        run(["git", "-C", RUNTIME, "submodule", "update", "--init", "--recursive"])
        return
    head = subprocess.run(["git", "-C", str(RUNTIME), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    if head != RUNTIME_COMMIT:
        print(f"regen: WARNING: build/n64modernruntime is at {head[:12] or 'no commit'}, pinned {RUNTIME_COMMIT[:12]}")


def ensure_image(disasm: Path) -> None:
    """wetrix.z64, the game image: adopted from the ROM the disassembly holds."""
    if IMAGE.is_file():
        return
    tools = BUILD / "tools"
    if not (tools / "CMakeCache.txt").is_file():
        run(["cmake", "-S", ROOT / "tools", "-B", tools, "-G", "Ninja"])
    run(["cmake", "--build", tools, "--parallel"])
    rom = Path(os.environ[ROM_ENV]) if os.environ.get(ROM_ENV) else disasm / "baserom.z64"
    run([tools / f"wetrix_provision{EXE}"] + ([rom] if rom.is_file() else []) + ["-o", ROOT])


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--no-disasm", action="store_true", help="don't rebuild the disassembly's ELF")
    ap.add_argument("--force", action="store_true", help="regenerate everything")
    args = ap.parse_args()

    ensure_runtime()
    disasm = disasm_dir(clone=not args.no_disasm)
    ensure_image(disasm)
    # The disassembly splits the same ROM the game image was adopted from.
    if disasm.is_dir() and IMAGE.is_file() and not (disasm / "baserom.z64").is_file():
        shutil.copyfile(IMAGE, disasm / "baserom.z64")

    if not args.no_disasm:
        if (disasm / "tools" / "build.py").is_file():
            run([sys.executable, disasm / "tools" / "build.py"] + (["--force"] if args.force else []), cwd=disasm)
        else:
            print(f"regen: no disassembly at {disasm}; using the ELF as it is")
    if (disasm / "build" / "wetrix.elf").is_file():
        run([sys.executable, ROOT / "tools" / "fetch_elf.py", "--disasm", disasm])

    for need in (BUILD / "wetrix.elf", ROOT / "wetrix.z64", RUNTIME / "N64Recomp" / "CMakeLists.txt"):
        if not need.exists():
            sys.exit(f"regen: missing {need} (see README.md, steps 1-3)")

    cache = TOOLS / "CMakeCache.txt"
    if cache.is_file():  # a build dir from another location (a moved folder) can't be reused
        made = re.search(r"^CMAKE_CACHEFILE_DIR:INTERNAL=(.*)$", cache.read_text(errors="replace"), re.M)
        if not made or Path(made.group(1)).resolve() != TOOLS.resolve():
            print(f"regen: {TOOLS.name} was configured elsewhere; rebuilding it")
            shutil.rmtree(TOOLS)
    if not cache.is_file():
        run(["cmake", "-S", RUNTIME / "N64Recomp", "-B", TOOLS, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"])
    run(["cmake", "--build", TOOLS, "--parallel", "--target", "N64RecompCLI", "RSPRecomp"])

    n64recomp, rsprecomp = TOOLS / f"N64Recomp{EXE}", TOOLS / f"RSPRecomp{EXE}"
    generate("N64Recomp", n64recomp, ROOT / "wetrix.toml", "output_func_path", BUILD / "recomp",
             [BUILD / "wetrix.elf", ROOT / "wetrix.toml", ROOT / "wetrix.z64", n64recomp], args.force)
    generate("RSPRecomp", rsprecomp, ROOT / "wetrix_rsp.toml", "output_file_path", BUILD / "rsp",
             [ROOT / "wetrix_rsp.toml", ROOT / "wetrix.z64", rsprecomp], args.force)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except subprocess.CalledProcessError as e:
        sys.exit(f"regen: failed ({e.returncode})")
