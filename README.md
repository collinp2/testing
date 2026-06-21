# NECRONAM MAX — Neural Amp Necromancy (feature-loaded edition)

![NECRONAM MAX editor](docs/screenshot.png)

A horror-themed **dual-amp Neural Amp Modeler (NAM) capture player** by CP
Software, built with JUCE. It is the feature-loaded sibling of
[NECRONAM](https://github.com/collinp2/cp_software/tree/necronam): two NAM
models you can run **in series or in parallel**, **two Flesh Render multiband
saturators** (one in front of the amps, one at the output), a **stereo dual-IR
cab mixer**, an **API-560 style graphic EQ**, **hi-pass / low-pass filters**, an
order-switchable **delay + reverb**, and a **strobe tuner** — all in a
**true-stereo** signal path, with an on-disk **preset browser**. The UI is
organised into **AMP / TONE / FX / TUNER** tabs.

> NECRONAM MAX is a **separate plugin** from NECRONAM (distinct name, plugin
> code and bundle id). Install both and use whichever fits the session — the
> original for a lean single-amp tone, MAX for dual-amp / stereo rigs.

## Signal chain

```
sum-to-mono input → input gain → noise gate
  → FRONT multiband saturation  (Flesh Render, mono — immediately before the amps)
  → FRONT filters               (mono hi-pass / low-pass)
  → DUAL AMP STAGE (two NAM models, each individually bypassable) → stereo bus
        Single   : Amp A only                            (centred)
        Series   : Amp A → Amp B  (Amp A level = drive into B)   (centred)
        Parallel : Amp A + Amp B, spread L↔R             (true stereo)
  → amp-bus output trim          (OUT meter)
  → DUAL CAB IR mixer (stereo)   IR A + IR B convolved & blended
  → DC blocker (~10 Hz)
  → API-560 graphic EQ           (10 octave bands, ±12 dB, proportional Q)
  → OUTPUT multiband saturation  (Flesh Render: sat → dist → fuzz, 3 bands)
  → hi-pass filter
  → low-pass filter
  → DELAY + REVERB               (order-switchable; each fully true-bypassed)
  → clean DI blend               (equal-power crossfade with the dry input)
  → output (Raw / Normalized / Calibrated)

TUNER: taps the dry input pre-amp; engaging it mutes the output and drives a
strobe display.
```

Everything from the cab stage onward runs **per channel (L/R)** — two instances
each of the EQ, output saturator, DC blocker and filters, plus the stereo delay
and reverb — so the stereo image created by the parallel amps (or by stereo
material) is preserved end to end. The front saturator is mono (it shapes the
guitar signal feeding the amps, like a pedal in front).

## Dual amps — routing

Load a `.nam` capture into **Amp A** and (optionally) **Amp B**, then pick a
routing:

- **Single** — Amp A only. Behaves like the original NECRONAM.
- **Series** — Amp A feeds Amp B. *Amp A Level* doubles as the drive into Amp B
  (e.g. a drive/preamp capture stacked into a power-amp capture).
- **Parallel** — Amp A and Amp B process the input independently and are mixed.
  **Spread** places them in the stereo field: 0% = both centred (dual-mono
  blend), 100% = Amp A hard-left / Amp B hard-right (full stereo). Equal-power
  pan law.

Each amp has its own **On** switch: in Series a bypassed amp passes straight
through to the next; in Parallel it drops out and the remaining amp re-centres —
so you can A/B the two models, or run just one.

*Amp A/B Level* trim and balance the two captures. The **Quality** (A2) control
is shared and applies to both models (see below).

## Cab — stereo dual-IR mixer

Two impulse responses, **IR A** and **IR B**, each with its own on/off and
level. Both are convolved on the stereo bus and summed, so you can blend two
cabs (or A/B between them). Mono IRs are applied identically to both channels;
the stereo width comes from the amp stage.

## Saturation — front and output

The Flesh Render multiband saturator (Linkwitz-Riley 3-band split → per-band
saturation → distortion → fuzz) appears **twice**, identical circuits:

- **Front** — mono, **immediately before the dual amps** (a pedal in front of
  the rig: clean it up or drive the models harder).
- **Output** — stereo, after the EQ near the end of the chain (post-amp tone
  shaping / output grit).

Each has its own on/off and full low/mid/high × sat/dist/fuzz controls, and is
**true-bypassed** (skipped and reset while off). Front saturation lives on the
**AMP** tab; output saturation on the **TONE** tab.

## Front filters

A second hi-pass / low-pass pair sits **between the front saturator and the
amps** (mono, on the AMP tab) — for tightening the low end going into a high-gain
model, or shaving fizz before the amp.

## Clean blend

The master strip's **Clean Blend** knob crossfades (equal-power) between the
fully processed output and the **clean DI** (tapped right after input gain,
bypassing the whole chain) — especially useful on bass to keep the low end solid
under a distorted amp. Calibration assumes the processed signal, so prefer Raw
output mode when blending heavily.

## Delay & reverb

A stereo **delay** (time / feedback / mix) and a stereo **reverb** (size / damp
/ mix) sit at the end of the chain. The **FX Order** selector swaps which comes
first (Delay → Reverb or Reverb → Delay).

Both are **completely true-bypassed**: while off they are skipped entirely (no
CPU, no colour) and their state is reset on the off transition, so a bypassed
module leaves no tail and cannot affect the signal. They live on the **FX** tab.

## Strobe tuner

The **TUNER** tab hosts a virtual strobe tuner. The processor captures the dry
input into a lock-free ring; pitch is detected on the message thread
(autocorrelation), and the display shows the nearest note, the cents offset, and
a strobe band whose drift rate is proportional to how far out of tune you are
(it stands still when you are in tune). **Engaging the tuner mutes the plugin
output** so you can tune silently.

## Presets

A built-in browser stores the **whole plugin state** (all parameters plus the
loaded model/IR file paths) as XML under:

```
~/Library/Application Support/CP Software/NECRONAM MAX/Presets   (macOS)
<userAppData>/CP Software/NECRONAM MAX/Presets                  (other)
```

Use **SAVE** to name and store the current tone, the **‹ ›** arrows to step
through presets, or the dropdown to jump to one. Presets reference the `.nam`
and `.wav` files by path — keep those files in place to recall a tone.

## Metering

Three peak meters (peak-hold, ~30 Hz):

- **IN** / **OUT** in the Input panel — the amp input (post input gain, i.e.
  what Amp A sees — NAM models are level-sensitive) and the amp-bus output.
- **MASTER** in the right-hand output strip — whole-plugin output. The over-0
  dBFS portion of each bar lights up as a clip warning.

Meter peaks are accumulated on the audio thread (lock-free) and read+reset by
the editor. The OUT/MASTER meters report the louder of the two channels.

## Low latency

Latency is **zero** whenever the loaded models' native sample rates match the
session rate (the common case). The only latency source is the per-model
sample-rate converter (`ResamplingContainer`, Lanczos), which engages only on a
rate mismatch; the reported latency is the **sum** of the two amps in Series and
the **max** in Parallel. The EQ, filters, cab convolution and saturator are
minimum-phase / zero-latency and add none (the saturator is not oversampled,
matching Flesh Render — extreme drive can alias on bright material).

> In Parallel with two models at *different* native rates, their latencies are
> not internally compensated; use models at the session rate (or matching rates)
> to avoid comb filtering.

## Quality / efficiency (NAM A2)

NAM **Architecture 2 (A2)** models are *slimmable*: a single model can trade CPU
for fidelity at runtime. The **Quality** slider drives
`nam::SlimmableModel::SetSlimmableSize(0..1)` for **both** amps:

- **far right = Max Quality** ("full" model) — the default.
- **far left = Max Efficiency** ("lite" model) — lowest CPU.

`SetSlimmableSize` is thread-safe but **not** real-time safe, so changes are
applied on the message thread (via an `AsyncUpdater`), never in `processBlock`.
Older **A1** models aren't slimmable; the slider greys out when neither amp is
A2.

## Output modes

Referenced to **Amp A**'s model metadata:

- **Raw** — output gain only.
- **Normalized** — targets ~−18 dBFS using the model's embedded loudness.
- **Calibrated** — reproduces real-world levels using the model's input/output
  dBu calibration and your interface's input-calibration value ("IN CAL", dBu at
  0 dBFS).

## Building

Requires CMake (≥ 3.21) and a C++20 compiler. JUCE and NeuralAmpModelerCore
(with its Eigen / AudioDSPTools submodules) are fetched automatically via
`FetchContent`, so the first configure needs network access and takes a while.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

For a faster local dev build, restrict to your host arch:
`-DCMAKE_OSX_ARCHITECTURES=arm64`.

On **macOS 15** JUCE is pinned to the `develop` branch (per the CP Software build
notes) to avoid removed-API issues.

Formats built: **VST3**, **AU**, **Standalone**. With `COPY_PLUGIN_AFTER_BUILD`
on, the VST3/AU are copied into the system plug-in folders automatically
(`/Library/Audio/Plug-Ins/...` on macOS).

## Project layout

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Build config; fetches JUCE + NeuralAmpModelerCore, compiles core + NAM sources |
| `Source/PluginProcessor.*` | Audio engine: dual amps, routing, stereo cab mixer + post chain, params, state |
| `Source/PluginEditor.*` | Horror-themed tabbed UI (AMP / TONE / FX / TUNER) + persistent preset bar & master strip |
| `Source/PresetManager.h` | On-disk preset browser (save/load/step) |
| `Source/PitchDetector.h` | Autocorrelation pitch tracker for the tuner (message-thread) |
| `Source/StrobeTuner.h` | Strobe-tuner display component |
| `Source/ResamplingNAM.h` | Wraps `nam::DSP` with on-demand sample-rate conversion |
| `Source/Api560EQ.h` | 10-band, octave-spaced, proportional-Q graphic EQ |
| `Source/Saturation.h` | Flesh Render multiband saturator (verbatim waveshaping) |
| `Source/HorrorLookAndFeel.*` | CP Software blood/bone theme |

## Credits / sources

- Neural Amp Modeler core & plugin — Steven Atkinson
  (`sdatkinson/NeuralAmpModelerCore`, `NeuralAmpModelerPlugin`), MIT.
- AudioDSPTools (noise gate, IR convolution, resampler, filters) —
  `sdatkinson/AudioDSPTools`.
- Saturation DSP & horror theme — CP Software "Flesh Render".
- Framework — JUCE.
