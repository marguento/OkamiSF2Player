# CLAUDE.md — Okami SF2 Player

A JUCE AU/VST3 instrument that plays SoundFonts through FluidSynth, for Ableton Live on the
user's M4 Mac. README.md has the build, structure, architecture and settings; DOCS.md the user guide; CHANGELOG.md the versions.
Private repo https://github.com/marguento/OkamiSF2Player (`main`, created Nov 10, 2026). It stays private (PLAYBOOK security checklist 4).

## The rule that matters most

**The sound must stay as faithful to the original as possible** (the user's words). So:
- At their defaults, every control adds nothing. `PluginTests` compares the plugin's output
  with bare FluidSynth bit for bit (`difference -200 dB`); any new control keeps that check
  green. A knob default like x1 can be stored as 0.99999994: round what reaches FluidSynth
  (it once moved every release by a fraction).
- Prefer FluidSynth's own mechanisms (generator offsets, its modulators, its effects) over
  processing of our own. Our only DSP is Tone (off by default) and gain.
- Interpolation is FluidSynth's 17-point sinc (`FLUID_INTERP_HIGH`), measured closer to ideal
  than the 4th-order default. FluidSynth 2.6's `FLUID_INTERP_7THORDER` is now its 25-point
  sinc (`HIGHEST`): too slow for real time, don't use it.

## Work loop

- Everything written in the repo is in English by default (docs, comments, commits, UI);
  another language only when the user asks for it (PLAYBOOK gotcha 15b).

- Build: `cmake --build build` (also installs the AU and VST3). Then
  `./build/PluginTests_artefacts/Release/PluginTests` (or `ctest --test-dir build`) and
  `auval -v aumu Sf2p Mrgt`. A change isn't done until both pass.
- Commit working changes on your own (the user's standing permission); never commit
  `build/` or `ThirdParty/` (ignored).
- A user-visible change bumps `VERSION` in CMakeLists.txt and adds a CHANGELOG.md line.
- Measure, don't guess (PLAYBOOK gotcha 20d): `TailTest <file> <preset> perf` prints CPU per
  block on the Release build; `PluginTests` fails above 30 % for 64 string notes.
- Look at the UI without touching the user's screen: `TailTest <file> <preset> snap out.png`.
  If the real standalone must be driven (cliclick), say so first, and stop if focus or a popup
  gets stuck instead of retrying keystrokes.
- Never quit Ableton or the user's apps. After an install the user restarts Ableton.

## FluidSynth

- Built from `ThirdParty/fluidsynth` (2.6.2) with `fluidsynth-fx-bypass.patch`: raw effect
  sends for the separate outputs (`fluid_synth_set_fx_bypass`), the sinc kernel tabulated
  (10x faster, within -63 dB of its 25-point reference), sample rates up to 192 kHz. After
  changing FluidSynth: rebuild, `cmake --install build`, regenerate the patch with
  `git diff > ../../fluidsynth-fx-bypass.patch`, rebuild the plugin.
- Building downloaded code needs the user's explicit OK (the permission classifier blocks
  it otherwise). FluidSynth, libogg and libvorbis got it (Nov 10); the build commands are in
  README.md. SF3 goes through libvorbisfile (`SF2PLAYER_VORBISFILE`), checked against ffmpeg.
- One engine per instance, swapped under `engineLock`; the audio thread only try-locks and
  stays silent instead of waiting. FluidSynth calls happen under that lock only.
- Channels 0-15 play the instrument, 16-31 the layer (`layerChannelOffset`).

## Plugin identity

Manufacturer `Mrgt`, plugin `Sf2p`, state tag `SF2Player`, settings folder
`~/Library/Application Support/SF2 Player/` (recent files, favorites). Never change these:
projects and favorites are found by them. The display name can change (it did: "SF2 Player"
became "Okami SF2 Player" in 1.1.0 and old projects kept loading).
