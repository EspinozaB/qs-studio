# Hardware test checklist

For testing a build on real synths. Tick what works, note what doesn't (what you clicked, what the QS showed, what QS Studio said).
Back up both synths before anything that writes to them.

## Setup
- [ ] Put `QS-Studio.exe` in its own folder (not Program Files). After the first run there's a `QS Studio Data` folder next to it.
- [ ] Windows warning on first run: note the exact wording (SmartScreen "Windows protected your PC", or "unknown publisher").
- [ ] Connection: pick MIDI in/out, Connect. The pill says "Connected to your QS6 (OS ...)". Same for the QSR.
- [ ] Switching synths: the panel look changes, and `QSStudio.QS6.library` / `QSStudio.QSR.library` both appear in `QS Studio Data`.

## QS6 first (not tested yet)
- [ ] **Back up now** (Backup & Restore): "complete backup, all 357 parts". Keep this file safe.
- [ ] **Built-in ROM check:** Cards & ROM, card slot empty, **Test card reader**. Note what it says.
  - If it reports the built-in sound ROM: **Dump internal sample ROM** (about 1 hour per 8 MB, resumable). Keep the file private.
  - If it reports NACK / no card: the QS6 doesn't expose its ROM this way; note it.
- [ ] Programs: click programs in User and Preset 1-3; the QS6 switches. Group keys (00-120) and 0-9 on the panel work.
- [ ] Panel: VALUE up/down, BANK left/right, PAGE left/right (MIDI channel), EDIT SELECT opens the editor, STORE.
- [ ] Strip: pitch wheel bends and springs back, mod wheel, Volume, Controller A.
- [ ] **Mixes:** MIX (or the Mixes tab), pick a Preset 2 mix. QS Studio asks to set MIDI Program Select to a channel: say yes.
  - [ ] The QS6 changes mixes, and its LCD matches QS Studio's (Preset 2 mix 43 = "Stik&Pad 2").
  - [ ] Back in PROGRAM: programs still change with Program Select on a channel. (Not confirmed yet: important.)
- [ ] **Read Preset 2 mixes from QS**: 100 of 100; afterwards Program Select is back to what it was (Global Settings page).
- [ ] **Read all banks from QS** (programs): names match the QS6.
- [ ] Program Editor: Get from QS, move a few knobs and hear it change, Store to a spare User slot.
- [ ] Global Settings: Read from QS shows your controller numbers; Controller B-D test sliders do something if a program uses them.

## QSR
- [ ] Same as above where it applies: panel (VALUE knob, CURSOR, MIDI CH, bank DOWN/UP, MIX/PROG), mixes, read banks.
- [ ] Card slot click on the panel opens the card banks.

## Moving banks
- [ ] Move banks QS6 <-> QSR: back up the QS6, convert, restore to the QSR (or copy to a RAM card bank). Programs that used Rhythm
      samples (Dreamscape, Raga Kit, Golden Dawn) sound right on the QSR.

## Record & Sample
- [ ] Pick the audio input, monitoring on, record a take, and find the WAV in `QS Studio Data/Recordings`.
- [ ] Auto-sample a short range (one octave, one velocity).
