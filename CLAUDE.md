# CLAUDE.md — Okami SF2 Player

A JUCE AU/VST3 instrument that plays SoundFonts through FluidSynth, for Ableton Live on the
user's M4 Mac. README.md has the build, structure, architecture, settings, tests and release;
DOCS.md the user guide; CHANGELOG.md the versions; CREDITS.md the credits and licenses.

Repo https://github.com/marguento/OkamiSF2Player (`main`, created Nov 10, 2026), private until
the user approves publishing it (the PLAYBOOK's "every repo private" rule gets an exception
here, at the user's request). History rewritten on Nov 10 before publishing: the old test piano
(unclear license) and a comment naming another SoundFont removed, every commit under the
GitHub noreply email (`git config user.email` is set to it in this repo).

## The rule that matters most

**The sound must stay as faithful to the original as possible** (the user's words). So:
- At their defaults, every control adds nothing. `PluginTests` compares the plugin's output
  with bare FluidSynth bit for bit (`difference -200 dB`); any new control keeps that check
  green. A knob default like x1 can be stored as 0.99999994: round what reaches FluidSynth
  (it once moved every release by a fraction).
- Prefer FluidSynth's own mechanisms (generator offsets, its modulators, its effects) over
  processing of our own. Our only DSP is Tone (off by default) and gain.
- When FluidSynth itself behaves a certain way, the plugin does too: check a surprise against
  bare FluidSynth before "fixing" it (CC120 and a note in the same instant keep the old voices
  in FluidSynth too).
- Interpolation is FluidSynth's 17-point sinc (`FLUID_INTERP_HIGH`), measured closer to ideal
  than the 4th-order default. FluidSynth 2.6's `FLUID_INTERP_7THORDER` is now its 25-point
  sinc (`HIGHEST`): too slow for real time, don't use it.
- A change to FluidSynth must leave it bit-identical where it didn't need to change (the
  chorus's bigger delay line is used only above 96 kHz for that reason; check with
  `EFFECTS=1 TailTest ... wav` before and after, `cmp`).

## Work loop

- Everything written in the repo is in English by default (docs, comments, commits, UI);
  another language only when the user asks for it (PLAYBOOK gotcha 15b).
- Build: `cmake --build build` (also installs the AU and VST3). Then
  `./build/PluginTests_artefacts/Release/PluginTests` (or `ctest --test-dir build`) and
  `auval -v aumu Sf2p Mrgt`. A change isn't done until both pass. If auval reports an older
  version, macOS cached it: `killall -9 AudioComponentRegistrar`.
- Commit working changes on your own (the user's standing permission) and push; never commit
  `build/`, `dist/`, `ThirdParty/` or `scripts/release.env` (ignored).
- A user-visible change bumps `VERSION` in CMakeLists.txt and adds a CHANGELOG.md line.
- Review loop (PLAYBOOK 19c): `/code-review high <range>` after each batch, fix, review the
  fixes again, until a pass comes back clean. Five passes on Nov 10-11, 2026, each finding
  real issues in the previous fixes (mostly threading); keep going until one is clean.
- Measure, don't guess (PLAYBOOK gotcha 20d): `TailTest <file> <preset> perf` prints CPU per
  block on the Release build; `PluginTests` fails above 30 % for 64 string notes.
- Look at the UI without touching the user's screen: `TailTest <file> <preset> snap out.png`.
  If the real standalone or Xcode must be driven (cliclick), say so first, and stop if focus or
  a popup gets stuck instead of retrying keystrokes.
- Never quit Ableton or the user's apps. After an install the user restarts Ableton.

## Threads (read before touching the processor)

- Audio thread: `processBlock` only try-locks `engineLock`; a failed try keeps the block's MIDI
  in `deferredEvents` (fixed array, no allocation). Nothing on it may allocate, wait or read
  the disk, except a MIDI program change to a preset not yet in memory (FluidSynth's dynamic
  loading; kept on purpose for timing, then pinned so it happens once per preset).
- `stateMutex` (recursive) guards the file, the preset list and choices and the sample rate
  between the message thread and `prepareToPlay`; take it before `engineLock`, never on the
  audio thread.
- `runMessageThreadTasks` (10 Hz timer; tests call it directly) pins presets, turns
  `engineLost` into the missing state, reloads a returning file. `enterMissingState` is the only
  way into the missing state.

## FluidSynth

- Built from `ThirdParty/fluidsynth` (2.6.2) with `fluidsynth-fx-bypass.patch`: raw effect
  sends (`fluid_synth_set_fx_bypass`), the sinc kernel tabulated and built at synth creation,
  sample rates up to 192 kHz with the chorus line doubled above 96 kHz, SF3 through
  libvorbisfile (`SF2PLAYER_VORBISFILE`, checked against ffmpeg). After changing FluidSynth:
  rebuild, `cmake --install build`, regenerate the patch with
  `git diff > ../../fluidsynth-fx-bypass.patch`, rebuild the plugin.
- Building downloaded code needs the user's explicit OK (the permission classifier blocks it
  otherwise). FluidSynth, libogg and libvorbis got it (Nov 10); exact versions and commands in
  README.md.
- Channels 0-15 play the instrument, 16-31 the layer (`layerChannelOffset`).

## Releasing

`scripts/release.sh` (README → Releasing). The notarization key's identifiers live in
`scripts/release.env`, ignored by git: never put them in a tracked file (the repo is meant to
go public).

Developer ID (Nov 11, 2026): only the Account Holder can create the "Developer ID
Application" and "Developer ID Installer" certificates. Tried both ways and Apple refused:
`POST /v1/certificates` with the App Store Connect key ("can only be performed by the Account
Holder"; the API doesn't even offer DEVELOPER_ID_INSTALLER) and Xcode signed in as
the user's Apple ID, whose role on the "Israel Villalobos" team is Admin. The user
has to sign in to Xcode with the Account Holder's Apple ID and create them (Settings → Apple
Accounts → the team → Manage Certificates → +), or have that role transferred.

## Plugin identity

Manufacturer `Mrgt`, plugin `Sf2p`, state tag `SF2Player`, settings folder
`~/Library/Application Support/SF2 Player/` (recent files, favorites). Never change these:
projects and favorites are found by them. The display name can change (it did: "SF2 Player"
became "Okami SF2 Player" in 1.1.0 and old projects kept loading).
