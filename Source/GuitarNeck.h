#pragma once

#include "ChordName.h"
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
    // until something is played, and every change goes to `onShapeChanged`. A click on the name
    // plays it (`onAudition`).
    std::function<void (const std::array<int, 6>&)> onShapeChanged;
    std::function<void()> onAudition;

    // The drawn chord (shown while drawing, until something is played) and the strings' tuning.
    void setShape (const std::array<int, 6>& frets, const GuitarEngine::Tuning& strings = GuitarEngine::tunings[0])
    {
        if (frets == shapeFrets && strings == tuning) return;
        tuning = strings;
        shapeFrets = frets;
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
        shapeMode = false;   // what's played replaces the drawing
        int around = fretPosition;
        if (around == 0)   // as played: centre on the notes' own frets
            around = 3;
        frets = GuitarEngine::placeOnStrings (shown.data(), noteCount, around, tuning);
        arrowUp = up;
        if (strummed) arrowGlow = 1.0f;
        // The sweep: each string struck lights in the strum's order (down: from the low E).
        int order = 0;
        for (int k = 0; k < 6; ++k)
        {
            const int string = up ? 5 - k : k;
            if (frets[(size_t) string] < 0) continue;
            stringDelay[(size_t) string] = strummed ? 0.045f * (float) order++ : 0.0f;
            stringGlow[(size_t) string] = strummed ? 0.0f : 1.0f;
            stringPending[(size_t) string] = strummed;
        }
        palmGlow = palm ? 1.0f : 0.0f;
        palmMuted = palm;
        age = 0.0f;
        repaint();
    }

    // The Chord Position to shade on the board (0-3: I to IV; -1: none, as in Kbd or a non-chord mode).
    // The rhythm Pattern plays (empty: Pattern off), shown at the display's foot.
    void setPattern (const juce::String& rhythm)
    {
        if (rhythm == patternName) return;
        patternName = rhythm;
        repaint();
    }

    void setPosition (int position)
    {
        if (position == shownPosition) return;
        shownPosition = position;
        repaint();
    }

    // Called by the editor's 20 Hz timer: the lights fade.
    void tick (float seconds)
    {
        bool strings = false;
        for (size_t i = 0; i < 6; ++i)
        {
            if (stringPending[i] && (stringDelay[i] -= seconds) <= 0.0f) { stringPending[i] = false; stringGlow[i] = 1.0f; }
            else if (! stringPending[i]) stringGlow[i] = juce::jmax (0.0f, stringGlow[i] - seconds * 2.2f);
            strings = strings || stringPending[i] || stringGlow[i] > 0.0f;
        }
        const bool changing = strings || arrowGlow > 0.0f || palmGlow > 0.0f || age < fadeAfter + 1.0f;
        arrowGlow = juce::jmax (0.0f, arrowGlow - seconds * 1.6f);
        palmGlow = juce::jmax (0.0f, palmGlow - seconds * 1.2f);
        age += seconds;
        if (changing) repaint();
    }

    // "C", "Am7", "G/B": the chord the pitch classes make, the bass after a slash when it isn't
    // the root. One note: its name. Nothing recognisable: an empty string.
    static juce::String chordName (int pitchClasses, int bass) { return ChordName::name (pitchClasses, bass); }

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

        // Left: a dark display with the chord's name, the strum arrow and the palm light.
        auto left = area.removeFromLeft (nameWidth).reduced (0.0f, 6.0f);
        g.setColour (juce::Colour (0xff0d0e0b));
        g.fillRoundedRectangle (left, 6.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (left, 6.0f, 1.5f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (left.reduced (1.5f), 5.0f, 1.0f);
        auto screen = left.reduced (10.0f, 6.0f);
        // The strum's direction, big, on the right of the name; lit by each strum.
        auto arrowArea = screen.withTrimmedLeft (screen.getWidth() - 22.0f).withHeight (screen.getHeight() * 0.55f);
        if (! drawn)
        {
            g.setColour (juce::Colour (0xff6fe07a).withAlpha (0.18f + 0.82f * arrowGlow));
            g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
            g.drawText (juce::String::fromUTF8 (arrowUp ? "\xe2\x86\x91" : "\xe2\x86\x93"), arrowArea, juce::Justification::centred);
        }
        g.setColour (cream.withAlpha (noteCount > 0 ? juce::jmax (fade, 0.55f) : 0.25f));
        g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        auto nameArea = screen.removeFromTop (screen.getHeight() * 0.48f).withTrimmedRight (drawn ? 0.0f : 24.0f);
        g.drawFittedText (noteCount > 0 ? (name.isNotEmpty() ? name : juce::String ("?")) : juce::String::fromUTF8 ("\xe2\x80\x93"),
                          nameArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
        if (noteCount > 0 && ! (drawn && noteCount == 0))
        {
            // Where it's played and its notes, small: "fret 3 · D A D F#".
            int lowestFret = 99;
            for (int f : frets) if (f > 0) lowestFret = juce::jmin (lowestFret, f);
            juce::StringArray noteNames;
            std::array<int, 6> sorted {};
            int n = 0;
            for (size_t st = 0; st < 6; ++st) if (frets[st] >= 0) sorted[(size_t) n++] = tuning[st] + frets[st];
            std::sort (sorted.begin(), sorted.begin() + n);
            static const char* const letters[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
            for (int i = 0; i < n; ++i) noteNames.add (letters[sorted[(size_t) i] % 12]);
            g.setColour (cream.withAlpha (0.55f * juce::jmax (fade, 0.6f)));
            g.setFont (juce::FontOptions (10.5f));
            const auto where = lowestFret == 99 ? juce::String ("open") : "fret " + juce::String (lowestFret);
            g.drawFittedText (where + juce::String::fromUTF8 (" \xc2\xb7 ") + noteNames.joinIntoString (" "),
                              screen.removeFromTop (16.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        }
        if (drawn && noteCount == 0)   // in the display, clear of the fret numbers
        {
            g.setColour (cream.withAlpha (0.55f));
            g.setFont (juce::FontOptions (11.0f));
            g.drawFittedText ("Click the strings\nto draw a chord", screen.toNearestInt(), juce::Justification::centredLeft, 2);
        }
        else
        {
            // Two small lights: the strum's direction, and a palm-muted note.
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            auto lights = screen.removeFromBottom (14.0f);
            auto light = [&g] (juce::Rectangle<float> r, const juce::String& text, juce::Colour on, float glow)
            {
                const auto led = r.removeFromLeft (8.0f).withSizeKeepingCentre (6.0f, 6.0f);
                g.setColour (on.withAlpha (0.18f + 0.82f * glow));
                g.fillEllipse (led);
                if (glow > 0.05f) { g.setColour (on.withAlpha (0.35f * glow)); g.fillEllipse (led.expanded (3.0f)); }
                r.removeFromLeft (3.0f);
                g.setColour (cream.withAlpha (0.35f + 0.6f * glow));
                g.drawText (text, r, juce::Justification::centredLeft);
            };
            if (patternName.isNotEmpty())
            {
                g.setColour (juce::Colour (0xff6fe07a).withAlpha (0.85f));
                g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
                g.drawFittedText ("PATTERN " + juce::String::fromUTF8 ("\xc2\xb7 ") + patternName,
                                  lights.removeFromRight (lights.getWidth() * 0.62f).toNearestInt(), juce::Justification::centredRight, 1, 0.7f);
            }
            light (lights, "PALM", colours.accent, palmGlow);   // the direction is the big arrow above
        }

        // Right: a rosewood fingerboard, high E on top as in tablature, frets 1 to 12.
        const auto geometry = layout();
        auto numbers = geometry.numbers;
        auto neck = geometry.neck;
        auto board = geometry.board;
        const int first = 1, columns = fretCount;
        const float columnWidth = geometry.columnWidth, rowGap = geometry.rowGap, openRoom = board.getX() - neck.getX();
        const float edge = rowGap * 0.5f;
        const auto wood = board.withY (neck.getY() - edge).withBottom (neck.getBottom() + edge);
        g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff4b2d1d), wood.getY(), juce::Colour (0xff2c180e), wood.getBottom()));
        g.fillRect (wood);
        for (int grain = 0; grain < 14; ++grain)   // a little grain along the board
        {
            const float y = wood.getY() + wood.getHeight() * (0.04f + 0.068f * (float) grain);
            g.setColour (juce::Colours::black.withAlpha (grain % 3 == 0 ? 0.16f : 0.08f));
            g.drawHorizontalLine ((int) y, wood.getX(), wood.getRight());
        }
        g.setColour (juce::Colour (0xffd9cba6).withAlpha (0.55f));   // the binding along both edges
        g.fillRect (wood.withHeight (1.5f));
        g.fillRect (wood.withY (wood.getBottom() - 1.5f).withHeight (1.5f));

        // The Chord Position's frets, softly lit, with its numeral.
        if (shownPosition >= 0 && shownPosition < 4)
        {
            static constexpr int windows[4][2] = { { 1, 4 }, { 3, 6 }, { 5, 8 }, { 7, 10 } };
            static const char* const numerals[] = { "I", "II", "III", "IV" };
            const float x0 = board.getX() + (windows[shownPosition][0] - 1) * columnWidth;
            const float x1 = board.getX() + windows[shownPosition][1] * columnWidth;
            const auto band = juce::Rectangle<float> (x0, wood.getY(), x1 - x0, wood.getHeight());
            g.setColour (juce::Colour (0xfff1e6c8).withAlpha (0.13f));
            g.fillRect (band);
            g.setColour (juce::Colour (0xfff1e6c8).withAlpha (0.4f));
            g.drawRect (band, 1.0f);
            g.setColour (juce::Colour (0xfff1e6c8).withAlpha (0.75f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("POS " + juce::String (numerals[shownPosition]), band.withHeight (12.0f).translated (0.0f, -12.5f),
                        juce::Justification::centred);
        }
        auto inlay = [&g] (float x, float y)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffbf7ee), x - 1.5f, y - 1.5f, juce::Colour (0xffbdb6a6), x + 3.5f, y + 3.5f, true));
            g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
        };
        for (int c : { 3, 5, 7, 9 })
            inlay (board.getX() + (c - 0.5f) * columnWidth, neck.getCentreY());
        inlay (board.getX() + 11.5f * columnWidth, neck.getY() + 1.5f * rowGap);
        inlay (board.getX() + 11.5f * columnWidth, neck.getY() + 3.5f * rowGap);

        for (int c = 1; c <= columns; ++c)   // nickel fret wires
        {
            const float x = board.getX() + c * columnWidth;
            g.setColour (juce::Colour (0xff9a9a9e));
            g.fillRect (x - 1.25f, wood.getY(), 2.5f, wood.getHeight());
            g.setColour (juce::Colour (0xffeeeef0));
            g.fillRect (x - 0.5f, wood.getY(), 0.8f, wood.getHeight());
        }
        g.setColour (juce::Colour (0xffece2c6));   // the bone nut
        g.fillRect (board.getX() - 4.0f, wood.getY() - 1.0f, 5.0f, wood.getHeight() + 2.0f);
        g.setColour (juce::Colour (0xffb9ad8c));
        g.fillRect (board.getX() + 0.0f, wood.getY() - 1.0f, 1.0f, wood.getHeight() + 2.0f);

        g.setColour (colours.dim);
        g.setFont (juce::FontOptions (10.0f));
        for (int c = 0; c < columns; ++c)
            g.drawText (juce::String (first + c), juce::Rectangle<float> (board.getX() + c * columnWidth, numbers.getY(), columnWidth, numbers.getHeight()),
                        juce::Justification::centred);

        static constexpr float gauge[6] = { 2.6f, 2.2f, 1.8f, 1.4f, 1.1f, 0.9f };   // low E to high E
        for (int string = 0; string < 6; ++string)
        {
            const int row = 5 - string;   // the low E at the bottom
            const float y = neck.getY() + row * rowGap;
            const float thick = gauge[string];
            g.setColour (juce::Colours::black.withAlpha (0.35f));   // its shadow on the board
            g.fillRect (neck.getX(), y - thick / 2.0f + 1.2f, wood.getRight() - neck.getX(), thick);
            const float glow = stringGlow[(size_t) string];
            const auto metal = string < 3 ? juce::Colour (0xffc9a46b) : juce::Colour (0xffd9d9de);   // wound bronze, plain steel
            if (glow > 0.02f)   // struck: it shines and shivers for a moment
            {
                juce::Path wave;
                const float amplitude = 1.6f * glow, x0 = neck.getX(), x1 = wood.getRight();
                wave.startNewSubPath (x0, y);
                for (float x = x0; x <= x1; x += 6.0f)
                    wave.lineTo (x, y + amplitude * std::sin ((x - x0) / (x1 - x0) * juce::MathConstants<float>::pi)
                                         * std::sin (x * 0.35f + age * 90.0f));
                g.setColour (juce::Colour (0xfffff1c8).withAlpha (0.35f * glow));
                g.strokePath (wave, juce::PathStrokeType (thick + 3.0f * glow));
                g.setColour (metal.interpolatedWith (juce::Colour (0xfffff6dc), glow * 0.8f));
                g.strokePath (wave, juce::PathStrokeType (thick + 0.6f * glow));
            }
            else
            {
                g.setColour (metal);
                g.fillRect (neck.getX(), y - thick / 2.0f, wood.getRight() - neck.getX(), thick);
            }
        }
        for (int string = 0; string < 6; ++string)
        {
            if (noteCount == 0) break;
            const float y = neck.getY() + (5 - string) * rowGap;
            const int fret = frets[(size_t) string];
            const auto green = palmMuted ? juce::Colour (0xff9aa39b) : juce::Colour (0xff62d86e);
            if (fret < 0)
            {
                g.setColour (cream.withAlpha (0.7f * fade));
                g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                g.drawText (juce::String::fromUTF8 ("\xc3\x97"), juce::Rectangle<float> (neck.getX(), y - 7.0f, openRoom - 4.0f, 14.0f),
                            juce::Justification::centred);
            }
            else if (fret == 0)
            {
                g.setColour (green.withAlpha (fade));
                g.drawEllipse (neck.getX() + 1.0f, y - 5.5f, 11.0f, 11.0f, 2.0f);
            }
            else
            {
                const float x = board.getX() + (fret - first + 0.5f) * columnWidth;
                const float size = juce::jmin (17.0f, rowGap * 0.95f);
                const auto dot = juce::Rectangle<float> (x - size / 2.0f, y - size / 2.0f, size, size);
                g.setColour (green.withAlpha (0.3f * fade));   // a soft glow
                g.fillEllipse (dot.expanded (3.0f));
                g.setGradientFill (juce::ColourGradient (green.brighter (0.6f).withAlpha (fade), x - size * 0.2f, y - size * 0.25f,
                                                         green.darker (0.5f).withAlpha (fade), x + size * 0.5f, y + size * 0.5f, true));
                g.fillEllipse (dot);
                g.setColour (juce::Colours::black.withAlpha (0.5f * fade));
                g.drawEllipse (dot, 1.0f);
            }
        }
    }

private:
    static constexpr float fadeAfter = 3.0f;   // seconds before a chord's dots dim
    static constexpr float nameWidth = 130.0f;
    static inline const juce::Colour cream { 0xfff1e6c8 };   // the display's text
    static constexpr int fretCount = 12;
    struct Geometry { juce::Rectangle<float> neck, board, numbers; float columnWidth, rowGap; };
    Geometry layout() const
    {
        auto area = getLocalBounds().toFloat().withTrimmedLeft (nameWidth + 10.0f);
        Geometry g;
        g.numbers = area.removeFromBottom (12.0f);
        g.neck = area.withTrimmedTop (20.0f).withTrimmedBottom (11.0f);   // room for the position label above, the numbers below
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
    std::array<float, 6> stringGlow {}, stringDelay {};
    std::array<bool, 6> stringPending {};
    int shownPosition = -1;
    juce::String patternName;
};
