# QS Studio

A modern, portable toolkit for the Alesis QS6 / QS7 / QS8 / QSR. One `.exe`, no installer, no registry, nothing in AppData:
everything it saves goes in a `QS Studio Data` folder next to the program.

**Status: 0.4.2 pre-alpha.** Only being tested by the author so far. See [Versions](#versions).

## Platforms

| | Windows | macOS (Apple silicon and Intel) | Linux |
|---|---|---|---|
| QS Studio (editor, librarian) | yes | yes (unsigned for now) | yes |
| QS Sound Player plugin | VST3 + standalone | VST3, AU + standalone | VST3, LV2 + standalone |

Builds of every platform are on the [latest release](https://github.com/EspinozaB/qs-studio/releases/tag/latest).
The Windows build is the one tested most; macOS and Linux builds are new in 0.6.0.

## What it does

The window works like a hardware editor: pages in a sidebar grouped by job, a plain-language summary at the top of each page and a
**How this works** button with step-by-step help. The look follows the synth, and the panels are drawn to match the hardware:

- **QS6, QS6.1, QS7, QS8 (and the .1 versions):** the black keyboard panel left to right: EDIT MODE (VALUE up/down, EDIT SELECT,
  STORE, PAGE / PROGRAM CHANNEL SELECT), the 2 x 16 character LCD, PLAY MODE (MIX, PROGRAM, BANK with COMPARE / GLOBAL), the 13 group
  keys and 0-9 with the Direct Select labels printed above and below them (from the QS6 manual). The strip below has the pitch and
  mod wheels, VOLUME and CONTROLLER A on the QS6 (A-D on the others), and the model's keyboard: 61 keys (QS6 family), 76 (QS7), 88 (QS8).
- **QS6.2, QS8.2:** the silver panel with the blue key blocks either side of the LCD, rounded keys, SEQ SELECT and TRANSPOSE (shown,
  not active yet); the sliders are labelled Filter, Delay, Envelope, Reverb.
- **QSR:** the 1U rack face: PHONES and VOLUME, the LCD (big number, two lines, MIX / EDIT / 1-16 indicators), the VALUE knob,
  CURSOR / EDIT / MIDI CH / STORE, MIX / PROG / bank DOWN / UP, the two card slots and POWER. CURSOR and MIDI CH work like on the unit.

Model names are shown in plain text, not as the maker's logos.
Change it under **Panel look**.

| Page | What it does |
|---|---|
| **Play** > Programs | Every bank (User, Preset 1-3, GenMIDI, card banks 1-11) with names and search, and the virtual front panel. Click, arrow or use the panel buttons to select programs on the QS; double-click or EDIT to edit. **Read this bank from QS** fills in names once; they are cached in `QSStudio.library` next to the exe. Audition a program in the edit buffer, store it to a User slot, or copy a bank onto a RAM card bank. |
| **Edit** > Program Editor | All four sounds on one screen plus full pages for Voice, Pitch, Filter, Amp, Range, Mod, LFOs, Envelopes and Tracking, and every drum of a drum kit. Changes go to the QS live. Get/Send the edit buffer, Store to User, open/save .syx. Undo/Redo; **Tools...** copies, pastes, swaps and resets sounds, starts a new program, randomizes or blends two programs. |
| **Play** > Sound Player | Opens a QCard image or a QS internal sample ROM dump and plays its voices (key splits, velocity layers, loops) from the keyboard, your controller or the computer keys, through this computer's audio. Not the full QS engine yet: no QS filters, envelopes or effects. The same player is a VST3/AU/LV2 plugin. |
| **Edit** > Mix Editor | The 16 channels of a mix: program and bank, on/off, volume, pan, output, effect level and bus, octave and semitone, key range, MIDI in/out, keyboard and controller switches, and which channel's effect the mix uses. Live edits, Store to User mix, open/save .syx. **Edit** on a channel opens that channel's program inside the mix. Undo/Redo and channel copy, paste and swap. |
| **Edit** > Effects Editor | An effect patch for the program or the mix: the configuration (1 Reverb, 2 Reverbs, Lezlie and Reverb, Reverb and EQ, Overdrive and Lezlie), then every pitch, delay, reverb, lezlie, overdrive, EQ and modulation parameter, shown for the effect types in use. Live edits, Store to a User effect, open/save .syx. |
| **Transfer** > Backup & Restore | **Back up now** asks the QS for all User programs, mixes, effects and Global settings and saves one .syx (checked for all 357 parts). **Restore** previews a file and sends it back (Global only if you tick it). Manual capture for dumps started from the QS. |
| **Transfer** > Move banks QS6 <-> QSR | Three steps: back up, convert, then restore or copy to a card. See below. |
| **Transfer** > Cards & ROM | Open QCard / QuadraCard / Sound Bridge images and export WAVs or SFZ; test the card slot, read a FLASH card, write an image to a FLASH card. **Dump internal sample ROM** saves the built-in samples on units that answer card reads with their ROM (confirmed on a QSR; a QS6 returns the card in its slot instead, and QS Studio now says so); resumable, about 1 hour per 8 MB. Keep ROM dumps private. |
| **Transfer** > Cards & ROM: Build a card | **Build a card from WAV files** makes a QS card image from your own samples, like Alesis Sound Bridge did: files named like `Piano_C3.wav` become one voice split across the keys, loops in the WAVs are kept, and each voice gets a program. Play it on the Sound Player or write it to a FLASH card. New and untested on hardware. |
| **Capture** > Record & Sample | Records the QS through your audio interface to WAV (16/24-bit) with live monitoring. **Auto-sample notes** plays a note range at chosen velocities and saves one file per note. |
| **Setup** > Connection | Pick MIDI in/out; QS Studio reconnects at startup and asks the synth what it is (model + OS version). **Transfer pace** slows long reads for flaky interfaces. **Open the logs** shows the daily session logs (what you clicked, what went to and from the synth) to attach to bug reports. |
| **Setup** > Global Settings | Read and edit Controller A-D and pedal CC numbers, MIDI Program Select and General MIDI mode on the QS, and try Controllers A-D (the QS6 only has the A slider). |
| **Setup** > Check-up | Asks the QS everything QS Studio can over MIDI and explains it: model and OS version, a timed SysEx test, every Global setting (with warnings for the ones that cause silence or ignored program changes), User bank stats and the card slot. Saves a report for bug reports. Changes nothing. |

The strip along the bottom plays the QS on the chosen MIDI channel: keyboard (your computer keys work too), pitch wheel, mod wheel,
Controller A (A-D on models with four sliders) and volume. **Hide keyboard** folds it away.

**Names for every bank, programs and mixes:** the factory names of Preset 1-3 and GenMIDI (programs and mixes) are built in, so
they show straight away. **Programs / Mixes** (or PROGRAM / MIX on the panel) switches the list. **Read all banks from QS** reads your
own User, Preset and GenMIDI programs, or mixes, straight from the synth (a few minutes, once). Or **Import a folder of .syx banks** and
choose which file fills which bank (the factory `Q678PST1`-`4` files from the
[Alesis Archive Project](https://github.com/jzemeocala/Alesis-Archive-Project/tree/HEAD/BANKS/FACTORY) are matched automatically;
programs and mixes are both imported). Imports only change QS Studio's list; to write a bank to the synth use Backup & Restore.

Picking mixes over MIDI: the QS only changes mixes on program changes when Global > MIDI Program Select is set to a **channel**
(not "On"). QS Studio offers to set it to its channel the first time you pick a mix. Reading Preset mixes sets it for the read and
puts your setting back afterwards.

**Where things are saved:** only in `QS Studio Data`, next to the program (on macOS, next to the `.app`; for a Linux AppImage,
next to the `.AppImage`). Nothing goes in AppData, the registry or your home folder, so the QS Studio folder can live anywhere
you can write to: Documents, the Desktop or a USB stick (not Program Files, which is read-only for normal programs; QS Studio
warns you if it can't write). **To update, replace the program and keep `QS Studio Data`.** The Connection page shows the folder
and opens it. Files from older versions (next to the exe, or in AppData) are moved in automatically.
- `QSStudio.<model>.library`: what was read or imported, **one file per model** (`QSStudio.QS6.library`, `QSStudio.QSR.library`),
  so two synths never overwrite each other. Export / Import on the Connection page to keep a copy or share one.
- `Backups\`: backups made on the Backup & Restore page.
- `QSStudio.settings`: MIDI ports, look, last model and other choices.

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

See [ROADMAP.md](ROADMAP.md) for the feature list against QS SoundEditor Pro and MIDI Quest, and the Sound Bridge work after it.
Longer term:

1. Card + SysEx toolkit and librarian (done)
2. Editor (programs done; mixes, effects next)
3. QS player VST3 / standalone using your own card images and decoded programs
4. 1:1 emulation core (needs the firmware EPROM dump and the sound/effects ASICs emulated)

## Versions

Version numbers follow [Semantic Versioning](https://semver.org): `MAJOR.MINOR.PATCH`. Everything stays below 1.0 until the
first public release. The stage is shown next to the number:

| Stage | Who has it | What it means |
|---|---|---|
| **pre-alpha** (0.4.x, now) | the author | Features still being built and changed daily; much untested on hardware. |
| **alpha** (0.5.x and up) | a small group of QS owners | Most features in, expect bugs and changes; back up your synth first. |
| **beta** | a wider group | Feature-complete for 1.0 (QS6 and QSR); only fixes from here. |
| **release candidate** (`1.0.0-rc.1`) | everyone who wants it | Believed ready; last chance to catch problems. |
| **1.0.0** | public | First stable release. |

PATCH goes up for fixes, MINOR for new features. (There is no "gamma" or "delta": after beta comes the release candidate.)

## Windows security prompts

QS Studio never asks for administrator rights (its manifest requests `asInvoker`), so it doesn't trigger a UAC prompt. The
"Windows protected your PC" (SmartScreen) or "unknown publisher" warning appears because the exe isn't code-signed yet. Until it
is: **More info > Run anyway**, or right-click the exe > Properties > tick **Unblock**. Signing is planned before the alpha.

## Trademarks, copyright and what ships

QS Studio is an unofficial, independent project. It is not made, sponsored or endorsed by Alesis or inMusic Brands.
Alesis, QuadraSynth and the QS model names are trademarks of their owners and are used here only to say which synths QS Studio
works with. The front panels are drawn from scratch to show where the controls are; they carry the model name in plain text and
no maker's logo or stylised model logo.

What is in this repository and the release: our own code and drawings, parameter layouts and sample/function name tables taken
as facts from the published QS manuals and SysEx spec, and the 10-character factory preset and mix **names** (no program, mix,
effect or sample data). What is never included or published: ROM dumps, card images, factory or user bank `.syx` files, and samples.
Those stay on your own computer (`.gitignore` blocks them).

This is a practical summary, not legal advice.

## Thanks

- [Project Q](https://github.com/brianpeat/project-q) (Brian Peat) for published findings used here: the QuadraSynth Plus Piano,
  QS6.1 and QS8.1 device inquiry codes, and that some units answer card reads with their built-in sound ROM. No code is copied.
