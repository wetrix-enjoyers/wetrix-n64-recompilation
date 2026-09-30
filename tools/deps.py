"""Where the repositories this one depends on are, cloning them when they are not there.

A dependency is looked for in this order:

  1. the folder named by its environment variable (WETRIX_DISASM_DIR, ...);
  2. a sibling folder next to this repository (../<name>), the layout of a
     developer who has cloned everything side by side;
  3. this repository's own deps/ folder.

If none exists and cloning is allowed, it is cloned into deps/ from
$WETRIX_GIT_BASE/<name> (default https://github.com/wetrix-enjoyers). A folder that
already exists is used as it is: nothing is pulled, and nothing is checked out over
it. Set WETRIX_GIT_BASE to a folder or URL to clone from somewhere else, for
example a local mirror.
"""

from __future__ import annotations

import os
import subprocess
from pathlib import Path

GIT_BASE = os.environ.get("WETRIX_GIT_BASE", "https://github.com/wetrix-enjoyers").rstrip("/")

# The branch, tag or commit a fresh clone is put on. Pin a tag or commit here for a
# release so that the same source always builds the same way.
REF = "main"


def locate(root: Path, name: str, env_var: str) -> Path | None:
    override = os.environ.get(env_var)
    if override:
        return Path(override).resolve()
    for candidate in (root.parent / name, root / "deps" / name):
        if candidate.is_dir():
            return candidate.resolve()
    return None


def resolve(root: Path, name: str, env_var: str, clone: bool = True) -> Path:
    """The folder holding dependency `name`, cloned into root/deps if it is missing.
    With clone=False, the deps/ path is returned (and may not exist)."""
    found = locate(root, name, env_var)
    if found is not None:
        return found
    dest = root / "deps" / name
    if not clone:
        return dest
    url = f"{GIT_BASE}/{name}"
    print(f"deps: {name} not found; cloning {url} into {dest}", flush=True)
    dest.parent.mkdir(exist_ok=True)
    subprocess.run(["git", "clone", url, str(dest)], check=True)
    subprocess.run(["git", "-C", str(dest), "checkout", REF], check=True)
    return dest.resolve()
