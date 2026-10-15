// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// =============================================================================
//  soundnuts_shot.cpp — offscreen UI screenshot tool for SoundNuts II.
//
//  Links against the juce_add_plugin SHARED-CODE target (SoundNutsII) so it
//  gets the processor, the foleys editor, every JUCE module and BinaryData
//  WITHOUT building or installing any VST3/AU/Standalone bundle.
//
//  It instantiates the processor, drives real audio through processBlock,
//  puts the editor on the desktop (off-screen) so that isShowing() is true and
//  the analyser timers actually fire, then writes one 2x PNG per UI state.
//
//  Nothing in here is part of the plugin build; it is a developer tool.
//  Usage:  soundnuts_shot [output-directory]
//          soundnuts_shot --selftest     operate every control, check the wiring
//  SOUNDNUTS_SHOT_SIZE=WxH renders every state at that size (e.g. the minimum).
// =============================================================================

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "WiringTest.h"

#include <CoreFoundation/CoreFoundation.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <vector>

// -----------------------------------------------------------------------------
//  Message-loop pump.
//  juce::MessageManager::runDispatchLoopUntil() does not exist in this build
//  (JUCE_MODAL_LOOPS_PERMITTED == 0). JUCE's mac message queue installs its
//  CFRunLoopSource on kCFRunLoopCommonModes and juce::Timer delivery rides that
//  same queue, so spinning the default run loop is enough.
// -----------------------------------------------------------------------------
static void pump (int ms)
{
    const auto end = juce::Time::currentTimeMillis() + (juce::int64) ms;
    while (juce::Time::currentTimeMillis() < end)
        CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.01, true);
}

// -----------------------------------------------------------------------------
//  Playhead: a steady 128 bpm 4/4 transport that is always "playing", so the
//  RegimeReadout shows a real bpm and bar-locked orbit reset / beat-locked LFOs
//  behave the way they do in a host.
// -----------------------------------------------------------------------------
struct ShotPlayHead final : juce::AudioPlayHead
{
    double ppq = 0.0;
    static constexpr double bpm = 128.0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setTimeSignature (TimeSignature { 4, 4 });
        p.setPpqPosition (ppq);
        p.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        p.setIsPlaying (true);
        p.setIsRecording (false);
        p.setTimeInSeconds (ppq * 60.0 / bpm);
        return p;
    }
};

// -----------------------------------------------------------------------------
//  Parameter helper — mirrors applyFactoryPreset(), so host-notify + APVTS
//  listeners + the async view-property publish all fire.
// -----------------------------------------------------------------------------
static void setP (SoundNutsProcessor& p, const char* id, float plain)
{
    if (auto* rp = p.state().getParameter (id))
    {
        rp->beginChangeGesture();
        rp->setValueNotifyingHost (rp->convertTo0to1 (plain));
        rp->endChangeGesture();
    }
    else
    {
        std::printf ("WARN unknown parameter '%s'\n", id);
    }
}

// -----------------------------------------------------------------------------
//  Recursive component search helpers.
// -----------------------------------------------------------------------------
template <typename T, typename Fn>
static void forEachDescendant (juce::Component& root, Fn&& fn)
{
    for (auto* c : root.getChildren())
    {
        if (auto* t = dynamic_cast<T*> (c))
            fn (t);
        forEachDescendant<T> (*c, fn);
    }
}

// foleys::RootItem owns a juce::TooltipWindow as a CHILD of the editor, so a
// visible tip would be baked straight into the snapshot. Kill it every time.
static void killTooltips (juce::Component& ed)
{
    forEachDescendant<juce::TooltipWindow> (ed, [] (juce::TooltipWindow* t)
    {
        t->hideTip();
        t->setVisible (false);
    });
}

// -----------------------------------------------------------------------------
//  Non-blankness metric, computed in-tool on the snapshot we just wrote.
// -----------------------------------------------------------------------------
struct ImageStats
{
    double lumaStdDev = 0.0;
    int    distinctRgb = 0;
    int    distinctLumaBuckets = 0;
};

static ImageStats measure (const juce::Image& img)
{
    ImageStats s;
    if (! img.isValid())
        return s;

    const juce::Image::BitmapData bits (img, juce::Image::BitmapData::readOnly);
    std::set<juce::uint32> rgbs;
    bool lumaSeen[256] = {};

    double sum = 0.0, sumSq = 0.0;
    juce::int64 n = 0;

    for (int y = 0; y < img.getHeight(); y += 7)
    {
        for (int x = 0; x < img.getWidth(); x += 7)
        {
            const auto c = bits.getPixelColour (x, y);
            const auto r = c.getRed(), g = c.getGreen(), b = c.getBlue();
            const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            sum   += luma;
            sumSq += luma * luma;
            ++n;

            if (rgbs.size() < 100000)
                rgbs.insert (((juce::uint32) r << 16) | ((juce::uint32) g << 8) | (juce::uint32) b);
            lumaSeen[(int) juce::jlimit (0.0, 255.0, luma)] = true;
        }
    }

    if (n > 0)
    {
        const double mean = sum / (double) n;
        s.lumaStdDev = std::sqrt (juce::jmax (0.0, sumSq / (double) n - mean * mean));
    }
    s.distinctRgb = (int) rgbs.size();
    for (bool seen : lumaSeen)
        if (seen) ++s.distinctLumaBuckets;
    return s;
}

// -----------------------------------------------------------------------------
//  Control wiring self-test (--selftest). The editor stays OFFSCREEN (no peer):
//  with a native window every pump waits on a display refresh.
// -----------------------------------------------------------------------------
static int runSelfTest()
{
    auto proc = std::make_unique<SoundNutsProcessor>();
    ShotPlayHead head;
    proc->setPlayHead (&head);
    proc->setPlayConfigDetails (2, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
    if (ed == nullptr) { std::printf ("FAIL no editor\n"); return 1; }
    ed->setVisible (true);
    pump (1400);

    if (auto* size = std::getenv ("SOUNDNUTS_SHOT_SIZE"))
    {
        const auto wh = juce::StringArray::fromTokens (juce::String (size), "x", {});
        if (wh.size() == 2) ed->setSize (wh[0].getIntValue(), wh[1].getIntValue());
        pump (300);
    }
    std::printf ("editor size: %d x %d\n", ed->getWidth(), ed->getHeight());

    acv2::test::WiringTest test { *proc, [] (int ms) { pump (ms); }, {}, 0 };
    test.run (*ed, "as opened, times in ms");

    for (auto* id : { pid::rateSync, pid::morphSync, pid::lfoARateSync, pid::lfoBRateSync })
        setP (*proc, id, 1.0f);
    pump (300);
    test.run (*ed, "times in beats");

    const int rc = test.report ({ pid::system });   // chosen in the system browser list

    ed.reset();
    pump (200);
    proc->setPlayHead (nullptr);
    proc->releaseResources();
    proc.reset();
    pump (200);
    return rc;
}

// -----------------------------------------------------------------------------
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // declared first: outlives everything

    // default output: ./shots under the current directory (pass a path to change it)
    const juce::String defaultOut = juce::File::getCurrentWorkingDirectory().getChildFile ("shots").getFullPathName();

    constexpr double sr    = 48000.0;
    constexpr int    block = 512;

    // The foleys ToolBox (FOLEYS_SHOW_GUI_EDITOR_PALLETTE=1) autosaves to
    // MagicGUIState::getResourcesFolder() every 10 s, i.e. plugin/Resources,
    // as magic.sav(N).xml. Remember what was there so we can remove only the
    // files THIS run created and leave any pre-existing ones alone.
    const juce::File resourcesDir = juce::File (juce::String (__FILE__))
                                        .getParentDirectory()      // tools
                                        .getParentDirectory()      // plugin
                                        .getChildFile ("Resources");
    juce::StringArray preExistingSaves;
    for (const auto& f : resourcesDir.findChildFiles (juce::File::findFiles, false, "magic.sav*.xml"))
        preExistingSaves.add (f.getFileName());

    auto removeNewAutosaves = [&]
    {
        for (const auto& f : resourcesDir.findChildFiles (juce::File::findFiles, false, "magic.sav*.xml"))
            if (! preExistingSaves.contains (f.getFileName()))
            {
                std::printf ("CLEANUP removing ToolBox autosave %s\n", f.getFileName().toRawUTF8());
                f.deleteFile();
            }
    };

    if (argc > 1 && juce::String (argv[1]) == "--selftest")
    {
        const int rc = runSelfTest();
        removeNewAutosaves();
        return rc;
    }

    juce::String outPath (argc > 1 ? juce::String (juce::CharPointer_UTF8 (argv[1])) : defaultOut);
    const juce::File outDir = outPath.startsWithChar ('/')
                                ? juce::File (outPath)
                                : juce::File::getCurrentWorkingDirectory().getChildFile (outPath);
    outDir.createDirectory();
    std::printf ("OUTDIR %s\n", outDir.getFullPathName().toRawUTF8());

    int failures = 0;

    {
        auto proc = std::make_unique<SoundNutsProcessor>();
        ShotPlayHead head;
        proc->setPlayHead (&head);
        proc->setPlayConfigDetails (2, 2, sr, block);
        proc->prepareToPlay (sr, block);        // builds 94 engines — slow, once

        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        double phase = 0.0;
        juce::Random rng (0x50D9BEA7);

        // 220 Hz saw + a little noise at -12 dBFS-ish. The visuals plot the
        // MODULATION trajectory, not the audio, but the DSP path should run on
        // finite, non-silent data.
        auto renderOneBlock = [&]
        {
            const double inc = 220.0 / sr;
            for (int i = 0; i < block; ++i)
            {
                phase += inc;
                if (phase >= 1.0) phase -= 1.0;
                const float s = (float) (0.25 * (2.0 * phase - 1.0))
                              + 0.01f * (rng.nextFloat() * 2.0f - 1.0f);
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s * 0.9f);
            }
            midi.clear();
            proc->processBlock (buffer, midi);
            head.ppq += ((double) block / sr) * (ShotPlayHead::bpm / 60.0);
        };

        auto drive = [&] (int rounds)
        {
            for (int i = 0; i < rounds; ++i)
            {
                for (int k = 0; k < 4; ++k)
                    renderOneBlock();
                pump (20);
            }
        };

        // ---- editor ---------------------------------------------------------
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
        if (ed == nullptr)
        {
            std::printf ("FAIL createEditor() returned nullptr\n");
            return 1;
        }

        // Every custom component's timerCallback() bails out unless
        // isShowing(), which needs a real (non-minimised) peer. Park a holder
        // window far off-screen so the physical mouse can never hover it.
        auto holder = std::make_unique<juce::Component>();
        holder->setBounds (-6000, -6000, ed->getWidth(), ed->getHeight());
        holder->addToDesktop (0);
        holder->setVisible (true);
        holder->addAndMakeVisible (*ed);
        ed->setTopLeftPosition (0, 0);
        ed->setVisible (true);

        auto syncHolder = [&]
        {
            holder->setBounds (-6000, -6000, ed->getWidth(), ed->getHeight());
            ed->setTopLeftPosition (0, 0);
        };

        // The foleys ToolBox is created via callAsync and its ctor rebuilds the
        // whole GuiItem tree. Pump generously BEFORE touching any child.
        pump (1400);
        if (auto* size = std::getenv ("SOUNDNUTS_SHOT_SIZE"))
        {
            const auto wh = juce::StringArray::fromTokens (juce::String (size), "x", {});
            if (wh.size() == 2) ed->setSize (wh[0].getIntValue(), wh[1].getIntValue());
            pump (300);
        }
        syncHolder();
        std::printf ("EDITOR %d x %d\n", ed->getWidth(), ed->getHeight());
        if (ed->getWidth() <= 0 || ed->getHeight() <= 0)
        {
            std::printf ("FAIL editor has zero size\n");
            return 1;
        }

        // ---- snapshot ------------------------------------------------------
        auto shoot = [&] (const juce::String& fileName, const juce::String& stateName) -> bool
        {
            killTooltips (*ed);
            pump (60);
            killTooltips (*ed);

            const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            if (! img.isValid())
            {
                std::printf ("FAIL STATE %s — invalid snapshot image\n", stateName.toRawUTF8());
                return false;
            }

            auto file = outDir.getChildFile (fileName);
            file.deleteFile();

            {
                juce::FileOutputStream os (file);
                if (! os.openedOk())
                {
                    std::printf ("FAIL STATE %s — cannot open %s\n",
                                 stateName.toRawUTF8(), file.getFullPathName().toRawUTF8());
                    return false;
                }
                os.setPosition (0);
                os.truncate();
                juce::PNGImageFormat png;
                if (! png.writeImageToStream (img, os))
                {
                    std::printf ("FAIL STATE %s — PNG encode failed\n", stateName.toRawUTF8());
                    return false;
                }
            }

            int visSliders = 0, visCombos = 0;
            forEachDescendant<juce::Slider>   (*ed, [&] (juce::Slider* s)   { if (s->isVisible()) ++visSliders; });
            forEachDescendant<juce::ComboBox> (*ed, [&] (juce::ComboBox* c) { if (c->isVisible()) ++visCombos; });
            std::printf ("  swap-check visibleSliders=%d visibleComboBoxes=%d\n", visSliders, visCombos);

            const auto st = measure (img);
            std::printf ("STATE %s %dx%d variance=%.2f distinctRgb=%d lumaBuckets=%d bytes=%d file=%s\n",
                         stateName.toRawUTF8(), img.getWidth(), img.getHeight(),
                         st.lumaStdDev, st.distinctRgb, st.distinctLumaBuckets,
                         (int) file.getSize(), file.getFullPathName().toRawUTF8());

            const bool ok = (st.lumaStdDev > 6.0) && (st.distinctRgb >= 400);
            if (! ok)
                std::printf ("FAIL STATE %s — image is effectively uniform "
                             "(variance=%.2f distinctRgb=%d)\n",
                             stateName.toRawUTF8(), st.lumaStdDev, st.distinctRgb);
            return ok;
        };

        auto record = [&] (bool ok) { if (! ok) ++failures; };

        // Baseline used by several states.
        auto applyBaseline = [&]
        {
            setP (*proc, pid::system,      0.0f);    // Lorenz
            setP (*proc, pid::macro,       0.75f);   // amber / chaos
            setP (*proc, pid::rate,        8.0f);    // fills the 4096-sample scope
            setP (*proc, pid::filterBase,  900.0f);
            setP (*proc, pid::filterAmt,   0.6f);
            setP (*proc, pid::reso,        0.30f);
            setP (*proc, pid::tremAmt,     0.40f);
            setP (*proc, pid::panAmt,      0.50f);
            setP (*proc, pid::smoothingMs, 3.0f);
            setP (*proc, pid::mix,         1.0f);
            setP (*proc, pid::output,      1.0f);
            setP (*proc, pid::lfoAShape,   0.0f);
            setP (*proc, pid::lfoARateMs,  500.0f);
            setP (*proc, pid::lfoAPhase,   0.25f);
            setP (*proc, pid::lfoBShape,   6.0f);
            setP (*proc, pid::lfoBRateMs,  3000.0f);
            setP (*proc, pid::lfoBPhase,   0.6f);
        };

        // foleys_GuiItem.cpp:290 only calls setVisible() when the bound
        // visibility Value CHANGES — a freshly (re)built GuiItem tree leaves
        // BOTH children of every class="swap" View visible, so the ComboBox
        // sits on top of the Slider no matter what the sync bools say.
        // Force a real transition (opposite -> wanted) right before the shot.
        auto applySyncFlags = [&] (bool rateS, bool morphS, bool lfoAS, bool lfoBS)
        {
            setP (*proc, pid::rateSync,     rateS  ? 0.0f : 1.0f);
            setP (*proc, pid::morphSync,    morphS ? 0.0f : 1.0f);
            setP (*proc, pid::lfoARateSync, lfoAS  ? 0.0f : 1.0f);
            setP (*proc, pid::lfoBRateSync, lfoBS  ? 0.0f : 1.0f);
            pump (250);
            setP (*proc, pid::rateSync,     rateS  ? 1.0f : 0.0f);
            setP (*proc, pid::morphSync,    morphS ? 1.0f : 0.0f);
            setP (*proc, pid::lfoARateSync, lfoAS  ? 1.0f : 0.0f);
            setP (*proc, pid::lfoBRateSync, lfoBS  ? 1.0f : 0.0f);
            pump (250);
        };

        // The browser's search field is plain UI state, not a parameter, so it
        // survives applyFactoryPreset(). Reset it explicitly between states.
        auto setSearchText = [&] (const juce::String& text)
        {
            juce::TextEditor* searchBox = nullptr;
            forEachDescendant<juce::TextEditor> (*ed, [&] (juce::TextEditor* t)
            {
                const auto tl = ed->getLocalPoint (t, juce::Point<int> (0, 0));
                if (tl.x < 260 && tl.y > 40 && searchBox == nullptr)
                    searchBox = t;
            });
            if (searchBox != nullptr)
                searchBox->setText (text, juce::sendNotification);
            else
                std::printf ("WARN left-rail search TextEditor not found\n");
            return searchBox != nullptr;
        };

        // =====================================================================
        //  1 — Default / Init (Lorenz in chaos, milliseconds mode)
        // =====================================================================
        proc->applyFactoryPreset (0);
        applyBaseline();
        pump (300);
        drive (36);
        applySyncFlags (false, false, false, false);   // milliseconds / rotary layout
        drive (22);
        record (shoot ("01_default-init-lorenz-chaos.png",
                       "1 — Default / Init (Lorenz in chaos, milliseconds mode)"));

        // =====================================================================
        //  2 — Tempo-sync mode: all four beat chips ON (the layout swap)
        // =====================================================================
        setP (*proc, pid::rateDiv,     5.0f);    // 1/8
        setP (*proc, pid::morphDiv,    8.0f);    // 1/4
        setP (*proc, pid::lfoARateDiv, 2.0f);    // 1/16
        setP (*proc, pid::lfoBRateDiv, 11.0f);   // 1/2
        pump (200);
        drive (36);
        applySyncFlags (true, true, true, true);       // beats / ComboBox layout
        drive (8);
        record (shoot ("02_tempo-sync-beat-chips.png",
                       "2 — Tempo-sync mode: all four beat chips ON (the layout swap)"));

        // =====================================================================
        //  3 — Analytic pendulum (closed-form badge + a different terrain)
        // =====================================================================
        proc->applyFactoryPreset (0);
        applyBaseline();
        pump (200);
        setP (*proc, pid::system,     2.0f);   // Pendulum
        setP (*proc, pid::solver,     2.0f);   // Force Analytic
        setP (*proc, pid::macro,      0.50f);
        setP (*proc, pid::rateDiv,    0.0f);   // 1/32 -> syncRateScale caps at 16
        setP (*proc, pid::divergence, 0.0f);
        setP (*proc, pid::tremAmt,    0.90f);
        setP (*proc, pid::filterAmt,  0.20f);
        setP (*proc, pid::panAmt,     0.60f);
        applySyncFlags (true, false, false, false);    // orbit beat-locked, rest in ms
        pump (200);
        drive (44);
        applySyncFlags (true, false, false, false);
        drive (8);
        record (shoot ("03_analytic-pendulum.png",
                       "3 — Analytic pendulum (closed-form badge + a different terrain)"));

        // =====================================================================
        //  4 — Factory preset 'Edge Riser'
        // =====================================================================
        proc->applyFactoryPreset (5);
        setP (*proc, pid::rate, 8.0f);         // so the scope actually fills
        pump (400);
        drive (44);
        applySyncFlags (false, false, false, false);
        drive (8);
        record (shoot ("04_factory-preset-edge-riser.png",
                       "4 — Factory preset 'Edge Riser' (Rossler, macro at the far edge, bar-locked reset)"));

        // =====================================================================
        //  5 — Mod matrix engaged + favourites pinned + search filter
        // =====================================================================
        proc->applyFactoryPreset (0);
        applyBaseline();
        proc->setFavorite ("lorenz",  true);
        proc->setFavorite ("thomas",  true);
        proc->setFavorite ("rossler", true);

        setP (*proc, pid::lfoAShape,      5.0f);    // S&H
        setP (*proc, pid::lfoARateMs,     250.0f);
        setP (*proc, pid::lfoBShape,      7.0f);    // Drift
        setP (*proc, pid::lfoBRateMs,     6000.0f);
        setP (*proc, pid::macroModA,       0.45f);
        setP (*proc, pid::macroModB,      -0.30f);
        setP (*proc, pid::filterBaseModA,  0.60f);
        setP (*proc, pid::filterBaseModB,  0.25f);
        setP (*proc, pid::tremAmtModA,    -0.50f);
        setP (*proc, pid::tremAmtModB,     0.35f);
        setP (*proc, pid::rateModA,        0.80f);
        setP (*proc, pid::rateModB,       -0.20f);
        setP (*proc, pid::macro,           0.55f);
        setP (*proc, pid::rate,            8.0f);
        pump (400);

        // The browser only rebuilds on construction / search-text change / star
        // click, so poke the search field to pick up the new favourites too.
        // Re-find the TextEditor each time (earlier pointers may be stale).
        if (setSearchText ("lo"))
            std::printf ("INFO search field found and filtered\n");
        pump (300);
        drive (44);
        applySyncFlags (false, false, false, false);
        drive (8);
        record (shoot ("05_mod-matrix-favourites-search.png",
                       "5 — Mod matrix fully engaged + favourites pinned + search filter (browser at work)"));

        // =====================================================================
        //  6 — Minimum size 1040x700 (responsive / density audit)
        // =====================================================================
        proc->applyFactoryPreset (0);
        applyBaseline();
        setSearchText ({});          // clear the state-5 filter
        pump (300);
        ed->setSize (1040, 700);
        syncHolder();
        pump (500);
        drive (36);
        applySyncFlags (false, false, false, false);
        drive (8);
        std::printf ("EDITOR (min) %d x %d\n", ed->getWidth(), ed->getHeight());
        record (shoot ("06_min-size-1040x700.png",
                       "6 — Minimum size 1040x700 (responsive / density audit)"));

        // restore + clean teardown (lets the ToolBox dtor remove its autosave)
        ed->setSize (1200, 780);
        syncHolder();
        pump (200);

        ed->setVisible (false);
        holder->removeChildComponent (ed.get());
        ed.reset();
        holder->removeFromDesktop();
        holder.reset();
        pump (200);
        proc->setPlayHead (nullptr);
        proc->releaseResources();
        proc.reset();
        pump (200);
    }

    removeNewAutosaves();

    if (failures == 0)
        std::printf ("ALL STATES OK\n");
    else
        std::printf ("FAILURES %d\n", failures);

    return failures == 0 ? 0 : 2;
}
