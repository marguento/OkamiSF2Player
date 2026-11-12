#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include <array>

// The "?" button's guide: a card over the whole window with four pages (Start, Guitar,
// Controls, About). Esc, the close button or a click outside the card closes it.
class Guide : public juce::Component
{
public:
    struct Colours { juce::Colour background, panel, line, text, dim, accent; };

    enum Page { start = 0, guitar = 1, controls = 2, about = 3 };

    Guide (Colours c, juce::String versionText) : colours (c), version (std::move (versionText))
    {
        setWantsKeyboardFocus (true);
        const char* const names[] = { "Start", "Guitar", "Controls", "About" };
        for (int i = 0; i < 4; ++i)
        {
            auto& tab = tabs[(size_t) i];
            tab.setButtonText (names[i]);
            tab.setClickingTogglesState (false);
            tab.setColour (juce::TextButton::buttonColourId, colours.panel);
            tab.setColour (juce::TextButton::buttonOnColourId, colours.accent);
            tab.setColour (juce::TextButton::textColourOffId, colours.text);
            tab.setColour (juce::TextButton::textColourOnId, colours.background);
            tab.onClick = [this, i] { show ((Page) i); };
            addAndMakeVisible (tab);
        }
        closeButton.setColour (juce::TextButton::buttonColourId, colours.panel);
        closeButton.setColour (juce::TextButton::textColourOffId, colours.text);
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);
        link.setColour (juce::HyperlinkButton::textColourId, colours.accent);
        link.setFont (juce::FontOptions (13.0f), false, juce::Justification::centredLeft);
        content.addChildComponent (link);
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
        show (start);
    }

    std::function<void()> onClose;

    void show (Page page)
    {
        current = page;
        for (size_t i = 0; i < tabs.size(); ++i)
            tabs[i].setToggleState ((int) i == (int) page, juce::dontSendNotification);
        content.items = itemsFor (page);
        link.setVisible (page == about);
        viewport.setViewPosition (0, 0);
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.6f));
        g.setColour (colours.background);
        g.fillRoundedRectangle (card.toFloat(), 10.0f);
        g.setColour (colours.line);
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 10.0f, 1.0f);
        g.setColour (colours.text);
        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.drawText ("Guide", card.getX() + 20, card.getY() + 14, 200, 24, juce::Justification::centredLeft);
    }

    void resized() override
    {
        card = getLocalBounds().reduced (juce::jmax (16, getWidth() / 14), 24);
        auto area = card.reduced (20, 14);
        auto top = area.removeFromTop (26);
        closeButton.setBounds (top.removeFromRight (26));
        top.removeFromRight (10);
        for (int i = 3; i >= 0; --i)
        {
            tabs[(size_t) i].setBounds (top.removeFromRight (84));
            top.removeFromRight (4);
        }
        area.removeFromTop (14);
        viewport.setBounds (area);
        content.layOut (area.getWidth() - viewport.getScrollBarThickness() - 4);
        if (current == about)
            link.setBounds (0, content.getHeight() - 26, content.getWidth(), 22);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! card.contains (e.getPosition()) && onClose) onClose();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey && onClose) { onClose(); return true; }
        return false;
    }

private:
    // A heading and its text; a heading alone opens a group.
    struct Item { juce::String heading, body; };

    struct Content : juce::Component
    {
        explicit Content (Guide& g) : guide (g) {}
        std::vector<Item> items;
        Guide& guide;

        juce::TextLayout layoutOf (const Item& item, int width) const
        {
            juce::AttributedString text;
            text.setWordWrap (juce::AttributedString::byWord);
            if (item.heading.isNotEmpty())
                text.append (item.heading + (item.body.isEmpty() ? "" : "\n"),
                             juce::FontOptions (item.body.isEmpty() ? 15.0f : 13.5f, juce::Font::bold),
                             item.body.isEmpty() ? guide.colours.accent : guide.colours.text);
            if (item.body.isNotEmpty())
                text.append (item.body, juce::FontOptions (13.0f), guide.colours.dim);
            juce::TextLayout layout;
            layout.createLayout (text, (float) width);
            return layout;
        }

        void layOut (int width)
        {
            int height = 0;
            for (const auto& item : items)
                height += (int) std::ceil (layoutOf (item, width).getHeight()) + (item.body.isEmpty() ? 6 : 12);
            setSize (width, height + (guide.current == about ? 34 : 4));
        }

        void paint (juce::Graphics& g) override
        {
            float y = 0;
            for (const auto& item : items)
            {
                if (item.body.isEmpty() && y > 0) y += 8;   // a group's heading: a little air above
                const auto layout = layoutOf (item, getWidth());
                layout.draw (g, { 0.0f, y, (float) getWidth(), layout.getHeight() });
                y += std::ceil (layout.getHeight()) + (item.body.isEmpty() ? 6.0f : 12.0f);
            }
        }
    };

    std::vector<Item> itemsFor (Page page) const
    {
        switch (page)
        {
            case start: return {
                { "Three steps", "" },
                { "1. Open a SoundFont", "Click the file menu at the top (or drop a .sf2 / .sf3 on the window). Recent files are kept in that menu." },
                { "2. Choose an instrument", "The instrument menu, the < > arrows, or type in Search. The star keeps it in Favorites; the triangle plays a note to hear it." },
                { "3. Play", "From your MIDI keyboard or a clip. In Ableton, the M key turns the computer keyboard into a piano (A S D F... white keys, W E T Y U black keys, Z X octave)." },
                { "Good to know", "" },
                { "Nothing changes the sound until you move it", "Every control starts where the SoundFont plays exactly as it was made. Each panel says which values leave it untouched." },
                { "Fold what you don't use", "Click a panel's title (GUITAR, REVERB, PLAY...) to fold it, and Keyboard to hide the keyboard. The window remembers." },
                { "Hints", "Point at any control and its explanation shows at the top of the window. Turn it off in the ? menu." },
            };
            case guitar: return {
                { "The Guitar panel", "" },
                { "When it shows", "By itself for a guitar (General MIDI 24-31, or an instrument named one). Force it On or Off with the Guitar menu in PLAY's header." },
                { "Presets", "The menu next to GUITAR: Acoustic Strum, Campfire, Double-Tracked, Slow Ballad, Muted Rock, Folk Pattern, Fingerpicking, Strum Keys. Off sets everything back to neutral." },
                { "Modes (the buttons under the neck)", "" },
                { "Solo", "Single notes, as you play them: melodies and lead lines." },
                { "Harmony", "Hold a chord below middle C (silent); every melody note above gets a second voice under it, from that chord." },
                { "Chords", "Notes played together are voiced like a guitarist would hold them and strummed." },
                { "Auto Chords", "One key plays its major chord; add keys to change it. The lowest key is the root: C + Eb = Cm, C + Bb = C7, C + B = Cmaj7, C + F = Csus4. Adding a key while holding changes the chord at once." },
                { "Bass & Chord", "Below middle C: single bass notes. From middle C: strummed chords. One hand each." },
                { "Bass & Pick", "The lowest note first, then the other strings picked one by one." },
                { "Shape", "Draw a chord on the neck; every key plays that shape moved to it, like a barre chord sliding up the neck." },
                { "Key Chords", "Choose a key and a scale at the top; each key plays its chord in that key (in C major: C, Dm, Em, F, G, Am, Bdim). Notes outside the key play their major chord." },
                { "Strum Keys", "Left hand holds a chord below middle C (silent). The white keys from middle C are the pick: C down, D up, E / F muted down / up, G the bass string, A / B the high strings down / up. Black keys damp the strings." },
                { "Pattern", "Hold a chord and the rhythm chosen at the top plays by itself, locked to Ableton's tempo: Folk, Pop, Reggae, Fingerpicking and more." },
                { "The neck", "" },
                { "Drawing chords", "Click a string at a fret to place a finger (again to lift it). Left of the nut, a click switches a string between open and not played. The name appears as you draw; click it to hear the chord." },
                { "Reading it", "The display names the last chord played (G/B: B in the bass). The lights show the strum's direction and palm-muted notes." },
            };
            case controls: return {
                { "Guitar", "" },
                { "Strum", "How long a chord takes from the first string to the last: 0 to 250 ms. Slow strums from about 100." },
                { "Way", "Strum down (low to high), up, or alternate." },
                { "Tune", "Standard, Drop D, Drop C, Open G or DADGAD: chords are voiced for those strings." },
                { "Fret", "Plays every chord at that fret position. Off: the notes as you press them." },
                { "Palm", "Soft notes (velocity under 45) are muted by the hand: shorter and darker." },
                { "Humanize", "Small random changes in timing, velocity and tuning, so it doesn't sound mechanical." },
                { "Double", "A second take of every note, slightly late and out of tune, on the other side: double-tracking." },
                { "Velocity strings", "Soft playing catches only the high strings, hard playing all six, like a real pick." },
                { "Tremolo, Rate", "A stereo tremolo; the menu at the top right locks it to Ableton's tempo." },
                { "Instrument", "" },
                { "Gain", "The plugin's output level." },
                { "Tone", "A gentle low-pass filter: darker towards the left. Off at the right." },
                { "Layer", "A second instrument from the same SoundFont: Layer plays both together, Split plays it below the split key." },
                { "Effects", "" },
                { "Reverb and Chorus", "The SoundFont's own effects, with presets. Effects to separate outputs sends them raw to Ableton instead, for your own effects." },
                { "Play", "" },
                { "Attack, Release", "How fast notes start and fade, x1 is the SoundFont's own." },
                { "Velocity", "Linear plays velocities as they come; + makes soft playing louder, - softer." },
                { "Transpose, Fine", "Shift the pitch by semitones and cents." },
                { "Bend", "The pitch wheel's range." },
                { "Glide, Mode", "Slide between notes; Poly, Mono or Legato." },
            };
            case about: return {
                { "Okami SF2 Player " + version, "A SoundFont player for Ableton and any AU or VST3 host, made to play the HQ Orchestral SoundFont collection faithfully, with a Guitar panel for guitar SoundFonts." },
                { "Built on", "FluidSynth (LGPL-2.1), the synth engine. JUCE, the plugin framework. libvorbis and libogg (BSD), for .sf3 files. tonal (MIT), the method behind the chord names." },
                { "Thanks", "To Edirol HQ Orchestral, the plugin that inspired this one (no affiliation with Roland). To the authors of the SoundFonts you play." },
                { "License", "Free and open source under the GNU AGPL v3. The full credits are in CREDITS.md." },
            };
        }
        return {};
    }

    Colours colours;
    juce::String version;
    Page current = start;
    juce::Rectangle<int> card;
    std::array<juce::TextButton, 4> tabs;
    juce::TextButton closeButton { juce::String::fromUTF8 ("\xc3\x97") };
    juce::HyperlinkButton link { "github.com/marguento/OkamiSF2Player", juce::URL ("https://github.com/marguento/OkamiSF2Player") };
    Content content { *this };
    juce::Viewport viewport;
};
