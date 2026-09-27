// =============================================================================
//  SoundNuts II — PluginProcessor.cpp  (v2 beta)
// =============================================================================
#include "PluginProcessor.h"
#include "ui/GuiItems.h"
#include "BinaryData.h"
#include "acv2/ui/LnfV2.h"      // pulls in acv2/ui/Theme.h

using APVTS = juce::AudioProcessorValueTreeState;
using namespace soundnuts;

namespace
{
    juce::NormalisableRange<float> unit() { return { 0.0f, 1.0f, 0.0001f }; }

    using FA = juce::AudioParameterFloatAttributes;

    FA percentText()
    {
        return FA().withStringFromValueFunction([](float v, int) {
                     return juce::String(acv2::units::percentToText(v));
                 })
                 .withValueFromStringFunction([](const juce::String& t) {
                     return (float) acv2::units::textToPercent(t.toStdString());
                 });
    }

    FA signedPercentText()
    {
        return FA().withStringFromValueFunction([](float v, int) {
                     return juce::String(acv2::units::percentToText(v, true));
                 })
                 .withValueFromStringFunction([](const juce::String& t) {
                     return (float) acv2::units::textToPercent(t.toStdString());
                 });
    }

    FA hzText()
    {
        return FA().withStringFromValueFunction([](float v, int) {
                     return juce::String(acv2::units::hzToText(v));
                 })
                 .withValueFromStringFunction([](const juce::String& t) {
                     return (float) acv2::units::textToHz(t.toStdString());
                 });
    }

    FA msText()
    {
        return FA().withStringFromValueFunction([](float v, int) {
                     return juce::String(acv2::units::msToText(v));
                 })
                 .withValueFromStringFunction([](const juce::String& t) {
                     return (float) acv2::units::textToMs(t.toStdString());
                 });
    }

    // LFO rate is stored as a period in ms (frozen three-param convention) but
    // reads out in Hz — the unit musicians expect on an LFO.
    FA lfoRateText()
    {
        return FA().withStringFromValueFunction([](float ms, int) {
                     return juce::String(acv2::units::hzToText(acv2::tempo::msToHz(ms)));
                 })
                 .withValueFromStringFunction([](const juce::String& t) {
                     const auto s = t.toStdString();
                     if (t.containsIgnoreCase("hz"))
                         return (float) acv2::tempo::hzToMs(acv2::units::textToHz(s));
                     const double v = acv2::units::textToMs(s);
                     return (float) (t.containsIgnoreCase("ms") || t.containsIgnoreCase("s")
                                         ? v : acv2::tempo::hzToMs(v)); // bare number = Hz
                 });
    }

    FA gainDbText()
    {
        return FA().withStringFromValueFunction([](float g, int) {
                     const double db = g > 0.0001f ? 20.0 * std::log10((double) g) : -96.0;
                     return juce::String(acv2::units::dbToText(db));
                 })
                 .withValueFromStringFunction([](const juce::String& t) {
                     const double db = acv2::units::textToDb(t.toStdString());
                     return (float) std::pow(10.0, db / 20.0);
                 });
    }

    FA ratioText()
    {
        return FA().withStringFromValueFunction([](float v, int) {
                     return "x " + juce::String(v, 2);
                 })
                 .withValueFromStringFunction([](const juce::String& t) {
                     return t.retainCharacters("0123456789.-").getFloatValue();
                 });
    }

    FA degreesText()
    {
        return FA().withStringFromValueFunction([](float v, int) {
                     return juce::String(juce::roundToInt(v * 360.0f)) + juce::String::fromUTF8(u8"°");
                 })
                 .withValueFromStringFunction([](const juce::String& t) {
                     return t.retainCharacters("0123456789.-").getFloatValue() / 360.0f;
                 });
    }
}

APVTS::ParameterLayout SoundNutsProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    auto f = [&](const char* id, const char* nm, juce::NormalisableRange<float> r, float d,
                 int hint, FA attr = {})
        { p.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,hint}, nm, r, d, attr)); };
    auto c = [&](const char* id, const char* nm, juce::StringArray ch, int d, int hint)
        { p.push_back(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id,hint}, nm, ch, d)); };
    auto b = [&](const char* id, const char* nm, bool d, int hint)
        { p.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id,hint}, nm, d)); };

    const auto divisions = acv2::tempo::labels<juce::StringArray>();
    const int  divQuarter = acv2::tempo::kDefaultDivision;   // "1/4"

    // ---- v1 core (hint 1) ---------------------------------------------------
    const int totalSystems = 3 + soundnuts::gen::kNumGeneratedSystems;
    p.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{pid::system,1}, "System", 0, totalSystems - 1, 0));
    f(pid::macro, "Macro", unit(), 0.5f, 1, percentText());
    f(pid::rate, "Orbit Rate", { 0.05f, 8.0f, 0.0f, 0.35f }, 1.0f, 1, ratioText());
    f(pid::filterAmt, "Filter Amount", unit(), 0.6f, 1, percentText());
    f(pid::filterBase, "Filter Base", { 80.0f, 12000.0f, 0.0f, 0.3f }, 900.0f, 1, hzText());
    f(pid::reso, "Resonance", unit(), 0.3f, 1, percentText());
    f(pid::tremAmt, "Tremolo Amount", unit(), 0.4f, 1, percentText());
    f(pid::panAmt, "Pan Amount", unit(), 0.5f, 1, percentText());
    f(pid::smoothingMs, "Smoothing", { 0.0f, 50.0f, 0.01f }, 3.0f, 1, msText());
    c(pid::solver, "Solver", { "Auto", "Numerical", "Analytic" }, 0, 1);
    c(pid::divergence, "On Blow-Up", { "Reset", "Free / Clamp" }, 0, 1);
    f(pid::mix, "Mix", unit(), 1.0f, 1, percentText());
    f(pid::output, "Output", { 0.0f, 2.0f, 0.001f }, 1.0f, 1, gainDbText());

    // ---- v2: orbit tempo-sync + morph + reset (hint 2) ----------------------
    b(pid::rateSync, "Orbit Sync", false, 2);
    c(pid::rateDiv, "Orbit Division", divisions, divQuarter, 2);
    f(pid::morphMs, "Morph Time", { 0.0f, 4000.0f, 0.0f, 0.35f }, 250.0f, 2, msText());
    b(pid::morphSync, "Morph Sync", false, 2);
    c(pid::morphDiv, "Morph Division", divisions, divQuarter, 2);
    c(pid::orbitReset, "Orbit Reset", { "Off", "1 Bar", "2 Bars", "4 Bars", "8 Bars" }, 0, 2);

    // ---- v2: LFO A/B --------------------------------------------------------
    juce::StringArray shapes;
    for (int i = 0; i < acv2::kNumLfoShapes; ++i)
        shapes.add(acv2::lfoShapeName((acv2::LfoShape) i));

    auto lfo = [&](const char* shape, const char* rateMs, const char* rateSync,
                   const char* rateDiv, const char* phase, const char* retrig,
                   const char* bipolar, const char* prefix)
    {
        c(shape, (juce::String(prefix) + " Shape").toRawUTF8(), shapes, 0, 2);
        f(rateMs, (juce::String(prefix) + " Rate").toRawUTF8(),
          { 20.0f, 20000.0f, 0.0f, 0.3f }, 500.0f, 2, lfoRateText());
        b(rateSync, (juce::String(prefix) + " Rate Sync").toRawUTF8(), false, 2);
        c(rateDiv, (juce::String(prefix) + " Rate Division").toRawUTF8(), divisions, divQuarter, 2);
        f(phase, (juce::String(prefix) + " Phase").toRawUTF8(), unit(), 0.0f, 2, degreesText());
        c(retrig, (juce::String(prefix) + " Retrig").toRawUTF8(), { "Free", "Beat-Locked" }, 0, 2);
        b(bipolar, (juce::String(prefix) + " Bipolar").toRawUTF8(), true, 2);
    };
    lfo(pid::lfoAShape, pid::lfoARateMs, pid::lfoARateSync, pid::lfoARateDiv,
        pid::lfoAPhase, pid::lfoARetrig, pid::lfoABipolar, "LFO A");
    lfo(pid::lfoBShape, pid::lfoBRateMs, pid::lfoBRateSync, pid::lfoBRateDiv,
        pid::lfoBPhase, pid::lfoBRetrig, pid::lfoBBipolar, "LFO B");

    // ---- v2: mod-matrix depths (±100 %) -------------------------------------
    auto depth = [&](const char* id, const char* nm)
        { f(id, nm, { -1.0f, 1.0f, 0.0001f }, 0.0f, 2, signedPercentText()); };
    depth(pid::macroModA,      "Macro < LFO A");
    depth(pid::macroModB,      "Macro < LFO B");
    depth(pid::filterBaseModA, "Filter < LFO A");
    depth(pid::filterBaseModB, "Filter < LFO B");
    depth(pid::tremAmtModA,    "Tremolo < LFO A");
    depth(pid::tremAmtModB,    "Tremolo < LFO B");
    depth(pid::rateModA,       "Rate < LFO A");
    depth(pid::rateModB,       "Rate < LFO B");

    return { p.begin(), p.end() };
}

SoundNutsProcessor::SoundNutsProcessor()
    : foleys::MagicProcessor(BusesProperties()
                                 .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                 .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "SOUNDNUTS", createLayout())
{
    FOLEYS_SET_SOURCE_PATH(__FILE__);
    scopeX.assign(kScope, 0.0f);
    scopeY.assign(kScope, 0.0f);
    seedPolicy_.setMode(acv2::SeedMode::Fixed);
    seedPolicy_.setGlobalSeed(0x50D9BEA75ull);       // stable random LFO character
    lfoA_.reseed(seedPolicy_.onNoteOn(0));
    lfoB_.reseed(seedPolicy_.onNoteOn(1));

    // sync-chip → visibility properties (mode-dependent controls, UI-only)
    for (auto* id : { pid::rateSync, pid::morphSync, pid::lfoARateSync, pid::lfoBRateSync })
        apvts.addParameterListener(id, this);
    publishViewProperties();
}

// ---- mode-dependent control visibility --------------------------------------
void SoundNutsProcessor::parameterChanged(const juce::String&, float)
{
    triggerAsyncUpdate();                            // Value writes = message thread
}

void SoundNutsProcessor::handleAsyncUpdate() { publishViewProperties(); }

void SoundNutsProcessor::publishViewProperties()
{
    auto set = [this](const char* prop, bool v)
    { magicState.getPropertyAsValue(prop).setValue(v); };

    const bool rateS  = apvts.getRawParameterValue(pid::rateSync)->load() > 0.5f;
    const bool morphS = apvts.getRawParameterValue(pid::morphSync)->load() > 0.5f;
    const bool lfoAS  = apvts.getRawParameterValue(pid::lfoARateSync)->load() > 0.5f;
    const bool lfoBS  = apvts.getRawParameterValue(pid::lfoBRateSync)->load() > 0.5f;

    set("view:rateMs",   !rateS);  set("view:rateDiv",   rateS);
    set("view:morphMs",  !morphS); set("view:morphDiv",  morphS);
    set("view:lfoAMs",   !lfoAS);  set("view:lfoADiv",   lfoAS);
    set("view:lfoBMs",   !lfoBS);  set("view:lfoBDiv",   lfoBS);
}

void SoundNutsProcessor::postSetStateInformation() { publishViewProperties(); }

// ---- named presets (all message thread) -------------------------------------
namespace
{
    struct FactoryPreset
    {
        const char* name;
        std::initializer_list<std::pair<const char*, float>> values; // plain param values
    };

    // Values are PLAIN (denormalized) parameter values; anything unlisted stays
    // at its default. System indices: 0 Lorenz, 1 Rössler, 2 Pendulum.
    static const FactoryPreset kFactoryPresets[] = {
        { "Init", {} },
        { "Butterfly Sweep",                       // Lorenz chaos wobbles the filter
          { { pid::system, 0.0f }, { pid::macro, 0.72f }, { pid::rate, 0.8f },
            { pid::filterAmt, 0.85f }, { pid::filterBase, 650.0f }, { pid::reso, 0.45f },
            { pid::tremAmt, 0.15f }, { pid::panAmt, 0.35f } } },
        { "Spiral Pulse",                          // Rössler locked to 1/8
          { { pid::system, 1.0f }, { pid::macro, 0.35f }, { pid::rateSync, 1.0f },
            { pid::rateDiv, 5.0f }, { pid::tremAmt, 0.8f }, { pid::panAmt, 0.2f },
            { pid::filterAmt, 0.35f } } },
        { "Elliptic Trem",                         // analytic pendulum, 1/4-locked tremolo
          { { pid::system, 2.0f }, { pid::macro, 0.55f }, { pid::solver, 2.0f },
            { pid::rateSync, 1.0f }, { pid::rateDiv, 8.0f }, { pid::tremAmt, 0.9f },
            { pid::filterAmt, 0.2f }, { pid::panAmt, 0.6f } } },
        { "Drifting Panorama",                     // slow LFO-B drift walks the macro
          { { pid::system, 0.0f }, { pid::macro, 0.5f }, { pid::panAmt, 1.0f },
            { pid::lfoBShape, 7.0f }, { pid::lfoBRateMs, 6000.0f },
            { pid::macroModB, 0.35f }, { pid::filterBaseModB, 0.3f },
            { pid::smoothingMs, 8.0f } } },
        { "Edge Riser",                            // ride the divergence boundary, bar reset
          { { pid::system, 1.0f }, { pid::macro, 0.93f }, { pid::divergence, 0.0f },
            { pid::orbitReset, 1.0f }, { pid::filterAmt, 1.0f }, { pid::reso, 0.6f },
            { pid::morphMs, 900.0f } } },
    };
    inline constexpr int kNumFactoryPresets = (int) std::size(kFactoryPresets);
}

juce::StringArray SoundNutsProcessor::factoryPresetNames() const
{
    juce::StringArray out;
    for (const auto& p : kFactoryPresets) out.add(p.name);
    return out;
}

void SoundNutsProcessor::applyFactoryPreset(int index)
{
    if (index < 0 || index >= kNumFactoryPresets) return;
    const auto& preset = kFactoryPresets[(size_t) index];

    // reset everything to defaults, then apply the preset's overrides
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost(rp->getDefaultValue());
            rp->endChangeGesture();
        }
    for (const auto& [id, value] : preset.values)
        if (auto* rp = apvts.getParameter(id))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost(rp->convertTo0to1(value));
            rp->endChangeGesture();
        }
}

juce::File SoundNutsProcessor::presetDirectory() const
{
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory)
        .getChildFile("SoundNuts II").getChildFile("Presets");
}

juce::StringArray SoundNutsProcessor::userPresetNames() const
{
    juce::StringArray out;
    for (const auto& f : presetDirectory().findChildFiles(juce::File::findFiles, false, "*.xml"))
        out.add(f.getFileNameWithoutExtension());
    out.sortNatural();
    return out;
}

bool SoundNutsProcessor::saveUserPreset(const juce::String& name)
{
    const auto safe = juce::File::createLegalFileName(name).trim();
    if (safe.isEmpty()) return false;
    auto dir = presetDirectory();
    if (!dir.createDirectory()) return false;
    if (auto xml = apvts.copyState().createXml())
        return xml->writeTo(dir.getChildFile(safe + ".xml"));
    return false;
}

bool SoundNutsProcessor::loadUserPreset(const juce::String& name)
{
    const auto file = presetDirectory().getChildFile(name + ".xml");
    if (!file.existsAsFile()) return false;
    if (auto xml = juce::XmlDocument::parse(file))
    {
        auto tree = juce::ValueTree::fromXml(*xml);
        if (tree.isValid() && tree.hasType(apvts.state.getType()))
        {
            apvts.replaceState(tree);
            publishViewProperties();
            return true;
        }
    }
    return false;
}

// ---- foleys -----------------------------------------------------------------
void SoundNutsProcessor::initialiseBuilder(foleys::MagicGUIBuilder& builder)
{
    builder.registerJUCEFactories();
    builder.registerJUCELookAndFeels();
    builder.registerLookAndFeel("LnfV2", std::make_unique<acv2::ui::LnfV2>(acv2::theme::kSoundNuts));
    snutsui::registerGuiItems(builder);
}

juce::ValueTree SoundNutsProcessor::createGuiValueTree()
{
    // fromUTF8: the (ptr, size) constructor garbles non-ASCII captions and tooltips
    const auto text = juce::String::fromUTF8(BinaryData::magic_xml, BinaryData::magic_xmlSize);
    auto tree = juce::ValueTree::fromXml(text);
    jassert(tree.isValid());                       // shipped default layout
    return tree.isValid() ? tree : juce::ValueTree("magic");
}

// ---- favorites (persist in the magic state tree -> host session) ------------
static juce::ValueTree favoritesNode(foleys::MagicProcessorState& ms)
{
    auto root = ms.getValueTree();
    auto node = root.getChildWithName("Favorites");
    if (!node.isValid())
    {
        node = juce::ValueTree("Favorites");
        root.appendChild(node, nullptr);
    }
    return node;
}

juce::StringArray SoundNutsProcessor::getFavoriteIds()
{
    juce::StringArray out;
    out.addTokens(favoritesNode(magicState).getProperty("ids", "").toString(), ";", "");
    out.removeEmptyStrings();
    return out;
}

void SoundNutsProcessor::setFavorite(const juce::String& systemId, bool starred)
{
    auto ids = getFavoriteIds();
    if (starred) ids.addIfNotAlreadyThere(systemId);
    else         ids.removeString(systemId);
    favoritesNode(magicState).setProperty("ids", ids.joinIntoString(";"), nullptr);
}

bool SoundNutsProcessor::isFavorite(const juce::String& systemId)
{
    return getFavoriteIds().contains(systemId);
}

// ---- audio ------------------------------------------------------------------
void SoundNutsProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    magicState.prepareToPlay(sampleRate, samplesPerBlock);

    // Pre-build one engine per system so switching / morphing never allocates
    // on the audio thread.
    ModConfig cfg;
    cfg.controlRateHz = 1000.0;
    engines_.clear();
    engines_.reserve((size_t) registry_.size());
    for (int i = 0; i < registry_.size(); ++i)
    {
        auto e = std::make_unique<ModEngine>(registry_.system(i), cfg);
        e->prepare(sampleRate, samplesPerBlock);
        engines_.push_back(std::move(e));
    }

    const int reserveN = juce::jmax(8192, samplesPerBlock);
    for (auto& v : blockCur_.out)  { v.clear(); v.reserve((size_t) reserveN); }
    for (auto& v : blockPrev_.out) { v.clear(); v.reserve((size_t) reserveN); }

    // Offline characteristic-period estimates (orbit tempo-sync fallback when the
    // baked map has no period). Sample-rate independent -> compute once.
    if (nativePeriods_.size() != (size_t) registry_.size())
    {
        nativePeriods_.assign((size_t) registry_.size(), -1.0);
        for (int i = 0; i < registry_.size(); ++i)
            nativePeriods_[(size_t) i] = estimateNativePeriod(registry_.system(i), 0.5, 200.0, 4096);
    }

    curSys_ = prevSys_ = -1;
    morphSamplesLeft_ = morphSamplesTotal_ = 0.0;
    lfoA_.prepare(sampleRate);
    lfoB_.prepare(sampleRate);
    svfL.reset(); svfR.reset();
}

bool SoundNutsProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return in == out;
}

void SoundNutsProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = buffer.getNumChannels();
    const int n = buffer.getNumSamples();
    if (engines_.empty() || n <= 0 || numCh <= 0)
        return;

    auto raw = [&](const char* id) { return apvts.getRawParameterValue(id)->load(); };

    // ---- transport (cache last known; hosts report partial info) -----------
    bool   isPlaying = false, havePpq = false;
    double ppq = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())            lastKnownBpm_ = *b;
            if (auto sig = pos->getTimeSignature()) { sigNum_ = sig->numerator; sigDen_ = sig->denominator; }
            if (auto q = pos->getPpqPosition())     { ppq = *q; havePpq = true; }
            isPlaying = pos->getIsPlaying();
        }
    bpmUi_.store(lastKnownBpm_);

    // ---- LFOs (block rate, TECH_BRIEF §2 + acv2 spec) ----------------------
    auto configureLfo = [&](acv2::Lfo& l, const char* shape, const char* rateMs,
                            const char* rateSyncId, const char* rateDivId,
                            const char* phase, const char* retrig, const char* bipolar)
    {
        l.setShape((acv2::LfoShape) (int) raw(shape));
        l.setBpm(lastKnownBpm_);
        if (raw(rateSyncId) > 0.5f) l.setSyncedDivision((int) raw(rateDivId));
        else                        l.setRateHz(acv2::tempo::msToHz(raw(rateMs)));
        l.setPhaseOffset(raw(phase));
        l.setRetrigMode(raw(retrig) > 0.5f ? acv2::LfoRetrig::Transport : acv2::LfoRetrig::Free);
        l.setBipolar(raw(bipolar) > 0.5f);
    };
    configureLfo(lfoA_, pid::lfoAShape, pid::lfoARateMs, pid::lfoARateSync,
                 pid::lfoARateDiv, pid::lfoAPhase, pid::lfoARetrig, pid::lfoABipolar);
    configureLfo(lfoB_, pid::lfoBShape, pid::lfoBRateMs, pid::lfoBRateSync,
                 pid::lfoBRateDiv, pid::lfoBPhase, pid::lfoBRetrig, pid::lfoBBipolar);
    if (havePpq)
    {
        lfoA_.setTransportPpq(ppq);
        lfoB_.setTransportPpq(ppq);
    }
    const float aVal = lfoA_.processBlock(n);
    const float bVal = lfoB_.processBlock(n);
    lfoAValue_.store(aVal);
    lfoBValue_.store(bVal);

    // ---- modulated (effective) values: bounded offsets BEFORE the DSP config
    auto mod2 = [&](const char* da, const char* db) { return raw(da) * aVal + raw(db) * bVal; };
    const double macroEff = juce::jlimit(0.0, 1.0, (double) raw(pid::macro) + mod2(pid::macroModA, pid::macroModB));
    const double filterBaseEff = juce::jlimit(20.0, 18000.0,
        (double) raw(pid::filterBase) * std::exp2(2.0 * mod2(pid::filterBaseModA, pid::filterBaseModB)));
    const double tremEff = juce::jlimit(0.0, 1.0, (double) raw(pid::tremAmt) + mod2(pid::tremAmtModA, pid::tremAmtModB));
    const double rateWarp = std::exp2(2.0 * mod2(pid::rateModA, pid::rateModB));

    // ---- system switch + morph crossfade -----------------------------------
    const int sysIdx = juce::jlimit(0, (int) engines_.size() - 1, (int) raw(pid::system));
    if (curSys_ < 0)
    {
        curSys_ = sysIdx;
        engines_[(size_t) curSys_]->retrigger();
    }
    else if (sysIdx != curSys_)
    {
        prevSys_ = curSys_;
        curSys_  = sysIdx;
        engines_[(size_t) curSys_]->retrigger();
        const double morphSec = raw(pid::morphSync) > 0.5f
            ? acv2::tempo::divisionToMs((int) raw(pid::morphDiv), lastKnownBpm_) * 0.001
            : (double) raw(pid::morphMs) * 0.001;
        morphSamplesTotal_ = morphSamplesLeft_ = juce::jmax(1.0, morphSec * sr);
    }

    // ---- orbit tempo-sync: one characteristic orbit per division -----------
    const auto& curSystem = registry_.system(curSys_);
    double rateScale;
    if (raw(pid::rateSync) > 0.5f)
    {
        const double divSec = acv2::tempo::divisionToMs((int) raw(pid::rateDiv), lastKnownBpm_) * 0.001;
        const double natPer = nativePeriodAt(curSystem, macroEff,
                                             nativePeriods_.empty() ? -1.0 : nativePeriods_[(size_t) curSys_]);
        rateScale = syncRateScale(natPer, divSec);
    }
    else
        rateScale = (double) raw(pid::rate);
    rateScale = juce::jlimit(0.01, 32.0, rateScale * rateWarp);

    // ---- bar-locked orbit reset --------------------------------------------
    const int resetChoice = (int) raw(pid::orbitReset);
    if (resetChoice > 0 && isPlaying && havePpq)
    {
        static constexpr int bars[] = { 1, 1, 2, 4, 8 };
        const double quartersPerReset =
            acv2::tempo::barQuarters(sigNum_, sigDen_) * bars[juce::jlimit(0, 4, resetChoice)];
        const auto bar = (long long) std::floor(ppq / juce::jmax(1e-9, quartersPerReset));
        if (!wasPlaying_)
            lastResetBar_ = bar;                     // arm on transport start
        else if (bar != lastResetBar_)
        {
            engines_[(size_t) curSys_]->retrigger(); // downbeat: clean orbit
            lastResetBar_ = bar;
        }
    }
    wasPlaying_ = isPlaying;

    // ---- run the engine(s) --------------------------------------------------
    ModConfig cfg;
    cfg.controlRateHz = 1000.0;
    cfg.rateScale     = rateScale;
    cfg.smoothingMs   = raw(pid::smoothingMs);
    cfg.solver        = (SolverPreference) (int) raw(pid::solver);
    cfg.divergence    = (DivergenceMode) (int) raw(pid::divergence);

    auto& engCur = *engines_[(size_t) curSys_];
    engCur.setConfig(cfg);
    engCur.process(macroEff, n, blockCur_);

    const bool morphing = morphSamplesLeft_ > 0.0 && prevSys_ >= 0 && prevSys_ != curSys_;
    if (morphing)
    {
        auto& engPrev = *engines_[(size_t) prevSys_];
        engPrev.setConfig(cfg);
        engPrev.process(macroEff, n, blockPrev_);
    }

    systemIndexUi_.store(curSys_);
    macroUi_.store(macroEff);
    analyticUi_.store(engCur.usingAnalytic(macroEff));

    // ---- audio path ---------------------------------------------------------
    const double filterAmt = raw(pid::filterAmt);
    const double reso      = raw(pid::reso);
    const double panAmt    = raw(pid::panAmt);
    const double mix       = raw(pid::mix);
    const double outGain   = raw(pid::output);

    const int dimCur  = blockCur_.channels;
    const int dimPrev = morphing ? blockPrev_.channels : 0;

    float* L = buffer.getWritePointer(0);
    float* R = numCh > 1 ? buffer.getWritePointer(1) : nullptr;

    int sp = scopePos.load();
    for (int i = 0; i < n; ++i)
    {
        auto chan = [&](const soundnuts::ModBlock& blk, int dim, int c) -> double
        {
            if (dim <= 0) return 0.0;
            if (c < dim)  return blk.out[(size_t) c][(size_t) i];
            return (c == 2 && dim > 1) ? blk.out[1][(size_t) i] : 0.0;  // z falls back to y
        };

        double mx = chan(blockCur_, dimCur, 0);
        double my = chan(blockCur_, dimCur, 1);
        double mz = chan(blockCur_, dimCur, 2);
        if (morphing && morphSamplesLeft_ > 0.0)
        {
            const double w = morphSamplesLeft_ / juce::jmax(1.0, morphSamplesTotal_); // 1 -> 0
            mx = (1.0 - w) * mx + w * chan(blockPrev_, dimPrev, 0);
            my = (1.0 - w) * my + w * chan(blockPrev_, dimPrev, 1);
            mz = (1.0 - w) * mz + w * chan(blockPrev_, dimPrev, 2);
            morphSamplesLeft_ -= 1.0;
        }

        // filter cutoff swept +/- 4 octaves about the base by mx
        const double cutoff = filterBaseEff * std::pow(2.0, mx * filterAmt * 4.0);
        svfL.set(cutoff, sr, reso);
        svfR.g = svfL.g; svfR.k = svfL.k; svfR.a1 = svfL.a1; svfR.a2 = svfL.a2; svfR.a3 = svfL.a3;

        // tremolo (attenuation LFO) from my, auto-pan from mz
        const double trem = 1.0 - tremEff * (0.5 * (1.0 - my));
        const double panAngle = 0.25 * juce::MathConstants<double>::pi * (1.0 + panAmt * mz); // 0..pi/2
        const double gL = std::cos(panAngle), gR = std::sin(panAngle);

        const double dryL = L[i];
        const double dryR = R ? R[i] : L[i];
        double wetL = svfL.process(dryL) * trem;
        double wetR = svfR.process(dryR) * trem;
        // equal-power pan (mix of the two wet channels toward L/R)
        const double monoW = 0.5 * (wetL + wetR);
        wetL = monoW * gL * 1.41421356; // sqrt2 keeps center unity-ish
        wetR = monoW * gR * 1.41421356;

        double oL = (mix * wetL + (1.0 - mix) * dryL) * outGain;
        double oR = (mix * wetR + (1.0 - mix) * dryR) * outGain;
        if (!std::isfinite(oL)) oL = 0.0;
        if (!std::isfinite(oR)) oR = 0.0;
        L[i] = (float) juce::jlimit(-1.5, 1.5, oL);
        if (R) R[i] = (float) juce::jlimit(-1.5, 1.5, oR);

        scopeX[(size_t) sp] = (float) mx;
        scopeY[(size_t) sp] = (float) my;
        sp = (sp + 1) % kScope;
    }
    scopePos.store(sp);

    if (morphing && morphSamplesLeft_ <= 0.0)
        prevSys_ = -1;
}

void SoundNutsProcessor::copyModHistory(std::vector<float>& x, std::vector<float>& y) const
{
    x.resize(kScope); y.resize(kScope);
    const int sp = scopePos.load();
    for (int i = 0; i < kScope; ++i)
    {
        const int idx = (sp + i) % kScope;
        x[(size_t) i] = scopeX[(size_t) idx];
        y[(size_t) i] = scopeY[(size_t) idx];
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SoundNutsProcessor(); }
