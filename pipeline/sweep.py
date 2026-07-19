#!/usr/bin/env python3
"""SoundNuts offline regime-map pipeline (FLOWS).

For each system, sweep the bifurcation macro b in [0,1]; at each point compute:
  * largest Lyapunov exponent  (Benettin two-trajectory, Jacobian-free)
  * boundedness
  * dominant period            (autocorrelation of coord 0)
  * per-coordinate affine normalization into [-1,1]
then classify the regime and emit JSON per pipeline/regime_schema.json, consumed
by dsp/ (RegimeMap / RegimePoint in attractor_system.hpp).

CONVENTIONS — must match the C++ side or the plugin reads the wrong terrain:
  * macro b in [0,1] maps LINEARLY onto each system's native bifurcation range
    (see SYSTEMS), identical to AttractorSystem::bifurcationRange() + the per-
    system mapBifurcation(). Lorenz: rho in [0,90]. Pendulum: theta0 in [0,pi].
  * period is in native time units (== "seconds at unit rate"; the plugin Rate
    knob rescales it).
  * for the pendulum the macro acts on the INITIAL CONDITION (amplitude), not on
    a vector-field parameter — make(b) returns (rhs, ic) so both cases are handled.

Maps (logistic / sequencer) are PARKED — this file is flows only.

Deps: numpy. dysts is the offline source for additional vector fields / ICs when
extending SYSTEMS, but is NOT required to run this.

Usage:
  python3 sweep.py                      # all systems, default grid -> out/*.json
  python3 sweep.py --grid 24 --only lorenz,pendulum
"""

from __future__ import annotations

import argparse
import json
import os
from dataclasses import dataclass, field
from typing import Callable, Optional, Tuple

try:
    import numpy as np
except ImportError:  # pragma: no cover
    raise SystemExit("sweep.py needs numpy:  pip install numpy")


# --------------------------------------------------------------------------- #
# Config
# --------------------------------------------------------------------------- #
@dataclass
class Thresholds:
    lle_chaos: float = 0.01    # 1/native-time; largest LE above this -> chaos
    fixed_var: float = 1e-6    # post-transient state variance below -> fixed point
    period_corr: float = 0.30  # autocorr peak below this -> no clear period (-1)
    bound_abs: float = 1e3     # |state| above this (or non-finite) -> divergent


@dataclass
class IntegCfg:
    dt: float                  # RK4 step (native time)
    transient_t: float         # time discarded before measuring
    measure_t: float           # time used for LLE / period / normalization
    renorm_t: float = 0.5      # Benettin renormalization interval (native time)
    sample_every: int = 2      # subsample factor for the stored trajectory


@dataclass
class FlowSystem:
    id: str
    dim: int
    bifurcation_param: str
    native_range: Tuple[float, float]
    make: Callable[[float], Tuple[Callable, "np.ndarray"]]  # b -> (rhs(y)->dy, ic)
    integ: IntegCfg
    thr: Thresholds = field(default_factory=Thresholds)
    # Integrable/conservative -> largest LE is analytically 0. The two-trajectory
    # estimator misreads such systems as weakly chaotic (it picks up shear: nearby
    # orbits dephase linearly, a finite-time bias ~ log(T)/T, NOT exponential).
    # When True we record LE=0 and classify by boundedness/variance only.
    integrable: bool = False


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


# --------------------------------------------------------------------------- #
# Numerics
# --------------------------------------------------------------------------- #
def rk4_step(rhs: Callable, y: "np.ndarray", dt: float) -> "np.ndarray":
    k1 = rhs(y)
    k2 = rhs(y + 0.5 * dt * k1)
    k3 = rhs(y + 0.5 * dt * k2)
    k4 = rhs(y + dt * k3)
    return y + (dt / 6.0) * (k1 + 2.0 * k2 + 2.0 * k3 + k4)


def _diverged(y: "np.ndarray", bound: float) -> bool:
    return (not np.all(np.isfinite(y))) or float(np.max(np.abs(y))) > bound


def integrate(rhs, y, dt, n_steps, bound, sample_every=1, collect=False):
    """Advance n_steps. Returns (final_state, bounded, samples|None)."""
    samples = [] if collect else None
    for i in range(n_steps):
        y = rk4_step(rhs, y, dt)
        if _diverged(y, bound):
            return y, False, (np.array(samples) if collect and samples else None)
        if collect and (i % sample_every == 0):
            samples.append(y.copy())
    return y, True, (np.array(samples) if collect else None)


def largest_lyapunov(rhs, y0, dt, n_steps, renorm_every, bound, d0=1e-9):
    """Benettin two-trajectory estimate of the largest Lyapunov exponent."""
    rng = np.random.default_rng(0xC0FFEE)            # fixed seed -> reproducible
    y = y0.copy()
    pert = rng.standard_normal(y.shape)
    pert /= np.linalg.norm(pert)
    yp = y + d0 * pert
    acc, cnt = 0.0, 0
    for i in range(n_steps):
        y = rk4_step(rhs, y, dt)
        yp = rk4_step(rhs, yp, dt)
        if _diverged(y, bound):
            break
        if (i + 1) % renorm_every == 0:
            diff = yp - y
            d = float(np.linalg.norm(diff))
            if d <= 0.0 or not np.isfinite(d):
                break
            acc += np.log(d / d0)
            cnt += 1
            yp = y + (d0 / d) * diff                 # pull back to distance d0
    if cnt == 0:
        return float("nan")
    return acc / (cnt * renorm_every * dt)


def dominant_period(traj, dt, sample_every, corr_thresh):
    """First strong autocorrelation peak of coord 0 -> (period|None, strength)."""
    x = traj[:, 0].astype(float)
    x = x - x.mean()
    if x.std() < 1e-12:
        return None, 0.0
    ac = np.correlate(x, x, mode="full")[len(x) - 1:]
    ac = ac / ac[0]
    zc = next((i for i in range(1, len(ac)) if ac[i - 1] > 0 >= ac[i]), None)
    if zc is None:
        return None, 0.0
    k = int(np.argmax(ac[zc:])) + zc
    strength = float(ac[k])
    if strength < corr_thresh:
        return None, strength
    return k * sample_every * dt, strength


def normalization(traj):
    """Per-coordinate affine map into [-1,1] via robust (0.5/99.5) percentiles."""
    lo = np.percentile(traj, 0.5, axis=0)
    hi = np.percentile(traj, 99.5, axis=0)
    center = 0.5 * (hi + lo)
    half = 0.5 * (hi - lo)
    half_safe = np.where(half > 1e-12, half, 1.0)        # avoid 1/0 on flat coords
    scale = np.where(half > 1e-12, 1.0 / half_safe, 0.0)
    return center, scale


def classify(bounded, lle, var, thr) -> str:
    if not bounded:
        return "divergent"
    if var < thr.fixed_var:
        return "fixed_point"
    if np.isfinite(lle) and lle > thr.lle_chaos:
        return "chaos"
    return "limit_cycle"                              # periodic / quasiperiodic


# --------------------------------------------------------------------------- #
# Sweep
# --------------------------------------------------------------------------- #
def sweep_point(sys: FlowSystem, b: float) -> dict:
    rhs, ic = sys.make(b)
    g = sys.integ
    bound = sys.thr.bound_abs
    n_trans = int(g.transient_t / g.dt)
    n_meas = int(g.measure_t / g.dt)
    renorm_every = max(1, int(g.renorm_t / g.dt))

    y, ok, _ = integrate(rhs, ic.copy(), g.dt, n_trans, bound)
    if not ok:
        return _point(b, "divergent", float("nan"), -1.0,
                      np.zeros(sys.dim), np.zeros(sys.dim))

    y_start = y.copy()
    _, ok, traj = integrate(rhs, y, g.dt, n_meas, bound,
                            sample_every=g.sample_every, collect=True)
    if not ok or traj is None or len(traj) < 8:
        return _point(b, "divergent", float("nan"), -1.0,
                      np.zeros(sys.dim), np.zeros(sys.dim))

    period, _ = dominant_period(traj, g.dt, g.sample_every, sys.thr.period_corr)
    var = float(np.mean(np.var(traj, axis=0)))
    center, scale = normalization(traj)

    if sys.integrable:
        lle = 0.0                                        # analytic; estimator unreliable here
        regime = "fixed_point" if var < sys.thr.fixed_var else "limit_cycle"
    else:
        lle = largest_lyapunov(rhs, y_start, g.dt, n_meas, renorm_every, bound)
        regime = classify(True, lle, var, sys.thr)

    return _point(b, regime, lle, -1.0 if period is None else period, center, scale)


def _point(b, regime, lle, period, center, scale) -> dict:
    return {
        "bifurcation": round(b, 6),
        "regime": regime,
        "largest_lyapunov": None if not np.isfinite(lle) else round(float(lle), 6),
        "period": round(float(period), 6),
        "center": [round(float(c), 6) for c in center],
        "scale": [round(float(s), 6) for s in scale],
    }


def sweep_system(sys: FlowSystem, grid: int) -> dict:
    points = [sweep_point(sys, i / (grid - 1)) for i in range(grid)]
    _, ref_ic = sys.make(0.5)
    return {
        "id": sys.id,
        "dim": sys.dim,
        "bifurcation_param": sys.bifurcation_param,
        "bifurcation_range": [sys.native_range[0], sys.native_range[1]],
        "on_attractor_ic": [round(float(v), 6) for v in ref_ic],   # at ref b=0.5
        "control_rate_hz": round(1.0 / sys.integ.dt, 3),
        "points": points,
    }


# --------------------------------------------------------------------------- #
# Curated starter systems (mappings MUST match dsp/.../systems/*.hpp)
# --------------------------------------------------------------------------- #
def _lorenz(b):
    rho = lerp(0.0, 90.0, b)                          # matches lorenz.hpp [0,90]
    sigma, beta = 10.0, 8.0 / 3.0

    def rhs(y):
        x, yy, z = y
        return np.array([sigma * (yy - x), x * (rho - z) - yy, x * yy - beta * z])

    return rhs, np.array([-9.7869288, -15.03852, 20.533978])


def _rossler(b):
    a, bb = 0.2, 0.2
    c = lerp(2.0, 18.0, b)                            # c-sweep: period-doubling -> chaos

    def rhs(y):
        x, yy, z = y
        return np.array([-yy - z, x + a * yy, bb + z * (x - c)])

    return rhs, np.array([-5.0, 5.0, 0.2])


def _pendulum(b):
    # macro acts on the INITIAL CONDITION (amplitude), matches pendulum.hpp.
    omega0 = 2.0 * np.pi
    theta0 = b * np.pi                                # [0,pi) libration

    def rhs(y):
        th, om = y
        return np.array([om, -omega0 * omega0 * np.sin(th)])

    return rhs, np.array([theta0, 0.0])


SYSTEMS = {
    "lorenz": FlowSystem(
        "lorenz", 3, "rho", (0.0, 90.0), _lorenz,
        IntegCfg(dt=0.01, transient_t=20.0, measure_t=80.0, renorm_t=0.5),
    ),
    "rossler": FlowSystem(
        "rossler", 3, "c", (2.0, 18.0), _rossler,
        IntegCfg(dt=0.02, transient_t=100.0, measure_t=300.0, renorm_t=1.0),
    ),
    "pendulum": FlowSystem(
        "pendulum", 2, "theta0", (0.0, float(np.pi)), _pendulum,
        IntegCfg(dt=0.002, transient_t=0.0, measure_t=12.0, renorm_t=0.2,
                 sample_every=1),
        integrable=True,                                 # conservative -> LE == 0
    ),
}


# --------------------------------------------------------------------------- #
# CLI
# --------------------------------------------------------------------------- #
def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="SoundNuts flow regime-map sweep")
    ap.add_argument("--grid", type=int, default=48, help="macro sample points")
    ap.add_argument("--only", type=str, default="", help="comma list of system ids")
    ap.add_argument("--out", type=str, default=os.path.join(here, "out"))
    args = ap.parse_args()

    ids = [s for s in args.only.split(",") if s] or list(SYSTEMS)
    os.makedirs(args.out, exist_ok=True)

    for sid in ids:
        if sid not in SYSTEMS:
            print(f"  ! unknown system '{sid}' (have: {', '.join(SYSTEMS)})")
            continue
        data = sweep_system(SYSTEMS[sid], args.grid)
        path = os.path.join(args.out, f"{sid}.json")
        with open(path, "w") as fh:
            json.dump(data, fh, indent=2)
        tally = {}
        for p in data["points"]:
            tally[p["regime"]] = tally.get(p["regime"], 0) + 1
        summary = "  ".join(f"{k}:{v}" for k, v in sorted(tally.items()))
        print(f"  {sid:10s} grid={args.grid}  ->  {summary}   [{path}]")


if __name__ == "__main__":
    main()
