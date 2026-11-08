# SCSP Synth - DPF plugin (VST3 / CLAP / LV2)

DSP only for now (no custom editor window - hosts show their generic parameter list). Built and tested on Linux with a small CLAP host (`clap_host.cpp`).
GPL-2.0-or-later (Mednafen-derived SCSP core + reverse engineered 68000).

## Build on Windows (MSYS2 MinGW64 shell)
    pacman -S --needed make git mingw-w64-x86_64-gcc
    git clone --depth 1 https://github.com/DISTRHO/DPF.git dpf       # inside this folder
    make DPF_PATH=dpf        # DPF_PATH means the path where you're building. I just use "C:\dev\dpf_plugin"
Result: `bin/SCSPSynth.vst3`, `bin/SCSPSynth.clap`, `bin/SCSPSynth.lv2`. Copy the .vst3 folder to `C:\Program Files\Common Files\VST3`.
(Linux/macOS: the same `make` works with g++/clang.)

## Loading the House of the Dead sounds
The plugin never contains ROM data. Tell it where your ROMs are, any one of these ways:
1. Put `epr-19720_1.31`, `mpr-19721_1.32`, `mpr-19722_1.34` (the `.31/.32/.34` names also work) in a `roms` folder inside `%APPDATA%\SCSPSynth\` (Linux: `~/.config/SCSPSynth/roms/`), or in a `roms` folder next to the plugin.
2. Or create `%APPDATA%\SCSPSynth\config.txt` with lines such as
       rom=C:\path\to\epr-19720_1.31
       ram=C:\path\to\scsp_ram.bin
       program=18
   (`ram=` = raw 512 KB SCSP RAM image, little-endian words as dumped by the harness; `program=` = program loaded on start.)
3. Hosts that can show file-path states can set the `rom` / `ram` states directly.

Programs: 0 = Init (editable patch), 1-44 = HOTD instruments 00-2B (program N = instrument N-1; 18 = piano 0x11, 16 = FM 0x0F, 1 = drum kit 00).
HOTD programs use their own data; only Gain and Bend Range apply. The "ROM Loaded" output parameter shows 1 when the ROMs were found.

## Init patch / exploring the chip
Waveform 0-4 are built-in single cycles; **5 = RAM window**: any address/length in SCSP RAM (8- or 16-bit). With the HOTD ROMs (or a RAM image) loaded, set
address 0x28C0, MDL 13, stack seeds 3368 / 0CC6, stack write inhibit on, sweep ticks 15 step 2 to recreate FM instrument 0F by hand.
Envelope (AR D1R DL D2R RR KRS), TL, direct level, pan, LFO (freq / pitch+amp wave and depth) and the modulation block (MDL, MDXSL, MDYSL, seeds, sweep) are all parameters.
Other notes sounding feed the SCSP sound stack exactly like on the chip, so FM voices change when you play chords.

## Not done yet
Editor window (ROM browse button, instrument list), DSP effects program, per-note stack/sweep UI, raw-RAM-image sample-set picker. `clap_host.cpp` is a test tool only.
