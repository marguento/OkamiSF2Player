# Okami SF2 Player

An AU / VST3 instrument (plus a standalone app) that plays any SoundFont (.sf2) through
FluidSynth, the reference SF2 player. At its defaults it plays exactly what FluidSynth plays
(the test suite checks it bit for bit); every control on top starts neutral. Native arm64,
JUCE 8.0.15, FluidSynth 2.6.2 (patched, see below). Version and changes: CHANGELOG.md.

## Build

```sh
brew install cmake ninja

# FluidSynth, once: a static library without audio drivers, in float precision, with
# fluidsynth-fx-bypass.patch applied (send outputs, a tabulated sinc kernel, 192 kHz).
cd ThirdParty/fluidsynth && git apply ../../fluidsynth-fx-bypass.patch && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -Denable-floats=ON \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_INSTALL_PREFIX=../fluidsynth-install -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DBUILD_SHARED_LIBS=OFF -Denable-framework=OFF -Denable-coreaudio=OFF -Denable-coremidi=OFF \
  -Denable-aufile=OFF -Denable-dbus=OFF -Denable-ipv6=OFF -Denable-jack=OFF -Denable-ladspa=OFF \
  -Denable-libsndfile=OFF -Denable-midishare=OFF -Denable-network=OFF -Denable-oss=OFF \
  -Denable-sdl3=OFF -Denable-pulseaudio=OFF -Denable-pipewire=OFF -Denable-readline=OFF \
  -Denable-openmp=OFF -Denable-native-dls=OFF -Denable-signalsmith=OFF -Denable-alsa=OFF \
  -Denable-systemd=OFF -Denable-portaudio=OFF && cmake --build build && cmake --install build && cd ../..

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The build copies the plugins to `~/Library/Audio/Plug-Ins/Components` (AU) and
`~/Library/Audio/Plug-Ins/VST3`. Check the AU with `auval -v aumu Sf2p Mrgt`.

## Tests

`build/PluginTests_artefacts/Release/PluginTests` (what `ctest` runs) renders through the real
processor and fails on any wrong result: bit-identical output to FluidSynth at the defaults,
each PLAY control (by level and by measured pitch), layer and split, tone, send outputs, 44.1 /
96 / 192 kHz, the saved state, hostile .sf2 files, and CPU (64 string notes under 30 % of a
128-sample block). With `~/Downloads/HQ_Orchestral_Soundfont_Collection_v3.0.sf2` present it
also checks that collection.

`TailTest` has tools for looking into a sound: `<file> <preset> wav <out.wav> <key> [tone Hz]`
renders one note, `<file> <preset> perf` prints CPU per block (`INTERP=4..7` picks the
interpolation), `<file> sends` lists every preset's sends, `<file> <preset> snap <out.png>`
saves a picture of the editor (`LAYER=2` for split) without touching the screen.

## Use

- **File:** the menu keeps the last 12 SoundFonts and shows how many instruments the open one
  holds; dropping an .sf2 on the plugin loads it. A file that moved is named in red.
- **Instruments:** a General MIDI collection (64+ presets in bank 0) is listed by family
  (Piano, Strings, Brass, Pipe (Flutes)...): bank 0 by program number, variation banks by their
  names. Each shows its number dimmer: `[040]`, or `[1:025]` for bank 1. The search matches
  names and families. ★ marks a favorite (kept for every project, listed first); ▶ plays a
  note; `<` `>` step through the list. Every MIDI channel plays the chosen instrument;
  program change and bank select on a channel switch that channel.
- **Only the chosen instruments' samples are in memory** (dynamic sample loading): a 500 MB
  collection costs a track tens of MB. Choosing another reads its samples from disk; the
  plugin stays silent for those blocks rather than make the audio thread wait.
- **Gain** after the synth. **Tone** is a 24 dB/octave high cut, off at the right end, for the
  hiss some recordings carry (the HQ violin's bow noise sits at 2-6 kHz: try 5 kHz).
- **Layer:** Off, Layer (a second instrument plays every note) or Split (it plays the keys below
  the split key, tinted on the keyboard), with its own level. It runs on FluidSynth channels
  16-31 and follows the controllers, bends and pressure.
- **Reverb and chorus:** FluidSynth's own, shared by every note, set by four knobs or a preset.
  Each panel says how much the chosen instrument sends ("sends 10 %"); many send nothing
  (the HQ collection's strings): raise CC91 / CC93 in the clip, up to +20 %.
- **Effects to separate outputs:** the sends leave dry, in each instrument's own proportions, on
  the "Reverb Send" and "Chorus Send" outputs, and the main output is dry. In Ableton: an audio
  track with Audio From the plugin's track, then "Reverb Send", Monitor In, a reverb at 100 %
  wet. Several tracks can feed it and share one reverb.
- **PLAY:** Attack and Release multiply the envelope's times (x1 = the SoundFont's), Velocity
  bends the response (Linear = as played), Transpose (semitones) and Fine (cents), Bend (the
  pitch wheel's range, ±2 by default), Mode (Poly; Mono: one note per channel; Legato: one
  note, connected notes don't restart) and Glide (ms). They are SF2 generator offsets, the
  same as NRPN, rounded so the defaults add exactly nothing.
- The mod wheel adds vibrato through the SoundFont's default modulator.
- The keyboard lights the notes that arrive, plays on click (lower on a key = louder) and dims
  the keys the instrument doesn't cover. A red line warns about notes at velocity 1-15.
- The window can be resized.

The project keeps the file, both instruments (by bank:program) and every knob. Choosing a file
or an instrument marks the project changed (VST3), so Ableton asks to save.

## Not yet

- SF3 (compressed) files: FluidSynth reads them through libsndfile + libvorbis, not built yet.

## License

FluidSynth is LGPL-2.1, linked statically, with the changes in fluidsynth-fx-bypass.patch.
Fine for personal use; distributing the plugin would need FluidSynth's source and a way to
relink against another build.
