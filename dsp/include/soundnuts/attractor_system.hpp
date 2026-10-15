// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// SoundNuts — chaotic-attractor modulation engine
// attractor_system.hpp — core interface for a dynamical-system mod source.
//
// JUCE-free, header-only, std-only. This is the contract every ported vector
// field implements and that the integrator / normalizer / mod engine consume.
//
// Status: INTERFACE STUB. Method bodies are intentionally unimplemented
// (marked SOUNDNUTS_STUB). The shapes here are load-bearing — they lock the
// data contract shared with pipeline/regime_schema.json.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace soundnuts {

// Max state dimension we support (Lorenz=3 ... a few 4-5D systems). Bump if a
// curated system needs more. Fixed-size keeps the hot path allocation-free.
inline constexpr int kMaxDim = 8;

using State = std::array<double, kMaxDim>;

// ---------------------------------------------------------------------------
// Params — the live, automatable parameters of a system.
//
// `bifurcation` is THE macro: a single normalized [0,1] knob that the host
// automates. It is mapped onto the system's native parameter range (see
// AttractorSystem::bifurcationRange) and is the axis the RegimeMap is built over.
// `extra` holds any additional native parameters held constant per preset.
// ---------------------------------------------------------------------------
struct Params {
    double bifurcation = 0.5;              // [0,1] macro -> native range
    std::array<double, kMaxDim> extra{};   // secondary native params (preset-fixed)
};

// ---------------------------------------------------------------------------
// Regime classification — mirrors pipeline/sweep.py output. One label per grid
// point of the bifurcation sweep.
// ---------------------------------------------------------------------------
enum class Regime : std::uint8_t {
    FixedPoint = 0,   // lambda < 0, settles            (gray  / dead)
    LimitCycle = 1,   // lambda ~ 0, bounded, periodic  (green / tempo-syncable)
    Chaos      = 2,   // lambda > 0, bounded            (amber)
    Divergent  = 3,   // unbounded                      (red   / riser zone)
};

// One sampled point of the bifurcation sweep, as emitted offline by the pipeline.
struct RegimePoint {
    double bifurcation;                    // [0,1] macro position
    Regime regime;
    double largestLyapunov;                // Benettin/QR estimate
    double period;                         // dominant period (seconds at unit rate); <0 if undefined
    // Per-coordinate affine normalization to map the raw trajectory into [-1,1].
    // norm: out[i] = (s[i] - center[i]) * scale[i]
    std::array<double, kMaxDim> center{};
    std::array<double, kMaxDim> scale{};
};

// The full precomputed phase diagram for one system, loaded from JSON at startup.
struct RegimeMap {
    std::vector<RegimePoint> points;       // sorted ascending by bifurcation

    // Look up (interpolated) normalization + classification at macro value b in
    // [0,1]: linear-interp center/scale/lyapunov/period, nearest-neighbor regime.
    // Empty map (not yet loaded) -> identity normalization so the engine still runs.
    [[nodiscard]] RegimePoint sampleAt(double b) const {
        RegimePoint r{};
        r.bifurcation = b;
        if (points.empty()) {
            r.regime = Regime::LimitCycle;
            r.largestLyapunov = 0.0;
            r.period = -1.0;
            r.center.fill(0.0);
            r.scale.fill(1.0);                 // identity: out = raw
            return r;
        }
        if (b <= points.front().bifurcation) return points.front();
        if (b >= points.back().bifurcation)  return points.back();

        std::size_t hi = 1;
        while (hi < points.size() && points[hi].bifurcation < b) ++hi;
        const RegimePoint& lo = points[hi - 1];
        const RegimePoint& up = points[hi];
        const double span = up.bifurcation - lo.bifurcation;
        const double t = span > 0.0 ? (b - lo.bifurcation) / span : 0.0;

        r.regime = (t < 0.5) ? lo.regime : up.regime;     // nearest-neighbor
        r.largestLyapunov = lo.largestLyapunov + t * (up.largestLyapunov - lo.largestLyapunov);
        r.period = (lo.period < 0.0 || up.period < 0.0)
                       ? -1.0 : lo.period + t * (up.period - lo.period);
        for (int i = 0; i < kMaxDim; ++i) {
            r.center[i] = lo.center[i] + t * (up.center[i] - lo.center[i]);
            r.scale[i]  = lo.scale[i]  + t * (up.scale[i]  - lo.scale[i]);
        }
        return r;
    }
};

// ---------------------------------------------------------------------------
// AnalyticSolution — OPTIONAL closed-form capability a system MAY expose.
//
// When present AND valid at the current macro value, the mod engine evaluates
// this directly instead of stepping the integrator. Wins over numerics:
//   * sample-exact, zero phase drift (perfectly periodic in a limit cycle),
//   * random access — eval(t) at any t, so DAW-timeline seeking is exact
//     (host jumps the playhead -> we land on the right phase, no re-integration),
//   * exact period known in closed form -> exact tempo-sync, no measurement.
//
// Only special systems/regimes have one (harmonic osc; pendulum & undamped
// Duffing via Jacobi elliptic functions; r=4 logistic via sin^2). Lorenz et al.
// have none -> analytic() returns nullptr and we always integrate.
// ---------------------------------------------------------------------------
struct AnalyticSolution {
    virtual ~AnalyticSolution() = default;

    // Is the closed form valid at this macro value? e.g. pendulum libration only,
    // undamped/unforced Duffing only. Outside this -> engine falls back to RK4.
    [[nodiscard]] virtual bool validAt(const Params& p) const = 0;

    // Exact fundamental period (seconds at unit rate), or <0 if aperiodic.
    [[nodiscard]] virtual double period(const Params& p) const = 0;

    // Evaluate state directly at time/phase t. No stepping, no history.
    virtual void eval(double t, const Params& p, State& s) const = 0;
};

// ---------------------------------------------------------------------------
// AttractorSystem — the only per-system code is rhs(). Everything else is data.
// Implementations live in systems/ (one header each: lorenz.hpp, rossler.hpp...).
// ---------------------------------------------------------------------------
struct AttractorSystem {
    virtual ~AttractorSystem() = default;

    // Identity / metadata ----------------------------------------------------
    [[nodiscard]] virtual std::string id()   const = 0;  // "lorenz"
    [[nodiscard]] virtual std::string name() const = 0;  // "Lorenz"
    [[nodiscard]] virtual int dim()          const = 0;  // active state dimension <= kMaxDim

    // Vector field: ds/dt = f(s; p). The ONLY math each system must supply.
    // `s` and `ds` are length dim(); higher indices ignored.
    virtual void rhs(const State& s, const Params& p, State& ds) const = 0;

    // Native range the [0,1] `bifurcation` macro maps onto, e.g. Lorenz rho.
    // STUB returns a placeholder; real systems override.
    [[nodiscard]] virtual std::pair<double, double> bifurcationRange() const {
        return {0.0, 1.0};
    }

    // On-attractor initial condition (from dysts) so we start clean, no transient.
    [[nodiscard]] virtual State onAttractorIC() const = 0;

    // Precomputed phase diagram for this system (loaded from pipeline JSON).
    [[nodiscard]] virtual const RegimeMap& regimeMap() const = 0;

    // Optional closed-form solver. nullptr (default) if none known for this
    // system. Owned by the system; lifetime == the system's.
    [[nodiscard]] virtual const AnalyticSolution* analytic() const { return nullptr; }
};

}  // namespace soundnuts
