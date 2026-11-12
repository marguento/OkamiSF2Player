#pragma once

#include "ChordName.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <limits>

// The Guitar panel's playing engine: turns the notes a clip or keyboard sends into what a
// guitarist would play, in the way of the classic guitar instruments for keyboards.
//
// The keyboard has three zones. The Main zone (E1-B4, MIDI 40-83: the guitar's own range) plays
// notes (Solo, Harmony) or takes chords (Chords, Bass & Chord, Bass & Pick), which are
// recognized, voiced on six strings and played. The Repeat zones (C1-D#1, 36-39, and C5-C6,
// 84-96) repeat what the Main zone holds: strums up or down, muted strums, the bass, single
// strings. Notes outside the zones play as they are.
//
// Every note lies on a string: a new note on a string stops the one ringing there, as on a
// real guitar. Soft notes can be palm-muted (cut short, darker); Humanize adds small random
// changes of velocity, timing and tuning. Every output is a note on or off at an absolute sample
// time, sometimes in a later block: the processor plays them as they fall due.
//
// Audio thread only. Fixed arrays, no allocation. With Direct on (the default) the processor
// doesn't use it at all, so the SoundFont plays exactly as before.
class GuitarEngine
{
public:
    enum Mode { solo = 0, harmony = 1, chords = 2, bassAndChord = 3, bassAndPick = 4 };
    static constexpr int modeCount = 5;

    // The keyboard's zones.
    static constexpr int leftRepeatLow = 36, leftRepeatHigh = 39, mainLow = 40, mainHigh = 83, rightRepeatLow = 84, rightRepeatHigh = 96;
    static bool inMain (int note) { return note >= mainLow && note <= mainHigh; }
    static bool inLeftRepeat (int note) { return note >= leftRepeatLow && note <= leftRepeatHigh; }
    static bool inRightRepeat (int note) { return note >= rightRepeatLow && note <= rightRepeatHigh; }
    static bool inRepeat (int note) { return inLeftRepeat (note) || inRightRepeat (note); }

    // What one stroke of the pick does (Repeat keys, a pattern's steps).
    enum Stroke { strokeDown, strokeUp, mutedDown, mutedUp, bassOnly, bassTwo, highDown, highUp, damp, pickString };

    // Harmony: what each key adds to its note.
    static constexpr const char* harmonyNames[] = { "4th Down", "5th Up", "Octave Up", "Power Chord 1", "Power Chord 2", "2 Octaves Up" };
    // Chord Position: where the voicing's highest note lies (I to IV), or Kbd: in the octave played.
    static constexpr const char* positionNames[] = { "I", "II", "III", "IV", "Kbd" };
    // Velocity Switch FX.
    enum VelocityFx { fxOff = 0, slowStrumHigh = 1, slowStrumLow = 2, veloMute = 3 };
    static constexpr const char* velocityFxNames[] = { "Off", "Slow Strum on Higher Velo", "Slow Strum on Lower Velo", "Velo Mute" };
    // Bass & Pick: what the black keys among its string keys do.
    static constexpr const char* addOnNames[] = { "Off", "Unison", "Chromatic" };

    // The rhythms of Pattern, one character per sixteenth note, looped: D down, U up, d and u
    // muted, B the bass alone, 1-6 one string of the chord (1 the lowest), . nothing.
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
        bool direct = true;     // a plain sampler: the engine isn't used at all
        int mode = solo;
        bool pattern = false;   // the Main zone's chord plays the rhythm by itself
        float strumMs = 0;      // from the first string to the last (0: a pick's natural 18 ms)
        int direction = down;   // the Main zone's strums
        float palm = 0;         // 0 to 1: how short and dark notes under velocity 45 get
        float humanize = 0;     // 0 to 1
        float doubleMix = 0;    // 0 to 1: Double's level (0: off)
        int rhythm = 0;         // Pattern: which of `rhythms`
        int tuning = 0;         // which of `tunings`
        bool velocityStrings = false;   // soft strums hit only the high strings, hard ones all six
        // Mode options.
        int position = 0;               // Chord Position: 0-3 I to IV, 4 Kbd
        int topString = 1, bottomString = 6;   // Chords: the strings strummed (1 the high E, 6 the low E)
        int repeatStrings = 4;          // Bass & Chord: how many upper strings the Repeat keys strum
        bool chordBass = false;         // a triad over another bass is played as such (Am/G)
        bool alterBass = false;         // the same chord again alternates its bass with the fifth
        bool bassMono = false;          // the second bass stops the first
        int addOn = 0;                  // Bass & Pick's black keys: 0 off, 1 unison, 2 chromatic
        int harmony = 2;                // which of `harmonyNames`
        int velocityFx = fxOff;         // which of `velocityFxNames`
        int fxVelocity = 90;            // its velocity threshold
        float fxTimeMs = 150.0f;        // a slow strum's length
        bool hold = false;              // chords keep sounding after the keys are let go
        // True when nothing would change: the processor then plays notes directly.
        bool isNeutral() const { return direct; }
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
        for (int channel = 0; channel < 16; ++channel)
            for (const auto& string : channels[(size_t) channel].strings)
                if (string.note >= 0)
                {
                    flushOffs (channel, string.note, false);
                    if (string.doubled) flushOffs (channel, string.note, true);
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
        for (auto& channel : channels) channel = {};
        for (auto& group : groups) group = {};
        nextUp = false;
    }

    // A note on from the clip or keyboard (channel 0-15), at an absolute sample time.
    void noteOn (const Settings& s, int channel, int note, int velocity, juce::int64 time)
    {
        auto& c = channels[(size_t) channel];
        if (! inMain (note) && ! inRepeat (note))   // outside the zones: as it is
        {
            playKeyed (s, channel, note, &note, 1, velocity, time, false);
            return;
        }
        if (s.mode == solo || s.mode == harmony)
        {
            if (inMain (note))
            {
                addKey (c.down, note);
                c.velocity = velocity;
                if (s.mode == solo)
                    playKeyed (s, channel, note, &note, 1, velocity, time, false, s.velocityFx == veloMute && velocity < s.fxVelocity);
                else
                {
                    // One voice at a time: the previous interval ends.
                    for (auto& group : groups)
                        if (group.used && group.channel == channel && group.harmony) endGroup (group, time);
                    std::array<int, 3> notes {};
                    const int n = harmonyNotes (s.harmony, note, notes.data());
                    auto* group = playKeyed (s, channel, note, notes.data(), n, velocity, time, true);
                    if (group != nullptr) group->harmony = true;
                    c.lastHarmony = notes;
                    c.lastHarmonyCount = n;
                }
                return;
            }
            // A Repeat key: the held notes again, white keys full, black keys muted.
            std::array<int, 16> notes {};
            int n = 0;
            if (s.mode == harmony)
            {
                if (c.down.count > 0) { n = c.lastHarmonyCount; std::copy (c.lastHarmony.begin(), c.lastHarmony.begin() + n, notes.begin()); }
            }
            else
                for (int i = 0; i < c.down.count; ++i) notes[(size_t) n++] = c.down.keys[(size_t) i];
            if (n == 0) return;
            std::sort (notes.begin(), notes.begin() + n);
            playKeyed (s, channel, note, notes.data(), n, velocity, time, s.mode == harmony, isBlack (note));
            return;
        }

        // Chord modes.
        if (inMain (note))
        {
            if (c.down.count == 0)   // a new chord: what was latched by Hold is let go
            {
                if (c.latched) endStrings (channel, time);
                c.latched = false;
                c.since = time;
                c.patternRunning = false;
            }
            addKey (c.down, note);
            addKey (c.taken, note);
            c.velocity = velocity;
            if (c.triggerAt < 0) c.triggerAt = time + samples (chordWindowMs);   // gather the chord's notes
            return;
        }
        repeatKey (s, channel, note, velocity, time);
    }

    // False when the engine never took this key (it was pressed before the engine was in use):
    // the caller then passes the note-off on as it is, so the note doesn't stick.
    bool noteOff (int channel, int note, juce::int64 time)
    {
        auto& c = channels[(size_t) channel];
        bool known = false;
        if (removeKey (c.down, note))
        {
            known = true;
            if (c.down.count == 0 && c.chordMode)
            {
                if (c.hold) c.latched = true;
                else if (c.triggerAt < 0) { endStrings (channel, time); c.voicing = {}; }
                // Still gathering: the chord is played when the window closes, then let go.
                else c.releaseAfterTrigger = true;
            }
        }
        if (c.chordMode && inRepeat (note)) known = true;   // strokes ring on: nothing to end
        for (auto& group : groups)
        {
            if (! group.used || group.channel != channel || group.key != note) continue;
            endGroup (group, time);
            known = true;
        }
        return known;
    }

    // All notes off on a channel (CC120 / CC123 from the clip): everything ends.
    void allOff (int channel, juce::int64 time)
    {
        endStrings (channel, time);
        channels[(size_t) channel] = {};
        for (auto& group : groups)
            if (group.used && group.channel == channel)
                endGroup (group, time);
    }

    // Plays the chords whose gathering window closes before `until`; lets go of chords Hold
    // kept once Hold is switched off.
    void advance (const Settings& s, juce::int64 until)
    {
        const bool chordMode = s.mode == chords || s.mode == bassAndChord || s.mode == bassAndPick;
        for (int channel = 0; channel < 16; ++channel)
        {
            auto& c = channels[(size_t) channel];
            c.chordMode = chordMode;
            c.hold = s.hold;
            if (c.latched && ! s.hold && c.down.count == 0)
            {
                endStrings (channel, until);
                c.latched = false;
                c.voicing = {};
            }
            if (c.triggerAt < 0 || c.triggerAt > until)
                continue;
            const auto at = c.triggerAt;
            c.triggerAt = -1;
            if (chordMode) triggerChord (s, channel, at);
            c.taken = {};
            if (c.releaseAfterTrigger && c.down.count == 0)
            {
                if (s.hold) c.latched = true;
                else { endStrings (channel, at + 1); c.voicing = {}; }
            }
            c.releaseAfterTrigger = false;
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
        for (auto& c : channels)
        {
            if (c.triggerAt >= 0) return true;
            for (auto& string : c.strings) if (string.note >= 0) return true;
        }
        return false;
    }

    // ---- Chords: what the keys ask for, and how a guitarist holds it.

    struct Chord
    {
        int mask = 0;       // pitch classes (bit n: n)
        int root = -1;      // pitch class
        int bass = -1;      // pitch class of the bass (the root, or a slash chord's bass)
    };

    // The chord the keys (low to high) ask for. Exact chord types first (C E G, E G C: C); else
    // the lowest key as the root, completed with the plainest type holding every key (one key:
    // its major chord; C + Eb: C minor; C + Bb: C7). With `chordBass`, a major or minor triad
    // over another bass keeps that bass (G A C E: Am/G).
    static Chord recognize (const int* keys, int count, bool chordBass)
    {
        Chord chord;
        if (count <= 0) return chord;
        int classes = 0;
        for (int i = 0; i < count; ++i) classes |= 1 << (keys[i] % 12);
        const int lowest = keys[0] % 12;
        auto relativeTo = [classes] (int root) { return ChordName::rotate (classes, root); };
        auto absolute = [] (int mask, int root) { int out = 0; for (int i = 0; i < 12; ++i) if (mask & (1 << i)) out |= 1 << ((i + root) % 12); return out; };

        if (chordBass && count >= 3)
        {
            int upper = 0;
            for (int i = 1; i < count; ++i) if (keys[i] % 12 != lowest) upper |= 1 << (keys[i] % 12);
            for (int root = 0; root < 12; ++root)
            {
                if (! (upper & (1 << root))) continue;
                const int relative = ChordName::rotate (upper, root);
                if (relative == ChordName::bits ({ 0, 4, 7 }) || relative == ChordName::bits ({ 0, 3, 7 }))
                    return { upper | (1 << lowest), root, lowest };
            }
        }
        if (juce::countNumberOfBits ((juce::uint32) classes) == 1)
            return { absolute (ChordName::bits ({ 0, 4, 7 }), lowest), lowest, lowest };
        // An exact type, the lowest key's pitch class tried first as the root.
        for (int attempt = 0; attempt < 12; ++attempt)
        {
            const int root = (lowest + attempt) % 12;
            if (! (classes & (1 << root))) continue;
            for (const auto& type : ChordName::types)
                if (relativeTo (root) == type.mask && juce::countNumberOfBits ((juce::uint32) type.mask) >= 3)
                    return { classes, root, root };
        }
        // Completed: the plainest type that holds every key, the lowest key as the root first.
        for (int attempt = 0; attempt < 12; ++attempt)
        {
            const int root = (lowest + attempt) % 12;
            if (! (classes & (1 << root))) continue;
            for (const auto& type : ChordName::types)
                if ((type.mask & relativeTo (root)) == relativeTo (root) && juce::countNumberOfBits ((juce::uint32) type.mask) >= 3)
                    return { absolute (type.mask, root), root, root };
        }
        return { absolute (ChordName::bits ({ 0, 4, 7 }), lowest), lowest, lowest };
    }

    // A chord on the six strings: a note per string (-1: not played), low E first, and where its
    // basses lie.
    struct Voicing
    {
        std::array<int, 6> notes { -1, -1, -1, -1, -1, -1 };
        int bassString = -1;    // Bass I: the root (or the slash chord's bass)
        int bassTwo = -1;       // Bass II: the fifth, usually on a neighbouring string (a note)
        int bassTwoString = -1;
        Chord chord;
        bool valid() const { return bassString >= 0; }
        int top() const { int t = -1; for (int n : notes) t = juce::jmax (t, n); return t; }
    };

    // The chord at `fret`: from the lowest string that has the bass, each string takes the first
    // fret from `fret` to `fret + 4` (open strings too, in the first position) whose note is in
    // the chord.
    static Voicing place (const Chord& chord, int fret, const Tuning& tuning)
    {
        Voicing v;
        v.chord = chord;
        for (size_t string = 0; string < 6; ++string)
        {
            for (int f = fret; f <= fret + 4; ++f)
            {
                const int note = tuning[string] + f;
                if (! (chord.mask & (1 << (note % 12)))) continue;
                if (v.bassString < 0 && note % 12 != chord.bass) continue;   // wait for a string with the bass
                v.notes[string] = note;
                if (v.bassString < 0) v.bassString = (int) string;
                break;
            }
        }
        return v;
    }

    // The voicing whose highest note falls in the Chord Position's range (I: E3-G#3, II: G3-B3,
    // III: A#3-D4, IV: C#4-F4, Kbd: the octave from the E at or below the highest key played),
    // the lowest such fret; else the closest. Bass II found too.
    static Voicing voiceChord (const Chord& chord, int position, int highestKey, const Tuning& tuning)
    {
        static constexpr int ranges[4][2] = { { 64, 68 }, { 67, 71 }, { 70, 74 }, { 73, 77 } };
        int low, high;
        if (position >= 0 && position < 4) { low = ranges[position][0]; high = ranges[position][1]; }
        else
        {
            low = highestKey - ((highestKey - 4) % 12 + 12) % 12;
            high = low + 11;
        }
        Voicing best;
        int bestDistance = std::numeric_limits<int>::max();
        for (int fret = 0; fret <= 12; ++fret)
        {
            const auto v = place (chord, fret, tuning);
            if (! v.valid()) continue;
            const int top = v.top();
            const int distance = top < low ? low - top : top > high ? top - high : 0;
            if (distance < bestDistance) { best = v; bestDistance = distance; }
            if (distance == 0) break;
        }
        if (! best.valid()) return best;
        // Bass II: the fifth, on the string below the bass when it can reach it, else on one above.
        const int fifth = (chord.bass + 7) % 12;
        const int bassNote = best.notes[(size_t) best.bassString];
        const int bassFret = bassNote - tuning[(size_t) best.bassString];
        if (best.bassString > 0)
            for (int f = juce::jmax (0, bassFret - 2); f <= bassFret + 2; ++f)
                if ((tuning[(size_t) best.bassString - 1] + f) % 12 == fifth)
                { best.bassTwo = tuning[(size_t) best.bassString - 1] + f; best.bassTwoString = best.bassString - 1; break; }
        for (int string = best.bassString + 1; string < 6 && best.bassTwo < 0; ++string)
            if (best.notes[(size_t) string] >= 0 && best.notes[(size_t) string] % 12 == fifth)
            { best.bassTwo = best.notes[(size_t) string]; best.bassTwoString = string; }
        if (best.bassTwo < 0) { best.bassTwo = bassNote; best.bassTwoString = best.bassString; }
        return best;
    }

    // Harmony: the notes a key plays (low to high).
    static int harmonyNotes (int kind, int note, int* out)
    {
        switch (kind)
        {
            case 0:  out[0] = note - 5; out[1] = note; return 2;               // a fourth down
            case 1:  out[0] = note; out[1] = note + 7; return 2;               // a fifth up
            case 3:  out[0] = note; out[1] = note + 7; out[2] = note + 12; return 3;   // fifth and fourth up
            case 4:  out[0] = note; out[1] = note + 5; out[2] = note + 12; return 3;   // fourth and fifth up
            case 5:  out[0] = note; out[1] = note + 12; out[2] = note + 24; return 3;  // two octaves
            default: out[0] = note; out[1] = note + 12; return 2;              // an octave up
        }
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

    // A chord is held (or kept by Hold) on `channel`: since when (-1: none) and how hard.
    juce::int64 heldSince (int channel) const
    {
        const auto& c = channels[(size_t) channel];
        return (c.down.count > 0 || c.latched) && c.voicing.valid() ? c.since : -1;
    }
    int heldVelocity (int channel) const { return channels[(size_t) channel].velocity; }

    // One stroke of the pick on the chord held on `channel`. Each string struck stops what rang
    // on it; the rest ring on.
    void stroke (const Settings& s, int channel, Stroke kind, int velocity, juce::int64 time, int string = 0)
    {
        auto& c = channels[(size_t) channel];
        if (kind == damp) { endStrings (channel, time); return; }
        if (! c.voicing.valid()) return;
        const auto& v = c.voicing;
        std::array<bool, 6> use {};
        int bassNote = -1, bassString = -1;
        switch (kind)
        {
            case bassOnly: bassNote = v.notes[(size_t) v.bassString]; bassString = v.bassString; break;
            case bassTwo:  bassNote = v.bassTwo; bassString = v.bassTwoString; break;
            case pickString:
            {
                int k = 0;   // the string-th string of the chord, from its lowest
                for (int i = 0; i < 6; ++i)
                    if (v.notes[(size_t) i] >= 0 && k++ == juce::jlimit (0, 5, string)) use[(size_t) i] = true;
                break;
            }
            case highDown: case highUp:
                for (int i = 3; i < 6; ++i) use[(size_t) i] = true;
                break;
            default:
                for (int i = 0; i < 6; ++i) use[(size_t) i] = true;
        }
        if (bassString >= 0)
        {
            playOnStrings (s, channel, { bassNote }, { bassString }, velocity, time, false, false, false);
            return;
        }
        strum (s, channel, use, velocity, time, kind == strokeUp || kind == mutedUp || kind == highUp, kind == mutedDown || kind == mutedUp);
    }

    // Plays notes now and lets them go `length` samples later (a drawn chord heard from the neck).
    void audition (const Settings& s, int channel, const int* notes, int count, int velocity, juce::int64 time, juce::int64 length)
    {
        auto* group = startGroupWith (s, channel, notes, count, velocity, time, true);
        if (group != nullptr) endGroup (*group, time + length);
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

    // The drawn shape's notes, low to high (none if nothing drawn).
    static int shapeNotes (const std::array<int, 6>& frets, int* out, const Tuning& strings = tunings[0])
    {
        int count = 0;
        for (size_t string = 0; string < 6; ++string)
            if (frets[string] >= 0 && juce::isPositiveAndBelow (strings[string] + frets[string], 128))
                out[count++] = strings[string] + frets[string];
        std::sort (out, out + count);
        return count;
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

    static bool isBlack (int note) { const int pc = note % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

private:
    struct Keys
    {
        std::array<int, 16> keys {};
        int count = 0;
    };
    static void addKey (Keys& k, int note)
    {
        for (int i = 0; i < k.count; ++i) if (k.keys[(size_t) i] == note) return;
        if (k.count < (int) k.keys.size()) k.keys[(size_t) k.count++] = note;
    }
    static bool removeKey (Keys& k, int note)
    {
        for (int i = 0; i < k.count; ++i)
            if (k.keys[(size_t) i] == note) { k.keys[(size_t) i] = k.keys[(size_t) --k.count]; return true; }
        return false;
    }

    // A note ringing on a string until something else is played there, a damp, or the chord let go.
    struct StringNote
    {
        int note = -1;
        juce::int64 onset = 0;
        bool doubled = false;
    };

    struct Channel
    {
        Keys down;               // Main zone keys held now
        Keys taken;              // Main zone keys pressed while the chord gathers
        int velocity = 100;
        juce::int64 since = 0;   // when the chord was taken
        juce::int64 triggerAt = -1;   // when the gathering window closes (-1: none)
        bool releaseAfterTrigger = false, latched = false, chordMode = false, hold = false, patternRunning = false;
        Voicing voicing;         // the chord held, as played
        int lastChordMask = -1;  // Alter Bass: the previous chord, and whether its Bass II came last
        bool lastWasBassTwo = false;
        std::array<StringNote, 6> strings {};
        std::array<int, 3> lastHarmony {};
        int lastHarmonyCount = 0;
    };

    // The Main zone's chord: recognized, voiced and played as the mode says.
    void triggerChord (const Settings& s, int channel, juce::int64 time)
    {
        auto& c = channels[(size_t) channel];
        Keys keys = c.down;
        for (int i = 0; i < c.taken.count; ++i) addKey (keys, c.taken.keys[(size_t) i]);
        if (keys.count == 0) return;
        std::sort (keys.keys.begin(), keys.keys.begin() + keys.count);
        const auto chord = recognize (keys.keys.data(), keys.count, s.chordBass);
        c.voicing = voiceChord (chord, s.position, keys.keys[(size_t) keys.count - 1], tuningOf (s.tuning));
        if (s.pattern && ! c.patternRunning) { c.since = time; c.patternRunning = true; }   // the rhythm starts with the chord
        if (! c.voicing.valid() || s.pattern) { showVoicing (c.voicing, false, false); return; }
        const int velocity = c.velocity;
        if (s.mode == chords)
        {
            std::array<bool, 6> use {};
            for (int i = 0; i < 6; ++i) use[(size_t) i] = 6 - i >= s.topString && 6 - i <= s.bottomString;
            bool goingUp = s.direction == up || (s.direction == alternate && nextUp);
            if (s.direction == alternate) nextUp = ! nextUp;
            strum (s, channel, use, velocity, time, goingUp, false);
            return;
        }
        // Bass & Chord, Bass & Pick: the bass alone (Alter Bass: the same chord again, the fifth).
        const int mask = c.voicing.chord.mask | (c.voicing.chord.bass << 12);
        const bool second = s.alterBass && mask == c.lastChordMask && ! c.lastWasBassTwo;
        c.lastChordMask = mask;
        c.lastWasBassTwo = second;
        endStrings (channel, time);   // a new chord: the strings change
        playBass (s, channel, second, velocity, time);
        showVoicing (c.voicing, false, false);
    }

    void playBass (const Settings& s, int channel, bool second, int velocity, juce::int64 time)
    {
        auto& c = channels[(size_t) channel];
        const auto& v = c.voicing;
        const int note = second ? v.bassTwo : v.notes[(size_t) v.bassString];
        const int string = second ? v.bassTwoString : v.bassString;
        if (s.bassMono && second) endString (channel, v.bassString, time);
        playOnStrings (s, channel, { note }, { string }, velocity, time, false, false, false);
    }

    // A Repeat key in a chord mode.
    void repeatKey (const Settings& s, int channel, int note, int velocity, juce::int64 time)
    {
        auto& c = channels[(size_t) channel];
        if (! c.voicing.valid()) return;
        // White keys strum (C, E, G up; D, F, A, B down); black keys strum muted (C#, F#, A# up;
        // D#, G# down).
        static constexpr bool upByClass[12] = { true, true, false, false, true, false, true, true, false, false, true, false };
        const bool goingUp = upByClass[note % 12], muted = isBlack (note);
        std::array<bool, 6> use {};
        if (s.mode == chords)
        {
            for (int i = 0; i < 6; ++i) use[(size_t) i] = 6 - i >= s.topString && 6 - i <= s.bottomString;
            strum (s, channel, use, velocity, time, goingUp, muted);
            return;
        }
        if (s.mode == bassAndChord)
        {
            if (note == 84) { playBass (s, channel, false, velocity, time); return; }
            if (note == 86) { playBass (s, channel, true, velocity, time); return; }
            for (int i = 6 - juce::jlimit (1, 6, s.repeatStrings); i < 6; ++i) use[(size_t) i] = true;
            strum (s, channel, use, velocity, time, goingUp, muted);
            return;
        }
        // Bass & Pick: the right zone's keys pick strings, the left zone strums the four upper ones.
        if (inLeftRepeat (note))
        {
            for (int i = 2; i < 6; ++i) use[(size_t) i] = true;
            strum (s, channel, use, velocity, time, goingUp, muted);
            return;
        }
        int target = -1, shift = 0;   // -2: Bass I, -3: Bass II, else a string index (0 the low E)
        switch (note)
        {
            case 84: target = -2; break;
            case 86: target = -3; break;
            case 88: target = 2; break;   // the 4th string (D)
            case 89: target = 3; break;   // the 3rd (G)
            case 91: target = 4; break;   // the 2nd (B)
            case 93: target = 5; break;   // the 1st (high E)
            default:
                if (s.addOn == 1)   // Unison: the black keys double the strings
                    target = note == 85 ? -3 : note == 87 ? 2 : note == 90 ? 4 : (note == 92 || note >= 94) ? 5 : -1;
                else if (s.addOn == 2)   // Chromatic: a semitone below, the top keys above the 1st string
                {
                    if (note == 85) { target = -3; shift = -1; }
                    else if (note == 87) { target = 2; shift = -1; }
                    else if (note == 90) { target = 4; shift = -1; }
                    else if (note == 92) { target = 5; shift = -1; }
                    else if (note >= 94) { target = 5; shift = note - 93; }
                }
        }
        if (target == -1) return;
        if (target == -2) { playBass (s, channel, false, velocity, time); return; }
        const auto& v = c.voicing;
        int string = target == -3 ? v.bassTwoString : target;
        int pitch = target == -3 ? v.bassTwo : v.notes[(size_t) string];
        if (pitch < 0) return;
        if (target == -3 && s.bassMono) endString (channel, v.bassString, time);
        playOnStrings (s, channel, { pitch + shift }, { string }, velocity, time, false, false, false);
    }

    // Strums the chord on the strings `use` marks, low to high (or high to low).
    void strum (const Settings& s, int channel, const std::array<bool, 6>& use, int velocity, juce::int64 time, bool goingUp, bool muted)
    {
        const auto& v = channels[(size_t) channel].voicing;
        std::array<int, 6> notes {}, strings {};
        int n = 0;
        for (int i = 0; i < 6; ++i)
            if (use[(size_t) i] && v.notes[(size_t) i] >= 0) { notes[(size_t) n] = v.notes[(size_t) i]; strings[(size_t) n++] = i; }
        if (n == 0) return;
        playOnStrings (s, channel, { notes.data(), n }, { strings.data(), n }, velocity, time, true, goingUp, muted);
        showVoicing (v, goingUp, true);
        last.palm = muted;
    }

    struct Span
    {
        Span (std::initializer_list<int> l) { for (int x : l) values[(size_t) count++] = x; }
        Span (const int* p, int n) { count = juce::jmin (n, 6); std::copy (p, p + count, values.begin()); }
        std::array<int, 6> values {};
        int count = 0;
    };

    // Notes on their strings, each stopping what rang there; they ring until stopped in turn.
    void playOnStrings (const Settings& s, int channel, Span notes, Span strings, int velocity, juce::int64 time,
                        bool strummed, bool goingUp, bool muted)
    {
        for (int i = 0; i < strings.count; ++i) endString (channel, strings.values[(size_t) i], time);
        auto stroke = s;
        stroke.direction = goingUp ? up : down;
        if (strummed && stroke.strumMs <= 0.0f) stroke.strumMs = 18.0f;   // a pick takes time to cross the strings
        if (muted)
        {
            stroke.palm = juce::jmax (0.85f, s.palm);
            velocity = juce::jmin (velocity, palmVelocity - 1);   // muted strokes are palm-muted
        }
        auto* group = startGroupWith (stroke, channel, notes.values.data(), notes.count, velocity, time, strummed && notes.count > 1);
        if (group == nullptr) return;
        auto& c = channels[(size_t) channel];
        for (int i = 0; i < group->outputCount; ++i)
        {
            // The outputs follow the strum's order; find each one's string.
            for (int k = 0; k < notes.count; ++k)
                if (notes.values[(size_t) k] == group->outputs[(size_t) i])
                {
                    c.strings[(size_t) strings.values[(size_t) k]] = { group->outputs[(size_t) i], group->onsets[(size_t) i], group->doubled };
                    break;
                }
        }
        *group = {};   // strings ring on their own, not tied to a key
    }

    void endString (int channel, int string, juce::int64 time)
    {
        if (string < 0 || string > 5) return;
        auto& note = channels[(size_t) channel].strings[(size_t) string];
        if (note.note >= 0) scheduleOffs (channel, note.note, note.onset, time, note.doubled);
        note = {};
    }

    void endStrings (int channel, juce::int64 time)
    {
        for (int string = 0; string < 6; ++string) endString (channel, string, time);
    }

    void showVoicing (const Voicing& v, bool goingUp, bool strummed)
    {
        last.count = 0;
        for (int n : v.notes) if (n >= 0) last.notes[(size_t) last.count++] = n;
        last.up = goingUp;
        last.strummed = strummed;
        last.palm = false;
        ++last.sequence;
    }

    // Notes tied to a key (Solo, Harmony, the zones' outside): they end when it's let go.
    struct Group;
    Group* playKeyed (const Settings& s, int channel, int key, const int* notes, int count, int velocity, juce::int64 time,
                      bool strummed, bool muted = false)
    {
        auto settings = s;
        if (muted)
        {
            settings.palm = juce::jmax (0.6f, s.palm);
            velocity = juce::jmin (velocity, palmVelocity - 1);
        }
        auto* group = startGroupWith (settings, channel, notes, count, velocity, time, strummed);
        if (group != nullptr) group->key = key;
        return group;
    }

    struct Group
    {
        bool used = false;
        int channel = 0;
        int key = -1;            // the key that ends it (-1: none)
        bool harmony = false;
        std::array<juce::uint8, 16> outputs {};
        std::array<juce::int64, 16> onsets {};   // when each output starts (strummed: later and later)
        int outputCount = 0;
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
    Group* startGroupWith (const Settings& s, int channel, const int* outputs, int outputCount, int velocity, juce::int64 time, bool strum)
    {
        if (outputCount <= 0) return nullptr;
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
        const bool goingUp = s.direction == up;
        auto strumTotal = strum ? samples (s.strumMs) : 0;
        // Velocity Switch FX: a slow strum above (or below) its velocity.
        if (strum && ((s.velocityFx == slowStrumHigh && velocity > s.fxVelocity) || (s.velocityFx == slowStrumLow && velocity < s.fxVelocity)))
            strumTotal = juce::jmax (strumTotal, samples (s.fxTimeMs));
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
            group->outputs[(size_t) i] = (juce::uint8) juce::jlimit (0, 127, note);
            juce::int64 at = time;
            if (n > 1)
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
            const auto pitch = group->outputs[(size_t) i];
            group->onsets[(size_t) i] = at;
            schedule ({ at, (juce::uint8) channel, pitch, velocityByte, detune, palm, false });
            // Palm mute: the string is stopped by the hand soon after it's struck (dropped if the
            // same pitch is struck again before then).
            if (palm)
                schedule ({ at + palmOff, (juce::uint8) channel, pitch, 0, 0, false, false, at });
            if (s.doubleMix > 0.0f)
            {
                const auto twin = at + samples (doubleDelayMs);
                schedule ({ twin, (juce::uint8) channel, pitch, velocityByte, (juce::int8) (detune + doubleDetuneCents), palm, true });
                if (palm)
                    schedule ({ twin + palmOff, (juce::uint8) channel, pitch, 0, 0, false, true, twin });
            }
        }
        return group;
    }

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
    std::array<Channel, 16> channels {};
    std::array<Group, 64> groups {};
    bool nextUp = false;
    LastPlayed last;
    juce::Random random { 1 };   // seeded: renders repeat exactly
};
