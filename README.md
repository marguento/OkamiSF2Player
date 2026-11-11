# Okami SF2 Player

An AU / VST3 instrument (plus a standalone app) that plays any SoundFont (.sf2)
with FluidSynth, the reference SF2 player: the SoundFont's modulators, 7th-order
interpolation, its reverb (Dattorro) and its 3-voice chorus, each SoundFont's own
reverb and chorus sends and CC91 / CC93. Native arm64, JUCE 8.0.15, FluidSynth 2.6.2.

## Build

```sh
brew install cmake ninja

# FluidSynth, once, as a static library without audio drivers (ThirdParty/fluidsynth-install),
# in float precision, with fluidsynth-fx-bypass.patch applied (the send outputs below, and the
# sinc interpolation's coefficients tabulated, about 10x faster):
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
```

The build copies the plugins to `~/Library/Audio/Plug-Ins/Components` (AU) and
`~/Library/Audio/Plug-Ins/VST3`. Check the AU with `auval -v aumu Sf2p Mrgt`.
`build/RenderTest_artefacts/Release/RenderTest <file.sf2>` plays a note offline and
prints each preset's key range and the output level.

No SF3 (compressed) files: FluidSynth needs libsndfile for those.

## Use

- The file menu keeps the last 12 SoundFonts; dropping an .sf2 on the plugin loads it.
- Under it: the font's name, author and how many instruments it holds.
- The search box filters the instruments by name or family ("brass", "flutes"; Return picks
  the first match, Esc clears it). A General MIDI collection (64+ presets in bank 0) is listed
  by family (Piano, Strings, Brass, Pipe (Flutes)...): bank 0 by program number, variation
  banks by their names, shown as bank:program ("1:025"). Other fonts list a heading per bank.
- Only the chosen instrument's samples are in memory (FluidSynth's dynamic sample
  loading): a 500 MB collection costs each track tens of MB, not 500. Choosing another
  instrument reads its samples from disk; the plugin stays silent for those blocks
  rather than making the audio thread wait.
- `<` `>` step through the instruments (bank:program). Every MIDI channel plays the
  chosen one; program change and bank select on a channel switch that channel.
- Reverb and chorus: FluidSynth's own, with its defaults; the switch in each panel
  turns one off. Gain is applied after the synth. Many instruments send nothing to the
  reverb (the HQ collection's strings): raise CC91 in the clip for some.
- Each panel's header says what the chosen instrument sends to that effect ("instrument
  sends 10 %", or "nothing"), read from the SoundFont's own generators; a range when its
  notes or layers differ.
- Effects to separate outputs: the reverb and chorus sends leave dry, in the SoundFont's own
  proportions per instrument, on the plugin's "Reverb Send" and "Chorus Send" outputs; the
  main output is then dry. In Ableton: a new audio track, Audio From the SF2 Player track,
  then "Reverb Send"; Monitor In; a reverb on it at 100 % wet. Several tracks can feed one
  such track, so they share a reverb.
- Tone: a 24 dB/octave high cut (off at the right end), for the hiss some recordings carry
  (the HQ violin's bow noise sits at 2-6 kHz: try 5 kHz).
- The project keeps the file, the instrument (by bank:program) and every knob. Choosing a
  file or an instrument marks the project changed (VST3), so Ableton asks to save. A file
  that moved stays named in the project, and the plugin says it can't find it.
- The keyboard lights the notes that arrive, plays on click (lower on a key = louder)
  and dims the keys the instrument doesn't cover.
- A red line warns when notes arrive at velocity 1-15 (nearly silent).

`build/TailTest_artefacts/Release/TailTest <file.sf2> <preset> check` tests the outputs, the
tone and the saved state; `... <preset> wav <out.wav> <key> [tone Hz]` renders one note; `... sends` lists every
preset's send amounts; `... <preset> snap <out.png> [sends]` saves a picture of the editor.

## License

FluidSynth is LGPL-2.1, linked statically, with one small change (fluidsynth-fx-bypass.patch). Fine for personal use; distributing the
plugin would need FluidSynth's source and a way to relink against another build.
