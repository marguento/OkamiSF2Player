#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>

// The Guitar panel's playing engine: turns the notes a clip or keyboard sends into what a
// guitarist would play. Chords (notes arriving together) are voiced on six strings at a fret
// position and strummed down, up or alternating; Bass & Pick plays the lowest note first and
// picks the rest; soft notes can be palm-muted (cut short, darker); Humanize adds small random
// changes of velocity, timing and tuning. Every output is a note on or off at an absolute
// sample time, sometimes in a later block: the processor plays them as they fall due.
//
// Audio thread only. Fixed arrays, no allocation. With every control neutral the processor
// doesn't use it at all, so the SoundFont plays exactly as before.
class GuitarEngine
{
public:
    enum Mode { solo = 0, chords = 1, bassAndPick = 2 };
    enum Direction { down = 0, up = 1, alternate = 2 };

    struct Settings
    {
        int mode = chords;
        float strumMs = 0;      // 0 to 80: from the first string to the last
        int direction = down;
        int fret = 0;           // 0: as played; 1 to 12: chords voiced at that fret
        float palm = 0;         // 0 to 1: how short and dark notes under velocity 45 get
        float humanize = 0;     // 0 to 1
        float doubleMix = 0;    // 0 to 1: Double's level (0: off)
        // True when nothing would change: the processor then plays notes directly.
        bool isNeutral() const
        {
            return strumMs <= 0.0f && fret == 0 && palm <= 0.0f && humanize <= 0.0f && doubleMix <= 0.0f
                && (mode == chords || mode == solo);
        }
    };

    struct Event
    {
        juce::int64 time = 0;   // absolute sample
        juce::uint8 channel = 0, note = 0, velocity = 0;   // velocity 0: note off
        juce::int8 detuneCents = 0;
        bool palmMuted = false;
        bool isDouble = false;   // the Double's twin: its own channels, panned right
    };

    // Double: the twin comes this much later and this much sharper (a second take, double-tracked).
    static constexpr float doubleDelayMs = 18.0f;
    static constexpr int doubleDetuneCents = 4;

    static constexpr int maxEvents = 1024;
    // Standard tuning, low to high: E2 A2 D3 G3 B3 E4.
    static constexpr std::array<int, 6> strings { 40, 45, 50, 55, 59, 64 };

    void prepare (double rate) { sampleRate = rate; reset(); }

    // Forget everything pending (a new engine, the panel turned off): `flushOffs` is called with
    // a note-off for every note the engine started and hasn't ended.
    template <typename Off>
    void reset (Off&& flushOffs)
    {
        for (auto& group : groups)
            if (group.used)
                for (int i = 0; i < group.outputCount; ++i)
                    flushOffs (group.channel, group.outputs[(size_t) i], group.doubled);
        reset();
    }

    void reset()
    {
        eventCount = 0;
        for (auto& pending : chordsPending) pending = {};
        for (auto& group : groups) group = {};
        nextUp = false;
    }

    // A note on or off from the clip (channel 0-15), at an absolute sample time.
    void noteOn (const Settings& s, int channel, int note, int velocity, juce::int64 time)
    {
        auto& pending = chordsPending[(size_t) channel];
        if (s.mode == solo)
        {
            const std::array<int, 1> one { note };
            startGroupWith (s, channel, one.data(), 1, one.data(), 1, velocity, time, false);
            return;
        }
        // Notes within the window are one chord; it's played when the window closes.
        if (pending.count == 0)
            pending.start = time;
        if (pending.count < (int) pending.keys.size())
        {
            pending.keys[(size_t) pending.count] = (juce::uint8) note;
            pending.released[(size_t) pending.count] = false;
            pending.velocity = juce::jmax (pending.velocity, velocity);
            ++pending.count;
        }
    }

    void noteOff (int channel, int note, juce::int64 time)
    {
        // Still gathering: remember it, so the chord's note ends right after it starts.
        auto& pending = chordsPending[(size_t) channel];
        for (int i = 0; i < pending.count; ++i)
            if (pending.keys[(size_t) i] == note)
                pending.released[(size_t) i] = true;
        for (auto& group : groups)
        {
            if (! group.used || group.channel != channel)
                continue;
            for (int i = 0; i < group.inputCount; ++i)
                if (group.inputs[(size_t) i] == note && group.held[(size_t) i])
                {
                    group.held[(size_t) i] = false;
                    if (--group.heldCount == 0)
                        endGroup (group, time);
                    return;
                }
        }
    }

    // Closes the chord windows that end before `until`, so their notes get scheduled.
    void advance (const Settings& s, juce::int64 until)
    {
        const auto window = samples (chordWindowMs);
        for (int channel = 0; channel < 16; ++channel)
        {
            auto& pending = chordsPending[(size_t) channel];
            if (pending.count == 0 || pending.start + window > until)
                continue;
            const auto chord = pending;
            pending = {};
            std::array<int, 16> keys {};
            for (int i = 0; i < chord.count; ++i)
                keys[(size_t) i] = chord.keys[(size_t) i];
            std::sort (keys.begin(), keys.begin() + chord.count);
            std::array<int, 16> voiced {};
            int voicedCount = chord.count;
            if (s.fret > 0)
                voicedCount = voice (keys.data(), chord.count, s.fret, voiced.data());
            else
                std::copy (keys.begin(), keys.begin() + chord.count, voiced.begin());
            startGroupWith (s, channel, keys.data(), chord.count, voiced.data(), voicedCount, chord.velocity,
                            chord.start + window, true);
            // A key released while the chord gathered still ends its part.
            for (int i = 0; i < chord.count; ++i)
                if (chord.released[(size_t) i])
                    noteOff (channel, chord.keys[(size_t) i], chord.start + window + 1);
        }
    }

    // Takes the next event due before `until`, earliest first.
    bool pop (juce::int64 until, Event& out)
    {
        int best = -1;
        for (int i = 0; i < eventCount; ++i)
            if (events[(size_t) i].time < until && (best < 0 || events[(size_t) i].time < events[(size_t) best].time))
                best = i;
        if (best < 0)
            return false;
        out = events[(size_t) best];
        events[(size_t) best] = events[(size_t) --eventCount];
        return true;
    }

    bool hasWork() const
    {
        if (eventCount > 0) return true;
        for (auto& group : groups) if (group.used) return true;
        for (auto& pending : chordsPending) if (pending.count > 0) return true;
        return false;
    }

    // A chord on six strings at `fret`: for each string from the low E, the first fret from
    // `fret` to `fret + 4` whose note is in the chord. The bass (the lowest key's note) is on
    // the lowest string that can play it; strings below it stay silent. Returns how many.
    static int voice (const int* keys, int count, int fret, int* out)
    {
        if (count <= 0) return 0;
        bool inChord[12] = {};
        for (int i = 0; i < count; ++i) inChord[keys[i] % 12] = true;
        const int bass = keys[0] % 12;
        int played = 0;
        bool bassPlaced = false;
        for (const int open : strings)
        {
            for (int f = fret; f <= fret + 4; ++f)
            {
                const int note = open + f;
                if (! inChord[note % 12]) continue;
                if (! bassPlaced && note % 12 != bass) continue;   // wait for a string that has the bass
                out[played++] = note;
                bassPlaced = true;
                break;
            }
        }
        if (played == 0)   // the chord doesn't fit the window: play it as keyed
        {
            for (int i = 0; i < count; ++i) out[i] = keys[i];
            return count;
        }
        return played;
    }

    static constexpr float chordWindowMs = 12.0f;
    static constexpr int palmVelocity = 45;

private:
    struct Pending
    {
        std::array<juce::uint8, 16> keys {};
        std::array<bool, 16> released {};
        int count = 0, velocity = 0;
        juce::int64 start = 0;
    };

    // The notes started for one chord (or one solo note), ended when its last key is let go.
    struct Group
    {
        bool used = false;
        int channel = 0;
        std::array<juce::uint8, 16> inputs {}, outputs {};
        std::array<bool, 16> held {};
        int inputCount = 0, outputCount = 0, heldCount = 0;
        bool doubled = false;
    };

    juce::int64 samples (float ms) const { return (juce::int64) (ms * 0.001 * sampleRate); }

    void schedule (const Event& e)
    {
        if (eventCount < maxEvents)
            events[(size_t) eventCount++] = e;
    }

    // Starts the notes of a chord (or one note), strummed and humanized.
    Group* startGroupWith (const Settings& s, int channel, const int* inputs, int inputCount, const int* outputs,
                           int outputCount, int velocity, juce::int64 time, bool strum)
    {
        Group* group = nullptr;
        for (auto& g : groups) if (! g.used) { group = &g; break; }
        if (group == nullptr) group = &groups[0];   // all in use: reuse the oldest slot
        *group = {};
        group->used = true;
        group->channel = channel;
        group->inputCount = juce::jmin (inputCount, 16);
        for (int i = 0; i < group->inputCount; ++i)
        {
            group->inputs[(size_t) i] = (juce::uint8) inputs[i];
            group->held[(size_t) i] = true;
        }
        group->heldCount = group->inputCount;
        group->outputCount = juce::jmin (outputCount, 16);
        group->doubled = s.doubleMix > 0.0f;

        // Order: a down strum goes low to high, an up strum high to low.
        bool goingUp = s.direction == up || (s.direction == alternate && nextUp);
        if (s.direction == alternate && strum) nextUp = ! nextUp;
        const auto strumTotal = strum ? samples (s.strumMs) : 0;
        const bool palm = s.palm > 0.0f && velocity < palmVelocity;
        const int n = group->outputCount;
        for (int i = 0; i < n; ++i)
        {
            const int index = goingUp ? n - 1 - i : i;
            const int note = outputs[index];
            group->outputs[(size_t) i] = (juce::uint8) note;
            juce::int64 at = time;
            if (s.mode == bassAndPick && strum && n > 1)
                at += i == 0 ? 0 : samples (juce::jmax (s.strumMs, 25.0f)) + (strumTotal * (i - 1)) / juce::jmax (1, n - 2);
            else if (n > 1)
                at += (strumTotal * i) / (n - 1);
            int vel = velocity;
            juce::int8 detune = 0;
            if (s.humanize > 0.0f)
            {
                vel += juce::roundToInt ((random.nextFloat() * 2.0f - 1.0f) * 5.0f * s.humanize);
                at += samples (random.nextFloat() * 5.0f * s.humanize);
                detune = (juce::int8) juce::roundToInt ((random.nextFloat() * 2.0f - 1.0f) * 5.0f * s.humanize);
            }
            const auto velocityByte = (juce::uint8) juce::jlimit (1, 127, vel);
            const auto palmOff = samples (juce::jmap (s.palm, 600.0f, 70.0f));
            schedule ({ at, (juce::uint8) channel, (juce::uint8) note, velocityByte, detune, palm, false });
            // Palm mute: the string is stopped by the hand soon after it's struck.
            if (palm)
                schedule ({ at + palmOff, (juce::uint8) channel, (juce::uint8) note, 0, 0, false, false });
            if (s.doubleMix > 0.0f)
            {
                const auto twin = at + samples (doubleDelayMs);
                schedule ({ twin, (juce::uint8) channel, (juce::uint8) note, velocityByte,
                            (juce::int8) (detune + doubleDetuneCents), palm, true });
                if (palm)
                    schedule ({ twin + palmOff, (juce::uint8) channel, (juce::uint8) note, 0, 0, false, true });
            }
        }
        return group;
    }

private:
    void endGroup (Group& group, juce::int64 time)
    {
        for (int i = 0; i < group.outputCount; ++i)
        {
            schedule ({ time, (juce::uint8) group.channel, group.outputs[(size_t) i], 0, 0, false, false });
            if (group.doubled)   // the twin ends as late as it started
                schedule ({ time + samples (doubleDelayMs), (juce::uint8) group.channel, group.outputs[(size_t) i], 0, 0, false, true });
        }
        group = {};
    }

    double sampleRate = 44100.0;
    std::array<Event, maxEvents> events {};
    int eventCount = 0;
    std::array<Pending, 16> chordsPending {};
    std::array<Group, 64> groups {};
    bool nextUp = false;
    juce::Random random { 1 };   // seeded: renders repeat exactly
};
