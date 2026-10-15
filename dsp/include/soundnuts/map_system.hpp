// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// SoundNuts — map_system.hpp
// Discrete-time system: x_{n+1} = f(x_n; p). One iterate per clock tick -> a
// natural MIDI *event* source (vs flows, which are natural continuous CV).
//
// Shares Params / State / Regime / RegimeMap / AnalyticSolution with
// AttractorSystem. Key difference: no integrator — the iteration IS the dynamics.
// The regime map here is the classic BIFURCATION DIAGRAM (period-1 -> 2 -> 4 ->
// chaos, periodic windows), and the regime reads directly as step-pattern length.
//
// Status: INTERFACE STUB (step() is real in concrete maps; see systems/logistic.hpp).

#pragma once

#include <string>
#include <utility>

#include "attractor_system.hpp"  // State, Params, Regime, RegimeMap, AnalyticSolution

namespace soundnuts {

struct MapSystem {
    virtual ~MapSystem() = default;

    [[nodiscard]] virtual std::string id()   const = 0;
    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual int dim()          const = 0;

    // One iteration: next = f(s; p). The only per-map code.
    virtual void step(const State& s, const Params& p, State& next) const = 0;

    // Native range the [0,1] macro maps onto (e.g. logistic r).
    [[nodiscard]] virtual std::pair<double, double> bifurcationRange() const {
        return {0.0, 1.0};
    }

    [[nodiscard]] virtual State seedIC() const = 0;                 // x_0
    [[nodiscard]] virtual const RegimeMap& regimeMap() const = 0;   // = bifurcation diagram

    // Optional closed form. For maps, AnalyticSolution::eval interprets its `t`
    // as the (real-extended) step index n. nullptr if none (default).
    [[nodiscard]] virtual const AnalyticSolution* analytic() const { return nullptr; }
};

}  // namespace soundnuts
