# Changelog

The version shows next to the title (`project(... VERSION x.y.z)` in CMakeLists.txt). Each
release bumps it and adds an entry here: one line per change a player would notice.

## 1.2.4 — Nov 11, 2026

- Saving the project never waits for a SoundFont that's loading.
- A damaged SoundFont is retried less and less often (1 to 30 minutes), not every minute.
- A controller's final value (volume, pan) survives a Reset All Controllers sent during a load.

## 1.2.3 — Nov 11, 2026

- During an instrument load, a fade or pan move (any controller) keeps its final value, and a
  Reset All Controllers isn't undone by a bend sent before it.
- A file chosen right after the audio device changed can't be mistaken for a lost one.
- A SoundFont that couldn't be read (still downloading, a drive mounting) is retried when it
  changes or a minute later.
- Instruments substituted by FluidSynth (a missing bank falls back to bank 0) stay in memory too.

## 1.2.2 — Nov 11, 2026

- Instruments a clip switches to with Program Change stay in memory, so switching back never
  reads them from disk again.
- A missing SoundFont loads again by itself within a second of reappearing (a drive plugged
  in); a damaged one says so ("Can't open") and isn't retried until it changes.
- Dense MIDI during an instrument load can't run the plugin out of room: past a limit only
  lasting changes are kept (note-offs, pedals, program and bank, the last pitch bend).

## 1.2.1 — Nov 10, 2026

- A note-off, pedal release or pitch-bend return that arrives while an instrument loads is no
  longer lost (no more stuck notes).
- Moving a reverb knob no longer clicks the chorus; moving a PLAY knob no longer resets the
  bend range or glide a clip set.
- A clip's Reset All Controllers no longer undoes Attack, Release, Fine, Glide or the layer's level.
- The first note no longer stutters while the interpolation table is built.
- Chorus depth reaches 20 ms at 192 kHz too.
- A project restored without a SoundFont, or whose file went missing, behaves consistently, keeps
  the layer's instrument, and loads the file again once it's back.
- Key ranges given in a SoundFont's global zones are shown on the keyboard.

## 1.2.0 — Nov 10, 2026

- Universal: Apple silicon and Intel Macs, from a signed and notarized installer.

## 1.1.0 — Nov 10, 2026

- Renamed Okami SF2 Player. Projects that used "SF2 Player" still open (same plugin codes).
- Faster and more faithful: 17-point sinc interpolation, about 10x cheaper than before (64
  string notes take 10 % of the CPU time available, not 146 %).
- PLAY panel: attack, release, velocity curve, transpose, fine tune, bend range, Poly / Mono /
  Legato and glide. All of them leave the SoundFont as it is at their defaults.
- Layer and split: a second instrument, on every note or below a split key, with its own level.
- Reverb and chorus presets (Room, Hall, Church, Cathedral...; Subtle, Wide, Ensemble).
- Effects to separate outputs: the reverb and chorus sends leave on their own outputs for
  Ableton's effects.
- Tone: a high cut for hiss in old recordings.
- The instrument list by family, with favorites (★) at the top and each number shown dimmer.
- ▶ plays a note of the chosen instrument.
- Each panel says how much the chosen instrument sends to the reverb and the chorus.
- The project remembers the file, both instruments and every knob, and Ableton asks to save
  after choosing them. A moved file is named, not forgotten.
- Works at 44.1 to 192 kHz (it played an octave low above 96 kHz).
- The window can be resized.
- .sf3 (compressed) SoundFonts open too.

## 1.0.0 — Nov 2026

- First version: any .sf2 through FluidSynth, as an AU and VST3 instrument.
