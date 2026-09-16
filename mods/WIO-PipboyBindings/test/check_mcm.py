#!/usr/bin/env python3
"""Cross-checks the MCM content against itself and against the plugin source.

Run from anywhere: python test/check_mcm.py

Three checks, and the second one matters more than it looks:

  1. Every $key referenced anywhere exists in the translation file. References come from
     config.json, keybinds.json, and string literals in src/ (keys the plugin localises itself at
     runtime). A missing config key renders as the raw "$KEY" text on screen; a missing source key
     silently falls back to the English text compiled beside it, so it looks fine on an English
     machine and is untranslated everywhere else.

  2. Every "key:Section" id in config.json has a matching key in settings.ini, and vice versa.
     THIS IS THE ONE THAT BITES SILENTLY. A widget whose id is not in settings.ini is not in
     MCM's store at all, so it reads and writes nothing - no error, no warning, the control just
     does not work.

  3. The translation file has no duplicate keys and is UTF-16 with CRLF, which is what the game's
     translator expects.

This script is mod-agnostic and shared verbatim across the WattzIO family: it finds the single
MCM config folder and the single translation file itself, and learns runtime-only keys by reading
src/ rather than from a hand-kept list that would drift.
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONFIG_ROOT = ROOT / "data" / "MCM" / "Config"
TRANSLATIONS = ROOT / "data" / "Interface" / "Translations"
SOURCE = ROOT / "src"

# A "$Key" inside a C++ string literal.
SOURCE_KEY = re.compile(r'"(\$[A-Za-z][A-Za-z0-9_]*)"')

failures = []


def fail(msg):
    failures.append(msg)
    print(f"  FAIL {msg}")


def single(paths, what):
    paths = sorted(paths)
    if len(paths) != 1:
        names = ", ".join(p.name for p in paths) or "none"
        print(f"expected exactly one {what}, found {len(paths)}: {names}")
        sys.exit(1)
    return paths[0]


def load_translation(path):
    raw = path.read_bytes()
    if raw[:2] != b"\xff\xfe":
        fail(f"{path.name} is not UTF-16 LE (no BOM)")
    # newline="" so CRLF survives the read and check 3 can actually see it.
    with path.open(encoding="utf-16", newline="") as handle:
        text = handle.read()
    if "\n" in text.replace("\r\n", ""):
        fail(f"{path.name} has lone LF line endings; the game wants CRLF")
    keys = {}
    for line in text.replace("\r\n", "\n").split("\n"):
        if not line.startswith("$"):
            continue
        key = line.split("\t", 1)[0]
        if key in keys:
            fail(f"duplicate translation key {key}")
        keys[key] = line
    return set(keys)


def referenced_keys(obj, out):
    """Every "$..." string anywhere in a config document."""
    if isinstance(obj, dict):
        for value in obj.values():
            referenced_keys(value, out)
    elif isinstance(obj, list):
        for value in obj:
            referenced_keys(value, out)
    elif isinstance(obj, str):
        for token in obj.replace("<", " ").replace(">", " ").split():
            if token.startswith("$"):
                out.add(token.strip("',\"()"))
    return out


def mod_prefix(translated):
    """The "$XXX_" prefix most of this mod's own keys share."""
    counts = {}
    for key in translated:
        match = re.match(r"(\$[A-Za-z0-9]+_)", key)
        if match:
            counts[match.group(1)] = counts.get(match.group(1), 0) + 1
    return max(counts, key=counts.get) if counts else "$"


def source_keys(prefix):
    # Only this mod's own prefix. Source also names the game's own keys (a vanilla HUD string
    # like "$QuickContainerTransfer"), which resolve from the game's translation, not this one.
    out = set()
    if SOURCE.is_dir():
        for path in sorted(SOURCE.rglob("*")):
            if path.suffix in (".h", ".hpp", ".cpp"):
                found = SOURCE_KEY.findall(path.read_text(encoding="utf-8", errors="replace"))
                out.update(key for key in found if key.startswith(prefix))
    return out


def widget_ids(obj, out):
    """Every id of the form key:Section."""
    if isinstance(obj, dict):
        wid = obj.get("id")
        if isinstance(wid, str) and ":" in wid:
            out.add(wid)
        for value in obj.values():
            widget_ids(value, out)
    elif isinstance(obj, list):
        for value in obj:
            widget_ids(value, out)
    return out


def ini_ids(path):
    out = set()
    section = None
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        line = line.strip()
        if not line or line.startswith(";"):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
        elif "=" in line:
            if section is None:
                fail(f"settings.ini has a key outside any section: {line}")
                continue
            out.add(f"{line.split('=', 1)[0].strip()}:{section}")
    return out


def main():
    mcm = single((p for p in CONFIG_ROOT.glob("*") if p.is_dir()), f"MCM config folder under {CONFIG_ROOT}")
    translation = single(TRANSLATIONS.glob("*.txt"), f"translation file under {TRANSLATIONS}")
    print(f"checking MCM content under {mcm}")
    translated = load_translation(translation)

    configs = {}
    for name in ("config.json", "keybinds.json"):
        path = mcm / name
        if path.is_file():
            configs[name] = json.loads(path.read_text(encoding="utf-8-sig"))
    if "config.json" not in configs:
        fail("config.json is missing")
        configs["config.json"] = {}

    # 1. referenced keys exist
    from_configs = set()
    for doc in configs.values():
        referenced_keys(doc, from_configs)
    from_source = source_keys(mod_prefix(translated))
    for key in sorted(from_configs - translated):
        fail(f"{key} is referenced by a config file but missing from the translation")
    for key in sorted(from_source - translated):
        fail(f"{key} is used in src/ but missing from the translation "
             f"(falls back to compiled-in English)")

    # 2. widget ids match settings.ini both ways
    widgets = widget_ids(configs["config.json"], set())
    ini = ini_ids(mcm / "settings.ini")
    for wid in sorted(widgets - ini):
        fail(f"{wid} is a widget in config.json with no key in settings.ini "
             f"(the control will silently read and write nothing)")
    for wid in sorted(ini - widgets):
        fail(f"{wid} is in settings.ini but no widget uses it")

    unused = sorted(translated - from_configs - from_source)

    print(f"  {len(translated)} translation keys, {len(from_configs)} referenced by config, "
          f"{len(from_source)} by source, {len(widgets)} widget ids, {len(ini)} ini keys")
    if unused:
        # Not a failure: a key can be kept for a control that is temporarily removed.
        print(f"  note: {len(unused)} translation key(s) unreferenced: {', '.join(unused)}")

    if failures:
        print(f"\n{len(failures)} problem(s)")
        return 1
    print("\nall checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
