#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "SoundFontInfo.h"
#include <array>
#include <atomic>
#include <memory>
#include <vector>

// A SoundFont (.sf2) instrument played by FluidSynth, the reference SF2
// player: its modulators, interpolation, reverb and chorus. The file is chosen
// in the editor and remembered in the host's project.
class SF2PlayerProcessor : public juce::AudioProcessor,
                           public juce::ChangeBroadcaster
{
public:
    SF2PlayerProcessor();
    ~SF2PlayerProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Okami SF2 Player"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    struct Preset
    {
        juce::String name;
        int bank = 0, number = 0, lowKey = 0, highKey = 127;
        SoundFontInfo::Sends sends;   // what it sends to the reverb and the chorus, in percent
    };

    // Message thread only.
    bool loadSoundFont (const juce::File& file);
    void selectPreset (int presetIndex);
    // The second instrument, for Layer and Split (playMode-like parameter "layerMode").
    void selectLayerPreset (int presetIndex);
    int selectedLayerPreset() const { return layerPreset; }
    const std::vector<Preset>& presets() const { return presetList; }   // message thread
    juce::File soundFontFile() const { return currentFile; }
    const SoundFontInfo& soundFontInfo() const { return currentInfo; }
    int selectedPreset() const { return currentPreset; }
    // The SoundFont the project names when it isn't on this Mac any more (moved, renamed or deleted).
    juce::File missingSoundFont() const { return missingFile; }

    // Voices sounding after the last block (for the editor and tests).
    std::atomic<int> activeVoices { 0 };
    // Tests: render with another interpolation (FLUID_INTERP_*); -1 keeps the 7th order.
    int interpolationOverride = -1;

    juce::AudioProcessorValueTreeState parameters;

    // For the editor: note-ons received, the latest velocity, the output's peak since last read.
    std::atomic<int> notesReceived { 0 };
    std::atomic<int> lastVelocity { 0 };
    std::atomic<float> outputPeak { 0.0f };

    // Notes from the host light the on-screen keyboard; its clicks play the synth.
    juce::MidiKeyboardState keyboardState;

    struct Engine;   // a FluidSynth instance with the SoundFont loaded

    // FluidSynth runs 32 channels: 0-15 play the instrument, 16-31 the layer (channel + 16).
    static constexpr int layerChannelOffset = 16, numSynthChannels = 32;

private:
    static constexpr int numMidiChannels = 16;

    void handleMidi (const juce::MidiMessage&);
    void render (int start, int end);
    void writeOutputs (juce::AudioBuffer<float>&, int numSamples, bool sendOutputs);
    void markProjectChanged();
    void applyEffectSettings (bool force);
    void applyPlaySettings (bool force);
    void applyPreset (Engine&, int presetIndex);
    void applyLayerPreset (Engine&, int presetIndex);
    int layerPreset = 0;
    int savedLayerBank = -1, savedLayerNumber = -1;
    void swapEngine (std::unique_ptr<Engine>);

    juce::SpinLock engineLock;   // guards `engine` between the audio and message threads
    std::unique_ptr<Engine> engine;

    juce::File currentFile, missingFile;
    int savedBank = -1, savedNumber = -1;   // the project's instrument, found again by bank:program on load
    bool restoringState = false;
    SoundFontInfo currentInfo;
    std::vector<Preset> presetList;
    int currentPreset = 0;
    double sampleRate = 44100.0;
    // What FluidSynth renders: main L/R, reverb L/R, chorus L/R.
    juce::AudioBuffer<float> work;
    float lastGain = 1.0f;

    // Tone: a 4-pole Butterworth low-pass (two biquads) on every channel of `work`.
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void setLowPass (double frequency, double rate, double q);
        void process (float* samples, int count) noexcept;
    };
    std::array<std::array<Biquad, 2>, 6> toneFilters;
    float toneFrequency = 0.0f;   // applied; 0 = not yet

    struct EffectSettings
    {
        bool reverbOn = true, chorusOn = true, sendOutputs = false;
        float reverbLevel = 0, reverbRoom = 0, reverbDamping = 0, reverbWidth = 0;
        float chorusLevel = 0, chorusRate = 0, chorusDepth = 0;
        int chorusVoices = 0;
        bool operator== (const EffectSettings& o) const   // exact on purpose: any change re-applies
        {
            using juce::exactlyEqual;
            return reverbOn == o.reverbOn && chorusOn == o.chorusOn && sendOutputs == o.sendOutputs && chorusVoices == o.chorusVoices
                && exactlyEqual (reverbLevel, o.reverbLevel) && exactlyEqual (reverbRoom, o.reverbRoom)
                && exactlyEqual (reverbDamping, o.reverbDamping) && exactlyEqual (reverbWidth, o.reverbWidth)
                && exactlyEqual (chorusLevel, o.chorusLevel) && exactlyEqual (chorusRate, o.chorusRate)
                && exactlyEqual (chorusDepth, o.chorusDepth);
        }
    };
    EffectSettings appliedEffects;
    EffectSettings currentEffectSettings() const;

    // How the instrument is played. Every default leaves the SoundFont as it is.
    struct PlaySettings
    {
        float attack = 1, release = 1;   // envelope time multipliers
        int fineTune = 0, bendRange = 2, mode = 0, glide = 0;   // cents, semitones, Poly/Mono/Legato, ms
        int layerAttenuation = 0;   // centibels, on the layer's channels
        bool operator== (const PlaySettings& o) const
        {
            return juce::exactlyEqual (attack, o.attack) && juce::exactlyEqual (release, o.release)
                && fineTune == o.fineTune && bendRange == o.bendRange && mode == o.mode && glide == o.glide
                && layerAttenuation == o.layerAttenuation;
        }
    };
    PlaySettings appliedPlay;
    bool playApplied = false;
    PlaySettings currentPlaySettings() const;

    // Notes as played after transposing, so a note-off finds its note even if the transpose moved meanwhile.
    std::array<std::array<juce::int16, 128>, 16> playedNote;   // -1: none
    int transposeFor (int note) const;
    int curvedVelocity (int velocity) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SF2PlayerProcessor)
};
