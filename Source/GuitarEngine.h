#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <limits>

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
    enum Mode { solo = 0, chords = 1, bassAndPick = 2, shape = 3, strumKeys = 4, pattern = 5 };

    // What one stroke of the pick does (Strum Keys' white keys, a pattern's steps).
    enum Stroke { strokeDown, strokeUp, mutedDown, mutedUp, bassOnly, highDown, highUp, damp, pickString };

    // Strum Keys: keys from this one up are the pick hand; below it, the chord.
    static constexpr int strumKeysFrom = 60;

    // The rhythms of Pattern mode, one character per sixteenth note, looped: D down, U up, d and
    // u muted, B the bass alone, 1-6 one string of the chord (1 the lowest), . nothing.
    struct Rhythm { const char* name; const char* steps; };
    static constexpr Rhythm rhythms[] = {
        { "Down Eighths",    "D.D.D.D.D.D.D.D." },
        { "Down Up",         "D.U.D.U.D.U.D.U." },
        { "Folk",            "D...D.U...U.D.U." },
        { "Pop",             "D...D.U.D.U.D.U." },
        { "Ballad",          "D.....D.D.U.D.U." },
        { "Country",         "B...D...B...D.U." },
        { "Rock Muted",      "d.d.d.d.D.d.d.d." },
        { "Reggae",          "....d.......d..." },
        { "Funk Sixteenths", "D.uUd.U.D.uUd.Uu" },
        { "Waltz",           "B...D...D..." },
        { "Fingerpicking",   "1.3.2.3.1.3.2.3." },
        { "Arpeggio",        "1.2.3.4.5.6.5.4." },
    };
    static constexpr int rhythmCount = (int) (sizeof (rhythms) / sizeof (rhythms[0]));
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
        // Shape mode: the chord drawn on the neck, a fret per string from the low E (-1: not
        // played, 0: open). Each key played strums it, moved so its bass is that key.
        std::array<int, 6> shapeFrets { -1, -1, -1, -1, -1, -1 };
        int rhythm = 0;         // Pattern mode: which of `rhythms`
        // True when nothing would change: the processor then plays notes directly.
        bool isNeutral() const
        {
            return strumMs <= 0.0f && fret == 0 && palm <= 0.0f && humanize <= 0.0f && doubleMix <= 0.0f
                && (mode == chords || mode == solo);   // Shape always plays the drawn chord
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
        for (auto& held : chordsHeld) held = {};
        for (auto& ringing : ringingNotes) ringing = {};
        for (auto& pending : chordsPending) pending = {};
        for (auto& group : groups) group = {};
        nextUp = false;
    }

    // A note on or off from the clip (channel 0-15), at an absolute sample time.
    void noteOn (const Settings& s, int channel, int note, int velocity, juce::int64 time)
    {
        auto& pending = chordsPending[(size_t) channel];
        if (s.mode == pattern || (s.mode == strumKeys && note < strumKeysFrom))
        {
            hold (channel, note, velocity, time);   // the chord hand: silent until a stroke
            return;
        }
        if (s.mode == strumKeys)
        {
            static constexpr int byPitchClass[12] = { strokeDown, damp, strokeUp, damp, mutedDown, mutedUp, damp,
                                                      bassOnly, damp, highDown, damp, highUp };
            stroke (s, channel, (Stroke) byPitchClass[note % 12], velocity, time);
            return;
        }
        if (s.mode == shape)
        {
            std::array<int, 6> notes {};
            const int count = shapeNotes (s.shapeFrets, note, notes.data());
            if (count == 0)   // nothing drawn: the key as played
            {
                const std::array<int, 1> one { note };
                startGroupWith (s, channel, one.data(), 1, one.data(), 1, velocity, time, false);
                return;
            }
            const std::array<int, 1> key { note };
            startGroupWith (s, channel, key.data(), 1, notes.data(), count, velocity, time, true);
            return;
        }
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
        // The chord hand lets go: when nothing is held any more, the strings are stopped.
        auto& held = chordsHeld[(size_t) channel];
        for (int i = 0; i < held.count; ++i)
            if (held.keys[(size_t) i] == note)
            {
                held.keys[(size_t) i] = held.keys[(size_t) --held.count];
                if (held.count == 0) endRinging (channel, time);
                return;
            }
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
        for (auto& ringing : ringingNotes) if (ringing.count > 0) return true;
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

    // The last chord (or note) started, for the panel's neck, chord name and strum arrow.
    struct LastPlayed
    {
        int sequence = 0;            // grows with every chord or note started
        std::array<int, 16> notes {};   // low to high
        int count = 0;
        bool up = false, strummed = false, palm = false;
    };
    const LastPlayed& lastPlayed() const { return last; }

    // A chord is held on `channel` (Strum Keys, Pattern): since when (-1: none) and how hard.
    juce::int64 heldSince (int channel) const { return chordsHeld[(size_t) channel].count > 0 ? chordsHeld[(size_t) channel].since : -1; }
    int heldVelocity (int channel) const { return chordsHeld[(size_t) channel].velocity; }

    // One stroke of the pick on the chord held on `channel` (or, with none held, the drawn shape).
    // The strings it strikes ring until the next stroke, a damp, or the chord hand letting go.
    void stroke (const Settings& s, int channel, Stroke kind, int velocity, juce::int64 time, int string = 0)
    {
        endRinging (channel, time);
        if (kind == damp) return;
        std::array<int, 16> chord {};
        const int count = heldChord (s, channel, chord.data());
        if (count == 0) return;
        std::array<int, 16> notes {};
        int n = 0;
        switch (kind)
        {
            case bassOnly:   notes[0] = chord[0]; n = 1; break;
            case pickString: notes[0] = chord[(size_t) juce::jlimit (0, count - 1, string)]; n = 1; break;
            case highDown: case highUp:
                for (int i = juce::jmax (0, count - 3); i < count; ++i) notes[(size_t) n++] = chord[(size_t) i];
                break;
            default:
                for (int i = 0; i < count; ++i) notes[(size_t) n++] = chord[(size_t) i];
        }
        auto strokeSettings = s;
        strokeSettings.direction = (kind == strokeUp || kind == mutedUp || kind == highUp) ? up : down;
        if (strokeSettings.strumMs <= 0.0f) strokeSettings.strumMs = 18.0f;   // a pick takes time to cross the strings
        const bool muted = kind == mutedDown || kind == mutedUp;
        if (muted)
        {
            strokeSettings.palm = juce::jmax (0.85f, s.palm);
            velocity = juce::jmin (velocity, palmVelocity - 1);   // muted strokes are palm-muted
        }
        auto& ringing = ringingNotes[(size_t) channel];
        const auto* group = startGroupWith (strokeSettings, channel, nullptr, 0, notes.data(), n, velocity, time, n > 1);
        ringing.count = n;
        ringing.doubled = s.doubleMix > 0.0f;
        for (int i = 0; i < n; ++i) ringing.notes[(size_t) i] = group->outputs[(size_t) i];
    }

    // The drawn shape's notes, low to high, moved so the lowest is `bassKey` (none if nothing drawn).
    static int shapeNotes (const std::array<int, 6>& frets, int bassKey, int* out)
    {
        int count = 0, lowest = 999;
        for (size_t string = 0; string < 6; ++string)
            if (frets[string] >= 0)
                lowest = juce::jmin (lowest, strings[string] + frets[string]);
        if (lowest == 999) return 0;
        for (size_t string = 0; string < 6; ++string)
            if (frets[string] >= 0)
            {
                const int note = strings[string] + frets[string] + bassKey - lowest;
                if (juce::isPositiveAndBelow (note, 128)) out[count++] = note;
            }
        std::sort (out, out + count);
        return count;
    }

    // A shape as six numbers packed in 30 bits (5 per string; 31: not played), so the editor can
    // hand it to the audio thread in one atomic.
    static juce::uint32 packShape (const std::array<int, 6>& frets)
    {
        juce::uint32 packed = 0;
        for (size_t i = 0; i < 6; ++i)
            packed |= (juce::uint32) (frets[i] < 0 ? 31 : juce::jlimit (0, 24, frets[i])) << (5 * i);
        return packed;
    }
    static std::array<int, 6> unpackShape (juce::uint32 packed)
    {
        std::array<int, 6> frets;
        for (size_t i = 0; i < 6; ++i)
        {
            const int value = (int) ((packed >> (5 * i)) & 31u);
            frets[i] = value == 31 ? -1 : value;
        }
        return frets;
    }

    // Where the notes lie on six strings for the neck picture: one note per string, strings
    // rising with the notes, frets (0-19) as close to `around` as possible. -1: string not played.
    // At most six notes (the lowest six).
    static std::array<int, 6> placeOnStrings (const int* notes, int count, int around)
    {
        std::array<int, 6> best;
        best.fill (-1);
        count = juce::jmin (count, 6);
        if (count <= 0) return best;
        int bestCost = std::numeric_limits<int>::max();
        // Every way to pick `count` rising strings (at most 6 choose 3 = 20 tries per size).
        for (int mask = 0; mask < (1 << 6); ++mask)
        {
            if (__builtin_popcount ((unsigned) mask) != count) continue;
            std::array<int, 6> frets;
            frets.fill (-1);
            int cost = 0, n = 0;
            bool fits = true;
            for (int string = 0; string < 6 && fits; ++string)
            {
                if (! (mask & (1 << string))) continue;
                const int fret = notes[n++] - strings[(size_t) string];
                if (fret < 0 || fret > 19) fits = false;
                else { frets[(size_t) string] = fret; cost += fret == 0 ? 0 : std::abs (fret - around); }
            }
            if (fits && cost < bestCost) { bestCost = cost; best = frets; }
        }
        return best;
    }

private:
    // The chord hand (Strum Keys, Pattern) and the strings the last stroke left ringing.
    struct Held
    {
        std::array<int, 16> keys {};
        int count = 0, velocity = 100;
        juce::int64 since = 0;
    };
    struct Ringing
    {
        std::array<juce::uint8, 16> notes {};
        int count = 0;
        bool doubled = false;
    };

    void hold (int channel, int note, int velocity, juce::int64 time)
    {
        auto& held = chordsHeld[(size_t) channel];
        if (held.count == 0) held.since = time;
        if (held.count < (int) held.keys.size()) held.keys[(size_t) held.count++] = note;
        held.velocity = velocity;
    }

    void endRinging (int channel, juce::int64 time)
    {
        auto& ringing = ringingNotes[(size_t) channel];
        for (int i = 0; i < ringing.count; ++i)
        {
            schedule ({ time, (juce::uint8) channel, ringing.notes[(size_t) i], 0, 0, false, false });
            if (ringing.doubled)
                schedule ({ time + samples (doubleDelayMs), (juce::uint8) channel, ringing.notes[(size_t) i], 0, 0, false, true });
        }
        ringing = {};
        // Strokes aren't tied to keys: their groups end with the ringing notes.
        for (auto& group : groups)
            if (group.used && group.channel == channel && group.inputCount == 0) group = {};
    }

    // The held chord as a guitarist would hold it: at the Fret knob's position, or the open
    // position when it's off. With nothing held, the shape drawn on the neck where it was drawn.
    int heldChord (const Settings& s, int channel, int* out) const
    {
        const auto& held = chordsHeld[(size_t) channel];
        if (held.count == 0)
        {
            std::array<int, 16> notes {};
            int lowest = 999;
            for (size_t i = 0; i < 6; ++i) if (s.shapeFrets[i] >= 0) lowest = juce::jmin (lowest, strings[i] + s.shapeFrets[i]);
            if (lowest == 999) return 0;
            const int n = shapeNotes (s.shapeFrets, lowest, notes.data());
            std::copy (notes.begin(), notes.begin() + n, out);
            return n;
        }
        std::array<int, 16> keys {};
        std::copy (held.keys.begin(), held.keys.begin() + held.count, keys.begin());
        std::sort (keys.begin(), keys.begin() + held.count);
        return voice (keys.data(), held.count, s.fret, out);
    }

    std::array<Held, 16> chordsHeld {};
    std::array<Ringing, 16> ringingNotes {};

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
        group->inputCount = inputs == nullptr ? 0 : juce::jmin (inputCount, 16);
        for (int i = 0; i < group->inputCount; ++i)
        {
            group->inputs[(size_t) i] = (juce::uint8) inputs[i];
            group->held[(size_t) i] = true;
        }
        group->heldCount = group->inputCount;
        group->outputCount = juce::jmin (outputCount, 16);
        group->doubled = s.doubleMix > 0.0f;
        last.count = group->outputCount;
        for (int i = 0; i < last.count; ++i) last.notes[(size_t) i] = outputs[i];
        std::sort (last.notes.begin(), last.notes.begin() + last.count);

        // Order: a down strum goes low to high, an up strum high to low.
        bool goingUp = s.direction == up || (s.direction == alternate && nextUp);
        if (s.direction == alternate && strum) nextUp = ! nextUp;
        const auto strumTotal = strum ? samples (s.strumMs) : 0;
        const bool palm = s.palm > 0.0f && velocity < palmVelocity;
        last.up = goingUp;
        last.strummed = strum && strumTotal > 0 && group->outputCount > 1;
        last.palm = palm;
        ++last.sequence;
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
    LastPlayed last;
    juce::Random random { 1 };   // seeded: renders repeat exactly
};
