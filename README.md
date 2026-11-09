# QS Studio

A modern, portable toolkit for the Alesis QS6 / QS7 / QS8 / QSR. One `.exe`, no installer, no registry:
settings are saved next to the exe (`QSStudio.settings`).

## What it does

| Tab | What it does |
|---|---|
| Library | Every bank (User, Preset 1-3, GenMIDI, Card A/B) with program names and search. Click or arrow through programs to select them on the QS; double-click to edit. **Read bank from QS** fills in names once; they are cached in `QSStudio.library` next to the exe. Load a .syx into a bank, send a program to the edit buffer, or store it to a User slot. |
| Editor | All four sounds on one screen (group, sample, level, pan, effects, cutoff, attack, release, key mode, velocity curve, transpose) plus full pages for Voice, Pitch, Filter, Amp, Range, Mod, LFOs, Envelopes and Tracking, and every drum of a drum kit. Changes go to the QS live (opcode 10, or a full edit-buffer dump for drums and the name). Get/Send the edit buffer, Store to User, open/save .syx. |
| Record | Records the QS through your audio interface to WAV (16/24-bit). **Auto-sample notes** plays a note range at chosen velocities and saves one file per note. |
| Device | Pick MIDI in/out, connect, identify the synth (model + OS version). |
| SysEx Librarian | Record dumps, **Ask QS to Send All**, save/open `.syx`, check a backup is complete (357 parts), send `.syx` back. |
| Card Tools | Open QCard / QuadraCard / Sound Bridge images, export WAVs or SFZ, test the card slot, read a card over MIDI, write an image to a FLASH card. |

The on-screen keyboard along the bottom plays the QS on the chosen MIDI channel (your computer keys work too).

**QS settings needed:** Global I/O set to MIDI, and Global > MIDI Program Select ON (needed to select and read Preset, GenMIDI and card banks).

The parameter layout comes straight from the Alesis QS SysEx spec (`tools/gen_params.py` generates `core/qs_params_table.inc` from it) and is checked
against all 640 factory programs. Choices the spec only numbers (key mode, mod sources and destinations, trigger types) show as numbers for now.

Card read/write uses the documented QS SysEx opcodes 11-15 (sector erase / write / request, ACK / NACK).
The write checksum is "7-bit sum of the previous 1173 bytes"; reading verifies that before anything is written.

## Building

**Download:** grab `QS-Studio.exe` from the [latest build release](https://github.com/EspinozaB/qs-studio/releases/tag/latest). It is rebuilt on every push to `main`.
The exe is not code-signed, so Windows SmartScreen will warn the first time: click **More info**, then **Run anyway**.
Locally: `cmake -B build && cmake --build build --config Release` (fetches JUCE 8).

Core tests (no JUCE): `cmake -B build -DQS_BUILD_APP=OFF && cmake --build build && ctest --test-dir build`

## Card image format notes

See `core/qs_card.cpp`. Summary:

- `0x000000` marker (`A5 C3 5A A5` Z-series, `5A A5 A5 C3` R-series), then 128 programs
  (349-byte stride on Z cards, 350 on R cards) and 100 mixes (138-byte new format / 123-byte old format).
- `0x010000` card header: name/version and voice group names.
- `0x01006C` voice table: name, key splits, up to 3 velocity layers, 12-byte zones
  (root, finetune, level, loop offset, 22-bit start/end word addresses).
- Samples: 16-bit signed big-endian mono, 48 kHz.

## Roadmap

1. Card + SysEx toolkit (this)
2. Editor (programs, mixes, effects, global; live editing with opcode 10)
3. QS player VST3 / standalone using your own card images and decoded programs
4. 1:1 emulation core (needs the firmware EPROM dump and the sound/effects ASICs emulated)

## Thanks

- [Project Q](https://github.com/brianpeat/project-q) (Brian Peat) for published findings used here: the QuadraSynth Plus Piano,
  QS6.1 and QS8.1 device inquiry codes, and that some units answer card reads with their built-in sound ROM. No code is copied.
