# -*- coding: utf-8 -*-
"""Rebuild the embedded MiSans subset and verify UI character coverage.

The full MiSans-Medium.ttf (~8 MB) is not committed. Pass its path with
--full, or place it at build/font-backup/MiSans-Medium.ttf.

    python tools/subset_font.py --full path/to/MiSans-Medium.ttf
    python tools/subset_font.py --check

Collected characters:
  1. every character in hspec.cpp string/char literals (all UI labels);
  2. printable ASCII 0x20-0x7E;
  3. characters from tools/hardware_words.txt (Chinese vendor names and
     hardware vocabulary), unless --minimal is given.

At runtime, any dynamic line containing a character outside this set is
drawn with the system CJK font instead (per-line fallback), so nothing is
ever rendered as tofu.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "hspec.cpp"
TTF_OUT = ROOT / "winres" / "MiSans-Medium.ttf"
TTF_FULL = ROOT / "build" / "font-backup" / "MiSans-Medium.ttf"
HARDWARE = ROOT / "tools" / "hardware_words.txt"

ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", '"': '"', "'": "'",
           "0": "\0", "a": "\a", "b": "\b", "f": "\f", "v": "\v"}


def _escaped(text, i, out):
    n = len(text)
    if i + 1 >= n:
        return i + 1
    c = text[i + 1]
    if c == "u" and i + 6 <= n:
        try:
            out.add(chr(int(text[i + 2:i + 6], 16)))
            return i + 6
        except ValueError:
            pass
    if c == "x":
        j = i + 2
        while j < n and text[j] in "0123456789abcdefABCDEF" and j - i < 5:
            j += 1
        try:
            out.add(chr(int(text[i + 2:j], 16)))
            return j
        except ValueError:
            pass
    out.add(ESCAPES.get(c, c))
    return i + 2


def source_chars(text):
    out = set()
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                i += 1
            i += 2
        elif c == '"':
            i += 1
            while i < n and text[i] != '"':
                if text[i] == "\\":
                    i = _escaped(text, i, out)
                else:
                    out.add(text[i])
                    i += 1
            i += 1
        elif c == "'":
            i += 1
            while i < n and text[i] != "'":
                if text[i] == "\\":
                    i = _escaped(text, i, out)
                else:
                    out.add(text[i])
                    i += 1
            i += 1
        else:
            i += 1
    return out


def hardware_chars():
    out = set()
    if HARDWARE.exists():
        out |= {c for c in HARDWARE.read_text(encoding="utf-8", errors="replace")
                if ord(c) >= 0x80}
    return out


def collect(include_hardware):
    chars = {chr(c) for c in range(0x20, 0x7F)}
    chars |= source_chars(SRC.read_text(encoding="utf-8", errors="replace"))
    if include_hardware:
        chars |= hardware_chars()
    chars -= set("\r\n\t\0")
    return chars


def _family_name(font):
    for r in font["name"].names:
        if r.nameID == 1 and r.platformID == 3:
            return r.toUnicode()
    return None


def main():
    from fontTools.ttLib import TTFont

    check_only = "--check" in sys.argv
    include_hardware = "--minimal" not in sys.argv
    full = TTF_FULL
    if "--full" in sys.argv:
        full = Path(sys.argv[sys.argv.index("--full") + 1])

    chars = collect(include_hardware)
    print("UI chars: %d" % len(chars))

    if check_only:
        font = TTFont(str(TTF_OUT))
        cmap = font.getBestCmap()
        missing = sorted(c for c in chars if ord(c) >= 0x80 and ord(c) not in cmap)
        if missing:
            print("[ERR] %d chars missing from the subset:" % len(missing))
            for c in missing:
                print("   U+%04X %s" % (ord(c), c))
            return 1
        os2 = font["OS/2"]
        if os2.usWeightClass != 500 or not (os2.fsSelection & 0x40):
            print("[ERR] OS/2 metadata is wrong (usWeightClass=%d fsSelection=0x%04X)"
                  % (os2.usWeightClass, os2.fsSelection))
            return 1
        fam = _family_name(font)
        if fam != "MiSans":
            print("[ERR] family name is %r (expected MiSans)" % fam)
            return 1
        print("[OK] subset covers all UI chars (cmap %d, %d bytes)"
              % (len(cmap), TTF_OUT.stat().st_size))
        return 0

    if not full.exists():
        print("[ERR] full font not found: %s (use --full)" % full)
        return 1

    from fontTools import subset
    font = TTFont(str(full))
    options = subset.Options()
    options.name_IDs = ["*"]
    options.notdef_outline = True
    options.recalc_bounds = True
    options.drop_tables += ["FFTM"]
    ss = subset.Subsetter(options)
    ss.populate(unicodes=[ord(c) for c in chars])
    ss.subset(font)

    # GDI / GDI+ match by family + weight; fix MiSans' non-standard metadata.
    os2 = font["OS/2"]
    os2.usWeightClass = 500
    os2.fsSelection = (os2.fsSelection & ~0x21) | 0x40

    nametbl = font["name"]
    nametbl.names = [r for r in nametbl.names if r.platformID == 3]
    for r in nametbl.names:
        if r.nameID == 1:
            r.string = "MiSans"
        elif r.nameID == 2:
            r.string = "Medium"

    font.save(str(TTF_OUT))
    print("[OK] wrote %s: %d chars, %d bytes"
          % (TTF_OUT.relative_to(ROOT), len(chars), TTF_OUT.stat().st_size))
    return 0


if __name__ == "__main__":
    sys.exit(main())
