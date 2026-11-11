# Toward a 1:1 QS emulation

Where things stand, what's known, and the plan. Nothing here ships Alesis code or data: the emulator will run the OS ROM
and sample ROMs that owners dump from their own synths.

## What a QS is made of

| Part | What it is | Status |
|---|---|---|
| CPU | Hitachi H8/510 (HD6415108) at 20 MHz, big-endian | MAME has an H8/500 core; its licence is to be checked before reuse. |
| OS ROM | 512 KB, one per model and version | Owner dumps in hand (below). |
| Sample ROM | QS6: 8 MB. QS7/QS8/QSR: 16 MB on two 8 MB chips | QSR first chip dumped over MIDI. QS6: needs a chip reader. |
| Sound generator | Alesis custom ASIC: 64 voices, sample playback, filters, envelopes | Undocumented. The main reverse-engineering job. |
| Effects processor | Alesis custom ASIC at 7.056 MHz (Quadraverb 2 lineage): reverb, delay, pitch, lezlie, overdrive, EQ | Undocumented. Second job. |
| Panel | Key and button scan, 2 x 16 character LCD (QSR: custom segment LCD), sliders, wheels | Straightforward once the I/O map is known. |
| I/O | MIDI, serial (PC/Mac), PCMCIA card slots | MIDI and cards first. |

MAME has a skeleton driver for the QS7 (`alesis_qs.cpp`, `MACHINE_IS_SKELETON`): it boots nothing yet. The sound chips,
panel and LCD are all TODO there.

## OS ROM dumps checked

`tools/fw_info.py` identifies a dump and prints the byte-sum checksum the QS itself shows:

| File | Model / version | Date in the ROM | Byte sum |
|---|---|---|---|
| qs6_1.00.BIN | QS6 OS 1.00 | Sept. 11th, 1995 | BB68 |
| Alesis_QS6_v2.00_F010.bin | QS6 OS 2.00 | Sep. 30, 1996 | F010 (matches the label) |
| Alesis_QS7_v1.02_DBCC.bin | QS7 OS 1.02 | Sept. 12th, 1996 | DBCC (matches the label) |
| QS8 v1.02.bin | QS8 OS 1.02 | Sept. 12th, 1996 | 63BA |

All four are 512 KB with each 16-bit word stored byte-swapped (MAME loads its QS7 ROM the same way). Reset vector 0x0001F4.
The ROMs hold the QS's own display text, which QS Studio now uses for effect input and modulation names.

## Sample ROM layout (QS7/QS8/QSR, from the QSR dump)

- Header: `00 80 00 00 04`, then the ROM name (`FQS78ROM v20220`).
- From byte 0x14: the voice directory, in the same format as a QCard's (name, key splits, velocity layers, then 12 bytes
  per zone: root key, fine tune, level, loop offset, end and start word addresses).
- Bit 22 of a zone's start address (the top bits QS Studio calls flagsB) selects the second 8 MB chip. The first chip's
  dump plays 228 of the 1094 voices fully and 114 partly; the rest are on the second chip.
- Every sample loops from its loop offset to its end. Single-cycle waves have offset 0 and loop whole. Drums loop a few
  silent words at the end. (Checked: "Pure Sine" plays at the right pitch, A = 220 and 440 Hz.)
- Group numbers: 0-15 the keyboard groups (Piano ... Sound FX), 24 Rhythm, 48-54 the drum groups (Kick, Snare, Toms,
  Cymbal, Percussion, Sound FX, Wave), 61 drum rhythms.

## Plan

1. **Sound Player (done, 0.6.0).** Plays the sample ROM and QCard voices as mapped, with a plain envelope. App page and
   VST3/AU/LV2 plugin. Useful now, and it proves the sample format.
2. **QS program engine (high level).** Play QS programs on those samples: four sounds, the QS's pitch, filter and amp
   envelopes, LFOs, tracking generator, mod matrix. Built from the program format QS Studio already edits, tuned against
   recordings of the real synth (Record & Sample's auto-sampler makes the reference set).
3. **Effects.** A model of the four-bus effects in the five configurations, tuned the same way.
4. **Low level, running the real OS.** H8/510 core plus the OS ROM, with the sound and effects ASICs implemented from what
   the OS writes to them. This is what makes it 1:1 (the real panel behaviour, the real parameter handling), and it needs
   the ASIC register map worked out by tracing the OS. The high-level engine from steps 2-3 becomes the ASIC model's
   reference.

What helps most now:
- A QS6 sample ROM read with a chip programmer (the QS6 doesn't hand it out over MIDI), and the QSR's second chip.
- Recordings of single notes from known programs on real units (the auto-sampler can do this hands-free).
- Photos of the main board chip markings, and the service manual's schematics, for the I/O map.
