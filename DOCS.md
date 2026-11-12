# Okami SF2 Player — User Guide

An instrument for Ableton Live that plays SoundFonts (`.sf2` and `.sf3`). With everything at
its default it sounds exactly like the original SoundFont: every control starts at a position
that changes nothing.

## Installing

1. Open the `Okami-SF2-Player-<version>.pkg` installer. It puts the plugin in
   `/Library/Audio/Plug-Ins` as AU and VST3 (Apple silicon and Intel, macOS 13 or later).
2. Get a SoundFont: the plugin was made for the
   [HQ Orchestral SoundFont Collection](https://musical-artifacts.com/artifacts/817), a free
   download of 522 MB with 195 instruments that carries the name of Roland's discontinued
   Edirol HQ Orchestral. It isn't included with the plugin: download it from that page.
   [docs/HQ-Orchestral-sends.md](docs/HQ-Orchestral-sends.md) lists what each of its
   instruments sends to the reverb and chorus.
3. Restart Ableton Live, or rescan plug-ins in its preferences.

## Putting it in Ableton

1. In Ableton's browser: **Plug-Ins → Marguento → Okami SF2 Player** (AU or VST3). Drag it to
   a MIDI track.
2. Choose a SoundFont in the top menu, or drag an `.sf2` / `.sf3` onto the window.
3. Choose an instrument and play.

If nothing sounds, check the notes' velocity in the clip: at 1-15 they're nearly silent, and
the plugin says so in a red line.

## File

- **File menu:** the last 12 SoundFonts opened, then "Open SoundFont...". The open one shows
  how many instruments it holds: `HQ_Orchestral... (195 instruments)`.
- **Drag and drop** an `.sf2` or `.sf3` to open it.
- **A moved or renamed file:** the project still remembers it, and the plugin says in red
  "Can't find" with its name. Put it back (or plug its drive in) and it loads again by itself
  within a second, with your instruments; or choose it again from the menu.
- **A file that can't be read** (damaged, or still downloading from the cloud) says "Can't
  open". It's tried again as soon as it changes, and otherwise after a wait that grows from one
  minute to thirty.
- **Memory:** only the chosen instruments' samples are loaded. A 500 MB collection takes tens
  of MB per track. Changing instruments can leave a moment of silence while its samples load.

## Instruments

- **Listed by family** when the SoundFont follows the General MIDI standard: Piano, Chromatic
  Percussion, Organ, Guitar, Bass, Strings, Ensemble & Choir, Brass, Reed (Sax, Oboe,
  Clarinet), Pipe (Flutes), Synth Lead, Synth Pad, Synth Effects, Ethnic, Percussive, Sound
  Effects and Drum Kits. Other SoundFonts are grouped by bank.
- **The dim number** after the name is its MIDI slot: `[040]` is program 40 of bank 0, and
  `[1:025]` program 25 of bank 1 (the variations). A Program Change in a clip with that number
  switches to it.
- **Search:** type part of a name ("flute") or a family ("brass"). Return picks the first
  match; Esc clears the search.
- **★ Favorites:** the star marks the chosen instrument. Favorites appear at the top of the
  list, in every track and project. Click again to remove it.
- **▶** plays a short note of the instrument (middle C, or the middle of its range).
- **`<` `>`** step to the previous or next instrument in the list.
- **The keyboard** lights the notes that arrive, plays on click (lower on a key = louder) and
  dims the keys the instrument doesn't have.

## Folding sections

A click on a section's title (**▾ REVERB**, **CHORUS**, **PLAY**, **GUITAR**) folds it to a strip
with its title, switch and presets, and the window shrinks; another click opens it. Reverb and
chorus fold together. **▾ Keyboard**, at the bottom left, folds the keyboard. The plugin remembers
which are folded, for every project.

## Gain and Tone

- **Gain:** output level, -24 to +12 dB. 0 dB is the original.
- **Tone:** cuts the highs, for the hiss of old recordings (a violin's bow noise, for example).
  All the way right is **Off**. 5-8 kHz softens hiss without dulling the sound; below 3 kHz it
  sounds muffled.

## Layer (a second instrument)

- **Off:** only the main instrument plays.
- **Layer:** a second instrument plays every note along with the main one (strings + piano,
  for example).
- **Split:** keys below the split note play the second instrument, the rest the main one (bass
  on the left, flute on the right, for example). The keyboard tints the second instrument's
  zone blue.
- **Split note:** C#-1 to G9, C4 by default. Change it with − / + or by dragging.
- **Level:** the second instrument's volume, -30 to 0 dB.
- The second instrument follows the clip's pedals, pitch bend and controllers.

## Reverb and Chorus

Each shows only its switch, as on the original sound module: the effects play as the SoundFont
sets them. A click on **▸ REVERB** or **▸ CHORUS** (or a right-click on their bars) brings back
their knobs and presets.

Each SoundFont says **how much** each instrument sends to the reverb and the chorus; the
plugin provides **one** shared reverb and **one** shared chorus (like a mixing desk). Each
panel's header says what the chosen instrument sends: `sends 10 %` or `sends nothing`.

- **✓** turns the effect on or off.
- **Reverb presets:** Default (FluidSynth's), Room, Studio, Hall, Church, Cathedral.
  **Chorus presets:** Default, Subtle, Wide, Ensemble. They move the knobs; change one by hand
  and the menu says "Custom".
- **Reverb:** Level 0-100 %, Room (size), Damping (how fast the tail's highs fade), Width
  (stereo spread).
- **Chorus:** Level 0-200 %, Rate 0.1-5 Hz, Depth 0-20 ms, Voices 1-10.
- **Instruments that send nothing:** many don't (in the HQ collection, the strings). To give
  them reverb, raise **CC91** in the clip (up to +20 %; **CC93** does the same for chorus), or
  put an Ableton reverb on the track.

### Effects to separate outputs

With this box checked, the plugin's reverb and chorus switch off and what each instrument
sends leaves **dry** on two outputs of their own, "Reverb Send" and "Chorus Send", in the
SoundFont's proportions. The main output is dry. In Ableton:

1. Create an audio track.
2. In **Audio From**, choose the plugin's track, then **Reverb Send**.
3. Set **Monitor** to **In** and add a reverb at 100 % wet.

Several tracks can send to that track and share one reverb.

## PLAY

At their defaults (x1, Linear, 0, Poly, Off) they change nothing.

- **Attack:** how long a note takes to come in, x0.25 to x4 of the original time.
- **Release:** how long it takes to fade after the key is let go, x0.25 to x4. Shorter also
  cuts noisy tails.
- **Velocity:** -100 to +100. Positive makes soft notes louder; negative, softer. **Linear** =
  as played.
- **Transpose:** -24 to +24 semitones. It uses the samples of the resulting note, as if you
  played another key.
- **Fine:** fine tuning, -100 to +100 cents.
- **Bend:** the pitch bend range, ±1 to ±24 semitones (±2 is the standard).
- **Mode:** **Poly** (normal), **Mono** (one note at a time; each note starts afresh),
  **Legato** (one note at a time; when notes are played connected, the next doesn't attack
  again: for violin, flute or sax lines).
- **Glide:** sliding from one note to the next, 0 (Off) to 2000 ms. In Legato, only between
  connected notes.
- **Mod wheel:** adds vibrato, as the SoundFont standard defines.

## Guitar

The **GUITAR** panel appears by itself, first under the instrument, when it's a guitar (General
MIDI programs 24-31, or an instrument named "guitar" or "gtr"). The button beside the instrument
says what it got: **Guitar** or **No panel**. For a SoundFont that doesn't follow General MIDI (a
guitar on program 0, a piano named "Gtr"), click it and choose **Guitar** or **No panel**; **Auto**
goes back to detecting. The choice is remembered for that instrument of that SoundFont, in every
project. The panel starts in **Direct**: every note as it comes, the SoundFont exactly as it is.
Choosing a mode turns Direct off.

**Zones.** The **Main zone** (E1-B4, MIDI 40-83: the guitar's own range) plays notes or takes
chords. The **Repeat zones** (C1-D#1 and C5-C6, tinted on the keyboard) repeat what the Main zone
holds. Notes outside the zones play as they are.

**Modes** (the buttons under the neck):

- **Solo:** notes as you play them. C5-C6 repeat the notes held (black keys muted).
- **Harmony:** one note at a time, with an interval (4th Down, 5th Up, Octave Up, Power Chord 1 —
  a fifth and a fourth up, Power Chord 2 — a fourth and a fifth up, 2 Octaves Up). The Repeat keys
  repeat it.
- **Chords:** the keys held in the Main zone are recognized as a chord (C E G or E G C: C; one key:
  its major chord; C + Eb: C minor; C + Bb: C7), voiced on six strings as a guitarist holds it,
  and strummed. Adding a key changes the chord. The Repeat keys strum it again: C, E, G up; D, F,
  A, B down; the black keys muted (C#, F#, A# up; D#, G# down).
- **Bass & Chord:** the chord plays its bass. C5: the bass; D5: the second bass (the fifth); the
  other Repeat keys strum the upper strings (Strings # of them).
- **Bass & Pick:** the chord plays its bass. C5 and D5: the basses; E5, F5, G5, A5: the 4th, 3rd,
  2nd and 1st strings; C1-D#1 strum the four upper strings.
- **Pattern:** the chord held in the Main zone plays the rhythm chosen in the options by itself:
  Down Eighths, Down Up, Folk, Pop, Ballad, Country, Rock Muted, Reggae, Funk Sixteenths, Waltz
  (3/4), Fingerpicking, Arpeggio. While Ableton plays, the steps fall on its grid; stopped, the
  rhythm starts with the chord, at Ableton's tempo.
- **Direct:** a plain sampler.
- **Hold** (by the display): chords keep sounding after the keys are let go, until the next chord.

Every note lies on a string: a new note on a string stops the one ringing there, as on a guitar;
letting the chord go stops them all (unless Hold).

**Options** (the box under the modes, only what the mode uses):

- **Velocity FX:** Slow Strum on Higher Velo / on Lower Velo (a chord played above or below
  **Velo** is strummed over **Time**), Velo Mute (notes under Velo are muted).
- **Chord Position:** where chords lie on the neck, by their highest note: I (E3-G#3, the open
  position), II (G3-B3), III (A#3-D4), IV (C#4-F4), or Kbd (the octave you play in).
- **Strings** (Chords): the highest and lowest strings strummed (1 is the high E, 6 the low E).
- **Strings #** (Bass & Chord): how many upper strings the Repeat keys strum.
- **Chord/Bass:** a major or minor chord over another bass key is played as such (G A C E: Am/G).
- **Alter Bass:** the same chord again plays its second bass. **Bass Mono:** the second bass stops
  the first.
- **Add-on keys** (Bass & Pick): the black keys among the string keys. Unison: C#5 the second
  bass, D#5 the 4th string, F#5 the 2nd, G#5 to C6 the 1st. Chromatic: a semitone below those
  (G#5 below the 1st string), and A#5 to C6 one to three semitones above the 1st.
- **Rhythm** (with Pattern), **Interval** (Harmony).

**Solo's articulations:**

- **Permanent:** H.O. (a key played while another is held, up to **Steps** semitones away, hammers
  on: the sounding string bends there without a new pick, and pulls off when let go) or Slide (the
  same, along the frets over **Time**).
- **Key switches:** hold C1, C#1, D1 or D#1 and the Main zone plays with its articulation (Muted,
  H.O., Slide and Tremolo by default); the **Pedal** can take one too (Off: it sustains as usual).
  Articulations: H.O., Slide, Tremolo (the notes picked again and again), Sustain 1-2 Mono (notes
  ring until a step of one or two semitones), Velo Mute (notes under Velo muted), Muted.
- **Bass Zone:** notes up to the key next to it play firmer and without articulations.
- **Velocity FX** add Slide Up, Slide Down, Bend and Reverse Bend: a note at Velo or harder slides
  up into its pitch, slides down from it, bends up to it, or starts pre-bent and comes down to it,
  over **Time** and **Steps**.

**Controllers** (the button at the top of the panel shows them): **Velo curve** (Linear, Convex,
Concave, S), **Accent** (the velocity where soft playing turns hard: below it velocities fill the
soft half, from it the hard half), **Release** (PLAY's release time), and what the **P.B.** (pitch
bender), **M.W.** (mod wheel) and **A.T.** (aftertouch) do and how far: Pitch, Slide (by semitones,
along the frets), Mono Bend (the lowest note only), Modulation, or as usual.

**The rest of the panel:**

- **Presets** (next to GUITAR): Acoustic Strum, Campfire, Double-Tracked, Slow Ballad, High Strings,
  Folk Pattern, Fingerpicking, Bass & Pick, or Off (Direct). Move a control and it says Custom.
- **Strum** (in the title bar): strum time, from the first string to the last, up to 250 ms (Off:
  a pick's 18 ms). **Hold** is beside it.
- **Controllers** (the title bar's button) adds, besides the curve, Accent, Release and wheels:
  - **Double:** a second take of every note, 18 ms later and 4 cents sharp, on the right, with
    the original on the left (double-tracking); the slider is the second take's level.
  - **Humanize:** small random changes to each note's velocity (up to ±5), timing (up to 5 ms
    later) and tuning (up to ±5 cents).
  - **Tune:** Standard, Drop D, Drop C, Open G or DADGAD.
  - **Tremolo** and **Rate:** a stereo tremolo; the menu beside them locks it to Ableton's tempo
    (1/2 to 1/16, triplets).
- When a guitar loads, REVERB / CHORUS and PLAY fold so the panel fits; the window never grows
  past the screen.
- **The neck** shows the last chord or note: its name in the display (C, Am7, G/B), where its
  notes lie (high E on top, as in tablature; × a string not played, ○ an open one), and lights for
  the strum's direction and palm-muted notes.
- **Drawing a chord:** click a string at a fret to put a finger there (again to lift it); left of
  the nut, a click switches the string between open and not played. The name appears as you draw;
  click it to hear the chord. Playing shows what's played instead. The drawing is saved with the
  project.
- **Help:** the **?** button at the top opens a guide (Start, Guitar, Controls) and About; the
  header explains whatever control the mouse is over (switch it off in the same menu).
- For acoustic guitar, try the reverb preset **Short Room** and the chorus preset **Bright**.

## MIDI from clips

- **Program Change** switches the instrument on that MIDI channel at once, so a note sent with
  it already plays the new one, live and in exports. Send a **Bank Select** (CC0) before it for
  the variation banks (`[1:025]` is bank 1, program 25). The first time a clip switches to an
  instrument that isn't loaded yet, its samples are read from disk right then, which can cause a
  short dropout live (never in an export); after that it stays in memory, so switching back is
  instant.
- **Controllers** work as the SoundFont defines them: volume (CC7), pan (CC10), expression
  (CC11), sustain (CC64), mod wheel (CC1), reverb and chorus sends (CC91, CC93), pitch bend
  (range set by **Bend**, or by RPN 0 from the clip).
- **Reset All Controllers** (CC121) from a clip resets the clip's controllers; the plugin's own
  knobs (Attack, Release, Fine, Glide, the layer's level) stay as you set them.
- While an instrument is loading the plugin is silent for a moment, but it keeps what the clip
  sent meanwhile (note-offs, pedals, the last position of every controller and bend), so no
  note gets stuck.

## Saving

The Ableton project keeps the file, both instruments and every knob. Changing the file or an
instrument marks the project changed, so Ableton asks to save on closing. Favorites and the
recent files list are kept by the plugin, for every project.

## Limits

- Sample rates from 8 to 192 kHz.
- Up to 256 voices at once (orchestral instruments use 4-6 voices per note).
- `.sf3` files are lossily compressed: they sound very close to their `.sf2`, not identical.
- Instruments that clips switched to with Program Change stay in memory until the SoundFont is
  reloaded.
- The window can be enlarged from its bottom-right corner.
