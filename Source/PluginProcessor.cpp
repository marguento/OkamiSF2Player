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
}

SF2PlayerProcessor::~SF2PlayerProcessor() = default;

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

// Called with the engine lock held. FluidSynth recalculates its effects only when told.
void SF2PlayerProcessor::applyEffectSettings (bool force)
{
    const auto s = currentEffectSettings();
    if (! force && s == appliedEffects)
        return;

    auto* synth = engine->synth;
    fluid_synth_reverb_on (synth, -1, s.reverbOn);
    fluid_synth_set_reverb_group_level (synth, -1, s.reverbLevel);
    fluid_synth_set_reverb_group_roomsize (synth, -1, s.reverbRoom);
    fluid_synth_set_reverb_group_damp (synth, -1, s.reverbDamping);
    fluid_synth_set_reverb_group_width (synth, -1, s.reverbWidth);
    fluid_synth_chorus_on (synth, -1, s.chorusOn);
    fluid_synth_set_chorus_group_level (synth, -1, s.chorusLevel);
    fluid_synth_set_chorus_group_speed (synth, -1, s.chorusRate);
    fluid_synth_set_chorus_group_depth (synth, -1, s.chorusDepth);
    fluid_synth_set_chorus_group_nr (synth, -1, s.chorusVoices);
    fluid_synth_set_fx_bypass (synth, s.sendOutputs);
    appliedEffects = s;
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

    fluid_settings_setnum (engine->settings, "synth.sample-rate", juce::jlimit (8000.0, 96000.0, sampleRate));
    fluid_settings_setnum (engine->settings, "synth.gain", synthGain);
    fluid_settings_setint (engine->settings, "synth.polyphony", 256);
    fluid_settings_setint (engine->settings, "synth.threadsafe-api", 0);   // the plugin's own lock serialises calls
    fluid_settings_setint (engine->settings, "synth.midi-channels", 16);
    // Only the samples of the instruments in use are read into memory: a 500 MB
    // collection costs each track only its own instrument's samples.
    fluid_settings_setint (engine->settings, "synth.dynamic-sample-loading", 1);

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
        if (engine != nullptr)
            applyEffectSettings (true);
    }
    // `fresh` now holds the old engine: freed here, outside the lock.
}

bool SF2PlayerProcessor::loadSoundFont (const juce::File& file)
{
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
    presetList = std::move (list);
    applyPreset (*fresh, preset);
    swapEngine (std::move (fresh));

    currentFile = file;
    missingFile = juce::File();
    currentInfo = std::move (info);
    currentPreset = preset;
    sendChangeMessage();
    markProjectChanged();
    return true;
}

void SF2PlayerProcessor::selectPreset (int presetIndex)
{
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

// The file and the instrument aren't parameters: without this the host doesn't know the
// project changed, and closing it without saving loses them (VST3's "dirty" flag).
void SF2PlayerProcessor::markProjectChanged()
{
    if (! restoringState)
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void SF2PlayerProcessor::prepareToPlay (double newSampleRate, int maximumExpectedSamplesPerBlock)
{
    work.setSize (6, juce::jmax (1, maximumExpectedSamplesPerBlock));
    toneFrequency = 0.0f;   // recompute the filters for this rate
    for (auto& channel : toneFilters)
        for (auto& stage : channel)
            stage.z1 = stage.z2 = 0.0f;
    const bool rateChanged = ! juce::exactlyEqual (newSampleRate, sampleRate);
    sampleRate = newSampleRate;

    // FluidSynth takes its sample rate when created: build it again for the new one.
    if (rateChanged && currentFile.existsAsFile())
        if (auto fresh = createEngine (currentFile, sampleRate))
        {
            applyPreset (*fresh, currentPreset);
            swapEngine (std::move (fresh));
        }
}

// ---------------------------------------------------------------------------- audio

void SF2PlayerProcessor::handleMidi (const juce::MidiMessage& m)
{
    const int channel = m.getChannel() - 1;
    if (channel < 0 || channel >= numMidiChannels)
        return;
    auto* synth = engine->synth;

    if (m.isNoteOn())
    {
        ++notesReceived;
        lastVelocity = m.getVelocity();
        fluid_synth_noteon (synth, channel, m.getNoteNumber(), m.getVelocity());
    }
    else if (m.isNoteOff())
        fluid_synth_noteoff (synth, channel, m.getNoteNumber());
    else if (m.isController())   // volume, pan, expression, sustain, mod wheel, CC91/93, bank select...
        fluid_synth_cc (synth, channel, m.getControllerNumber(), m.getControllerValue());
    else if (m.isPitchWheel())
        fluid_synth_pitch_bend (synth, channel, m.getPitchWheelValue());
    else if (m.isProgramChange())
        fluid_synth_program_change (synth, channel, m.getProgramChangeNumber());
    else if (m.isChannelPressure())
        fluid_synth_channel_pressure (synth, channel, m.getChannelPressureValue());
    else if (m.isAftertouch())
        fluid_synth_key_pressure (synth, channel, m.getNoteNumber(), m.getAfterTouchValue());
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
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    if (work.getNumSamples() < numSamples)   // a host sending more than it announced
        work.setSize (6, numSamples, false, false, true);
    work.clear (0, numSamples);
    bool sendOutputs = false;

    {
        // Never wait: while the message thread holds the engine (choosing an instrument
        // reads its samples from disk), this block stays silent instead.
        const juce::SpinLock::ScopedTryLockType lock (engineLock);
        if (! lock.isLocked() || engine == nullptr)
            return;
        applyEffectSettings (false);
        sendOutputs = appliedEffects.sendOutputs;

        // Render up to each MIDI event, so notes start on the exact sample.
        int position = 0;
        for (const auto metadata : midi)
        {
            const int eventTime = juce::jlimit (0, numSamples, metadata.samplePosition);
            render (position, eventTime);
            position = eventTime;
            handleMidi (metadata.getMessage());
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
    auto state = parameters.copyState();
    // A missing file stays in the project, so finding it again (or putting it back) restores it.
    const auto file = currentFile != juce::File() ? currentFile : missingFile;
    state.setProperty ("sf2Path", file.getFullPathName(), nullptr);
    state.setProperty ("preset", currentPreset, nullptr);
    if (juce::isPositiveAndBelow (currentPreset, (int) presetList.size()))
    {
        state.setProperty ("presetBank", presetList[(size_t) currentPreset].bank, nullptr);
        state.setProperty ("presetNumber", presetList[(size_t) currentPreset].number, nullptr);
    }
    else if (savedBank >= 0)
    {
        state.setProperty ("presetBank", savedBank, nullptr);
        state.setProperty ("presetNumber", savedNumber, nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SF2PlayerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    parameters.replaceState (state);

    const auto path = state.getProperty ("sf2Path").toString();
    const juce::File file = juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
    currentPreset = (int) state.getProperty ("preset", 0);
    savedBank = (int) state.getProperty ("presetBank", -1);
    savedNumber = (int) state.getProperty ("presetNumber", -1);

    const juce::ScopedValueSetter<bool> restoring (restoringState, true);
    if (file.existsAsFile())
    {
        currentFile = file;   // so loadSoundFont keeps the saved preset
        if (loadSoundFont (file))
            return;
    }
    if (file != juce::File())
    {
        missingFile = file;   // kept in the project; the editor says it's missing
        swapEngine (nullptr);
        currentFile = juce::File();
        currentInfo = {};
        presetList.clear();
        sendChangeMessage();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SF2PlayerProcessor();
}
