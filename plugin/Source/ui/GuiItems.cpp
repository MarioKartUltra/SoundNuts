// =============================================================================
//  SoundNuts II — ui/GuiItems.cpp
// =============================================================================
#include "GuiItems.h"
#include "../PluginProcessor.h"
#include "acv2/ui/Theme.h"

namespace snutsui
{
using namespace juce;

// ---- theme shortcuts (acid-chartreuse on blue-black) ------------------------
namespace th
{
    inline Colour c(uint32_t argb) { return Colour(argb); }
    inline Colour background()  { return c(acv2::theme::kSoundNuts.background); }
    inline Colour hero()        { return c(acv2::theme::kSoundNuts.hero); }
    inline Colour panel()       { return c(acv2::theme::kSoundNuts.panel); }
    inline Colour outline()     { return c(acv2::theme::kSoundNuts.outline); }
    inline Colour accent()      { return c(acv2::theme::kSoundNuts.accent); }
    inline Colour accentDim()   { return c(acv2::theme::kSoundNuts.accentDim); }
    inline Colour text()        { return c(acv2::theme::kSoundNuts.textPrimary); }
    inline Colour text2()       { return c(acv2::theme::kSoundNuts.textSecondary); }

    inline Colour regime(soundnuts::Regime r)
    {
        switch (r)
        {
            case soundnuts::Regime::FixedPoint: return c(0xFF4A5468);
            case soundnuts::Regime::LimitCycle: return c(0xFF57B860);
            case soundnuts::Regime::Chaos:      return c(0xFFD79A3A);
            case soundnuts::Regime::Divergent:  return c(0xFFD9503C);
        }
        return Colours::black;
    }

    inline Font mono(float h)  { return Font(FontOptions(Font::getDefaultMonospacedFontName(), h, Font::plain)); }
    inline Font label(float h) { return Font(FontOptions(h)); }
    inline Font bold(float h)  { return Font(FontOptions(h).withStyle("Bold")); }

    // The abstract identity mark: three nested orbit ellipses + a satellite dot.
    inline void drawOrbitSigil(Graphics& g, Rectangle<float> r, Colour col, float alpha)
    {
        const auto cx = r.getCentreX(), cy = r.getCentreY();
        const float R = jmin(r.getWidth(), r.getHeight()) * 0.5f;
        for (int i = 0; i < 3; ++i)
        {
            const float rot = MathConstants<float>::pi * (float) i / 3.0f;
            Path e;
            e.addEllipse(cx - R, cy - R * 0.42f, R * 2.0f, R * 0.84f);
            e.applyTransform(AffineTransform::rotation(rot, cx, cy));
            g.setColour(col.withAlpha(alpha));
            g.strokePath(e, PathStrokeType(jmax(1.0f, R * 0.06f)));
        }
        g.setColour(col.withAlpha(jmin(1.0f, alpha * 2.5f)));
        const float dr = jmax(1.5f, R * 0.09f);
        g.fillEllipse(cx + R * 0.62f - dr, cy - R * 0.30f - dr, dr * 2.0f, dr * 2.0f);
    }
}

static SoundNutsProcessor* getProc(foleys::GuiItem& item)
{
    return dynamic_cast<SoundNutsProcessor*>(item.getMagicState().getProcessor());
}

// =============================================================================
//  Phase scope — the hero visual. Attractor trajectory (x vs y), trail fading
//  with age, regime-agnostic; ANALYTIC chip when the closed form is running.
// =============================================================================
class PhaseScopeComponent : public Component, private Timer, public SettableTooltipClient
{
public:
    explicit PhaseScopeComponent(SoundNutsProcessor* p) : proc(p) { startTimerHz(30); }

    void paint(Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(th::hero());
        g.fillRoundedRectangle(b, 6.0f);

        // grid + identity watermark
        g.setColour(th::outline().withAlpha(0.55f));
        g.drawHorizontalLine((int) b.getCentreY(), b.getX() + 4, b.getRight() - 4);
        g.drawVerticalLine((int) b.getCentreX(), b.getY() + 4, b.getBottom() - 4);
        th::drawOrbitSigil(g, b.reduced(b.getWidth() * 0.30f, b.getHeight() * 0.30f),
                           th::accent(), 0.05f);

        const int N = (int) xs.size();
        if (N > 2 && (int) ys.size() == N)
        {
            const float cx = b.getCentreX(), cy = b.getCentreY();
            const float rx = b.getWidth() * 0.46f, ry = b.getHeight() * 0.46f;
            auto px = [&](float v) { return cx + jlimit(-1.0f, 1.0f, v) * rx; };
            auto py = [&](float v) { return cy - jlimit(-1.0f, 1.0f, v) * ry; };

            for (int i = 2; i < N; i += 2)
            {
                const float a = (float) i / (float) N;      // newer = brighter
                g.setColour(th::accent().withAlpha(a * a * 0.9f));
                g.drawLine(px(xs[(size_t)(i - 2)]), py(ys[(size_t)(i - 2)]),
                           px(xs[(size_t) i]),      py(ys[(size_t) i]), 1.2f);
            }
            g.setColour(th::text());
            g.fillEllipse(px(xs[(size_t)(N - 1)]) - 2.5f, py(ys[(size_t)(N - 1)]) - 2.5f, 5.0f, 5.0f);
        }

        if (analytic)   // closed-form badge
        {
            auto chip = b.reduced(8.0f).removeFromTop(16.0f).removeFromRight(72.0f);
            g.setColour(th::accentDim().withAlpha(0.85f));
            g.fillRoundedRectangle(chip, 4.0f);
            g.setColour(th::hero());
            g.setFont(th::bold(10.0f));
            g.drawText("ANALYTIC", chip, Justification::centred, false);
        }

        g.setColour(th::outline());
        g.drawRoundedRectangle(b.reduced(0.5f), 6.0f, 1.0f);
    }

private:
    void timerCallback() override
    {
        if (proc == nullptr || ! isShowing()) return;
        proc->copyModHistory(xs, ys);
        analytic = proc->currentAnalytic();
        repaint();
    }

    SoundNutsProcessor* proc = nullptr;
    std::vector<float> xs, ys;
    bool analytic = false;
};

class PhaseScopeItem : public foleys::GuiItem
{
public:
    FOLEYS_DECLARE_GUI_FACTORY(PhaseScopeItem)

    PhaseScopeItem(foleys::MagicGUIBuilder& b, const ValueTree& node) : GuiItem(b, node)
    {
        comp = std::make_unique<PhaseScopeComponent>(getProc(*this));
        addAndMakeVisible(*comp);
    }
    void update() override {}
    Component* getWrappedComponent() override { return comp.get(); }

private:
    std::unique_ptr<PhaseScopeComponent> comp;
};

// =============================================================================
//  Regime strip — the bifurcation terrain under the Macro. Click / drag jumps
//  the macro (click-to-jump creative feature). Marker = EFFECTIVE macro
//  (post-LFO), so modulation is visible on the terrain.
// =============================================================================
class RegimeStripComponent : public Component, private Timer, public SettableTooltipClient
{
public:
    explicit RegimeStripComponent(SoundNutsProcessor* p) : proc(p)
    {
        if (proc != nullptr)
            if (auto* par = proc->state().getParameter("macro"))
                attachment = std::make_unique<ParameterAttachment>(*par, [](float) {});
        startTimerHz(20);
    }

    void paint(Graphics& g) override
    {
        auto b = getLocalBounds();
        if (proc == nullptr) { g.fillAll(th::panel()); return; }

        const auto& map = proc->registry().system(sysShown).regimeMap();
        const int w = jmax(1, b.getWidth());
        for (int x = 0; x < w; ++x)
        {
            const double bif = (double) x / jmax(1, w - 1);
            g.setColour(th::regime(map.sampleAt(bif).regime).withAlpha(0.85f));
            g.fillRect(b.getX() + x, b.getY(), 1, b.getHeight());
        }
        // effective-macro marker
        const int mx = b.getX() + (int) (macroShown * (w - 1));
        g.setColour(th::text());
        g.fillRect(mx - 1, b.getY(), 2, b.getHeight());
        g.setColour(th::outline());
        g.drawRect(b, 1);
    }

    void mouseDown(const MouseEvent& e) override
    {
        if (attachment == nullptr) return;
        attachment->beginGesture();
        drag(e);
    }
    void mouseDrag(const MouseEvent& e) override { drag(e); }
    void mouseUp(const MouseEvent&) override
    {
        if (attachment != nullptr) attachment->endGesture();
    }

private:
    void drag(const MouseEvent& e)
    {
        if (attachment == nullptr) return;
        const float v = jlimit(0.0f, 1.0f, (float) e.x / (float) jmax(1, getWidth() - 1));
        attachment->setValueAsPartOfGesture(v);
    }

    void timerCallback() override
    {
        if (proc == nullptr || ! isShowing()) return;
        const int    s = proc->currentSystemIndex();
        const double m = proc->currentMacro();
        if (s != sysShown || std::abs(m - macroShown) > 1e-4)
        {
            sysShown = s;
            macroShown = m;
            repaint();
        }
    }

    SoundNutsProcessor* proc = nullptr;
    std::unique_ptr<ParameterAttachment> attachment;
    int sysShown = 0;
    double macroShown = 0.5;
};

class RegimeStripItem : public foleys::GuiItem
{
public:
    FOLEYS_DECLARE_GUI_FACTORY(RegimeStripItem)

    RegimeStripItem(foleys::MagicGUIBuilder& b, const ValueTree& node) : GuiItem(b, node)
    {
        comp = std::make_unique<RegimeStripComponent>(getProc(*this));
        addAndMakeVisible(*comp);
    }
    void update() override {}
    Component* getWrappedComponent() override { return comp.get(); }

private:
    std::unique_ptr<RegimeStripComponent> comp;
};

// =============================================================================
//  Regime readout — system · regime · λ · period · host tempo. Mono figures.
// =============================================================================
class RegimeReadoutComponent : public Component, private Timer, public SettableTooltipClient
{
public:
    explicit RegimeReadoutComponent(SoundNutsProcessor* p) : proc(p) { startTimerHz(10); }

    void paint(Graphics& g) override
    {
        g.setColour(th::text2());
        g.setFont(th::mono(12.0f));
        g.drawText(line, getLocalBounds().reduced(4, 0), Justification::centredLeft, false);
    }

private:
    void timerCallback() override
    {
        if (proc == nullptr || ! isShowing()) return;
        const int sysIdx = proc->currentSystemIndex();
        const double macro = proc->currentMacro();
        const auto pt = proc->registry().system(sysIdx).regimeMap().sampleAt(macro);
        const char* rn = pt.regime == soundnuts::Regime::FixedPoint ? "fixed point"
                       : pt.regime == soundnuts::Regime::LimitCycle ? "limit cycle"
                       : pt.regime == soundnuts::Regime::Chaos      ? "chaos" : "divergent";
        String s;
        s << proc->registry().name(sysIdx) << "   " << rn
          << "   \xce\xbb=" << String(pt.largestLyapunov, 2)
          << (pt.period > 0.0 ? "   T=" + String(pt.period, 2) : String("   aperiodic"))
          << "   " << String(proc->currentBpm(), 1) << " bpm";
        if (s != line) { line = s; repaint(); }
    }

    SoundNutsProcessor* proc = nullptr;
    String line;
};

class RegimeReadoutItem : public foleys::GuiItem
{
public:
    FOLEYS_DECLARE_GUI_FACTORY(RegimeReadoutItem)

    RegimeReadoutItem(foleys::MagicGUIBuilder& b, const ValueTree& node) : GuiItem(b, node)
    {
        comp = std::make_unique<RegimeReadoutComponent>(getProc(*this));
        addAndMakeVisible(*comp);
    }
    void update() override {}
    Component* getWrappedComponent() override { return comp.get(); }

private:
    std::unique_ptr<RegimeReadoutComponent> comp;
};

// =============================================================================
//  System browser — 94 systems need findability: search box + starred
//  favorites pinned on top + click-to-select. Favorites persist in state.
// =============================================================================
class SystemBrowserComponent : public Component,
                               private ListBoxModel,
                               private Timer,
                               public SettableTooltipClient
{
public:
    explicit SystemBrowserComponent(SoundNutsProcessor* p) : proc(p)
    {
        search.setTextToShowWhenEmpty("search systems...", th::text2().withAlpha(0.6f));
        search.setFont(th::label(13.0f));
        search.setColour(TextEditor::backgroundColourId, th::hero());
        search.setColour(TextEditor::textColourId, th::text());
        search.setColour(TextEditor::outlineColourId, th::outline());
        search.setColour(TextEditor::focusedOutlineColourId, th::accentDim());
        search.onTextChange = [this] { rebuild(); };
        addAndMakeVisible(search);

        list.setModel(this);
        list.setRowHeight(22);
        list.setColour(ListBox::backgroundColourId, th::hero());
        list.setColour(ListBox::outlineColourId, th::outline());
        addAndMakeVisible(list);

        if (proc != nullptr)
            if (auto* par = proc->state().getParameter("system"))
                attachment = std::make_unique<ParameterAttachment>(
                    *par, [this](float) { list.repaint(); });

        rebuild();
        startTimerHz(4);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        search.setBounds(b.removeFromTop(24));
        b.removeFromTop(4);
        list.setBounds(b);
    }

private:
    struct Row { int sys; bool fav; };

    void rebuild()
    {
        rows.clear();
        if (proc == nullptr) { list.updateContent(); return; }
        auto& reg = proc->registry();
        const auto favIds = proc->getFavoriteIds();
        const auto needle = search.getText().trim().toLowerCase();

        auto matches = [&](int i)
        {
            if (needle.isEmpty()) return true;
            return String(reg.name(i)).toLowerCase().contains(needle)
                || String(reg.id(i)).toLowerCase().contains(needle);
        };
        for (int i = 0; i < reg.size(); ++i)     // favorites first
            if (favIds.contains(String(reg.id(i))) && matches(i))
                rows.push_back({ i, true });
        for (int i = 0; i < reg.size(); ++i)
            if (! favIds.contains(String(reg.id(i))) && matches(i))
                rows.push_back({ i, false });
        list.updateContent();
        list.repaint();
    }

    int getNumRows() override { return (int) rows.size(); }

    void paintListBoxItem(int row, Graphics& g, int w, int h, bool) override
    {
        if (row < 0 || row >= (int) rows.size() || proc == nullptr) return;
        const auto& r = rows[(size_t) row];
        const bool current = r.sys == currentSys;

        if (current)
        {
            g.setColour(th::accent().withAlpha(0.16f));
            g.fillRect(0, 0, w, h);
            g.setColour(th::accent());
            g.fillRect(0, 0, 3, h);
        }
        // star zone
        g.setColour(r.fav ? th::accent() : th::outline());
        g.setFont(th::label(14.0f));
        g.drawText(r.fav ? String::fromUTF8(u8"★") : String::fromUTF8(u8"☆"),
                   Rectangle<int>(4, 0, 18, h), Justification::centred, false);

        g.setColour(current ? th::text() : th::text2());
        g.setFont(th::label(13.0f));
        g.drawText(proc->registry().name(r.sys), 26, 0, w - 30, h,
                   Justification::centredLeft, true);
    }

    void listBoxItemClicked(int row, const MouseEvent& e) override
    {
        if (row < 0 || row >= (int) rows.size() || proc == nullptr) return;
        const auto& r = rows[(size_t) row];
        if (e.x < 24)                                        // star toggle
        {
            proc->setFavorite(String(proc->registry().id(r.sys)), ! r.fav);
            rebuild();
            return;
        }
        if (attachment != nullptr)
            attachment->setValueAsCompleteGesture((float) r.sys);
    }

    void timerCallback() override
    {
        if (proc == nullptr || ! isShowing()) return;
        const int s = proc->currentSystemIndex();
        if (s != currentSys)
        {
            currentSys = s;
            list.repaint();
        }
    }

    SoundNutsProcessor* proc = nullptr;
    TextEditor search;
    ListBox list;
    std::vector<Row> rows;
    std::unique_ptr<ParameterAttachment> attachment;
    int currentSys = 0;
};

class SystemBrowserItem : public foleys::GuiItem
{
public:
    FOLEYS_DECLARE_GUI_FACTORY(SystemBrowserItem)

    SystemBrowserItem(foleys::MagicGUIBuilder& b, const ValueTree& node) : GuiItem(b, node)
    {
        comp = std::make_unique<SystemBrowserComponent>(getProc(*this));
        addAndMakeVisible(*comp);
    }
    void update() override {}
    Component* getWrappedComponent() override { return comp.get(); }

private:
    std::unique_ptr<SystemBrowserComponent> comp;
};

// =============================================================================
//  LFO monitor — two bipolar bars with an animated dot each (mod micro-motion).
// =============================================================================
class LfoMonitorComponent : public Component, private Timer, public SettableTooltipClient
{
public:
    explicit LfoMonitorComponent(SoundNutsProcessor* p) : proc(p) { startTimerHz(30); }

    void paint(Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced(2.0f);
        const float rowH = b.getHeight() * 0.5f;
        drawRow(g, b.removeFromTop(rowH).reduced(0, 3.0f), "A", a);
        drawRow(g, b.reduced(0, 3.0f), "B", bVal);
    }

private:
    void drawRow(Graphics& g, Rectangle<float> r, const char* tag, float v)
    {
        g.setColour(th::text2());
        g.setFont(th::bold(10.0f));
        auto label = r.removeFromLeft(14.0f);
        g.drawText(tag, label, Justification::centredLeft, false);

        const float cy = r.getCentreY();
        g.setColour(th::outline());
        g.fillRoundedRectangle(r.getX(), cy - 1.0f, r.getWidth(), 2.0f, 1.0f);
        g.setColour(th::outline().brighter(0.2f));
        g.fillRect(r.getCentreX() - 0.5f, cy - 4.0f, 1.0f, 8.0f);

        const float x = r.getX() + (jlimit(-1.0f, 1.0f, v) * 0.5f + 0.5f) * r.getWidth();
        g.setColour(th::accent());
        g.fillEllipse(x - 3.0f, cy - 3.0f, 6.0f, 6.0f);
    }

    void timerCallback() override
    {
        if (proc == nullptr || ! isShowing()) return;
        a    = proc->lfoAValue().load();
        bVal = proc->lfoBValue().load();
        repaint();
    }

    SoundNutsProcessor* proc = nullptr;
    float a = 0.0f, bVal = 0.0f;
};

class LfoMonitorItem : public foleys::GuiItem
{
public:
    FOLEYS_DECLARE_GUI_FACTORY(LfoMonitorItem)

    LfoMonitorItem(foleys::MagicGUIBuilder& b, const ValueTree& node) : GuiItem(b, node)
    {
        comp = std::make_unique<LfoMonitorComponent>(getProc(*this));
        addAndMakeVisible(*comp);
    }
    void update() override {}
    Component* getWrappedComponent() override { return comp.get(); }

private:
    std::unique_ptr<LfoMonitorComponent> comp;
};

// =============================================================================
//  Orbit sigil header mark + BETA tag. Identity WITHOUT any title text
//  (stealth-beta rule).
// =============================================================================
class OrbitSigilComponent : public Component, public SettableTooltipClient
{
public:
    void paint(Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        auto sig = b.removeFromLeft(b.getHeight());
        th::drawOrbitSigil(g, sig.reduced(3.0f), th::accent(), 0.75f);

        b.removeFromLeft(8.0f);
        auto chip = b.removeFromLeft(44.0f).reduced(0.0f, b.getHeight() * 0.28f);
        g.setColour(th::accent());
        g.fillRoundedRectangle(chip, 4.0f);
        g.setColour(th::background());
        g.setFont(th::bold(10.0f));
        g.drawText("BETA", chip, Justification::centred, false);
    }
};

class OrbitSigilItem : public foleys::GuiItem
{
public:
    FOLEYS_DECLARE_GUI_FACTORY(OrbitSigilItem)

    OrbitSigilItem(foleys::MagicGUIBuilder& b, const ValueTree& node) : GuiItem(b, node)
    {
        comp = std::make_unique<OrbitSigilComponent>();
        addAndMakeVisible(*comp);
    }
    void update() override {}
    Component* getWrappedComponent() override { return comp.get(); }

private:
    std::unique_ptr<OrbitSigilComponent> comp;
};

// =============================================================================
//  Preset bar — factory + user presets (~/Music/<product>/Presets, path never
//  shown), save box inline.
// =============================================================================
class PresetBarComponent : public Component, public SettableTooltipClient
{
public:
    explicit PresetBarComponent(SoundNutsProcessor* p) : proc(p)
    {
        combo.setTextWhenNothingSelected("presets...");
        combo.setColour(ComboBox::backgroundColourId, th::hero());
        combo.setColour(ComboBox::textColourId, th::text());
        combo.setColour(ComboBox::outlineColourId, th::outline());
        combo.setColour(ComboBox::arrowColourId, th::text2());
        combo.onChange = [this] { comboPicked(); };
        addAndMakeVisible(combo);

        name.setTextToShowWhenEmpty("preset name...", th::text2().withAlpha(0.6f));
        name.setFont(th::label(12.0f));
        name.setColour(TextEditor::backgroundColourId, th::hero());
        name.setColour(TextEditor::textColourId, th::text());
        name.setColour(TextEditor::outlineColourId, th::outline());
        name.setColour(TextEditor::focusedOutlineColourId, th::accentDim());
        addAndMakeVisible(name);

        save.setButtonText("Save");
        save.setColour(TextButton::buttonColourId, th::panel());
        save.setColour(TextButton::textColourOffId, th::text());
        save.onClick = [this] { doSave(); };
        addAndMakeVisible(save);

        refresh();
    }

    void resized() override
    {
        auto b = getLocalBounds();
        save.setBounds(b.removeFromRight(46).reduced(1));
        b.removeFromRight(4);
        name.setBounds(b.removeFromRight(jmin(120, b.getWidth() / 2)).reduced(0, 1));
        b.removeFromRight(4);
        combo.setBounds(b.reduced(0, 1));
    }

private:
    void refresh()
    {
        if (proc == nullptr) return;
        combo.clear(dontSendNotification);
        combo.addSectionHeading("Factory");
        const auto factory = proc->factoryPresetNames();
        for (int i = 0; i < factory.size(); ++i)
            combo.addItem(factory[i], 1000 + i);
        const auto user = proc->userPresetNames();
        if (! user.isEmpty())
        {
            combo.addSectionHeading("User");
            for (int i = 0; i < user.size(); ++i)
                combo.addItem(user[i], 2000 + i);
        }
    }

    void comboPicked()
    {
        if (proc == nullptr) return;
        const int id = combo.getSelectedId();
        if (id >= 2000)      proc->loadUserPreset(combo.getItemText(combo.getSelectedItemIndex()));
        else if (id >= 1000) proc->applyFactoryPreset(id - 1000);
    }

    void doSave()
    {
        if (proc == nullptr) return;
        const auto n = name.getText().trim();
        if (n.isEmpty()) return;
        if (proc->saveUserPreset(n))
        {
            name.setText({}, dontSendNotification);
            refresh();
            combo.setText(n, dontSendNotification);
        }
    }

    SoundNutsProcessor* proc = nullptr;
    ComboBox combo;
    TextEditor name;
    TextButton save;
};

class PresetBarItem : public foleys::GuiItem
{
public:
    FOLEYS_DECLARE_GUI_FACTORY(PresetBarItem)

    PresetBarItem(foleys::MagicGUIBuilder& b, const ValueTree& node) : GuiItem(b, node)
    {
        comp = std::make_unique<PresetBarComponent>(getProc(*this));
        addAndMakeVisible(*comp);
    }
    void update() override {}
    Component* getWrappedComponent() override { return comp.get(); }

private:
    std::unique_ptr<PresetBarComponent> comp;
};

// =============================================================================
void registerGuiItems(foleys::MagicGUIBuilder& builder)
{
    builder.registerFactory("PhaseScope",    &PhaseScopeItem::factory);
    builder.registerFactory("RegimeStrip",   &RegimeStripItem::factory);
    builder.registerFactory("RegimeReadout", &RegimeReadoutItem::factory);
    builder.registerFactory("SystemBrowser", &SystemBrowserItem::factory);
    builder.registerFactory("LfoMonitor",    &LfoMonitorItem::factory);
    builder.registerFactory("OrbitSigil",    &OrbitSigilItem::factory);
    builder.registerFactory("PresetBar",     &PresetBarItem::factory);
}

} // namespace snutsui
