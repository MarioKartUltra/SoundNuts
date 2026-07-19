// SoundNuts — integrator.hpp
// Fixed-step RK4 at a control rate decoupled from audio sample rate.

#pragma once

#include <cmath>

#include "attractor_system.hpp"

namespace soundnuts {

// Advances a system's state at a fixed control rate (e.g. 1-4 kHz). The mod
// engine resamples/interpolates control-rate output up to block rate. Keeping
// integration sample-rate-independent makes the same patch deterministic across
// projects and hosts.
class Integrator {
public:
    Integrator(const AttractorSystem& sys, double controlRateHz)
        : sys_(sys), controlRateHz_(controlRateHz) {}

    // Reset to the on-attractor IC (call on load / note-retrigger / divergence).
    void reset() { state_ = sys_.onAttractorIC(); }

    // Seed an arbitrary state — used by the analytic<->numerical handoff so there
    // is no discontinuity when the macro crosses a closed-form validity boundary.
    void setState(const State& s) { state_ = s; }

    // One RK4 step of size dt = rateScale / controlRateHz. `rateScale` is the user
    // "Rate" knob warping the system's native clock. Returns false if the step
    // diverged (non-finite or |state| over kDivergenceBound) so the mod engine can
    // fire the riser/reseed.
    bool step(const Params& p, double rateScale) {
        const int d = sys_.dim();
        const double dt = rateScale / controlRateHz_;
        State k1{}, k2{}, k3{}, k4{}, tmp{};

        sys_.rhs(state_, p, k1);
        for (int i = 0; i < d; ++i) tmp[i] = state_[i] + 0.5 * dt * k1[i];
        sys_.rhs(tmp, p, k2);
        for (int i = 0; i < d; ++i) tmp[i] = state_[i] + 0.5 * dt * k2[i];
        sys_.rhs(tmp, p, k3);
        for (int i = 0; i < d; ++i) tmp[i] = state_[i] + dt * k3[i];
        sys_.rhs(tmp, p, k4);
        for (int i = 0; i < d; ++i)
            state_[i] += (dt / 6.0) * (k1[i] + 2.0 * k2[i] + 2.0 * k3[i] + k4[i]);

        for (int i = 0; i < d; ++i)
            if (!std::isfinite(state_[i]) || std::fabs(state_[i]) > kDivergenceBound)
                return false;
        return true;
    }

    [[nodiscard]] const State& state() const { return state_; }
    [[nodiscard]] double controlRateHz() const { return controlRateHz_; }

    // Matches pipeline/sweep.py Thresholds.bound_abs so the C++ divergence call
    // agrees with the offline "divergent" regime classification.
    static constexpr double kDivergenceBound = 1e3;

private:
    const AttractorSystem& sys_;
    double controlRateHz_;
    State  state_{};
};

}  // namespace soundnuts
