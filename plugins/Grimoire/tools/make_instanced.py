#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Gonzi Audio
"""Grimoire: GR!MM's sources (src/grimm/) -> src/core/, with every mutable file-level global moved into per-instance
state.

GR!MM is written for one FM-1: its state lives in `static` globals. A plugin may be loaded several times in one process,
so each top-level `static` declaration that is not const, inline or a function becomes its own small struct (its
initialisers kept as default member initialisers), reached through one thread-local pointer to the running instance's
table of parts:

    static int32_t lfo_rng = 0x2545F491u;
 -> struct gp_17 { int32_t lfo_rng = 0x2545F491u; };
    #define lfo_rng (((gp_17 *)grimm_parts[17])->lfo_rng)

src/core/parts.h lists the parts so the plugin can make and free one set per instance (grimm_new_parts /
grimm_reset_parts / grimm_free_parts). Re-run after changing src/grimm/:  python3 tools/make_instanced.py
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "src", "grimm")
DST = os.path.join(HERE, "..", "src", "core")


def strip_attr(s):
    out, i = "", 0
    while True:
        j = s.find("__attribute__", i)
        if j < 0:
            return out + s[i:]
        out += s[i:j]
        k = s.index("(", j)
        depth = 0
        while True:
            if s[k] == "(":
                depth += 1
            elif s[k] == ")":
                depth -= 1
                if depth == 0:
                    break
            k += 1
        i = k + 1


def split_top(s, sep):
    parts, depth, cur = [], 0, ""
    for c in s:
        if c in "{([":
            depth += 1
        elif c in "})]":
            depth -= 1
        if c == sep and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += c
    parts.append(cur)
    return parts


def statements(text):
    """top-level statements: (start, end) offsets; comments, strings and preprocessor lines skipped"""
    i, n, depth, start = 0, len(text), 0, None
    line_start = True
    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            i = text.index("*/", i + 2) + 2
            continue
        if text.startswith("//", i):
            i = text.index("\n", i)
            continue
        if c == "\n":
            line_start = True
            i += 1
            continue
        if line_start and c == "#":         # a preprocessor line (with its continuations)
            while True:
                e = text.index("\n", i)
                if text[e - 1] != "\\":
                    break
                i = e + 1
            i = e
            continue
        if not c.isspace():
            line_start = False
        if c in "\"'":
            q = c
            i += 1
            while text[i] != q:
                i += 2 if text[i] == "\\" else 1
            i += 1
            continue
        if depth == 0 and start is None and not c.isspace():
            start = i
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0 and start is not None:
                # a function body ends here (a struct's does at its ';')
                head = text[start:i]
                if not re.match(r"\s*(static\s+)?(volatile\s+)?(struct|union|enum)\b", head) and "=" not in head.split("{")[0]:
                    yield start, i + 1
                    start = None
        elif c == ";" and depth == 0 and start is not None:
            yield start, i + 1
            start = None
        i += 1


def mutable_decl(stmt):
    s = strip_attr(re.sub(r"/\*.*?\*/", " ", stmt, flags=re.S)).strip()
    if not s.startswith("static ") or not s.endswith(";"):
        return None
    if re.match(r"static\s+(const|inline|constexpr)\b", s):
        return None
    body = s[len("static "):-1].strip()
    if body.startswith("const ") or " const " in body.split("=")[0].split("{")[0] and "*" not in body.split("=")[0]:
        return None
    # the declarators: after the type (a struct's after its closing brace)
    if re.match(r"(volatile\s+)?struct\s*\{", body):
        close = body.index("{")
        depth = 0
        for k in range(close, len(body)):
            if body[k] == "{":
                depth += 1
            elif body[k] == "}":
                depth -= 1
                if depth == 0:
                    break
        typ, decls = body[:k + 1], body[k + 1:]
    else:
        m = re.match(r"((?:volatile\s+|unsigned\s+|signed\s+)*[A-Za-z_][A-Za-z_0-9]*(?:\s*\*)*)\s*(.*)$", body, re.S)
        typ, decls = m.group(1), m.group(2)
    if "(" in decls.split("=")[0]:
        return None                         # a function (or a pointer to one)
    names = []
    for d in split_top(decls, ","):
        d = d.strip()
        m = re.match(r"\**\s*([A-Za-z_][A-Za-z_0-9]*)", d)
        if not m:
            return None
        names.append(m.group(1))
    return s, typ, names


def main():
    os.makedirs(DST, exist_ok=True)
    idx = 0
    parts = []
    for fn in sorted(os.listdir(SRC)):
        src = open(os.path.join(SRC, fn), encoding="utf-8").read()
        if not fn.endswith(".c"):
            open(os.path.join(DST, fn), "w", encoding="utf-8").write(src)
            continue
        out, last = [], 0
        for a, b in statements(src):
            d = mutable_decl(src[a:b])
            if not d:
                continue
            s, typ, names = d
            member = s[len("static "):]
            out.append(src[last:a])
            gp = "gp_%d" % idx
            mac = "".join("#define %s (((%s *)grimm_parts[%d])->%s)\n" % (nm, gp, idx, nm) for nm in names)
            out.append("struct %s { %s };   /* (Grimoire: was a static, now the instance's: tools/make_instanced.py) */\n%s"
                       % (gp, member, mac))
            parts.append((idx, gp, fn, names))
            idx += 1
            last = b
        out.append(src[last:])
        open(os.path.join(DST, fn), "w", encoding="utf-8").write("".join(out))
    with open(os.path.join(DST, "parts.h"), "w") as f:
        f.write("/* generated by tools/make_instanced.py: an instance's state, one part per GR!MM global */\n")
        f.write("#define GRIMM_NPARTS %d\n" % idx)
        f.write("static void grimm_new_parts(void **p)\n{\n")
        for i, gp, fn, names in parts:
            f.write("    p[%d] = new %s();   /* %s: %s */\n" % (i, gp, fn, ", ".join(names)))
        f.write("}\nstatic void grimm_reset_parts(void **p)   /* every part as new, in place (no allocation) */\n{\n")
        for i, gp, fn, names in parts:
            f.write("    ::new (p[%d]) %s();\n" % (i, gp))
        f.write("}\nstatic void grimm_free_parts(void **p)\n{\n")
        for i, gp, fn, names in parts:
            f.write("    delete (%s *)p[%d];\n" % (gp, i))
        f.write("}\n")
    print("%d globals moved into instance state" % idx)
    for i, gp, fn, names in parts:
        print("  %-12s %s" % (fn, ", ".join(names)))


if __name__ == "__main__":
    main()
