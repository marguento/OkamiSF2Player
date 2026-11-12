#pragma once

#include "ChordName.h"

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
    enum Mode { solo = 0, chords = 1, bassAndPick = 2, shape = 3, strumKeys = 4, pattern = 5, bassAndChord = 6, harmony = 7, keyChords = 8, autoChords = 9 };

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
        int tuning = 0;         // which of `tunings`
        bool velocityStrings = false;   // soft strums hit only the high strings, hard ones all six
        // Key Chords: the key's tonic (0 C ... 11 B) and scale (0 major, 1 minor, 2 major with
        // sevenths, 3 minor with sevenths).
        int keyRoot = 0, keyScale = 0;
        // Mode options. Strings: chords use the strings from this one up (0: all six, 1: from the
        // A, 2: from the D, 3: from the G). Slow strum: notes softer than `slowBelow` (0: off) are
        // strummed over at least `slowMs`, a gentle stroke.
        int firstString = 0;
        int slowBelow = 0;
        float slowMs = 150.0f;
        // True when nothing would change: the processor then plays notes directly.
        bool isNeutral() const
        {
            return strumMs <= 0.0f && slowBelow <= 0 && fret == 0 && palm <= 0.0f && humanize <= 0.0f && doubleMix <= 0.0f && ! velocityStrings
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
        // A note-off: when the note it ends started (-1: any). An off whose pitch was struck
        // again since is stale and dropped, so it never cuts the newer note.
        juce::int64 onTime = -1;
    };

    // Double: the twin comes this much later and this much sharper (a second take, double-tracked).
    static constexpr float doubleDelayMs = 18.0f;
    static constexpr int doubleDetuneCents = 4;

    static constexpr int maxEvents = 1024;
    // Standard tuning, low to high: E2 A2 D3 G3 B3 E4.
    static constexpr std::array<int, 6> strings { 40, 45, 50, 55, 59, 64 };
    // The tunings the panel offers, low string first: Standard, Drop D, Drop C, Open G, DADGAD.
    using Tuning = std::array<int, 6>;
    static constexpr std::array<Tuning, 5> tunings { {
        { 40, 45, 50, 55, 59, 64 },   // E A D G B E
        { 38, 45, 50, 55, 59, 64 },   // D A D G B E
        { 36, 43, 48, 53, 57, 62 },   // C G C F A D
        { 38, 43, 50, 55, 59, 62 },   // D G D G B D
        { 38, 45, 50, 55, 57, 62 },   // D A D G A D
    } };
    static constexpr const char* tuningNames[] = { "Standard", "Drop D", "Drop C", "Open G", "DADGAD" };
    static const Tuning& tuningOf (int index) { return tunings[(size_t) juce::jlimit (0, (int) tunings.size() - 1, index)]; }

    void prepare (double rate) { sampleRate = rate; reset(); }

    // Forget everything pending (a new engine, the panel turned off): `flushOffs` is called with
    // a note-off for every note the engine started and hasn't ended.
    template <typename Off>
    void reset (Off&& flushOffs)
    {
        for (auto& group : groups)
            if (group.used)
                for (int i = 0; i < group.outputCount; ++i)
                {
                    flushOffs (group.channel, group.outputs[(size_t) i], false);
                    if (group.doubled) flushOffs (group.channel, group.outputs[(size_t) i], true);
                }
        for (int i = 0; i < eventCount; ++i)   // offs still to come (a twin's, a palm's)
            if (events[(size_t) i].velocity == 0)
                flushOffs (events[(size_t) i].channel, events[(size_t) i].note, events[(size_t) i].isDouble);
        reset();
    }

    void reset()
    {
        eventCount = 0;
        for (auto& channel : lastOnTime) channel.fill (-1);
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
        if (s.mode == keyChords)
        {
            std::array<int, 4> chord {};
            const int size = keyChord (s, note, chord.data());
            std::array<int, 16> voiced {};
            const int n = voice (chord.data(), size, s.fret, voiced.data(), tuningOf (s.tuning), s.firstString);
            const std::array<int, 1> key { note };
            startGroupWith (s, channel, key.data(), 1, voiced.data(), n, velocity, time, true);
            return;
        }
        if (s.mode == bassAndChord && note < strumKeysFrom)   // the left hand: bass notes, one by one
        {
            const std::array<int, 1> one { note };
            startGroupWith (s, channel, one.data(), 1, one.data(), 1, velocity, time, false);
            return;
        }
        if (s.mode == harmony && note >= strumKeysFrom)
        {
            // The melody note and a second voice under it: the held chord's nearest tone a third
            // to a sixth below, else a major third below.
            int below = note - 4;
            const auto& held = chordsHeld[(size_t) channel];
            for (int gap = 3; gap <= 9 && held.count > 0; ++gap)
            {
                bool inChord = false;
                for (int i = 0; i < held.count; ++i) inChord = inChord || held.keys[(size_t) i] % 12 == (note - gap + 120) % 12;
                if (inChord) { below = note - gap; break; }
            }
            const std::array<int, 1> key { note };
            const std::array<int, 2> pair { juce::jmax (0, below), note };
            startGroupWith (s, channel, key.data(), 1, pair.data(), 2, velocity, time, true);
            return;
        }
        if (s.mode == pattern || ((s.mode == strumKeys || s.mode == harmony) && note < strumKeysFrom))
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
            const int count = shapeNotes (s.shapeFrets, note, notes.data(), tuningOf (s.tuning), s.firstString);
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

    // False when the engine never took this key (it was pressed before the engine was in use):
    // the caller then passes the note-off on as it is, so the note doesn't stick.
    bool noteOff (int channel, int note, juce::int64 time)
    {
        // The chord hand lets go: when nothing is held any more, the strings are stopped.
        auto& held = chordsHeld[(size_t) channel];
        for (int i = 0; i < held.count; ++i)
            if (held.keys[(size_t) i] == note)
            {
                held.keys[(size_t) i] = held.keys[(size_t) --held.count];
                if (held.count == 0) endRinging (channel, time);
                return true;
            }
        // Still gathering: remember it, so the chord's note ends right after it starts.
        bool known = false;
        auto& pending = chordsPending[(size_t) channel];
        for (int i = 0; i < pending.count; ++i)
            if (pending.keys[(size_t) i] == note)
                pending.released[(size_t) i] = known = true;
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
                    return true;
                }
        }
        return known;
    }

    // All notes off on a channel (CC120 / CC123 from the clip): everything the engine holds
    // or started there ends, the chord hand included.
    void allOff (int channel, juce::int64 time)
    {
        chordsPending[(size_t) channel] = {};
        chordsHeld[(size_t) channel] = {};
        endRinging (channel, time);
        for (auto& group : groups)
            if (group.used && group.channel == channel)
                endGroup (group, time);
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
            int keyCount = chord.count;
            for (int i = 0; i < chord.count; ++i)
                keys[(size_t) i] = chord.keys[(size_t) i];
            const auto at = chord.start + window;
            if (s.mode == autoChords)
            {
                // A key added to a chord still held changes it: the keys still down join the new
                // ones, and the old chord ends as the new one is strummed.
                for (auto& group : groups)
                {
                    if (! group.used || group.channel != channel) continue;
                    for (int i = 0; i < group.inputCount && keyCount < (int) keys.size(); ++i)
                        if (group.held[(size_t) i]) keys[(size_t) keyCount++] = group.inputs[(size_t) i];
                    endGroup (group, at);
                }
            }
            std::sort (keys.begin(), keys.begin() + keyCount);
            keyCount = (int) (std::unique (keys.begin(), keys.begin() + keyCount) - keys.begin());
            std::array<int, 16> voiced {};
            int voicedCount = keyCount;
            if (s.mode == autoChords)
            {
                std::array<int, 16> full {};
                const int size = completeChord (keys.data(), keyCount, full.data());
                voicedCount = voice (full.data(), size, s.fret, voiced.data(), tuningOf (s.tuning), s.firstString);
            }
            else if (s.fret > 0)
                voicedCount = voice (keys.data(), keyCount, s.fret, voiced.data(), tuningOf (s.tuning), s.firstString);
            else
                std::copy (keys.begin(), keys.begin() + keyCount, voiced.begin());
            startGroupWith (s, channel, keys.data(), keyCount, voiced.data(), voicedCount, chord.velocity, at, true);
            // A key released while the chord gathered still ends its part.
            for (int i = 0; i < chord.count; ++i)
                if (chord.released[(size_t) i])
                    noteOff (channel, chord.keys[(size_t) i], chord.start + window + 1);
        }
    }

    // Takes the next event due before `until`, earliest first.
    bool pop (juce::int64 until, Event& out)
    {
        for (;;)
        {
            int best = -1;
            for (int i = 0; i < eventCount; ++i)
                if (events[(size_t) i].time < until && (best < 0 || events[(size_t) i].time < events[(size_t) best].time
                                                        || (events[(size_t) i].time == events[(size_t) best].time
                                                            && events[(size_t) i].velocity == 0 && events[(size_t) best].velocity > 0)))
                    best = i;   // at the same time, offs first
            if (best < 0)
                return false;
            out = events[(size_t) best];
            events[(size_t) best] = events[(size_t) --eventCount];
            auto& started = lastOnTime[(size_t) (out.channel + (out.isDouble ? 16 : 0))][out.note];
            if (out.velocity > 0)
            {
                started = out.time;
                return true;
            }
            if (out.onTime < 0 || started == out.onTime)
                return true;
            // A stale off: that pitch was struck again since the note it was meant to end.
        }
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
    static int voice (const int* keys, int count, int fret, int* out, const Tuning& tuning = tunings[0], int firstString = 0)
    {
        if (count <= 0) return 0;
        bool inChord[12] = {};
        for (int i = 0; i < count; ++i) inChord[keys[i] % 12] = true;
        const int bass = keys[0] % 12;
        int played = 0;
        bool bassPlaced = false;
        firstString = juce::jlimit (0, 5, firstString);
        for (size_t string = (size_t) firstString; string < tuning.size(); ++string)
        {
            const int open = tuning[string];
            for (int f = fret; f <= fret + 4; ++f)
            {
                const int note = open + f;
                if (! inChord[note % 12]) continue;
                // Wait for a string that has the bass (on the high strings alone, any chord tone).
                if (! bassPlaced && firstString == 0 && note % 12 != bass) continue;
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

    // Auto Chords: the chord the keys ask for, completed. One key: its major chord. More: the
    // lowest key is the root, and the plainest chord type (ChordName's order) holding every key
    // is played (C + Eb: C minor; C + Bb: C7; C + F: Csus4). Keys no type holds play as they are.
    static int completeChord (const int* keys, int count, int* out)
    {
        if (count <= 0) return 0;
        const int root = keys[0];
        int relative = 0;
        for (int i = 0; i < count; ++i) relative |= 1 << ((keys[i] - root + 120) % 12);
        int mask = 0;
        if (count == 1 || relative == 1)
            mask = ChordName::bits ({ 0, 4, 7 });
        else
            for (const auto& type : ChordName::types)
                if ((type.mask & relative) == relative) { mask = type.mask; break; }
        if (mask == 0)
        {
            std::copy (keys, keys + count, out);
            return count;
        }
        int n = 0;
        for (int interval = 0; interval < 12; ++interval)
            if (mask & (1 << interval)) out[n++] = root + interval;
        return n;
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
        for (int i = 0; i < group->outputCount; ++i)
        {
            ringing.notes[(size_t) i] = group->outputs[(size_t) i];
            ringing.onsets[(size_t) i] = group->onsets[(size_t) i];
        }
        ringing.count = group->outputCount;
    }

    // Key Chords: the chord a key plays in the chosen key. A note of the scale: the chord the
    // scale builds on it (a triad, or a seventh chord); any other note: its major triad (a chord
    // borrowed from outside the key, as Bb in C). Low to high, the key itself first.
    static int keyChord (const Settings& s, int note, int* out)
    {
        static constexpr int majorScale[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static constexpr int minorScale[7] = { 0, 2, 3, 5, 7, 8, 10 };
        const bool minor = s.keyScale == 1 || s.keyScale == 3;
        const bool sevenths = s.keyScale >= 2;
        const int* scale = minor ? minorScale : majorScale;
        const int degreeOffset = ((note % 12) - s.keyRoot + 12) % 12;
        int degree = -1;
        for (int d = 0; d < 7; ++d) if (scale[d] == degreeOffset) degree = d;
        if (degree < 0)   // outside the key: its major triad
        {
            out[0] = note; out[1] = note + 4; out[2] = note + 7;
            return 3;
        }
        const int count = sevenths ? 4 : 3;
        for (int i = 0; i < count; ++i)
        {
            const int step = degree + 2 * i;   // every other note of the scale: root, third, fifth, seventh
            out[i] = note + scale[step % 7] + 12 * (step / 7) - scale[degree];
        }
        return count;
    }

    // The drawn shape's notes, low to high, moved so the lowest is `bassKey` (none if nothing drawn).
    static int shapeNotes (const std::array<int, 6>& frets, int bassKey, int* out, const Tuning& strings = tunings[0], int firstString = 0)
    {
        int count = 0, lowest = 999;
        for (size_t string = 0; string < 6; ++string)
            if (frets[string] >= 0)
                lowest = juce::jmin (lowest, strings[string] + frets[string]);
        if (lowest == 999) return 0;
        for (size_t string = (size_t) juce::jlimit (0, 5, firstString); string < 6; ++string)
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
    static std::array<int, 6> placeOnStrings (const int* notes, int count, int around, const Tuning& strings = tunings[0])
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
        std::array<juce::int64, 16> onsets {};
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
            scheduleOffs (channel, ringing.notes[(size_t) i], ringing.onsets[(size_t) i], time, ringing.doubled);
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
            const auto& tuning = tuningOf (s.tuning);
            for (size_t i = 0; i < 6; ++i) if (s.shapeFrets[i] >= 0) lowest = juce::jmin (lowest, tuning[i] + s.shapeFrets[i]);
            if (lowest == 999) return 0;
            const int n = shapeNotes (s.shapeFrets, lowest, notes.data(), tuning, s.firstString);
            std::copy (notes.begin(), notes.begin() + n, out);
            return n;
        }
        std::array<int, 16> keys {};
        std::copy (held.keys.begin(), held.keys.begin() + held.count, keys.begin());
        std::sort (keys.begin(), keys.begin() + held.count);
        return voice (keys.data(), held.count, s.fret, out, tuningOf (s.tuning), s.firstString);
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
        std::array<juce::int64, 16> onsets {};   // when each output starts (strummed: later and later)
        int inputCount = 0, outputCount = 0, heldCount = 0;
        bool doubled = false;
    };

    // A note's off (and its twin's) at `time`, but never before the note itself has started: a
    // release in the middle of a slow strum would otherwise end strings before they sound.
    void scheduleOffs (int channel, int note, juce::int64 onset, juce::int64 time, bool doubled)
    {
        schedule ({ juce::jmax (time, onset + 1), (juce::uint8) channel, (juce::uint8) note, 0, 0, false, false, onset });
        if (doubled)
        {
            const auto twin = onset + samples (doubleDelayMs);
            schedule ({ juce::jmax (time + samples (doubleDelayMs), twin + 1), (juce::uint8) channel, (juce::uint8) note,
                        0, 0, false, true, twin });
        }
    }

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
        if (group == nullptr)   // all in use (keys never let go): end one, never drop its notes
        {
            group = &groups[(size_t) (nextVictim++ % (int) groups.size())];
            endGroup (*group, time);
        }
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
        // Velocity → strings: a soft strum catches the high strings only, a hard one all of them.
        if (s.velocityStrings && strum && outputCount > 3)
        {
            const int keep = velocity < 50 ? 3 : velocity < 90 ? juce::jmax (4, outputCount - 1) : outputCount;
            outputs += outputCount - keep;
            outputCount = keep;
        }
        group->outputCount = juce::jmin (outputCount, 16);
        group->doubled = s.doubleMix > 0.0f;
        last.count = group->outputCount;
        for (int i = 0; i < last.count; ++i) last.notes[(size_t) i] = outputs[i];
        std::sort (last.notes.begin(), last.notes.begin() + last.count);

        // Order: a down strum goes low to high, an up strum high to low.
        bool goingUp = s.direction == up || (s.direction == alternate && nextUp);
        if (s.direction == alternate && strum) nextUp = ! nextUp;
        auto strumTotal = strum ? samples (s.strumMs) : 0;
        if (strum && s.slowBelow > 0 && velocity < s.slowBelow)   // a soft stroke: slower
            strumTotal = juce::jmax (strumTotal, samples (s.slowMs));
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
            group->onsets[(size_t) i] = at;
            schedule ({ at, (juce::uint8) channel, (juce::uint8) note, velocityByte, detune, palm, false });
            // Palm mute: the string is stopped by the hand soon after it's struck (dropped if the
            // same pitch is struck again before then).
            if (palm)
                schedule ({ at + palmOff, (juce::uint8) channel, (juce::uint8) note, 0, 0, false, false, at });
            if (s.doubleMix > 0.0f)
            {
                const auto twin = at + samples (doubleDelayMs);
                schedule ({ twin, (juce::uint8) channel, (juce::uint8) note, velocityByte,
                            (juce::int8) (detune + doubleDetuneCents), palm, true });
                if (palm)
                    schedule ({ twin + palmOff, (juce::uint8) channel, (juce::uint8) note, 0, 0, false, true, twin });
            }
        }
        return group;
    }

private:
    void endGroup (Group& group, juce::int64 time)
    {
        for (int i = 0; i < group.outputCount; ++i)
            scheduleOffs (group.channel, group.outputs[(size_t) i], group.onsets[(size_t) i], time, group.doubled);
        group = {};
    }

    double sampleRate = 44100.0;
    std::array<Event, maxEvents> events {};
    int eventCount = 0;
    // When each pitch last started, per channel (16-31: the Double's twins): stale offs are dropped.
    std::array<std::array<juce::int64, 128>, 32> lastOnTime {};
    int nextVictim = 0;
    std::array<Pending, 16> chordsPending {};
    std::array<Group, 64> groups {};
    bool nextUp = false;
    LastPlayed last;
    juce::Random random { 1 };   // seeded: renders repeat exactly
};
