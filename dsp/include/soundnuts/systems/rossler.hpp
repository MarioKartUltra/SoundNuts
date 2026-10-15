// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// SoundNuts — systems/rossler.hpp
// Rössler '76. Single-folded-band chaos; smoother / more "rolling" than Lorenz,
// which makes it a nice musical LFO source.
//
//   dx/dt = -y - z
//   dy/dt = x + a y
//   dz/dt = b + z (x - c)
//
// Bifurcation macro -> c (a, b fixed). Period-doubling route to chaos:
//   c ~ 2..4     period-1 limit cycle
//   c ~ 4..4.2   period-2, then 4, 8 ...
//   c ~ 5.7      canonical strange attractor
//   higher c     screw-type chaos / periodic windows
//
// No closed form -> analytic() stays nullptr (always integrate).
// Mapping MUST match pipeline/sweep.py (_rossler): c in [2, 18], a=b=0.2.
//
// Status: rhs() REAL; regimeMap/IC stubbed until rossler.json is loaded.

#pragma once

#include "../attractor_system.hpp"

namespace soundnuts::systems {

class Rossler final : public AttractorSystem {
public:
    [[nodiscard]] std::string id()   const override { return "rossler"; }
    [[nodiscard]] std::string name() const override { return "Rössler"; }
    [[nodiscard]] int dim()          const override { return 3; }

    void rhs(const State& s, const Params& p, State& ds) const override {
        const double c = mapBifurcation(p.bifurcation);
        ds[0] = -s[1] - s[2];
        ds[1] = s[0] + a_ * s[1];
        ds[2] = b_ + s[2] * (s[0] - c);
    }

    // Macro [0,1] -> c, spanning the period-doubling cascade through chaos.
    [[nodiscard]] std::pair<double, double> bifurcationRange() const override {
        return {kCMin, kCMax};
    }

    // On-attractor-ish IC (matches pipeline; transient settles it onto the band).
    [[nodiscard]] State onAttractorIC() const override {
        return State{-5.0, 5.0, 0.2};
    }

    [[nodiscard]] const RegimeMap& regimeMap() const override { return regime_; }

    RegimeMap& mutableRegimeMap() { return regime_; }  // registry loads JSON here

private:
    [[nodiscard]] double mapBifurcation(double b) const {
        return kCMin + b * (kCMax - kCMin);
    }

    static constexpr double a_ = 0.2;
    static constexpr double b_ = 0.2;
    static constexpr double kCMin = 2.0;
    static constexpr double kCMax = 18.0;

    RegimeMap regime_{};
};

}  // namespace soundnuts::systems
