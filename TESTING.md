# Hardware test checklist

For testing a build on real synths. Tick what works, note what doesn't (what you clicked, what the QS showed, what QS Studio said).
Back up both synths before anything that writes to them.

## Setup
- [ ] Put `QS-Studio.exe` in its own folder (not Program Files). After the first run there's a `QS Studio Data` folder next to it.
- [ ] Windows warning on first run: note the exact wording (SmartScreen "Windows protected your PC", or "unknown publisher").
- [ ] Connection: pick MIDI in/out, Connect. The pill says "Connected to your QS6 (OS ...)". Same for the QSR.
- [ ] Switching synths: the panel look changes, and `QSStudio.QS6.library` / `QSStudio.QSR.library` both appear in `QS Studio Data`.

## 0.8.0: what's new to test
- [ ] **Setup guide:** on first start (or Setup guide in the sidebar): Connect finds your QS ("Found your QS6"), Back up now
      runs a backup, Start playing lands on Programs. Is anything unclear for someone who's never used QS Studio?
- [ ] **Updates...** (sidebar) > Check for updates now. Once a newer build is out: the dialog lists what changed; Update now
      downloads, closes QS Studio, replaces it and starts the new one. Check your settings and libraries are still there.
      Windows: a console window flashes while it swaps the file. macOS: say whether it reopens without a security prompt.
- [ ] **Sidebar:** Play / Edit / Library / Synth. Can testers find things?
- [ ] **How this works** (top right) now holds the longer explanations; is the shorter page text enough?

## 0.7.0: what's new to test
- [ ] **Undo / Redo** (buttons, or Ctrl+Z / Ctrl+Y; Cmd on a Mac) in the Program, Mix and Effects editors. With Live edit on,
      the QS follows the undo.
- [ ] **Program Editor > Tools...:** copy a sound and paste it into another; swap two sounds; reset a sound; New program from
      scratch; Randomize (try 20% first); Blend with another program. Listen on the QS after each.
- [ ] **Mix Editor > Channel tools...:** copy, paste and swap channels.
- [ ] **Programs > Bank tools... > Search everything saved:** type part of a name; double-click a result to open it.
- [ ] **Drag a User program onto another** in the Programs list (connected): it asks, then swaps them on the QS.
- [ ] **Aftertouch...** (bottom bar): the Pressure slider sends aftertouch from the screen; switching off "Pass my controller's
      aftertouch" stops a controller's pressure reaching the QS. Try a patch that uses aftertouch.
- [ ] **Aftertouch... > MIDI monitor:** hit keys or pads soft and hard; it shows each note's softest and hardest velocity.
- [ ] **Cards & ROM > Build a card from WAV files...:** pick a few WAVs (name a set like Piano_C2.wav, Piano_C3.wav,
      Piano_C4.wav to get one split voice). Play the result on the Sound Player. Then, only with a FLASH card you can rewrite:
      Write image to FLASH card, and see whether the QS lists the voices (card sample groups) and the programs play them.
      This is the one we most need hardware results for.
- [ ] **Setup > Check-up:** Run the check-up with the QS connected; save the report and send it, whether it looks right or not.

## 0.6.2: fixes to check
- [ ] **Sound Player with the Classical card:** notes stop when you let go, and playing more notes doesn't build up a whine.
      (The cause: the player added its sound onto a buffer that still held the last block, so every block fed the next.)
- [ ] **Decay and Sustain:** for pianos, mallets and plucks, set Sustain around 0.2 and Decay 1-3 s so held notes fade like
      the QS does. Sustain 1 holds the sample's loop for as long as the key is down.

## 0.6.1: fixes to check
- [ ] **Sound Player with your QCard .bin:** opens at a sane level. The log line "Sound Player: opened ..." says which byte order
      it found. If it refuses the file ("doesn't point at audio"), send the file and the log.
- [ ] **Mix + QS Studio keyboard:** pick a layered or split mix (two or more channels with Keyboard on); the on-screen keys and
      your controller now play every channel the QS's own keys would, not just channel 1.
- [ ] **Effects Editor:** the box at the top explains the configuration and says which sends the program uses; effects with mix 0
      show as off; a send without its own reverb shows "Into Reverb 1". Change the configuration with Live edit on: QS Studio
      reads the QS's new defaults back.

## 0.6.0: what's new to test
- [ ] **Mix switching:** click through 10-20 mixes, fast and slow. QS Studio now re-sends Mix mode every time, spaces the steps
      out, checks the QS's mix name afterwards and retries once if it didn't switch. If one still fails, the status bar says so:
      send that day's log (Connection > Open the logs).
- [ ] **Sound Player** (Play page): Audio output... pick your interface; open the QSR ROM dump (or a card image); play voices
      from the keyboard, your controller and the computer keys. Voices marked "% in this file" need the ROM's second chip.
- [ ] **QS Sound Player plugin:** load the VST3 in your DAW, open the same file, play it from a MIDI track; save and reopen the
      project (it should remember the file and the voice).
- [ ] **Effects Editor:** easier to read? Each column is one send; delay and predelay are single millisecond controls now.
- [ ] **Slider values:** drag Volume and Controller A on the QS6 panel strip; the number shows over the cap, not cut off.
- [ ] macOS / Linux testers: does QS Studio start, find MIDI ports and keep its "QS Studio Data" folder next to it?

## 0.5.0: what's new to test (QS6)
Turn on **Log MIDI traffic** (Connection page) and attach today's log (**Open the logs**) to anything that misbehaves.
- [ ] Global > MIDI Program Select is **On** before you start.
- [ ] **Programs after mixes:** pick a few mixes, switch back to Programs, click programs. The QS6 should change program and
      stay out of the Global edit page. If it doesn't, the status bar says "The QS didn't switch" and the log has a "Check FAILED" line.
- [ ] **Read all banks** now also reads each program's effect (a little slower). Afterwards **Its effect** opens a program's effect.
- [ ] **Mix Editor:** Mixes > pick a User mix > **Edit this mix**. Change volume, pan and a channel's program; hear it.
      **Edit** on a channel opens that channel's program; change something; hear it inside the mix. **Store to User mix...** to a spare slot.
- [ ] **Effects Editor:** Get from QS (Program), change the reverb type and decay, hear it. Switch the configuration. Try Mix.
      Note any parameter that moves the wrong thing on the QS display (the spec has typos; we fixed the ones we found).
- [ ] **Compare** in the Program Editor: on = the original, off = your edits.
- [ ] **Bank tools...** on the Programs page: save a bank as .syx; swap two spare User programs; export names to CSV.
- [ ] **Computer keys:** with nothing in a text box, A W S E D... play notes, Z / X change octave, - / = velocity, Space stops.
- [ ] **Transfer pace** (Connection page): set Slow if bank reads time out.

## QS6 ROM: answered
The QS6 answers card reads with the card in its slot (your dump was a 512 KB program card, repeated), not its sample ROM.
Getting its samples means recording them (Record & Sample) or reading the ROM chips with a programmer.

## QS6 first
- [ ] **Back up now** (Backup & Restore): "complete backup, all 357 parts". Keep this file safe.
- [ ] Programs: click programs in User and Preset 1-3; the QS6 switches. Group keys (00-120) and 0-9 on the panel work.
- [ ] Panel: VALUE up/down, BANK left/right, PAGE left/right (MIDI channel), EDIT SELECT opens the editor, STORE.
- [ ] Strip: pitch wheel bends and springs back, mod wheel, Volume, Controller A.
- [ ] **Mixes:** MIX (or the Mixes tab), pick a Preset 2 mix. The QS6 changes mixes, and its LCD matches QS Studio's
      (Preset 2 mix 43 = "Stik&Pad 2").
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
