// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// SoundNuts — systems/logistic.hpp
// Logistic map x_{n+1} = r x_n (1 - x_n). The reference MAP (sequencer) system.
//
// Macro -> r in [2.5, 4]. Its regime map IS the famous bifurcation diagram:
//   r < 3        fixed point
//   3 .. ~3.449  period-2,  then 4, 8, ...   (the cascade = step patterns 2,4,8)
//   ~3.5699      onset of chaos
//   ~3.828       period-3 window (and other windows above)
//   r = 4        fully chaotic — AND exactly solvable: x_n = sin^2(2^n pi theta0)
//
// So the period-doubling cascade reads directly as sequencer pattern length.
// Status: step() REAL; analytic (r=4) REAL; regimeMap stubbed until pipeline runs.

#pragma once

#include <cmath>

#include "../map_system.hpp"

namespace soundnuts::systems {

namespace {
constexpr double kLogPi = 3.14159265358979323846;
constexpr double kRmin  = 2.5;                                   // below: fixed point
inline double logisticR(double b) { return kRmin + b * (4.0 - kRmin); }  // [0,1]->[2.5,4]
}  // namespace

// Exact solution, valid only at r = 4: x_n = sin^2(2^n pi theta0),
// theta0 = asin(sqrt(x0)) / pi. Useful for exact seeking of the chaotic series.
class LogisticAnalytic final : public AnalyticSolution {
public:
    [[nodiscard]] bool validAt(const Params& p) const override {
        return logisticR(p.bifurcation) >= 4.0 - 1e-9;          // exact only at r=4
    }
    [[nodiscard]] double period(const Params&) const override { return -1.0; }  // aperiodic

    // `t` carries the (real-extended) step index n.
    void eval(double t, const Params&, State& s) const override {
        const double v = std::sin(std::pow(2.0, t) * kLogPi * theta0_);
        s[0] = v * v;
    }

    void setTheta0FromX0(double x0) { theta0_ = std::asin(std::sqrt(x0)) / kLogPi; }

private:
    double theta0_ = std::asin(std::sqrt(0.2)) / kLogPi;
};

class Logistic final : public MapSystem {
public:
    [[nodiscard]] std::string id()   const override { return "logistic"; }
    [[nodiscard]] std::string name() const override { return "Logistic"; }
    [[nodiscard]] int dim()          const override { return 1; }

    void step(const State& s, const Params& p, State& next) const override {
        const double r = logisticR(p.bifurcation);
        next[0] = r * s[0] * (1.0 - s[0]);
    }

    [[nodiscard]] std::pair<double, double> bifurcationRange() const override {
        return {kRmin, 4.0};
    }
    [[nodiscard]] State seedIC() const override { return State{0.2}; }
    [[nodiscard]] const RegimeMap& regimeMap() const override { return regime_; }
    [[nodiscard]] const AnalyticSolution* analytic() const override { return &analytic_; }

    RegimeMap& mutableRegimeMap() { return regime_; }

private:
    RegimeMap        regime_{};
    LogisticAnalytic analytic_{};
};

}  // namespace soundnuts::systems
