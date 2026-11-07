# -*- coding: utf-8 -*-
r"""Build the compact PCI ID tables embedded by HSpec.

Input files are the full IDS exports shipped with the reference tools:
    PCI\VEN_10DE&DEV_25A2&SUBSYS_...=NVIDIA GeForce RTX 3050 Laptop GPU

We keep only VEN+DEV (the SUBSYS/REV variants almost always share the same
device name) and the most common name for each pair.  Output:

    data/gpu_ids.txt   VENDEV=Name
    data/nic_ids.txt   VENDEV=Name

Usage:
    python tools/build_ids.py --gpu <显卡ID.IDS> --nic <网卡ID.ids>
"""
import collections
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT_GPU = ROOT / "data" / "gpu_ids.txt"
OUT_NIC = ROOT / "data" / "nic_ids.txt"

ID_RE = re.compile(r"VEN_([0-9A-Fa-f]{4})&DEV_([0-9A-Fa-f]{4})")
SQUARE_RE = re.compile(r"\[\d+\]")


def build(path, out):
    pairs = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("[") or "=" not in line:
                continue
            key, name = line.split("=", 1)
            m = ID_RE.search(key)
            if not m:
                continue
            vd = (m.group(1) + m.group(2)).upper()
            name = SQUARE_RE.sub("", name).strip()
            if not name:
                continue
            pairs.setdefault(vd, collections.Counter())[name] += 1

    lines = []
    for vd in sorted(pairs):
        name = pairs[vd].most_common(1)[0][0]
        lines.append("%s=%s" % (vd, name))
    out.parent.mkdir(parents=True, exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("%s: %d entries, %d bytes" % (out.relative_to(ROOT), len(lines),
                                        out.stat().st_size))


def main():
    args = sys.argv[1:]
    gpu = nic = None
    if "--gpu" in args:
        gpu = args[args.index("--gpu") + 1]
    if "--nic" in args:
        nic = args[args.index("--nic") + 1]
    if not gpu or not nic:
        print("usage: build_ids.py --gpu <file> --nic <file>")
        return 1
    build(gpu, OUT_GPU)
    build(nic, OUT_NIC)
    return 0


if __name__ == "__main__":
    sys.exit(main())
