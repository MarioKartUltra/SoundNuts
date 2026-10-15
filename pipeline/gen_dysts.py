#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Abraham Joseph Okayli Masaryk
"""Codegen SoundNuts C++ attractor systems from the dysts source.

Parses dysts/flows.py (static `_rhs` methods) + chaotic_attractors.json, translates
each vector field to C++, VALIDATES the translation point-wise against the original
Python (ast.unparse), integrates (numpy RK4) to bake a phase-diagram normalization
(center/scale — no chaos required), and emits:

    dsp/include/soundnuts/generated_systems.hpp

Usage:  python3 pipeline/gen_dysts.py --dysts /path/to/dysts_src
Skips (and logs) anything that isn't a clean autonomous ODE flow we can translate.
"""
import argparse, ast, json, math, os, random

CURATED = {"lorenz", "rossler", "pendulum"}          # keep the hand-tuned ones
KMAXDIM = 8
ALLOWED_CALLS = {
    "sin","cos","tan","exp","log","log10","sqrt","cbrt","sinh","cosh","tanh",
    "arcsin","arccos","arctan","arctan2","sign","abs","fabs","floor","ceil","power",
}
CPP_FUNC = {"arcsin":"asin","arccos":"acos","arctan":"atan","arctan2":"atan2",
            "abs":"fabs","sign":"sgn","power":"__pow__"}

class Unsupported(Exception): pass

# ---- AST -> expression string (mode: 'cpp' or 'py') --------------------------
def emit(node, statevars, params, mode):
    def e(n): return emit(n, statevars, params, mode)
    if isinstance(node, ast.Constant):
        if isinstance(node.value, bool) or not isinstance(node.value, (int, float)):
            raise Unsupported("non-numeric constant")
        return repr(float(node.value))
    if isinstance(node, ast.Name):
        if node.id in statevars: return node.id
        if node.id in params:    return repr(float(params[node.id]))
        if node.id == "t":       raise Unsupported("t (non-autonomous)")
        raise Unsupported(f"name {node.id}")
    if isinstance(node, ast.Attribute):        # np.pi / math.pi
        if node.attr == "pi": return "M_PI" if mode == "cpp" else "math.pi"
        raise Unsupported(f"attr {node.attr}")
    if isinstance(node, ast.UnaryOp):
        if isinstance(node.op, ast.USub): return f"(-({e(node.operand)}))"
        if isinstance(node.op, ast.UAdd): return e(node.operand)
        raise Unsupported("unaryop")
    if isinstance(node, ast.BinOp):
        a, b = e(node.left), e(node.right)
        if isinstance(node.op, ast.Add):  return f"({a} + {b})"
        if isinstance(node.op, ast.Sub):  return f"({a} - {b})"
        if isinstance(node.op, ast.Mult): return f"({a} * {b})"
        if isinstance(node.op, ast.Div):  return f"({a} / {b})"
        if isinstance(node.op, ast.Pow):
            return f"std::pow({a}, {b})" if mode == "cpp" else f"(({a}) ** ({b}))"
        raise Unsupported("binop")
    if isinstance(node, ast.Call):
        if node.args and node.keywords: raise Unsupported("kwargs")
        fn = node.func.attr if isinstance(node.func, ast.Attribute) else \
             (node.func.id if isinstance(node.func, ast.Name) else None)
        if fn not in ALLOWED_CALLS: raise Unsupported(f"call {fn}")
        args = [e(a) for a in node.args]
        if mode == "cpp":
            cf = CPP_FUNC.get(fn, fn)
            if cf == "__pow__": return f"std::pow({args[0]}, {args[1]})"
            if cf == "sgn":     return f"sgn({args[0]})"      # our helper, not std::
            return f"std::{cf}(" + ", ".join(args) + ")"
        else:
            pf = {"arcsin":"asin","arccos":"acos","arctan":"atan","arctan2":"atan2",
                  "abs":"fabs","sign":"__sign__","power":"__pow__"}.get(fn, fn)
            if pf == "__pow__":  return f"(({args[0]}) ** ({args[1]}))"
            if pf == "__sign__": return f"__sign__({args[0]})"
            return f"math.{pf}(" + ", ".join(args) + ")"
    raise Unsupported(type(node).__name__)

def translate_rhs(fn, statevars, params, mode):
    """Return list of dim expression strings (one per output coordinate)."""
    assigns = {}
    ret = None
    for stmt in fn.body:
        if isinstance(stmt, ast.Assign) and len(stmt.targets) == 1 and isinstance(stmt.targets[0], ast.Name):
            assigns[stmt.targets[0].id] = stmt.value
        elif isinstance(stmt, ast.Return):
            ret = stmt.value
        elif isinstance(stmt, ast.Expr):
            continue
        else:
            raise Unsupported("stmt " + type(stmt).__name__)
    if ret is None or not isinstance(ret, ast.Tuple):
        raise Unsupported("return not a tuple")
    out = []
    for elt in ret.elts:
        node = assigns.get(elt.id) if isinstance(elt, ast.Name) and elt.id in assigns else elt
        out.append(emit(node, statevars, params, mode))
    return out

# ---- numeric helpers for validation + normalization -------------------------
def make_py_callable(exprs, statevars):
    ns = {"math": math, "M_PI": math.pi, "__sign__": lambda x: (x > 0) - (x < 0)}
    body = "def _f(" + ", ".join(statevars) + "):\n    return (" + ", ".join(exprs) + ",)\n"
    exec(body, ns)
    return ns["_f"]

def rk4_box(f, ic, dt, nsteps, transient):
    s = list(ic); dim = len(ic)
    lo = [float("inf")] * dim; hi = [float("-inf")] * dim
    for n in range(nsteps):
        k1 = f(*s)
        k2 = f(*[s[i] + 0.5*dt*k1[i] for i in range(dim)])
        k3 = f(*[s[i] + 0.5*dt*k2[i] for i in range(dim)])
        k4 = f(*[s[i] + dt*k3[i] for i in range(dim)])
        s = [s[i] + (dt/6.0)*(k1[i] + 2*k2[i] + 2*k3[i] + k4[i]) for i in range(dim)]
        for v in s:
            if not math.isfinite(v) or abs(v) > 1e6: return None
        if n >= transient:
            for i in range(dim):
                lo[i] = min(lo[i], s[i]); hi[i] = max(hi[i], s[i])
    return lo, hi

# ---- main -------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dysts", required=True)
    args = ap.parse_args()
    flows = os.path.join(args.dysts, "dysts", "flows.py")
    dataf = os.path.join(args.dysts, "dysts", "data", "chaotic_attractors.json")
    meta = json.load(open(dataf))
    tree = ast.parse(open(flows).read())

    here = os.path.dirname(os.path.abspath(__file__))
    dst = os.path.join(here, "..", "dsp", "include", "soundnuts", "generated_systems.hpp")

    ported, skipped = [], []
    rng = random.Random(12345)

    classes = [n for n in ast.walk(tree) if isinstance(n, ast.ClassDef)]
    cpp_classes = []

    for cls in classes:
        cid = cls.name.lower()
        if cid in CURATED: continue
        if cls.name not in meta:
            skipped.append((cls.name, "no json metadata")); continue
        # find static _rhs
        rhs = next((f for f in cls.body if isinstance(f, ast.FunctionDef) and f.name == "_rhs"), None)
        if rhs is None:
            skipped.append((cls.name, "no static _rhs (rhs(self)/N-D)")); continue
        argnames = [a.arg for a in rhs.args.args]
        if rhs.args.vararg or "t" not in argnames:
            skipped.append((cls.name, "no t / *args")); continue
        ti = argnames.index("t")
        statevars = argnames[:ti]; paramnames = argnames[ti+1:]
        dim = len(statevars)
        info = meta[cls.name]
        params = info.get("parameters", {})
        ic = info.get("initial_conditions", [])
        try:
            if dim == 0 or dim > KMAXDIM: raise Unsupported(f"dim {dim}")
            if len(ic) != dim: raise Unsupported("IC/dim mismatch")
            for pn in paramnames:
                if pn not in params: raise Unsupported(f"param {pn} missing")
                if not isinstance(params[pn], (int, float)): raise Unsupported(f"param {pn} non-scalar")
            cpp_exprs = translate_rhs(rhs, statevars, params, "cpp")
            py_exprs  = translate_rhs(rhs, statevars, params, "py")
            # validate translation vs original python (unparse), point-wise
            orig_src = "def _g(" + ", ".join(argnames) + "):\n" + \
                       "\n".join("    " + l for l in ast.unparse(ast.Module(rhs.body, [])).splitlines())
            gns = {"math": math, "np": math, "M_PI": math.pi}
            # give np the math funcs it needs
            import types
            npmod = types.SimpleNamespace(**{k: getattr(math, k, None) for k in
                     ("sin","cos","tan","exp","log","log10","sqrt","cbrt","sinh","cosh","tanh",
                      "arcsin","arccos","arctan","fabs","floor","ceil")})
            npmod.abs = abs; npmod.sign = lambda x: (x>0)-(x<0); npmod.power = pow
            npmod.arctan2 = math.atan2; npmod.pi = math.pi
            npmod.arcsin = math.asin; npmod.arccos = math.acos; npmod.arctan = math.atan
            gns["np"] = npmod
            exec(orig_src, gns); g = gns["_g"]
            ftr = make_py_callable(py_exprs, statevars)
            ok = True
            for _ in range(24):
                st = [rng.uniform(-1.5, 1.5) for _ in range(dim)]
                pv = [params[p] for p in paramnames]
                a = g(*st, 0.0, *pv); b = ftr(*st)
                for i in range(dim):
                    if not (math.isfinite(a[i]) and math.isfinite(b[i])) or abs(a[i]-b[i]) > 1e-7*(1+abs(a[i])):
                        ok = False; break
                if not ok: break
            if not ok: raise Unsupported("translation mismatch")
            # normalization via integration of the validated translated field
            dt = info.get("dt") or 0.01
            period = info.get("period") or 0.0
            nsteps = min(12000, max(4000, int(6*period/dt))) if period > 0 else 8000
            box = rk4_box(ftr, ic, dt, nsteps, nsteps//4)
            if box is None: raise Unsupported("unbounded")
            lo, hi = box
            center = [round((lo[i]+hi[i])/2.0, 6) for i in range(dim)]
            scale  = [round(2.0/(hi[i]-lo[i]), 6) if (hi[i]-lo[i]) > 1e-9 else 0.0 for i in range(dim)]
            cpp_classes.append(dict(cls=cls.name, cid=cid, dim=dim, statevars=statevars,
                                    exprs=cpp_exprs, ic=[round(float(v),6) for v in ic],
                                    center=center, scale=scale))
            ported.append(cls.name)
        except Unsupported as ex:
            skipped.append((cls.name, str(ex)))
        except Exception as ex:
            skipped.append((cls.name, "err: " + str(ex)[:60]))

    # ---- emit header --------------------------------------------------------
    def padc(v): return (v + [0.0]*KMAXDIM)[:KMAXDIM]
    def pads(v): return (v + [0.0]*KMAXDIM)[:KMAXDIM]
    L = []
    L.append("// SPDX-License-Identifier: AGPL-3.0-or-later")
    L.append("// Copyright (C) 2026 Abraham Joseph Okayli Masaryk")
    L.append("// SoundNuts — generated_systems.hpp  (GENERATED by pipeline/gen_dysts.py)")
    L.append("// Equations from dysts (https://github.com/williamgilpin/dysts), Apache-2.0; see NOTICE.")
    L.append(f"// {len(ported)} attractor systems codegen'd from dysts. Do not edit by hand.")
    L.append("#pragma once")
    L.append("#include <cmath>")
    L.append('#include "attractor_system.hpp"')
    L.append("namespace soundnuts { namespace gen {")
    L.append("inline double sgn(double x){ return (x>0.0)-(x<0.0); }")
    L.append("")
    for c in cpp_classes:
        decl = " ".join(f"const double {v} = s[{i}];" for i, v in enumerate(c["statevars"]))
        cen = "{" + ", ".join(repr(float(x)) for x in padc(c["center"])) + "}"
        scl = "{" + ", ".join(repr(float(x)) for x in pads(c["scale"])) + "}"
        icv = "{" + ", ".join(repr(float(x)) for x in (c["ic"] + [0.0]*KMAXDIM)[:KMAXDIM]) + "}"
        L.append(f"class {c['cls']} final : public AttractorSystem {{")
        L.append(f"public:")
        L.append(f'  std::string id() const override {{ return "{c["cid"]}"; }}')
        L.append(f'  std::string name() const override {{ return "{c["cls"]}"; }}')
        L.append(f"  int dim() const override {{ return {c['dim']}; }}")
        L.append(f"  void rhs(const State& s, const Params&, State& ds) const override {{")
        L.append(f"    {decl}")
        for i, ex in enumerate(c["exprs"]):
            L.append(f"    ds[{i}] = {ex};")
        L.append(f"  }}")
        L.append(f"  State onAttractorIC() const override {{ return State{icv}; }}")
        L.append(f"  const RegimeMap& regimeMap() const override {{ return regime_; }}")
        L.append(f"  RegimeMap& mutableRegimeMap() {{ return regime_; }}")
        L.append(f"private:")
        L.append(f"  RegimeMap regime_ {{ {{ RegimePoint{{ 0.5, Regime::LimitCycle, 0.0, -1.0, {cen}, {scl} }} }} }};")
        L.append(f"}};")
        L.append("")
    L.append("template <class F> inline void forEachGenerated(F&& add) {")
    for c in cpp_classes:
        L.append(f"  add(std::make_unique<{c['cls']}>());")
    L.append("}")
    L.append(f"inline constexpr int kNumGeneratedSystems = {len(cpp_classes)};")
    L.append("} } // namespace soundnuts::gen")
    L.append("")
    open(os.path.abspath(dst), "w").write("\n".join(L))

    print(f"PORTED {len(ported)} systems; SKIPPED {len(skipped)}")
    print("wrote", os.path.relpath(dst, os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    from collections import Counter
    reasons = Counter(r.split(" ")[0] for _, r in skipped)
    print("skip reasons:", dict(reasons))
    for nm, r in skipped[:40]:
        print(f"  skip {nm}: {r}")

if __name__ == "__main__":
    main()
