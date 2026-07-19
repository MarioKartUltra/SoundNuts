# SoundNuts — plugin

JUCE FX wrapper around the validated JUCE-free `soundnuts::ModEngine`. A
**chaotic-attractor modulation effect**: the trajectory of a strange attractor
(Lorenz / Rössler / Pendulum) drives a filter, tremolo, and auto-pan on the
incoming audio. One **Macro** knob walks the bifurcation diagram
(`fixed point → limit cycle → chaos → tamed blow-up`); a colored **regime strip**
shows the terrain and a **phase-space scope** shows the attractor live.

The DSP core, the offline regime pipeline (`../pipeline/sweep.py`), and the baked
normalization (`../dsp/include/soundnuts/registry_data.hpp`, regenerate with
`python3 ../pipeline/bake_regimes.py`) are all upstream of this wrapper.

## Build (Apple Silicon)
```bash
./build.sh            # pins Apple clang + Xcode SDK, scrubs conda env vars
```
Artifacts under `build/SoundNuts_artefacts/Release/` (VST3 / Standalone / AU).
`COPY_PLUGIN_AFTER_BUILD` installs VST3+AU to your user plug-in folders.

## Install / signing (macOS, arm64)
arm64 code must be signed to load in a host. Ad-hoc sign + copy to the system
folder if that's what your DAW scans:
```bash
codesign --force --deep -s - ~/Library/Audio/Plug-Ins/VST3/SoundNuts.vst3
cp -R ~/Library/Audio/Plug-Ins/VST3/SoundNuts.vst3 /Library/Audio/Plug-Ins/VST3/
codesign --force --deep -s - /Library/Audio/Plug-Ins/VST3/SoundNuts.vst3
```
Use `/usr/bin/codesign` explicitly (a conda `codesign` shim may shadow it).

## Test
It's an **audio effect** — it needs an input signal.
- **In a DAW:** drop it on an audio track playing a pad/drum loop. Turn **Macro**
  from left (parked) toward the amber zone (chaos); watch the scope + regime strip.
- **Standalone:** `open build/SoundNuts_artefacts/Release/Standalone/SoundNuts.app`,
  set an input device (mic/loopback) in ⚙︎ Options.
- Validate as a host: `auval -v aufx Snut Sdnt`.

Controls: System, Macro, Rate, Filter Base/Amount, Resonance, Tremolo, Auto-Pan,
Smoothing, Solver (Auto/Numerical/Analytic), On Blow-Up (Reset/Free-Clamp),
Mix, Output.

---

# v2 — SoundNuts II (beta)

v2 lives beside v1 under a NEW identity so both load in a DAW at once:
`aufx Snt2 Sdnt`, bundle `com.soundnuts.v2`, product **SoundNuts II**, version 2.0.0.
The v1 bundles/tag (`v1-baseline`) are untouched. Universal binary (arm64 + x86_64).

## What's new
- **foleys_gui_magic GUI** (`foleys::MagicProcessor`, C++20) with a designed default
  layout in `Resources/magic.xml`. Beta builds ship the WYSIWYG palette
  (`FOLEYS_SHOW_GUI_EDITOR_PALLETTE=1`): open the editor inside the plugin (cog icon)
  and redesign details live. Custom GuiItems: `PhaseScope` (hero), `RegimeStrip`
  (click/drag to jump the Macro), `RegimeReadout`, `SystemBrowser`, `LfoMonitor`,
  `OrbitSigil`, `PresetBar`. Resizable; tooltips on every control. Stealth-beta: the
  rendered GUI carries no product name — identity is the chartreuse accent + orbit
  sigil + BETA tag.
- **Orbit tempo-sync**: `beats` chip locks one characteristic orbit to a beat division
  (frozen 16-division table; measured native periods, see `docs/MATH.md`). Bar-locked
  **orbit reset** (off / 1 / 2 / 4 / 8 bars).
- **LFO A/B mod matrix**: 8 shapes (incl. S&H / smoothed random / drift, seeded and
  deterministic), free-Hz or synced rate, transport phase-lock, bipolar/unipolar;
  bounded-offset depths onto Macro, Filter Base, Tremolo, Rate.
- **System morph**: crossfade between systems (ms or beats), engines pre-built so
  switching never allocates on the audio thread.
- **System browser** with search + starred favorites (persist in the session state).
- **Presets**: 6 factory presets + named user presets under
  `~/Music/SoundNuts II/Presets/*.xml`.
- All time/rate controls read in real units (ms / s, Hz, %, dB, divisions).

## Build
```bash
./build.sh               # arm64 dev loop
./build.sh --universal   # arm64 + x86_64 release build
ctest --test-dir build --output-on-failure   # v1 core check + v2 infra/orbit checks
```
Artifacts under `build/SoundNutsII_artefacts/Release/`. Validate: `auval -v aufx Snt2 Sdnt`.
Vendored shared v2 infra in `Source/common/acv2` (TempoSync/Lfo/SeedPolicy/Units +
Theme/LnfV2). foleys checkout is expected at `external/foleys_gui_magic` (symlink).
Math notes: `../docs/MATH.md`.
