# QS Studio roadmap

Where QS Studio stands against the two editors QS owners use most (SoundTower QS SoundEditor Pro and MIDI Quest 13 Pro),
then what comes after. The gap list comes from their published help, file lists and screenshots only. We don't take their
programs apart.

## Already in QS Studio

- Program librarian: read one bank or all banks, factory names built in, search, per-model libraries (QS6 and QSR kept apart).
- Mixes: read the 100 user mixes, list them, pick one on the synth.
- Program editor: all four sounds, every program parameter, drum programs, QS6 and QS7/8/QSR sample names.
- Store a program to a User slot; audition without storing.
- Full backup (Send All, 357 parts) and restore with a preview, Global optional.
- Move banks between a QS6 and a QSR.
- QCard: check for a card, read card banks, write a bank onto a FLASH card.
- Global settings editor.
- Front panel for each model, on-screen keyboard, wheels and sliders, and a MIDI controller passed through to the synth
  (the QS SoundEditor "External Keyboard" feature, for the QSR).
- Recording and auto-sampling.

## Gaps closed in 0.5.0

1. Mix editor: all 16 channels (program, bank, level, pan, output, effect level and bus, octave, semitone, key range, MIDI and
   controller switches), the mix's effects channel, live edits, Store to User mix. (MIDI Quest: Mix, Mix.Mem.)
2. A channel's program edited inside the mix, in the QS's per-channel edit buffer. (MIDI Quest: Patch.1-16.mix.)
3. Effects editor: all five configurations with every parameter the spec lists, shown per effect type, for the program and the mix.
   (MIDI Quest: FX.Pgm, FX.Mix.)
4. Effects banks: reading a bank now reads every program's effect too; effects are kept in the library and saved with banks.
   (MIDI Quest: FX.Bnk.)
5. Programs and effects stay together: Copy to a User slot, swap, copy, send a bank and save a bank all carry the effect when
   it's known. (MIDI Quest: PatchFX.)
6. Bank tools: save a bank (programs, effects, mixes) as .syx, send a whole bank to the User bank, swap or copy User programs.
   (MIDI Quest: Patch.Bnk, Mix.Bnk.)
7. Compare in the Program Editor.
8. Play from the computer keyboard, with octave and velocity keys. (QS SoundEditor.)
9. Transfer pace setting for slow interfaces. (QS SoundEditor "Trouble Shooting".)
10. Name lists exported as CSV.
11. Session logs with MIDI traffic, for bug reports.

## 0.7.0

Editor gaps closed (QS SoundEditor Pro, MIDI Quest, Sound Bridge):
- Undo and redo in the Program, Mix and Effects editors.
- Program Editor tools: copy, paste, swap and reset sounds; new program from scratch; randomize; blend two programs.
- Mix channel tools: copy, paste and swap channels.
- Search across every saved library, backup and .syx file; drag to swap User programs.
- Aftertouch: an on-screen Pressure slider, and a switch to block a controller's aftertouch.
- MIDI monitor for keys and e-drum pads (softest and hardest velocity per note).
- Card builder (Sound Bridge): WAVs to a QS card image with key splits, loops and a program per voice. Untested on hardware.
- Check-up page: model and OS version, MIDI link test, every Global setting explained, User bank stats, card slot.

Next:
- Hardware results for built cards; then velocity layers, drum voices and effects on built cards.
- Reviews of the Transfer, Capture and Setup pages (on hold), then the final pass on layout and ease of use for new users.
- QS SoundEditor Pro's full PDF help may list more. We'll add anything it shows.

## 0.6.0

- Sound Player (Play page) and the QS Sound Player plugin (VST3, AU, LV2, standalone): QCard images and internal ROM dumps.
- Builds for Windows, macOS and Linux.
- Effects editor regrouped: one column per effect send, a card per effect, delay and predelay times in milliseconds,
  input sources and modulation destinations named as the QS shows them.
- Toward the 1:1 emulation: see [docs/EMULATION.md](docs/EMULATION.md).

## QS6 samples

The QS6 doesn't hand out its sample ROM over MIDI: card reads return the card in its slot (tested: a 512 KB program card came
back, repeated). Its samples can be recorded through the audio outputs (Record & Sample), or read from the ROM chips with a
programmer. Recorded samples are the starting point for the card builder below.

## After that: Sound Bridge territory

Alesis Sound Bridge builds QCard images from your own samples, programs and mixes. It hasn't been updated in decades.

- Build a FLASH card image from WAV files: sample import, loop points, root keys, programs that use them.
- Read a card image back out to WAVs and .syx.
- Song and sequence data on cards, where the format allows.
- Write the image straight to a FLASH card over MIDI (QS Studio already writes card sectors).

## Never shipped

ROMs, samples, card images, factory banks and other Alesis data. QS Studio reads them from your own synth and files.
