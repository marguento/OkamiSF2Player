# Credits

Okami SF2 Player is built on the work of others. Thank you.

| Project | Authors | License | Used for |
|---|---|---|---|
| [FluidSynth](https://www.fluidsynth.org) 2.6.2 | Peter Hanappe, Tom Moebert, Jean-Jacques Ceresa and the FluidSynth contributors | LGPL-2.1-or-later | the synth engine: SoundFont playback, modulators, reverb and chorus. Changed in `fluidsynth-fx-bypass.patch` (raw effect sends, a tabulated sinc kernel, 192 kHz, SF3 through libvorbisfile) |
| [JUCE](https://juce.com) 8.0.15 | Raw Material Software | AGPLv3 / JUCE licence | the plugin framework: AU, VST3, standalone, the window and its controls |
| [libvorbis](https://xiph.org/vorbis/) 1.3.7 and [libogg](https://xiph.org/ogg/) 1.3.5 | Xiph.Org Foundation | BSD-3-Clause | decoding the compressed samples of .sf3 files |
| [tonal](https://github.com/tonaljs/tonal) | danigb and contributors | MIT | the method the Guitar panel uses to name chords (each note tried as the root against a dictionary of chord types, the bass preferred, a missing fifth allowed); written anew in C++, no code copied |
| [VST 3 SDK](https://github.com/steinbergmedia/vst3sdk) (inside JUCE) | Steinberg Media Technologies | MIT | the VST3 format |
| [AudioUnitSDK](https://github.com/apple/AudioUnitSDK) (inside JUCE) | Apple | Apache-2.0 | the AU format |
| Vintage Dreams Waves v2 (`Tests/fonts`) | Ian Wilson | freeware, redistributed unaltered with its notice | the test suite (the .sf3 is FluidSynth's conversion, made with his permission) |

## Edirol HQ Orchestral

The reason this plugin exists: **Edirol HQ Orchestral**, the orchestral sound module plugin by
Roland Corporation (sold under its Edirol brand in the 2000s, now discontinued). Its strings,
brass and woodwinds shaped a lot of music, ours included. Okami SF2 Player isn't affiliated with
or endorsed by Roland, contains none of its software or samples, and doesn't run the original
plugin: it plays SoundFonts. Edirol and Roland are trademarks of Roland Corporation.

## The HQ Orchestral SoundFont Collection

Okami SF2 Player was made to play the
[HQ Orchestral SoundFont Collection](https://musical-artifacts.com/artifacts/817) (v3.0,
195 instruments) faithfully, and its tests check it when it's present. The collection is not
part of this project and isn't redistributed here: its author is unknown, and the person who
shared it on musical-artifacts.com says it isn't theirs. Download it from that page.

## License of this project

Okami SF2 Player's own code is licensed under the GNU Affero General Public License v3 (see
`LICENSE`). Built from source, it links FluidSynth statically; anyone can rebuild it against
another FluidSynth with the steps in README.md, as the LGPL requires.
