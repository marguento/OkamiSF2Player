# Okami SF2 Player — User Guide

An instrument for Ableton Live that plays SoundFonts (`.sf2` and `.sf3`). With everything at
its default it sounds exactly like the original SoundFont: every control starts at a position
that changes nothing.

## Installing

1. Open the `Okami-SF2-Player-<version>.pkg` installer. It puts the plugin in
   `/Library/Audio/Plug-Ins` as AU and VST3 (Apple silicon and Intel, macOS 13 or later).
2. Get a SoundFont: the plugin was made for the
   [HQ Orchestral SoundFont Collection](https://musical-artifacts.com/artifacts/817), a free
   download of 522 MB with 195 instruments.
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
- **A moved or renamed file:** the project still remembers it, and the plugin names the one it
  can't find in red. Choose it again and everything comes back.
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

## Saving

The Ableton project keeps the file, both instruments and every knob. Changing the file or an
instrument marks the project changed, so Ableton asks to save on closing. Favorites and the
recent files list are kept by the plugin, for every project.

## Limits

- Sample rates from 8 to 192 kHz.
- Up to 256 voices at once (orchestral instruments use 4-6 voices per note).
- `.sf3` files are lossily compressed: they sound very close to their `.sf2`, not identical.
- The window can be enlarged from its bottom-right corner.
