# QS sound card format

What QS Studio knows about QCard / Sound Bridge FLASH card images, from reading real cards. "Verified" means checked
against Alesis's own cards; "guess" means it matches what we've seen but isn't confirmed on hardware.
No card data is in this repository.

## Layout (8 MB card, big-endian 16-bit words)

| Bytes | What | Status |
|---|---|---|
| 0x00000 | `A5 C3 5A A5` (Z-series QCard) or `5A A5 A5 C3` (R-series QuadraCard) | verified |
| 0x00004 | 128 programs: 349 bytes each on Z cards (program byte 0 left out), 350 on R cards | verified |
| then | 100 mixes: 138 bytes (Z) or 123 bytes (R, the pre-2.00 mix format) | verified |
| then | 7040 bytes in 55-byte records, probably the 128 effects in a packing we haven't decoded | guess |
| 0x10000 | Sample header `00 70 38 1C`, then `02 40 00 00` (seen on 8 MB cards; `40` may be 64 Mbit) | verified bytes, meaning a guess |
| 0x10008 | Voice count, 16-bit | verified |
| 0x1000A | Card name, 14 characters | verified |
| 0x10018 | 14 group names, 6 characters each: slots 0-7 are keyboard groups 16-23, slots 8-13 drum groups 55-60 | slots 0-7 verified, 8-13 a guess |
| 0x1006C | Voice directory | verified |
| after | Samples, 16-bit signed, 48 kHz | verified |

## Voice directory

Each voice: group, index (1-based within the group, what a program's Sample number stores), a 10-character name, the
number of key splits, velocity layers - 1, two velocity split points (128 = unused), the split bytes (MIDI note - 12),
the top key (128), a pad byte to an even length, then 12 bytes per zone (`(splits + 1) x layers`, interleaved by layer):

| Byte | What |
|---|---|
| 0 | root key (MIDI note) |
| 1 | fine tune (values like 250 and 119 are common; meaning not decoded) |
| 2 | unknown (0 on nearly all zones) |
| 3 | level (252 on most zones) |
| 4-5 | loop offset in words from the start, 16-bit, so a loop must start within 65535 words (1.36 s) |
| 6-8 | end word address (22 bits; the top two bits of byte 8 are flags, meaning unknown) |
| 9-11 | start word address (22 bits; top bit of byte 9 picks the second chip in an internal ROM) |

Programs pick a card sample with Sample group = the voice's group and Sample number = its index (checked on the
Classical card's programs).

## Loops

Every zone loops. Playback runs from start to end - 1, then from start + loop offset to end - 1 again. The end word
holds a copy of the loop start, and about 8 guard words follow each sample (copies of the loop's first words), so the
interpolation never reads past the loop. One-shot sounds loop a short stretch of silence after the sound.

Alesis's cards reuse audio: several zones (and voices) point into the same sample data, sometimes one inside another.
A key range played at a fixed pitch (Bow Noise) is 127 zones of one sample with a root per key.

## Built cards

`core/qs_cardbuild.cpp` writes all of the above from WAV files. Rebuilding the Classical card's voices from their own
samples gives identical key ranges, roots, loops and audio when read back. What hasn't been tested is whether a QS
accepts a built card: the header bytes we don't understand are copied from Alesis's cards, the effects area is left
blank (programs on a built card have no effect), and only keyboard voices with one velocity layer are made so far.
