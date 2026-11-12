#pragma once

#include "GuitarEngine.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// The Guitar panel's strip: the chord's name, a six-string neck with where its notes lie, an
// arrow lit by each strum (down or up) and a light for palm-muted notes. Fed by the editor's
// timer from the processor's GuitarView; draws nothing until something is played.
class GuitarNeck : public juce::Component, public juce::SettableTooltipClient
{
public:
    struct Colours
    {
        juce::Colour text, dim, accent, panel, line;
    };
    explicit GuitarNeck (Colours c) : colours (c) {}

    // Drawing chords: a click on a string at a fret puts a finger there (again: lifts it); left
    // of the nut, a string toggles open and not played. The drawn chord is shown (and named)
    // while `shapeMode` is on, and every change goes to `onShapeChanged`. A click on the name
    // plays the chord (`onAudition`).
    std::function<void (const std::array<int, 6>&)> onShapeChanged;
    std::function<void()> onAudition;

    void setShape (const std::array<int, 6>& frets, bool shapeModeOn, const GuitarEngine::Tuning& strings = GuitarEngine::tunings[0])
    {
        if (frets == shapeFrets && shapeModeOn == shapeMode && strings == tuning) return;
        tuning = strings;
        shapeFrets = frets;
        shapeMode = shapeModeOn;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto at = e.position;
        if (at.x < nameWidth)
        {
            if (onAudition) onAudition();
            return;
        }
        const auto geometry = layout();
        const int string = 5 - juce::jlimit (0, 5, juce::roundToInt ((at.y - geometry.neck.getY()) / geometry.rowGap));
        auto frets = shapeFrets;
        if (at.x < geometry.board.getX())
            frets[(size_t) string] = frets[(size_t) string] == 0 ? -1 : 0;   // open, or not played
        else
        {
            const int fret = 1 + juce::jlimit (0, fretCount - 1, (int) ((at.x - geometry.board.getX()) / geometry.columnWidth));
            frets[(size_t) string] = frets[(size_t) string] == fret ? -1 : fret;
        }
        shapeFrets = frets;
        shapeMode = true;
        if (onShapeChanged) onShapeChanged (frets);
        repaint();
    }

    // A new chord or note (the processor's sequence moved on).
    void show (const int* notes, int count, int pitchClasses, int bass, bool up, bool strummed, bool palm, int fretPosition,
               const GuitarEngine::Tuning& strings = GuitarEngine::tunings[0])
    {
        tuning = strings;
        noteCount = juce::jlimit (0, 6, count);
        for (int i = 0; i < noteCount; ++i) shown[(size_t) i] = notes[i];
        name = chordName (pitchClasses, bass);
        int around = fretPosition;
        if (around == 0)   // as played: centre on the notes' own frets
            around = 3;
        frets = GuitarEngine::placeOnStrings (shown.data(), noteCount, around, tuning);
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
        float fade = age < fadeAfter ? 1.0f : juce::jmax (0.35f, 1.0f - (age - fadeAfter));
        // Shape mode shows the drawn chord, always bright; otherwise the last one played.
        auto frets = this->frets;
        auto name = this->name;
        int noteCount = this->noteCount;
        bool drawn = false;
        if (shapeMode)
        {
            drawn = true;
            fade = 1.0f;
            frets = shapeFrets;
            std::array<int, 6> notes {};
            noteCount = 0;
            int classes = 0;
            for (size_t s = 0; s < 6; ++s)
                if (frets[s] >= 0) { notes[(size_t) noteCount++] = tuning[s] + frets[s]; classes |= 1 << ((tuning[s] + frets[s]) % 12); }
            std::sort (notes.begin(), notes.begin() + noteCount);
            name = noteCount > 0 ? chordName (classes, notes[0] % 12) : juce::String();
        }

        // Left: the chord's name, the strum arrow and the palm light.
        auto left = area.removeFromLeft (nameWidth);
        g.setColour (colours.text.withAlpha (noteCount > 0 ? fade : 0.25f));
        g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        g.drawText (noteCount > 0 ? (name.isNotEmpty() ? name : juce::String ("?")) : juce::String::fromUTF8 ("\xe2\x80\x93"),
                    left.removeFromTop (left.getHeight() * 0.6f), juce::Justification::centredLeft);
        if (drawn && noteCount == 0)
        {
            g.setColour (colours.dim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText ("Click the strings to draw a chord", area.withTrimmedLeft (8.0f).removeFromBottom (12.0f),
                        juce::Justification::centredRight);
        }
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        auto lights = left;
        g.setColour (colours.dim.interpolatedWith (colours.accent, arrowGlow));
        g.drawText (juce::String::fromUTF8 (arrowUp ? "\xe2\x86\x91 UP" : "\xe2\x86\x93 DOWN"), lights.removeFromLeft (48.0f),
                    juce::Justification::centredLeft);
        g.setColour (colours.dim.withAlpha (0.5f).interpolatedWith (colours.accent, palmGlow));
        g.drawText ("PALM", lights, juce::Justification::centredLeft);

        // Right: the neck, high E on top as in tablature, frets 1 to 12.
        const auto geometry = layout();
        auto numbers = geometry.numbers;
        auto neck = geometry.neck;
        auto board = geometry.board;
        const int first = 1, columns = fretCount;
        const float columnWidth = geometry.columnWidth, rowGap = geometry.rowGap, openRoom = board.getX() - neck.getX();
        for (int c : { 3, 5, 7, 9, 12 })   // the inlay dots of a real neck
        {
            g.setColour (colours.line.withAlpha (0.6f));
            const float x = board.getX() + (c - 0.5f) * columnWidth;
            g.fillEllipse (x - 2.5f, neck.getCentreY() - 2.5f, 5.0f, 5.0f);
        }

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
                g.drawEllipse (neck.getX() + 1.0f, y - 5.0f, 10.0f, 10.0f, 1.6f);
            }
            else
            {
                const float x = board.getX() + (fret - first + 0.5f) * columnWidth;
                const float size = juce::jmin (16.0f, rowGap * 0.85f);
                g.setColour (dot);
                g.fillEllipse (x - size / 2.0f, y - size / 2.0f, size, size);
            }
        }
    }

private:
    static constexpr float fadeAfter = 3.0f;   // seconds before a chord's dots dim
    static constexpr float nameWidth = 110.0f;
    static constexpr int fretCount = 12;
    struct Geometry { juce::Rectangle<float> neck, board, numbers; float columnWidth, rowGap; };
    Geometry layout() const
    {
        auto area = getLocalBounds().toFloat().withTrimmedLeft (nameWidth + 8.0f);
        Geometry g;
        g.numbers = area.removeFromBottom (12.0f);
        g.neck = area.withTrimmedTop (12.0f).withTrimmedBottom (4.0f);
        g.board = g.neck.withTrimmedLeft (16.0f);
        g.columnWidth = g.board.getWidth() / (float) fretCount;
        g.rowGap = g.neck.getHeight() / 5.0f;
        return g;
    }
    std::array<int, 6> shapeFrets { -1, -1, -1, -1, -1, -1 };
    bool shapeMode = false;
    GuitarEngine::Tuning tuning = GuitarEngine::tunings[0];
    Colours colours;
    std::array<int, 6> shown {};
    std::array<int, 6> frets { -1, -1, -1, -1, -1, -1 };
    int noteCount = 0;
    juce::String name;
    bool arrowUp = false, palmMuted = false;
    float arrowGlow = 0.0f, palmGlow = 0.0f, age = 99.0f;
};
