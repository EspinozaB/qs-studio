# QS Studio

A modern, portable toolkit for the Alesis QS6 / QS7 / QS8 / QSR. One `.exe`, no installer, no registry:
settings are saved next to the exe (`QSStudio.settings`).

## What it does

The window works like a hardware editor: pages in a sidebar grouped by job, a plain-language summary at the top of each page and a
**How this works** button with step-by-step help. The look follows the synth: a QS6 gets a green LCD and the QS6 front panel
(PROGRAM / EDIT SELECT / STORE, BANK and VALUE arrows, the 13 group buttons and 0-9); a QS7, QS8 or QSR gets the grey-blue LCD and the
rack panel (PROG / EDIT / STORE, bank DOWN/UP, VALUE knob). Change it under **Panel look**.

| Page | What it does |
|---|---|
| **Play** > Programs | Every bank (User, Preset 1-3, GenMIDI, card banks 1-11) with names and search, and the virtual front panel. Click, arrow or use the panel buttons to select programs on the QS; double-click or EDIT to edit. **Read this bank from QS** fills in names once; they are cached in `QSStudio.library` next to the exe. Audition a program in the edit buffer, store it to a User slot, or copy a bank onto a RAM card bank. |
| **Edit** > Program Editor | All four sounds on one screen plus full pages for Voice, Pitch, Filter, Amp, Range, Mod, LFOs, Envelopes and Tracking, and every drum of a drum kit. Changes go to the QS live. Get/Send the edit buffer, Store to User, open/save .syx. |
| **Transfer** > Backup & Restore | **Back up now** asks the QS for all User programs, mixes, effects and Global settings and saves one .syx (checked for all 357 parts). **Restore** previews a file and sends it back (Global only if you tick it). Manual capture for dumps started from the QS. |
| **Transfer** > Move banks QS6 <-> QSR | Three steps: back up, convert, then restore or copy to a card. See below. |
| **Transfer** > Cards & ROM | Open QCard / QuadraCard / Sound Bridge images and export WAVs or SFZ; test the card slot, read a FLASH card, write an image to a FLASH card. **Dump internal sample ROM** saves the built-in samples on units that answer card reads with their ROM (confirmed on a QSR); resumable, about 1 hour per 8 MB. Keep ROM dumps private. |
| **Capture** > Record & Sample | Records the QS through your audio interface to WAV (16/24-bit) with live monitoring. **Auto-sample notes** plays a note range at chosen velocities and saves one file per note. |
| **Setup** > Connection | Pick MIDI in/out; QS Studio reconnects at startup and asks the synth what it is (model + OS version). |
| **Setup** > Global Settings | Read and edit Controller A-D and pedal CC numbers, MIDI Program Select and General MIDI mode on the QS, and try Controllers A-D (the QS6 only has the A slider). |

The strip along the bottom plays the QS on the chosen MIDI channel: keyboard (your computer keys work too), pitch wheel, mod wheel,
Controller A (A-D on models with four sliders) and volume. **Hide keyboard** folds it away.

**Names for every bank:** Programs > **Read all banks from QS** reads User, Preset 1-3 and GenMIDI straight from the synth (a few minutes, once).
Or **Import a folder of .syx banks** and choose which file fills which bank (the Alesis factory `Q678PST1`-`4` files from the
[Alesis Archive Project](https://github.com/jzemeocala/Alesis-Archive-Project/tree/HEAD/BANKS/FACTORY) are matched automatically).
Imports only change QS Studio's list; to write a bank to the synth use Backup & Restore. No Alesis data ships with QS Studio.

**QS6 and QS7/QS8/QSR banks:** the QS6 has its own sample ROM; the QS7, QS8 and QSR share another. A program stores only a sample
group and number, so a QS6 program can play a different sample on a QSR. **Move banks QS6 <-> QSR** (or Programs > **Convert a bank file**) rewrites each
program to the same-named sample on the other model (exact, or a close name such as GrandPiano -> GrndPianoL) and writes a report of
every change and every sample with no match. Sample tables come from the sample charts in the Alesis reference manuals
(`tools/gen_samples.py`); the QS7/QS8 chart was checked against a QSR's ROM directory. The QS6 chart is unverified until a QS6 ROM is read.

**Play from your own MIDI keyboard:** pick it under **Controller in**; everything you play goes to the QS on the chosen channel.

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
