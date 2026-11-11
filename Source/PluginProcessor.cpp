#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <fluidsynth.h>
#include <algorithm>

// FluidSynth's master gain is 0.2 by default, made for a sound card playing
// 16 channels at once; for one instrument on a track 1.0 gives a usual level
// (one piano note at velocity 100 peaks around -18 dB).
static constexpr double synthGain = 1.0;
static constexpr float toneOff = 20000.0f;

struct SF2PlayerProcessor::Engine
{
    fluid_settings_t* settings = nullptr;
    fluid_synth_t* synth = nullptr;
    int fontId = -1;

    ~Engine()
    {
        if (synth != nullptr)    delete_fluid_synth (synth);
        if (settings != nullptr) delete_fluid_settings (settings);
    }
};

// ---------------------------------------------------------------------------- parameters

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    using Float = juce::AudioParameterFloat;
    using Attributes = juce::AudioParameterFloatAttributes;
    const auto percent = Attributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });
    auto range = [] (float low, float high, float step) { return juce::NormalisableRange<float> (low, high, step); };

    // Defaults are FluidSynth's own (synth/fluid_synth.h), so a fresh instance sounds like FluidSynth.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<Float> (juce::ParameterID { "gain", 1 }, "Gain", range (-24.0f, 12.0f, 0.1f), 0.0f,
                                         Attributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "fsReverbOn", 2 }, "Reverb On", true));
    layout.add (std::make_unique<Float> (juce::ParameterID { "fsReverbLevel", 2 }, "Reverb Level", range (0.0f, 1.0f, 0.01f), 0.7f, percent));
    layout.add (std::make_unique<Float> (juce::ParameterID { "fsReverbRoom", 2 }, "Reverb Room", range (0.0f, 1.0f, 0.01f), 0.5f, percent));
    layout.add (std::make_unique<Float> (juce::ParameterID { "fsReverbDamping", 2 }, "Reverb Damping", range (0.0f, 1.0f, 0.01f), 0.2f, percent));
    layout.add (std::make_unique<Float> (juce::ParameterID { "fsReverbWidth", 2 }, "Reverb Width", range (0.0f, 1.0f, 0.01f), 0.8f, percent));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "fsChorusOn", 2 }, "Chorus On", true));
    layout.add (std::make_unique<Float> (juce::ParameterID { "fsChorusLevel", 2 }, "Chorus Level", range (0.0f, 2.0f, 0.01f), 0.6f, percent));
    layout.add (std::make_unique<Float> (juce::ParameterID { "fsChorusRate", 2 }, "Chorus Rate",
                                         juce::NormalisableRange<float> (0.1f, 5.0f, 0.01f, 0.5f), 0.2f, Attributes().withLabel ("Hz")));
    layout.add (std::make_unique<Float> (juce::ParameterID { "fsChorusDepth", 2 }, "Chorus Depth", range (0.0f, 20.0f, 0.05f), 4.25f,
                                         Attributes().withLabel ("ms")));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "fsChorusVoices", 2 }, "Chorus Voices", 1, 10, 3));
    // Playing. Every default is neutral: the SoundFont plays as its author made it.
    auto times = Attributes().withStringFromValueFunction ([] (float v, int)
    {
        return juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) + juce::String (v, v < 1.0f ? 2 : 1);
    });
    layout.add (std::make_unique<Float> (juce::ParameterID { "attack", 4 }, "Attack",
                                         juce::NormalisableRange<float> (0.25f, 4.0f, 0.01f, 0.5f), 1.0f, times));
    layout.add (std::make_unique<Float> (juce::ParameterID { "release", 4 }, "Release",
                                         juce::NormalisableRange<float> (0.25f, 4.0f, 0.01f, 0.5f), 1.0f, times));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "velocityCurve", 4 }, "Velocity Curve", -100, 100, 0,
                                         juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
                                         { return v == 0 ? juce::String ("Linear") : (v > 0 ? "+" : "") + juce::String (v); })));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "transpose", 4 }, "Transpose", -24, 24, 0,
                                         juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
                                         { return (v > 0 ? "+" : "") + juce::String (v) + " st"; })));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "fineTune", 4 }, "Fine Tune", -100, 100, 0,
                                         juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
                                         { return (v > 0 ? "+" : "") + juce::String (v) + " ct"; })));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "bendRange", 4 }, "Bend Range", 1, 24, 2,
                                         juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
                                         { return juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String (v) + " st"; })));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "playMode", 4 }, "Play Mode",
                                                              juce::StringArray { "Poly", "Mono", "Legato" }, 0));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "glide", 4 }, "Glide", 0, 2000, 0,
                                         juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
                                         { return v == 0 ? juce::String ("Off") : juce::String (v) + " ms"; })));

    // A second instrument: Off, Layer (both play every note) or Split (notes below the split key
    // play the second one). Off by default: only the instrument plays.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "layerMode", 5 }, "Layer Mode",
                                                              juce::StringArray { "Off", "Layer", "Split" }, 0));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "splitKey", 5 }, "Split Key", 1, 127, 60,
                                         juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
                                         { return juce::MidiMessage::getMidiNoteName (v, true, true, 4); })));
    layout.add (std::make_unique<Float> (juce::ParameterID { "layerLevel", 5 }, "Layer Level", range (-30.0f, 0.0f, 0.5f), 0.0f,
                                         Attributes().withLabel ("dB")));

    // Off: the built-in reverb and chorus play in the main output. On: their sends go out
    // unprocessed on the Reverb and Chorus outputs, for Ableton's own effects.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "sendOutputs", 3 }, "Send Outputs", false));
    // A high cut against the hiss some recordings carry (a violin's bow); at the top it's off.
    layout.add (std::make_unique<Float> (juce::ParameterID { "tone", 3 }, "Tone",
                                         juce::NormalisableRange<float> (1000.0f, toneOff, 10.0f, 0.3f), toneOff,
                                         Attributes().withStringFromValueFunction ([] (float v, int)
                                         {
                                             return v >= toneOff - 1.0f ? juce::String ("Off")
                                                                        : juce::String (v / 1000.0f, 1) + " kHz";
                                         })
                                         .withValueFromStringFunction ([] (const juce::String& text)
                                         {
                                             if (text.trim().equalsIgnoreCase ("off")) return toneOff;
                                             const float v = text.getFloatValue();
                                             return text.containsIgnoreCase ("k") || v < 100.0f ? v * 1000.0f : v;
                                         })));
    return layout;
}

SF2PlayerProcessor::SF2PlayerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Reverb Send", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Chorus Send", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "SF2Player", makeLayout())
{
    for (auto& channel : playedNote)
        channel.fill (-1);
    anyCollapsed = true;   // so the first clear fills the collapsed values
    clearDeferredMidi();
    lastPinRequest.fill (-1);
    startTimerHz (10);
}

SF2PlayerProcessor::~SF2PlayerProcessor()
{
    stopTimer();
}

void SF2PlayerProcessor::runMessageThreadTasks()
{
    flushProjectChanged();

    // Presets a clip switched to, pinned while still selected on some channel: their samples
    // are in memory then, so pinning reads nothing (and one no longer selected is skipped,
    // rather than read from disk under the lock). One lock for the whole batch.
    std::array<int, 256> keys {};
    int count = 0;
    pinFifo.read (pinFifo.getNumReady()).forEach ([&] (int index) { keys[(size_t) count++] = pinQueue[(size_t) index]; });
    if (count > 0)
    {
        const juce::SpinLock::ScopedLockType lock (engineLock);
        if (engine != nullptr)
            for (int k = 0; k < count; ++k)
                for (int channel = 0; channel < numSynthChannels; ++channel)
                    if (auto* preset = fluid_synth_get_channel_preset (engine->synth, channel);
                        preset != nullptr && fluid_preset_get_banknum (preset) * 128 + fluid_preset_get_num (preset) == keys[(size_t) k])
                    {
                        fluid_synth_pin_preset (engine->synth, engine->fontId, keys[(size_t) k] / 128, keys[(size_t) k] % 128);
                        break;
                    }
    }

    if (++housekeepingTicks % 10 != 0)   // the file check: once a second
        return;

    // A missing file that's back (a drive plugged in), or an unreadable one that changed.
    const StateWriter writer (*this);
    if (currentFile == juce::File() && missingFile != juce::File() && missingFile.existsAsFile() && shouldRetry (missingFile))
    {
        const auto file = missingFile;
        const juce::ScopedValueSetter<bool> restoring (restoringState, true);
        currentFile = file;   // so loadSoundFont keeps the project's instruments
        if (! loadSoundFont (file))
        {
            currentFile = juce::File();
            recordFailure (file);
            sendChangeMessage();
        }
    }
}

void SF2PlayerProcessor::recordFailure (const juce::File& file)
{
    // The same file failing again waits twice as long before the next try (up to 30 minutes).
    retryWaitMs = file == failedFile ? juce::jmin (retryWaitMs * 2, (juce::uint32) 30 * 60000) : (juce::uint32) 60000;
    failedFile = file;
    failedFileTime = file.getLastModificationTime();
    failedFileSize = file.getSize();
    failedAtMs = juce::Time::getMillisecondCounter();
}

bool SF2PlayerProcessor::shouldRetry (const juce::File& file) const
{
    return file != failedFile || file.getLastModificationTime() != failedFileTime || file.getSize() != failedFileSize
        || juce::Time::getMillisecondCounter() - failedAtMs > retryWaitMs;   // e.g. a cloud placeholder now downloaded
}

void SF2PlayerProcessor::markProjectChanged()
{
    if (! restoringState)
        projectChangePending = true;   // sent by the message thread's timer, outside stateMutex
}

void SF2PlayerProcessor::flushProjectChanged()
{
    // The file and the instrument aren't parameters: without this the host doesn't know the
    // project changed, and closing it without saving loses them (VST3's "dirty" flag).
    if (projectChangePending.exchange (false))
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

// Called by the outermost StateWriter as it ends, with stateMutex held.
void SF2PlayerProcessor::publish()
{
    auto fresh = std::make_shared<State>();
    fresh->file = currentFile;
    fresh->missing = missingFile;
    fresh->presets = presetList;
    fresh->info = currentInfo;
    fresh->preset = currentPreset;
    fresh->layer = layerPreset;
    if (juce::isPositiveAndBelow (currentPreset, (int) presetList.size()))
        fresh->bank = presetList[(size_t) currentPreset].bank, fresh->number = presetList[(size_t) currentPreset].number;
    else
        fresh->bank = savedBank, fresh->number = savedNumber;   // the file is missing: what the project had
    if (juce::isPositiveAndBelow (layerPreset, (int) presetList.size()))
        fresh->layerBank = presetList[(size_t) layerPreset].bank, fresh->layerNumber = presetList[(size_t) layerPreset].number;
    else
        fresh->layerBank = savedLayerBank, fresh->layerNumber = savedLayerNumber;
    std::shared_ptr<const State> old;
    {
        const juce::SpinLock::ScopedLockType lock (publishedLock);
        old = std::exchange (published, std::move (fresh));
    }
    // `old` is freed here, outside the SpinLock.
}

void SF2PlayerProcessor::enterMissingState (const juce::File& file, bool keepInstruments, bool unreadable)
{
    if (keepInstruments)
    {
        if (juce::isPositiveAndBelow (currentPreset, (int) presetList.size()))
            savedBank = presetList[(size_t) currentPreset].bank, savedNumber = presetList[(size_t) currentPreset].number;
        if (juce::isPositiveAndBelow (layerPreset, (int) presetList.size()))
            savedLayerBank = presetList[(size_t) layerPreset].bank, savedLayerNumber = presetList[(size_t) layerPreset].number;
    }
    missingFile = file;
    if (unreadable)
        recordFailure (file);
    else if (file == failedFile)
        failedFile = juce::File();   // missing, not damaged: load it as soon as it's back
    swapEngine (nullptr);
    currentFile = juce::File();
    currentInfo = {};
    presetList.clear();
    sendChangeMessage();
}

SF2PlayerProcessor::EffectSettings SF2PlayerProcessor::currentEffectSettings() const
{
    auto value = [this] (const char* id) { return parameters.getRawParameterValue (id)->load(); };
    EffectSettings s;
    s.reverbOn = value ("fsReverbOn") > 0.5f;
    s.reverbLevel = value ("fsReverbLevel");
    s.reverbRoom = value ("fsReverbRoom");
    s.reverbDamping = value ("fsReverbDamping");
    s.reverbWidth = value ("fsReverbWidth");
    s.chorusOn = value ("fsChorusOn") > 0.5f;
    s.chorusLevel = value ("fsChorusLevel");
    s.chorusRate = value ("fsChorusRate");
    s.chorusDepth = value ("fsChorusDepth");
    s.chorusVoices = (int) value ("fsChorusVoices");
    s.sendOutputs = value ("sendOutputs") > 0.5f;
    return s;
}

// Called with the engine lock held. Only what changed reaches FluidSynth: each chorus setter
// restarts the chorus's modulators, so resending it while another knob moves would click.
void SF2PlayerProcessor::applyEffectSettings (bool force)
{
    const auto s = currentEffectSettings();
    if (! force && s == appliedEffects)
        return;

    const auto& a = appliedEffects;
    using juce::exactlyEqual;
    auto* synth = engine->synth;
    if (force || s.reverbOn != a.reverbOn)                        fluid_synth_reverb_on (synth, -1, s.reverbOn);
    if (force || ! exactlyEqual (s.reverbLevel, a.reverbLevel))     fluid_synth_set_reverb_group_level (synth, -1, s.reverbLevel);
    if (force || ! exactlyEqual (s.reverbRoom, a.reverbRoom))       fluid_synth_set_reverb_group_roomsize (synth, -1, s.reverbRoom);
    if (force || ! exactlyEqual (s.reverbDamping, a.reverbDamping)) fluid_synth_set_reverb_group_damp (synth, -1, s.reverbDamping);
    if (force || ! exactlyEqual (s.reverbWidth, a.reverbWidth))     fluid_synth_set_reverb_group_width (synth, -1, s.reverbWidth);
    if (force || s.chorusOn != a.chorusOn)                        fluid_synth_chorus_on (synth, -1, s.chorusOn);
    if (force || ! exactlyEqual (s.chorusLevel, a.chorusLevel))     fluid_synth_set_chorus_group_level (synth, -1, s.chorusLevel);
    if (force || ! exactlyEqual (s.chorusRate, a.chorusRate))       fluid_synth_set_chorus_group_speed (synth, -1, s.chorusRate);
    if (force || ! exactlyEqual (s.chorusDepth, a.chorusDepth))     fluid_synth_set_chorus_group_depth (synth, -1, s.chorusDepth);
    if (force || s.chorusVoices != a.chorusVoices)                fluid_synth_set_chorus_group_nr (synth, -1, s.chorusVoices);
    if (force || s.sendOutputs != a.sendOutputs)                  fluid_synth_set_fx_bypass (synth, s.sendOutputs);
    appliedEffects = s;
}

// A multiplier on the envelope's times is an offset in timecents: 1200 per doubling. Whole
// timecents: the knob's x1 is stored as 0.99999994, and even that -0.0001 moved the release.
static float timecents (float multiplier)
{
    return (float) juce::roundToInt (1200.0f * std::log2 (multiplier));
}

SF2PlayerProcessor::PlaySettings SF2PlayerProcessor::currentPlaySettings() const
{
    auto value = [this] (const char* id) { return parameters.getRawParameterValue (id)->load(); };
    PlaySettings s;
    s.attack = value ("attack");
    s.release = value ("release");
    s.fineTune = juce::roundToInt (value ("fineTune"));
    s.bendRange = juce::roundToInt (value ("bendRange"));
    s.mode = juce::roundToInt (value ("playMode"));
    s.glide = juce::roundToInt (value ("glide"));
    s.layerAttenuation = juce::roundToInt (-value ("layerLevel") * 10.0f);
    return s;
}

// Called with the engine lock held. The SoundFont's own values stay; these are offsets on
// top (SF2's real-time generator offsets, the same as NRPN), so at their defaults nothing changes.
// Only what changed is sent: the bend range and glide are also MIDI state a clip may set
// (RPN 0, CC65 / CC5), which moving Attack must not overwrite.
void SF2PlayerProcessor::applyPlaySettings (bool force)
{
    const auto s = currentPlaySettings();
    if (! force && playApplied && s == appliedPlay)
        return;
    const bool all = force || ! playApplied;
    const auto& a = appliedPlay;

    auto* synth = engine->synth;
    for (int channel = 0; channel < numSynthChannels; ++channel)
    {
        if (all)
        {
            applyChannelSettings (channel, s);
            fluid_synth_pitch_wheel_sens (synth, channel, s.bendRange);
            fluid_synth_set_portamento_mode (synth, channel, s.mode == 2 ? FLUID_CHANNEL_PORTAMENTO_MODE_LEGATO_ONLY
                                                                         : FLUID_CHANNEL_PORTAMENTO_MODE_EACH_NOTE);
            fluid_synth_set_legato_mode (synth, channel, s.mode == 1 ? FLUID_CHANNEL_LEGATO_MODE_RETRIGGER
                                                                     : FLUID_CHANNEL_LEGATO_MODE_MULTI_RETRIGGER);
            continue;
        }
        if (channel >= layerChannelOffset && s.layerAttenuation != a.layerAttenuation)
            fluid_synth_set_gen (synth, channel, GEN_ATTENUATION, (float) s.layerAttenuation);   // centibels
        if (! juce::exactlyEqual (s.attack, a.attack))
            fluid_synth_set_gen (synth, channel, GEN_VOLENVATTACK, timecents (s.attack));
        if (! juce::exactlyEqual (s.release, a.release))
            fluid_synth_set_gen (synth, channel, GEN_VOLENVRELEASE, timecents (s.release));
        if (s.fineTune != a.fineTune)
            fluid_synth_set_gen (synth, channel, GEN_FINETUNE, (float) s.fineTune);
        if (s.bendRange != a.bendRange)
            fluid_synth_pitch_wheel_sens (synth, channel, s.bendRange);

        if (s.glide != a.glide)
        {
            // Glide: portamento, its time in ms (linear mode: MSB * 128 + LSB).
            fluid_synth_cc (synth, channel, 65, s.glide > 0 ? 127 : 0);
            fluid_synth_cc (synth, channel, 5, s.glide / 128);
            fluid_synth_cc (synth, channel, 37, s.glide % 128);
        }
        if (s.mode != a.mode)
        {
            // FluidSynth's defaults (glide on each note, multi-retrigger legato), except: Legato glides
            // only between connected notes, Mono starts every note afresh.
            fluid_synth_set_portamento_mode (synth, channel, s.mode == 2 ? FLUID_CHANNEL_PORTAMENTO_MODE_LEGATO_ONLY
                                                                         : FLUID_CHANNEL_PORTAMENTO_MODE_EACH_NOTE);
            fluid_synth_set_legato_mode (synth, channel, s.mode == 1 ? FLUID_CHANNEL_LEGATO_MODE_RETRIGGER
                                                                     : FLUID_CHANNEL_LEGATO_MODE_MULTI_RETRIGGER);
        }
    }
    if (all || s.mode != a.mode)
    {
        fluid_synth_all_notes_off (synth, -1);
        fluid_synth_reset_basic_channel (synth, -1);
        // Poly: as a fresh FluidSynth. Mono and Legato: each channel plays one note at a time.
        fluid_synth_set_basic_channel (synth, 0, s.mode == 0 ? FLUID_CHANNEL_MODE_OMNION_POLY : FLUID_CHANNEL_MODE_OMNIOFF_MONO,
                                       numSynthChannels);
    }
    appliedPlay = s;
    playApplied = true;
}

// The per-channel settings FluidSynth keeps as generator offsets and controllers: all of them
// on a fresh engine, again after a clip's Reset All Controllers (CC121) cleared them.
void SF2PlayerProcessor::applyChannelSettings (int channel, const PlaySettings& s)
{
    auto* synth = engine->synth;
    if (channel >= layerChannelOffset)
        fluid_synth_set_gen (synth, channel, GEN_ATTENUATION, (float) s.layerAttenuation);
    fluid_synth_set_gen (synth, channel, GEN_VOLENVATTACK, timecents (s.attack));
    fluid_synth_set_gen (synth, channel, GEN_VOLENVRELEASE, timecents (s.release));
    fluid_synth_set_gen (synth, channel, GEN_FINETUNE, (float) s.fineTune);
    if (s.glide > 0)
    {
        fluid_synth_cc (synth, channel, 65, 127);
        fluid_synth_cc (synth, channel, 5, s.glide / 128);
        fluid_synth_cc (synth, channel, 37, s.glide % 128);
    }
}

// Controllers kept in order past the deferred reserve, never collapsed: bank select, the hold,
// sostenuto and soft pedals, and channel mode messages (120 and up).
static bool keptInOrder (int cc)
{
    return cc == 0 || cc == 32 || cc == 64 || cc == 66 || cc == 67 || cc >= 120;
}

// Controllers FluidSynth's Reset All Controllers leaves alone (fluid_channel_init_ctrl in
// fluid_chan.c): bank select, volume, balance, pan, sound controllers 1-10, effect depths 1-5.
static bool survivesReset (int cc)
{
    return cc == 0 || cc == 32 || cc == 7 || cc == 39 || cc == 8 || cc == 40 || cc == 10 || cc == 42
        || (cc >= 70 && cc <= 79) || (cc >= 91 && cc <= 95);
}

void SF2PlayerProcessor::deferMidi (const juce::MidiBuffer& midi)
{
    for (const auto metadata : midi)
    {
        if (metadata.numBytes > 3)
            continue;   // sysex: building a message from it could allocate, and nothing here plays it
        const juce::MidiMessage m (metadata.data, metadata.numBytes);
        const int channel = m.getChannel() - 1;
        const bool onChannel = juce::isPositiveAndBelow (channel, 16);
        const int cc = m.isController() ? m.getControllerNumber() : -1;

        // Past `deferredNormal`, continuous values keep only their latest per channel.
        const bool rpn = cc == 6 || cc == 38 || (cc >= 96 && cc <= 101);   // parameter number and data entry, kept in order
        if (numDeferred >= deferredNormal && onChannel)
        {
            if (m.isPitchWheel())       { deferredBend[(size_t) channel] = m.getPitchWheelValue(); anyCollapsed = true; continue; }
            if (m.isChannelPressure())  { deferredPressure[(size_t) channel] = m.getChannelPressureValue(); anyCollapsed = true; continue; }
            if (cc >= 0 && ! rpn && ! keptInOrder (cc))
            {
                deferredController[(size_t) channel][(size_t) cc] = (juce::int8) m.getControllerValue();
                anyCollapsed = true;
                continue;
            }
        }
        // What changes lasting state goes in up to the full capacity: note-offs, pedals, bank
        // and program changes, parameter numbers and data, resets.
        const bool lasting = m.isNoteOff() || m.isProgramChange() || rpn || (cc >= 0 && keptInOrder (cc));
        if (numDeferred < (lasting ? deferredCapacity : deferredNormal))
        {
            deferredEvents[(size_t) numDeferred++] = m;
            // A reset makes the values collapsed before it moot, for the controllers FluidSynth
            // resets (not bank, volume, balance, pan, effect depths or sound controllers).
            if (onChannel && m.isResetAllControllers())
            {
                deferredBend[(size_t) channel] = deferredPressure[(size_t) channel] = -1;
                auto& values = deferredController[(size_t) channel];
                for (int c = 0; c < 120; ++c)
                    if (! survivesReset (c))
                        values[(size_t) c] = -1;
            }
        }
    }
}

void SF2PlayerProcessor::playDeferredMidi()
{
    if (numDeferred == 0 && ! anyCollapsed)
        return;   // the usual block: nothing kept
    for (int i = 0; i < numDeferred; ++i)
        handleMidi (deferredEvents[(size_t) i]);
    for (int channel = 0; channel < 16 && anyCollapsed; ++channel)
    {
        for (int cc = 0; cc < 128; ++cc)
            if (const int value = deferredController[(size_t) channel][(size_t) cc]; value >= 0)
                handleMidi (juce::MidiMessage::controllerEvent (channel + 1, cc, value));
        if (deferredPressure[(size_t) channel] >= 0)
            handleMidi (juce::MidiMessage::channelPressureChange (channel + 1, deferredPressure[(size_t) channel]));
        if (deferredBend[(size_t) channel] >= 0)
            handleMidi (juce::MidiMessage::pitchWheel (channel + 1, deferredBend[(size_t) channel]));
    }
    clearDeferredMidi();
}

void SF2PlayerProcessor::clearDeferredMidi()
{
    numDeferred = 0;
    if (! anyCollapsed)
        return;
    anyCollapsed = false;
    deferredBend.fill (-1);
    deferredPressure.fill (-1);
    for (auto& channel : deferredController)
        channel.fill (-1);
}

int SF2PlayerProcessor::transposeFor (int note) const
{
    return note + juce::roundToInt (parameters.getRawParameterValue ("transpose")->load());
}

// Velocity curve: 0 leaves velocities as they come; + lifts soft notes, - lowers them.
int SF2PlayerProcessor::curvedVelocity (int velocity) const
{
    const float amount = parameters.getRawParameterValue ("velocityCurve")->load();
    if (juce::approximatelyEqual (amount, 0.0f))
        return velocity;
    const float exponent = std::pow (2.0f, -amount / 50.0f);   // +100 -> 0.25, -100 -> 4
    return juce::jlimit (1, 127, juce::roundToInt (127.0f * std::pow ((float) velocity / 127.0f, exponent)));
}

// ---------------------------------------------------------------------------- engine

bool SF2PlayerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    for (int bus = 1; bus < layouts.outputBuses.size(); ++bus)   // the sends: stereo, or switched off by the host
        if (const auto set = layouts.getChannelSet (false, bus); ! set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    return layouts.getMainInputChannelSet().isDisabled();
}

static std::unique_ptr<SF2PlayerProcessor::Engine> createEngine (const juce::File& file, double sampleRate)
{
    // FluidSynth prints warnings to stderr (such as GM presets a single-instrument font lacks): not useful in a plugin.
    fluid_set_log_function (FLUID_WARN, nullptr, nullptr);
    fluid_set_log_function (FLUID_INFO, nullptr, nullptr);

    auto engine = std::make_unique<SF2PlayerProcessor::Engine>();
    engine->settings = new_fluid_settings();
    if (engine->settings == nullptr)
        return nullptr;

    fluid_settings_setnum (engine->settings, "synth.sample-rate", juce::jlimit (8000.0, 192000.0, sampleRate));
    fluid_settings_setnum (engine->settings, "synth.gain", synthGain);
    fluid_settings_setint (engine->settings, "synth.polyphony", 256);
    fluid_settings_setint (engine->settings, "synth.threadsafe-api", 0);   // the plugin's own lock serialises calls
    fluid_settings_setint (engine->settings, "synth.midi-channels", SF2PlayerProcessor::numSynthChannels);
    // Only the samples of the instruments in use are read into memory: a 500 MB
    // collection costs each track only its own instrument's samples.
    fluid_settings_setint (engine->settings, "synth.dynamic-sample-loading", 1);
    fluid_settings_setstr (engine->settings, "synth.portamento-time", "linear");   // CC5/CC37 = milliseconds

    engine->synth = new_fluid_synth (engine->settings);
    if (engine->synth == nullptr)
        return nullptr;

    engine->fontId = fluid_synth_sfload (engine->synth, file.getFullPathName().toRawUTF8(), 0);   // presets chosen by applyPreset
    if (engine->fontId == FLUID_FAILED)
        return nullptr;

    // 17-point sinc: within -63 to -85 dB of the 25-point one (the closest to ideal resampling)
    // where the 4th-order default is at -43 dB, for samples pitched far from their root. Real
    // time thanks to the tabulated kernel in fluidsynth-fx-bypass.patch (FluidSynth 2.6 computes
    // its sines per sample: the 25-point one took 146 % of a block at 64 notes; this takes 11 %).
    fluid_synth_set_interp_method (engine->synth, -1, FLUID_INTERP_HIGH);
    return engine;
}

void SF2PlayerProcessor::applyLayerPreset (Engine& e, int presetIndex)
{
    if (presetIndex < 0 || presetIndex >= (int) presetList.size())
        return;
    const auto& p = presetList[(size_t) presetIndex];
    for (int channel = layerChannelOffset; channel < numSynthChannels; ++channel)
        fluid_synth_program_select (e.synth, channel, e.fontId, p.bank, p.number);
}

void SF2PlayerProcessor::selectLayerPreset (int presetIndex)
{
    const StateWriter writer (*this);
    if (presetIndex < 0 || presetIndex >= (int) presetList.size())
        return;
    {
        const juce::SpinLock::ScopedLockType lock (engineLock);
        if (engine == nullptr)
            return;
        for (int channel = layerChannelOffset; channel < numSynthChannels; ++channel)
            fluid_synth_all_notes_off (engine->synth, channel);
        applyLayerPreset (*engine, presetIndex);
    }
    layerPreset = presetIndex;
    sendChangeMessage();
    markProjectChanged();
}

void SF2PlayerProcessor::applyPreset (Engine& e, int presetIndex)
{
    if (presetIndex < 0 || presetIndex >= (int) presetList.size())
        return;
    const auto& p = presetList[(size_t) presetIndex];
    for (int channel = 0; channel < numMidiChannels; ++channel)
        fluid_synth_program_select (e.synth, channel, e.fontId, p.bank, p.number);
}

void SF2PlayerProcessor::swapEngine (std::unique_ptr<Engine> fresh)
{
    {
        const juce::SpinLock::ScopedLockType lock (engineLock);
        std::swap (engine, fresh);
        ++engineGeneration;
        if (engine != nullptr)
        {
            applyEffectSettings (true);
            applyPlaySettings (true);
        }
    }
    // `fresh` now holds the old engine: freed here, outside the lock.
}

bool SF2PlayerProcessor::loadSoundFont (const juce::File& file)
{
    const StateWriter writer (*this);
    auto fresh = createEngine (file, sampleRate);
    if (fresh == nullptr)
        return false;

    // Its presets, sorted like a sound module's banks.
    std::vector<Preset> list;
    auto info = SoundFontInfo::read (file);
    if (auto* font = fluid_synth_get_sfont_by_id (fresh->synth, fresh->fontId))
    {
        fluid_sfont_iteration_start (font);
        while (auto* preset = fluid_sfont_iteration_next (font))
        {
            Preset p;
            p.name = juce::String::fromUTF8 (fluid_preset_get_name (preset));
            p.bank = fluid_preset_get_banknum (preset);
            p.number = fluid_preset_get_num (preset);
            if (auto range = info.keyRanges.find ({ p.bank, p.number }); range != info.keyRanges.end())
                p.lowKey = range->second.first, p.highKey = range->second.second;
            if (auto amounts = info.sends.find ({ p.bank, p.number }); amounts != info.sends.end())
                p.sends = amounts->second;
            list.push_back (p);
        }
    }
    std::sort (list.begin(), list.end(), [] (const Preset& a, const Preset& b)
               { return std::tie (a.bank, a.number) < std::tie (b.bank, b.number); });

    // The project's instrument by bank:program (its index changes if the file gained presets),
    // else the same index when the same file is reloaded, else the first.
    int preset = (file == currentFile) ? juce::jlimit (0, juce::jmax (0, (int) list.size() - 1), currentPreset) : 0;
    if (savedBank >= 0)
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].bank == savedBank && list[i].number == savedNumber)
                preset = (int) i;
    savedBank = savedNumber = -1;
    int layer = (file == currentFile) ? juce::jlimit (0, juce::jmax (0, (int) list.size() - 1), layerPreset) : 0;
    if (savedLayerBank >= 0)
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].bank == savedLayerBank && list[i].number == savedLayerNumber)
                layer = (int) i;
    savedLayerBank = savedLayerNumber = -1;
    presetList = std::move (list);
    applyPreset (*fresh, preset);
    applyLayerPreset (*fresh, layer);
    layerPreset = layer;
    swapEngine (std::move (fresh));

    currentFile = file;
    missingFile = juce::File();
    failedFile = juce::File();   // a later failure starts again from a one-minute wait
    retryWaitMs = 60000;
    currentInfo = std::move (info);
    currentPreset = preset;
    sendChangeMessage();
    markProjectChanged();
    return true;
}

void SF2PlayerProcessor::selectPreset (int presetIndex)
{
    const StateWriter writer (*this);
    if (presetIndex < 0 || presetIndex >= (int) presetList.size())
        return;
    {
        const juce::SpinLock::ScopedLockType lock (engineLock);
        if (engine == nullptr)
            return;
        fluid_synth_all_notes_off (engine->synth, -1);
        applyPreset (*engine, presetIndex);
    }
    currentPreset = presetIndex;
    sendChangeMessage();
    markProjectChanged();
}


void SF2PlayerProcessor::prepareToPlay (double newSampleRate, int maximumExpectedSamplesPerBlock)
{
    work.setSize (6, juce::jmax (1, maximumExpectedSamplesPerBlock));
    shortMidi.ensureSize (32768);
    toneFrequency = 0.0f;   // recompute the filters for this rate
    for (auto& channel : toneFilters)
        for (auto& stage : channel)
            stage.z1 = stage.z2 = 0.0f;
    const StateWriter writer (*this);
    const bool rateChanged = ! juce::exactlyEqual (newSampleRate, sampleRate);
    sampleRate = newSampleRate;

    // FluidSynth takes its sample rate when created: build it again for the new one. If that
    // fails (the file moved or can't be read), stop playing rather than play out of tune at the
    // old rate: the missing-file state, which reloads it once it's back.
    if (rateChanged && currentFile != juce::File())
    {
        if (auto fresh = currentFile.existsAsFile() ? createEngine (currentFile, sampleRate) : nullptr)
        {
            applyPreset (*fresh, currentPreset);
            applyLayerPreset (*fresh, layerPreset);
            swapEngine (std::move (fresh));
        }
        else
            enterMissingState (currentFile, true, currentFile.existsAsFile());
    }
}

// ---------------------------------------------------------------------------- audio

void SF2PlayerProcessor::handleMidi (const juce::MidiMessage& m)
{
    const int channel = m.getChannel() - 1;
    if (channel < 0 || channel >= numMidiChannels)
        return;
    auto* synth = engine->synth;
    const int layer = channel + layerChannelOffset;
    const int layerMode = juce::roundToInt (parameters.getRawParameterValue ("layerMode")->load());   // 0 off, 1 layer, 2 split

    if (m.isNoteOn())
    {
        ++notesReceived;
        lastVelocity = m.getVelocity();
        const int note = transposeFor (m.getNoteNumber());
        if (! juce::isPositiveAndBelow (note, 128))
            return;   // transposed off the keyboard
        playedNote[(size_t) channel][(size_t) m.getNoteNumber()] = (juce::int16) note;
        const int velocity = curvedVelocity (m.getVelocity());
        // Split: the key as played (not transposed) picks the side.
        const bool lower = layerMode == 2 && m.getNoteNumber() < juce::roundToInt (parameters.getRawParameterValue ("splitKey")->load());
        if (! lower)
            fluid_synth_noteon (synth, channel, note, velocity);
        if (layerMode == 1 || lower)
            fluid_synth_noteon (synth, layer, note, velocity);
    }
    else if (m.isNoteOff())
    {
        auto& played = playedNote[(size_t) channel][(size_t) m.getNoteNumber()];
        const int note = played >= 0 ? played : transposeFor (m.getNoteNumber());
        played = -1;
        if (juce::isPositiveAndBelow (note, 128))
        {
            // Both sides: the mode or the split may have changed while the key was down.
            fluid_synth_noteoff (synth, channel, note);
            fluid_synth_noteoff (synth, layer, note);
        }
    }
    else if (m.isProgramChange())
    {
        // At once, as FluidSynth does, so a note sent with it plays the new instrument and an
        // export renders it exactly. The first switch to an instrument not in memory reads its
        // samples from disk here (dynamic sample loading): a possible glitch live, once.
        fluid_synth_program_change (synth, channel, m.getProgramChangeNumber());
        // The preset FluidSynth actually chose (it falls back to bank 0 when the bank is missing).
        if (auto* preset = fluid_synth_get_channel_preset (synth, channel))
        {
            const int key = fluid_preset_get_banknum (preset) * 128 + fluid_preset_get_num (preset);
            if (key != lastPinRequest[(size_t) channel])   // a repeated program change queues nothing
                pinFifo.write (1).forEach ([this, key, channel] (int index)
                {
                    pinQueue[(size_t) index] = key;
                    lastPinRequest[(size_t) channel] = key;   // only once it's queued (a full FIFO skips it)
                });
        }
    }
    else
    {
        // Controllers, bends and pressure reach the layer too, so it follows the playing.
        for (const int target : { channel, layer })
        {
            if (m.isController())   // volume, pan, expression, sustain, mod wheel, CC91/93, bank select...
            {
                if (target == layer && (m.getControllerNumber() == 0 || m.getControllerNumber() == 32))
                    continue;   // bank select is for the instrument
                fluid_synth_cc (synth, target, m.getControllerNumber(), m.getControllerValue());
                if (m.getControllerNumber() == 121)   // Reset All Controllers cleared the plugin's offsets
                    applyChannelSettings (target, appliedPlay);
            }
            else if (m.isPitchWheel())
                fluid_synth_pitch_bend (synth, target, m.getPitchWheelValue());
            else if (m.isChannelPressure())
                fluid_synth_channel_pressure (synth, target, m.getChannelPressureValue());
            else if (m.isAftertouch())
            {
                const int played = playedNote[(size_t) channel][(size_t) m.getNoteNumber()];
                if (played >= 0)
                    fluid_synth_key_pressure (synth, target, played, m.getAfterTouchValue());
            }
        }
    }
}

// Renders [start, end) of `work`: FluidSynth adds to the buffers, which processBlock cleared.
void SF2PlayerProcessor::render (int start, int end)
{
    const int n = end - start;
    if (n <= 0)
        return;
    float* out[2] = { work.getWritePointer (0, start), work.getWritePointer (1, start) };
    float* fx[4] = { work.getWritePointer (2, start), work.getWritePointer (3, start),     // reverb L/R
                     work.getWritePointer (4, start), work.getWritePointer (5, start) };   // chorus L/R
    fluid_synth_process (engine->synth, n, 4, fx, 2, out);
}

void SF2PlayerProcessor::Biquad::setLowPass (double frequency, double rate, double q)
{
    const double w = juce::MathConstants<double>::twoPi * frequency / rate;
    const double cosw = std::cos (w), alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
    b0 = (float) ((1.0 - cosw) / 2.0 / a0);
    b1 = (float) ((1.0 - cosw) / a0);
    b2 = b0;
    a1 = (float) (-2.0 * cosw / a0);
    a2 = (float) ((1.0 - alpha) / a0);
}

void SF2PlayerProcessor::Biquad::process (float* samples, int count) noexcept
{
    for (int i = 0; i < count; ++i)
    {
        const float x = samples[i], y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        samples[i] = y;
    }
}

void SF2PlayerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int numSamples = buffer.getNumSamples();
    // Sysex isn't played, and building a message from it allocates: a block carrying one is
    // swapped for a copy without it (into a buffer reserved in prepareToPlay) before anything
    // reads it, the on-screen keyboard included (JUCE's helper builds a message per event).
    bool hasSysex = false;
    for (const auto metadata : midi)
        hasSysex = hasSysex || metadata.numBytes > 3;
    if (hasSysex)
    {
        shortMidi.clear();
        for (const auto metadata : midi)
            if (metadata.numBytes <= 3)
                shortMidi.addEvent (metadata.data, metadata.numBytes, metadata.samplePosition);
        midi.swapWith (shortMidi);
    }
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    if (work.getNumSamples() < numSamples)   // a host sending more than it announced
        work.setSize (6, numSamples, false, false, true);
    work.clear (0, numSamples);
    bool sendOutputs = false;

    {
        // Never wait: while the message thread holds the engine (choosing an instrument
        // reads its samples from disk), this block stays silent instead.
        const juce::SpinLock::ScopedTryLockType lock (engineLock);
        if (! lock.isLocked())
        {
            deferMidi (midi);   // kept for the next block
            return;
        }
        if (engine == nullptr)   // no SoundFont: nothing to play these events, or older ones
        {
            clearDeferredMidi();
            return;
        }
        if (const int generation = engineGeneration.load(); generation != seenGeneration)
        {
            seenGeneration = generation;   // a new engine: nothing pinned in it yet
            lastPinRequest.fill (-1);
        }
        applyEffectSettings (false);
        applyPlaySettings (false);
        sendOutputs = appliedEffects.sendOutputs;

        // Events a skipped block kept, first.
        playDeferredMidi();

        // Render up to each MIDI event, so notes start on the exact sample.
        int position = 0;
        for (const auto metadata : midi)
        {
            const int eventTime = juce::jlimit (0, numSamples, metadata.samplePosition);
            render (position, eventTime);
            position = eventTime;
            if (metadata.numBytes <= 3)   // sysex isn't played: building its message could allocate
                handleMidi (juce::MidiMessage (metadata.data, metadata.numBytes));
        }
        render (position, numSamples);
        activeVoices = fluid_synth_get_active_voice_count (engine->synth);
        if (interpolationOverride >= 0)
            fluid_synth_set_interp_method (engine->synth, -1, interpolationOverride), interpolationOverride = -1;
    }

    // Built-in effects: their output joins the main one.
    if (! sendOutputs)
        for (int ch = 2; ch < 6; ++ch)
            work.addFrom (ch % 2, 0, work, ch, 0, numSamples);

    // Tone, below its top: 4-pole Butterworth (Q 0.54 and 1.31).
    const float tone = parameters.getRawParameterValue ("tone")->load();
    if (tone < toneOff - 1.0f)
    {
        if (! juce::exactlyEqual (tone, toneFrequency))
        {
            const double frequency = juce::jmin ((double) tone, sampleRate * 0.45);
            for (auto& channel : toneFilters)
            {
                channel[0].setLowPass (frequency, sampleRate, 0.5412);
                channel[1].setLowPass (frequency, sampleRate, 1.3066);
            }
            toneFrequency = tone;
        }
        const int channels = sendOutputs ? 6 : 2;
        for (int ch = 0; ch < channels; ++ch)
            for (auto& stage : toneFilters[(size_t) ch])
                stage.process (work.getWritePointer (ch), numSamples);
    }
    else if (toneFrequency > 0.0f)   // switched off: start clean next time
    {
        for (auto& channel : toneFilters)
            for (auto& stage : channel)
                stage.z1 = stage.z2 = 0.0f;
        toneFrequency = 0.0f;
    }

    const float gain = juce::Decibels::decibelsToGain (parameters.getRawParameterValue ("gain")->load());
    work.applyGainRamp (0, numSamples, lastGain, gain);
    lastGain = gain;

    writeOutputs (buffer, numSamples, sendOutputs);

    const float peak = juce::jmax (work.getMagnitude (0, 0, numSamples), work.getMagnitude (1, 0, numSamples));
    float previous = outputPeak.load();
    while (peak > previous && ! outputPeak.compare_exchange_weak (previous, peak)) {}
}

// Main: dry (plus the built-in effects); Reverb / Chorus Send: the sends, only in send mode.
void SF2PlayerProcessor::writeOutputs (juce::AudioBuffer<float>& buffer, int numSamples, bool sendOutputs)
{
    const int buses = getBusCount (false);
    for (int bus = 0; bus < buses; ++bus)
    {
        if (bus > 0 && ! sendOutputs)
            break;
        auto out = getBusBuffer (buffer, false, bus);
        const int from = bus * 2;
        if (out.getNumChannels() >= 2)
        {
            out.copyFrom (0, 0, work, from, 0, numSamples);
            out.copyFrom (1, 0, work, from + 1, 0, numSamples);
        }
        else if (out.getNumChannels() == 1)
        {
            out.copyFrom (0, 0, work.getReadPointer (from), numSamples, 0.5f);
            out.addFrom (0, 0, work, from + 1, 0, numSamples, 0.5f);
        }
    }
}

// ---------------------------------------------------------------------------- state

juce::AudioProcessorEditor* SF2PlayerProcessor::createEditor()
{
    return new SF2PlayerEditor (*this);
}

void SF2PlayerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Never waits for a SoundFont to load: the file and instruments come from the published
    // snapshot. A missing file stays in the project, so finding it again restores it.
    const auto st = state();
    auto saved = parameters.copyState();
    saved.setProperty ("sf2Path", (st->file != juce::File() ? st->file : st->missing).getFullPathName(), nullptr);
    saved.setProperty ("preset", st->preset, nullptr);
    if (st->bank >= 0)
    {
        saved.setProperty ("presetBank", st->bank, nullptr);
        saved.setProperty ("presetNumber", st->number, nullptr);
    }
    if (st->layerBank >= 0)
    {
        saved.setProperty ("layerBank", st->layerBank, nullptr);
        saved.setProperty ("layerNumber", st->layerNumber, nullptr);
    }
    if (auto xml = saved.createXml())
        copyXmlToBinary (*xml, destData);
}

void SF2PlayerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    parameters.replaceState (state);

    const StateWriter writer (*this);
    const auto path = state.getProperty ("sf2Path").toString();
    const juce::File file = juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
    currentPreset = (int) state.getProperty ("preset", 0);
    savedBank = (int) state.getProperty ("presetBank", -1);
    savedNumber = (int) state.getProperty ("presetNumber", -1);
    savedLayerBank = (int) state.getProperty ("layerBank", -1);
    savedLayerNumber = (int) state.getProperty ("layerNumber", -1);

    // Published at once, before the (possibly long) load: a save meanwhile gets this project's
    // file and instruments along with its parameters, never the previous project's.
    {
        auto restoringSnapshot = std::make_shared<State>();
        restoringSnapshot->file = file;
        restoringSnapshot->preset = currentPreset;
        restoringSnapshot->bank = savedBank, restoringSnapshot->number = savedNumber;
        restoringSnapshot->layerBank = savedLayerBank, restoringSnapshot->layerNumber = savedLayerNumber;
        std::shared_ptr<const State> old;
        {
            const juce::SpinLock::ScopedLockType lock (publishedLock);
            old = std::exchange (published, std::move (restoringSnapshot));
        }
    }

    const juce::ScopedValueSetter<bool> restoring (restoringState, true);
    if (file.existsAsFile())
    {
        currentFile = file;   // so loadSoundFont keeps the saved preset
        if (loadSoundFont (file))
            return;
    }
    // A missing file is kept in the project (the editor says so); a state without a file (a
    // host's default) means no SoundFont. Either way the old one stops, so the sound, the
    // editor and the next save agree.
    enterMissingState (file, false, file.existsAsFile());   // the project's instruments are already saved above
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SF2PlayerProcessor();
}
