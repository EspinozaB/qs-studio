# QS Studio

A modern, portable toolkit for the Alesis QS6 / QS7 / QS8 / QSR. One `.exe`, no installer, no registry:
settings are saved next to the exe (`QSStudio.settings`).

## Status: milestone 1 (card + SysEx toolkit)

| Tab | What it does |
|---|---|
| Device | Pick MIDI in/out, connect, **Identify synth** (model + OS version via Universal Device Inquiry). |
| SysEx Librarian | Record dumps from the QS, **Ask QS to Send All** (opcode 0C), save/open `.syx`, list program and mix names, send `.syx` back (paced for the QS). |
| Card Tools | Open QCard / QuadraCard / Sound Bridge images (R and Z series): voices, programs, mixes. Export WAVs (loop + root key) or SFZ instruments. **Test card reader**, **Read card from QS** over MIDI, **Write image to FLASH card** (Sound Bridge replacement). |

Card read/write uses the documented QS SysEx opcodes 11-15 (sector erase / write / request, ACK / NACK).
The write checksum is "7-bit sum of the previous 1173 bytes"; reading verifies that before anything is written.

## Building

GitHub Actions builds it on every push: open the latest run under **Actions** and download **QS-Studio-windows**.
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
