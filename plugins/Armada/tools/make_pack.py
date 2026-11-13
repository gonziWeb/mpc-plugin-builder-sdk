#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Gonzi Audio
"""Armada: a firmware's C sources -> one C++ file whose state is per plugin instance.

The FM-1 firmwares keep their state in file-level `static` variables: fine for one synth, wrong for a plugin that
is loaded several times in one process (and rendered from several threads). This tool

  1. flattens a pack's translation unit (tools/packs/<name>.py: the include list and the glue) with the C
     preprocessor (comments kept, system headers left as #include lines);
  2. parses the result with libclang as C and finds every variable with static storage that is not const
     (file level, and `static` inside functions, which are hoisted to file level);
  3. rewrites each reference to such a variable as PKV(k), the same variable inside the running instance's block
     (one thread-local base pointer + a compile-time offset), and adds the table the plugin uses to make a block
     (sizes, initial values, pointers between the variables);
  4. applies the pack's C -> C++ fix-ups and writes src/gen/pk_<name>.cpp.

    python3 tools/make_pack.py <name> [...]        (pip install libclang; zig via the SDK's .sdk-tools/zigenv)
"""
import importlib.util
import os
import re
import subprocess
import sys
import tempfile

import clang.cindex as ci

HERE = os.path.dirname(os.path.abspath(__file__))
PLUGIN = os.path.dirname(HERE)
SDK = os.path.dirname(os.path.dirname(PLUGIN))
GEN = os.path.join(PLUGIN, "src")
ZIG = [os.path.join(SDK, ".sdk-tools", "zigenv", "bin", "python"), "-m", "ziglang"]
K = ci.CursorKind
SYS_HEADERS = set("stdint.h stddef.h string.h stdlib.h stdio.h math.h stdbool.h limits.h stdarg.h assert.h time.h unistd.h "
                  "ctype.h inttypes.h errno.h float.h sys/wait.h sys/types.h sys/time.h stdalign.h stdnoreturn.h "
                  "emscripten.h emscripten/emscripten.h pthread.h sched.h signal.h".split())


def load_spec(name):
    sys.path.insert(0, os.path.join(HERE, "packs"))
    path = os.path.join(HERE, "packs", name + ".py")
    spec = importlib.util.spec_from_file_location("pack_" + name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def flatten(tu_text, incdirs, defines, workdir):
    """cc -E with comments, system headers kept as #include lines (a directory of stand-ins that leave a marker)"""
    fake = os.path.join(workdir, "sys")
    os.makedirs(fake, exist_ok=True)
    src = os.path.join(workdir, "tu.c")
    with open(src, "w") as f:
        f.write(tu_text)
    for _ in range(64):
        cmd = ["gcc", "-E", "-P", "-C", "-nostdinc", "-undef", "-std=gnu11", "-isystem", fake]
        cmd += ["-I" + d for d in incdirs] + ["-D" + d for d in defines] + [src]
        r = subprocess.run(cmd, capture_output=True, text=True)
        m = re.search(r"fatal error: ([^ :]+): No such file", r.stderr)
        if r.returncode and m:
            h = m.group(1)
            if h not in SYS_HEADERS:
                sys.exit("flatten: missing header %s\n%s" % (h, r.stderr[-800:]))
            os.makedirs(os.path.dirname(os.path.join(fake, h)), exist_ok=True)
            with open(os.path.join(fake, h), "w") as f:
                f.write("__PK_SYSINC__ <%s>\n" % h)
            continue
        if r.returncode:
            sys.exit("flatten failed:\n" + r.stderr[-3000:])
        out = r.stdout
        seen, lines = set(), []
        for ln in out.split("\n"):
            m = re.match(r"\s*__PK_SYSINC__ <(.+)>", ln)
            if m:
                if m.group(1) not in seen:
                    seen.add(m.group(1))
                    lines.append("#include <%s>" % m.group(1))
                continue
            lines.append(ln)
        return "\n".join(lines)
    sys.exit("flatten: too many missing headers")


def is_const(t):
    t = t.get_canonical()
    while t.kind in (ci.TypeKind.CONSTANTARRAY, ci.TypeKind.INCOMPLETEARRAY, ci.TypeKind.VARIABLEARRAY):
        if t.is_const_qualified():
            return True
        t = t.get_array_element_type().get_canonical()
    return t.is_const_qualified()


def offs(text):
    """line -> byte offset table for (line, column) -> offset"""
    o, acc = [0], 0
    for ln in text.split(b"\n"):
        acc += len(ln) + 1
        o.append(acc)
    return o


def instance(text, name, hoist_ok=True, keep_shared=(), force=frozenset()):
    """returns the rewritten text and the number of instanced variables"""
    path = "pk_%s_flat.c" % name
    idx = ci.Index.create()
    res = subprocess.run(["gcc", "-print-file-name=include"], capture_output=True, text=True).stdout.strip()
    tu = idx.parse(path, args=["-std=gnu11", "-w", "-ferror-limit=0", "-isystem", res], unsaved_files=[(path, text)])
    errs = [d for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
    if errs:
        for d in errs[:20]:
            print("  C parse error:", d.location.line, d.spelling)
        sys.exit("the flattened source does not parse as C")
    data = text.encode()
    lo = offs(data)

    def off(loc):
        return lo[loc.line - 1] + loc.column - 1

    wraps = []                     # (start, end, prefix, suffix)
    consts = []                    # const statics without an initialiser (C: zero; C++: needs = {})
    cdecl = {}                     # const statics: usr -> declarations
    shared_refs = []               # (const table, variable): a shared table holds a variable's address
    vars_, order = {}, []          # usr -> record
    edits = []                     # (start, end, replacement)
    hoists = []                    # (function start offset, text)
    hoisted = {}                   # (start, end) of a function's static statement -> its variables
    entries = []                   # (offset, rank, k, symbol): where each variable's table entry goes

    def var_record(c, func):
        d = c.get_definition() or c
        usr = d.get_usr() or c.get_usr()
        if usr in vars_:
            return vars_[usr]
        rec = {"name": c.spelling, "k": len(order), "func": func, "cur": c}
        vars_[usr] = rec
        order.append(rec)
        return rec

    def redeclared(decls, nm):
        """C's tentative definitions: one declaration stays. A declaration ahead of the initialised definition
        becomes `extern` and the definition loses `static` (inside the pack's namespace)"""
        if len(decls) < 2:
            return
        inits = [d for d in decls if "=" in [t.spelling for t in d.get_tokens()]]
        keep = decls[0]
        for d in decls[1:]:
            s2, e2 = off(d.extent.start), off(d.extent.end)
            if inits and d is inits[0]:
                for d2 in (decls[0], d):
                    tk = next(t for t in d2.get_tokens() if t.spelling == "static")
                    edits.append((off(tk.extent.start), off(tk.extent.end), "extern" if d2 is decls[0] else "/*static*/"))
                continue
            while data[e2:e2 + 1] != b";":
                e2 += 1
            edits.append((s2, e2 + 1, "/* (%s: declared above) */" % nm))

    def ptr_to_void(t):
        t = t.get_canonical()
        return t.kind == ci.TypeKind.POINTER and t.get_pointee().get_canonical().kind == ci.TypeKind.VOID

    def visit(c, func, in_static_init):
        k = c.kind
        if k == K.UNEXPOSED_EXPR:                       # an implicit conversion from void * (C only): made explicit
            t = c.type.get_canonical()
            kids = list(c.get_children())
            if t.kind == ci.TypeKind.POINTER and not ptr_to_void(c.type) and len(kids) == 1 and ptr_to_void(kids[0].type):
                s0, e0 = off(c.extent.start), off(c.extent.end)
                if e0 > s0:
                    wraps.append((s0, e0, "((%s)(" % c.type.spelling, "))"))
        if k == K.FUNCTION_DECL:
            for ch in c.get_children():
                visit(ch, c if c.is_definition() else None, False)
            return
        if k == K.VAR_DECL:
            static = c.storage_class == ci.StorageClass.STATIC or (func is None and c.storage_class != ci.StorageClass.EXTERN)
            mutable = static and (not is_const(c.type) or c.spelling in force) and c.spelling not in keep_shared
            if mutable and c.storage_class != ci.StorageClass.EXTERN:
                rec = var_record(c, func)
                rec.setdefault("decls", []).append(c)
            if static and not mutable and c.storage_class != ci.StorageClass.EXTERN and is_const(c.type):
                has_init = any(ch.kind not in (K.TYPE_REF, K.INTEGER_LITERAL, K.DECL_REF_EXPR, K.UNEXPOSED_ATTR) or
                               ch.kind == K.INIT_LIST_EXPR for ch in c.get_children())
                usr = c.get_usr()
                cdecl.setdefault(usr, []).append(c)
            for ch in c.get_children():
                visit(ch, func, in_static_init or (True if mutable else (c.spelling if static else False)))
            return
        if k == K.DECL_REF_EXPR:
            r = c.referenced
            if r is not None and r.kind == K.VAR_DECL:
                d = r.get_definition() or r
                usr = d.get_usr() or r.get_usr()
                rec = vars_.get(usr)
                if rec is None and (r.storage_class == ci.StorageClass.STATIC or r.semantic_parent.kind == K.TRANSLATION_UNIT) \
                        and r.storage_class != ci.StorageClass.EXTERN and (not is_const(r.type) or r.spelling in force) \
                        and r.spelling not in keep_shared:
                    rec = var_record(r, None)
                if rec is not None:
                    if in_static_init:
                        rec.setdefault("init_refs", 0)
                        rec["init_refs"] += 1          # an address in a static initialiser: relocated at run time
                        if in_static_init is not True:
                            shared_refs.append((in_static_init, rec["name"]))
                    else:
                        s, e = off(c.extent.start), off(c.extent.end)
                        edits.append((s, e, "PKV(%d)" % rec["k"]))
            return
        for ch in c.get_children():
            visit(ch, func, in_static_init)

    for c in tu.cursor.get_children():
        if c.location.file and c.location.file.name == path:
            visit(c, None, False)

    # declarations: after each file-level one its table entry; a function's static goes up to file level
    tail = []
    for rec in order:
        decls = rec.get("decls", [])
        if not decls:
            sys.exit("no declaration found for %s" % rec["name"])
        k = rec["k"]
        nm = rec["name"]
        d0 = decls[0]
        if rec["func"] is not None or d0.semantic_parent.kind != K.TRANSLATION_UNIT:
            # hoist: cut the declaration statement out of the function, rename, put it before the function
            f = d0.semantic_parent
            while f.kind != K.FUNCTION_DECL:
                f = f.semantic_parent
            s = off(d0.extent.start)
            while s > 0 and data[s - 1:s] not in (b";", b"{", b"}", b"/"):   # the statement's start (`static T a, b;`)
                s -= 1
            while data[s:s + 1] in (b" ", b"\n", b"\t"):
                s += 1
            e = off(d0.extent.end)
            depth = 0
            while True:
                ch = data[e:e + 1]
                if ch in b"{([":
                    depth += 1
                elif ch in b"})]":
                    depth -= 1
                elif ch == b";" and depth == 0:
                    break
                e += 1
            new = "pkl_%s_%s_%d" % (f.spelling, nm, k)
            rec["sym"] = new
            key = (s, e)
            if key not in hoisted:
                hoisted[key] = {"f": f, "names": [], "stmt": data[s:e + 1].decode()}
                edits.append((s, e + 1, "/* (statics: hoisted) */"))
            hoisted[key]["names"].append((nm, new, k))
        else:
            rec["sym"] = nm
            redeclared(decls, nm)
            last = decls[0]
            e = off(last.extent.end)
            depth = 0
            while True:
                ch = data[e:e + 1]
                if ch in b"{([":
                    depth += 1
                elif ch in b"})]":
                    depth -= 1
                elif ch == b";" and depth == 0:
                    break
                e += 1
            entries.append((e + 1, 1, k, nm))
    for (s0, e0), h in hoisted.items():
        stmt, ents = h["stmt"], []
        for nm, new, k in h["names"]:
            stmt, n = re.subn(r"\b%s\b" % re.escape(nm), new, stmt, count=1)
            if not n:
                sys.exit("hoist: cannot rename %s" % nm)
            entries.append((off(h["f"].extent.start), 0, k, new))
        hoists.append((off(h["f"].extent.start), stmt + "\n"))
    for usr, ds in cdecl.items():
        if len(ds) > 1:
            redeclared(ds, ds[0].spelling)
        if not any("=" in [t.spelling for t in d.get_tokens()] for d in ds):
            consts.append(ds[0])
    kw = set("new delete class template this operator private public protected namespace typename virtual friend "
             "explicit mutable try catch throw using export and or not xor bool true false typeid nullptr".split())
    for t in tu.cursor.get_tokens():
        if t.kind == ci.TokenKind.IDENTIFIER and t.spelling in kw and t.spelling not in ("bool", "true", "false"):
            if t.location.file and t.location.file.name == path:
                edits.append((off(t.extent.start), off(t.extent.end), t.spelling + "_"))
    entries.sort()
    prev = None
    for pos, rank, k, sym in entries:
        txt = "\n" + entry(k, sym, prev) + "\n"
        if rank == 0:
            hoists.append((pos, txt))                 # (after the hoisted statement: list order at one offset)
        else:
            edits.append((pos, pos, txt))
        prev = k
    last_k = prev
    for c in consts:
        e = off(c.extent.end)
        edits.append((e, e, " = {}"))
    # apply: replacements (start, end, text); the casts wrap whatever is inside them
    ev = []                                           # (offset, order, kind, payload)
    for s, e, t in edits:
        ev.append((s, e, t))
    for s, t in hoists:
        ev.append((s, s, t))
    ev.sort(key=lambda x: (x[0], x[1]))               # (stable: a hoisted statement before its entries)
    pre, suf = {}, {}
    for s, e, a, b in wraps:
        pre.setdefault(s, []).append((e, a))
        suf.setdefault(e, []).append((s, b))
    out, pos, skip_to = [], 0, 0
    marks = sorted(set(list(pre) + list(suf)))

    def emit(upto):
        nonlocal pos
        for m in [m for m in marks if pos <= m <= upto]:
            if m < pos:
                continue
            out.append(data[pos:m].decode())
            pos = m
            for _, b in sorted(suf.get(m, []), key=lambda x: -x[0]):
                out.append(b)
            for _, a in sorted(pre.get(m, []), key=lambda x: -x[0]):
                out.append(a)
            suf.pop(m, None)
            pre.pop(m, None)
        out.append(data[pos:upto].decode())
        pos = upto

    for s, e, t in ev:
        if s < pos:
            if t.startswith("PKV("):
                continue                              # a reference inside a statement that was cut out
            sys.exit("overlapping edits at %d (%r)" % (s, t[:60]))
        emit(s)
        out.append(t)
        # wraps that start or end inside a replaced range are dropped
        for m in [m for m in marks if s < m < e]:
            pre.pop(m, None)
            suf.pop(m, None)
        pos = e
    emit(len(data))
    body = "".join(out)
    table = ["static const pk_var_t pk_vars[] = {"]
    for rec in order:
        table.append("    {pk_a_%d, pk_o_%d, sizeof(pk_t_%d)}," % (rec["k"], rec["k"], rec["k"]))
    table.append("};")
    table.append("enum : size_t { pk_block_size = PK_AL(pk_e_%d, 16) };" % last_k if order else
                 "enum : size_t { pk_block_size = 16 };")
    tabs = frozenset(t for t, _ in shared_refs)
    if tabs - force:                                  # a const table of pointers to variables: per instance too
        print("  per instance as well (they hold variables' addresses): %s" % ", ".join(sorted(tabs - force)))
        return instance(text, name, hoist_ok, keep_shared, force | tabs)
    return body, "\n".join(table), order


def entry(k, sym, prevk):
    prev = "pk_e_%d" % prevk if prevk is not None else "0"
    return ("typedef decltype(%s) pk_t_%d; static void *const pk_a_%d = (void *)&%s; "
            "enum : size_t { pk_o_%d = PK_AL(%s, alignof(pk_t_%d) > 8 ? alignof(pk_t_%d) : 8), pk_e_%d = pk_o_%d + sizeof(pk_t_%d) };"
            % (sym, k, k, sym, k, prev, k, k, k, k, k))


HEAD = r"""// GENERATED by tools/make_pack.py from tools/packs/%(name)s.py: do not edit (edit the sources or the spec and re-run).
// %(title)s
// %(licence)s
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include "armada_pack.h"
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wc99-designator"
#pragma clang diagnostic ignored "-Wreorder-init-list"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wwritable-strings"
#pragma clang diagnostic ignored "-Wc++11-narrowing"
#pragma clang diagnostic ignored "-Wparentheses"
#pragma clang diagnostic ignored "-Wshift-negative-value"
#pragma clang diagnostic ignored "-Wtautological-compare"
#pragma clang diagnostic ignored "-Wtype-limits"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#pragma clang diagnostic ignored "-Wchar-subscripts"
#pragma clang diagnostic ignored "-Wunused-value"
#pragma clang diagnostic ignored "-Wstring-plus-int"
#pragma clang diagnostic ignored "-Wconstant-conversion"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wmisleading-indentation"
#pragma clang diagnostic ignored "-Wself-assign"
#pragma clang diagnostic ignored "-Wignored-qualifiers"
#pragma clang diagnostic ignored "-Wextra"
#pragma clang diagnostic ignored "-Wall"
#endif
namespace pk_%(name)s {
static thread_local unsigned char *pk_tls;
struct pk_var_t { void *addr; size_t off, size; };
#define PK_AL(x, a) (((size_t)(x) + ((size_t)(a) - 1u)) & ~((size_t)(a) - 1u))
#define PKV(k) (*(pk_t_##k *)(pk_tls + pk_o_##k))
template <class T> static T *pk_tmp(T v) { static thread_local T x; x = v; return &x; }   // &(T){v} of C
"""

TAIL = r"""
#undef PKV
// ---- instances: a block per plugin instance; the variables' initial values; pointers between them moved ----
static unsigned char *pk_new_block()
{
    unsigned char *b = (unsigned char *)std::calloc(1, pk_block_size + 64);
    if (!b) return nullptr;
    const size_t nv = sizeof(pk_vars) / sizeof(pk_vars[0]);
    for (size_t i = 0; i < nv; i++)
        std::memcpy(b + pk_vars[i].off, pk_vars[i].addr, pk_vars[i].size);
    for (size_t i = 0; i < nv; i++) {                 // pointers into the shared variables -> this block
        if (pk_vars[i].size < sizeof(void *)) continue;
        for (size_t o = 0; o + sizeof(void *) <= pk_vars[i].size; o += sizeof(void *)) {
            uintptr_t v;
            std::memcpy(&v, b + pk_vars[i].off + o, sizeof v);
            if (!v) continue;
            for (size_t j = 0; j < nv; j++) {
                uintptr_t a = (uintptr_t)pk_vars[j].addr;
                if (v >= a && v < a + pk_vars[j].size) {
                    v = (uintptr_t)(b + pk_vars[j].off) + (v - a);
                    std::memcpy(b + pk_vars[i].off + o, &v, sizeof v);
                    break;
                }
            }
        }
    }
    return b;
}
struct pk_scope { unsigned char *old; explicit pk_scope(unsigned char *b) : old(pk_tls) { pk_tls = b; } ~pk_scope() { pk_tls = old; } };
"""


def main(names):
    os.makedirs(GEN, exist_ok=True)
    for name in names:
        spec = load_spec(name)
        with tempfile.TemporaryDirectory() as wd:
            flat = flatten(spec.TU, spec.INCDIRS, getattr(spec, "DEFINES", []), wd)
        ctors = re.findall(r"__attribute__\s*\(\(constructor\)\)\s*static\s+void\s+(\w+)\s*\(", flat)
        flat = re.sub(r"__attribute__\s*\(\(constructor\)\)\s*", "", flat)   # run per instance (the glue's init)
        flat = flat.replace("/*PK_CTORS*/", " ".join("%s();" % c for c in ctors))
        for a, b in getattr(spec, "PRE_FIXES", []):
            flat, n = re.subn(a, b, flat, flags=re.M | re.S)
            if not n:
                sys.exit("%s: PRE_FIX %r matched nothing" % (name, a))
        body, table, order = instance(flat, name, keep_shared=getattr(spec, "SHARED", ()))
        # the FM-1's flash, mapped at a fixed address on the device: here a page of zeros (an empty user slot)
        body, nx = re.subn(r"(static inline const uint8_t \*fm1_xip_ptr\(uint32_t off\) \{) return [^;]*;",
                           r"\1 (void)off; return armada_zero_page;", body)
        if "fm1_xip_ptr(" in body and not nx:
            sys.exit("%s: fm1_xip_ptr is there but was not replaced" % name)
        body = re.sub(r"\brestrict\b", "__restrict", body)
        body = re.sub(r"&\((u?int(?:8|16|32)_t)\)\{([^{};]*)\}", r"pk_tmp<\1>(\2)", body)
        for a, b in getattr(spec, "FIXES", []):
            body, n = re.subn(a, b, body, flags=re.M | re.S)
            if not n:
                sys.exit("%s: FIX %r matched nothing" % (name, a))
        out = HEAD % {"name": name, "title": spec.TITLE, "licence": spec.LICENCE}
        out += body + "\n" + table + TAIL + spec.CXX + "\n} // namespace pk_%s\n" % name
        dst = os.path.join(GEN, "pk_%s.cpp" % name)
        with open(dst, "w") as f:
            f.write(out)
        ninit = sum(1 for r in order if r.get("init_refs"))
        print("%s: %d instanced variables (%d hoisted from functions, %d addressed from static initialisers) -> %s (%d KB)"
              % (name, len(order), sum(1 for r in order if r["func"] is not None), ninit, os.path.relpath(dst, PLUGIN),
                 len(out) // 1024))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1:])
