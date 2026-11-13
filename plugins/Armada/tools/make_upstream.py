#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Gonzi Audio
"""Armada: keep each pack's upstream sources beside the plugin.

    python3 tools/make_upstream.py <pack> [...]      -> upstream/<pack>.tar.xz

The archive holds, unchanged, every file of the firmware that the pack's translation unit includes (found with the
preprocessor's dependency list), the headers the firmware's own generators made (build/gen), its licence files and its
tests/hostsim.c: what tools/make_pack.py reads to write src/pk_<pack>.cpp again. To regenerate a pack:
    mkdir up && for f in upstream/*.tar.xz; do tar -C up -xf $f; done
    ARMADA_UPSTREAM=$PWD/up python3 tools/make_pack.py <pack>
"""
import io
import os
import re
import subprocess
import sys
import tarfile
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PLUGIN = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import make_pack  # noqa: E402


def deps(spec, workdir):
    fake = os.path.join(workdir, "sys")
    os.makedirs(fake, exist_ok=True)
    src = os.path.join(workdir, "tu.c")
    with open(src, "w") as f:
        f.write(spec.TU)
    for _ in range(64):
        cmd = ["gcc", "-E", "-M", "-nostdinc", "-undef", "-std=gnu11", "-isystem", fake]
        cmd += ["-I" + d for d in spec.INCDIRS] + ["-D" + d for d in getattr(spec, "DEFINES", [])] + [src]
        r = subprocess.run(cmd, capture_output=True, text=True)
        m = re.search(r"fatal error: ([^ :]+): No such file", r.stderr)
        if r.returncode and m and m.group(1) in make_pack.SYS_HEADERS:
            os.makedirs(os.path.dirname(os.path.join(fake, m.group(1))), exist_ok=True)
            open(os.path.join(fake, m.group(1)), "w").close()
            continue
        if r.returncode:
            sys.exit(r.stderr[-2000:])
        out = r.stdout.replace("\\\n", " ")
        return [os.path.realpath(p) for p in out.split(":", 1)[1].split() if not p.startswith(workdir)]
    sys.exit("too many missing headers")


def main(names):
    os.makedirs(os.path.join(PLUGIN, "upstream"), exist_ok=True)
    for name in names:
        spec = make_pack.load_spec(name)
        root = os.path.realpath(spec.ROOT)
        with tempfile.TemporaryDirectory() as wd:
            files = set(deps(spec, wd))
        for extra in ("LICENSE", "LICENSE.md", "LICENSING.md", "COPYING", "README.md", "CREDITS.md", "tests/hostsim.c",
                      "firmware/engines/LICENSE", "firmware/bsp/LICENSE", "firmware/bsp/NOTICE"):
            p = os.path.join(root, extra)
            if os.path.isfile(p):
                files.add(os.path.realpath(p))
        lic = os.path.join(root, "LICENSES")
        if os.path.isdir(lic):
            for f in os.listdir(lic):
                files.add(os.path.realpath(os.path.join(lic, f)))
        outside = sorted(f for f in files if not f.startswith(root + os.sep))
        if outside:
            sys.exit("%s: files outside %s: %s" % (name, root, outside[:5]))
        dst = os.path.join(PLUGIN, "upstream", name + ".tar.xz")
        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w:xz", preset=9) as tar:
            note = ("%s\n%s\nupstream commit: %s\nThese files are unchanged copies. build/gen/* were written by the firmware's "
                    "own generators (its tools/).\n" % (spec.TITLE, spec.LICENCE, getattr(spec, "COMMIT", "?"))).encode()
            ti = tarfile.TarInfo(name + "/ARMADA-UPSTREAM.txt")
            ti.size = len(note)
            tar.addfile(ti, io.BytesIO(note))
            for f in sorted(files):
                tar.add(f, arcname=name + "/" + os.path.relpath(f, root), recursive=False,
                        filter=lambda t: (setattr(t, "uid", 0), setattr(t, "gid", 0), setattr(t, "uname", ""),
                                          setattr(t, "gname", ""), setattr(t, "mtime", 0), t)[-1])
        with open(dst, "wb") as f:
            f.write(buf.getvalue())
        print("%s: %d files -> upstream/%s.tar.xz (%d KB)" % (name, len(files), name, len(buf.getvalue()) // 1024))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1:])
