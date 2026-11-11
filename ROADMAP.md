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

## Gaps to close (in the order we plan to do them)

1. **Mix editor.** All 16 channels: program, bank, level, pan, output, effect bus and send, key range, transpose, the
   MIDI switches. It works on the mix edit buffer, then stores to a User mix. (MIDI Quest: Mix, Mix.Mem.)
2. **Edit a channel's program from inside a mix.** (MIDI Quest: Patch.1-16.mix.)
3. **Effects editor.** Program effects and mix effects: configuration and every reverb, delay, chorus, Lezlie, overdrive
   and EQ parameter. (MIDI Quest: FX.Pgm, FX.Mix.)
4. **Effects banks.** Read and write all 128 program effects. (MIDI Quest: FX.Bnk.)
5. **A program together with its effects.** Copy, store and save them as a pair, so a program never lands with the wrong
   effect. This also covers card banks 1-8. (MIDI Quest: PatchFX, PatchFX.Cd1-8.)
6. **Bank librarian moves.** Drag to reorder, swap two slots, copy between banks, send a whole bank to User in one go,
   and save a bank or a set of mixes as its own .syx. (MIDI Quest: Patch.Bnk, Mix.Bnk.)
7. **Compare.** Hear the stored program against the edited one, like the COMPARE key.
8. **Play from the computer keyboard.** Letter rows play notes, number keys pick octaves, plus and minus set velocity,
   and keys 1-9 can be set to fixed notes for drums. (QS SoundEditor help.)
9. **Slower transfers on request.** A setting that spaces out big bank requests for slow interfaces, on top of the
   retries we already do. (QS SoundEditor help, "Trouble Shooting".)
10. **Name lists.** Export any bank or mix list as text or CSV for printing.
11. **Search across everything saved.** Every library and backup, not just the open one. (MIDI Quest's database.)
12. **Drum pad monitor.** Shows each incoming note and velocity, for setting up an e-drum kit with the QS.
    (QS SoundEditor's Drum Kit Calibration.) Low priority.

Still to confirm: QS SoundEditor Pro's full PDF help may list more, such as random or blended programs. We'll add
anything it shows.

## After that: Sound Bridge territory

Alesis Sound Bridge builds QCard images from your own samples, programs and mixes. It hasn't been updated in decades.

- Build a FLASH card image from WAV files: sample import, loop points, root keys, programs that use them.
- Read a card image back out to WAVs and .syx.
- Song and sequence data on cards, where the format allows.
- Write the image straight to a FLASH card over MIDI (QS Studio already writes card sectors).

## Never shipped

ROMs, samples, card images, factory banks and other Alesis data. QS Studio reads them from your own synth and files.
