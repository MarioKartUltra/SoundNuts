// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// SoundNuts — systems/pendulum.hpp
// Gravity pendulum: theta'' + omega0^2 sin(theta) = 0, state (theta, omega).
// The reference ANALYTIC-CAPABLE system: exact Jacobi-elliptic solution in the
// libration regime, with the period stretching to infinity at the separatrix.
//
// Macro -> initial amplitude theta0 in [0, pi):
//   small theta0      near-sinusoidal limit cycle, period ~ 2pi/omega0
//   theta0 -> pi      period -> infinity (separatrix) — LFO slows dramatically
//   beyond            rotation (numerical only here)
//
// Status: rhs() REAL; AnalyticSolution REAL up to the elliptic primitives in
// analytic/elliptic.hpp (declared, impl pending). regimeMap/IC stubbed.

#pragma once

#include <cmath>

#include "../analytic/elliptic.hpp"
#include "../attractor_system.hpp"

namespace soundnuts::systems {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

// Closed form for the librating pendulum.
//   k = sin(theta0/2)  (elliptic modulus)
//   theta(t) = 2 asin( k * sn(omega0 t, k) )
//   omega(t) = dtheta/dt = 2 omega0 k * cn(omega0 t, k)
//   period   = 4 K(k) / omega0
class PendulumAnalytic final : public AnalyticSolution {
public:
    explicit PendulumAnalytic(double omega0) : omega0_(omega0) {}

    // Closed form valid only for libration (theta0 < pi). Rotation -> integrator.
    [[nodiscard]] bool validAt(const Params& p) const override {
        return amplitude(p) < kPi - kEps;
    }

    [[nodiscard]] double period(const Params& p) const override {
        const double k = std::sin(0.5 * amplitude(p));
        return 4.0 * detail::ellint_K(k) / omega0_;          // -> inf at separatrix
    }

    void eval(double t, const Params& p, State& s) const override {
        const double k = std::sin(0.5 * amplitude(p));
        const double u = omega0_ * t;
        s[0] = 2.0 * std::asin(k * detail::jacobi_sn(u, k));  // theta(t)
        s[1] = 2.0 * omega0_ * k * detail::jacobi_cn(u, k);   // omega(t)
    }

private:
    [[nodiscard]] static double amplitude(const Params& p) {
        return p.bifurcation * kPi;                           // [0,1] -> [0, pi)
    }
    static constexpr double kEps = 1e-4;
    double omega0_;
};

class Pendulum final : public AttractorSystem {
public:
    [[nodiscard]] std::string id()   const override { return "pendulum"; }
    [[nodiscard]] std::string name() const override { return "Pendulum"; }
    [[nodiscard]] int dim()          const override { return 2; }

    void rhs(const State& s, const Params&, State& ds) const override {
        ds[0] = s[1];
        ds[1] = -omega0_ * omega0_ * std::sin(s[0]);
    }

    [[nodiscard]] std::pair<double, double> bifurcationRange() const override {
        return {0.0, kPi};                                   // amplitude -> separatrix
    }

    // Conservative system: the "IC" is amplitude-dependent. When analytic() is
    // present the engine seeds from analytic_.eval(0, params) at retrigger; this
    // (release from rest at max amplitude) is the numerical-mode fallback seed.
    [[nodiscard]] State onAttractorIC() const override {
        return State{kPi * 0.5, 0.0};
    }

    [[nodiscard]] const RegimeMap& regimeMap() const override { return regime_; }
    [[nodiscard]] const AnalyticSolution* analytic() const override { return &analytic_; }

    RegimeMap& mutableRegimeMap() { return regime_; }

private:
    static constexpr double omega0_ = 6.28318530717958647692;  // 2*pi rad/s native
    RegimeMap        regime_{};
    PendulumAnalytic analytic_{omega0_};
};

}  // namespace soundnuts::systems
