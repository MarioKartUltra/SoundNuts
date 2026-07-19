# SoundNuts — chaotic-attractor modulation plugin

A modulation source whose LFO is the trajectory of a nonlinear dynamical system,
with a curated **strange-attractor library** and a **precomputed regime (phase)
diagram** so the user can automate across `fixed point → limit cycle →
period-doubling → chaos → tamed blow-up`.

The regime map is not documentation — it is the macro knob's *terrain*, and the
gesture of dragging across it is the instrument.

---

## 1. Core mechanic: one macro walks the bifurcation diagram

Every shipped system exposes a bifurcation parameter `p`. Mapping `p` to one
automatable macro gives, left→right:

| Zone | Regime | Musical use |
|------|--------|-------------|
| gray   | fixed point             | dead / parked |
| green  | limit cycle / quasiperiodic | clean periodic LFO, **tempo-syncable** (measure period) |
| green→amber | period-doubling cascade | rhythmic subdivisions spawn (1→2→4 lobes) — the headline gesture |
| amber  | chaos                   | evolving, never-repeating "organic" mod |
| red    | (tamed) blow-up         | automate toward boundary → amplitude explodes → clamp+softclip+resync = **built-in riser/drop** |

`blow-up` is a feature: detect divergence, ramp output (riser), re-seed to the
on-attractor IC on the next note/bar.

---

## 2. Library: dysts as an OFFLINE data pipeline (not a runtime dep)

[`dysts`](https://github.com/williamgilpin/dysts) (Gilpin 2021) ships ~130 systems
as JSON: `{rhs, params, dt, period, maximum_lyapunov_estimated, on-attractor ICs}`.

- Port RHS by hand to C++ (each vector field is 3–6 lines).
- Curate ~15–25 bounded, distinct systems with an accessible bifurcation param:
  Lorenz, Rössler, Thomas, Aizawa, Halvorsen, Chen, Dadras, Sprott family,
  Chua, Langford.
- Use dysts' precomputed ICs so the plugin **starts on the attractor** (no load
  transient) and its Lyapunov estimates to label the default regime.

---

## 3. Regime / phase diagram: computed, not eyeballed

Offline, per (system, bifurcation param `p`):

1. Sweep `p` over a grid.
2. At each `p`: integrate, discard transient, compute
   - **largest Lyapunov exponent** (Benettin / QR),
   - **boundedness** (does |state| stay finite),
   - **dominant period** (autocorrelation / PSD peak).
3. Classify:
   - `λ < 0`, settles  → fixed point (gray, dead zone)
   - `λ ≈ 0`, bounded, sharp period → limit cycle / quasiperiodic (green, syncable)
   - `λ > 0`, bounded → chaos (amber)
   - unbounded → divergent (red, riser zone)
4. Emit a 1-D strip (or 2-D for two params): colormap + period curve + per-`p`
   normalization (affine min/max per coordinate). ~1 KB per system.

This strip powers the UI terrain AND the **guard rails** ("snap to nearest limit
cycle", "stay bounded", "ride the red edge").

---

## 3a. Analytic solutions (optional, preferred when available)

Some systems/regimes have closed forms. When one exists AND is valid at the
current macro value, evaluate it directly instead of integrating. Wins:

- **sample-exact, zero phase drift** — perfectly periodic in a limit cycle;
- **random access** — `eval(t)` at any `t`, so DAW-timeline seeking is exact
  (host jumps the playhead → land on the right phase, no re-integration);
- **exact period in closed form** → exact tempo-sync, no measurement.

It's an *optional capability* (`AnalyticSolution`, exposed via
`AttractorSystem::analytic()` → `nullptr` if none). The engine selects via
`SolverPreference { Auto | ForceNumerical | ForceAnalytic }`. At a validity
boundary the engine **hands off without a click**: seed the integrator from the
analytic state (and sync the analytic phase from the integrator on the way back).

Closed forms in the curated set:

| System | Closed form | Valid where | Notes |
|--------|-------------|-------------|-------|
| Harmonic osc | sine/exp | always | trivial; exact sync baseline |
| **Pendulum** | Jacobi `sn`/`cn`, period `4K(k)/ω₀` | libration (θ₀<π) | period → ∞ at separatrix = killer macro |
| Duffing (undamped, unforced) | Jacobi elliptic | bounded wells | |
| Logistic map, r=4 | `xₙ = sin²(2ⁿπθ₀)` | r=4 only | "exactly solvable chaos" curiosity |
| Lorenz / Rössler / Thomas / … | none | — | always integrate |

Primitives live in `dsp/include/soundnuts/analytic/` (`elliptic.hpp`: Jacobi
`sn/cn/dn` + `K(k)` via AGM/Landen, **modulus-k convention** — documented because
the m=k² convention silently doubles periods).

## 3b. Maps vs flows — two output modes, one library  [MIDI/sequencer PARKED]

> Parked for now: the map/sequencer + MIDI path is kept as a stub + this note,
> but NOT being built. Focus is the flow → CV path. Don't develop MIDI further
> until un-parked. `map_system.hpp` / `sequencer.hpp` / `systems/logistic.hpp`
> stay as-is (they compile, cost nothing).


Continuous-time **flows** (Lorenz, pendulum…) and discrete-time **maps**
(logistic, Hénon, Ikeda…) are different *interfaces* and want different outputs:

| | Flow `dx/dt=f(x)` | Map `x_{n+1}=f(x_n)` |
|--|--|--|
| Interface | `AttractorSystem::rhs()` | `MapSystem::step()` |
| Natural output | continuous **CV / LFO** | **MIDI step sequencer** (1 iterate = 1 event) |
| Engine | `ModEngine` (integrator) | `SequencerEngine` (clock + quantizer) |
| Regime map | Lyapunov sweep strip | **bifurcation diagram** (= step-pattern length) |
| Period-doubling reads as | rhythmic lobe count | pattern length 2→4→8 |

So the discrete-time logistic path *is* the sequencer mode — a second product
mode sharing the same system library, regime map, and macro.

**Resolution / the 7-bit question.** "Automation = MIDI = 7-bit" is a 1990s
artifact and false for modern hosts. Plugin params / host automation are **float**
(VST3/AU/CLAP). 7-bit is *only* MIDI 1.0 CC. So:

- keep the chaos engine **float internally**; the FX / CV / host-param-mod paths
  are full resolution — no zipper.
- quantize **only at a MIDI-CC output boundary** (`Quantizer::bitDepth`): 7-bit
  standard CC, 14-bit CC / pitch-bend, MPE, or MIDI 2.0 (host support patchy 2026).
- maps *want* quantization (musical), flows want smoothness → different paths.

## 4. Hard DSP, in priority order

1. **Normalization.** Per-(system, p) affine min/max LUT from the offline sweep,
   interpolated at runtime, then final soft-clip. A running normalizer alone lags
   during period-doubling jumps — use the LUT as the anchor.
2. **Timescale decoupling.** Integrate at a fixed **control rate** (1–4 kHz) with
   RK4, decoupled from sample rate; cubic-interpolate to block rate. "Rate" scales
   integrator `dt`; tempo-sync engages only in the green zone where period exists.
3. **Divergence = feature.** `|state| > thresh` → ramp output (riser) → re-seed to
   on-attractor IC at next note/bar. Modes: "reset on divergence" vs "free-run + clamp".
4. **Multi-output.** A 3-D system yields x,y,z — three phase-locked-but-distinct
   mod signals. Their natural correlation can't be faked with three free LFOs.

---

## 5. Plugin format / routing

Routing is what kills modulator plugins (VST3 can't freely modulate other plugins).

- **v1 (MVP) = self-contained multiband FX**: chaos engine drives built-in
  gain / filter / pan / pitch (a ShaperBox/LFOTool competitor). Differentiator =
  attractor library + regime macro. No routing problem, immediately sellable.
- **v2** = routing outputs: CLAP polyphonic param modulation (Bitwig), MIDI CC,
  CV-over-audio for modular.

UI: phase-space scope (Lissajous of two coords) + colored regime strip under the
macro. The scope sells the demo video by itself.

---

## 6. Architecture (JUCE; DSP core is JUCE-free / header-only / testable)

```
SoundNuts/
  dsp/include/soundnuts/      # JUCE-free DSP core (this is what we stub first)
    attractor_system.hpp      # flow interface: rhs() + Params + RegimeMap + AnalyticSolution
    map_system.hpp            # map interface: step() (discrete-time, sequencer)
    registry.hpp              # curated system registry
    integrator.hpp            # fixed-step RK4 at control rate
    normalizer.hpp            # affine LUT + softclip
    mod_engine.hpp            # flows -> CV: rate/sync, N outputs, divergence, solver pref
    sequencer.hpp             # maps -> MIDI: clock + Quantizer (7/14-bit at CC boundary)
    analytic/
      elliptic.hpp            # Jacobi sn/cn/dn + K(k), modulus-k convention
    systems/                  # one file per system (rhs/step + optional analytic)
      lorenz.hpp              # flow, rhs only (no closed form)
      rossler.hpp             # flow, rhs only; macro -> c, period-doubling route
      pendulum.hpp            # flow, rhs + Jacobi-elliptic AnalyticSolution
      logistic.hpp            # map,  step + r=4 exact solution; bifurcation diagram
  pipeline/                   # offline dysts -> regime-map JSON (Python)
    sweep.py                  # Benettin LLE + period + bounds sweep
    regime_schema.json        # data contract shared with C++ side
  plugin/                     # JUCE wrapper (later)
```

Data contract (`RegimeMap` JSON) is locked from BOTH ends: `pipeline/` emits it,
`dsp/` consumes it.

---

## 7. Build order

1. **[load-bearing] DONE** `pipeline/sweep.py` — Benettin LLE + period + bounds +
   normalization sweep → regime-map JSON. Validated: Lorenz fixed-pt→chaos,
   Rössler periodic→chaos, pendulum limit-cycle w/ period 1.02→1.65 toward the
   separatrix. Integrable systems flagged (LE≡0; estimator misreads shear).
   Starter set lorenz/rossler/pendulum; extend from dysts.
2. **DONE** `dsp/` interface stubs — flow + map interfaces, engines, analytic hook.
3. Port vector fields to `systems/` — **lorenz, rossler, pendulum DONE** (C++ now
   mirrors the pipeline's three; Rössler rhs cross-checked at c=5.7). Extend more
   from dysts as needed.
4. **DONE** `elliptic.hpp` (AGM + descending Landen), **integrator RK4**,
   **RegimeMap::sampleAt** (interp norm + nearest regime), **mod_engine.process**
   (analytic/numerical select, control-rate→audio interp, divergence reseed,
   one-pole smoothing). Validated: K to 1e-10; **analytic pendulum period 1.6551 ==
   pipeline 1.65**; **in-engine RK4 vs closed form agree to 3.9e-13 over a period**;
   engine output finite & bounded in all solver modes.
   NEXT: `registry.hpp` + JSON loader to populate each system's RegimeMap from
   `pipeline/out/*.json` (until then normalization is identity → hard clamp).
5. JUCE FX wrapper (gain/filter/pan), phase scope, regime strip UI.
6. v2 routing outputs.

MVP ≈ 3–4 weeks. The dysts→regime pipeline is the part that is genuinely our edge.
