#!/usr/bin/env python3
"""Fetch the ELF from wetrix-n64-disassembly into build/wetrix.elf.

    python tools/fetch_elf.py [--disasm DIR] [--check]

The disassembly builds the ELF (its tools/build_elf.py) and verifies it
against the ROM (its tools/verify_elf.py); this copies the result here, where
wetrix.toml expects it. The copy only happens when the content differs, so an
unchanged ELF keeps its timestamp.

Afterwards it says whether build/recomp is older than the ELF, which means
N64Recomp must run again (README, step 4): recompiled C from an older ELF
silently compiles the wrong functions.

--check copies nothing and exits 1 if the ELF here is missing, differs from the
disassembly's, or is newer than build/recomp.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import deps  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DISASM = deps.resolve(ROOT, "wetrix-n64-disassembly", "WETRIX_DISASM_DIR", clone=False)
ELF = ROOT / "build" / "wetrix.elf"
RECOMP_MARK = ROOT / "build" / "recomp" / "funcs.h"


def sha256(p: Path) -> str:
    h = hashlib.sha256()
    with p.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--disasm", default=os.environ.get("WETRIX_DISASM_DIR", str(DEFAULT_DISASM)),
                    help="the wetrix-n64-disassembly checkout (default: the sibling folder)")
    ap.add_argument("--check", action="store_true", help="report only; exit 1 if anything is out of date")
    args = ap.parse_args()

    src = Path(args.disasm).resolve() / "build" / "wetrix.elf"
    if not src.is_file():
        print(f"fetch_elf: no ELF at {src}", file=sys.stderr)
        print("  build it in the disassembly first: tools/build_elf.py, then tools/verify_elf.py", file=sys.stderr)
        return 1

    stale = False
    src_hash = sha256(src)
    have = ELF.is_file() and sha256(ELF) == src_hash
    if have:
        print(f"fetch_elf: build/wetrix.elf is current ({src_hash[:16]})")
    elif args.check:
        print("fetch_elf: build/wetrix.elf is " + ("different from" if ELF.is_file() else "missing; expected")
              + f" the disassembly's ({src_hash[:16]})")
        stale = True
    else:
        ELF.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, ELF)  # fresh mtime: the ELF here is now newer than build/recomp
        print(f"fetch_elf: copied {src} -> build/wetrix.elf ({src_hash[:16]})")

    if ELF.is_file():
        if not RECOMP_MARK.is_file():
            print("fetch_elf: build/recomp is empty: run tools/regen.py")
            stale = True
        elif ELF.stat().st_mtime > RECOMP_MARK.stat().st_mtime:
            print("fetch_elf: build/recomp is older than the ELF: run tools/regen.py")
            stale = True

    return 1 if (stale and args.check) else 0


if __name__ == "__main__":
    sys.exit(main())
