#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Armada: run a Felucca-family firmware's own table generators (tools/build.py lists them) into its build/gen/."""
import os, re, subprocess, sys
root = os.path.abspath(sys.argv[1])
bp = open(os.path.join(root, "tools", "build.py"), errors="replace").read()
os.makedirs(os.path.join(root, "build", "gen"), exist_ok=True)
done = 0
for m in re.finditer(r'\[\s*tools\s*/\s*"(gen_[a-z0-9_]+\.py)"\s*,\s*GEN\s*/\s*"([A-Za-z0-9_.]+)"([^\]]*)\]', bp):
    script, out, rest = m.groups()
    if re.search(r"font|icons|keycaps|palettes", script):
        continue
    extra = re.findall(r'"([^"]+)"', rest)
    cmd = [sys.executable, os.path.join(root, "tools", script), os.path.join(root, "build", "gen", out)] + extra
    r = subprocess.run(cmd, cwd=root, capture_output=True, text=True)
    print("  %-22s %s %s" % (script, "ok" if r.returncode == 0 else "FAILED", "" if r.returncode == 0 else r.stderr.strip().split("\n")[-1][:150]))
    done += r.returncode == 0
print("  %d generated" % done)
