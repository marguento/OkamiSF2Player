#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <functional>
#include <iterator>
#include <map>

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

    // The pads: the kit's keys in rows of an octave (C to B), low octaves at the bottom like a
    // pad controller. A click plays the key (lower on the pad is louder, as on the keyboard), a
    // double-click renames it; each pad lights as its key sounds, whoever plays it.
    struct Pads : juce::Component, juce::TooltipClient, private juce::MidiKeyboardState::Listener
    {
        explicit Pads (juce::MidiKeyboardState& s) : state (s) { state.addListener (this); }
        ~Pads() override { state.removeListener (this); }

        int lowKey = 36, highKey = 60;
        std::map<int, juce::String> customNames;   // the user's names for this kit, by key
        std::function<void (int note, const juce::String& name)> onRename;   // an empty name: back to General MIDI's

        static constexpr int rowHeight = 30, gap = 4;
        int firstOctave() const { return lowKey / 12; }
        int rows() const { return juce::jmax (1, highKey / 12 - lowKey / 12 + 1); }
        int preferredHeight() const { return rows() * (rowHeight + gap) - gap; }
        bool plays (int note) const { return note >= lowKey && note <= highKey; }
        juce::String nameOf (int note) const
        {
            if (auto it = customNames.find (note); it != customNames.end() && it->second.isNotEmpty()) return it->second;
            if (auto gm = gmName (note); gm.isNotEmpty()) return gm;
            return juce::MidiMessage::getMidiNoteName (note, true, true, 3);
        }

        juce::Rectangle<float> padBounds (int note) const
        {
            const int row = rows() - 1 - (note / 12 - firstOctave());   // the lowest octave at the bottom
            const float width = ((float) getWidth() - gap * 11.0f) / 12.0f;
            return { (float) (note % 12) * (width + gap), (float) (row * (rowHeight + gap)), width, (float) rowHeight };
        }
        int noteAt (juce::Point<float> p) const
        {
            for (int note = firstOctave() * 12; note < (firstOctave() + rows()) * 12; ++note)
                if (padBounds (note).contains (p)) return plays (note) ? note : -1;
            return -1;
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
            }
        }

        juce::String getTooltip() override
        {
            const int note = noteAt (getMouseXYRelative().toFloat());
            if (note < 0) return {};
            return nameOf (note) + " (" + juce::MidiMessage::getMidiNoteName (note, true, true, 3)
                 + "): click to hear it, double-click to rename it";
        }

        void mouseMove (const juce::MouseEvent& e) override { setHovered (noteAt (e.position)); }
        void mouseExit (const juce::MouseEvent&) override { setHovered (-1); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            const int note = noteAt (e.position);
            if (note < 0 || e.getNumberOfClicks() > 1) return;
            const auto r = padBounds (note);
            const float velocity = juce::jlimit (0.2f, 1.0f, 0.35f + 0.65f * (e.position.y - r.getY()) / r.getHeight());
            state.noteOn (1, note, velocity);
            held = note;
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (held >= 0) state.noteOff (1, held, 0.0f);
            held = -1;
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
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
        int hovered = -1, held = -1;
        void setHovered (int note) { if (note != hovered) { hovered = note; repaint(); } }
    };
}
