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
