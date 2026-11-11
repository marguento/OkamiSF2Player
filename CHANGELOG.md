# Changelog

The version shows next to the title (`project(... VERSION x.y.z)` in CMakeLists.txt). Each
release bumps it and adds an entry here: one line per change a player would notice.

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

## 1.0.0 — Nov 2026

- First version: any .sf2 through FluidSynth, as an AU and VST3 instrument.
