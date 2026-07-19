// =============================================================================
//  SoundNuts II — PluginProcessor.h  (v2 beta)
//  Chaotic-attractor modulation FX. The trajectory of one of 94 strange
//  attractors drives a filter, tremolo, and auto-pan; one Macro walks the
//  bifurcation diagram. v2 adds: foleys MagicProcessor GUI (WYSIWYG palette),
//  tempo-synced orbit speed (three-param sync convention), LFO A/B -> mod
//  matrix (Macro / Filter / Tremolo / Rate), smooth system morphing,
//  bar-locked orbit reset, per-system favorites + search, named presets.
//  DSP core stays JUCE-free (dsp/include/soundnuts) — the JUCE layer only
//  converts APVTS -> plain config each block.
// =============================================================================
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <foleys_gui_magic/foleys_gui_magic.h>

#include <atomic>
#include <memory>
#include <vector>

#include "soundnuts/registry.hpp"
#include "soundnuts/mod_engine.hpp"
#include "soundnuts/orbit_sync.hpp"

#include "acv2/dsp/TempoSync.h"
#include "acv2/dsp/Lfo.h"
#include "acv2/dsp/SeedPolicy.h"
#include "acv2/dsp/Units.h"

namespace pid
{
    // v1 params (versionHint 1 — same DSP meaning as v1)
    inline constexpr auto system      = "system";
    inline constexpr auto macro       = "macro";        // the bifurcation knob
    inline constexpr auto rate        = "rate";         // native-clock warp (x)
    inline constexpr auto filterAmt   = "filterAmt";
    inline constexpr auto filterBase  = "filterBase";
    inline constexpr auto reso        = "resonance";
    inline constexpr auto tremAmt     = "tremAmt";
    inline constexpr auto panAmt      = "panAmt";
    inline constexpr auto smoothingMs = "smoothing";
    inline constexpr auto solver      = "solver";
    inline constexpr auto divergence  = "divergence";
    inline constexpr auto mix         = "mix";
    inline constexpr auto output      = "output";

    // v2 params (versionHint 2)
    inline constexpr auto rateSync    = "rateSync";     // orbit lock: ms/beats chip
    inline constexpr auto rateDiv     = "rateDiv";      // frozen 16-division list
    inline constexpr auto morphMs     = "morphMs";      // system crossfade time
    inline constexpr auto morphSync   = "morphSync";
    inline constexpr auto morphDiv    = "morphDiv";
    inline constexpr auto orbitReset  = "orbitReset";   // Off / every N bars

    inline constexpr auto lfoAShape   = "lfoAShape";
    inline constexpr auto lfoARateMs  = "lfoARateMs";
    inline constexpr auto lfoARateSync= "lfoARateSync";
    inline constexpr auto lfoARateDiv = "lfoARateDiv";
    inline constexpr auto lfoAPhase   = "lfoAPhase";
    inline constexpr auto lfoARetrig  = "lfoARetrig";
    inline constexpr auto lfoABipolar = "lfoABipolar";

    inline constexpr auto lfoBShape   = "lfoBShape";
    inline constexpr auto lfoBRateMs  = "lfoBRateMs";
    inline constexpr auto lfoBRateSync= "lfoBRateSync";
    inline constexpr auto lfoBRateDiv = "lfoBRateDiv";
    inline constexpr auto lfoBPhase   = "lfoBPhase";
    inline constexpr auto lfoBRetrig  = "lfoBRetrig";
    inline constexpr auto lfoBBipolar = "lfoBBipolar";

    inline constexpr auto macroModA      = "macroModA";
    inline constexpr auto macroModB      = "macroModB";
    inline constexpr auto filterBaseModA = "filterBaseModA";
    inline constexpr auto filterBaseModB = "filterBaseModB";
    inline constexpr auto tremAmtModA    = "tremAmtModA";
    inline constexpr auto tremAmtModB    = "tremAmtModB";
    inline constexpr auto rateModA       = "rateModA";
    inline constexpr auto rateModB       = "rateModB";
}

// Minimal TPT state-variable low-pass (Zavalishin), stable across modulation.
struct SvfLowpass
{
    double ic1 = 0.0, ic2 = 0.0, g = 0.0, k = 1.0, a1 = 0, a2 = 0, a3 = 0;
    void reset() { ic1 = ic2 = 0.0; }
    void set(double cutoffHz, double sampleRate, double resonance01)
    {
        cutoffHz = juce::jlimit(20.0, sampleRate * 0.45, cutoffHz);
        g = std::tan(juce::MathConstants<double>::pi * cutoffHz / sampleRate);
        k = 2.0 - 1.9 * juce::jlimit(0.0, 1.0, resonance01);   // 2 (no res) .. 0.1
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    inline double process(double v0)
    {
        const double v3 = v0 - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return v2; // low-pass
    }
};

class SoundNutsProcessor : public foleys::MagicProcessor,
                           private juce::AudioProcessorValueTreeState::Listener,
                           private juce::AsyncUpdater
{
public:
    SoundNutsProcessor();
    ~SoundNutsProcessor() override { cancelPendingUpdate(); }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    const juce::String getName() const override { return "SoundNuts II"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    // ---- foleys ---------------------------------------------------------------
    void initialiseBuilder(foleys::MagicGUIBuilder& builder) override;
    juce::ValueTree createGuiValueTree() override;

    // ---- editor helpers (all polled from the message thread) ------------------
    juce::AudioProcessorValueTreeState& state() { return apvts; }
    soundnuts::Registry& registry() { return registry_; }
    int    currentSystemIndex() const { return systemIndexUi_.load(); }
    double currentMacro()       const { return macroUi_.load(); }      // effective (post-LFO)
    bool   currentAnalytic()    const { return analyticUi_.load(); }
    double currentBpm()         const { return bpmUi_.load(); }
    std::atomic<float>& lfoAValue() { return lfoAValue_; }
    std::atomic<float>& lfoBValue() { return lfoBValue_; }
    // Copy the recent modulation trajectory for the phase scope.
    void copyModHistory(std::vector<float>& x, std::vector<float>& y) const;

    // ---- favorites (persist inside the plugin state tree) ---------------------
    juce::StringArray getFavoriteIds();
    void setFavorite(const juce::String& systemId, bool starred);
    bool isFavorite(const juce::String& systemId);

    // ---- named presets (message thread only) ----------------------------------
    juce::StringArray factoryPresetNames() const;
    void applyFactoryPreset(int index);
    juce::File presetDirectory() const;          // ~/Music/SoundNuts II/Presets
    juce::StringArray userPresetNames() const;
    bool saveUserPreset(const juce::String& name);
    bool loadUserPreset(const juce::String& name);

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void postSetStateInformation() override;

private:
    // Mode-dependent control visibility (UI-only; params stay registered).
    // The four *Sync bools publish view:* properties the magic.xml binds to.
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void publishViewProperties();

    double sr = 48000.0;
    juce::AudioProcessorValueTreeState apvts;

    soundnuts::Registry registry_;

    // One engine per system, pre-built in prepareToPlay so switching/morphing
    // never allocates on the audio thread.
    std::vector<std::unique_ptr<soundnuts::ModEngine>> engines_;
    std::vector<double> nativePeriods_;      // offline estimates (orbit sync fallback)
    soundnuts::ModBlock blockCur_, blockPrev_;
    int    curSys_  = -1;
    int    prevSys_ = -1;
    double morphSamplesLeft_ = 0.0, morphSamplesTotal_ = 0.0;

    SvfLowpass svfL, svfR;

    // LFOs (block-rate, applied as bounded offsets before the DSP config)
    acv2::Lfo lfoA_, lfoB_;
    std::atomic<float> lfoAValue_ { 0.0f }, lfoBValue_ { 0.0f };
    acv2::SeedPolicy seedPolicy_;            // vendored infra (fixed-seed LFO reseed)

    // Transport cache (TECH_BRIEF §2)
    double lastKnownBpm_ = 120.0;
    int    sigNum_ = 4, sigDen_ = 4;
    bool   wasPlaying_ = false;
    long long lastResetBar_ = -1;

    // UI-facing atomics
    std::atomic<int>    systemIndexUi_ { 0 };
    std::atomic<double> macroUi_       { 0.5 };
    std::atomic<bool>   analyticUi_    { false };
    std::atomic<double> bpmUi_         { 120.0 };

    // Rolling mod history for the phase scope (UI thread copies it).
    static constexpr int kScope = 4096;
    std::vector<float> scopeX, scopeY;
    std::atomic<int>   scopePos { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundNutsProcessor)
};
