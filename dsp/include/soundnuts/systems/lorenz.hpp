// SoundNuts — systems/lorenz.hpp
// Lorenz '63. Reference implementation showing the per-system pattern: the only
// real code is rhs(); everything else is data wired from the offline pipeline.
//
//   dx/dt = sigma (y - x)
//   dy/dt = x (rho - z) - y
//   dz/dt = x y - beta z
//
// Bifurcation macro -> rho. Classic landmarks along rho:
//   rho < 1            fixed point (origin stable)
//   1 < rho < ~24.06   nontrivial fixed points / transient
//   rho ~ 24.06        onset of chaos
//   rho = 28           canonical strange attractor
//   high rho           periodic windows / can run away numerically -> riser zone
//
// Status: rhs() is REAL. Metadata/regimeMap are STUB placeholders until the
// pipeline emits lorenz.json (then this wires to it / codegen fills it in).

#pragma once

#include "../attractor_system.hpp"

namespace soundnuts::systems {

class Lorenz final : public AttractorSystem {
public:
    [[nodiscard]] std::string id()   const override { return "lorenz"; }
    [[nodiscard]] std::string name() const override { return "Lorenz"; }
    [[nodiscard]] int dim()          const override { return 3; }

    void rhs(const State& s, const Params& p, State& ds) const override {
        const double rho = mapBifurcation(p.bifurcation);
        ds[0] = sigma_ * (s[1] - s[0]);
        ds[1] = s[0] * (rho - s[2]) - s[1];
        ds[2] = s[0] * s[1] - beta_ * s[2];
    }

    // Macro [0,1] -> rho over a musically useful span straddling the chaos onset.
    [[nodiscard]] std::pair<double, double> bifurcationRange() const override {
        return {kRhoMin, kRhoMax};
    }

    // dysts on-attractor IC (placeholder near the rho=28 attractor; replace with
    // exact dysts value when porting).
    [[nodiscard]] State onAttractorIC() const override {
        return State{-9.7869288, -15.03852, 20.533978};
    }

    // STUB: returns an empty map until lorenz.json is loaded by the registry.
    [[nodiscard]] const RegimeMap& regimeMap() const override { return regime_; }

    RegimeMap& mutableRegimeMap() { return regime_; }  // registry loads JSON here

private:
    [[nodiscard]] double mapBifurcation(double b) const {
        return kRhoMin + b * (kRhoMax - kRhoMin);
    }

    static constexpr double sigma_ = 10.0;
    static constexpr double beta_  = 8.0 / 3.0;
    static constexpr double kRhoMin = 0.0;
    static constexpr double kRhoMax = 90.0;   // pushes into periodic windows + runaway edge

    RegimeMap regime_{};
};

}  // namespace soundnuts::systems
