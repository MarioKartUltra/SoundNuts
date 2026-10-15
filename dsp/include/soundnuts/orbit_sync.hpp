// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// SoundNuts — orbit_sync.hpp  (v2)
// Tempo-sync support for the attractor's native clock. JUCE-free, header-only.
//
// The "Rate" knob is a warp of the system's native time: dt = rateScale / fc.
// To lock the ORBIT to a musical division we need the attractor's characteristic
// period in native time units. Sources, in order of preference:
//   1. the baked regime map (curated systems carry a measured period per macro),
//   2. an offline estimate (mean-crossing intervals of channel 0 over a short
//      RK4 run) — done once per system at prepare time, NEVER on the audio thread.
// Then rateScale = nativePeriod / divisionSeconds makes one characteristic orbit
// take exactly one division.

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "attractor_system.hpp"
#include "integrator.hpp"

namespace soundnuts {

// Bounds for the sync-derived rate warp. Manual "Rate" is 0.05..8; sync may ask
// for a little more (1/32 at high BPM) but must stay inside RK4-stable dt.
inline constexpr double kSyncRateScaleMin = 0.02;
inline constexpr double kSyncRateScaleMax = 16.0;

// Offline estimate of the characteristic period (native time units) of channel 0
// at macro `bif`. Returns <= 0 if no oscillation could be measured (fixed point,
// immediate divergence). Allocation + O(steps) work: call from prepare, not RT.
inline double estimateNativePeriod(const AttractorSystem& sys, double bif,
                                   double controlRateHz = 200.0, int steps = 8192)
{
    if (controlRateHz <= 0.0 || steps < 64) return -1.0;

    Params p;
    p.bifurcation = bif;

    Integrator integ(sys, controlRateHz);
    integ.reset();

    const int warmup = steps / 4;
    for (int i = 0; i < warmup; ++i)
        if (!integ.step(p, 1.0)) { integ.reset(); }

    std::vector<double> x;
    x.reserve(static_cast<std::size_t>(steps));
    for (int i = 0; i < steps; ++i) {
        if (!integ.step(p, 1.0)) { integ.reset(); }
        x.push_back(integ.state()[0]);
    }

    double mean = 0.0;
    for (double v : x) mean += v;
    mean /= static_cast<double>(x.size());

    // Upward mean-crossings; period = span between first and last crossing / cycles.
    int firstCross = -1, lastCross = -1, crossings = 0;
    for (std::size_t i = 1; i < x.size(); ++i) {
        if (x[i - 1] < mean && x[i] >= mean) {
            if (firstCross < 0) firstCross = static_cast<int>(i);
            lastCross = static_cast<int>(i);
            ++crossings;
        }
    }
    if (crossings < 3 || lastCross <= firstCross) return -1.0;

    const double samplesPerCycle =
        static_cast<double>(lastCross - firstCross) / static_cast<double>(crossings - 1);
    const double period = samplesPerCycle / controlRateHz;
    return (std::isfinite(period) && period > 0.0) ? period : -1.0;
}

// Best available native period at macro `bif`: baked map first, else the given
// prepared fallback (from estimateNativePeriod), else 1 native time unit.
inline double nativePeriodAt(const AttractorSystem& sys, double bif, double preparedFallback)
{
    const RegimePoint pt = sys.regimeMap().sampleAt(bif);
    if (pt.period > 0.0 && std::isfinite(pt.period)) return pt.period;
    if (preparedFallback > 0.0 && std::isfinite(preparedFallback)) return preparedFallback;
    return 1.0;
}

// rateScale so that one native period spans `divisionSeconds` of wall time.
inline double syncRateScale(double nativePeriodUnits, double divisionSeconds)
{
    if (!(nativePeriodUnits > 0.0) || !std::isfinite(nativePeriodUnits)) nativePeriodUnits = 1.0;
    if (!(divisionSeconds > 1e-6) || !std::isfinite(divisionSeconds))    divisionSeconds = 0.5;
    return std::clamp(nativePeriodUnits / divisionSeconds, kSyncRateScaleMin, kSyncRateScaleMax);
}

}  // namespace soundnuts
