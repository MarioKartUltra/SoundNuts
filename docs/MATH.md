# SoundNuts II — math notes

Terse reference for what the plugin actually computes. Code: `dsp/include/soundnuts/`
(JUCE-free), JUCE layer in `plugin/Source/`.

## Systems

94 autonomous flows `dx/dt = f(x; p)`, `x ∈ R^d`, `d ≤ 8`:

- 3 curated (`systems/`): Lorenz, Rössler, pendulum. One bifurcation parameter each,
  mapped from the Macro `m ∈ [0,1]` by a per-system affine ramp.
- 91 codegen'd from dysts (`generated_systems.hpp`; `pipeline/gen_dysts.py` parses each
  Python `_rhs` AST, emits C++, and validates point-wise against the original). Macro
  scales each system's dominant parameter around its chaotic reference value.

## Integration

Fixed-step classical RK4 at a control rate f_c (1 kHz in the plugin), decoupled from the
audio rate. Native time step per control step: `dt = rateScale / f_c`. Audio-rate output
is linear interpolation between the last two control samples (plus one-pole smoothing,
below). The pendulum additionally carries a closed form (Jacobi `sn/cn`, period
`4K(k)/ω0` — `analytic/elliptic.hpp`, modulus-k convention); when valid (libration) and
selected, the engine evaluates it directly: sample-exact, drift-free, seekable.

## Regime map ("the terrain")

Offline sweep (`pipeline/sweep.py`) per macro grid point: largest Lyapunov exponent λ
(Benettin), boundedness, dominant period T, and per-channel affine normalization
(center c_i, scale s_i). Classification:

- λ < 0, settles → fixed point (gray)
- λ ≈ 0, bounded, sharp period → limit cycle (green; tempo-syncable)
- λ > 0, bounded → chaos (amber)
- unbounded → divergent (red; riser zone)

Runtime output per channel: `y_i = clamp((x_i − c_i)·s_i, −1, 1)`, then one-pole
smoothing `ŷ += α(y − ŷ)`, `α = 1 − exp(−1/(τ f_s))`, τ = Smoothing.

Divergence (‖x‖ over threshold or non-finite): mode *Reset* re-seeds the on-attractor
IC (the clamp turns the excursion into a riser first); *Free/Clamp* only clamps.

## Orbit tempo-sync (`orbit_sync.hpp`)

Rate knob = native-clock warp. In beats mode we want one characteristic orbit per
division of duration `T_div` seconds (frozen 16-division table, quarter-note bpm):

```
rateScale = T_native / T_div,   clamped to [0.02, 16]
```

`T_native` (native time units) comes from the baked regime map's period at the current
macro when present, else from a prepare-time estimate: RK4 run at 200 Hz control rate,
warm-up discarded, mean value of channel 0, period = mean spacing of upward
mean-crossings (needs ≥ 3 crossings, else unsynced fallback of 1 native unit). Chaotic
regimes have no exact period — the estimate locks the *characteristic* timescale, which
is the musically useful notion.

Bar-locked orbit reset: with host ppq and signature, bar index
`b = floor(ppq / (quartersPerBar·N))`; on change of `b` while playing, retrigger the
attractor (armed, not fired, at transport start).

## Modulation mapping (audio path)

With `x, y, z` the first three normalized channels (z falls back to y for 2-D systems):

- Filter: TPT state-variable low-pass (Zavalishin), cutoff
  `f = f_base · 2^(4·A_filt·x)` — ±4 octaves at full depth, clamped to [20 Hz, 0.45 f_s];
  resonance maps to k = 2 − 1.9·r.
- Tremolo: gain `g_t = 1 − A_trem·(1 − y)/2` ∈ [1 − A_trem, 1].
- Auto-pan: equal-power, angle `θ = (π/4)(1 + A_pan·z)`, `g_L = cosθ, g_R = sinθ`
  on the wet mono sum (·√2 to keep center unity).
- Output: dry/wet mix, output trim, `isfinite` guard, hard clamp ±1.5.

## LFO A/B (acv2 spec)

Block-rate evaluation; value applied as a bounded offset BEFORE the Params struct, so
the DSP core never sees the mod matrix:

```
macro_eff = clamp(macro + d_A·a + d_B·b, 0, 1)
f_base'   = clamp(f_base · 2^(2(d_A·a + d_B·b)), 20, 18k)   (±2 octaves)
trem'     = clamp(trem + d_A·a + d_B·b, 0, 1)
rateScale' = rateScale · 2^(2(d_A·a + d_B·b))               (±2 octaves)
```

`a, b ∈ [−1,1]` (or [0,1] unipolar), depths `d ∈ [−1,1]`. Shapes: sine, tri, saws,
square (pw), S&H, smoothed random (one-pole toward per-cycle targets, closed-form slew
over n samples), drift (slow wander, cutoff ≈ hz/8). Random shapes are splitmix64-seeded
→ deterministic per seed (SeedPolicy Fixed mode with a build-constant seed). Sync mode
sets the LFO period to a division; transport retrig phase-locks to the bar via ppq.

## System morph

On a system switch the previous engine keeps running; outputs crossfade linearly over
the morph time (ms or division): `x = (1−w)·x_new + w·x_old`, w: 1 → 0. Both engines
are pre-built at prepare time (all 94), so switching allocates nothing on the audio
thread.
