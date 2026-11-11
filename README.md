# Okami SF2 Player

An AU / VST3 instrument (plus a standalone app) that plays SoundFonts (.sf2 and .sf3) through
FluidSynth, the reference SoundFont player, made for Ableton Live on Apple silicon. At its
defaults it plays exactly what FluidSynth plays (the test suite checks it bit for bit); every
control on top starts neutral. How to use it: DOCS.md. Versions: CHANGELOG.md. Credits and licenses: CREDITS.md.

## Tech stack

| Part | What | Notes |
|---|---|---|
| Plugin framework | JUCE 8.0.15 | AU, VST3, Standalone; CMake + Ninja, no Xcode project |
| Synth engine | FluidSynth 2.6.2, static, float | patched: `fluidsynth-fx-bypass.patch` |
| SF3 decoding | libvorbisfile / libvorbis 1.3.7, libogg 1.3.5 | static, from xiph.org |
| Target | macOS 13+, arm64 | built and used on an M4 with macOS 27 |
| Identity | manufacturer `Mrgt`, plugin `Sf2p`, AU `aumu` | never change: projects find the plugin by them |

## Getting started

```sh
brew install cmake ninja
# ThirdParty/ holds JUCE, fluidsynth, ogg and vorbis checkouts (not in git; clone them there).

# libogg and libvorbis, once (static, into ThirdParty/xiph-install):
cd ThirdParty && I=$PWD/xiph-install
for lib in ogg vorbis; do (cd $lib && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DBUILD_SHARED_LIBS=OFF -DCMAKE_PREFIX_PATH=$I -DCMAKE_INSTALL_PREFIX=$I -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  && cmake --build build && cmake --install build); done

# FluidSynth, once: no audio drivers, float, the patch applied, SF3 through libvorbisfile:
cd fluidsynth && git apply ../../fluidsynth-fx-bypass.patch && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -Denable-floats=ON -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_INSTALL_PREFIX=../fluidsynth-install -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_C_FLAGS="-DSF2PLAYER_VORBISFILE=1 -I$I/include" -DCMAKE_CXX_FLAGS="-DSF2PLAYER_VORBISFILE=1 -I$I/include" \
  -DCMAKE_EXE_LINKER_FLAGS="$I/lib/libvorbisfile.a $I/lib/libvorbis.a $I/lib/libogg.a" \
  -Denable-framework=OFF -Denable-coreaudio=OFF -Denable-coremidi=OFF -Denable-aufile=OFF -Denable-dbus=OFF \
  -Denable-ipv6=OFF -Denable-jack=OFF -Denable-ladspa=OFF -Denable-libsndfile=OFF -Denable-midishare=OFF \
  -Denable-network=OFF -Denable-oss=OFF -Denable-sdl3=OFF -Denable-pulseaudio=OFF -Denable-pipewire=OFF \
  -Denable-readline=OFF -Denable-openmp=OFF -Denable-native-dls=OFF -Denable-signalsmith=OFF -Denable-alsa=OFF \
  -Denable-systemd=OFF -Denable-portaudio=OFF && cmake --build build && cmake --install build && cd ../..

# The plugin (also installs the AU and VST3 into ~/Library/Audio/Plug-Ins):
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
auval -v aumu Sf2p Mrgt
```

## Project structure

| File | What it holds |
|---|---|
| `CMakeLists.txt` | the plugin target, three console test targets, ctest, the version |
| `Source/PluginProcessor.h/.cpp` | parameters, the FluidSynth engine (created, swapped under a lock), MIDI routing (transpose, velocity curve, layer/split), rendering into six channels, tone filter, gain, outputs, saved state |
| `Source/PluginEditor.h/.cpp` | the look (`SF2Look`), the keyboard with ranges and the split tint, every control, instrument lists by family with favorites, effect presets, preview |
| `Source/SoundFontInfo.h` | reads what FluidSynth doesn't expose from the file itself: name, author, comment, each preset's key range and reverb/chorus sends (bounded against hostile files) |
| `Tests/PluginTests.cpp` | the test suite (what `ctest` runs) |
| `Tests/TailTest.cpp` | tools: render a note to WAV, CPU per block, list sends, snapshot the editor |
| `Tests/RenderTest.cpp` | plays a note offline and prints the presets with key ranges |
| `Tests/fonts/` | Vintage Dreams Waves .sf2 and .sf3 (Ian Wilson, freeware, unaltered, with its notice) for the tests |
| `fluidsynth-fx-bypass.patch` | every change to FluidSynth (see below) |
| `DOCS.md`, `CHANGELOG.md`, `CLAUDE.md` | user guide, versions, notes for Claude sessions |

## Architecture

- **One FluidSynth per plugin instance** (`Engine`: settings, synth, font id). Loading a file or
  changing the sample rate builds a new one off the audio thread and swaps it in under a
  `SpinLock`; the old one is freed outside the lock. The audio thread only *try*-locks: while
  the message thread holds it (reading samples from disk), that block stays silent.
- **Every FluidSynth call happens under that lock.** Settings reach FluidSynth from the audio
  thread: `processBlock` compares the parameters with what it last applied
  (`EffectSettings`, `PlaySettings`) and sends only changes.
- **32 FluidSynth channels:** MIDI channels 1-16 play the instrument on 0-15, and the layer on
  16-31. Note-offs go to both sides and use the note actually played (`playedNote`), so a
  transpose or a layer change mid-note never leaves a note stuck.
- **Six rendered channels per block** (`work`): main L/R, reverb L/R, chorus L/R, from
  `fluid_synth_process`. Built-in effects: the effect channels are added to main. Send
  outputs: FluidSynth hands out the raw sends (patch) and they go to buses 2 and 3.
- **State ownership:** parameters live in the `AudioProcessorValueTreeState`; the file, the
  instrument and the layer instrument are plain members saved with it; recent files and
  favorites live in the editor's settings file (shared by every instance and project).

## Key systems

- **Fidelity.** Extra controls are FluidSynth's own mechanisms: SF2 generator offsets
  (`fluid_synth_set_gen`, the same as NRPN: attack, release, fine tune, layer attenuation),
  its legato/portamento/basic-channel modes, pitch-wheel sensitivity. Offsets are rounded so a
  default adds exactly 0 (a knob's x1 was stored as 0.99999994 and moved the release once).
- **Interpolation.** `FLUID_INTERP_HIGH`, FluidSynth's 17-point sinc, within -63 to -85 dB of
  its 25-point one, closer to ideal than the 4th-order default (-43 dB). FluidSynth 2.6
  computes its sinc's sines per sample; the patch tabulates the kernel (1024 phases, linear
  between), about 10x faster: 64 string notes take about 10 % of a 128-sample block.
- **The FluidSynth patch** (`fluidsynth-fx-bypass.patch`): `fluid_synth_set_fx_bypass` (raw
  sends for the separate outputs), the tabulated sinc, a sample-rate cap of 192 kHz instead of
  96 (it played an octave low above), and SF3 decoding through libvorbisfile without
  libsndfile (checked against ffmpeg's decoder: within one 16-bit step, every sample exactly
  as long as in the .sf2 original).
- **Memory.** Dynamic sample loading: only the chosen instruments' samples are in RAM (a
  522 MB collection costs a track tens of MB).
- **Hostile files.** FluidSynth validates a file before our reader runs; our reader still
  clamps every index to its chunk and bounds its total work. Tested with garbage, truncated
  and crafted files.
- **Host integration.** Choosing a file or an instrument calls
  `updateHostDisplay (withNonParameterStateChanged)`, so Ableton asks to save. The instrument
  is saved by bank:program, a missing file's path is kept and shown.

## Settings reference

Parameters (automatable, saved in the project; IDs with their version):

| ID | Range | Default | Notes |
|---|---|---|---|
| `gain` | -24 to +12 dB | 0 | after the synth |
| `tone` | 1 kHz to 20 kHz | 20 kHz = Off | 4-pole Butterworth low-pass |
| `fsReverbOn` | bool | on | |
| `fsReverbLevel`, `fsReverbRoom`, `fsReverbDamping`, `fsReverbWidth` | 0-1 | 0.7, 0.5, 0.2, 0.8 | FluidSynth's defaults |
| `fsChorusOn` | bool | on | |
| `fsChorusLevel` | 0-2 | 0.6 | |
| `fsChorusRate` | 0.1-5 Hz | 0.2 | |
| `fsChorusDepth` | 0-20 ms | 4.25 | |
| `fsChorusVoices` | 1-10 | 3 | |
| `sendOutputs` | bool | off | raw sends on the Reverb / Chorus Send outputs |
| `attack`, `release` | x0.25 to x4 | x1 | envelope time multipliers (timecent offsets) |
| `velocityCurve` | -100 to +100 | 0 = Linear | exponent 2^(-v/50) |
| `transpose` | -24 to +24 st | 0 | on the note number |
| `fineTune` | -100 to +100 ct | 0 | |
| `bendRange` | 1-24 st | 2 | |
| `playMode` | Poly / Mono / Legato | Poly | |
| `glide` | 0-2000 ms | 0 = Off | portamento, linear time mode |
| `layerMode` | Off / Layer / Split | Off | |
| `splitKey` | 1-127 | 60 (C4) | keys below it play the layer in Split |
| `layerLevel` | -30 to 0 dB | 0 | SF2 attenuation on channels 16-31 |

Saved alongside the parameters: `sf2Path`, `preset`, `presetBank`, `presetNumber`,
`layerBank`, `layerNumber` (state tag `SF2Player`).

Settings file `~/Library/Application Support/SF2 Player/SF2 Player.settings`: `recentFiles`
(last 12) and `favorites` (`<file name>|<bank>:<program>`, one per line).

## Tests

`ctest` runs `PluginTests`: bit-identical output to FluidSynth at the defaults; each PLAY
control measured (level, pitch by autocorrelation); layer and split; tone; send outputs; SF3;
44.1 / 96 / 192 kHz in tune; saved state; hostile files; CPU under 30 % of a block for 64
string notes. With `~/Downloads/HQ_Orchestral_Soundfont_Collection_v3.0.sf2` present it also
checks that collection (195 instruments, 125 with reverb sends, 47 with chorus).

`TailTest` tools: `<file> <preset> wav <out.wav> <key> [tone Hz]`, `<file> <preset> perf`
(`INTERP=4..7`), `<file> sends`, `<file> <preset> snap <out.png> [sends]` (`LAYER=2` for split).

## License

Okami SF2 Player's own code is AGPLv3 (`LICENSE`). FluidSynth is LGPL-2.1, linked statically
with the changes in fluidsynth-fx-bypass.patch; the build steps above rebuild it from source,
so it can be replaced as the LGPL requires. libogg and libvorbis are BSD. Every project used,
and the HQ collection's status, are in CREDITS.md.
