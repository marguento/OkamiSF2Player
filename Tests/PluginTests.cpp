// The plugin's tests: each check renders through the real processor and fails the run
// (exit code 1) when a result is off. `ctest` runs it with Tests/fonts; with the HQ
// Orchestral collection in ~/Downloads it also checks that collection and the CPU use.
#include <set>
#include "../Source/PluginProcessor.h"
#include "../Source/GuitarPresets.h"
#include <set>
#include "../Source/SoundFontInfo.h"
#include "../Source/GuitarNeck.h"
#include <fluidsynth.h>
#include <juce_events/juce_events.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <thread>
#include <sys/sysctl.h>

struct PluginTestsAccess
{
    static juce::SpinLock& lock (SF2PlayerProcessor& p) { return p.engineLock; }
    static std::recursive_mutex& state (SF2PlayerProcessor& p) { return p.stateMutex; }
};

namespace
{
constexpr double rate = 48000.0;
constexpr int testPreset = 2;   // Vintage Dreams Waves: "Simple Square Floot"
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
    explicit Player (const juce::File& sf2, int preset = testPreset, bool effects = false)
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

// The fundamental between `from` and `to` seconds, by autocorrelation (50 Hz to 2 kHz): the
// first peak within 10 % of the highest, so a multiple of the period isn't taken for it.
double pitch (const std::vector<float>& x, double from, double to, double sr = rate)
{
    const size_t a = (size_t) (from * sr), n = (size_t) ((to - from) * sr);
    const int minLag = (int) (sr / 2000), maxLag = (int) (sr / 50);
    std::vector<double> r ((size_t) maxLag + 2, 0.0);
    for (int lag = minLag; lag <= maxLag + 1; ++lag)
    {
        double sum = 0;
        for (size_t i = 0; i + (size_t) lag < n; ++i) sum += (double) x[a + i] * x[a + i + (size_t) lag];
        r[(size_t) lag] = sum / (double) (n - (size_t) lag);
    }
    const double best = *std::max_element (r.begin() + minLag, r.end());
    if (best <= 1e-12)
        return 0.0;   // silence: no pitch
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
        if (r[(size_t) lag] >= 0.9 * best && r[(size_t) lag] >= r[(size_t) lag - 1] && r[(size_t) lag] >= r[(size_t) lag + 1])
        {
            // Parabolic interpolation around the peak for a fractional lag.
            const double l = r[(size_t) lag - 1], c = r[(size_t) lag], h = r[(size_t) lag + 1];
            const double denominator = l - 2 * c + h;
            const double shift = std::abs (denominator) > 1e-12 ? 0.5 * (l - h) / denominator : 0.0;
            return sr / ((double) lag + shift);
        }
    return 0.0;
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
    setenv ("SF2PLAYER_SETTINGS_FOLDER", "SF2 Player (tests)", 1);   // never the user's recent files or favorites

    juce::ScopedJuceInitialiser_GUI gui;
    // Vintage Dreams Waves (Ian Wilson, freeware, redistributed unaltered: Tests/fonts), the font
    // FluidSynth tests with; its "Simple Square Floot" is a plain tone, easy to measure.
    const auto fonts = juce::File (__FILE__).getSiblingFile ("fonts");
    const auto testFont = fonts.getChildFile ("VintageDreamsWaves-v2.sf2");
    const auto hq = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                        .getChildFile ("Downloads/HQ_Orchestral_Soundfont_Collection_v3.0.sf2");

    // ---- Fidelity: nothing added at the defaults.
    fidelityTests (testFont, testPreset, 60, "Vintage Dreams square");
    fidelityTests (testFont, 0, 72, "Vintage Dreams FM bells");
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
        p.selectPreset (testPreset);
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

    // ---- Layer and split. Level checks layer the square with itself (+6 dB when equal); the split
    // check puts FM Bells (two voices a note) under the square (one voice), so the voice count
    // says which side played.
    {
        Player single (testFont), layered (testFont), quieter (testFont);
        layered.p.selectLayerPreset (testPreset);
        quieter.p.selectLayerPreset (testPreset);
        layered.set ("layerMode", 1.0f);
        quieter.set ("layerMode", 1.0f);
        quieter.set ("layerLevel", -6.0f);
        const double one = rms (single.note (60), 0.1, 0.5), two = rms (layered.note (60), 0.1, 0.5), lower = rms (quieter.note (60), 0.1, 0.5);
        expect (std::abs (20 * std::log10 (two / one) - 6.02) < 0.3, "Layer: the second instrument plays every note too (same one: +6 dB)",
                juce::String (20 * std::log10 (two / one), 2) + " dB");
        // The layer 6 dB down: one + half of one = +3.5 dB over the single instrument.
        expect (std::abs (20 * std::log10 (lower / one) - 3.52) < 0.3, "Layer level -6 dB lowers the second instrument by 6 dB",
                juce::String (20 * std::log10 (lower / one), 2) + " dB over one");

        constexpr int bells = 0;   // Vintage Dreams Waves: "FM Bells 1"
        auto voices = [&] (int preset, int layerMode, int key)
        {
            Player pl (testFont, preset);
            pl.p.selectLayerPreset (bells);
            pl.set ("layerMode", (float) layerMode);
            pl.set ("splitKey", 60.0f);
            pl.render (0.3, [key] (int b, juce::MidiBuffer& m) { if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, key, (juce::uint8) 100), 0); });
            return pl.p.activeVoices.load();
        };
        const int squareVoices = voices (testPreset, 0, 72), bellVoices = voices (bells, 0, 48);
        const int below = voices (testPreset, 2, 48), above = voices (testPreset, 2, 72);
        expect (squareVoices != bellVoices && below == bellVoices && above == squareVoices,
                "Split: keys below the split play the second instrument, the rest the first",
                "below " + juce::String (below) + " voices (bells " + juce::String (bellVoices) + "), above " + juce::String (above)
                    + " (square " + juce::String (squareVoices) + ")");

        // Switching the layer off while a note is held still stops it.
        Player held (testFont);
        held.p.selectLayerPreset (bells);
        held.set ("layerMode", 1.0f);
        const auto out = held.render (3.0, [&] (int b, juce::MidiBuffer& m)
        {
            if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            if (b == blockOf (0.2)) held.set ("layerMode", 0.0f);
            if (b == blockOf (0.4)) m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        });
        expect (rms (out, 2.5, 3.0) < 1e-4, "A layered note still stops after the layer is switched off", db (rms (out, 2.5, 3.0)));
    }

    // ---- The audio thread never waits and never loses an event.
    {
        // A block that can't take the engine (an instrument loading) keeps its events: the
        // note-off arriving then still stops the note.
        Player held (testFont);
        const auto out = held.render (2.0, [&] (int b, juce::MidiBuffer& m)
        {
            if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            if (b == blockOf (0.3))
            {
                const juce::SpinLock::ScopedLockType busy (PluginTestsAccess::lock (held.p));   // the message thread is loading
                juce::AudioBuffer<float> buffer (6, block);
                juce::MidiBuffer off;
                off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
                held.p.processBlock (buffer, off);   // skipped: silent, the note-off kept
            }
        });
        // Compared with the same note let go normally, and with one never let go.
        Player normal (testFont), stuck (testFont);
        const auto released = normal.note (60, 100, 0.3, 2.0);
        const auto ringing = stuck.note (60, 100, 3.0, 2.0);
        const double kept = rms (out, 1.5, 2.0), expected = rms (released, 1.5, 2.0), never = rms (ringing, 1.5, 2.0);
        expect (kept < never * 0.1 && std::abs (20 * std::log10 ((kept + 1e-12) / (expected + 1e-12))) < 3,
                "A note-off arriving while an instrument loads still stops the note",
                db (kept) + " (let go normally " + db (expected) + ", never let go " + db (never) + ")");

        // A long load with dense controller traffic: past the reserve, bends collapse to the last
        // one, and the note-off and the bend's return still arrive.
        Player busy (testFont);
        busy.render (0.1, [] (int b, juce::MidiBuffer& m) { if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0); });
        {
            const juce::SpinLock::ScopedLockType loading (PluginTestsAccess::lock (busy.p));
            juce::AudioBuffer<float> buffer (6, block);
            for (int b = 0; b < 40; ++b)   // 40 skipped blocks of 200 bends each: 8,000 events
            {
                juce::MidiBuffer m;
                for (int i = 0; i < 200; ++i) m.addEvent (juce::MidiMessage::pitchWheel (1, 8192 + (i % 2 == 0 ? 4000 : -4000)), i);
                if (b == 39) { m.addEvent (juce::MidiMessage::pitchWheel (1, 8192), 255); }
                busy.p.processBlock (buffer, m);
            }
        }
        const auto centred = busy.render (0.5, [] (int, juce::MidiBuffer&) {});
        Player straight (testFont);
        const auto reference = straight.note (57, 100, 1.0, 0.6);
        expect (std::abs (cents (pitch (centred, 0.2, 0.4), pitch (reference, 0.2, 0.4))) < 5,
                "8,000 events during a load: the last pitch bend (back to centre) still arrives",
                juce::String (cents (pitch (centred, 0.2, 0.4), pitch (reference, 0.2, 0.4)), 1) + " cents off centre");

        // During a load, a volume fade (CC7) keeps its final value, and a bend sent before a
        // Reset All Controllers doesn't come back after it.
        auto loading = [&] (Player& pl, const std::function<void (juce::MidiBuffer&, int)>& fill)
        {
            const juce::SpinLock::ScopedLockType busy (PluginTestsAccess::lock (pl.p));
            juce::AudioBuffer<float> buffer (6, block);
            for (int b = 0; b < 40; ++b)
            {
                juce::MidiBuffer m;
                fill (m, b);
                pl.p.processBlock (buffer, m);
            }
        };
        Player faded (testFont), loud (testFont);
        loading (faded, [] (juce::MidiBuffer& m, int b)
        {
            for (int i = 0; i < 100; ++i) m.addEvent (juce::MidiMessage::controllerEvent (1, 7, b == 39 && i == 99 ? 0 : 100 - i % 50), i);
        });
        const double fadedLevel = rms (faded.note (60, 100, 0.5, 0.5), 0.1, 0.4), loudLevel = rms (loud.note (60, 100, 0.5, 0.5), 0.1, 0.4);
        expect (fadedLevel < loudLevel * 0.01, "4,000 volume changes during a load: the final one (0) still applies",
                db (fadedLevel) + " vs " + db (loudLevel));

        Player bent (testFont);
        loading (bent, [] (juce::MidiBuffer& m, int b)
        {
            for (int i = 0; i < 100; ++i) m.addEvent (juce::MidiMessage::controllerEvent (1, 1, i), i);   // fill past the reserve
            if (b == 38) m.addEvent (juce::MidiMessage::pitchWheel (1, 12000), 200);
            if (b == 39) m.addEvent (juce::MidiMessage::controllerEvent (1, 121, 0), 200);
        });
        const auto afterReset = bent.note (57, 100, 0.6, 0.6);
        Player plainA (testFont);
        const auto plainNote = plainA.note (57, 100, 0.6, 0.6);
        expect (std::abs (cents (pitch (afterReset, 0.2, 0.4), pitch (plainNote, 0.2, 0.4))) < 5,
                "A bend before a Reset All Controllers, both during a load, doesn't come back",
                juce::String (cents (pitch (afterReset, 0.2, 0.4), pitch (plainNote, 0.2, 0.4)), 1) + " cents");

        // A MIDI program change takes effect at once: a note sent with it plays the new instrument.
        Player switching (testFont);
        switching.render (0.2, [] (int b, juce::MidiBuffer& m)
        {
            if (b == 0)
            {
                m.addEvent (juce::MidiMessage::programChange (1, 0), 0);   // FM Bells: two voices a note
                m.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100), 0);
            }
        });
        const int firstSwitch = switching.p.activeVoices.load();
        switching.p.runMessageThreadTasks();   // pins FM Bells: switching back won't read it from disk
        auto playAfter = [&] (int program)
        {
            // All Sound Off a block before: in the same instant as a note, FluidSynth itself keeps
            // the old voices (checked against bare FluidSynth).
            switching.render (0.2, [program] (int b, juce::MidiBuffer& m)
            {
                if (b == 0)
                    m.addEvent (juce::MidiMessage::allSoundOff (1), 0);
                if (b == 1)
                {
                    m.addEvent (juce::MidiMessage::programChange (1, program), 0);
                    m.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100), 0);
                }
            });
            return switching.p.activeVoices.load();
        };
        const int back = playAfter (testPreset), again = playAfter (0);
        expect (firstSwitch == 2 && back == 1 && again == 2, "A MIDI program change applies to the note sent with it, back and forth",
                juce::String (firstSwitch) + ", " + juce::String (back) + ", " + juce::String (again) + " voices");

        // Reset All Controllers (CC121) clears FluidSynth's generator offsets: the plugin puts its own back.
        Player reset (testFont), tuned (testFont);
        for (auto* pl : { &reset, &tuned }) pl->set ("fineTune", 50.0f);
        const auto a = tuned.note (57);
        const auto b = reset.render (2.0, [] (int bl, juce::MidiBuffer& m)
        {
            if (bl == 0) { m.addEvent (juce::MidiMessage::controllerEvent (1, 121, 0), 0); m.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0); }
        });
        expect (std::abs (cents (pitch (b, 0.2, 0.4), pitch (a, 0.2, 0.4))) < 3, "Fine tune survives a clip's Reset All Controllers",
                juce::String (cents (pitch (b, 0.2, 0.4), pitch (a, 0.2, 0.4)), 1) + " cents off");
    }

    // ---- Saving never waits: getStateInformation while another thread holds the state (a
    // SoundFont loading) returns at once, with the file and instrument.
    {
        Player saving (testFont);
        std::atomic<bool> holding { false }, release { false };
        std::thread loader ([&]
        {
            const std::lock_guard<std::recursive_mutex> busy (PluginTestsAccess::state (saving.p));
            holding = true;
            while (! release) std::this_thread::sleep_for (std::chrono::milliseconds (1));
        });
        while (! holding) std::this_thread::sleep_for (std::chrono::milliseconds (1));
        juce::MemoryBlock block;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        saving.p.getStateInformation (block);
        const double ms = juce::Time::getMillisecondCounterHiRes() - start;
        release = true;
        loader.join();
        auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
        expect (ms < 50 && xml != nullptr && xml->getStringAttribute ("sf2Path") == testFont.getFullPathName()
                    && xml->getIntAttribute ("presetNumber") == testPreset,
                "Saving the project doesn't wait for a SoundFont loading", juce::String (ms, 1) + " ms");
    }

    // ---- A file that goes missing during a rate change, then comes back.
    {
        const auto copy = juce::File::createTempFile (".sf2");
        testFont.copyFileTo (copy);
        SF2PlayerProcessor p;
        p.setRateAndBufferSizeDetails (rate, block);
        p.prepareToPlay (rate, block);
        p.loadSoundFont (copy);
        p.selectPreset (testPreset);
        const auto moved = copy.getSiblingFile ("moved-away.sf2");
        copy.moveFileTo (moved);
        p.prepareToPlay (44100.0, block);   // can't rebuild: silent, and the message thread says so
        for (int i = 0; i < 10; ++i) p.runMessageThreadTasks();
        const bool reported = p.missingSoundFont() == copy && p.presets().empty();
        moved.moveFileTo (copy);
        for (int i = 0; i < 10; ++i) p.runMessageThreadTasks();
        expect (reported && p.soundFontFile() == copy && p.selectedPreset() == testPreset,
                "A file lost during a rate change is reported, then reloaded with its instrument once back",
                juce::String (reported ? "reported" : "not reported") + ", then preset " + juce::String (p.selectedPreset()));
        copy.deleteFile();

        // A rebuild that failed, then a file chosen before the message thread noticed: the
        // chosen file stays loaded.
        const auto other = juce::File::createTempFile (".sf2");
        testFont.copyFileTo (other);
        SF2PlayerProcessor q;
        q.setRateAndBufferSizeDetails (rate, block);
        q.prepareToPlay (rate, block);
        q.loadSoundFont (copy.existsAsFile() ? copy : testFont);
        const auto gone = juce::File::createTempFile (".sf2");
        testFont.copyFileTo (gone);
        q.loadSoundFont (gone);
        gone.deleteFile();
        q.prepareToPlay (44100.0, block);   // fails: the file is gone
        q.loadSoundFont (other);            // the user picks another before the next check
        for (int i = 0; i < 10; ++i) q.runMessageThreadTasks();
        expect (q.soundFontFile() == other && ! q.presets().empty() && q.missingSoundFont() == juce::File(),
                "A failed rebuild doesn't undo a file chosen right after it", q.soundFontFile().getFileName());
        other.deleteFile();
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

    // ---- SF3: the same font compressed plays the same instruments, close to the uncompressed one.
    {
        const auto sf2 = testFont, sf3 = fonts.getChildFile ("VintageDreamsWaves-v2.sf3");
        if (sf2.existsAsFile() && sf3.existsAsFile())
        {
            Player a (sf2), b (sf3);
            const auto& pa = a.p.presets();
            const auto& pb = b.p.presets();
            bool same = pa.size() == pb.size() && ! pa.empty();
            for (size_t i = 0; same && i < pa.size(); ++i)
                same = pa[i].name == pb[i].name && pa[i].bank == pb[i].bank && pa[i].number == pb[i].number;
            const auto x = a.note (60), y = b.note (60);
            double diff = 0, level = 0;
            for (size_t i = 0; i < juce::jmin (x.size(), y.size()); ++i)
                diff += std::pow ((double) x[i] - y[i], 2.0), level += std::pow ((double) x[i], 2.0);
            const double ratio = level > 0 ? std::sqrt (diff / level) : 1.0;
            // Vorbis is lossy (these single-cycle waves lose about 1 dB of highs): the same pitch and
            // nearly the same level. The decoder itself was checked against ffmpeg's: within one 16-bit
            // step, and every sample exactly as long as in the .sf2.
            const double levelChange = 20 * std::log10 (rms (y, 0.1, 0.9) / rms (x, 0.1, 0.9));
            expect (same && level > 0 && std::abs (cents (pitch (y, 0.2, 0.4), pitch (x, 0.2, 0.4))) < 5 && std::abs (levelChange) < 2,
                    "SF3: the compressed font loads its " + juce::String ((int) pb.size()) + " instruments, in tune and at its level",
                    juce::String (levelChange, 1) + " dB, waveform difference " + db (ratio));
        }
    }

    // ---- Hostile files: whatever an .sf2 holds, loading it ends quickly and never crashes.
    {
        const auto dir = juce::File::createTempFile ("sf2-hostile");
        dir.createDirectory();
        auto write = [&] (const juce::String& name, const juce::MemoryBlock& data)
        {
            auto f = dir.getChildFile (name);
            f.replaceWithData (data.getData(), data.getSize());
            return f;
        };
        juce::MemoryBlock original;
        testFont.loadFileAsData (original);

        juce::MemoryBlock garbage;
        juce::Random random (42);
        for (int i = 0; i < 100000; ++i) garbage.append (&i, 1), static_cast<char*> (garbage.getData())[i] = (char) random.nextInt (256);
        juce::MemoryBlock truncated (original.getData(), original.getSize() / 2);
        juce::MemoryBlock riffOnly ("RIFF\xff\xff\xff\x7fsfbk", 12);

        // A pdta whose presets point at every bag, and whose bags point at 65,535 generators.
        juce::MemoryOutputStream pdta;
        auto chunk = [&] (const char* id, const juce::MemoryBlock& body)
        {
            pdta.write (id, 4); pdta.writeInt ((int) body.getSize()); pdta.write (body.getData(), body.getSize());
        };
        auto records = [] (int count, int size, std::function<void (juce::MemoryOutputStream&, int)> fill)
        {
            juce::MemoryOutputStream out;
            for (int i = 0; i < count; ++i) { const auto start = out.getPosition(); fill (out, i); while (out.getPosition() < start + size) out.writeByte (0); }
            return out.getMemoryBlock();
        };
        chunk ("phdr", records (2001, 38, [] (juce::MemoryOutputStream& o, int i) { o.write ("Evil", 4); o.setPosition (o.getPosition() + 16); o.writeShort ((short) i); o.writeShort (0); o.writeShort (i == 2000 ? (short) 0xffff : 0); }));
        chunk ("pbag", records (3000, 4, [] (juce::MemoryOutputStream& o, int i) { o.writeShort ((short) (i % 2 ? 0xffff : 0)); }));
        chunk ("pgen", records (10, 4, [] (juce::MemoryOutputStream& o, int) { o.writeShort (41); o.writeShort (0); }));
        chunk ("inst", records (3, 22, [] (juce::MemoryOutputStream& o, int i) { o.setPosition (o.getPosition() + 20); o.writeShort (i == 2 ? (short) 0xffff : 0); }));
        chunk ("ibag", records (3000, 4, [] (juce::MemoryOutputStream& o, int i) { o.writeShort ((short) (i % 2 ? 0xffff : 0)); }));
        chunk ("igen", records (10, 4, [] (juce::MemoryOutputStream& o, int) { o.writeShort (53); o.writeShort (0); }));
        juce::MemoryOutputStream evil;
        evil.write ("RIFF", 4); evil.writeInt ((int) pdta.getDataSize() + 16); evil.write ("sfbk", 4);
        evil.write ("LIST", 4); evil.writeInt ((int) pdta.getDataSize() + 4); evil.write ("pdta", 4);
        evil.write (pdta.getData(), pdta.getDataSize());

        bool allRefused = true;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (const auto& [name, data] : { std::pair { juce::String ("garbage.sf2"), garbage }, std::pair { juce::String ("truncated.sf2"), truncated },
                                          std::pair { juce::String ("riff-only.sf2"), riffOnly }, std::pair { juce::String ("evil.sf2"), evil.getMemoryBlock() } })
        {
            const auto file = write (name, data);
            SoundFontInfo::read (file);   // our own reader, which runs on files FluidSynth accepts
            SF2PlayerProcessor p;
            allRefused = allRefused && ! p.loadSoundFont (file);
        }
        const double ms = juce::Time::getMillisecondCounterHiRes() - start;
        expect (allRefused && ms < 3000, "Hostile files (garbage, truncated, a header alone, indices pointing everywhere) are refused quickly",
                juce::String (ms, 0) + " ms");
        dir.deleteRecursively();
    }

    // ---- A key range given only in a global zone applies to the zones without one (SF2 2.04, 9.4).
    {
        juce::MemoryOutputStream pdta;
        auto chunk = [&] (const char* id, std::initializer_list<int> shorts, int recordSize, int records)
        {
            juce::MemoryOutputStream body;
            for (int v : shorts) body.writeShort ((short) v);
            while ((int) body.getDataSize() < recordSize * records) body.writeByte (0);
            pdta.write (id, 4); pdta.writeInt ((int) body.getDataSize()); pdta.write (body.getData(), body.getDataSize());
        };
        auto preset = [] (juce::MemoryOutputStream& o, const char* name, int number, int bag)
        {
            char padded[20] = {}; std::strncpy (padded, name, 19); o.write (padded, 20);
            o.writeShort ((short) number); o.writeShort (0); o.writeShort ((short) bag); o.writeInt (0); o.writeInt (0); o.writeInt (0);
        };
        juce::MemoryOutputStream phdr;
        preset (phdr, "Ranged", 0, 0);
        preset (phdr, "EOP", 0, 2);
        pdta.write ("phdr", 4); pdta.writeInt ((int) phdr.getDataSize()); pdta.write (phdr.getData(), phdr.getDataSize());
        chunk ("pbag", { 0, 0, 1, 0, 2, 0 }, 4, 3);              // zone 0: global, zone 1: the instrument
        chunk ("pgen", { 43, 48 | (72 << 8), 41, 0, 0, 0 }, 4, 3);   // keyRange 48-72 (lo, hi bytes), instrument 0
        juce::MemoryOutputStream inst;
        char instName[20] = "Inst"; inst.write (instName, 20); inst.writeShort (0);
        char eoi[20] = "EOI"; inst.write (eoi, 20); inst.writeShort (1);
        pdta.write ("inst", 4); pdta.writeInt ((int) inst.getDataSize()); pdta.write (inst.getData(), inst.getDataSize());
        chunk ("ibag", { 0, 0, 1, 0 }, 4, 2);
        chunk ("igen", { 53, 0, 0, 0 }, 4, 2);                   // sampleID 0, no range of its own
        juce::MemoryOutputStream sf;
        sf.write ("RIFF", 4); sf.writeInt ((int) pdta.getDataSize() + 16); sf.write ("sfbk", 4);
        sf.write ("LIST", 4); sf.writeInt ((int) pdta.getDataSize() + 4); sf.write ("pdta", 4);
        sf.write (pdta.getData(), pdta.getDataSize());
        const auto file = juce::File::createTempFile ("ranged.sf2");
        file.replaceWithData (sf.getData(), sf.getDataSize());
        const auto info = SoundFontInfo::read (file);
        const auto range = info.keyRanges.find ({ 0, 0 });
        expect (range != info.keyRanges.end() && range->second == std::pair { 48, 72 }, "A global zone's key range applies to its zones",
                range == info.keyRanges.end() ? juce::String ("no range") : juce::String (range->second.first) + "-" + juce::String (range->second.second));
        file.deleteFile();
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
        // Under Rosetta (the Intel half run on Apple silicon) the time is the translation's,
        // not a real Intel Mac's: reported, not judged.
        int translated = 0;
        size_t size = sizeof (translated);
        if (sysctlbyname ("sysctl.proc_translated", &translated, &size, nullptr, 0) == 0 && translated == 1)
            std::printf ("info  CPU under Rosetta: 64 string notes take %.1f %% of the time available (not judged)\n", share * 100);
        else
            expect (share < 0.3, "CPU: 64 string notes take under 30 % of the time available", juce::String (share * 100, 1) + " %");
    }

    // ------------------------------------------------------------------ Guitar panel
    {
        // The neck's chord names: the bass as root first, a slash when it isn't the root.
        auto classes = [] (std::initializer_list<int> pcs) { int m = 0; for (int pc : pcs) m |= 1 << pc; return m; };
        const bool names = GuitarNeck::chordName (classes ({ 0, 4, 7 }), 0) == "C"
                        && GuitarNeck::chordName (classes ({ 9, 0, 4, 7 }), 9) == "Am7"
                        && GuitarNeck::chordName (classes ({ 7, 11, 2 }), 11) == "G/B"
                        && GuitarNeck::chordName (classes ({ 2, 6, 9, 0 }), 2) == "D7"
                        && GuitarNeck::chordName (classes ({ 4 }), 4) == "E"
                        && GuitarNeck::chordName (classes ({ 0, 1, 2 }), 0).isEmpty()
                        // Partial chords, as drawn on a neck: two notes, a seventh without its fifth.
                        && GuitarNeck::chordName (classes ({ 0, 4 }), 0) == "C(no5)"
                        && GuitarNeck::chordName (classes ({ 9, 0 }), 9) == "Am(no5)"
                        && GuitarNeck::chordName (classes ({ 0, 7 }), 0) == "C5"
                        && GuitarNeck::chordName (classes ({ 0, 4, 10 }), 0) == "C7"
                        && GuitarNeck::chordName (classes ({ 0, 2 }), 0) == "C M2"
                        && GuitarNeck::chordName (classes ({ 2, 6, 9, 0, 4 }), 2) == "D9"
                        && GuitarNeck::chordName (classes ({ 7, 11, 2, 5 }), 5) == "G7/F";
        expect (names, "Guitar: chords are named (C, Am7, G/B, D7, D9, G7/F; partial ones like C(no5), C5, C7 without its fifth; intervals)");
        const int cShape[] = { 48, 55, 60, 64, 67 };
        const auto placed = GuitarEngine::placeOnStrings (cShape, 5, 3);
        expect (placed == std::array<int, 6> { -1, 3, 5, 5, 5, 3 }, "Guitar: the neck places a voiced chord on its strings");

        // A drawn shape's notes, kept through packing.
        {
            const std::array<int, 6> openC { -1, 3, 2, 0, 1, 0 };
            int notes[6] = {};
            const int n = GuitarEngine::shapeNotes (openC, notes);
            expect (std::vector<int> (notes, notes + n) == std::vector<int> { 48, 52, 55, 60, 64 }
                        && GuitarEngine::unpackShape (GuitarEngine::packShape (openC)) == openC,
                    "Guitar: a drawn shape gives its notes (and is kept through packing)");
        }

        // Every guitar test below plays the engine directly: these collect what it sends.
        struct Heard { std::vector<std::pair<juce::int64, int>> ons; int offs = 0; bool palm = false; };
        auto drain = [] (GuitarEngine& g, juce::int64 until)
        {
            Heard h;
            GuitarEngine::Event ev;
            while (g.pop (until, ev))
            {
                if (ev.velocity > 0) { h.ons.push_back ({ ev.time, ev.note }); h.palm = h.palm || ev.palmMuted; }
                else ++h.offs;
            }
            std::sort (h.ons.begin(), h.ons.end());
            return h;
        };
        auto notesOf = [] (const Heard& h) { std::vector<int> n; for (auto [t, note] : h.ons) n.push_back (note); std::sort (n.begin(), n.end()); return n; };
        const auto second = (juce::int64) rate;
        auto chordSettings = [] (int mode) { GuitarEngine::Settings s; s.direct = false; s.mode = mode; return s; };
        auto take = [&] (GuitarEngine& g, const GuitarEngine::Settings& s, std::initializer_list<int> keys, juce::int64 at, int velocity = 100)
        {
            for (int key : keys) g.noteOn (s, 0, key, velocity, at);
            g.advance (s, at + second / 2);
        };

        // Chords are recognized like a guitarist reads them: C E G and E G C are C; one key is its
        // major chord; added keys change it (C + Eb: Cm, C + Bb: C7); Chord/Bass keeps a triad's own
        // bass (G A C E: Am/G).
        {
            auto chord = [] (std::vector<int> keys, bool chordBass = false) { return GuitarEngine::recognize (keys.data(), (int) keys.size(), chordBass); };
            auto is = [] (GuitarEngine::Chord c, std::initializer_list<int> pcs, int root, int bass)
            {
                int mask = 0;
                for (int pc : pcs) mask |= 1 << pc;
                return c.mask == mask && c.root == root && c.bass == bass;
            };
            const bool ok = is (chord ({ 60, 64, 67 }), { 0, 4, 7 }, 0, 0) && is (chord ({ 52, 55, 60 }), { 0, 4, 7 }, 0, 0)
                         && is (chord ({ 60 }), { 0, 4, 7 }, 0, 0) && is (chord ({ 60, 63 }), { 0, 3, 7 }, 0, 0)
                         && is (chord ({ 60, 70 }), { 0, 4, 7, 10 }, 0, 0) && is (chord ({ 43, 57, 60, 64 }, true), { 7, 9, 0, 4 }, 9, 7);
            expect (ok, "Guitar: chords are recognized (inversions, one key, partial chords, Chord/Bass)");
        }

        // Chord Position: C in position I is the open C (x32010), its second bass the low G; a D
        // in Drop D takes the low D string; positions III and IV put the top note higher.
        {
            const int c[] = { 60, 64, 67 }, d[] = { 50, 54, 57 };
            const auto cChord = GuitarEngine::recognize (c, 3, false);
            const auto open = GuitarEngine::voiceChord (cChord, 0, 67, GuitarEngine::tunings[0]);
            const auto dropD = GuitarEngine::voiceChord (GuitarEngine::recognize (d, 3, false), 0, 57, GuitarEngine::tunings[1]);
            const auto third = GuitarEngine::voiceChord (cChord, 2, 67, GuitarEngine::tunings[0]);
            const auto fourth = GuitarEngine::voiceChord (cChord, 3, 67, GuitarEngine::tunings[0]);
            const bool ok = open.notes == std::array<int, 6> { -1, 48, 52, 55, 60, 64 } && open.bassTwo == 43
                         && dropD.notes == std::array<int, 6> { 38, 45, 50, 57, 62, 66 }
                         && third.top() >= 70 && third.top() <= 74 && fourth.top() >= 73 && fourth.top() <= 77;
            // D: positions III and IV overlap at its top note, but each still gets its own shape.
            std::array<std::array<int, 6>, 4> dShapes;
            for (int p = 0; p < 4; ++p) dShapes[(size_t) p] = GuitarEngine::voiceChord (GuitarEngine::recognize (d, 3, false), p, 57, GuitarEngine::tunings[0]).notes;
            const bool distinct = dShapes[0] != dShapes[1] && dShapes[1] != dShapes[2] && dShapes[2] != dShapes[3];
            // Kbd: the octave the chord is played in picks the position (E1: I ... E4: IV).
            const auto dChord = GuitarEngine::recognize (d, 3, false);
            bool kbd = true;
            for (int p = 0; p < 4; ++p)
                kbd = kbd && GuitarEngine::voiceChord (dChord, 4, 42 + 12 * p, GuitarEngine::tunings[0]).notes == dShapes[(size_t) p];
            expect (ok && distinct && kbd, "Guitar: Chord Position places the voicing (I: open C, its second bass G; Drop D's low D; III and IV higher; each position its own shape; Kbd by the octave played)",
                    juce::String (third.top()) + ", " + juce::String (fourth.top()));
        }

        // Chords: the Main zone's chord is strummed down, low to high over the strum time, after the
        // gathering window; letting go ends every string.
        {
            GuitarEngine g;
            g.prepare (rate);
            auto s = chordSettings (GuitarEngine::chords);
            s.strumMs = 40;
            take (g, s, { 67, 60, 64 }, 0);
            const auto h = drain (g, second);
            const auto window = (juce::int64) (GuitarEngine::chordWindowMs * 0.001 * rate);
            // Low to high, starting when the window closes; a hand's spread: under the 40 ms set (a
            // hard stroke is quicker), never all at once.
            bool rising = h.ons.size() == 5;
            for (size_t i = 1; rising && i < h.ons.size(); ++i) rising = h.ons[i].second > h.ons[i - 1].second && h.ons[i].first >= h.ons[i - 1].first;
            const auto spread = h.ons.empty() ? 0 : h.ons.back().first - h.ons.front().first;
            const bool timing = rising && h.ons.front() == std::pair<juce::int64, int> { window, 48 } && spread > 1200 && spread <= 1920;
            for (int key : { 60, 64, 67 }) g.noteOff (0, key, second);
            const auto after = drain (g, second * 2);
            expect (timing && after.offs == 5 && ! g.hasWork(), "Chords: the chord is strummed low to high over the strum time; letting go ends it",
                    juce::String ((int) h.ons.size()) + " notes, " + juce::String (after.offs) + " offs");
        }

        // Repeat keys: C5 strums up (the high E first), C#5 a muted up strum, D5 down; C1 too.
        {
            GuitarEngine g;
            g.prepare (rate);
            auto s = chordSettings (GuitarEngine::chords);
            take (g, s, { 60, 64, 67 }, 0);
            drain (g, second);
            g.noteOn (s, 0, 84, 100, second);
            const auto up = drain (g, second * 2);
            g.noteOn (s, 0, 85, 100, second * 2);
            const auto muted = drain (g, second * 3);
            g.noteOn (s, 0, 38, 100, second * 3);
            const auto down = drain (g, second * 4);
            // An up stroke catches the high strings (four of the five here), the high E first.
            const bool ok = up.ons.size() == 4 && up.ons.front().second == 64 && ! up.palm && muted.palm && muted.ons.size() == 4
                         && down.ons.size() == 5 && down.ons.front().second == 48;
            expect (ok, "Chords: the Repeat keys strum the chord again (white up or down, black muted)");
        }

        // Strings: from the 1st to the 4th, the low strings stay silent. Velocity strings: a soft
        // chord catches the three high strings. Slow Strum on Lower Velo: a soft chord is slow.
        {
            GuitarEngine g;
            g.prepare (rate);
            auto s = chordSettings (GuitarEngine::chords);
            s.bottomString = 4;
            take (g, s, { 60, 64, 67 }, 0);
            const bool strings = notesOf (drain (g, second)) == std::vector<int> { 52, 55, 60, 64 };
            g.reset();
            auto v = chordSettings (GuitarEngine::chords);
            v.velocityStrings = true;
            take (g, v, { 60, 64, 67 }, 0, 30);
            const bool soft = notesOf (drain (g, second)) == std::vector<int> { 55, 60, 64 };
            g.reset();
            auto f = chordSettings (GuitarEngine::chords);
            f.velocityFx = GuitarEngine::slowStrumLow;
            f.fxVelocity = 60;
            f.fxTimeMs = 200.0f;
            take (g, f, { 60, 64, 67 }, 0, 40);
            const auto slow = drain (g, second);
            const double spread = (double) (slow.ons.back().first - slow.ons.front().first) * 1000.0 / rate;
            expect (strings && soft && spread >= 190.0, "Chords: Strings limits the strings; Velocity strings and a slow soft strum",
                    juce::String (spread, 0) + " ms");
        }

        // Bass & Chord: the chord plays its bass; D5 the second bass, E5 the upper strings;
        // Alter Bass: the same chord again plays the second bass.
        {
            GuitarEngine g;
            g.prepare (rate);
            auto s = chordSettings (GuitarEngine::bassAndChord);
            s.alterBass = true;
            take (g, s, { 60, 64, 67 }, 0);
            const auto bass = notesOf (drain (g, second));
            g.noteOn (s, 0, 86, 100, second);
            const auto two = notesOf (drain (g, second * 2));
            g.noteOn (s, 0, 88, 100, second * 2);
            const auto upper = notesOf (drain (g, second * 3));
            for (int key : { 60, 64, 67 }) g.noteOff (0, key, second * 3);
            take (g, s, { 60, 64, 67 }, second * 4);
            const auto again = notesOf (drain (g, second * 5));
            expect (bass == std::vector<int> { 48 } && two == std::vector<int> { 43 } && upper == std::vector<int> { 52, 55, 60, 64 }
                        && again == std::vector<int> { 43 },
                    "Bass & Chord: the bass, D5 the second bass, the other keys the upper strings; Alter Bass alternates");
        }

        // Bass & Pick: A5 the 1st string, G5 the 2nd, E5 the 4th; Chromatic: G#5 a semitone below the 1st.
        {
            GuitarEngine g;
            g.prepare (rate);
            auto s = chordSettings (GuitarEngine::bassAndPick);
            s.addOn = 2;
            take (g, s, { 60, 64, 67 }, 0);
            drain (g, second);
            std::vector<int> picked;
            int at = 1;
            for (int key : { 93, 91, 88, 92 })
            {
                g.noteOn (s, 0, key, 100, second * at);
                for (int n : notesOf (drain (g, second * ++at))) picked.push_back (n);
            }
            expect (picked == std::vector<int> { 64, 60, 52, 63 }, "Bass & Pick: the string keys pick the chord's strings (and Chromatic's)");
        }

        // Harmony: one voice at a time with its interval (an octave up); C5 repeats it. Solo: C5
        // repeats the notes held; Velo Mute mutes soft notes.
        {
            GuitarEngine g;
            g.prepare (rate);
            auto h = chordSettings (GuitarEngine::harmony);
            g.noteOn (h, 0, 60, 100, 0);
            const auto first = notesOf (drain (g, second));
            g.noteOn (h, 0, 62, 100, second);
            const auto next = drain (g, second * 2);
            g.noteOn (h, 0, 84, 100, second * 2);
            const auto repeated = notesOf (drain (g, second * 3));
            g.reset();
            auto s = chordSettings (GuitarEngine::solo);
            s.velocityFx = GuitarEngine::veloMute;
            s.fxVelocity = 80;
            g.noteOn (s, 0, 60, 100, 0);
            g.noteOn (s, 0, 64, 100, 0);
            const auto loud = drain (g, second);
            g.noteOn (s, 0, 84, 100, second);
            const auto again = notesOf (drain (g, second * 2));
            g.noteOn (s, 0, 67, 40, second * 2);
            const auto soft = drain (g, second * 3);
            expect (first == std::vector<int> { 60, 72 } && notesOf (next) == std::vector<int> { 62, 74 } && next.offs == 2
                        && repeated == std::vector<int> { 62, 74 } && ! loud.palm && again == std::vector<int> { 60, 64 } && soft.palm,
                    "Harmony plays its interval one voice at a time; Solo's Repeat keys and Velo Mute");
        }

        // Solo's articulations. H.O.: a legato step bends the string sounding (no new pick), its
        // release pulls off. Slide: by semitones over its time. A key switch (C1: Muted) mutes;
        // D#1 (Tremolo) picks again and again; Sustain 1-2 Mono (on the pedal) keeps notes until a
        // step of one or two semitones.
        {
            GuitarEngine g;
            g.prepare (rate);
            auto s = chordSettings (GuitarEngine::solo);
            s.soloFx = GuitarEngine::artHammer;
            g.noteOn (s, 0, 60, 100, 0);
            g.noteOn (s, 0, 62, 100, 1000);
            const auto hammered = drain (g, second);
            const float up = g.pitchCents (0, 60, 2000);
            g.noteOff (0, 62, 3000);
            const float back = g.pitchCents (0, 60, 4000);
            g.noteOff (0, 60, 5000);
            const auto ended = drain (g, second);
            const bool hammer = notesOf (hammered) == std::vector<int> { 60 } && up == 200.0f && back == 0.0f && ended.offs == 1;

            g.reset();
            auto sl = chordSettings (GuitarEngine::solo);
            sl.soloFx = GuitarEngine::artSlide;
            sl.legatoMs = 100.0f;
            g.noteOn (sl, 0, 60, 100, 0);
            g.noteOn (sl, 0, 62, 100, 1000);   // a step of two: within Steps
            const float mid = g.pitchCents (0, 60, 1000 + (juce::int64) (0.05 * rate));
            const bool slide = mid == 100.0f && g.pitchCents (0, 60, second) == 200.0f;

            g.reset();
            auto ks = chordSettings (GuitarEngine::solo);
            g.noteOn (ks, 0, 36, 100, 0);   // C1: Muted
            g.noteOn (ks, 0, 60, 100, 10);
            const bool muted = drain (g, second).palm;
            g.noteOff (0, 36, 20);
            g.noteOff (0, 60, 30);
            drain (g, second);
            g.noteOn (ks, 0, 39, 100, second);   // D#1: Tremolo
            g.noteOn (ks, 0, 60, 100, second);
            g.advance (ks, second * 2);
            g.tremolo (ks, second + second / 2);
            const auto picked = drain (g, second * 2);

            g.reset();
            auto sus = chordSettings (GuitarEngine::solo);
            sus.pedalFx = GuitarEngine::artSustainMono;
            g.advance (sus, 0);
            g.pedal (0, true, 0);
            g.advance (sus, 1);
            g.noteOn (sus, 0, 60, 100, 10);
            g.noteOff (0, 60, 100);
            const auto kept = drain (g, second);
            g.noteOn (sus, 0, 67, 100, second);   // a fifth away: both ring
            g.noteOff (0, 67, second + 100);
            const auto both = drain (g, second * 2);
            g.noteOn (sus, 0, 62, 100, second * 2);   // a step from 60: it stops 60
            const auto stepped = drain (g, second * 3);
            g.pedal (0, false, second * 3);
            const auto released = drain (g, second * 4);
            const bool sustain = kept.offs == 0 && both.offs == 0 && stepped.offs == 1 && released.offs >= 1;
            expect (hammer && slide && muted && picked.ons.size() >= 5 && sustain,
                    "Solo: H.O. and pull-off, Slide by semitones, key switches (Muted, Tremolo), Sustain 1-2 Mono on the pedal",
                    juce::String ((int) picked.ons.size()) + " tremolo picks, slide at " + juce::String (mid));
        }

        // Slow if Hard on Pattern's strokes: a hard chord's strokes take Time, a soft one's don't.
        {
            auto spread = [&] (int velocity)
            {
                GuitarEngine g;
                g.prepare (rate);
                auto p = chordSettings (GuitarEngine::chords);
                p.pattern = true;
                p.strumMs = 30.0f;
                p.velocityFx = GuitarEngine::slowStrumHigh;
                p.fxVelocity = 90;
                p.fxTimeMs = 150.0f;
                take (g, p, { 60, 64, 67 }, 0, velocity);
                drain (g, second);
                g.stroke (p, 0, GuitarEngine::strokeDown, g.heldVelocity (0), second);
                const auto h = drain (g, second * 2);
                juce::int64 low = std::numeric_limits<juce::int64>::max(), high = 0;
                for (auto [t, n] : h.ons) { low = juce::jmin (low, t); high = juce::jmax (high, t); }
                std::printf ("    velocity %d: %d notes over %.0f ms\n", velocity, (int) h.ons.size(), (double) (high - low) * 1000.0 / rate);
                return (double) (high - low) * 1000.0 / rate;
            };
            const double hard = spread (120), soft = spread (60);
            expect (hard > 100.0 && soft < 60.0, "Slow if Hard slows Pattern's strokes of a hard chord only",
                    juce::String (hard, 0) + " ms vs " + juce::String (soft, 0) + " ms");
        }

        // Velocity FX in the chord modes: Velo Mute mutes a soft chord (not a hard one), Slide Down
        // comes into the note from above, fret by fret; and leaving the chord modes lets a chord
        // latched by Hold go.
        {
            GuitarEngine g;
            g.prepare (rate);
            auto m = chordSettings (GuitarEngine::chords);
            m.velocityFx = GuitarEngine::veloMute;
            m.fxVelocity = 45;
            take (g, m, { 60, 64, 67 }, 0, 30);
            const bool softMuted = drain (g, second).palm && g.lastPlayed().fx == GuitarEngine::veloMute;
            for (int key : { 60, 64, 67 }) g.noteOff (0, key, second);
            take (g, m, { 62, 65, 69 }, second * 2, 110);
            const bool hardOpen = ! drain (g, second * 3).palm;
            g.reset();
            auto d = chordSettings (GuitarEngine::solo);
            d.velocityFx = GuitarEngine::slideDown;
            d.fxVelocity = 90;
            d.fxTimeMs = 100.0f;
            g.noteOn (d, 0, 60, 120, 0);
            const bool fromAbove = g.pitchCents (0, 60, 0) == 200.0f && g.pitchCents (0, 60, second) == 0.0f;
            g.reset();
            auto h = chordSettings (GuitarEngine::chords);
            h.hold = true;
            take (g, h, { 60, 64, 67 }, 0);
            for (int key : { 60, 64, 67 }) g.noteOff (0, key, second / 2);
            drain (g, second);
            const bool latched = g.heldSince (0) >= 0;
            auto solo = h;
            solo.mode = GuitarEngine::solo;
            g.advance (solo, second * 2);
            const auto let = drain (g, second * 3);
            expect (softMuted && hardOpen && fromAbove && latched && g.heldSince (0) < 0 && let.offs > 0,
                    "Velo Mute mutes soft chords; Slide Down comes from above; Solo lets a held chord go");
        }

        // Velocity FX Bend: a hard note starts two semitones low and bends up. Accent and the
        // velocity curve shape what's played; the bass zone plays firmer and without articulations.
        {
            GuitarEngine g;
            g.prepare (rate);
            auto b = chordSettings (GuitarEngine::solo);
            b.velocityFx = GuitarEngine::bend;
            b.fxVelocity = 90;
            b.fxTimeMs = 100.0f;
            g.noteOn (b, 0, 60, 120, 0);
            const bool bent = g.pitchCents (0, 60, 0) == -200.0f && g.pitchCents (0, 60, second) == 0.0f;
            g.noteOn (b, 0, 64, 60, 10);   // soft: no bend
            const bool soft = g.pitchCents (0, 64, 10) == 0.0f;
            g.reset();
            auto c = chordSettings (GuitarEngine::solo);
            c.velocityCurve = 1;   // convex: soft notes louder
            g.noteOn (c, 0, 60, 40, 0);
            const auto louder = g.pop (second, *std::make_unique<GuitarEngine::Event>());
            GuitarEngine::Event ev;
            g.reset();
            g.noteOn (c, 0, 60, 40, 0);
            g.pop (second, ev);
            const bool curved = louder && ev.velocity > 40;
            g.reset();
            auto z = chordSettings (GuitarEngine::solo);
            z.bassZone = true;
            g.noteOn (z, 0, 36, 100, 0);   // C1: Muted
            g.noteOn (z, 0, 45, 80, 10);   // in the bass zone (up to E2)
            g.pop (second, ev);
            const bool zone = ! ev.palmMuted && ev.velocity == 100;
            expect (bent && soft && curved && zone, "Velocity FX Bend, the velocity curve, and the bass zone");
        }

        // A chord held while the position changes (a pattern playing): the next stroke takes the new shape.
        {
            GuitarEngine g;
            g.prepare (rate);
            auto s = chordSettings (GuitarEngine::chords);
            s.pattern = true;
            take (g, s, { 50, 54, 57 }, 0);   // D, held
            g.stroke (s, 0, GuitarEngine::strokeDown, 100, second);
            const auto first = notesOf (drain (g, second * 2));
            s.position = 3;
            g.advance (s, second * 2);
            g.stroke (s, 0, GuitarEngine::strokeDown, 100, second * 2);
            const auto second2 = notesOf (drain (g, second * 3));
            expect (! first.empty() && ! second2.empty() && first != second2 && second2.back() > first.back(),
                    "Chords: changing the position while a chord is held reshapes the next strokes");
        }

        // Hold: the chord rings after the keys are let go, until the next chord.
        {
            GuitarEngine g;
            g.prepare (rate);
            auto s = chordSettings (GuitarEngine::chords);
            s.hold = true;
            take (g, s, { 60, 64, 67 }, 0);
            for (int key : { 60, 64, 67 }) g.noteOff (0, key, second);
            g.advance (s, second * 2);
            const auto held = drain (g, second * 2);
            take (g, s, { 62, 66, 69 }, second * 2);
            const auto next = drain (g, second * 3);
            expect (held.offs == 0 && next.offs >= 5, "Hold: the chord rings after the keys are let go, until the next one");
        }

        // Outside the zones, notes play as they are.
        {
            GuitarEngine g;
            g.prepare (rate);
            auto s = chordSettings (GuitarEngine::chords);
            g.noteOn (s, 0, 100, 100, 0);
            expect (notesOf (drain (g, second)) == std::vector<int> { 100 }, "Guitar: notes outside the zones play as they are");
        }

        // A release in the middle of a slow strum still ends the late strings, and a palm-mute off
        // never cuts the same pitch struck again.
        {
            GuitarEngine g;
            g.prepare (rate);
            auto slow = chordSettings (GuitarEngine::chords);
            slow.strumMs = 200;
            for (int key : { 60, 64, 67 }) g.noteOn (slow, 0, key, 100, 0);
            g.advance (slow, 1000);
            for (int key : { 60, 64, 67 }) g.noteOff (0, key, 1000);   // let go right after the strum starts
            std::map<int, int> sounding;
            GuitarEngine::Event ev;
            while (g.pop (second * 2, ev)) sounding[ev.note] += ev.velocity > 0 ? 1 : -1;
            bool allEnded = true;
            for (auto [note, count] : sounding) allEnded = allEnded && count == 0;
            expect (allEnded && sounding.size() == 5, "Guitar: letting go mid-strum ends every string, the late ones too");

            GuitarEngine p;
            p.prepare (rate);
            auto palm = chordSettings (GuitarEngine::solo);
            palm.palm = 0.2f;   // a long palm cut (about 500 ms)
            p.noteOn (palm, 0, 60, 30, 0);       // soft: palm-muted, its off scheduled ahead
            p.noteOff (0, 60, 100);
            p.noteOn (palm, 0, 60, 120, 2000);   // the same pitch, hard, before that off
            std::vector<std::pair<juce::int64, int>> order;
            while (p.pop (second * 2, ev)) order.push_back ({ ev.time, ev.velocity });
            int ons = 0, offsAfterSecond = 0;
            for (auto [time, velocity] : order)
            {
                if (velocity > 0) ++ons;
                else if (ons == 2) ++offsAfterSecond;
            }
            expect (ons == 2 && offsAfterSecond == 0, "Guitar: a palm-mute's late off doesn't cut the same note struck again");
        }

        // Pattern: a held chord plays its rhythm by itself, and stops when it's let go.
        {
            Player patterned (testFont);
            patterned.set ("guitarPanel", 1);
            patterned.set ("guitarDirect", 0);
            patterned.set ("guitarPattern", 0);   // the panel starts with Pattern and Hold on
            patterned.set ("guitarHold", 0);
            patterned.set ("guitarMode", 2);
            patterned.set ("guitarPattern", 1);
            patterned.set ("strumPattern", 0);   // Down Eighths
            const auto out = patterned.render (2.0, [] (int b, juce::MidiBuffer& m)
            {
                if (b == 0) for (int key : { 48, 52, 55 }) m.addEvent (juce::MidiMessage::noteOn (1, key, (juce::uint8) 100), 0);
                if (b == blockOf (1.0)) for (int key : { 48, 52, 55 }) m.addEvent (juce::MidiMessage::noteOff (1, key), 0);
            });
            // Eighths at 120 bpm: a stroke every 0.25 s; each one restarts the strings.
            const bool plays = rms (out, 0.05, 0.2) > 0.01 && rms (out, 0.55, 0.7) > 0.01;
            const bool stops = rms (out, 1.7, 2.0) < rms (out, 0.55, 0.7) * 0.1;
            expect (plays && stops, "Pattern: a held chord strums its rhythm by itself and stops when let go",
                    db (rms (out, 0.55, 0.7)) + " playing, " + db (rms (out, 1.7, 2.0)) + " after");
        }

        // On the plugin: with the panel on but every control neutral, the sound is the SoundFont's.
        auto chord = [] (Player& player)
        {
            return player.render (1.5, [] (int b, juce::MidiBuffer& m)
            {
                if (b == 0) for (int key : { 60, 64, 67 }) m.addEvent (juce::MidiMessage::noteOn (1, key, (juce::uint8) 100), 10);
                if (b == blockOf (1.0)) for (int key : { 60, 64, 67 }) m.addEvent (juce::MidiMessage::noteOff (1, key), 0);
            });
        };
        Player plain (testFont), neutral (testFont), strummed (testFont);
        neutral.set ("guitarPanel", 1);
        neutral.set ("guitarDirect", 1);   // Direct
        strummed.set ("guitarPanel", 1);
        strummed.set ("guitarDirect", 0);
        strummed.set ("guitarPattern", 0);   // the panel starts with Pattern and Hold on
        strummed.set ("guitarHold", 0);
        strummed.set ("strum", 60);
        const auto a = chord (plain), b = chord (neutral), c = chord (strummed);
        expect (a == b, "Guitar: the panel on in Direct changes nothing (bit for bit)");
        // Strummed: the top note comes ~60 ms after the bottom one, so the first 40 ms are quieter.
        const double early = rms (c, 0.0, 0.05), plainEarly = rms (a, 0.0, 0.05);
        expect (early < plainEarly * 0.9 && rms (c, 0.2, 0.9) > 0.01, "Guitar: strumming spreads the chord's start",
                db (early) + " vs " + db (plainEarly));

        // Double: the original leans left, a twin 18 ms later leans right.
        {
            Player doubled (testFont);
            doubled.set ("guitarPanel", 1);
            doubled.set ("guitarDirect", 0);
            doubled.set ("guitarPattern", 0);   // the panel starts with Pattern and Hold on
            doubled.set ("guitarHold", 0);
            doubled.set ("guitarMode", 0);   // Solo: the note as it comes, and its twin
            doubled.set ("doubleMix", 1.0f);
            juce::AudioBuffer<float> buffer (6, block);
            std::vector<float> left, right;
            for (int b = 0; b < blockOf (0.6); ++b)
            {
                juce::MidiBuffer m;
                if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                doubled.p.processBlock (buffer, m);
                for (int i = 0; i < block; ++i) { left.push_back (buffer.getSample (0, i)); right.push_back (buffer.getSample (1, i)); }
            }
            const double leftEarly = rms (left, 0.0, 0.015), rightEarly = rms (right, 0.0, 0.015);
            expect (leftEarly > rightEarly * 1.5 && rms (right, 0.1, 0.5) > 0.005,
                    "Double: the original leans left, its twin comes later on the right", db (leftEarly) + " vs " + db (rightEarly));
        }

        // Tremolo: the level swings at its rate.
        Player tremolo (testFont);
        tremolo.set ("guitarPanel", 1);   // a Guitar panel control: it acts only while the panel shows
        tremolo.set ("tremoloDepth", 1.0f);
        tremolo.set ("tremoloRate", 5.0f);
        const auto t = tremolo.note (60, 100, 1.5, 1.5);
        double low = 1e9, high = 0;
        for (double at = 0.3; at < 1.2; at += 0.02) { const double v = rms (t, at, at + 0.02); low = juce::jmin (low, v); high = juce::jmax (high, v); }
        expect (high > low * 4, "Tremolo: the level swings with depth at 100 %", db (low) + " to " + db (high));
    }

    {
        // Presets: every parameter they name exists, each built-in plays differently, and the
        // user's survive a round trip through the settings (anything unknown or broken left out).
        Player presets (testFont);
        bool allExist = true;
        for (const auto& id : GuitarPresets::ids()) allExist = allExist && presets.p.parameters.getParameter (id) != nullptr;
        for (const auto& preset : GuitarPresets::builtIns())
            for (const auto& [id, value] : preset.values) allExist = allExist && GuitarPresets::ids().contains (id);
        std::set<std::map<juce::String, float>> distinct;
        for (const auto& preset : GuitarPresets::builtIns()) distinct.insert (preset.values);
        GuitarPresets::Preset mine { "Mine", { { "strum", 80.0f }, { "guitarMode", 3.0f } }, true };
        auto back = GuitarPresets::fromXml (GuitarPresets::toXml ({ mine }));
        const auto broken = GuitarPresets::fromXml ("<GuitarPresets><Preset name=\"X\"><Value id=\"gain\" value=\"9\"/>"
                                                    "<Value id=\"strum\" value=\"nan\"/></Preset><Preset name=\"\"/></GuitarPresets>");
        expect (allExist && distinct.size() == GuitarPresets::builtIns().size() && back.size() == 1 && back[0].name == "Mine"
                    && back[0].values == mine.values && broken.size() == 1 && broken[0].values.empty()
                    && GuitarPresets::fromXml ("not xml").empty(),
                "Presets: real parameters, each built-in different, the user's kept through the settings");
    }

    std::printf ("\n%d of %d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
