#pragma once

#include "GuitarEngine.h"
#include <juce_gui_basics/juce_gui_basics.h>

// The Guitar panel's strip: the chord's name, a six-string neck with where its notes lie, an
// arrow lit by each strum (down or up) and a light for palm-muted notes. Fed by the editor's
// timer from the processor's GuitarView; draws nothing until something is played.
class GuitarNeck : public juce::Component
{
public:
    struct Colours
    {
        juce::Colour text, dim, accent, panel, line;
    };
    explicit GuitarNeck (Colours c) : colours (c) { setInterceptsMouseClicks (false, false); }

    // A new chord or note (the processor's sequence moved on).
    void show (const int* notes, int count, int pitchClasses, int bass, bool up, bool strummed, bool palm, int fretPosition)
    {
        noteCount = juce::jlimit (0, 6, count);
        for (int i = 0; i < noteCount; ++i) shown[(size_t) i] = notes[i];
        name = chordName (pitchClasses, bass);
        int around = fretPosition;
        if (around == 0)   // as played: centre on the notes' own frets
            around = 3;
        frets = GuitarEngine::placeOnStrings (shown.data(), noteCount, around);
        arrowUp = up;
        if (strummed) arrowGlow = 1.0f;
        palmGlow = palm ? 1.0f : 0.0f;
        palmMuted = palm;
        age = 0.0f;
        repaint();
    }

    // Called by the editor's 20 Hz timer: the lights fade.
    void tick (float seconds)
    {
        const bool changing = arrowGlow > 0.0f || palmGlow > 0.0f || age < fadeAfter + 1.0f;
        arrowGlow = juce::jmax (0.0f, arrowGlow - seconds * 1.6f);
        palmGlow = juce::jmax (0.0f, palmGlow - seconds * 1.2f);
        age += seconds;
        if (changing) repaint();
    }

    // "C", "Am7", "G/B": the chord the pitch classes make, the bass after a slash when it isn't
    // the root. One note: its name. Nothing recognisable: an empty string.
    static juce::String chordName (int pitchClasses, int bass)
    {
        static const char* const noteNames[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        if (pitchClasses == 0) return {};
        if ((pitchClasses & (pitchClasses - 1)) == 0)   // one pitch class
            for (int pc = 0; pc < 12; ++pc)
                if (pitchClasses == 1 << pc) return noteNames[pc];
        struct Shape { const char* suffix; std::initializer_list<int> intervals; };
        static const Shape shapes[] = {
            { "", { 0, 4, 7 } }, { "m", { 0, 3, 7 } }, { "7", { 0, 4, 7, 10 } }, { "maj7", { 0, 4, 7, 11 } },
            { "m7", { 0, 3, 7, 10 } }, { "6", { 0, 4, 7, 9 } }, { "m6", { 0, 3, 7, 9 } }, { "sus4", { 0, 5, 7 } },
            { "sus2", { 0, 2, 7 } }, { "7sus4", { 0, 5, 7, 10 } }, { "add9", { 0, 2, 4, 7 } }, { "9", { 0, 2, 4, 7, 10 } },
            { "m9", { 0, 2, 3, 7, 10 } }, { "maj9", { 0, 2, 4, 7, 11 } }, { "dim", { 0, 3, 6 } }, { "dim7", { 0, 3, 6, 9 } },
            { "m7b5", { 0, 3, 6, 10 } }, { "aug", { 0, 4, 8 } }, { "mmaj7", { 0, 3, 7, 11 } }, { "5", { 0, 7 } },
        };
        // The bass as the root first, so G-B-D over G is "G", not "Bm/G".
        for (int attempt = 0; attempt < 13; ++attempt)
        {
            const int root = attempt == 0 ? bass : attempt - 1;
            if (root < 0 || (attempt > 0 && root == bass) || ! (pitchClasses & (1 << root))) continue;
            for (const auto& shape : shapes)
            {
                int mask = 0;
                for (int interval : shape.intervals) mask |= 1 << ((root + interval) % 12);
                if (mask != pitchClasses) continue;
                juce::String result = juce::String (noteNames[root]) + shape.suffix;
                if (bass >= 0 && bass != root) result << "/" << noteNames[bass];
                return result;
            }
        }
        return {};
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        const float fade = age < fadeAfter ? 1.0f : juce::jmax (0.35f, 1.0f - (age - fadeAfter));

        // Left: the chord's name, the strum arrow and the palm light.
        auto left = area.removeFromLeft (96.0f);
        g.setColour (colours.text.withAlpha (noteCount > 0 ? fade : 0.25f));
        g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        g.drawText (noteCount > 0 ? (name.isNotEmpty() ? name : juce::String ("?")) : juce::String::fromUTF8 ("\xe2\x80\x93"),
                    left.removeFromTop (left.getHeight() * 0.6f), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        auto lights = left;
        g.setColour (colours.dim.interpolatedWith (colours.accent, arrowGlow));
        g.drawText (juce::String::fromUTF8 (arrowUp ? "\xe2\x86\x91 UP" : "\xe2\x86\x93 DOWN"), lights.removeFromLeft (48.0f),
                    juce::Justification::centredLeft);
        g.setColour (colours.dim.withAlpha (0.5f).interpolatedWith (colours.accent, palmGlow));
        g.drawText ("PALM", lights, juce::Justification::centredLeft);

        // Right: the neck, high E on top as in tablature.
        area.removeFromLeft (8.0f);
        auto numbers = area.removeFromBottom (12.0f);
        auto neck = area.withTrimmedTop (12.0f).withTrimmedBottom (4.0f);
        int low = 99, high = 0;
        for (int f : frets) if (f > 0) { low = juce::jmin (low, f); high = juce::jmax (high, f); }
        const int first = low == 99 ? 1 : juce::jmax (1, juce::jmin (low, high - 4));
        const int columns = juce::jmax (5, high - first + 1);
        const float openRoom = 14.0f;
        auto board = neck.withTrimmedLeft (openRoom);
        const float columnWidth = board.getWidth() / (float) columns;
        const float rowGap = neck.getHeight() / 5.0f;

        g.setColour (colours.line);
        for (int c = 0; c <= columns; ++c)   // fret wires; the nut thicker
        {
            const float x = board.getX() + c * columnWidth;
            g.fillRect (x - (c == 0 && first == 1 ? 1.5f : 0.5f), neck.getY(), c == 0 && first == 1 ? 3.0f : 1.0f, neck.getHeight());
        }
        g.setColour (colours.dim);
        g.setFont (juce::FontOptions (10.0f));
        for (int c = 0; c < columns; ++c)
            g.drawText (juce::String (first + c), juce::Rectangle<float> (board.getX() + c * columnWidth, numbers.getY(), columnWidth, numbers.getHeight()),
                        juce::Justification::centred);

        for (int string = 0; string < 6; ++string)
        {
            const int row = 5 - string;   // the low E at the bottom
            const float y = neck.getY() + row * rowGap;
            g.setColour (colours.line.brighter (0.2f));
            g.fillRect (board.getX(), y - (string < 3 ? 0.75f : 0.5f), board.getWidth(), string < 3 ? 1.5f : 1.0f);
            const int fret = frets[(size_t) string];
            const auto dot = (palmMuted ? colours.dim : colours.accent).withAlpha (fade);
            if (noteCount == 0) continue;
            if (fret < 0)
            {
                g.setColour (colours.dim.withAlpha (0.6f * fade));
                g.setFont (juce::FontOptions (10.0f));
                g.drawText (juce::String::fromUTF8 ("\xc3\x97"), juce::Rectangle<float> (neck.getX(), y - 6.0f, openRoom - 2.0f, 12.0f),
                            juce::Justification::centred);
            }
            else if (fret == 0)
            {
                g.setColour (dot);
                g.drawEllipse (neck.getX() + 2.0f, y - 4.0f, 8.0f, 8.0f, 1.5f);
            }
            else
            {
                const float x = board.getX() + (fret - first + 0.5f) * columnWidth;
                const float size = juce::jmin (11.0f, rowGap * 0.9f);
                g.setColour (dot);
                g.fillEllipse (x - size / 2.0f, y - size / 2.0f, size, size);
            }
        }
    }

private:
    static constexpr float fadeAfter = 3.0f;   // seconds before a chord's dots dim
    Colours colours;
    std::array<int, 6> shown {};
    std::array<int, 6> frets { -1, -1, -1, -1, -1, -1 };
    int noteCount = 0;
    juce::String name;
    bool arrowUp = false, palmMuted = false;
    float arrowGlow = 0.0f, palmGlow = 0.0f, age = 99.0f;
};
