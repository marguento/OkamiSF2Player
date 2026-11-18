#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <functional>
#include <iterator>
#include <map>
#include <vector>

// Drum kits: which instruments are kits, what each key plays on the General MIDI map, and the
// DRUM KIT panel's pads (one per key the kit plays, a row per octave, lit as they sound).
namespace DrumKit
{
    // General MIDI's percussion map (keys 35-81), plus the GS keys around it (27-34, 82-87).
    inline juce::String gmName (int note)
    {
        static const char* const names[] = {
            "High Q", "Slap", "Scratch Push", "Scratch Pull", "Sticks", "Square Click", "Metronome", "Metro Bell",   // 27-34
            "Kick 2", "Kick", "Side Stick", "Snare", "Clap", "Snare 2", "Low Floor Tom", "Closed Hat",                // 35-42
            "High Floor Tom", "Pedal Hat", "Low Tom", "Open Hat", "Low-Mid Tom", "High-Mid Tom", "Crash", "High Tom", // 43-50
            "Ride", "China", "Ride Bell", "Tambourine", "Splash", "Cowbell", "Crash 2", "Vibraslap",                  // 51-58
            "Ride 2", "High Bongo", "Low Bongo", "Mute Conga", "Open Conga", "Low Conga", "High Timbale", "Low Timbale", // 59-66
            "High Agogo", "Low Agogo", "Cabasa", "Maracas", "Short Whistle", "Long Whistle", "Short Guiro", "Long Guiro", // 67-74
            "Claves", "High Block", "Low Block", "Mute Cuica", "Open Cuica", "Mute Triangle", "Open Triangle",          // 75-81
            "Shaker", "Jingle Bell", "Bell Tree", "Castanets", "Mute Surdo", "Open Surdo"                              // 82-87
        };
        return juce::isPositiveAndBelow (note - 27, (int) std::size (names)) ? juce::String (names[note - 27]) : juce::String();
    }

    // A kit: General MIDI's drum bank (128), or an instrument named one ("TR-808 Kit", "Rock Drums", "Latin Percussion").
    inline bool isKit (int bank, const juce::String& name)
    {
        if (bank == 128) return true;
        juce::StringArray words;
        words.addTokens (name.toLowerCase(), " -_/()[].,:", "");
        // "Drum" alone is one instrument (General MIDI's Taiko Drum, Synth Drum), and Steel Drums too.
        if (words.contains ("kit") || words.contains ("drumkit") || words.contains ("drumset") || words.contains ("percussion"))
            return true;
        return words.contains ("drums") && ! words.contains ("steel");
    }

    // A drum set's pieces, each played by the first of its keys the kit has (General MIDI's map),
    // with the computer key that plays it.
    struct Piece
    {
        const char* name;
        std::array<int, 2> notes;   // the key it plays (the first the kit has); -1 for none
        char key;                   // its computer key
        std::array<int, 3> lights;  // other keys that light it (articulations: the rim, an open hat)
    };
    enum PieceId { crash, crash2, splash, ride, china, hatClosed, hatOpen, hatPedal, tomHigh, tomMid,
                   tomFloor, snare, sideStick, clap, kick, rideBell, pieceCount };
    inline const std::array<Piece, pieceCount>& pieces()
    {
        static const std::array<Piece, pieceCount> list { {
            { "Crash",       { 49, -1 }, 'q', { -1, -1, -1 } },
            { "Crash 2",     { 57, -1 }, 'w', { -1, -1, -1 } },
            { "Splash",      { 55, -1 }, 'y', { -1, -1, -1 } },
            { "Ride",        { 51, 59 }, 'e', { 53, -1, -1 } },
            { "China",       { 52, -1 }, 't', { -1, -1, -1 } },
            { "Closed Hat",  { 42, -1 }, 'a', { 46, 44, -1 } },
            { "Open Hat",    { 46, -1 }, 's', { -1, -1, -1 } },
            { "Pedal Hat",   { 44, -1 }, 'd', { -1, -1, -1 } },
            { "High Tom",    { 50, 48 }, 'f', { -1, -1, -1 } },
            { "Mid Tom",     { 47, 45 }, 'g', { -1, -1, -1 } },
            { "Floor Tom",   { 43, 41 }, 'h', { -1, -1, -1 } },
            { "Snare",       { 38, 40 }, 'x', { 37, 39, -1 } },
            { "Side Stick",  { 37, -1 }, 'c', { -1, -1, -1 } },
            { "Clap",        { 39, -1 }, 'v', { -1, -1, -1 } },
            { "Kick",        { 36, 35 }, 'z', { -1, -1, -1 } },
            { "Ride Bell",   { 53, -1 }, 'r', { -1, -1, -1 } },
        } };
        return list;
    }

    // The DRUM KIT panel: the kit drawn as a drum set (Kit) or as pads, one per key the kit
    // plays in rows of an octave, the lowest at the bottom (Pads). A click plays (lower on a pad
    // is louder), a double-click on a pad renames it, and once clicked the panel takes the
    // computer's keys (Z kick, X snare, A/S/D hi-hat, F/G/H toms, Q/W crashes, E ride…). Every
    // piece and pad lights as its key sounds, whoever plays it.
    struct Pads : juce::Component, juce::TooltipClient, private juce::MidiKeyboardState::Listener
    {
        explicit Pads (juce::MidiKeyboardState& s) : state (s)
        {
            state.addListener (this);
            setWantsKeyboardFocus (true);
            setMouseClickGrabsKeyboardFocus (true);
        }
        ~Pads() override { releaseAll(); state.removeListener (this); }
        void visibilityChanged() override { if (! isVisible()) releaseAll(); }   // never a note left held
        void focusLost (FocusChangeType) override { releaseAll(); repaint(); }
        void focusGained (FocusChangeType) override { repaint(); }

        int lowKey = 36, highKey = 60;
        bool kitView = true;   // the drum set; false: the pads
        std::map<int, juce::String> customNames;   // the user's names for this kit, by key
        std::function<void (int note, const juce::String& name)> onRename;   // an empty name: back to General MIDI's

        static constexpr int rowHeight = 30, gap = 4, kitHeight = 250, hintHeight = 16;
        int firstOctave() const { return lowKey / 12; }
        int rows() const { return juce::jmax (1, highKey / 12 - lowKey / 12 + 1); }
        int preferredHeight() const { return kitView ? kitHeight : rows() * (rowHeight + gap) - gap; }
        bool plays (int note) const { return note >= lowKey && note <= highKey; }
        juce::String nameOf (int note) const
        {
            if (auto it = customNames.find (note); it != customNames.end() && it->second.isNotEmpty()) return it->second;
            if (auto gm = gmName (note); gm.isNotEmpty()) return gm;
            return juce::MidiMessage::getMidiNoteName (note, true, true, 3);
        }
        // The key a piece plays in this kit (-1: the kit has none of its keys).
        int noteOf (int piece) const
        {
            for (int note : pieces()[(size_t) piece].notes)
                if (note >= 0 && plays (note)) return note;
            return -1;
        }
        // The key a computer key plays (-1: none).
        int noteForKey (juce::juce_wchar c) const
        {
            c = juce::CharacterFunctions::toLowerCase (c);
            for (int p = 0; p < pieceCount; ++p)
                if (pieces()[(size_t) p].key == c) return noteOf (p);
            return -1;
        }
        char keyForNote (int note) const
        {
            for (int p = 0; p < pieceCount; ++p)
                if (noteOf (p) == note) return pieces()[(size_t) p].key;
            return 0;
        }

        // ---- Pads
        juce::Rectangle<float> padBounds (int note) const
        {
            const int row = rows() - 1 - (note / 12 - firstOctave());   // the lowest octave at the bottom
            const float width = ((float) getWidth() - gap * 11.0f) / 12.0f;
            return { (float) (note % 12) * (width + gap), (float) (row * (rowHeight + gap)), width, (float) rowHeight };
        }

        // ---- Kit: each piece's place on a 1000 x 440 stage, scaled to fit and centred.
        juce::AffineTransform stage() const
        {
            const float height = (float) getHeight() - hintHeight;   // the keys' line under the stage
            const float scale = juce::jmin ((float) getWidth() / 1000.0f, height / 440.0f);
            return juce::AffineTransform::scale (scale).translated (((float) getWidth() - 1000.0f * scale) / 2.0f,
                                                                    (height - 440.0f * scale) / 2.0f);
        }
        // Where a piece is (cymbals and heads as an ellipse; the kick as its front head); empty if not drawn.
        static juce::Rectangle<float> placeOf (int piece)
        {
            switch (piece)
            {
                case crash:     return { 130, 40, 240, 48 };
                case crash2:    return { 590, 40, 190, 38 };
                case splash:    return { 420, 30, 120, 24 };
                case ride:      return { 740, 120, 250, 50 };
                case china:     return { 830, 30, 150, 30 };
                case hatClosed: return { 20, 175, 175, 34 };
                case tomHigh:   return { 345, 145, 130, 40 };
                case tomMid:    return { 500, 145, 140, 44 };
                case tomFloor:  return { 720, 250, 180, 56 };
                case snare:     return { 185, 255, 180, 52 };
                case kick:      return { 375, 225, 210, 210 };
                default:        return {};
            }
        }
        // The piece under a point (the front-most), -1 for none.
        int pieceAt (juce::Point<float> p) const
        {
            const auto at = p.transformedBy (stage().inverted());
            for (int piece : { hatClosed, snare, tomHigh, tomMid, tomFloor, crash, crash2, splash, ride, china, kick })
            {
                auto r = placeOf (piece);
                if (piece != kick && piece != hatClosed && piece != crash && piece != crash2 && piece != splash
                    && piece != ride && piece != china)
                    r = r.withHeight (r.getHeight() + shellDepth (piece));   // a drum: its shell too
                if (noteOf (piece) >= 0 && r.expanded (6.0f).contains (at)) return piece;
            }
            return -1;
        }
        static float shellDepth (int piece) { return piece == tomFloor ? 105.0f : piece == snare ? 42.0f : 52.0f; }
        float lightOf (int piece) const
        {
            float level = 0.0f;
            if (const int note = noteOf (piece); note >= 0) level = glow[(size_t) note];
            for (int note : pieces()[(size_t) piece].lights)
                if (note >= 0 && plays (note)) level = juce::jmax (level, glow[(size_t) note]);
            return level;
        }

        // Called on the editor's timer: a key struck since flashes (even a hit shorter than a
        // tick, on and off in one block), then fades; a held one stays half lit.
        void tick()
        {
            bool changed = false;
            for (int note = 0; note < 128; ++note)
            {
                auto& level = glow[(size_t) note];
                if (struck[(size_t) note].exchange (false, std::memory_order_relaxed)) { level = 1.0f; changed = true; }
                else if (level > 0.0f)
                {
                    const bool on = state.isNoteOnForChannels (0xffff, note);
                    level = on ? juce::jmax (level - 0.04f, 0.45f) : juce::jmax (0.0f, level - 0.12f);
                    changed = true;
                }
            }
            if (changed) repaint();
        }

        void paint (juce::Graphics& g) override
        {
            if (kitView) paintKit (g); else paintPads (g);
            if (hasKeyboardFocus (false))   // it takes the computer's keys now
            {
                g.setColour (juce::Colour (0xffe0a64b).withAlpha (0.55f));
                g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 5.0f, 1.0f);
            }
        }

        void paintPads (juce::Graphics& g)
        {
            const juce::Colour accent (0xffe0a64b), ink (0xfff1e6c8);
            for (int note = firstOctave() * 12; note < (firstOctave() + rows()) * 12; ++note)
            {
                const auto r = padBounds (note);
                if (! plays (note))
                {
                    g.setColour (juce::Colours::white.withAlpha (0.03f));
                    g.fillRoundedRectangle (r, 4.0f);
                    continue;
                }
                const float level = glow[(size_t) note];
                g.setColour (juce::Colour (0xff232b47).interpolatedWith (accent, level * 0.7f).brighter (note == hovered ? 0.12f : 0.0f));
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (juce::Colours::black.withAlpha (0.4f));
                g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
                g.setColour (ink.withAlpha (0.92f));
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.drawFittedText (nameOf (note), r.reduced (4.0f, 2.0f).withTrimmedBottom (9.0f).toNearestInt(), juce::Justification::centred, 2, 0.55f);
                g.setColour (ink.withAlpha (0.4f));
                g.setFont (juce::FontOptions (9.0f));
                g.drawText (juce::MidiMessage::getMidiNoteName (note, true, true, 3), r.reduced (3.0f, 1.0f), juce::Justification::centredBottom);
                if (const char key = keyForNote (note))
                {
                    g.setColour (accent.withAlpha (0.8f));
                    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
                    g.drawText (juce::String::charToString ((juce::juce_wchar) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key)),
                                r.reduced (4.0f, 2.0f), juce::Justification::topRight);
                }
            }
        }

        void paintKit (juce::Graphics& g)
        {
            const auto t = stage();
            const float scale = t.mat00;
            const juce::Colour accent (0xffe0a64b), ink (0xfff1e6c8), chrome (0xff8a90a0);
            auto place = [&] (int piece) { return placeOf (piece).transformedBy (t); };
            auto stand = [&] (float x, float top, float bottom)
            {
                g.setColour (chrome.withAlpha (0.55f));
                g.drawLine ((float) x, top, (float) x, bottom, 2.0f * scale + 0.5f);
                g.drawLine (x, bottom, x - 26.0f * scale, bottom + 8.0f * scale, 1.5f);
                g.drawLine (x, bottom, x + 26.0f * scale, bottom + 8.0f * scale, 1.5f);
            };
            auto label = [&] (int piece, juce::Rectangle<float> r, bool below)
            {
                const char key = pieces()[(size_t) piece].key;
                const auto text = juce::String (pieces()[(size_t) piece].name);
                auto area = below ? juce::Rectangle<float> (r.getCentreX() - 60.0f, r.getBottom() + 1.0f, 120.0f, 13.0f)
                                  : juce::Rectangle<float> (r.getCentreX() - 60.0f, r.getY() - 14.0f, 120.0f, 13.0f);
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                g.setColour (ink.withAlpha (0.75f));
                const auto keyText = juce::String::charToString ((juce::juce_wchar) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key));
                g.drawText (text + "  ", area, juce::Justification::centred);
                const float width = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text + "  ");
                const auto badge = juce::Rectangle<float> (area.getCentreX() + width / 2.0f - 2.0f, area.getY() + 0.5f, 12.0f, 12.0f);
                g.setColour (accent.withAlpha (0.85f));
                g.drawRoundedRectangle (badge, 2.5f, 1.0f);
                g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
                g.drawText (keyText, badge, juce::Justification::centred);
            };
            auto cymbal = [&] (int piece)
            {
                if (noteOf (piece) < 0) return;
                const auto r = place (piece);
                const float level = lightOf (piece);
                stand (r.getCentreX(), r.getCentreY(), (float) getHeight() - hintHeight - 8.0f);
                juce::ColourGradient brass (juce::Colour (0xffd9b45a).brighter (level * 0.6f), r.getCentreX(), r.getCentreY(),
                                            juce::Colour (0xff7a5a22), r.getRight(), r.getCentreY(), true);
                g.setGradientFill (brass);
                g.fillEllipse (r);
                if (level > 0.0f) { g.setColour (accent.withAlpha (level * 0.55f)); g.fillEllipse (r.expanded (4.0f * level)); }
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.drawEllipse (r, 1.0f);
                g.setColour (juce::Colour (0xfff5e3b0).withAlpha (0.8f));
                g.fillEllipse (r.withSizeKeepingCentre (r.getHeight() * 0.5f, r.getHeight() * 0.25f));
                if (piece == hovered) { g.setColour (juce::Colours::white.withAlpha (0.15f)); g.fillEllipse (r); }
                label (piece, r, false);
            };
            auto drum = [&] (int piece)
            {
                if (noteOf (piece) < 0) return;
                const auto head = place (piece);
                const float depth = shellDepth (piece) * scale;
                const float level = lightOf (piece);
                const juce::Colour shell = piece == snare ? juce::Colour (0xffb8bcc8) : juce::Colour (0xff8e2b2b);
                juce::Path body;
                body.startNewSubPath (head.getX(), head.getCentreY());
                body.lineTo (head.getX(), head.getCentreY() + depth);
                // The shell's bottom edge: the lower half of the head's ellipse, from 9 o'clock round by 6 to 3.
                body.addCentredArc (head.getCentreX(), head.getCentreY() + depth, head.getWidth() / 2.0f, head.getHeight() / 2.0f,
                                    0.0f, 1.5f * juce::MathConstants<float>::pi, 0.5f * juce::MathConstants<float>::pi);
                body.lineTo (head.getRight(), head.getCentreY());
                body.closeSubPath();
                g.setGradientFill (juce::ColourGradient::horizontal (shell.darker (0.5f), head.getX(), shell.brighter (0.2f), head.getCentreX()));
                g.fillPath (body);
                g.setColour (chrome.withAlpha (0.6f));
                g.strokePath (body, juce::PathStrokeType (1.0f));
                for (float y : { 0.15f, 0.85f })   // the hoops
                    g.drawLine (head.getX(), head.getCentreY() + depth * y, head.getRight(), head.getCentreY() + depth * y, 1.0f);
                g.setColour (juce::Colour (0xffe9e4d6).interpolatedWith (accent, level * 0.8f).brighter (piece == hovered ? 0.1f : 0.0f));
                g.fillEllipse (head);
                g.setColour (chrome);
                g.drawEllipse (head, 1.5f);
                label (piece, piece == snare ? head.withHeight (head.getHeight() + depth) : head, piece == snare);   // the toms' above: the kick is below
            };

            // Back to front: stands and cymbals, the kick, the toms on it, the floor tom, snare, hi-hat.
            for (int piece : { china, crash2, splash, crash, ride })
                cymbal (piece);
            if (noteOf (kick) >= 0)
            {
                const auto r = place (kick);
                const float level = lightOf (kick);
                g.setColour (juce::Colour (0xff6e1f1f));
                g.fillEllipse (r);
                g.setColour (juce::Colour (0xff1d1b22).interpolatedWith (accent, level * 0.5f).brighter (hovered == kick ? 0.12f : 0.0f));
                g.fillEllipse (r.reduced (10.0f * scale));
                g.setColour (chrome);
                g.drawEllipse (r.reduced (5.0f * scale), 2.0f);
                g.setColour (ink.withAlpha (0.35f + level * 0.5f));
                g.setFont (juce::FontOptions (22.0f * scale * 2.2f, juce::Font::bold));
                g.drawText ("OKAMI", r, juce::Justification::centred);
                label (kick, r.withTrimmedBottom (r.getHeight() * 0.22f), true);   // inside, under OKAMI
            }
            for (int piece : { tomHigh, tomMid, tomFloor, snare })
                drum (piece);
            if (noteOf (hatClosed) >= 0)
            {
                auto r = place (hatClosed);
                const float level = lightOf (hatClosed);
                stand (r.getCentreX(), r.getCentreY(), (float) getHeight() - hintHeight - 8.0f);
                for (float dy : { 8.0f * scale, 0.0f })   // the bottom and top hats
                {
                    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd9b45a).brighter (level * 0.6f), r.getCentreX(), r.getCentreY() + dy,
                                                             juce::Colour (0xff7a5a22), r.getRight(), r.getCentreY() + dy, true));
                    g.fillEllipse (r.translated (0.0f, dy));
                    g.setColour (juce::Colours::black.withAlpha (0.35f));
                    g.drawEllipse (r.translated (0.0f, dy), 1.0f);
                }
                if (level > 0.0f) { g.setColour (accent.withAlpha (level * 0.55f)); g.fillEllipse (r.expanded (4.0f * level)); }
                if (hovered == hatClosed) { g.setColour (juce::Colours::white.withAlpha (0.15f)); g.fillEllipse (r); }
                label (hatClosed, r, false);
            }
            // The hi-hat's other keys, the snare's and the ride's, under the stage.
            juce::StringArray more;
            for (int piece : { hatOpen, hatPedal, sideStick, clap, rideBell })
                if (noteOf (piece) >= 0)
                    more.add (juce::String::charToString ((juce::juce_wchar) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) pieces()[(size_t) piece].key))
                              + " " + pieces()[(size_t) piece].name);
            g.setColour (ink.withAlpha (0.45f));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText ((hasKeyboardFocus (false) ? juce::String ("Keys on: ") : juce::String ("Click the kit to play it with your keys. Also: "))
                            + more.joinIntoString (juce::String::fromUTF8 ("  \xc2\xb7  ")),
                        getLocalBounds().removeFromBottom (hintHeight).reduced (6, 0), juce::Justification::centredLeft);
        }

        juce::String getTooltip() override
        {
            if (kitView)
            {
                const int piece = pieceAt (getMouseXYRelative().toFloat());
                if (piece < 0) return {};
                const int note = noteOf (piece);
                return nameOf (note) + " (" + juce::MidiMessage::getMidiNoteName (note, true, true, 3) + ", key "
                     + juce::String::charToString ((juce::juce_wchar) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) pieces()[(size_t) piece].key)) + ")";
            }
            const int note = noteAt (getMouseXYRelative().toFloat());
            if (note < 0) return {};
            return nameOf (note) + " (" + juce::MidiMessage::getMidiNoteName (note, true, true, 3)
                 + "): click to hear it, double-click to rename it";
        }

        int noteAt (juce::Point<float> p) const
        {
            if (kitView)
            {
                const int piece = pieceAt (p);
                return piece >= 0 ? noteOf (piece) : -1;
            }
            for (int note = firstOctave() * 12; note < (firstOctave() + rows()) * 12; ++note)
                if (padBounds (note).contains (p)) return plays (note) ? note : -1;
            return -1;
        }

        void mouseMove (const juce::MouseEvent& e) override { setHovered (kitView ? pieceAt (e.position) : noteAt (e.position)); }
        void mouseExit (const juce::MouseEvent&) override { setHovered (-1); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            const int note = noteAt (e.position);
            if (note < 0 || (! kitView && e.getNumberOfClicks() > 1)) return;   // a pad's double-click renames; the kit's quick hits all play
            float velocity = 0.85f;
            if (! kitView)
            {
                const auto r = padBounds (note);
                velocity = juce::jlimit (0.2f, 1.0f, 0.35f + 0.65f * (e.position.y - r.getY()) / r.getHeight());
            }
            releaseMouse();
            state.noteOn (1, note, velocity);
            mouseNote = note;
        }
        void mouseUp (const juce::MouseEvent&) override { releaseMouse(); }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (kitView) return;   // renaming is the pads'
            const int note = noteAt (e.position);
            if (note < 0 || onRename == nullptr) return;
            auto* window = new juce::AlertWindow ("Rename pad", juce::MidiMessage::getMidiNoteName (note, true, true, 3)
                                                  + juce::String::fromUTF8 (" \xc2\xb7 empty for General MIDI's name (")
                                                  + (gmName (note).isNotEmpty() ? gmName (note) : juce::String ("none")) + ")",
                                                  juce::MessageBoxIconType::NoIcon);
            window->addTextEditor ("name", nameOf (note));
            window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            window->enterModalState (true, juce::ModalCallbackFunction::create (
                [safe = juce::Component::SafePointer<Pads> (this), window, note] (int result)
                {
                    const auto text = window->getTextEditorContents ("name").trim().substring (0, 40);
                    if (result == 1 && safe != nullptr && safe->onRename != nullptr)
                        safe->onRename (note, text == gmName (note) ? juce::String() : text);
                }), true);
        }

        // The computer's keys: a mapped key strikes its piece (Shift: an accent); auto-repeat
        // doesn't strike again, and letting go ends the note.
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown() || key.getModifiers().isAltDown()) return false;
            const int note = noteForKey (key.getTextCharacter());
            if (note < 0) return false;
            const int code = key.getKeyCode();
            for (const auto& [heldCode, heldNote] : keysDown)
                if (heldCode == code) return true;   // auto-repeat
            keysDown.push_back ({ code, note });
            state.noteOn (1, note, key.getModifiers().isShiftDown() ? 1.0f : 0.8f);
            return true;
        }
        bool keyStateChanged (bool isKeyDown) override
        {
            if (isKeyDown) return false;
            bool any = false;
            for (auto it = keysDown.begin(); it != keysDown.end();)
                if (! juce::KeyPress::isKeyCurrentlyDown (it->first))
                {
                    state.noteOff (1, it->second, 0.0f);
                    it = keysDown.erase (it);
                    any = true;
                }
                else
                    ++it;
            return any;
        }
        void releaseAll()
        {
            releaseMouse();
            for (const auto& [code, note] : keysDown) state.noteOff (1, note, 0.0f);
            keysDown.clear();
        }

    private:
        juce::MidiKeyboardState& state;
        std::array<float, 128> glow {};
        std::array<std::atomic<bool>, 128> struck {};   // set from any thread (the audio thread's MIDI), read on the timer
        // MidiKeyboardState::Listener: may run on the audio thread, so it only sets a flag.
        void handleNoteOn (juce::MidiKeyboardState*, int, int note, float) override
        {
            if (juce::isPositiveAndBelow (note, 128)) struck[(size_t) note].store (true, std::memory_order_relaxed);
        }
        void handleNoteOff (juce::MidiKeyboardState*, int, int, float) override {}
        int hovered = -1, mouseNote = -1;
        std::vector<std::pair<int, int>> keysDown;   // (key code, note) held on the computer's keyboard
        void releaseMouse()
        {
            if (mouseNote >= 0) state.noteOff (1, mouseNote, 0.0f);
            mouseNote = -1;
        }
        void setHovered (int value) { if (value != hovered) { hovered = value; repaint(); } }
    };
}
