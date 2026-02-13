# GEQ-12 — 12-Band Graphic Equalizer (AAX Plugin)

A professional audio plugin implementing a 12-band graphic equalizer for use in DAWs. Builds as an AAX plugin for Avid Pro Tools, with a format-agnostic DSP core that can also be wrapped for VST3, AU, etc.

## Band Frequencies

ISO 1/3-octave centers: **25, 40, 63, 100, 160, 250, 400, 630, 1k, 2.5k, 6.3k, 16k Hz**

Each band provides +/-12 dB of gain using peaking EQ biquad filters (Audio EQ Cookbook).

## Project Structure

```
src/
  dsp/
    Biquad.h          # Biquad filter (peak, shelf, LP, HP)
    GraphicEQ.h        # 12-band graphic EQ processor
  plugin/
    PluginParameters.h # Parameter IDs, names, ranges
    PluginProcessor.h  # Format-agnostic plugin processor
  aax/
    GEQ12_AAX.h        # AAX SDK wrapper and algorithm callback
    GEQ12_Describe.cpp # AAX entry point (GetEffectDescriptions)
tests/
    test_biquad.cpp           # Biquad filter unit tests
    test_graphic_eq.cpp       # GraphicEQ unit tests
    test_plugin_processor.cpp # Plugin processor tests
```

## Building

### Tests only (no SDK required)

```bash
cmake -B build -DGEQ12_BUILD_TESTS=ON
cmake --build build
./build/geq12_tests
```

### AAX plugin (requires Avid AAX SDK)

```bash
cmake -B build -DGEQ12_BUILD_AAX=ON -DAAX_SDK_PATH=/path/to/AAX_SDK
cmake --build build
```

The AAX SDK is available under license from [Avid](https://developer.avid.com). After building, copy the `.aaxplugin` bundle to the Pro Tools plugin directory.

## Architecture

- **Biquad** — Direct Form II Transposed biquad filter implementing the standard Audio EQ Cookbook forms (Robert Bristow-Johnson). Supports peak, low/high shelf, and low/high pass types.
- **GraphicEQ** — Chains 12 peaking biquad filters in series with per-channel state for stereo processing. Gain per band is clamped to +/-12 dB.
- **PluginProcessor** — Format-agnostic wrapper adding bypass control and parameter mapping. Designed to be wrapped by any plugin format (AAX, VST3, AU).
- **AAX wrapper** — Maps the Avid AAX SDK callbacks (`GetEffectDescriptions`, algorithm render) to the PluginProcessor.
