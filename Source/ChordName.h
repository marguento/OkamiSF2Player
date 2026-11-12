#pragma once

#include <juce_core/juce_core.h>

// Names a chord from its pitch classes, for the Guitar panel (what's drawn on the neck or
// played on the keyboard). The method follows tonal's chord detection (tonaljs.github.io,
// MIT): every pitch class is tried as the root against a dictionary of chord types; the bass
// as root wins, else the chord is named over its bass ("C/E"); a missing perfect fifth is
// allowed, as guitarists often leave it out. Two notes are named too (C5, C(no5), an interval).
namespace ChordName
{
    struct Type { int mask; const char* suffix; };   // mask: bit n = n semitones above the root

    constexpr int bits (std::initializer_list<int> intervals)
    {
        int mask = 0;
        for (int i : intervals) mask |= 1 << (i % 12);
        return mask;
    }

    // Common chord types, plainest first (so among equal matches the simpler name wins).
    inline const Type types[] = {
        { bits ({ 0, 4, 7 }), "" },            { bits ({ 0, 3, 7 }), "m" },
        { bits ({ 0, 4, 7, 10 }), "7" },       { bits ({ 0, 4, 7, 11 }), "maj7" },
        { bits ({ 0, 3, 7, 10 }), "m7" },      { bits ({ 0, 5, 7 }), "sus4" },
        { bits ({ 0, 2, 7 }), "sus2" },        { bits ({ 0, 4, 7, 9 }), "6" },
        { bits ({ 0, 3, 7, 9 }), "m6" },       { bits ({ 0, 2, 4, 7 }), "add9" },
        { bits ({ 0, 2, 3, 7 }), "madd9" },    { bits ({ 0, 3, 6 }), "dim" },
        { bits ({ 0, 4, 8 }), "aug" },         { bits ({ 0, 3, 6, 9 }), "dim7" },
        { bits ({ 0, 3, 6, 10 }), "m7b5" },    { bits ({ 0, 3, 7, 11 }), "mMaj7" },
        { bits ({ 0, 5, 7, 10 }), "7sus4" },   { bits ({ 0, 2, 7, 10 }), "7sus2" },
        { bits ({ 0, 2, 4, 7, 10 }), "9" },    { bits ({ 0, 2, 4, 7, 11 }), "maj9" },
        { bits ({ 0, 2, 3, 7, 10 }), "m9" },   { bits ({ 0, 2, 4, 7, 9 }), "6/9" },
        { bits ({ 0, 2, 3, 7, 9 }), "m6/9" },  { bits ({ 0, 2, 5, 7, 10 }), "9sus4" },
        { bits ({ 0, 4, 5, 7 }), "add11" },    { bits ({ 0, 3, 5, 7 }), "madd11" },
        { bits ({ 0, 2, 4, 5, 7, 10 }), "11" }, { bits ({ 0, 2, 3, 5, 7, 10 }), "m11" },
        { bits ({ 0, 2, 4, 7, 9, 10 }), "13" }, { bits ({ 0, 2, 4, 7, 9, 11 }), "maj13" },
        { bits ({ 0, 2, 3, 7, 9, 10 }), "m13" }, { bits ({ 0, 1, 4, 7, 10 }), "7b9" },
        { bits ({ 0, 3, 4, 7, 10 }), "7#9" },  { bits ({ 0, 4, 6, 7, 10 }), "7#11" },
        { bits ({ 0, 4, 6, 10 }), "7b5" },     { bits ({ 0, 4, 8, 10 }), "7#5" },
        { bits ({ 0, 4, 8, 11 }), "maj7#5" },  { bits ({ 0, 4, 6, 7, 11 }), "maj7#11" },
        { bits ({ 0, 4, 7, 8, 10 }), "7b13" }, { bits ({ 0, 4, 6 }), "b5" },
        { bits ({ 0, 4, 7, 10, 2, 6 }), "9#11" }, { bits ({ 0, 1, 4, 7, 8, 10 }), "7b9b13" },
        { bits ({ 0, 7 }), "5" },
    };

    inline const char* const noteNames[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    inline const char* const intervalNames[] = { "", "m2", "M2", "m3", "M3", "4th", "tritone", "5th", "m6", "M6", "m7", "M7" };

    inline int rotate (int pitchClasses, int root)
    {
        int relative = 0;
        for (int pc = 0; pc < 12; ++pc)
            if (pitchClasses & (1 << pc)) relative |= 1 << ((pc - root + 12) % 12);
        return relative;
    }

    // "C", "Am7", "G/B", "D7(no5)"... ; one note: its name; nothing to say: an empty string.
    inline juce::String name (int pitchClasses, int bass)
    {
        pitchClasses &= 0xfff;
        if (pitchClasses == 0) return {};
        const int count = juce::countNumberOfBits ((juce::uint32) pitchClasses);
        if (bass < 0 || ! (pitchClasses & (1 << bass)))
            for (bass = 0; ! (pitchClasses & (1 << bass)); ++bass) {}
        if (count == 1) return noteNames[bass];

        // Best match: exact before a missing fifth, the bass as root before an inversion,
        // the dictionary's order (simplest) last.
        int bestScore = -1;
        juce::String best;
        constexpr int fifth = 1 << 7;
        for (int attempt = 0; attempt < 12; ++attempt)
        {
            const int root = (bass + attempt) % 12;
            if (! (pitchClasses & (1 << root))) continue;
            const int relative = rotate (pitchClasses, root);
            for (int t = 0; t < (int) std::size (types); ++t)
            {
                const auto& type = types[t];
                const bool exact = relative == type.mask;
                // Without its fifth: the type has a perfect fifth, the notes don't, the rest matches,
                // and its third is there (what tells major from minor; a sus chord without its
                // fifth is just an interval).
                const bool noFifth = ! exact && (type.mask & fifth) && ! (relative & fifth)
                                  && (type.mask & ~fifth) == relative && (relative & bits ({ 3, 4 })) != 0;
                if (! exact && ! noFifth) continue;
                const int score = (exact ? 2000 : 1000) + (root == bass ? 500 : 0) - t;
                if (score <= bestScore) continue;
                bestScore = score;
                juce::String result = juce::String (noteNames[root]) + type.suffix;
                // A triad without its fifth says so; larger chords leave the fifth out silently.
                if (noFifth && juce::countNumberOfBits ((juce::uint32) type.mask) <= 3) result << "(no5)";
                if (root != bass) result << "/" << noteNames[bass];
                best = result;
            }
        }
        if (best.isNotEmpty()) return best;
        if (count == 2)   // two notes that make no chord: the interval above the bass
        {
            const int other = rotate (pitchClasses, bass) & ~1;
            for (int i = 1; i < 12; ++i)
                if (other == 1 << i) return juce::String (noteNames[bass]) + " " + intervalNames[i];
        }
        return {};
    }
}
