// The plugin's tests: each check renders through the real processor and fails the run
// (exit code 1) when a result is off. `ctest` runs it with Tests/test.sf2; with the HQ
// Orchestral collection in ~/Downloads it also checks that collection and the CPU use.
#include "../Source/PluginProcessor.h"
#include <fluidsynth.h>
#include <juce_events/juce_events.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 256;
int failures = 0, checks = 0;

void expect (bool ok, const juce::String& what, const juce::String& detail = {})
{
    ++checks;
    if (! ok) ++failures;
    std::printf ("%s  %s%s\n", ok ? "ok  " : "FAIL", what.toRawUTF8(), detail.isEmpty() ? "" : ("   (" + detail + ")").toRawUTF8());
}

juce::String db (double v) { return juce::String (v > 0 ? 20 * std::log10 (v) : -200.0, 1) + " dB"; }

struct Player
{
    SF2PlayerProcessor p;
    explicit Player (const juce::File& sf2, int preset = 0, bool effects = false)
    {
        auto layout = p.getBusesLayout();
        p.setBusesLayout (layout);
        p.setRateAndBufferSizeDetails (rate, block);
        p.prepareToPlay (rate, block);
        p.loadSoundFont (sf2);
        p.selectPreset (preset);
        set ("fsReverbOn", effects ? 1.0f : 0.0f);
        set ("fsChorusOn", effects ? 1.0f : 0.0f);
    }
    void set (const char* id, float value)
    {
        auto* parameter = p.parameters.getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }
    // Renders `seconds`, calling `events` with each block's index to add MIDI. Main output, left+right.
    std::vector<float> render (double seconds, const std::function<void (int, juce::MidiBuffer&)>& events)
    {
        juce::AudioBuffer<float> buffer (6, block);
        std::vector<float> out;
        const int blocks = (int) (seconds * rate / block);
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            events (b, midi);
            p.processBlock (buffer, midi);
            for (int i = 0; i < block; ++i)
                out.push_back (0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i)));
        }
        return out;
    }
    std::vector<float> note (int key, int velocity = 100, double hold = 1.0, double total = 2.0)
    {
        const int off = (int) (hold * rate / block);
        return render (total, [&] (int b, juce::MidiBuffer& m)
        {
            if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, key, (juce::uint8) velocity), 0);
            if (b == off) m.addEvent (juce::MidiMessage::noteOff (1, key), 0);
        });
    }
};

int blockOf (double seconds) { return (int) (seconds * rate / block); }

double rms (const std::vector<float>& x, double from, double to)
{
    const size_t a = (size_t) (from * rate), b = juce::jmin (x.size(), (size_t) (to * rate));
    double sum = 0;
    for (size_t i = a; i < b; ++i) sum += (double) x[i] * x[i];
    return b > a ? std::sqrt (sum / (double) (b - a)) : 0.0;
}

// The fundamental between `from` and `to` seconds, by autocorrelation (50 Hz to 2 kHz).
double pitch (const std::vector<float>& x, double from, double to, double sr = rate)
{
    const size_t a = (size_t) (from * sr), n = (size_t) ((to - from) * sr);
    double best = 0; int bestLag = 0;
    for (int lag = (int) (sr / 2000); lag <= (int) (sr / 50); ++lag)
    {
        double sum = 0;
        for (size_t i = 0; i + (size_t) lag < n; ++i) sum += (double) x[a + i] * x[a + i + (size_t) lag];
        sum /= (double) (n - (size_t) lag);
        if (sum > best * 1.02) best = sum, bestLag = lag;   // the first strong peak, not a later multiple
    }
    return bestLag > 0 ? sr / bestLag : 0.0;
}

double cents (double f, double reference) { return 1200.0 * std::log2 (f / reference); }

// The same note through FluidSynth alone, set up as the plugin sets it: what "faithful" means here.
std::vector<float> referenceRender (const juce::File& sf2, int bank, int program, int key, double seconds, double hold)
{
    auto* settings = new_fluid_settings();
    fluid_settings_setnum (settings, "synth.sample-rate", rate);
    fluid_settings_setnum (settings, "synth.gain", 1.0);
    fluid_settings_setint (settings, "synth.polyphony", 256);
    fluid_settings_setint (settings, "synth.reverb.active", 0);
    fluid_settings_setint (settings, "synth.chorus.active", 0);
    auto* synth = new_fluid_synth (settings);
    const int font = fluid_synth_sfload (synth, sf2.getFullPathName().toRawUTF8(), 0);
    fluid_synth_program_select (synth, 0, font, bank, program);
    fluid_synth_set_interp_method (synth, -1, FLUID_INTERP_HIGH);

    std::vector<float> left ((size_t) (seconds * rate)), right (left.size()), out (left.size());
    const size_t off = (size_t) blockOf (hold) * block;
    fluid_synth_noteon (synth, 0, key, 100);
    fluid_synth_write_float (synth, (int) off, left.data(), 0, 1, right.data(), 0, 1);
    fluid_synth_noteoff (synth, 0, key);
    fluid_synth_write_float (synth, (int) (left.size() - off), left.data(), (int) off, 1, right.data(), (int) off, 1);
    for (size_t i = 0; i < out.size(); ++i) out[i] = 0.5f * (left[i] + right[i]);
    delete_fluid_synth (synth);
    delete_fluid_settings (settings);
    return out;
}

void fidelityTests (const juce::File& sf2, int preset, int key, const juce::String& label)
{
    Player player (sf2, preset);
    const auto presets = player.p.presets();
    const auto& info = presets[(size_t) preset];
    const auto plugin = player.note (key, 100, 1.0, 2.0);
    const auto reference = referenceRender (sf2, info.bank, info.number, key, 2.0, 1.0);
    double diff = 0, level = 0;
    for (size_t i = 0; i < juce::jmin (plugin.size(), reference.size()); ++i)
        diff += std::pow ((double) plugin[i] - reference[i], 2.0), level += std::pow ((double) reference[i], 2.0);
    const double ratio = level > 0 ? std::sqrt (diff / level) : 1.0;
    if (std::getenv ("FIDELITY_DEBUG") != nullptr)
        for (double t = 0; t < 2.0; t += 0.1)
        {
            double d = 0, l = 0;
            for (size_t i = (size_t) (t * rate); i < (size_t) ((t + 0.1) * rate) && i < plugin.size(); ++i)
                d += std::pow ((double) plugin[i] - reference[i], 2.0), l += std::pow ((double) reference[i], 2.0);
            std::printf ("   %.1f s: level %s, difference %s\n", t, db (std::sqrt (l / 4800)).toRawUTF8(), db (std::sqrt (d / 4800)).toRawUTF8());
        }
    expect (level > 0 && ratio < 1e-4, label + ": at its defaults the plugin plays exactly what FluidSynth plays",
            "difference " + db (ratio));
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto testFont = juce::File (__FILE__).getSiblingFile ("test.sf2");
    const auto hq = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                        .getChildFile ("Downloads/HQ_Orchestral_Soundfont_Collection_v3.0.sf2");

    // ---- Fidelity: nothing added at the defaults.
    fidelityTests (testFont, 0, 60, "test.sf2 piano");
    if (hq.existsAsFile())
    {
        fidelityTests (hq, 40, 67, "HQ Violin");
        fidelityTests (hq, 0, 48, "HQ Steinway");
    }

    // ---- Playing controls.
    {
        Player plain (testFont), slow (testFont);
        slow.set ("attack", 4.0f);
        const auto a = plain.note (60), b = slow.note (60);
        // The test piano's attack is about 1 ms; x4 makes it about 4.
        expect (rms (b, 0, 0.002) < rms (a, 0, 0.002) * 0.8, "Attack x4 starts the note more slowly",
                db (rms (a, 0, 0.002)) + " -> " + db (rms (b, 0, 0.002)));
    }
    {
        Player plain (testFont), shortRelease (testFont), longRelease (testFont);
        shortRelease.set ("release", 0.25f);
        longRelease.set ("release", 4.0f);
        const auto a = plain.note (60, 100, 0.5, 2.0), b = shortRelease.note (60, 100, 0.5, 2.0), c = longRelease.note (60, 100, 0.5, 2.0);
        expect (rms (b, 0.6, 0.9) < rms (a, 0.6, 0.9), "Release x0.25 fades faster", db (rms (a, 0.6, 0.9)) + " -> " + db (rms (b, 0.6, 0.9)));
        expect (rms (c, 0.6, 0.9) > rms (a, 0.6, 0.9), "Release x4 fades slower", db (rms (a, 0.6, 0.9)) + " -> " + db (rms (c, 0.6, 0.9)));
    }
    {
        Player plain (testFont), curved (testFont);
        curved.set ("velocityCurve", 100.0f);
        const auto a = plain.note (60, 20), b = curved.note (60, 20);
        expect (rms (b, 0.1, 0.5) > rms (a, 0.1, 0.5) * 1.5, "Velocity curve +100 lifts a soft note",
                db (rms (a, 0.1, 0.5)) + " -> " + db (rms (b, 0.1, 0.5)));
    }
    {
        Player plain (testFont), up (testFont), fine (testFont);
        up.set ("transpose", 12.0f);
        fine.set ("fineTune", 50.0f);
        const auto a = plain.note (57), b = up.note (57), c = fine.note (57);   // A3, 220 Hz
        const double f0 = pitch (a, 0.2, 0.4), f1 = pitch (b, 0.2, 0.4), f2 = pitch (c, 0.2, 0.4);
        expect (std::abs (cents (f1, f0) - 1200) < 25, "Transpose +12 plays an octave up", juce::String (f0, 1) + " Hz -> " + juce::String (f1, 1) + " Hz");
        expect (std::abs (cents (f2, f0) - 50) < 15, "Fine tune +50 raises it 50 cents", juce::String (cents (f2, f0), 1) + " cents");

        // Changing the transpose while a note is held must not leave it stuck.
        Player held (testFont);
        const auto out = held.render (3.0, [&] (int b, juce::MidiBuffer& m)
        {
            if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            if (b == blockOf (0.3)) held.set ("transpose", 5.0f);
            if (b == blockOf (0.5)) m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        });
        expect (rms (out, 2.5, 3.0) < 1e-4, "A note held across a transpose change still stops", db (rms (out, 2.5, 3.0)));
    }
    {
        Player wide (testFont);
        wide.set ("bendRange", 12.0f);
        const auto out = wide.render (1.0, [] (int b, juce::MidiBuffer& m)
        {
            if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
            if (b == blockOf (0.4)) m.addEvent (juce::MidiMessage::pitchWheel (1, 16383), 0);
        });
        const double before = pitch (out, 0.15, 0.35), after = pitch (out, 0.6, 0.8);
        expect (std::abs (cents (after, before) - 1200) < 30, "Bend range 12: full pitch bend is an octave", juce::String (cents (after, before), 0) + " cents");
    }
    {
        Player poly (testFont), mono (testFont);
        mono.set ("playMode", 1.0f);
        for (auto* pl : { &poly, &mono }) pl->set ("release", 0.25f);   // the notes mono let go fade out first
        auto chord = [] (Player& pl)
        {
            pl.render (1.5, [] (int b, juce::MidiBuffer& m)
            {
                if (b == 0)
                    for (int k : { 60, 64, 67 }) m.addEvent (juce::MidiMessage::noteOn (1, k, (juce::uint8) 100), 0);
            });
            return pl.p.activeVoices.load();
        };
        const int polyVoices = chord (poly), monoVoices = chord (mono);
        expect (monoVoices < polyVoices && monoVoices > 0, "Mono plays one note of a chord", juce::String (polyVoices) + " voices -> " + juce::String (monoVoices));
    }
    {
        Player legato (testFont);
        legato.set ("playMode", 2.0f);
        legato.set ("glide", 400.0f);
        const auto out = legato.render (1.2, [] (int b, juce::MidiBuffer& m)
        {
            if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
            if (b == blockOf (0.5)) m.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 0);   // overlapping: legato
            if (b == blockOf (0.5) + 1) m.addEvent (juce::MidiMessage::noteOff (1, 57), 0);
        });
        const double start = pitch (out, 0.3, 0.45), middle = pitch (out, 0.62, 0.7), end = pitch (out, 1.05, 1.15);
        const double c = cents (middle, start);
        expect (c > 150 && c < 1100 && std::abs (cents (end, start) - 1200) < 40, "Legato with glide slides to the next note",
                juce::String (cents (middle, start), 0) + " cents halfway, " + juce::String (cents (end, start), 0) + " at the end");
    }
    {
        Player plain (testFont), wheel (testFont);
        const auto a = plain.note (57, 100, 1.5, 1.5);
        const auto b = wheel.render (1.5, [] (int bl, juce::MidiBuffer& m)
        {
            if (bl == 0) { m.addEvent (juce::MidiMessage::controllerEvent (1, 1, 127), 0); m.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0); }
        });
        double spreadA = 0, spreadB = 0, lo = 1e9, hi = 0;
        for (double t = 0.3; t < 1.3; t += 0.05) { const double f = pitch (b, t, t + 0.05); lo = juce::jmin (lo, f); hi = juce::jmax (hi, f); }
        spreadB = cents (hi, lo);
        lo = 1e9; hi = 0;
        for (double t = 0.3; t < 1.3; t += 0.05) { const double f = pitch (a, t, t + 0.05); lo = juce::jmin (lo, f); hi = juce::jmax (hi, f); }
        spreadA = cents (hi, lo);
        expect (spreadB > spreadA + 10, "Mod wheel adds vibrato (the SoundFont default modulator)",
                juce::String (spreadA, 0) + " -> " + juce::String (spreadB, 0) + " cents of pitch swing");
    }

    // ---- Sample rates: the same A3 (220 Hz) at 44.1, 96 and 192 kHz, with the reverb and chorus on.
    for (const double sr : { 44100.0, 96000.0, 192000.0 })
    {
        SF2PlayerProcessor p;
        p.setRateAndBufferSizeDetails (sr, block);
        p.prepareToPlay (sr, block);
        p.loadSoundFont (testFont);
        juce::AudioBuffer<float> buffer (6, block);
        std::vector<float> out;
        for (int b = 0; b < (int) (1.5 * sr / block); ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
            if (b == (int) (0.5 * sr / block)) midi.addEvent (juce::MidiMessage::noteOff (1, 57), 0);
            p.processBlock (buffer, midi);
            for (int i = 0; i < block; ++i) out.push_back (buffer.getSample (0, i));
        }
        const double f = pitch (out, 0.2, 0.4, sr);
        double tail = 0;
        for (size_t i = (size_t) (0.9 * sr); i < (size_t) (1.2 * sr); ++i) tail = juce::jmax (tail, (double) std::abs (out[i]));
        expect (std::abs (cents (f, 220.0)) < 30 && tail > 1e-6, "At " + juce::String (sr / 1000, 1) + " kHz: in tune, with a reverb tail",
                juce::String (f, 1) + " Hz, tail " + db (tail));
    }

    // ---- Layer and split (the test font has one instrument: it layers with itself).
    {
        Player single (testFont), layered (testFont), quieter (testFont);
        layered.set ("layerMode", 1.0f);
        quieter.set ("layerMode", 1.0f);
        quieter.set ("layerLevel", -6.0f);
        const double one = rms (single.note (60), 0.1, 0.5), two = rms (layered.note (60), 0.1, 0.5), lower = rms (quieter.note (60), 0.1, 0.5);
        expect (std::abs (20 * std::log10 (two / one) - 6.02) < 0.3, "Layer: the second instrument plays every note too (same one: +6 dB)",
                juce::String (20 * std::log10 (two / one), 2) + " dB");
        // The layer 6 dB down: one + half of one = +3.5 dB over the single instrument.
        expect (std::abs (20 * std::log10 (lower / one) - 3.52) < 0.3, "Layer level -6 dB lowers the second instrument by 6 dB",
                juce::String (20 * std::log10 (lower / one), 2) + " dB over one");

        Player split (testFont);
        split.set ("layerMode", 2.0f);
        split.set ("splitKey", 60.0f);
        split.render (0.3, [] (int b, juce::MidiBuffer& m) { if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0); });
        const int below = split.p.activeVoices.load();
        Player above (testFont);
        above.set ("layerMode", 2.0f);
        above.set ("splitKey", 60.0f);
        above.render (0.3, [] (int b, juce::MidiBuffer& m) { if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100), 0); });
        const int high = above.p.activeVoices.load();
        Player single2 (testFont);
        single2.render (0.3, [] (int b, juce::MidiBuffer& m) { if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100), 0); });
        expect (below == single2.p.activeVoices.load() && high == below, "Split: each note plays on one side only",
                juce::String (below) + " and " + juce::String (high) + " voices");

        // Switching the layer off while a note is held still stops it.
        Player held (testFont);
        held.set ("layerMode", 1.0f);
        const auto out = held.render (3.0, [&] (int b, juce::MidiBuffer& m)
        {
            if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            if (b == blockOf (0.2)) held.set ("layerMode", 0.0f);
            if (b == blockOf (0.4)) m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        });
        expect (rms (out, 2.5, 3.0) < 1e-4, "A layered note still stops after the layer is switched off", db (rms (out, 2.5, 3.0)));
    }

    // ---- Tone, outputs and saved state.
    {
        Player plain (testFont), dark (testFont);
        dark.set ("tone", 2000.0f);
        auto highs = [] (const std::vector<float>& x)
        {
            double sum = 0; for (size_t i = 1; i < x.size(); ++i) sum += std::pow ((double) x[i] - x[i - 1], 2.0);
            return sum;
        };
        const auto a = plain.note (72), b = dark.note (72);
        expect (highs (b) < highs (a) * 0.7, "Tone at 2 kHz removes highs", juce::String (10 * std::log10 (highs (b) / highs (a)), 1) + " dB");
    }
    if (hq.existsAsFile())
    {
        Player sends (hq, 0, true);
        sends.set ("sendOutputs", 1.0f);
        juce::AudioBuffer<float> buffer (6, block);
        double reverb = 0;
        for (int b = 0; b < 100; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            sends.p.processBlock (buffer, midi);
            reverb = juce::jmax (reverb, (double) buffer.getRMSLevel (2, 0, block));
        }
        expect (reverb > 0, "Send outputs: the Steinway's 1 % reverb send leaves on the Reverb output", db (reverb));

        const auto presets = sends.p.presets();
        int withReverb = 0, withChorus = 0;
        for (const auto& pr : presets) withReverb += pr.sends.reverbMax > 0, withChorus += pr.sends.chorusMax > 0;
        expect (presets.size() == 195 && withReverb == 125 && withChorus == 47, "HQ: 195 instruments, 125 with reverb, 47 with chorus",
                juce::String ((int) presets.size()) + ", " + juce::String (withReverb) + ", " + juce::String (withChorus));
    }
    {
        juce::MemoryBlock state;
        {
            Player saved (testFont);
            saved.set ("transpose", -3.0f);
            saved.set ("tone", 8000.0f);
            saved.p.getStateInformation (state);
        }
        SF2PlayerProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());
        expect (restored.soundFontFile() == testFont && juce::roundToInt (restored.parameters.getRawParameterValue ("transpose")->load()) == -3
                    && juce::approximatelyEqual (restored.parameters.getRawParameterValue ("tone")->load(), 8000.0f),
                "The project restores the file and the knobs");

        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        xml->setAttribute ("sf2Path", "/nowhere/Gone.sf2");
        juce::MemoryBlock moved, again;
        juce::AudioProcessor::copyXmlToBinary (*xml, moved);
        SF2PlayerProcessor missing;
        missing.setStateInformation (moved.getData(), (int) moved.getSize());
        missing.getStateInformation (again);
        auto back = juce::AudioProcessor::getXmlFromBinary (again.getData(), (int) again.getSize());
        expect (missing.missingSoundFont().getFileName() == "Gone.sf2" && back->getStringAttribute ("sf2Path") == "/nowhere/Gone.sf2",
                "A missing file stays named in the project");
    }

    if (hq.existsAsFile())
    {
        juce::MemoryBlock state;
        {
            Player saved (hq, 40);
            saved.p.selectLayerPreset (48);
            saved.set ("layerMode", 1.0f);
            saved.p.getStateInformation (state);
        }
        SF2PlayerProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());
        expect (restored.selectedPreset() == 40 && restored.selectedLayerPreset() == 48, "The project restores the instrument and the layer's",
                juce::String (restored.selectedPreset()) + ", " + juce::String (restored.selectedLayerPreset()));
    }
    if (hq.existsAsFile())
    {
        // Violin (keys 55-96) over Strings below C4: a low G2 can only sound through the strings.
        auto play = [&] (int mode, int key)
        {
            Player pl (hq, 40);
            pl.p.selectLayerPreset (48);
            pl.set ("layerMode", (float) mode);
            pl.set ("splitKey", 60.0f);
            return rms (pl.note (key, 100, 0.5, 0.5), 0.1, 0.4);
        };
        expect (play (0, 43) < 1e-6 && play (2, 43) > 1e-3, "Split: notes below the split play the second instrument",
                "G2 alone " + db (play (0, 43)) + ", split " + db (play (2, 43)));
    }

    // ---- CPU: 64 string notes (256 voices) at 48 kHz in 128-sample blocks.
    if (hq.existsAsFile())
    {
        SF2PlayerProcessor p;
        const int n = 128;
        p.setRateAndBufferSizeDetails (rate, n);
        p.prepareToPlay (rate, n);
        p.loadSoundFont (hq);
        p.selectPreset (48);
        juce::AudioBuffer<float> buffer (6, n);
        double total = 0;
        const int blocks = (int) (rate * 5 / n);
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b % 375 == 0)
                for (int k = 0; k < 64; ++k) midi.addEvent (juce::MidiMessage::noteOn (1 + k % 16, 36 + (k * 7) % 60, (juce::uint8) 100), 0);
            const auto start = juce::Time::getHighResolutionTicks();
            p.processBlock (buffer, midi);
            total += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);
        }
        const double share = total / blocks / (n / rate);
        expect (share < 0.3, "CPU: 64 string notes take under 30 % of the time available", juce::String (share * 100, 1) + " %");
    }

    std::printf ("\n%d of %d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
