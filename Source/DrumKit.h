#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <cmath>
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
    struct Pads : juce::Component, juce::TooltipClient, private juce::MidiKeyboardState::Listener, private juce::Timer
    {
        explicit Pads (juce::MidiKeyboardState& s) : state (s)
        {
            state.addListener (this);
            setWantsKeyboardFocus (true);
            setMouseClickGrabsKeyboardFocus (true);
        }
        ~Pads() override { releaseAll(); state.removeListener (this); }
        void visibilityChanged() override
        {
            if (isVisible()) startTimerHz (60);   // the lights and the hits' motion
            else { stopTimer(); releaseAll(); }   // never a note left held
        }
        void focusLost (FocusChangeType) override { releaseAll(); repaint(); }
        void focusGained (FocusChangeType) override { repaint(); }

        int lowKey = 36, highKey = 60;
        bool kitView = true;   // the drum set; false: the pads
        std::map<int, juce::String> customNames;   // the user's names for this kit, by key
        std::function<void (int note, const juce::String& name)> onRename;   // an empty name: back to General MIDI's

        static constexpr int rowHeight = 30, gap = 4, hintHeight = 16;
        int firstOctave() const { return lowKey / 12; }
        int rows() const { return juce::jmax (1, highKey / 12 - lowKey / 12 + 1); }
        // The kit keeps its stage's shape: it grows with the window's width.
        int preferredHeight (int width) const
        {
            return kitView ? juce::jlimit (220, 460, juce::roundToInt ((float) width * 0.44f) + hintHeight) : rows() * (rowHeight + gap) - gap;
        }
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
            if (drawingRest) return 0.0f;
            float level = 0.0f;
            if (const int note = noteOf (piece); note >= 0) level = glow[(size_t) note];
            for (int note : pieces()[(size_t) piece].lights)
                if (note >= 0 && plays (note)) level = juce::jmax (level, glow[(size_t) note]);
            return level;
        }

        // 60 times a second: a key struck since flashes (even a hit shorter than a frame, on
        // and off in one block), then fades; a held one stays half lit.
        void timerCallback() override { tick(); }
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
                    level = on ? juce::jmax (level - 0.015f, 0.45f) : juce::jmax (0.0f, level - 0.035f);
                    changed = true;
                }
            }
            if (! changed) return;
            if (! kitView) { repaint(); return; }
            // Only where something moves (and where it moved last frame, to clear it).
            juce::Rectangle<float> area;
            for (int piece : drawOrder)
                if (noteOf (piece) >= 0 && lightOf (piece) > 0.0f)
                    area = area.isEmpty() ? areaOf (piece) : area.getUnion (areaOf (piece));
            const auto both = lastMoving.isEmpty() ? area : area.isEmpty() ? lastMoving : area.getUnion (lastMoving);
            lastMoving = area;
            if (! both.isEmpty()) repaint (both.getSmallestIntegerContainer());
        }

        void paint (juce::Graphics& g) override
        {
            if (kitView) paintKitCached (g); else paintPads (g);
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

        bool isHovered (int piece) const { return ! drawingRest && hovered == piece; }

        // The kit at rest is drawn once into a picture (again when the size or the kit's keys
        // change); each frame paints it, then only what moves: the pieces lit or under the mouse,
        // and any piece drawn after one of them that overlaps it (so the front stays in front).
        // About 1.8 ms a frame drawn in full; the picture and a piece or two cost a fraction.
        void paintKitCached (juce::Graphics& g)
        {
            const float pixels = g.getInternalContext().getPhysicalPixelScaleFactor();
            const auto key = juce::String (getWidth()) + "x" + juce::String (getHeight()) + "@" + juce::String (pixels, 2)
                           + ":" + juce::String (lowKey) + "-" + juce::String (highKey) + (hasKeyboardFocus (false) ? "f" : "");
            if (key != restKey || ! restImage.isValid())
            {
                restKey = key;
                restImage = juce::Image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) getWidth() * pixels)),
                                         juce::jmax (1, juce::roundToInt ((float) getHeight() * pixels)), true);
                juce::Graphics picture (restImage);
                picture.addTransform (juce::AffineTransform::scale (pixels));
                drawingRest = true;
                paintKit (picture, nullptr);
                drawingRest = false;
            }
            g.drawImage (restImage, getLocalBounds().toFloat());
            std::array<bool, pieceCount> moving {};
            bool any = false;
            juce::Rectangle<float> dirty;
            for (int piece : drawOrder)
            {
                const auto area = areaOf (piece);
                const bool active = noteOf (piece) >= 0 && (lightOf (piece) > 0.0f || isHovered (piece));
                if (active || (! dirty.isEmpty() && dirty.intersects (area)))
                {
                    moving[(size_t) piece] = true;
                    dirty = dirty.isEmpty() ? area : dirty.getUnion (area);
                    any = true;
                }
            }
            if (any) paintKit (g, &moving);
        }
        static constexpr std::array<int, 11> drawOrder { china, crash2, splash, crash, ride, kick, tomHigh, tomMid, tomFloor, snare, hatClosed };
        // Everything a piece draws (its body, stand, motion and label), in the component.
        juce::Rectangle<float> areaOf (int piece) const
        {
            auto r = placeOf (piece);
            if (piece == tomHigh || piece == tomMid || piece == tomFloor || piece == snare)
                r = r.withHeight (r.getHeight() + shellDepth (piece));
            return r.transformedBy (stage()).expanded (14.0f).withTrimmedTop (-18.0f);
        }

        void paintKit (juce::Graphics& g, const std::array<bool, pieceCount>* only)
        {
            auto wanted = [only] (int piece) { return only == nullptr || (*only)[(size_t) piece]; };
            const auto t = stage();
            const float scale = t.mat00;
            const juce::Colour accent (0xffe0a64b), ink (0xfff1e6c8), chrome (0xffb9bfcc), chromeDark (0xff5d6270);
            const float floorY = (float) getHeight() - hintHeight - 8.0f;
            auto place = [&] (int piece) { return placeOf (piece).transformedBy (t); };
            auto keyText = [] (int piece)
            {
                return juce::String::charToString ((juce::juce_wchar) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) pieces()[(size_t) piece].key));
            };
            // A hit's motion: a decaying swing, from the light's level (1 at the hit, fading out).
            auto swing = [] (float level, float speed) { return level * std::sin ((1.0f - level) * speed); };

            auto floorShadow = [&] (float x, float width)
            {
                if (only != nullptr) return;   // already in the picture (twice would darken it)
                g.setColour (juce::Colours::black.withAlpha (0.28f));
                g.fillEllipse (x - width / 2.0f, floorY - 4.0f * scale, width, 12.0f * scale);
            };
            auto stand = [&] (float x, float top)
            {
                floorShadow (x, 70.0f * scale);
                g.setGradientFill (juce::ColourGradient::horizontal (chromeDark, x - 2.0f, chrome, x + 2.0f));
                g.fillRect (x - 1.6f * scale - 0.5f, top, 3.2f * scale + 1.0f, floorY - top);
                g.setColour (chrome.withAlpha (0.8f));
                for (float side : { -1.0f, 1.0f })   // the tripod's legs
                    g.drawLine (x, floorY - 24.0f * scale, x + side * 30.0f * scale, floorY + 4.0f * scale, 1.6f);
                g.drawLine (x, floorY - 24.0f * scale, x, floorY + 6.0f * scale, 1.6f);
            };
            auto label = [&] (int piece, juce::Rectangle<float> r, bool below)
            {
                const auto text = juce::String (pieces()[(size_t) piece].name) + "  ";
                auto area = below ? juce::Rectangle<float> (r.getCentreX() - 60.0f, r.getBottom() + 2.0f, 120.0f, 13.0f)
                                  : juce::Rectangle<float> (r.getCentreX() - 60.0f, r.getY() - 15.0f, 120.0f, 13.0f);
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                const float width = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
                const auto pill = juce::Rectangle<float> (area.getCentreX() - width / 2.0f - 5.0f, area.getY() - 1.0f, width + 22.0f, 15.0f);
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.fillRoundedRectangle (pill, 7.0f);
                g.setColour (ink.withAlpha (0.85f));
                g.drawText (text, pill.withTrimmedLeft (5.0f), juce::Justification::centredLeft);
                const auto badge = juce::Rectangle<float> (pill.getRight() - 15.0f, pill.getY() + 1.5f, 12.0f, 12.0f);
                g.setColour (accent);
                g.fillRoundedRectangle (badge, 3.0f);
                g.setColour (juce::Colour (0xff1a1410));
                g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
                g.drawText (keyText (piece), badge, juce::Justification::centred);
            };

            auto cymbalShape = [&] (juce::Rectangle<float> r, float level, float tilt, bool hovered)
            {
                const auto rotation = juce::AffineTransform::rotation (tilt, r.getCentreX(), r.getCentreY());
                juce::Path disc;
                disc.addEllipse (r);
                disc.applyTransform (rotation);
                juce::ColourGradient brass (juce::Colour (0xfff0d27a).brighter (level * 0.5f), r.getCentreX() - r.getWidth() * 0.15f, r.getY(),
                                            juce::Colour (0xff6e4e1c), r.getRight(), r.getBottom(), false);
                brass.addColour (0.45, juce::Colour (0xffc99a3e));
                g.setGradientFill (brass);
                g.fillPath (disc);
                // Lathe grooves and the bell.
                g.setColour (juce::Colour (0xff5a3f14).withAlpha (0.35f));
                for (float k : { 0.82f, 0.64f, 0.46f })
                {
                    juce::Path ring;
                    ring.addEllipse (r.withSizeKeepingCentre (r.getWidth() * k, r.getHeight() * k));
                    ring.applyTransform (rotation);
                    g.strokePath (ring, juce::PathStrokeType (0.7f));
                }
                juce::Path bell;
                bell.addEllipse (r.withSizeKeepingCentre (r.getWidth() * 0.22f, r.getHeight() * 0.42f).translated (0.0f, -r.getHeight() * 0.08f));
                bell.applyTransform (rotation);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffff0c0), r.getCentreX(), r.getCentreY() - r.getHeight() * 0.2f,
                                                         juce::Colour (0xffb48632), r.getCentreX(), r.getCentreY() + r.getHeight() * 0.1f, false));
                g.fillPath (bell);
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.strokePath (disc, juce::PathStrokeType (1.0f));
                if (level > 0.0f)
                {
                    g.setColour (accent.withAlpha (level * 0.45f));
                    g.fillPath (disc);
                }
                if (hovered && ! drawingRest) { g.setColour (juce::Colours::white.withAlpha (0.14f)); g.fillPath (disc); }
            };
            auto cymbal = [&] (int piece)
            {
                if (noteOf (piece) < 0 || ! wanted (piece)) return;
                const auto r = place (piece);
                const float level = lightOf (piece);
                stand (r.getCentreX(), r.getCentreY());
                cymbalShape (r, level, swing (level, 14.0f) * 0.10f, isHovered (piece));
                label (piece, r, false);
            };
            auto lugs = [&] (juce::Rectangle<float> head, float depth)
            {
                g.setColour (chrome.withAlpha (0.9f));
                for (float k : { 0.12f, 0.37f, 0.63f, 0.88f })
                {
                    const float x = head.getX() + head.getWidth() * k;
                    const float bulge = std::sqrt (juce::jmax (0.0f, 1.0f - std::pow ((k - 0.5f) * 2.0f, 2.0f))) * head.getHeight() / 2.0f;
                    g.fillRoundedRectangle (x - 2.5f * scale, head.getCentreY() + bulge + depth * 0.25f, 5.0f * scale, depth * 0.5f, 1.5f);
                }
            };
            auto drum = [&] (int piece)
            {
                if (noteOf (piece) < 0 || ! wanted (piece)) return;
                const float level = lightOf (piece);
                const float pulse = 1.0f + 0.05f * swing (level, 18.0f);   // the head gives under the stick
                const auto head = place (piece);
                const float depth = shellDepth (piece) * scale;
                const juce::Colour shell = piece == snare ? juce::Colour (0xffc9ccd6) : juce::Colour (0xff9a2a26);
                if (piece == tomFloor)   // its three legs
                {
                    g.setColour (chrome.withAlpha (0.85f));
                    for (float k : { 0.12f, 0.88f })
                        g.drawLine (head.getX() + head.getWidth() * k, head.getCentreY() + depth * 0.5f,
                                    head.getX() + head.getWidth() * (k < 0.5f ? 0.02f : 0.98f), floorY, 2.0f);
                    floorShadow (head.getCentreX(), head.getWidth() * 1.1f);
                }
                if (piece == snare)
                    stand (head.getCentreX(), head.getCentreY() + depth);
                juce::Path body;
                body.startNewSubPath (head.getX(), head.getCentreY());
                body.lineTo (head.getX(), head.getCentreY() + depth);
                body.addCentredArc (head.getCentreX(), head.getCentreY() + depth, head.getWidth() / 2.0f, head.getHeight() / 2.0f,
                                    0.0f, 1.5f * juce::MathConstants<float>::pi, 0.5f * juce::MathConstants<float>::pi);
                body.lineTo (head.getRight(), head.getCentreY());
                body.closeSubPath();
                juce::ColourGradient wrap (shell.darker (0.7f), head.getX(), 0.0f, shell.darker (0.6f), head.getRight(), 0.0f, false);
                wrap.addColour (0.3, shell.brighter (0.25f));
                wrap.addColour (0.42, shell.brighter (0.6f));   // the shine on the lacquer
                wrap.addColour (0.55, shell);
                g.setGradientFill (wrap);
                g.fillPath (body);
                lugs (head, depth);
                // The hoops, top and bottom.
                g.setColour (chrome);
                juce::Path bottomHoop;
                bottomHoop.addCentredArc (head.getCentreX(), head.getCentreY() + depth, head.getWidth() / 2.0f, head.getHeight() / 2.0f,
                                          0.0f, 1.5f * juce::MathConstants<float>::pi, 0.5f * juce::MathConstants<float>::pi, true);
                g.strokePath (bottomHoop, juce::PathStrokeType (2.0f * scale + 0.5f));
                const auto drawn = head.withSizeKeepingCentre (head.getWidth() * pulse, head.getHeight() * pulse);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff6f2e8).interpolatedWith (accent, level * 0.75f),
                                                         drawn.getCentreX(), drawn.getY(),
                                                         juce::Colour (0xffcfc8b6).interpolatedWith (accent, level * 0.6f),
                                                         drawn.getCentreX(), drawn.getBottom(), false));
                g.fillEllipse (drawn);
                if (level > 0.2f)   // a ripple running out from the stick
                {
                    const float spread = 1.0f - level;
                    g.setColour (juce::Colours::white.withAlpha (level * 0.5f));
                    g.drawEllipse (drawn.withSizeKeepingCentre (drawn.getWidth() * (0.25f + spread), drawn.getHeight() * (0.25f + spread)), 1.2f);
                }
                if (isHovered (piece)) { g.setColour (juce::Colours::white.withAlpha (0.12f)); g.fillEllipse (drawn); }
                g.setColour (chrome);
                g.drawEllipse (drawn, 2.2f * scale + 0.5f);
                g.setColour (chromeDark);
                g.drawEllipse (drawn.reduced (2.0f * scale), 0.8f);
                label (piece, piece == snare ? head.withHeight (head.getHeight() + depth) : head, piece == snare);
            };

            // Back to front: the cymbals and their stands, the kick (with the toms on it), the
            // floor tom, the snare, the hi-hat.
            for (int piece : { china, crash2, splash, crash, ride })
                cymbal (piece);
            if (noteOf (kick) >= 0 && wanted (kick))
            {
                const float level = lightOf (kick);
                const float pulse = 1.0f + 0.03f * swing (level, 16.0f);
                const auto base = place (kick);
                const auto r = base.withSizeKeepingCentre (base.getWidth() * pulse, base.getHeight() * pulse);
                floorShadow (r.getCentreX(), r.getWidth() * 1.15f);
                // The spurs, and the tom holder rising from the shell.
                g.setColour (chrome.withAlpha (0.85f));
                g.drawLine (r.getX() + r.getWidth() * 0.12f, r.getCentreY() + r.getHeight() * 0.3f, r.getX() - 12.0f * scale, floorY, 2.0f);
                g.drawLine (r.getRight() - r.getWidth() * 0.12f, r.getCentreY() + r.getHeight() * 0.3f, r.getRight() + 12.0f * scale, floorY, 2.0f);
                for (int tom : { tomHigh, tomMid })
                    if (noteOf (tom) >= 0)
                    {
                        const auto h = place (tom);
                        g.drawLine (r.getCentreX(), r.getY() + 6.0f * scale, h.getCentreX(), h.getCentreY() + shellDepth (tom) * scale * 0.7f, 3.0f * scale);
                    }
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xffa83030), r.getX(), r.getY(), juce::Colour (0xff4a1414), r.getRight(), r.getBottom(), false));
                g.fillEllipse (r);
                const auto front = r.reduced (11.0f * scale);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2833).interpolatedWith (accent, level * 0.45f).brighter (isHovered (kick) ? 0.12f : 0.0f),
                                                         front.getCentreX(), front.getY(),
                                                         juce::Colour (0xff141218).interpolatedWith (accent, level * 0.3f), front.getCentreX(), front.getBottom(), false));
                g.fillEllipse (front);
                g.setColour (chrome);
                g.drawEllipse (r.reduced (5.0f * scale), 3.0f * scale + 0.5f);
                for (int i = 0; i < 10; ++i)   // the claws round the hoop
                {
                    const float angle = juce::MathConstants<float>::twoPi * (float) i / 10.0f;
                    const auto p = r.getCentre() + juce::Point<float> (std::sin (angle), -std::cos (angle)) * (r.getWidth() / 2.0f - 5.0f * scale);
                    g.fillEllipse (juce::Rectangle<float> (5.0f * scale, 5.0f * scale).withCentre (p));
                }
                g.setColour (ink.withAlpha (0.3f + level * 0.5f));
                g.setFont (juce::FontOptions (48.0f * scale, juce::Font::bold));
                g.drawText ("OKAMI", r.translated (0.0f, -r.getHeight() * 0.04f), juce::Justification::centred);
                // The pedal in front, its beater swinging in on a hit.
                const auto pedal = juce::Rectangle<float> (r.getCentreX() - 14.0f * scale, floorY - 10.0f * scale, 28.0f * scale, 14.0f * scale);
                g.setColour (chromeDark);
                g.fillRoundedRectangle (pedal, 2.0f);
                const float beat = level > 0.0f ? juce::jlimit (0.0f, 1.0f, level * 1.4f) : 0.0f;
                const auto beaterTop = juce::Point<float> (r.getCentreX() + (1.0f - beat) * 10.0f * scale, r.getBottom() - r.getHeight() * 0.32f + beat * 6.0f * scale);
                g.setColour (chrome);
                g.drawLine (juce::Line<float> (pedal.getCentre(), beaterTop), 2.0f * scale);
                g.setColour (juce::Colour (0xffe9e4d6));
                g.fillEllipse (juce::Rectangle<float> (12.0f * scale, 12.0f * scale).withCentre (beaterTop));
                label (kick, r.withTrimmedBottom (r.getHeight() * 0.24f), true);
            }
            for (int piece : { tomHigh, tomMid, tomFloor, snare })
                drum (piece);
            if (noteOf (hatClosed) >= 0 && wanted (hatClosed))
            {
                const auto r = place (hatClosed);
                const float level = lightOf (hatClosed);
                stand (r.getCentreX(), r.getCentreY());
                // An open hat rings and sways wider than a closed one (it stays in place: the
                // picture at rest is under it).
                const float open = ! drawingRest && plays (46) ? glow[46] : 0.0f;
                cymbalShape (r.translated (0.0f, 8.0f * scale), level * 0.5f, swing (open, 16.0f) * 0.04f, false);
                cymbalShape (r, level, swing (level, 20.0f) * (0.05f + 0.07f * open), isHovered (hatClosed));
                label (hatClosed, r, false);
            }
            if (only != nullptr) return;   // the line under the stage is in the picture
            // The hi-hat's other keys, the snare's and the ride's, under the stage.
            juce::StringArray more;
            for (int piece : { hatOpen, hatPedal, sideStick, clap, rideBell })
                if (noteOf (piece) >= 0)
                    more.add (keyText (piece) + " " + pieces()[(size_t) piece].name);
            g.setColour (ink.withAlpha (0.45f));
            g.setFont (juce::FontOptions (10.0f));
            g.drawFittedText (juce::String ("Also: ") + more.joinIntoString (juce::String::fromUTF8 ("  \xc2\xb7  "))
                                  + juce::String::fromUTF8 ("  \xc2\xb7  Shift: accent"),
                              getLocalBounds().removeFromBottom (hintHeight).reduced (6, 0), juce::Justification::centredLeft, 1, 0.8f);
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
        bool drawingRest = false;
        juce::Rectangle<float> lastMoving;   // where the kit moved last frame   // drawing the resting picture: no lights, no hover
        juce::Image restImage;
        juce::String restKey;       // what the picture was drawn for (size, pixels, the kit's keys, focus)
        std::vector<std::pair<int, int>> keysDown;   // (key code, note) held on the computer's keyboard
        void releaseMouse()
        {
            if (mouseNote >= 0) state.noteOff (1, mouseNote, 0.0f);
            mouseNote = -1;
        }
        void setHovered (int value) { if (value != hovered) { hovered = value; repaint(); } }
    };
}
