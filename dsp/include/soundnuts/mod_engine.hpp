// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// SoundNuts — mod_engine.hpp
// Ties integrator + regime-map normalization into N bounded modulation outputs.
// The flow -> CV path (maps/sequencer live in sequencer.hpp, parked).

#pragma once

#include <algorithm>
#include <cmath>

#include "attractor_system.hpp"
#include "integrator.hpp"

namespace soundnuts {

enum class DivergenceMode : std::uint8_t {
    ResetOnDivergence = 0,  // |state| over thresh -> ramp out (riser) then reseed IC
    FreeRunClamp      = 1,   // never reseed (unless non-finite), hard-clip the output
};

// Closed-form vs numerical selection.
//   Auto           = use analytic() when present AND validAt(macro); else RK4.
//   ForceNumerical = always integrate (e.g. to keep the chaotic "feel" of error).
//   ForceAnalytic  = analytic wherever present (holds across invalid regions for a
//                    guaranteed drift-free / seekable mod).
enum class SolverPreference : std::uint8_t {
    Auto = 0,
    ForceNumerical = 1,
    ForceAnalytic = 2,
};

struct ModConfig {
    double controlRateHz = 2000.0;   // fixed at construction (see note on setConfig)
    double rateScale     = 1.0;      // user "Rate" knob (native-clock warp)
    bool   tempoSync     = false;    // only valid in green/LimitCycle zone
    DivergenceMode   divergence = DivergenceMode::ResetOnDivergence;
    SolverPreference solver     = SolverPreference::Auto;
    double smoothingMs   = 2.0;      // one-pole on each output to de-zipper
};

// One block of modulation output. Up to dim() channels, each in [-1,1].
struct ModBlock {
    int channels = 0;                                    // == sys.dim()
    std::array<std::vector<float>, kMaxDim> out;         // per-channel, length = numSamples
};

class ModEngine {
public:
    ModEngine(const AttractorSystem& sys, const ModConfig& cfg)
        : sys_(sys), cfg_(cfg), integ_(sys, cfg.controlRateHz) {}

    void prepare(double audioSampleRate, int /*maxBlockSize*/) {
        audioSampleRate_ = audioSampleRate;
        updateSmoothing();
        retrigger();
    }

    // NOTE: controlRateHz is bound at construction; changing it needs a new engine.
    void setConfig(const ModConfig& cfg) {
        cfg_ = cfg;
        updateSmoothing();
    }

    void retrigger() {                                   // note-on / load
        integ_.reset();
        ctrlCur_ = ctrlPrev_ = integ_.state();
        ctrlFrac_ = 0.0;
        analyticPhase_ = 0.0;
        ySmooth_.fill(0.0);
    }

    // Produce `numSamples` of normalized, smoothed modulation for macro `bif` in
    // [0,1]. Per cfg_.solver, evaluates the closed form (sample-exact) or steps the
    // integrator at control rate and linearly interpolates to audio rate; then
    // normalizes via regimeMap().sampleAt(bif), clamps, and one-pole smooths.
    void process(double bif, int numSamples, ModBlock& dst) {
        const int d = sys_.dim();
        dst.channels = d;
        for (int c = 0; c < d; ++c)
            dst.out[c].assign(static_cast<std::size_t>(numSamples), 0.0f);

        Params p;
        p.bifurcation = bif;                             // extra[] preset-fixed (default)
        const RegimePoint nrm = sys_.regimeMap().sampleAt(bif);
        const bool analytic = useAnalytic(p);

        const double dtAudio = cfg_.rateScale / audioSampleRate_;          // native t / sample
        const double ctrlPerSample = integ_.controlRateHz() / audioSampleRate_;

        for (int n = 0; n < numSamples; ++n) {
            State raw{};
            if (analytic) {
                analyticPhase_ += dtAudio;
                sys_.analytic()->eval(analyticPhase_, p, raw);
            } else {
                ctrlFrac_ += ctrlPerSample;
                while (ctrlFrac_ >= 1.0) {
                    ctrlFrac_ -= 1.0;
                    ctrlPrev_ = ctrlCur_;
                    const bool ok = integ_.step(p, cfg_.rateScale);
                    ctrlCur_ = integ_.state();
                    if (!ok) onDivergence();
                }
                for (int c = 0; c < d; ++c)
                    raw[c] = ctrlPrev_[c] + ctrlFrac_ * (ctrlCur_[c] - ctrlPrev_[c]);
            }

            for (int c = 0; c < d; ++c) {
                double v = (raw[c] - nrm.center[c]) * nrm.scale[c];
                v = std::clamp(v, -1.0, 1.0);            // final clip (riser hits here)
                ySmooth_[c] += smoothAlpha_ * (v - ySmooth_[c]);
                dst.out[c][static_cast<std::size_t>(n)] = static_cast<float>(ySmooth_[c]);
            }
        }
    }

    // Current regime under the macro — UI terrain + guard rails + sync-enable.
    [[nodiscard]] Regime currentRegime(double bif) const {
        return sys_.regimeMap().sampleAt(bif).regime;
    }

    // Is the engine running closed-form (vs integrating) at this macro? UI badge.
    [[nodiscard]] bool usingAnalytic(double bif) const {
        Params p; p.bifurcation = bif;
        return useAnalytic(p);
    }

    // Exact seek to host timeline position `t` (seconds). Meaningful only when
    // usingAnalytic(); numerical mode can't seek exactly, so it is left running.
    void seek(double t, double bif) {
        if (usingAnalytic(bif)) analyticPhase_ = cfg_.rateScale * t;
    }

private:
    [[nodiscard]] bool useAnalytic(const Params& p) const {
        const AnalyticSolution* a = sys_.analytic();
        switch (cfg_.solver) {
            case SolverPreference::ForceNumerical: return false;
            case SolverPreference::ForceAnalytic:  return a != nullptr;
            case SolverPreference::Auto:
            default:                               return a != nullptr && a->validAt(p);
        }
    }

    void onDivergence() {
        const int d = sys_.dim();
        bool nonfinite = false;
        for (int i = 0; i < d; ++i)
            if (!std::isfinite(integ_.state()[i])) { nonfinite = true; break; }
        if (cfg_.divergence == DivergenceMode::ResetOnDivergence || nonfinite) {
            integ_.reset();
            ctrlCur_ = ctrlPrev_ = integ_.state();
        }
    }

    void updateSmoothing() {
        const double tau = cfg_.smoothingMs * 0.001;
        smoothAlpha_ = (tau > 0.0 && audioSampleRate_ > 0.0)
                           ? 1.0 - std::exp(-1.0 / (tau * audioSampleRate_))
                           : 1.0;
    }

    const AttractorSystem& sys_;
    ModConfig   cfg_;
    Integrator  integ_;
    double      audioSampleRate_ = 0.0;

    // Closed-form path
    double analyticPhase_ = 0.0;

    // Numerical path: linear interpolation between consecutive control samples
    State  ctrlPrev_{}, ctrlCur_{};
    double ctrlFrac_ = 0.0;

    // Output smoothing (one-pole per channel)
    double smoothAlpha_ = 1.0;
    State  ySmooth_{};
};

}  // namespace soundnuts
