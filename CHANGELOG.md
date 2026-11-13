# Changelog

The version shows next to the title (`project(... VERSION x.y.z)` in CMakeLists.txt). Each
release bumps it and adds an entry here: one line per change a player would notice.

## 1.3.0 — Nov 11, 2026

- **Guitar panel**, to play guitar SoundFonts the way the classic keyboard guitar instruments do.
  It appears by itself for a guitar (General MIDI 24-31, or an instrument named one), or for any
  instrument from the button beside the instrument (**Guitar** / **Standard**, detected or chosen), remembered for each
  instrument of each SoundFont. It starts in **Direct**: the SoundFont as it is.
  - **Zones:** the Main zone (E1-B4) plays notes or takes chords; the Repeat zones (C1-D#1, C5-C6,
    tinted on the keyboard) repeat them: strums up and down, muted strums, basses, single strings.
  - **Modes:** Solo; Harmony (each note with an interval: a fourth down, a fifth or an octave up,
    power chords, two octaves); Chords (the chord is recognized from the keys, even one key or two,
    voiced like a guitarist holds it and strummed); Bass & Chord (the bass, a second bass, the
    upper strings); Bass & Pick (the basses and each string on its own key). Plus **Pattern** (the
    held chord plays a rhythm by itself, on Ableton's grid: twelve rhythms) and **Hold**.
  - **Options for each mode:** Velocity Switch FX (a slow strum above or below a velocity, or soft
    notes muted), Chord Position (I to IV, or Kbd), Strings, Chord/Bass (Am/G), Alter Bass, Bass
    Mono, Bass & Pick's add-on keys (Unison, Chromatic), Harmony's interval.
  - **Solo's articulations:** H.O. (hammer-on and pull-off) and Slide between legato notes, always
    or on key switches (C1-D#1) and the pedal, with Tremolo, Sustain 1-2 Mono, Velo Mute and Muted;
    a Bass Zone; Velocity FX Slide Up, Slide Down, Bend and Reverse Bend.
  - **Controllers:** the pitch bender, mod wheel and aftertouch as Pitch, Slide (by semitones),
    Mono Bend (the lowest note) or Modulation.
  - Every note lies on a string: a new note on a string stops the one ringing there
    when the pick reaches it. Strums are played by a hand: a hard stroke is quicker, an up stroke
    quicker and on the high strings, the strings between fall a little early or late, the first
    ones hit are the strongest.
  - **Pattern**, **Hold** and **Direct** together in the panel's title bar; the rest stays out of
    the way in **CONTROLLERS** (folding to its title): **Strum time** (up to 250 ms), velocity curve, Accent, Release, the wheels, **Double** (a second
    take, 18 ms later and a little sharp, on the right), **Humanize** (small random changes of
    velocity, timing and tuning), **Tune** (Standard, Drop D, Drop C, Open G, DADGAD) and a stereo
    **Tremolo** (at a rate or locked to Ableton's tempo), in a box of their own: FEEL and SOUND
    side by side, the three wheels' jobs under them.
  - **Presets** hold every control of the panel: Acoustic Strum, Campfire, Double-Tracked, Slow
    Ballad, High Strings, Folk Pattern, Muted Rock, Fingerpicking, Bass & Pick, Lead Solo, Octave
    Lead, and Off (Direct). **Save current as…** keeps your own for every project; **Delete**
    removes one.
  - **The neck** lights each string as the pick crosses it (down from the low E, up from the high
    one) and shades the Chord Position's frets; the display shows the chord, a big strum arrow, the
    fret and its notes. Pattern, Direct, Hold and the on/off options are lit switches; each mode's
    options sit in a box named after it.
  - **The neck** shows what's played (a rosewood neck, frets, inlays, strings) and the chord's
    name in a display (about 45 chord types, slash chords, partial chords, intervals), with lights
    for the strum's direction and palm-muted notes. **Draw chords on it** to see their name, and
    click the name to hear them; saved with the project.
- Reverb and chorus show only their switches, playing as the SoundFont sets them; a click on their
  title (or a right-click on the bar) shows their knobs and presets.
- The window is in sections with their own colors: **SOUNDFONT** (the file, its author in italics
  beside it, and the instrument), **GENERAL** (Gain, Tone, the effects, PLAY:
  what every instrument has), then the instrument's own: **GUITAR**, or **INSTRUMENT**
  (Layer and Split, which a guitar's zones leave no room for: a guitar turns the layer off).
  SOUNDFONT and GENERAL fold to one line (a click on the title) saying what's loaded and the
  levels; GENERAL keeps its meter.
- **Help built in:** the ? button opens a guide (Start, Guitar, Controls) and About; the header
  explains whatever control the mouse is over (turn it off in the ? menu).
- Reverb preset "Short Room" and chorus preset "Bright", for acoustic guitar.
- The keyboard follows the neck: the notes the guitar sounds get the neck's green dots on their keys
  (the keys you press stay orange).
- The keyboard names its keys (each C with its octave, as Ableton counts them), lights the keys played,
  shows the notes held and their chord in its title, and with a guitar names its zones over them.
- A compact on-screen keyboard in its own **PIANO** section, folding to its title (it shows what's
  played; a MIDI or computer keyboard plays): the
  window is as tall as what it shows and stretches only sideways.
- The window never grows past the screen: with a guitar the effects and PLAY start folded, and
  opening a section folds others when it wouldn't fit.
- Sections fold: a click on REVERB / CHORUS, PLAY or GUITAR folds it to its title (its switch and
  presets stay), and the window shrinks. **▾ Keyboard** folds the keyboard. Remembered for every
  project.
- The on-screen keyboard leaves the computer's keys to the host (Ableton's own computer keyboard).

## 1.2.6 — Nov 11, 2026

- The window always shows the finished state after a project opens or the audio device changes
  (it could keep showing "No instruments" until the next change).
- Saving while another project is opening can't mix the two.
- A favorite star never lights up with no instrument chosen.
- Switching instruments in the window right after a clip's Program Change keeps that clip's
  instrument in memory too.

## 1.2.5 — Nov 11, 2026

- The plugin window never freezes while a SoundFont loads, and stays quick with very large
  SoundFonts (the instrument list is no longer copied per instrument).
- Saving while a project is being restored saves that project, never a mix of two.
- After a sample-rate change or a new SoundFont, Program Change instruments are kept in memory
  again.

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
