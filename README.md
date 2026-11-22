# VOIDCARVE — Spectral Sidechain Carver (VST3)

A Trackspacer-style plugin. It turns down only the frequencies of your music that clash with the sidechain (vocal, kick, lead) at the moment they clash, so the sidechain sits on top of the mix without pumping the whole track.

**Engine:** 32 log-spaced bands from 20 Hz to 20 kHz, each with its own attack/release envelope, using a 2048-pt STFT with 8× overlap. Latency is 2048 samples, and FL Studio compensates for it automatically.

## Controls
| Knob | What it does |
|---|---|
| **Amount** | Depth of the carve (up to 24 dB) |
| **Sense** | Sidechain sensitivity (±24 dB) |
| **Attack / Release** | How fast the carve reacts and recovers |
| **Low Cut / High Cut** | Frequency range that gets carved. You can also drag the amber handles on the display |
| **Mix** | Blend between the dry and carved signal |
| **Output** | Output gain |

On the display, **violet** is your music, **cyan** is the sidechain, and the **magenta** curve is the live cut. The header light turns green when a sidechain signal is coming in.

## Get the installer .exe

**Option A: GitHub (no tools needed)**
1. Create a new GitHub repo and upload this whole folder, including `.github/`.
2. Go to the **Actions** tab. The build runs automatically (about 10 min).
3. Open the finished run and download **VoidCarve-Windows-Installer**. It contains `VoidCarve-1.0.0-Windows-Setup.exe`.

**Option B: Build on your PC**
Install Visual Studio 2022 (the "Desktop development with C++" workload), CMake, Git and Inno Setup 6. Then double-click `build_windows.bat`.

## FL Studio setup
See `installer/AfterInstall.txt`. The installer also shows it at the end. In short: put VoidCarve on the music insert, route the vocal track to it with **"Sidechain to this track"**, then choose that input in the plugin wrapper's settings.

## Tests
`tests/engine_test.cpp` checks three things: the plugin is transparent when idle, it carves the right band, and it does nothing when no sidechain is connected.
`g++ -O2 -std=c++17 -ISource tests/engine_test.cpp -o t && ./t`

## License note
VoidCarve is built with JUCE 8, which is free under its Starter licence or AGPLv3. Check the licence terms before selling it.
