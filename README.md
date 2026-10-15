# SoundNuts II

A **chaotic-attractor modulation effect**. The trajectory of a strange attractor drives a filter,
a tremolo and an auto-pan on the incoming audio; one **Macro** knob walks the system's bifurcation
diagram from a fixed point through limit cycles and period doubling into chaos, and a coloured
regime strip shows the terrain while a phase-space scope shows the attractor live. 94 systems: three
curated by hand (Lorenz, Rössler, a driven pendulum) and 91 translated from the
[dysts](https://github.com/williamgilpin/dysts) collection. VST3, AU and standalone on macOS.
Version 2 ("SoundNuts II", beta) installs beside version 1 under its own identity.

## Layout

- `dsp/` — the JUCE-free, header-only modulation engine: the curated systems, the generated
  systems, the integrators (numerical and analytic where a system allows it), regime maps.
- `pipeline/` — the offline tools: `gen_dysts.py` translates dysts' equations to C++ and checks
  each translation point-wise against the original; `sweep.py` and `bake_regimes.py` compute and
  bake the regime maps. `DYSTS_COVERAGE.txt` lists what was ported and why the rest was skipped.
- `plugin/` — the JUCE wrapper, its layout, tests and build. See [`plugin/README.md`](plugin/README.md)
  for the controls, the build and the install.
- `docs/MATH.md` — the maths behind orbit tempo-sync and the regime maps.

## Regenerating the systems

```sh
git clone --depth 1 https://github.com/williamgilpin/dysts.git /tmp/dysts
python3 pipeline/gen_dysts.py --dysts /tmp/dysts
```

## Licence

SoundNuts II is free software under the GNU Affero General Public License, version 3 or later
(`LICENSE`). Third-party components and their licences, including dysts, are listed in `NOTICE`.
