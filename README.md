# MPC-16 Crunch Sampler (VST3 / AU / Standalone)

[![Made with Antigravity](https://img.shields.io/badge/Made%20with-Antigravity-ff7a00?style=for-the-badge)](https://antigravity.google)

An MPC-style 16-pad drum & phrase sampler audio plugin built with **C++17 and JUCE**, featuring independent granular time-stretching, classic hardware repitch, per-pad bitcrusher & sample-rate decimator, and master MPC-60 / SP-1200 vintage emulation modes.

## Features

- **4x4 MPC Drum Pad Grid (Pads A01–A16, MIDI Notes C1–D#2)**:
  - Velocity-sensitive pad triggering via MIDI or mouse click position.
  - **Drag & Drop** `.wav`, `.aiff`, `.flac`, `.ogg`, or `.mp3` files onto individual pads (or drop multiple files at once to auto-fill sequential pads) or onto the waveform display.
  - **Built-in Factory Kit**: Pre-loaded with 16 synthesized drum & chord/stab samples (Kicks, Snares, Clap, Rim, Closed/Pedal/Open Hats, Toms, Cowbell, Shaker, Crash, Lo-Fi Rhodes Chord, Vinyl Stab) so you can play immediately on launch.
  - **Full Level** mode (forces 100% velocity) and **16 Levels (Pitch)** mode (spreads the currently selected pad chromatically across all 16 pads from `-4` to `+11` semitones).
  - **4 Choke Groups** (with click-free 3 ms choke fade; Hats on Pads 6–8 are linked to Choke Group 1 by default).
- **Interactive Waveform & Sample Trimming**:
  - Draggable **Start** and **End** trim markers directly on the waveform.
  - Live animated playhead and **Reverse** playback mode.
  - **One-Shot** vs **Gate (Note-Off)** trigger modes.
- **Pitch & Real-Time Time-Stretching**:
  - **Granular Time-Stretch Mode**: Dual Hann-windowed overlap-add (OLA) engine allowing independent pitch shifting (`-24` to `+24` semitones + fine tune cents) and time-stretching (`0.50x` to `2.00x`) with adjustable grain size (`10–100 ms`).
  - **Classic MPC Repitch Mode**: 4-point Catmull-Rom cubic interpolation where pitch and speed are organically coupled like vintage hardware samplers.
- **Lo-Fi Bitcrusher & Sample-Rate Decimator**:
  - Per-pad **Bit Depth** (`1.0` to `16.0` bits), **Sample-Rate Decimation** (`1x` to `64x` sample-and-hold aliasing), **Saturation Drive** (`0` to `24 dB`), and parallel **Crush Mix** (`0–100%`).
  - **Global Vintage Bus Emulation**: Switch between `Clean 32-Bit`, `MPC-60 Vintage (12-Bit / 40 kHz)`, and `SP-1200 Crunch (12-Bit / 26.04 kHz)`.
- **Amplitude & ADSR Envelope**:
  - Per-pad `Attack`, `Decay`, `Sustain`, `Release`, `Level (dB)`, and equal-power `Pan`.

---

## Building the Plugin (macOS / Windows / Linux)

### Prerequisites
- **CMake** (`>= 3.22`) — install on macOS via `brew install cmake` or download from [cmake.org](https://cmake.org/download/)
- **C++17 Compiler** — Xcode Command Line Tools (`xcode-select --install`) on macOS

### Configure & Build
CMake will automatically fetch **JUCE 8.0.4** via `FetchContent` on the first configure step (or you can pass `-DJUCE_DIR=/path/to/JUCE` to use an existing local checkout):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j8
```

### Build Outputs
With `COPY_PLUGIN_AFTER_BUILD TRUE` enabled in `CMakeLists.txt`, the compiled plugins are automatically installed to your system plugin folders on macOS:
- **VST3**: `~/Library/Audio/Plug-Ins/VST3/MPC-16 Crunch Sampler.vst3`
- **AU (AudioUnit)**: `~/Library/Audio/Plug-Ins/Components/MPC-16 Crunch Sampler.component`
- **Standalone App**: `build/MPC16Sampler_artefacts/Release/Standalone/MPC-16 Crunch Sampler.app`
