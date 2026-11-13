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
                { "When it shows", "By itself for a guitar (General MIDI 24-31, or an instrument named one). The button beside the instrument says Guitar or Standard (· auto when detected); click it to choose for an instrument that's detected wrong. Remembered for that instrument." },
                { "Presets", "The menu next to GUITAR. A preset holds every control of the panel, CONTROLLERS included: Acoustic Strum, Campfire, Double-Tracked, Slow Ballad, High Strings, Folk Pattern, Muted Rock, Fingerpicking, Bass & Pick, Lead Solo, Octave Lead; Off (Direct) plays the SoundFont as it is. Save current as... keeps yours (the same name replaces it) for every project; Delete removes one." },
                { "The keyboard's zones", "" },
                { "Main zone, E1 to B4", "Where you play notes (Solo, Harmony) or take chords (Chords, Bass & Chord, Bass & Pick). Chords are recognized from the keys: C E G is C, one key is its major chord, C + Eb is C minor, C + Bb is C7." },
                { "Repeat zones, C1 to D#1 and C5 to C6", "Tinted on the keyboard. They repeat what the Main zone holds: in Chords, C, E and G strum up; D, F, A and B down; the black keys strum muted. Keep the chord with one hand and play the rhythm with the other." },
                { "Modes (the buttons under the neck)", "" },
                { "Solo", "Notes as you play them. C5 to C6 repeat the notes held. C1 to D#1 are key switches: hold one and the Main zone plays with its articulation (Muted, H.O., Slide, Tremolo by default; Sustain 1-2 Mono and Velo Mute too). The sustain pedal can take one as well." },
                { "Harmony", "One note at a time, with the interval chosen in its options: a fourth down, a fifth or an octave up, power chords, two octaves." },
                { "Chords", "The chord taken in the Main zone is voiced like a guitarist would hold it and strummed. The Repeat keys strum it again." },
                { "Bass & Chord", "The chord's bass plays. C5: the bass, D5: the second bass (the fifth), the other Repeat keys strum the upper strings." },
                { "Bass & Pick", "The chord's bass plays. C5 and D5 the basses, E5, F5, G5 and A5 pick the 4th to the 1st string; C1 to D#1 strum the upper four." },
                { "Pattern", "With the chord held in the Main zone, the rhythm chosen in the options plays by itself, in time with Ableton." },
                { "Direct", "A plain sampler: every note as it comes, the SoundFont as it is. It's on until you choose a mode." },
                { "Hold", "Chords keep sounding after the keys are let go, until the next chord." },
                { "Options (the box under the modes)", "" },
                { "Velocity FX", "What a note gets from a velocity (90 by default; hit hard): a slow strum, a slide into it from below or from above, a bend up to it, a reverse bend (pre-bent, released to it); under a velocity (45): a slow strum, or muted (Velo Mute). Time and Steps set how long and how far. The display names it when it fires." },
                { "Pattern, Hold in Solo and Harmony", "They play a held chord, so they work in the chord modes only (dimmed elsewhere)." },
                { "Permanent (Solo)", "H.O.: a key played while another is held, up to Steps away, hammers on (the string bends there, no new pick) and pulls off when let go. Slide: the same, sliding along the frets over Time." },
                { "Articulations", "Tremolo picks the notes again and again; Sustain 1-2 Mono keeps notes ringing until a step of one or two semitones; Muted mutes everything; Velo Mute mutes soft notes." },
                { "Bass Zone (Solo)", "Notes up to the key next to it play firmer and without articulations: a bass line under the melody." },
                { "Chord Position", "Where chords lie on the neck: I is the open position, up to IV; Kbd: the octave you play the chord in picks it (from E1: I, E2: II, E3: III, E4: IV)." },
                { "Strings", "Chords: the six strings, low E to high E; the lit ones are strummed. Click one to leave it out or bring it back." },
                { "Chord/Bass", "Off: the keys you hold are read as one chord (G A C E: Am7 over its root). On: the lowest key is the bass and the others the chord above it, played as such (G + A C E: Am/G, with G in the bass)." },
                { "Alter Bass, Bass Mono", "The same chord again alternates its bass with the fifth; the second bass stops the first." },
                { "Add-on keys", "Bass & Pick's black keys: Unison doubles the strings, Chromatic plays a semitone below them." },
                { "The neck", "" },
                { "Drawing chords", "Click a string at a fret to place a finger (again to lift it). Left of the nut, a click switches a string between open and not played. The name appears as you draw; click it to hear the chord. Playing shows what's played instead." },
                { "Reading it", "The display names the last chord played (G/B: B in the bass). The lights show the strum's direction and palm-muted notes." },
            };
            case controls: return {
                { "Guitar", "" },
                { "Pattern, Hold, Direct", "Together in the title bar: how the panel plays. Direct, set apart, turns the rest off." },
                { "Double", "Under Controllers: a second take of every note, slightly late and out of tune, on the other side (double-tracking)." },
                { "Humanize", "Under Controllers: small random changes in timing, velocity and tuning, so it doesn't sound mechanical." },
                { "Tune", "Under Controllers: Standard, Drop D, Drop C, Open G or DADGAD; chords are voiced for those strings." },
                { "Tremolo, Rate", "Under Controllers: a stereo tremolo; the menu beside it locks it to Ableton's tempo." },
                { "CONTROLLERS (the box under the options; click its title to fold it)", "FEEL: Strum, Velocity, Accent, Humanize. SOUND: Release, Tune, Double, Tremolo. WHEELS: P.B., M.W., A.T." },
                { "Strum", "How long a chord takes from the first string to the last, up to 250 ms (Off: a pick's natural 18 ms)." },
                { "Velocity, Accent", "The curve velocities go through (linear, convex: soft notes louder, concave: softer, S), and the velocity where soft playing turns hard." },
                { "Release", "How long notes ring after they're let go (the same as PLAY's Release)." },
                { "P.B., M.W., A.T.", "What the pitch bender, mod wheel and aftertouch do, and how far: Pitch, Slide (by semitones, along the frets), Mono Bend (the lowest note only), Mod. (modulation)." },
                { "Instrument", "" },
                { "Gain", "The plugin's output level." },
                { "Tone", "A gentle low-pass filter: darker towards the left. Off at the right." },
                { "Layer", "A second instrument from the same SoundFont: Layer plays both together, Split plays it below the split key." },
                { "Effects", "" },
                { "Reverb and Chorus", "The SoundFont's own effects, on or off, as the SoundFont sets them. A click on their title (or a right-click on the bar) shows their knobs and presets. Effects to separate outputs sends them raw to Ableton instead, for your own effects." },
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
