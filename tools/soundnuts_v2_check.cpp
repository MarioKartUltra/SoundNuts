// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// SoundNuts II — v2 offline checks (JUCE-free). Wired as CTest `soundnuts_v2`.
//   1. frozen tempo-division table math
//   2. LFO shape bounds + seeded determinism (S&H / smooth-random / drift)
//   3. LFO synced-division period
//   4. seed policy: fixed / per-key / random-on-strike
//   5. orbit sync: estimateNativePeriod on a curated system + syncRateScale math
//   6. units round-trips (the readouts every knob shows)
//   7. morph-style crossfade bounds on two live engines
#include "acv2/dsp/TempoSync.h"
#include "acv2/dsp/Lfo.h"
#include "acv2/dsp/SeedPolicy.h"
#include "acv2/dsp/Units.h"
#include "soundnuts/registry.hpp"
#include "soundnuts/mod_engine.hpp"
#include "soundnuts/orbit_sync.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; }         \
    } while (0)

int main()
{
    using namespace acv2;

    // 1 ---- tempo table --------------------------------------------------------
    CHECK(tempo::kNumDivisions == 16, "division table frozen at 16");
    CHECK(std::abs(tempo::divisionToMs(8, 120.0) - 500.0) < 1e-9, "1/4 @120 = 500 ms");
    CHECK(std::abs(tempo::divisionToMs(5, 120.0) - 250.0) < 1e-9, "1/8 @120 = 250 ms");
    CHECK(std::abs(tempo::divisionToMs(3, 120.0) - 250.0 * 2.0 / 3.0) < 1e-9, "1/8T @120");
    CHECK(std::abs(tempo::divisionToMs(7, 120.0) - 375.0) < 1e-9, "1/8. @120 = 375 ms");
    CHECK(std::abs(tempo::divisionToMs(15, 60.0) - 16000.0) < 1e-9, "4/1 @60 = 16 s");
    CHECK(std::abs(tempo::barQuarters(6, 8) - 3.0) < 1e-12, "6/8 bar = 3 quarters");
    CHECK(tempo::indexOfLabel("1/4") == 8, "label lookup");

    // 2 ---- LFO bounds + determinism ------------------------------------------
    for (int s = 0; s < kNumLfoShapes; ++s)
    {
        Lfo l;
        l.prepare(48000.0);
        l.setShape((LfoShape) s);
        l.setRateHz(3.7);
        l.reseed(0xABCDEF12345ull);
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < 2000; ++i)
        {
            const float v = l.processBlock(256);
            lo = std::min(lo, v); hi = std::max(hi, v);
        }
        CHECK(lo >= -1.0f && hi <= 1.0f, "LFO bipolar bounds");
        l.setBipolar(false);
        for (int i = 0; i < 200; ++i)
        {
            const float v = l.processBlock(256);
            CHECK(v >= 0.0f && v <= 1.0f, "LFO unipolar bounds");
        }
    }
    {
        auto run = [](std::uint64_t seed) {
            Lfo l;
            l.prepare(48000.0);
            l.setShape(LfoShape::SampleHold);
            l.setRateHz(11.0);
            l.reseed(seed);
            std::vector<float> out;
            for (int i = 0; i < 400; ++i) out.push_back(l.processBlock(128));
            return out;
        };
        CHECK(run(42) == run(42), "same seed => identical S&H stream");
        CHECK(run(42) != run(43), "different seed => different S&H stream");
    }

    // 3 ---- LFO synced division period ----------------------------------------
    {
        Lfo l;
        l.prepare(48000.0);
        l.setShape(LfoShape::SawUp);
        l.setBpm(120.0);
        l.setSyncedDivision(8);                     // 1/4 @120 = 0.5 s
        const double hz = l.rateHz();
        CHECK(std::abs(hz - 2.0) < 1e-9, "1/4 @120 => 2 Hz LFO");
    }

    // 4 ---- seed policy --------------------------------------------------------
    {
        SeedPolicy sp;
        sp.setGlobalSeed(1234);
        sp.setMode(SeedMode::Fixed);
        CHECK(sp.onNoteOn(60) == sp.onNoteOn(72), "fixed: same seed for all keys");
        sp.setMode(SeedMode::PerKey);
        const auto a = sp.onNoteOn(60), b = sp.onNoteOn(72), a2 = sp.onNoteOn(60);
        CHECK(a == a2, "per-key: stable per key");
        CHECK(a != b, "per-key: differs across keys");
        sp.setMode(SeedMode::RandomOnStrike);
        const auto r1 = sp.onNoteOn(60), r2 = sp.onNoteOn(60);
        CHECK(r1 != r2, "random-on-strike: fresh seed per strike");
        CHECK(sp.lastUsedSeed() == r2, "last seed reported to UI");
    }

    // 5 ---- orbit sync ---------------------------------------------------------
    {
        soundnuts::Registry reg;
        // pendulum (idx 2) librates: mean-crossing estimate must find a period
        const double per = soundnuts::estimateNativePeriod(reg.system(2), 0.4, 200.0, 8192);
        CHECK(per > 0.0 && std::isfinite(per), "pendulum native period measurable");
        const double eff = soundnuts::nativePeriodAt(reg.system(2), 0.4, per);
        CHECK(eff > 0.0, "nativePeriodAt returns a usable period");
        // rateScale * divisionSeconds == nativePeriod (inside clamp range)
        const double divSec = 0.5;                  // 1/4 @120
        const double rs = soundnuts::syncRateScale(eff, divSec);
        if (rs > soundnuts::kSyncRateScaleMin && rs < soundnuts::kSyncRateScaleMax)
            CHECK(std::abs(rs * divSec - eff) < 1e-9, "sync: one orbit per division");
        CHECK(soundnuts::syncRateScale(-1.0, 0.5) > 0.0, "sync: degenerate period safe");
        CHECK(soundnuts::syncRateScale(1.0, 0.0) > 0.0, "sync: degenerate division safe");
        CHECK(soundnuts::syncRateScale(1e9, 0.001) <= soundnuts::kSyncRateScaleMax,
              "sync: clamped to RK4-stable warp");
    }

    // 6 ---- units round-trips --------------------------------------------------
    CHECK(units::msToText(250.0) == std::string("250 ms"), "ms text");
    CHECK(units::msToText(1500.0) == std::string("1.50 s"), "s text");
    CHECK(std::abs(units::textToMs("1.5 s") - 1500.0) < 1e-9, "text->ms seconds");
    CHECK(std::abs(units::textToHz("2.5 kHz") - 2500.0) < 1e-9, "text->Hz kilo");
    CHECK(std::abs(units::textToPercent("+35 %") - 0.35) < 1e-9, "text->percent");
    CHECK(std::abs(units::textToDb("-6 dB") + 6.0) < 1e-9, "text->dB");

    // 7 ---- two-engine morph bounds (the v2 crossfade never leaves [-1,1]) ----
    {
        soundnuts::Registry reg;
        soundnuts::ModConfig cfg;
        cfg.controlRateHz = 1000.0;
        soundnuts::ModEngine e0(reg.system(0), cfg), e1(reg.system(1), cfg);
        e0.prepare(48000.0, 256);
        e1.prepare(48000.0, 256);
        soundnuts::ModBlock b0, b1;
        for (int blk = 0; blk < 40; ++blk)
        {
            e0.process(0.7, 256, b0);
            e1.process(0.3, 256, b1);
            const double w = 1.0 - blk / 39.0;
            for (int n = 0; n < 256; ++n)
            {
                const double x = (1.0 - w) * b0.out[0][(size_t) n] + w * b1.out[0][(size_t) n];
                CHECK(std::isfinite(x) && x >= -1.0001 && x <= 1.0001, "morph mix bounded");
                if (failures > 20) break;
            }
            if (failures > 20) break;
        }
    }

    std::printf("%s — %d failure(s)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
