#include "PluginProcessor.h"
#include "DrumKit.h"
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
    // Guitar panel (version 6): every control starts where it changes nothing.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "guitarPanel", 6 }, "Guitar Panel",
                                                              juce::StringArray { "Auto", "On", "Off" }, 0));
    // Direct: a plain sampler, the guitar engine off (the default: the SoundFont as it is).
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "guitarDirect", 6 }, "Guitar Direct", true));   // neutral: a project from 1.2 plays as it did
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "guitarMode", 6 }, "Guitar Mode",
                                                              juce::StringArray { "Solo", "Harmony", "Chords", "Bass & Chord", "Bass & Pick" }, 2));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "guitarPattern", 6 }, "Guitar Pattern", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "guitarHold", 6 }, "Guitar Hold", false));
    {
        juce::StringArray names;
        for (const char* name : GuitarEngine::positionNames) names.add (name);
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "chordPosition", 6 }, "Chord Position", names, 0));
        names.clear();
        for (const char* name : GuitarEngine::harmonyNames) names.add (name);
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "harmonyInterval", 6 }, "Harmony Interval", names, 2));
        names.clear();
        for (const char* name : GuitarEngine::velocityFxNames) names.add (name);
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "velocityFx", 6 }, "Velocity Switch FX", names, 0));
        names.clear();
        for (const char* name : GuitarEngine::addOnNames) names.add (name);
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "addOnKeys", 6 }, "Add-on String Keys", names, 0));
    }
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "stringsTop", 6 }, "Strings From", 1, 6, 1));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "stringsBottom", 6 }, "Strings To", 1, 6, 6));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "repeatStrings", 6 }, "Repeat Strings", 1, 6, 4));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "chordBass", 6 }, "Chord/Bass", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "alterBass", 6 }, "Alter Bass", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bassMono", 6 }, "Bass Mono", false));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "fxVelocity", 6 }, "FX Velocity", 1, 127, 90));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "fxSteps", 6 }, "FX Steps", 1, 12, 2));
    {
        juce::StringArray names;
        for (const char* name : GuitarEngine::articulationNames) names.add (name);
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "soloFx", 6 }, "Solo Permanent FX",
                                                                  juce::StringArray { "Off", "H.O.", "Slide" }, 0));
        const int defaults[] = { GuitarEngine::artMuted, GuitarEngine::artHammer, GuitarEngine::artSlide, GuitarEngine::artTremolo };
        const char* keys[] = { "C1", "C#1", "D1", "D#1" };
        for (int i = 0; i < 4; ++i)
            layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "keySwitch" + juce::String (i + 1), 6 },
                                                                      juce::String ("Key Switch ") + keys[i], names, defaults[i]));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "pedalFx", 6 }, "Pedal FX", names, 0));
        names.clear();
        for (const char* name : GuitarEngine::velocityCurveNames) names.add (name);
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "guitarVeloCurve", 6 }, "Guitar Velocity Curve", names, 0));
    }
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "legatoSteps", 6 }, "Legato Steps", 1, 12, 2));
    layout.add (std::make_unique<Float> (juce::ParameterID { "legatoTime", 6 }, "Slide Time", range (10.0f, 400.0f, 1.0f), 60.0f,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                                             [] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; })));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bassZone", 6 }, "Bass Zone", false));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "bassZoneTop", 6 }, "Bass Zone Top", 40, 64, 52,
                                                           juce::AudioParameterIntAttributes().withStringFromValueFunction (
                                                               [] (int v, int) { return juce::MidiMessage::getMidiNoteName (v, true, true, 3); })));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "accent", 6 }, "Accent", 2, 126, 64));
    {
        // The wheels and aftertouch: what each one does, and how far (semitones).
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "pbAssign", 6 }, "Pitch Bender",
                                                                  juce::StringArray { "Pitch", "Slide", "Mono Bend" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mwAssign", 6 }, "Mod Wheel",
                                                                  juce::StringArray { "Modulation", "Pitch", "Slide", "Mono Bend" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "atAssign", 6 }, "Aftertouch",
                                                                  juce::StringArray { "Off", "Pitch", "Modulation" }, 0));
        for (const char* id : { "pbRange", "mwRange", "atRange" })
            layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, 6 }, juce::String (id).replace ("Range", " Range"), 1, 12, 2));
    }
    layout.add (std::make_unique<Float> (juce::ParameterID { "fxTime", 6 }, "FX Time", range (20.0f, 500.0f, 1.0f), 150.0f,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                                             [] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; })));
    {
        juce::StringArray names;
        for (const char* name : GuitarEngine::tuningNames) names.add (name);
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "guitarTuning", 6 }, "Guitar Tuning", names, 0));
    }
    {
        juce::StringArray names;
        for (const auto& rhythm : GuitarEngine::rhythms) names.add (rhythm.name);
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "strumPattern", 6 }, "Strum Pattern", names, 2));
    }
    layout.add (std::make_unique<Float> (juce::ParameterID { "strum", 6 }, "Strum", juce::NormalisableRange<float> (0.0f, 250.0f, 1.0f, 0.5f), 0.0f,
                                         Attributes().withStringFromValueFunction ([] (float v, int)
                                         { return v < 0.5f ? juce::String ("Off") : juce::String (juce::roundToInt (v)) + " ms"; })));
    layout.add (std::make_unique<Float> (juce::ParameterID { "humanize", 6 }, "Humanize", range (0.0f, 1.0f, 0.01f), 0.0f, percent));
    // DRUM KIT's Humanize: each hit a little louder or softer, and a little late, at random.
    layout.add (std::make_unique<Float> (juce::ParameterID { "drumVelocityHuman", 7 }, "Drum Humanize Velocity", range (0.0f, 1.0f, 0.01f), 0.0f, percent));
    layout.add (std::make_unique<Float> (juce::ParameterID { "drumTimingHuman", 7 }, "Drum Humanize Timing", range (0.0f, 1.0f, 0.01f), 0.0f, percent));
    layout.add (std::make_unique<Float> (juce::ParameterID { "doubleMix", 6 }, "Double", range (0.0f, 1.0f, 0.01f), 0.0f, percent));
    layout.add (std::make_unique<Float> (juce::ParameterID { "tremoloDepth", 6 }, "Tremolo Depth", range (0.0f, 1.0f, 0.01f), 0.0f, percent));
    layout.add (std::make_unique<Float> (juce::ParameterID { "tremoloRate", 6 }, "Tremolo Rate", range (0.5f, 12.0f, 0.1f), 5.0f,
                                         Attributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " Hz"; })));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "tremoloSync", 6 }, "Tremolo Sync",
                                                              juce::StringArray { "Free", "1/2", "1/4", "1/8", "1/16", "1/4T", "1/8T" }, 0));

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
    for (auto& channel : twinKey)
        channel.fill (-1);
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
    // Checked on the snapshot first: the writer (the mutex, a publish) only when one is due.
    if (const auto st = state(); st->file != juce::File() || st->missing == juce::File() || ! st->missing.existsAsFile())
        return;
    const StateWriter writer (*this);
    if (currentFile == juce::File() && missingFile != juce::File() && missingFile.existsAsFile() && shouldRetry (missingFile))
    {
        const auto file = missingFile;
        const Restoring restoring (restoringState);
        currentFile = file;   // so loadSoundFont keeps the project's instruments
        if (! loadSoundFont (file))
        {
            currentFile = juce::File();
            recordFailure (file);
            stateChanged = true;
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
    guitarInstrument = juce::isPositiveAndBelow (currentPreset, (int) presetList.size())
                       && isGuitar (presetList[(size_t) currentPreset]);
    if (juce::isPositiveAndBelow (currentPreset, (int) presetList.size()))
        fresh->bank = presetList[(size_t) currentPreset].bank, fresh->number = presetList[(size_t) currentPreset].number;
    else
        fresh->bank = savedBank, fresh->number = savedNumber;   // the file is missing: what the project had
    if (juce::isPositiveAndBelow (layerPreset, (int) presetList.size()))
        fresh->layerBank = presetList[(size_t) layerPreset].bank, fresh->layerNumber = presetList[(size_t) layerPreset].number;
    else
        fresh->layerBank = savedLayerBank, fresh->layerNumber = savedLayerNumber;
    exchangePublished (std::move (fresh));
}

void SF2PlayerProcessor::exchangePublished (std::shared_ptr<const State> fresh)
{
    {
        const juce::SpinLock::ScopedLockType lock (publishedLock);
        std::swap (published, fresh);
    }
    // `fresh` now holds the old snapshot: freed here, outside the SpinLock.
}

void SF2PlayerProcessor::enterMissingState (const juce::File& file, bool keepInstruments, bool unreadable)
{
    const StateWriter writer (*this);
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
    refreshKitFlag();   // no instrument while the file is missing: not a kit either
    stateChanged = true;
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

SF2PlayerProcessor::PlaySettings SF2PlayerProcessor::currentPlaySettings (bool guitarPlays) const
{
    auto value = [this] (const char* id) { return parameters.getRawParameterValue (id)->load(); };
    PlaySettings s;
    s.attack = value ("attack");
    s.release = value ("release");
    s.fineTune = juce::roundToInt (value ("fineTune"));
    s.bendRange = juce::roundToInt (value ("bendRange"));
    s.mode = juce::roundToInt (value ("playMode"));
    s.glide = juce::roundToInt (value ("glide"));
    // The Guitar panel playing (not Direct) strums chords on one channel and slides by itself:
    // Mono or Legato would keep one note of each chord, and Glide would slide string into string.
    if (guitarPlays)
    {
        s.mode = 0;
        s.glide = 0;
    }
    s.layerAttenuation = juce::roundToInt (-value ("layerLevel") * 10.0f);
    return s;
}

// Called with the engine lock held. The SoundFont's own values stay; these are offsets on
// top (SF2's real-time generator offsets, the same as NRPN), so at their defaults nothing changes.
// Only what changed is sent: the bend range and glide are also MIDI state a clip may set
// (RPN 0, CC65 / CC5), which moving Attack must not overwrite.
void SF2PlayerProcessor::applyPlaySettings (bool force, bool guitarPlays)
{
    const auto s = currentPlaySettings (guitarPlays);
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
        if (isLayerChannel (channel) && s.layerAttenuation != a.layerAttenuation)
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
    if (isLayerChannel (channel))
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
        const bool lasting = changesLastingState (m);
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

// Note-offs, pedals, bank and program changes, parameter numbers and data, resets: what a flood
// must never lose (a continuous value's next one replaces it).
bool SF2PlayerProcessor::changesLastingState (const juce::MidiMessage& m)
{
    const int cc = m.isController() ? m.getControllerNumber() : -1;
    const bool rpn = cc == 6 || cc == 38 || (cc >= 96 && cc <= 101);
    return m.isNoteOff() || m.isProgramChange() || rpn || (cc >= 0 && keptInOrder (cc));
}

void SF2PlayerProcessor::playDeferredMidi (const GuitarEngine::Settings* guitarSettings, juce::int64 at, const Humanize& human)
{
    if (numDeferred == 0 && ! anyCollapsed)
        return;   // the usual block: nothing kept
    for (int i = 0; i < numDeferred; ++i)
    {
        const auto& m = deferredEvents[(size_t) i];
        const int channel = m.getChannel() - 1;
        if (guitarSettings == nullptr)
        {
            playOrWait (m, at, human);   // the block's own rules: Humanize, and waiting for its key
            continue;
        }
        if (juce::isPositiveAndBelow (channel, numMidiChannels))
        {
            if (guitarController (m, *guitarSettings, channel, at)) continue;   // a wheel with a job
            if (m.isNoteOn()) { if (GuitarEngine::inMain (m.getNoteNumber())) lastPlayedVelocity = m.getVelocity(); playedGuitar = true; guitar.noteOn (*guitarSettings, channel, m.getNoteNumber(), m.getVelocity(), at); continue; }
            if (m.isNoteOff() && guitar.noteOff (channel, m.getNoteNumber(), at)) continue;
            if (m.isAllNotesOff() || m.isAllSoundOff()) guitar.allOff (channel, at);
        }
        handleMidi (m);
    }
    // Collapsed controllers: the wheels go through the guitar when it gives them a job.
    auto send = [&] (const juce::MidiMessage& m, int channel)
    {
        if (guitarSettings == nullptr) noteWheelPosition (m);
        else if (guitarController (m, *guitarSettings, channel, at)) return;   // it notes the position itself
        handleMidi (m);
    };
    for (int channel = 0; channel < 16 && anyCollapsed; ++channel)
    {
        for (int cc = 0; cc < 128; ++cc)
            if (const int value = deferredController[(size_t) channel][(size_t) cc]; value >= 0)
                send (juce::MidiMessage::controllerEvent (channel + 1, cc, value), channel);
        if (deferredPressure[(size_t) channel] >= 0)
            send (juce::MidiMessage::channelPressureChange (channel + 1, deferredPressure[(size_t) channel]), channel);
        if (deferredBend[(size_t) channel] >= 0)
            send (juce::MidiMessage::pitchWheel (channel + 1, deferredBend[(size_t) channel]), channel);
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
    for (int channel = layerChannelOffset; channel < doubleChannelOffset; ++channel)
        fluid_synth_program_select (e.synth, channel, e.fontId, p.bank, p.number);
    ++pinGeneration;   // the channels' presets changed: a clip's next program change pins afresh
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
        for (int channel = layerChannelOffset; channel < doubleChannelOffset; ++channel)
            fluid_synth_all_notes_off (engine->synth, channel);
        applyLayerPreset (*engine, presetIndex);
    }
    layerPreset = presetIndex;
    stateChanged = true;
    markProjectChanged();
}

void SF2PlayerProcessor::applyPreset (Engine& e, int presetIndex)
{
    if (presetIndex < 0 || presetIndex >= (int) presetList.size())
        return;
    const auto& p = presetList[(size_t) presetIndex];
    for (int channel = 0; channel < numMidiChannels; ++channel)
    {
        fluid_synth_program_select (e.synth, channel, e.fontId, p.bank, p.number);
        fluid_synth_program_select (e.synth, channel + doubleChannelOffset, e.fontId, p.bank, p.number);
    }
    ++pinGeneration;
}

void SF2PlayerProcessor::swapEngine (std::unique_ptr<Engine> fresh)
{
    {
        const juce::SpinLock::ScopedLockType lock (engineLock);
        std::swap (engine, fresh);
        ++pinGeneration;
        guitar.reset();   // its notes were in the old engine
        guitarEngaged = false;
        clearWheels();   // the new engine's voices start in tune
        dropLateHits();   // Humanize's waiting hits were meant for the old instrument
        refreshKitFlag();   // the instrument's id and kit flag with the engine, under the lock
        kitGeneration.fetch_add (1);   // every channel plays the chosen instrument again: none is "moved"
        if (engine != nullptr)
        {
            applyEffectSettings (true);
            applyPlaySettings (true, guitarPlaying());
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
            p.drumKit = DrumKit::isKit (p.bank, p.name)
                     || (info.kitShaped.count ({ p.bank, p.number }) > 0 && ! DrumKit::soundsMelodic (p.name));
            if (auto range = info.keyRanges.find ({ p.bank, p.number }); range != info.keyRanges.end())
                p.lowKey = range->second.first, p.highKey = range->second.second;
            if (auto set = info.keySets.find ({ p.bank, p.number }); set != info.keySets.end() && set->second.any())
                p.keys = set->second;
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
    currentPreset = preset;   // before the swap, which publishes the kit flag with it
    swapEngine (std::move (fresh));   // publishes the kit flag with the engine

    currentFile = file;
    missingFile = juce::File();
    failedFile = juce::File();   // a later failure starts again from a one-minute wait
    retryWaitMs = 60000;
    currentInfo = std::move (info);
    stateChanged = true;
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
        dropLateHits();   // Humanize's waiting hits were meant for the old instrument
        currentPreset = presetIndex;
        refreshKitFlag();   // with the change itself, under the lock: no block sees the new
        kitGeneration.fetch_add (1);   // instrument with the old one's id, and a clip's program change right after still counts
    }
    stateChanged = true;
    markProjectChanged();
}


void SF2PlayerProcessor::prepareToPlay (double newSampleRate, int maximumExpectedSamplesPerBlock)
{
    work.setSize (6, juce::jmax (1, maximumExpectedSamplesPerBlock));
    playedHere.ensureSize (4096);
    shortMidi.ensureSize (1 << 19);   // as much as blockMidi: a block with sysex is copied here
    blockMidi.ensureSize (1 << 19);   // 512 KB: about 58,000 events, past any host's block
    previewHere.ensureSize (1024);
    {
        const juce::SpinLock::ScopedLockType lock (engineLock);
        if (guitarEngaged && engine != nullptr)
            flushGuitar();   // the same engine plays on: end what the guitar started before forgetting it
        guitar.prepare (newSampleRate);
        guitarEngaged = false;
        sampleClock = 0;
        tremoloPhase = 0.0;
        // Humanize's late hits (and pressure for them) belong to the timeline that stopped (an
        // export starting shouldn't play one); a waiting note-off still ends its note, first thing
        // on the new timeline.
        dropLateHits();
    }
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
        if (! velocityRecorded) lastVelocity = m.getVelocity();   // (Humanize records the clip's own, before it varies it)
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
        // The Guitar panel's Double plays the same instrument: it follows the clip's bank and program.
        fluid_synth_program_change (synth, channel + doubleChannelOffset, m.getProgramChangeNumber());
        // The preset FluidSynth actually chose (it falls back to bank 0 when the bank is missing).
        channelMoved.set ((size_t) channel);   // the kit's Humanize stays on this channel only while it plays the kit
        if (auto* preset = fluid_synth_get_channel_preset (synth, channel))
        {
            const int key = fluid_preset_get_banknum (preset) * 128 + fluid_preset_get_num (preset);
            if (key == presetId.load (std::memory_order_relaxed))
                channelMoved.reset ((size_t) channel);
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
        // Controllers, bends and pressure reach the layer and the Double too, so they follow the playing.
        for (const int target : { channel, layer, channel + doubleChannelOffset })
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

// The earliest late note due before `until`, taken off the list (false: none).
// Equal times play in the order they came in. Nothing due (the usual case, before every event): no
// scan. One pass finds the earliest and the time of the next, which becomes the earliest after it.
bool SF2PlayerProcessor::takeEarliestLate (juce::int64 until, LateNote& out)
{
    if (lateCount == 0 || nextLateAt >= until) return false;
    int first = 0;
    auto next = std::numeric_limits<juce::int64>::max();
    for (int i = 1; i < lateCount; ++i)
    {
        const auto& late = lateNotes[(size_t) i];
        const auto& best = lateNotes[(size_t) first];
        if (earlier (late, best))
        {
            next = juce::jmin (next, best.at);
            first = i;
        }
        else
            next = juce::jmin (next, late.at);
    }
    if (lateNotes[(size_t) first].at >= until)   // nextLateAt out of date (a removal that didn't refresh it): never play early
    {
        jassertfalse;
        nextLateAt = lateNotes[(size_t) first].at;
        return false;
    }
    out = lateNotes[(size_t) first];
    lateNotes[(size_t) first] = lateNotes[(size_t) --lateCount];
    nextLateAt = next;
    return true;
}

// A key's waiting messages, now, in the order they came in (the list is full and one more must
// follow them): moved to the list's end, played, then cut off.
void SF2PlayerProcessor::playKeyNow (int channel, int key)
{
    const auto begin = lateNotes.begin(), end = begin + lateCount;
    // std::partition doesn't allocate (stable_partition may); the order of what stays doesn't matter.
    const auto mine = std::partition (begin, end, [&] (const LateNote& late)
                                      { return channelOf (late) != channel || late.slot != key; });   // as queued (a split moved since can't hide it)
    std::sort (mine, end, [] (const LateNote& a, const LateNote& b) { return a.order < b.order; });
    lateCount = (int) (mine - begin);
    for (auto it = mine; it != end; ++it)
        handleMidi (juce::MidiMessage (it->bytes, it->size));
    keyDue[(size_t) channel][(size_t) key] = -1;
    refreshNextLate();
}

void SF2PlayerProcessor::refreshNextLate()
{
    nextLateAt = std::numeric_limits<juce::int64>::max();
    for (int i = 0; i < lateCount; ++i)
        nextLateAt = juce::jmin (nextLateAt, lateNotes[(size_t) i].at);
}

// Drum Humanize's late notes due before `until`, earliest first, each at its own sample of this
// block (rendering up to it); one already overdue (a block skipped) plays at once.
void SF2PlayerProcessor::playLateNotes (juce::int64 until, juce::int64 blockStart, int& position)
{
    const juce::ScopedValueSetter<bool> recorded (velocityRecorded, true);   // their velocities were recorded as they came
    LateNote late;
    while (takeEarliestLate (until, late))
    {
        const int at = (int) juce::jlimit ((juce::int64) position, (juce::int64) work.getNumSamples(), late.at - blockStart);
        render (position, at);
        position = juce::jmax (position, at);
        handleMidi (juce::MidiMessage (late.bytes, late.size));
    }
}

// One message of a block (or of a skipped block, at its start) outside the Guitar panel: Drum
// Humanize's velocity, then played now or kept to play later. Only a key's messages wait, and only
// for that key (ties keep the order they came in): a late hit after its key's waiting note-off, a
// note-off or the key's pressure after its late hit (as long as the hit waited, so it keeps its
// length), a hit again after the last one ended. Everything else plays as it comes.
void SF2PlayerProcessor::playOrWait (juce::MidiMessage message, juce::int64 now, const Humanize& human)
{
    noteWheelPosition (message);
    const int channel = message.getChannel() - 1;
    const bool onChannel = juce::isPositiveAndBelow (channel, 16);
    if (onChannel && isAllOffBytes (message.getRawData(), message.getRawDataSize()))   // nothing on that channel waits past it (only notes wait)
    {
        for (int i = lateCount; --i >= 0;)
            if (channelOf (lateNotes[(size_t) i]) == channel)
                lateNotes[(size_t) i] = lateNotes[(size_t) --lateCount];
        forgetKeyWaits (channel);
        refreshNextLate();
    }
    if (message.isNoteOn()) lastVelocity = message.getVelocity();   // as it came, for the soft-notes warning
    const juce::ScopedValueSetter<bool> recorded (velocityRecorded, true);
    const bool kitNote = message.isNoteOn() && onChannel && ! channelMoved.test ((size_t) channel)
                      && message.getNoteNumber() >= human.splitBelow;
    if (kitNote && human.velocity > 0.0f)
    {
        const float factor = 1.0f + (humanRandom.nextFloat() * 2.0f - 1.0f) * 0.3f * human.velocity;
        message.setVelocity (juce::jlimit (1.0f / 127.0f, 1.0f, message.getFloatVelocity() * factor));
    }
    if (message.isNoteOn())
    {
        lastPlayedVelocity = message.getVelocity();   // Direct: the note as played
        if (human.panelShown) playedGuitar = true;           // the panel played (in Direct): too late for the start
    }
    juce::int64 lateAt = -1;
    const bool keyMessage = onChannel && message.getRawDataSize() == 3 && (message.isNoteOnOrOff() || message.isAftertouch());
    // Keys that cut each other off (a kit's hi-hat: closed, pedal, open) wait as one, so a later
    // closed hat never sounds before the open one it must stop.
    const int key = keyMessage ? slotOf (message.getNoteNumber() & 0x7f, human.splitBelow) : 0;
    if (keyMessage)
    {
        // A quarter of the list stays for what must follow a waiting hit (its note-off,
        // a hit again): Humanize's own delay and a key's pressure wait only above it.
        const bool spare = lateCount < (int) lateNotes.size() * 3 / 4;
        const auto waits = keyDue[(size_t) channel][(size_t) key];
        if (message.isAftertouch() && waits > now && ! spare)
            return;   // pressure behind a waiting message of its key, with no room to spare: dropped
        auto& delay = keyDelay[(size_t) channel][(size_t) (message.getNoteNumber() & 0x7f)];   // the key's last hit's Humanize delay
        if (message.isNoteOn())
            delay = kitNote && human.maxDelay >= 1.0 && spare ? (juce::int32) (humanRandom.nextDouble() * human.maxDelay) : 0;
        // A hit waits its delay; its note-off waits as long, so the hit keeps the length played.
        if (delay > 0 && ! message.isAftertouch())
            lateAt = now + delay;
        if (waits > now)
            lateAt = juce::jmax (lateAt, waits);
        if (message.isNoteOn())   // what the hit really waits (its own delay, or longer behind its key): its note-off waits as long
            delay = (juce::int32) juce::jmax ((juce::int64) 0, lateAt - now);
        if (lateAt > now && lateCount == (int) lateNotes.size())
        {
            // No room to wait: the key's waiting messages play now, in order, then this
            // one, so its order holds whatever the list holds (a rare flood).
            playKeyNow (channel, key);
            lateAt = -1;
            if (message.isNoteOn()) delay = 0;   // it plays now, so its note-off needn't wait
        }
    }
    if (lateAt > now && lateCount < (int) lateNotes.size())
    {
        keyDue[(size_t) channel][(size_t) key] = lateAt;
        nextLateAt = juce::jmin (nextLateAt, lateAt);
        auto& late = lateNotes[(size_t) lateCount++];
        late.at = lateAt;
        late.order = lateOrder++;
        late.slot = key;
        late.size = 3;
        std::copy (message.getRawData(), message.getRawData() + 3, late.bytes);
        return;
    }
    handleMidi (message);
}

// The host's notes onto the window's keys and lights. JUCE's state ends notes on CC123, not on an
// all-sound-off (CC120): each is shown as one, in its place in the block, by patching the byte in
// place for that one read (no allocation), then putting it back.
void SF2PlayerProcessor::showHostNotes (juce::MidiBuffer& fromHost, int numSamples)
{
    int patchedCount = 0;
    for (const auto metadata : fromHost)
        if (metadata.numBytes == 3 && (metadata.data[0] & 0xf0) == 0xb0 && metadata.data[1] == 120 && patchedCount < (int) patchedSoundOffs.size())
        {
            auto* byte = const_cast<juce::uint8*> (metadata.data) + 1;
            *byte = 123;
            patchedSoundOffs[(size_t) patchedCount++] = byte;
        }
    hostNotes.processNextMidiBuffer (fromHost, 0, numSamples, false);   // reads it only (nothing injected)
    for (int i = 0; i < patchedCount; ++i)
        *patchedSoundOffs[(size_t) i] = 120;
}

// Every late note at once, in time order, rendering nothing (whoever renders the block next plays them).
void SF2PlayerProcessor::playLateNotesNow()
{
    const juce::ScopedValueSetter<bool> recorded (velocityRecorded, true);
    LateNote late;
    while (takeEarliestLate (std::numeric_limits<juce::int64>::max(), late))
        handleMidi (juce::MidiMessage (late.bytes, late.size));
    forgetKeyWaits();   // nothing waits: what comes next needn't wait for these
}

// Every waiting late note, gone (the engine that would play them is).
void SF2PlayerProcessor::clearLateNotes()
{
    lateCount = 0;
    refreshNextLate();
    forgetKeyWaits();
}

// Humanize's waiting hits are dropped (their timeline or instrument is gone); a waiting note-off
// still ends its note, first thing next. Under engineLock or on the audio thread.
void SF2PlayerProcessor::dropLateHits()
{
    int kept = 0;
    for (int i = 0; i < lateCount; ++i)
        if (const auto& late = lateNotes[(size_t) i]; isNoteOffBytes (late.bytes, late.size))
        {
            lateNotes[(size_t) kept] = lateNotes[(size_t) i];
            lateNotes[(size_t) kept++].at = 0;
        }
    lateCount = kept;
    refreshNextLate();
    forgetKeyWaits();
}

// The keys' waiting times and Humanize delays, for one channel or all (-1), where messages leave
// the list other than by playing in turn.
void SF2PlayerProcessor::forgetKeyWaits (int channel)
{
    for (int c = 0; c < 16; ++c)
        if (channel < 0 || c == channel)
        {
            keyDue[(size_t) c].fill (-1);
            keyDelay[(size_t) c].fill (0);
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

// ---------------------------------------------------------------------------- guitar

// Under engineLock, with an engine.
void SF2PlayerProcessor::flushGuitar()
{
    guitar.reset ([this] (int channel, int note, bool doubled)
    {
        if (! doubled)
            handleMidi (juce::MidiMessage::noteOff (channel + 1, note));
        else if (auto& key = twinKey[(size_t) channel][(size_t) note]; key >= 0)
        {
            fluid_synth_noteoff (engine->synth, channel + doubleChannelOffset, key);
            key = -1;
        }
    });
    guitarEngaged = false;
    // The guitar's retuning comes off the voices still ringing (their tails would get the wheel twice).
    {
        std::array<fluid_voice_t*, 256> voices {};
        fluid_synth_get_voicelist (engine->synth, voices.data(), (int) voices.size(), -1);
        for (auto* voice : voices)
            if (voice != nullptr)
                if (auto* applied = appliedPitchFor (voice, (juce::int64) fluid_voice_get_id (voice)); applied != nullptr && applied->cents != 0.0f)
                {
                    fluid_voice_gen_incr (voice, GEN_FINETUNE, -applied->cents);
                    fluid_voice_update_param (voice, GEN_FINETUNE);
                    applied->cents = 0.0f;
                }
    }
    // The guitar stops (Direct, the panel hidden): the synth takes every wheel back where it is.
    for (int channel = 0; channel < 16; ++channel)
    {
        if (lastPbAssign != 0)
            handleMidi (juce::MidiMessage::pitchWheel (channel + 1, juce::jlimit (0, 16383, 8192 + juce::roundToInt (wheelBend[(size_t) channel] * 8192.0f))));
        if (lastMwAssign != 0 || lastAtAssign == 2)   // the mod wheel, or the CC1 pressure drove
            handleMidi (juce::MidiMessage::controllerEvent (channel + 1, 1, juce::roundToInt (wheelMod[(size_t) channel] * 127.0f)));
        if (lastAtAssign != 0)
            handleMidi (juce::MidiMessage::channelPressureChange (channel + 1, juce::roundToInt (wheelPressure[(size_t) channel] * 127.0f)));
    }
    clearWheels();
}

void SF2PlayerProcessor::setGuitarShape (const std::array<int, 6>& frets)
{
    const auto packed = GuitarEngine::packShape (frets);
    if (guitarShape.exchange (packed) != packed)
        markProjectChanged();
}

bool SF2PlayerProcessor::isGuitar (const Preset& p)
{
    // General MIDI's guitars are programs 24-31 (nylon, steel, jazz, clean, muted, overdriven,
    // distortion, harmonics), in the melodic banks; any other preset named like one.
    if (p.bank != 128 && p.number >= 24 && p.number <= 31)
        return true;
    const auto name = p.name.toLowerCase();
    return name.contains ("guitar") || name.contains ("gtr");
}

bool SF2PlayerProcessor::guitarPanelActive() const
{
    switch (juce::roundToInt (parameters.getRawParameterValue ("guitarPanel")->load()))
    {
        case 1:  return true;
        case 2:  return false;
        default: return guitarInstrument.load();
    }
}

GuitarEngine::Settings SF2PlayerProcessor::currentGuitarSettings() const
{
    auto value = [this] (const char* id) { return parameters.getRawParameterValue (id)->load(); };
    GuitarEngine::Settings s;
    s.direct = value ("guitarDirect") > 0.5f;
    s.mode = juce::jlimit (0, GuitarEngine::modeCount - 1, juce::roundToInt (value ("guitarMode")));
    s.pattern = value ("guitarPattern") > 0.5f;
    s.hold = value ("guitarHold") > 0.5f;
    s.strumMs = value ("strum");
    s.humanize = value ("humanize");
    s.doubleMix = value ("doubleMix");
    s.rhythm = juce::jlimit (0, GuitarEngine::rhythmCount - 1, juce::roundToInt (value ("strumPattern")));
    s.tuning = juce::roundToInt (value ("guitarTuning"));
    s.position = juce::roundToInt (value ("chordPosition"));
    s.topString = juce::roundToInt (value ("stringsTop"));
    s.bottomString = juce::jmax (s.topString, juce::roundToInt (value ("stringsBottom")));
    s.repeatStrings = juce::roundToInt (value ("repeatStrings"));
    s.chordBass = value ("chordBass") > 0.5f;
    s.alterBass = value ("alterBass") > 0.5f;
    s.bassMono = value ("bassMono") > 0.5f;
    s.addOn = juce::roundToInt (value ("addOnKeys"));
    s.harmony = juce::roundToInt (value ("harmonyInterval"));
    s.velocityFx = juce::roundToInt (value ("velocityFx"));
    s.fxVelocity = juce::roundToInt (value ("fxVelocity"));
    s.fxTimeMs = value ("fxTime");
    s.fxSteps = juce::roundToInt (value ("fxSteps"));
    s.soloFx = juce::roundToInt (value ("soloFx"));
    static constexpr const char* keySwitchIds[] = { "keySwitch1", "keySwitch2", "keySwitch3", "keySwitch4" };   // no String built here: audio thread
    for (int i = 0; i < 4; ++i) s.keySwitch[(size_t) i] = juce::roundToInt (value (keySwitchIds[i]));
    s.pedalFx = juce::roundToInt (value ("pedalFx"));
    s.legatoSteps = juce::roundToInt (value ("legatoSteps"));
    s.legatoMs = value ("legatoTime");
    s.bassZone = value ("bassZone") > 0.5f;
    s.bassZoneTop = juce::roundToInt (value ("bassZoneTop"));
    s.velocityCurve = juce::roundToInt (value ("guitarVeloCurve"));
    s.accent = juce::roundToInt (value ("accent"));
    return s;
}

// -1 with no voice playing: FluidSynth numbers notes from 0, so 0 is a real voice's id.
juce::int64 SF2PlayerProcessor::newestVoiceId() const
{
    std::array<fluid_voice_t*, 256> voices {};
    fluid_synth_get_voicelist (engine->synth, voices.data(), (int) voices.size(), -1);
    juce::int64 newest = -1;
    for (auto* voice : voices)
        if (voice != nullptr)
            newest = juce::jmax (newest, (juce::int64) fluid_voice_get_id (voice));
    return newest;
}

void SF2PlayerProcessor::tweakNewVoices (int channel, int detuneCents, bool palmMuted, int pan, float attenuationCb,
                                         juce::int64 sinceVoiceId)
{
    std::array<fluid_voice_t*, 256> voices {};
    fluid_synth_get_voicelist (engine->synth, voices.data(), (int) voices.size(), -1);
    for (auto* voice : voices)
    {
        if (voice == nullptr || (juce::int64) fluid_voice_get_id (voice) <= sinceVoiceId)
            continue;
        const int voiceChannel = fluid_voice_get_channel (voice);
        if (voiceChannel != channel && voiceChannel != channel + layerChannelOffset)
            continue;
        if (pan != 0)   // tenths of a percent, -500 left to 500 right, on top of the SoundFont's
        {
            fluid_voice_gen_incr (voice, GEN_PAN, (float) pan);
            fluid_voice_update_param (voice, GEN_PAN);
        }
        if (attenuationCb > 0.0f)
        {
            fluid_voice_gen_incr (voice, GEN_ATTENUATION, attenuationCb);
            fluid_voice_update_param (voice, GEN_ATTENUATION);
        }
        if (detuneCents != 0)
        {
            fluid_voice_gen_incr (voice, GEN_FINETUNE, (float) detuneCents);
            fluid_voice_update_param (voice, GEN_FINETUNE);
        }
        if (palmMuted)   // the hand on the strings: about 1.3 kHz, a little resonance
        {
            fluid_voice_gen_set (voice, GEN_FILTERFC, 8700.0f);
            fluid_voice_update_param (voice, GEN_FILTERFC);
        }
    }
}

// Retunes voices by what their pitch should be now: the engine's glide for their note, plus the
// wheels' offset for the channel (and Mono Bend's for the lowest note). Only voices newer than
// `sinceVoiceId` when it's given (a note just started).
SF2PlayerProcessor::AppliedPitch* SF2PlayerProcessor::appliedPitchFor (const void* voice, juce::int64 id)
{
    const auto size = appliedPitch.size();
    // Fibonacci hashing: the pool's evenly spaced addresses spread over the table (no clusters),
    // its top bits picking the slot.
    static_assert (std::tuple_size_v<decltype (appliedPitch)> == 1024, "the shift below takes the top 10 bits");
    const auto start = (size_t) (((juce::uint64) reinterpret_cast<std::uintptr_t> (voice) * 11400714819323198485ull) >> 54);
    for (size_t k = 0; k < size; ++k)
    {
        auto& slot = appliedPitch[(start + k) % size];
        if (slot.voice == voice || slot.voice == nullptr)
        {
            if (slot.voice == nullptr || slot.id != id) slot = { voice, id, 0.0f };   // a new note on this voice
            return &slot;
        }
    }
    return nullptr;
}

void SF2PlayerProcessor::applyPitch (juce::int64 now, juce::int64 sinceVoiceId)
{
    std::array<fluid_voice_t*, 256> voices {};
    fluid_synth_get_voicelist (engine->synth, voices.data(), (int) voices.size(), -1);
    const int transpose = transposeFor (0);
    std::array<int, 16> lowest;
    for (int c = 0; c < numMidiChannels; ++c) lowest[(size_t) c] = monoCents[(size_t) c] != 0.0f ? guitar.lowestHeld (c) : -1;
    for (auto* voice : voices)
    {
        if (voice == nullptr) continue;
        const auto id = (juce::int64) fluid_voice_get_id (voice);
        if (id <= sinceVoiceId) continue;
        const int channel = fluid_voice_get_channel (voice) % 16;   // the instrument's, the layer's and the Double's
        const int note = fluid_voice_get_key (voice) - transpose;
        float cents = guitar.pitchCents (channel, note, now) + channelCents[(size_t) channel];
        if (note == lowest[(size_t) channel]) cents += monoCents[(size_t) channel];
        auto* slot = appliedPitchFor (voice, id);
        if (slot == nullptr) continue;   // more voices in the pool than slots: can't happen (256 at most)
        auto& applied = *slot;
        if (std::abs (cents - applied.cents) < 0.5f) continue;
        fluid_voice_gen_incr (voice, GEN_FINETUNE, cents - applied.cents);
        fluid_voice_update_param (voice, GEN_FINETUNE);
        applied.cents = cents;
    }
}

// The wheels, aftertouch and the pedal when the Guitar panel gives them a job (Pitch, Slide, Mono
// Bend; the pedal's articulation in Solo). True when taken: the synth doesn't get it.
bool SF2PlayerProcessor::guitarController (const juce::MidiMessage& m, const GuitarEngine::Settings& s, int channel, juce::int64 at)
{
    if (m.isSustainPedalOn() || m.isSustainPedalOff())
    {
        if (s.mode != GuitarEngine::solo || s.pedalFx == GuitarEngine::artOff) return false;
        guitar.pedal (channel, m.isSustainPedalOn(), at);
        return true;
    }
    const int pb = blockPb, mw = blockMw, at2 = blockAt;   // read once a block
    bool taken = false;
    noteWheelPosition (m);   // kept whatever the wheel's job
    if (m.isPitchWheel() && pb != 0)
        taken = true;
    else if (m.isController() && m.getControllerNumber() == 1 && mw != 0)
        taken = true;
    else if (m.isChannelPressure() && at2 != 0)
    {
        if (at2 == 2)   // Modulation: the pressure plays the mod wheel
        {
            handleMidi (juce::MidiMessage::controllerEvent (channel + 1, 1, m.getChannelPressureValue()));
            return true;
        }
        taken = true;
    }
    return taken;   // the offsets follow at the block's refreshWheelOffsets
}

// Where each wheel is, whatever its job (Direct, the guitar's, the synth's): a job taken or given
// back starts from there.
void SF2PlayerProcessor::noteWheelPosition (const juce::MidiMessage& m)
{
    const int channel = m.getChannel() - 1;
    if (! juce::isPositiveAndBelow (channel, 16)) return;
    if (m.isPitchWheel()) wheelBend[(size_t) channel] = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
    else if (m.isController() && m.getControllerNumber() == 1) wheelMod[(size_t) channel] = (float) m.getControllerValue() / 127.0f;
    else if (m.isChannelPressure()) wheelPressure[(size_t) channel] = (float) m.getChannelPressureValue() / 127.0f;
    else if (m.isResetAllControllers()) { wheelBend[(size_t) channel] = wheelMod[(size_t) channel] = wheelPressure[(size_t) channel] = 0.0f; }
}

// The wheels' offsets from their last positions and today's assignments, every channel: so an
// assignment set back to its default (or a cleared wheel) leaves no stale detune behind.
void SF2PlayerProcessor::refreshWheelOffsets()
{
    auto value = [this] (const char* id) { return juce::roundToInt (parameters.getRawParameterValue (id)->load()); };
    const int pb = blockPb, mw = blockMw, at2 = blockAt;   // as read once this block (guitarController went by them)
    const int pbRange = value ("pbRange"), mwRange = value ("mwRange"), atRange = value ("atRange");
    // A wheel given to the guitar: the synth's own bend, modulation or pressure from it goes back
    // to rest, so it doesn't count twice. Given back while the guitar plays, the synth takes it at
    // its next move; when the guitar stops, flushGuitar hands every wheel back where it is.
    if (pb != 0 && lastPbAssign == 0)
        for (int channel = 0; channel < 16; ++channel) handleMidi (juce::MidiMessage::pitchWheel (channel + 1, 8192));
    if (mw != 0 && lastMwAssign == 0)
        for (int channel = 0; channel < 16; ++channel) handleMidi (juce::MidiMessage::controllerEvent (channel + 1, 1, 0));
    if (lastAtAssign == 2 && at2 != 2)   // pressure no longer drives CC1: the mod wheel's own (or rest, if it has a job)
        for (int channel = 0; channel < 16; ++channel)
            handleMidi (juce::MidiMessage::controllerEvent (channel + 1, 1, mw != 0 ? 0 : juce::roundToInt (wheelMod[(size_t) channel] * 127.0f)));
    if (at2 != 0 && lastAtAssign == 0)
        for (int channel = 0; channel < 16; ++channel) handleMidi (juce::MidiMessage::channelPressureChange (channel + 1, 0));
    lastAtAssign = at2;
    lastPbAssign = pb;
    lastMwAssign = mw;
    for (int channel = 0; channel < 16; ++channel)
    {
        // Pitch smooth, Slide by semitones (along the frets), Mono Bend on the lowest note.
        float channelOffset = 0.0f, monoOffset = 0.0f;
        auto add = [&] (int kind, float amount, int range)   // kind: 1 pitch, 2 slide, 3 mono bend
        {
            const float cents = amount * 100.0f * (float) range;
            if (kind == 1) channelOffset += cents;
            else if (kind == 2) channelOffset += 100.0f * std::round (cents / 100.0f);
            else if (kind == 3) monoOffset += cents;
        };
        if (pb != 0) add (pb == 1 ? 2 : 3, wheelBend[(size_t) channel], pbRange);
        if (mw != 0) add (mw, wheelMod[(size_t) channel], mwRange);
        if (at2 == 1) add (1, wheelPressure[(size_t) channel], atRange);
        channelCents[(size_t) channel] = channelOffset;
        monoCents[(size_t) channel] = monoOffset;
    }
}

void SF2PlayerProcessor::clearWheels()
{
    channelCents.fill (0.0f);   // the wheels stay where they are (Reset All Controllers rests them)
    monoCents.fill (0.0f);
    appliedPitch.fill ({});
    lastPbAssign = lastMwAssign = lastAtAssign = 0;   // a wheel with a job re-centres the (new) synth's own
}

// Sixteenth-note steps: on the host's grid while it plays (so a pattern lines up with the clip),
// else from the moment the chord was taken, at the host's tempo (120 without one).
void SF2PlayerProcessor::runPattern (const GuitarEngine::Settings& s, const juce::Optional<juce::AudioPlayHead::PositionInfo>& position,
                                     juce::int64 blockStart, int numSamples)
{
    double bpm = 120.0;
    std::optional<double> ppq;
    if (position)
        {
            if (const auto tempo = position->getBpm()) bpm = juce::jlimit (20.0, 400.0, *tempo);
            if (const auto beat = position->getPpqPosition(); beat && position->getIsPlaying()) ppq = *beat;
        }
    const double samplesPerStep = sampleRate * 60.0 / bpm / 4.0;
    const auto& rhythm = GuitarEngine::rhythms[(size_t) s.rhythm];
    const int length = (int) std::strlen (rhythm.steps);
    for (int channel = 0; channel < numMidiChannels; ++channel)
    {
        const auto since = guitar.heldSince (channel);
        if (since < 0)
            continue;
        // The step at the block's start, as a fraction, and its count from the pattern's start.
        const double startStep = ppq ? *ppq * 4.0 : (double) (blockStart - since) / samplesPerStep;
        for (auto k = (juce::int64) std::ceil (startStep - 1e-9); ; ++k)
        {
            const auto offset = (juce::int64) std::llround ((double (k) - startStep) * samplesPerStep);
            if (offset >= numSamples) break;
            const auto time = blockStart + juce::jmax ((juce::int64) 0, offset);
            if (time < since) continue;   // the grid's steps before the chord was taken
            const char step = rhythm.steps[(size_t) (((k % length) + length) % length)];
            const int velocity = juce::roundToInt (guitar.heldVelocity (channel) * (k % 4 == 0 ? 1.0f : 0.82f));
            switch (step)
            {
                case 'D': guitar.stroke (s, channel, GuitarEngine::strokeDown, velocity, time); break;
                case 'U': guitar.stroke (s, channel, GuitarEngine::strokeUp, velocity, time); break;
                case 'd': guitar.stroke (s, channel, GuitarEngine::mutedDown, velocity, time); break;
                case 'u': guitar.stroke (s, channel, GuitarEngine::mutedUp, velocity, time); break;
                case 'B': guitar.stroke (s, channel, GuitarEngine::bassOnly, velocity, time); break;
                default:
                    if (step >= '1' && step <= '6')
                        guitar.stroke (s, channel, GuitarEngine::pickString, velocity, time, step - '1');
                    break;
            }
        }
    }
}

// Stereo tremolo on the main output: a sine per side, a quarter cycle apart; free in Hz, or
// locked to the host's tempo and position.
void SF2PlayerProcessor::applyTremolo (int numSamples)
{
    const float depth = parameters.getRawParameterValue ("tremoloDepth")->load();
    if (depth <= 0.0f)
        return;
    static constexpr double beatsPerCycle[] = { 0.0, 2.0, 1.0, 0.5, 0.25, 2.0 / 3.0, 1.0 / 3.0 };
    const int sync = juce::jlimit (0, 6, juce::roundToInt (parameters.getRawParameterValue ("tremoloSync")->load()));
    double hz = parameters.getRawParameterValue ("tremoloRate")->load();
    if (sync > 0 && blockPosition)
        if (const auto bpm = blockPosition->getBpm())
        {
            hz = *bpm / 60.0 / beatsPerCycle[sync];
            if (const auto ppq = blockPosition->getPpqPosition(); ppq && blockPosition->getIsPlaying())
                tremoloPhase = juce::MathConstants<double>::twoPi * std::fmod (*ppq / beatsPerCycle[sync], 1.0);
        }
    const double step = juce::MathConstants<double>::twoPi * hz / sampleRate;
    auto* left = work.getWritePointer (0);
    auto* right = work.getWritePointer (1);
    for (int i = 0; i < numSamples; ++i)
    {
        const float l = 0.5f + 0.5f * (float) std::sin (tremoloPhase);
        const float r = 0.5f + 0.5f * (float) std::sin (tremoloPhase + juce::MathConstants<double>::halfPi);
        left[i] *= 1.0f - depth * l;
        right[i] *= 1.0f - depth * r;
        tremoloPhase += step;
    }
    tremoloPhase = std::fmod (tremoloPhase, juce::MathConstants<double>::twoPi);
}

void SF2PlayerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& hostMidi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int numSamples = buffer.getNumSamples();
    const juce::int64 blockStart = sampleClock;   // the guitar engine's timeline
    sampleClock += numSamples;
    // Sysex isn't played, and building a message from it allocates: a block carrying one is
    // read from a copy without it (into a buffer reserved in prepareToPlay, never swapped away)
    // by everything below, the on-screen keyboard included (JUCE's helper builds a message per
    // event).
    if (numSamples <= 0)   // a host flushing parameters: the MIDI waits for a block with samples (the window's notes stay queued)
    {
        playedHere.clear();   // nothing of the window's goes out from this block
        bool sysex = false;   // JUCE's read builds a message per event, and a sysex one would allocate
        for (const auto metadata : hostMidi) sysex = sysex || metadata.numBytes > 3;
        if (! sysex)
            showHostNotes (hostMidi, 1);   // the host's notes still show
        deferMidi (hostMidi);
        hostMidi.clear();
        return;
    }
    blockPosition = {};   // the playhead, asked once a block (the transport, Pattern, the tremolo)
    if (auto* head = getPlayHead())
        blockPosition = head->getPosition();
    bool hasSysex = false;
    for (const auto metadata : hostMidi)
        hasSysex = hasSysex || metadata.numBytes > 3;
    if (hasSysex)
    {
        shortMidi.clear();
        for (const auto metadata : hostMidi)
            if (metadata.numBytes <= 3)
                shortMidi.addEvent (metadata.data, metadata.numBytes, metadata.samplePosition);
    }
    const auto& fromHost = hasSysex ? shortMidi : hostMidi;
    // What's played in the window (its keyboard, the drums' pads, kit and computer keys) is the
    // plugin's MIDI output, so the host can record it ("MIDI From" this track on another one); the
    // host's own notes aren't sent back. Two keyboard states keep them apart: `keyboardState` holds
    // only the window's notes (so a clip letting go of the same key can't swallow the window's own
    // key-up), `hostNotes` only shows the host's on the window's keys and lights. Each is read under
    // its own lock (the window adds and removes listeners under it). The block plays both, the
    // window's after the host's at any one sample, from our own buffer, reserved for far more than
    // a block carries, so nothing allocates here.
    playedHere.clear();
    keyboardState.processNextMidiBuffer (playedHere, 0, numSamples, true);
    showHostNotes (const_cast<juce::MidiBuffer&> (fromHost), numSamples);   // read only (bytes patched and restored)
    blockMidi.clear();
    blockMidi.data.addArray (fromHost.data);   // the bytes as they are: one copy, not an insertion per event
    bool playedNoteOn = false;   // a note played in the window (not a key let go)
    for (const auto metadata : playedHere)
    {
        blockMidi.addEvent (metadata.data, metadata.numBytes, metadata.samplePosition);
        playedNoteOn = playedNoteOn || isNoteOnBytes (metadata.data, metadata.numBytes);
    }
    auto& midi = blockMidi;
    {
        // REC waiting for play starts with the transport; a take with the transport ends with it.
        // REC always arms; a host without a transport (no playhead) records at once instead, decided
        // here at the first block, so a REC clicked before any block ran still waits for play.
        const bool playingNow = blockPosition && blockPosition->getIsPlaying();
        if (blockPosition.hasValue()) hostTransportSeen = true;   // once a position came, a block without one is a gap, not "no transport"
        int expected = takeArmed;
        if ((! blockPosition.hasValue() && ! hostTransportSeen) || wrapperType == wrapperType_Standalone)   // the standalone's playhead never plays
            takeState.compare_exchange_strong (expected, takeFree);
        // A take with the song ends only when the host says it stopped (a block without a position is a gap).
        const bool stoppedNow = blockPosition.hasValue() && ! blockPosition->getIsPlaying();
        expected = takeArmed;
        if (playingNow) takeState.compare_exchange_strong (expected, takeWithHost);
        expected = takeWithHost;
        if (stoppedNow) takeState.compare_exchange_strong (expected, takeEndedByHost);
        expected = takeArmed;   // played without starting the song: a take of its own, at once
        if (! playingNow && playedNoteOn && ! bypassedBlock)   // a note while bypassed is no one's take
            takeState.compare_exchange_strong (expected, takeFree);
    }
    if (const int take = takeState.load(); take == takeIdle || take == takeArmed)
        takeHeld.reset();   // no take running: the next one starts holding nothing
    // REC's stop waits for a block writing its events (counted before it reads the state), so the
    // last notes of a take reach it instead of the next take's leftovers.
    takeWriters.fetch_add (1);
    // The host's timeline as last seen while playing, to carry a take with the song across a block
    // that comes without a position (a gap), so it keeps its place in the bars.
    if (blockPosition && blockPosition->getIsPlaying() && blockPosition->getPpqPosition())
    {
        lastGrid.known = true;
        lastGrid.ppq = *blockPosition->getPpqPosition();
        lastGrid.seconds = takeSeconds;
        lastGrid.bpm = blockPosition->getBpm() ? juce::jlimit (20.0, 400.0, *blockPosition->getBpm()) : 120.0;
        const auto sig = blockPosition->getTimeSignature();
        lastGrid.barQuarters = sig && sig->numerator > 0 && sig->denominator > 0 ? juce::jlimit (0.25, 64.0, 4.0 * sig->numerator / sig->denominator) : 4.0;
        const auto bar = blockPosition->getPpqPositionOfLastBarStart();
        lastGrid.barStart = bar && *bar <= lastGrid.ppq ? *bar : -1.0;
    }
    else if (blockPosition)
        lastGrid.known = false;   // stopped: nothing to carry
    if (const int take = takeState.load(); (take == takeWithHost || take == takeFree) && ! playedHere.isEmpty())
    {
        const double sr = juce::jmax (1.0, getSampleRate());
        double bpm = 120.0, barQuarters = 4.0;
        juce::Optional<double> ppq;
        double barStart = -1.0;
        if (blockPosition)
        {
            if (const auto tempo = blockPosition->getBpm()) bpm = juce::jlimit (20.0, 400.0, *tempo);
            if (const auto sig = blockPosition->getTimeSignature(); sig && sig->numerator > 0 && sig->denominator > 0)
                barQuarters = juce::jlimit (0.25, 64.0, 4.0 * sig->numerator / sig->denominator);
            if (blockPosition->getIsPlaying())
            {
                ppq = blockPosition->getPpqPosition();
                if (const auto bar = blockPosition->getPpqPositionOfLastBarStart(); bar && ppq && *bar <= *ppq) barStart = *bar;
            }
        }
        if (! ppq && take == takeWithHost && lastGrid.known && ! blockPosition)   // a gap mid-song: carried from the last block's timeline
        {
            bpm = lastGrid.bpm;
            barQuarters = lastGrid.barQuarters;
            ppq = lastGrid.ppq + (takeSeconds - lastGrid.seconds) * bpm / 60.0;
            barStart = lastGrid.barStart;
        }
        takeBpm.store (bpm, std::memory_order_relaxed);
        takeBarQuarters.store (barQuarters, std::memory_order_relaxed);
        for (const auto metadata : playedHere)
        {
            if (metadata.numBytes > 3) continue;
            // While bypassed nothing new goes in, but a key let go still ends a note the take holds.
            const int key = metadata.numBytes == 3 ? (metadata.data[0] & 0x0f) * 128 + (metadata.data[1] & 0x7f) : -1;
            const bool on = isNoteOnBytes (metadata.data, metadata.numBytes), off = isNoteOffBytes (metadata.data, metadata.numBytes);
            if (bypassedBlock && ! (off && key >= 0 && takeHeld.test ((size_t) key))) continue;
            if (on && key >= 0) takeHeld.set ((size_t) key);
            if (off && key >= 0) takeHeld.reset ((size_t) key);
            TakeEvent event;
            event.seconds = takeSeconds + metadata.samplePosition / sr;   // a clock no audio restart sets back
            event.onGrid = ppq.hasValue();
            event.barStart = barStart;
            event.bpm = bpm;
            event.barQuarters = barQuarters;
            event.ppq = ppq ? *ppq + metadata.samplePosition / sr * bpm / 60.0 : 0.0;
            event.size = metadata.numBytes;
            std::copy (metadata.data, metadata.data + metadata.numBytes, event.bytes);
            takeFifo.write (1).forEach ([&] (int index) { takeEvents[(size_t) index] = event; });   // full: dropped
        }
    }
    takeWriters.fetch_sub (1);
    takeSeconds += numSamples / juce::jmax (1.0, getSampleRate());
    struct SendPlayed   // at every way out of this block
    {
        juce::MidiBuffer& host; const juce::MidiBuffer& played; bool standalone;
        int room;   // what the host's buffer held this block: room it surely has
        ~SendPlayed()
        {
            // Into the host's own buffer only within the room it surely has: what it held this block,
            // or the 2048 bytes JUCE's AU and VST3 wrappers reserve for it, so nothing allocates here.
            // (A block's window notes are a few; past that they still sound, they just don't go out.)
            host.clear();
            if (standalone) return;   // the standalone sends nothing: its buffer reserves no room to write into
            if (played.data.size() <= juce::jmax (room, 2048))
                host.data.addArray (played.data);
        }
    } sendPlayed { hostMidi, playedHere, wrapperType == wrapperType_Standalone, hostMidi.data.size() };

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
            for (const auto metadata : midi)   // the wheels still move
                if (metadata.numBytes <= 3) noteWheelPosition (juce::MidiMessage (metadata.data, metadata.numBytes));
            clearDeferredMidi();
            if (lateCount > 0)   // Humanize's waiting notes go with the engine (none plays when a file comes back)
            {
                clearLateNotes();
            }
            previewHere.clear();   // previews clicked meanwhile are dropped, not saved up for when a file comes back
            previewNotes.processNextMidiBuffer (previewHere, 0, numSamples, false);
            return;
        }
        if (const int generation = pinGeneration.load(); generation != seenGeneration)
        {
            seenGeneration = generation;   // a new engine or preset: forget the last requests
            lastPinRequest.fill (-1);
        }
        applyEffectSettings (false);
        sendOutputs = appliedEffects.sendOutputs;


        // The transport, every block: Ableton stopping lets a chord Hold kept go (its note-offs
        // don't end it, and no all-notes-off comes). Played with it stopped, a pattern runs on its own.
        bool transportStopped = false;
        if (blockPosition)
        {
            transportStopped = hostWasPlaying && ! blockPosition->getIsPlaying();
            hostWasPlaying = blockPosition->getIsPlaying();
        }

        // The Guitar panel's notes go through its engine (strummed, voiced, humanized), unless
        // it's in Direct: then the notes play exactly as they come. One reading of the parameters
        // for the block (the editor's guitarPlaying() asks the same).
        const auto guitarSettings = currentGuitarSettings();
        const bool panelShown = guitarPanelActive();
        const bool useGuitar = panelShown && ! guitarSettings.direct;
        applyPlaySettings (false, useGuitar);   // the same reading: Mono and Glide stand aside exactly when the guitar plays
        blockPb = juce::roundToInt (parameters.getRawParameterValue ("pbAssign")->load());   // the wheels' jobs, once a block
        blockMw = juce::roundToInt (parameters.getRawParameterValue ("mwAssign")->load());
        blockAt = juce::roundToInt (parameters.getRawParameterValue ("atAssign")->load());
        if (! useGuitar && guitarEngaged)
            flushGuitar();
        if (! useGuitar)   // asked for the guitar as it stopped playing: dropped, never played late
        {
            auditionRequests.store (0, std::memory_order_relaxed);
            previewRequest.store (0, std::memory_order_relaxed);
        }
        if (useGuitar && ! guitarEngaged)
            forgetKeyWaits();   // Humanize's per-key delays don't outlive the guitar taking over
        if (useGuitar)
            guitarEngaged = true;

        // Drum Humanize for this block: velocity varied by up to ±30 %, hits delayed by up to 20 ms.
        // Only where the DRUM KIT panel shows (never with the Guitar panel, even in Direct), and only
        // on the kit's own notes: not a Split's lower instrument, not in Layer (both instruments play
        // the same notes, so it would reach the second one), not a channel a clip moved.
        if (const int generation = kitGeneration.load (std::memory_order_relaxed); generation != seenKitGeneration)
        {
            seenKitGeneration = generation;
            channelMoved.reset();
        }
        Humanize human;
        {
            const bool kit = ! panelShown && drumsActive();   // (the Guitar panel playing implies it shows)
            human.velocity = kit ? parameters.getRawParameterValue ("drumVelocityHuman")->load() : 0.0f;
            human.maxDelay = kit ? getSampleRate() * 0.020 * parameters.getRawParameterValue ("drumTimingHuman")->load() : 0.0;
            const int layerMode = juce::roundToInt (parameters.getRawParameterValue ("layerMode")->load());
            human.splitBelow = layerMode == 1 ? 128
                             : layerMode == 2 ? juce::roundToInt (parameters.getRawParameterValue ("splitKey")->load()) : -1;
            human.panelShown = panelShown;
        }

        // Events a skipped block kept, first: through the guitar engine when it's in use, else by
        // the same rules as this block's (what waits since before plays first, then they wait in turn).
        if (! useGuitar)
        {
            int start = 0;
            playLateNotes (blockStart + 1, blockStart, start);
        }
        else if (lateCount > 0)   // the guitar took over: Humanize's late notes play now, before anything after them
            playLateNotesNow();
        playDeferredMidi (useGuitar ? &guitarSettings : nullptr, blockStart, human);
        // A preview or chord audition plays too, but it's no one's playing: not sent out, not recorded,
        // not humanized; as it is, at the block's start. Taken from its state only here, with the
        // engine held, so a block skipped for a busy engine leaves it waiting there, not lost.
        previewHere.clear();
        previewNotes.processNextMidiBuffer (previewHere, 0, numSamples, true);
        for (const auto metadata : previewHere)   // as 1.3 did: a preview's note-off is never held back, so a preview always ends
            if (metadata.numBytes <= 3) handleMidi (juce::MidiMessage (metadata.data, metadata.numBytes));

        if (! useGuitar)
        {

            // Render up to each MIDI event, so notes start on the exact sample.
            int position = 0;
            for (const auto metadata : midi)
            {
                const int eventTime = juce::jlimit (0, numSamples, metadata.samplePosition);
                // What waits until this sample or before it plays first (it came in earlier), so what
                // still waits is due strictly later and an event that must follow it can wait for it.
                // An all-notes-off on that very sample is the exception: what it silences doesn't start.
                const bool allOff = isAllOffBytes (metadata.data, metadata.numBytes);
                playLateNotes (blockStart + eventTime + (allOff ? 0 : 1), blockStart, position);
                render (position, eventTime);
                position = eventTime;
                if (metadata.numBytes <= 3)   // sysex isn't played: building its message could allocate
                    playOrWait (juce::MidiMessage (metadata.data, metadata.numBytes), blockStart + eventTime, human);
            }
            playLateNotes (blockStart + numSamples, blockStart, position);
            render (position, numSamples);


        }
        else
        {
            // This block's events: the clip's (notes into the engine, the rest as they are)
            // and the engine's notes that fall due, in time order.
            using Item = GuitarItem;
            auto& items = guitarItems;   // a member: 2048 items are too much for the audio thread's stack
            static constexpr int maxItems = (int) std::tuple_size_v<std::decay_t<decltype (guitarItems)>>;
            int count = 0, order = 0, index = -1, overflowCount = 0;
            for (const auto metadata : midi)
            {
                ++index;
                if (metadata.numBytes > 3)
                    continue;
                const auto message = juce::MidiMessage (metadata.data, metadata.numBytes);
                const int channel = message.getChannel() - 1;
                const auto at = blockStart + juce::jlimit (0, numSamples, metadata.samplePosition);
                const bool onChannel = juce::isPositiveAndBelow (channel, numMidiChannels);
                if (onChannel && guitarController (message, guitarSettings, channel, at))
                    {}   // a wheel, aftertouch or the pedal, taken by the guitar
                else if (onChannel && message.isNoteOn())
                {
                    if (GuitarEngine::inMain (message.getNoteNumber()))   // a played note (not a key switch or Repeat key), as played
                        lastPlayedVelocity = message.getVelocity();
                    playedGuitar = true;
                    guitar.noteOn (guitarSettings, channel, message.getNoteNumber(), message.getVelocity(), at);
                }
                else if (onChannel && message.isNoteOff() && guitar.noteOff (channel, message.getNoteNumber(), at))
                    {}   // the engine ends what it started for this key
                else
                {
                    // A key pressed before the engine was in use ends as it started; all-notes-off
                    // also clears what the engine holds (its chord hand, Pattern's rhythm).
                    if (onChannel && (message.isAllNotesOff() || message.isAllSoundOff()))
                        guitar.allOff (channel, at);
                    if (count >= maxItems)   // past the list (a flood): played after it, at the block's end, in order
                    {
                        if (overflowCount < (int) overflowIndices.size()) overflowIndices[(size_t) overflowCount++] = index;
                        else if (onChannel && message.isNoteOff()) lateNoteOffs[(size_t) channel].set ((size_t) message.getNoteNumber());   // past even that (16k): a note still ends, last; the rest is lost
                        continue;
                    }
                    Item item { juce::jlimit (0, numSamples, metadata.samplePosition), order++, {}, metadata.numBytes, {}, false };
                    std::copy (metadata.data, metadata.data + metadata.numBytes, item.bytes);
                    items[(size_t) count++] = item;
                }
            }
            const auto blockEnd = blockStart + numSamples;
            if (transportStopped) guitar.releaseLatched (blockStart);
            guitar.advance (guitarSettings, blockEnd);   // first: the chord a pattern plays is voiced here
            guitar.tremolo (guitarSettings, blockEnd);
            refreshWheelOffsets();   // an assignment changed since: no stale offset
            // The neck's drawn chord, heard as drawn: strummed once, its own length, never a held
            // chord (Hold and Pattern don't take it).
            if (auditionRequests.exchange (0, std::memory_order_acq_rel) > 0)
            {
                std::array<int, 6> notes {};
                const int n = GuitarEngine::shapeNotes (guitarShapeFrets(), notes.data(), GuitarEngine::tuningOf (guitarTuning()));
                if (n > 0) guitar.audition (guitarSettings, 0, notes.data(), n, 100, blockStart, (juce::int64) (sampleRate * 1.5));
            }
            if (const int preview = previewRequest.exchange (0, std::memory_order_acq_rel); preview > 0)   // the Preview button: one note
            {
                const int note = preview - 1;
                guitar.audition (guitarSettings, 0, &note, 1, 100, blockStart, (juce::int64) (sampleRate * 0.7));
            }
            if (guitarSettings.pattern && guitarSettings.mode != GuitarEngine::solo && guitarSettings.mode != GuitarEngine::harmony)
                runPattern (guitarSettings, blockPosition, blockStart, numSamples);
            GuitarEngine::Event due;
            while (count < maxItems && guitar.pop (blockEnd, due))
                items[(size_t) count++] = { (int) juce::jlimit ((juce::int64) 0, (juce::int64) numSamples - 1, due.time - blockStart),
                                            order++, {}, 0, due, true };
            std::sort (items.begin(), items.begin() + count, [] (const Item& a, const Item& b)
                       { return a.position != b.position ? a.position < b.position : a.order < b.order; });

            int position = 0;
            applyPitch (blockStart, -1);   // slides and bends move on
            for (int i = 0; i < count; ++i)
            {
                const auto& item = items[(size_t) i];
                render (position, item.position);
                position = item.position;
                if (! item.fromGuitar)
                {
                    handleMidi (juce::MidiMessage (item.bytes, item.size));
                    continue;
                }
                const auto& note = item.note;
                const int twinChannel = note.channel + doubleChannelOffset;
                if (note.velocity == 0)
                {
                    if (note.isDouble)
                    {
                        auto& key = twinKey[note.channel][note.note];
                        if (key >= 0) fluid_synth_noteoff (engine->synth, twinChannel, key);
                        key = -1;
                    }
                    else
                        handleMidi (juce::MidiMessage::noteOff (note.channel + 1, note.note));
                    continue;
                }
                // Double: the original a little left, its twin a little right and at Double's level.
                const bool doubled = guitarSettings.doubleMix > 0.0f;
                const int pan = doubled ? (note.isDouble ? doublePan : -doublePan) : 0;
                const float attenuation = note.isDouble ? -200.0f * std::log10 (juce::jmax (0.01f, guitarSettings.doubleMix)) : 0.0f;
                const bool tweak = note.detuneCents != 0 || note.palmMuted || pan != 0 || attenuation > 0.0f;
                const auto before = newestVoiceId();
                if (note.isDouble)
                {
                    const int key = transposeFor (note.note);
                    if (juce::isPositiveAndBelow (key, 128))
                    {
                        fluid_synth_noteon (engine->synth, twinChannel, key, curvedVelocity (note.velocity));
                        twinKey[note.channel][note.note] = (juce::int16) key;   // its off uses this, whatever Transpose does meanwhile
                    }
                }
                else
                    handleMidi (juce::MidiMessage::noteOn (note.channel + 1, note.note, (juce::uint8) note.velocity));
                if (tweak)
                    tweakNewVoices (note.isDouble ? twinChannel : note.channel, note.detuneCents, note.palmMuted, pan, attenuation, before);
                applyPitch (blockStart + item.position, before);   // a slide or bend starts with the note
            }
            // A flood's messages past the list: after the ones before them (so the last value sent
            // stays), each at its own sample when that's still ahead.
            if (overflowCount > 0)
            {
                int i = 0, next = 0;
                for (const auto metadata : midi)
                {
                    if (next >= overflowCount) break;
                    if (i++ != overflowIndices[(size_t) next]) continue;
                    ++next;
                    const int at = juce::jlimit (0, numSamples, metadata.samplePosition);
                    if (at > position) { render (position, at); position = at; }
                    handleMidi (juce::MidiMessage (metadata.data, metadata.numBytes));
                }
            }
            // Note-offs past even the overflow list: after everything (never before their note-on).
            for (int channel = 0; channel < numMidiChannels; ++channel)
                if (auto& offs = lateNoteOffs[(size_t) channel]; offs.any())
                {
                    for (int note = 0; note < 128; ++note)
                        if (offs.test ((size_t) note)) handleMidi (juce::MidiMessage::noteOff (channel + 1, note));
                    offs.reset();
                }
            render (position, numSamples);
        }
        if (const auto& last = guitar.lastPlayed(); last.sequence != guitarView.sequence.load (std::memory_order_relaxed))
        {
            const int shown = juce::jmin (last.count, 6);
            int classes = 0;
            for (int i = 0; i < last.count; ++i) classes |= 1 << (last.notes[(size_t) i] % 12);
            for (int i = 0; i < 6; ++i) guitarView.notes[(size_t) i].store (i < shown ? last.notes[(size_t) i] : -1, std::memory_order_relaxed);
            guitarView.count.store (shown, std::memory_order_relaxed);
            guitarView.pitchClasses.store (classes, std::memory_order_relaxed);
            guitarView.bass.store (last.count > 0 ? last.notes[0] % 12 : -1, std::memory_order_relaxed);
            guitarView.up.store (last.up, std::memory_order_relaxed);
            guitarView.strummed.store (last.strummed, std::memory_order_relaxed);
            guitarView.palm.store (last.palm, std::memory_order_relaxed);
            guitarView.fx.store (last.fx, std::memory_order_relaxed);
            guitarView.position.store (last.position, std::memory_order_relaxed);
            guitarView.sequence.store (last.sequence, std::memory_order_release);
        }
        activeVoices = fluid_synth_get_active_voice_count (engine->synth);
        if (interpolationOverride >= 0)
            fluid_synth_set_interp_method (engine->synth, -1, interpolationOverride), interpolationOverride = -1;
    }

    // Built-in effects: their output joins the main one.
    if (! sendOutputs)
        for (int ch = 2; ch < 6; ++ch)
            work.addFrom (ch % 2, 0, work, ch, 0, numSamples);

    if (guitarPanelActive())
        applyTremolo (numSamples);   // a Guitar panel control: off (depth 0) by default

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
    while (! bypassedBlock && peak > previous && ! outputPeak.compare_exchange_weak (previous, peak)) {}   // bypassed: silent, so the meter rests
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
    saved.setProperty ("drumChoice", drumChoice.load(), nullptr);
    {
        juce::StringArray frets;
        for (int fret : guitarShapeFrets()) frets.add (juce::String (fret));
        saved.setProperty ("guitarShape", frets.joinIntoString (","), nullptr);
    }
    if (auto xml = saved.createXml())
        copyXmlToBinary (*xml, destData);
}

void SF2PlayerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;
    {
        // A saved project: its own settings, never the first-try start. The lock waits for a start
        // being given (its edits then land before this state replaces them) and is let go at once,
        // never held through the load.
        const juce::ScopedLock startLocked (startLock);
        stateRestored = true;
    }

    auto state = juce::ValueTree::fromXml (*xml);
    // The writer first, the parameters only once this project's file is published: a save
    // while another writer holds the mutex (a long load) gets the old project whole. Only a
    // save in the microseconds between the two could still pair them; closing that would make
    // saving wait on stateMutex, which the design rules out.
    const StateWriter writer (*this);
    const auto path = state.getProperty ("sf2Path").toString();
    const juce::File file = juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
    currentPreset = (int) state.getProperty ("preset", 0);
    savedBank = (int) state.getProperty ("presetBank", -1);
    savedNumber = (int) state.getProperty ("presetNumber", -1);
    savedLayerBank = (int) state.getProperty ("layerBank", -1);
    savedLayerNumber = (int) state.getProperty ("layerNumber", -1);
    int restoredDrumChoice = -1;   // stored once the restoring snapshot is out (below)
    {
        // Saved as one number (instrument * 4 + choice); 1.4 builds before it saved the two apart.
        int packed = (int) state.getProperty ("drumChoice", -1);
        if (state.hasProperty ("drumChoiceFor"))
        {
            const int instrument = (int) state.getProperty ("drumChoiceFor", -1);
            packed = juce::isPositiveAndBelow (instrument, 16384 * 128) && juce::isPositiveAndBelow (packed, 3) ? instrument * 4 + packed : -1;
        }
        restoredDrumChoice = packed >= 0 && packed % 4 <= 2 && packed / 4 < 16384 * 128 ? packed : -1;   // banks up to 14 bits
        state.removeProperty ("drumChoice", nullptr);
        state.removeProperty ("drumChoiceFor", nullptr);
    }
    {
        std::array<int, 6> frets { -1, -1, -1, -1, -1, -1 };
        const auto parts = juce::StringArray::fromTokens (state.getProperty ("guitarShape").toString(), ",", "");
        for (int i = 0; i < 6 && i < parts.size(); ++i)
            frets[(size_t) i] = juce::jlimit (-1, 24, parts[i].getIntValue());
        guitarShape = GuitarEngine::packShape (frets);
    }

    // Published at once, before the (possibly long) load: a save meanwhile gets this project's
    // file and instruments along with its parameters, never the previous project's.
    {
        auto restoring = std::make_shared<State>();
        if (file.existsAsFile())
            restoring->file = file;
        else
            restoring->missing = file;   // shown as missing at once, as it'll end
        restoring->preset = currentPreset;
        restoring->layer = -1;   // its index is known only once loaded; saved by bank:program
        restoring->bank = savedBank, restoring->number = savedNumber;
        restoring->layerBank = savedLayerBank, restoring->layerNumber = savedLayerNumber;
        exchangePublished (std::move (restoring));
        // Only now (the window sees no instrument in the restoring snapshot, so it offers none):
        // stored earlier, an open window could read the old instrument and write over it.
        drumChoice.store (restoredDrumChoice);
    }
    parameters.replaceState (state);

    const Restoring restoring (restoringState);
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
