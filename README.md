# IR Capture

A standalone macOS application for capturing impulse responses (IRs) from hardware devices such as guitar cabinets, reverb units, or any linear audio system.

**[Download IR Capture v1.0.0](https://github.com/collinp2/cp_software/releases/tag/v1.0.0)**

> [!IMPORTANT]
> This app is unsigned. After installing on any Mac, run this command in Terminal before opening it:
> ```bash
> xattr -cr "/Applications/IR Capture.app"
> ```
> Without this step macOS will block the app with a "damaged" error. Alternatively, right-click the app in Finder and choose **Open**.

## How It Works

IR Capture uses a dual-stimulus measurement technique for accuracy:

1. **Sine sweep** (20 Hz – 20 kHz, 3 seconds) — played through the output and recorded back through the input. The IR is computed via frequency-domain deconvolution using a time-reversed, amplitude-weighted inverse sweep.
2. **Band-limited noise** (20 Hz – 20 kHz, 3 seconds) — played immediately after. The IR is computed via Wiener deconvolution.

The two results are averaged and the tail is trimmed to produce the final IR, which is saved as a **24-bit mono WAV** file.

## Setup

Connect your audio interface:
- **Output** → device input (e.g. amp input, hardware unit input)
- **Device output** (e.g. speaker cabinet mic, hardware unit output) → **Input**

Use **Audio Setup** to select your input and output devices.

## Controls

| Control | Description |
|---|---|
| **Gain (dB)** | Output level of the test signal sent to your device (-18 to +18 dB). Lower this if clipping is detected. |
| **Normalize output** | When checked, peak-normalizes the captured IR to 0.99 FS before saving. |
| **Base Name** | Filename prefix for saved IRs. Resetting the name resets the counter to 001. |
| **Save To...** | Choose the output directory. Defaults to `~/Music/IR Captures/`. |
| **Capture** | Starts the capture sequence (sweep → noise → process → save). |

## Metering

- **OUT** (left) — level of the test signal being sent to your output device
- **IN** (right) — level of the signal being recorded from your input device

Click either meter to reset the peak hold and clip indicator.

## Clip Detection

If either the input or output signal hits 0 dBFS during a capture, the capture is immediately aborted and the status bar shows a warning. Lower the **Gain** slider and try again.

## File Naming

Files are saved as `<BaseName>_001.wav`, `_002.wav`, etc. The counter only increments when a file with that name already exists. Changing the Base Name resets the counter to 001.

## Building from Source

Requirements:
- macOS 13+
- CMake 3.22+
- Xcode command line tools

```bash
git clone https://github.com/collinp2/cp_software.git
cd cp_software
git checkout claude/ir-capture-app
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The built app will be at `build/IRCapture_artefacts/Release/IR Capture.app`.

## Technical Details

- **Stimulus duration:** 3 seconds each (sweep + noise), 0.5 s silence pad between them
- **Frequency range:** 20 Hz – 20 kHz
- **Output format:** 24-bit mono WAV at the current device sample rate
- **Framework:** JUCE (develop branch), built with CMake
