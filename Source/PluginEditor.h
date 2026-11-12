#pragma once

#include "PluginProcessor.h"
#include "GuitarNeck.h"
#include "Guide.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

// Dark, quiet, one accent colour: knobs as thin arcs, flat controls.
struct SF2Look : juce::LookAndFeel_V4
{
    static inline const juce::Colour background { 0xff151518 };
    static inline const juce::Colour panel      { 0xff1d1d22 };
    static inline const juce::Colour line       { 0xff2c2c33 };
    static inline const juce::Colour text       { 0xffe8e8ec };
    static inline const juce::Colour dim        { 0xff8a8a94 };
    static inline const juce::Colour accent     { 0xffe0a64b };
    static inline const juce::Colour warning    { 0xffe5734f };

    SF2Look();
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider&) override;

    // An instrument's "Name  [040]": the bracketed number drawn dimmer, in the list and in the box.
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& itemText,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon,
                            const juce::Colour* textColour) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        // The small preset menus in the panel headers get text to match their height.
        return juce::FontOptions (box.getHeight() < 24 ? (float) box.getHeight() * 0.68f : juce::jmin (16.0f, (float) box.getHeight() * 0.85f));
    }
    static int numberStart (const juce::String& s) { return s.lastIndexOf ("  ["); }
    // The Guitar panel's mode buttons: one size for all, on one line.
    juce::Font getTextButtonFont (juce::TextButton& button, int height) override
    {
        if (button.getProperties().contains ("guitarMode")) return juce::FontOptions (13.0f, juce::Font::bold);
        return LookAndFeel_V4::getTextButtonFont (button, height);
    }
};

// The on-screen keyboard, with the keys the preset doesn't play dimmed.
// A switch with a round light, as on a hardware panel (the Guitar panel's Pattern, Direct, Hold
// and its on/off options): it toggles on each click.
struct LedButton : juce::TextButton
{
    explicit LedButton (const juce::String& text) : juce::TextButton (text) { setClickingTogglesState (true); }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (juce::Colour (0xff2a1f17).brighter (down ? 0.15f : over ? 0.08f : 0.0f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
        const bool on = getToggleState();
        const auto led = r.removeFromLeft (r.getHeight()).withSizeKeepingCentre (8.0f, 8.0f).translated (3.0f, 0.0f);
        const juce::Colour green (0xff6fe07a);
        if (on) { g.setColour (green.withAlpha (0.3f)); g.fillEllipse (led.expanded (3.0f)); }
        g.setColour (on ? green : juce::Colour (0xff1c3a20));
        g.fillEllipse (led);
        g.setColour (juce::Colour (0xfff1e6c8).withAlpha (on ? 1.0f : 0.6f));
        g.setFont (juce::FontOptions (juce::jmin (14.0f, r.getHeight() * 0.62f), juce::Font::bold));
        g.drawText (getButtonText(), r.withTrimmedRight (6.0f), juce::Justification::centred);
    }
};

struct RangeKeyboard : juce::MidiKeyboardComponent
{
    using MidiKeyboardComponent::MidiKeyboardComponent;
    int lowKey = 0, highKey = 127;
    int splitKey = -1;   // Split: the keys below it play the second instrument (tinted)
    bool repeatZones = false;   // the Guitar panel's Repeat zones (C1-D#1, C5-C6), tinted

    void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour lineColour, juce::Colour textColour) override;
    void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour noteFillColour) override;
};

class SF2PlayerEditor : public juce::AudioProcessorEditor,
                        public juce::FileDragAndDropTarget,
                        private juce::ChangeListener,
                        private juce::Timer
{
public:
    explicit SF2PlayerEditor (SF2PlayerProcessor&);
    ~SF2PlayerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragging = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragging = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    // A knob with its name above it, tied to a parameter.
    struct Knob
    {
        Knob (SF2PlayerEditor& editor, const juce::String& parameterID, const juce::String& name);
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        juce::AudioProcessorValueTreeState::SliderAttachment attachment;
        void setBounds (juce::Rectangle<int>);
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void chooseFile();
    void load (const juce::File&);
    void refresh();
    void refreshFileBox (const SF2PlayerProcessor::State&);
    void stepPreset (int delta);

    SF2PlayerProcessor& player;
    SF2Look look;

    std::unique_ptr<juce::PropertiesFile> settings;
    juce::RecentlyOpenedFilesList recentFiles;
    static constexpr int openItemId = 1000;

    juce::ComboBox fileBox;
    juce::Label infoLabel;
    juce::Label authorLabel;   // the SoundFont's author, small and italic, after its name in the file box
    void placeAuthor();
    juce::TextButton previousButton { "<" }, nextButton { ">" };
    juce::ShapeButton favouriteButton { "Favorite", juce::Colours::transparentBlack, juce::Colours::transparentBlack,
                                        juce::Colours::transparentBlack };   // a star: marks the chosen instrument a favourite
    juce::ComboBox presetBox;
    juce::TextEditor searchBox;
    void fillPresetBox();   // from a fresh snapshot
    void fillPresetBox (const SF2PlayerProcessor::State&);

    // Favourites, kept for every project: "<file name>|<bank>:<program>" in the settings file.
    static constexpr int favouriteIdBase = 100000;   // list ids of the copies under "Favorites"
    // A list id back to the instrument's index (favorites are copies with their own ids).
    static int presetIndexOf (int id) { return id > favouriteIdBase ? id - favouriteIdBase - 1 : id - 1; }
    int addInstruments (juce::ComboBox&, const juce::String& query, const SF2PlayerProcessor::State&,
                        const juce::StringArray& favourite) const;
    static juce::String favouriteKey (const SF2PlayerProcessor::State&, int presetIndex);
    juce::StringArray favourites() const;
    void toggleFavourite();
    void refreshFavouriteButton (const SF2PlayerProcessor::State&, const juce::StringArray& favourite);

    juce::Slider gainSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label gainLabel { {}, "Gain" };
    juce::AudioProcessorValueTreeState::SliderAttachment gainAttachment;
    juce::Slider toneSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label toneLabel { {}, "Tone" };
    juce::AudioProcessorValueTreeState::SliderAttachment toneAttachment;
    juce::ToggleButton sendsToggle { "Effects to separate outputs" };

    // The second instrument: Off / Layer / Split, which one, the split key and its level.
    juce::Label layerLabel { {}, "Layer" }, layerLevelLabel { {}, "Level" };
    juce::ComboBox layerModeBox, layerPresetBox;
    juce::AudioProcessorValueTreeState::ComboBoxAttachment layerModeAttachment;
    juce::Slider splitSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::AudioProcessorValueTreeState::SliderAttachment splitAttachment;
    juce::Slider layerLevelSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::AudioProcessorValueTreeState::SliderAttachment layerLevelAttachment;
    void refreshLayerControls();
    juce::AudioProcessorValueTreeState::ButtonAttachment sendsAttachment;

    juce::ToggleButton reverbToggle, chorusToggle;
    juce::AudioProcessorValueTreeState::ButtonAttachment reverbOnAttachment, chorusOnAttachment;

    Knob reverbLevel { *this, "fsReverbLevel", "Level" };
    Knob reverbRoom { *this, "fsReverbRoom", "Room" };
    Knob reverbDamping { *this, "fsReverbDamping", "Damping" };
    Knob reverbWidth { *this, "fsReverbWidth", "Width" };
    Knob chorusLevel { *this, "fsChorusLevel", "Level" };
    Knob chorusRate { *this, "fsChorusRate", "Rate" };
    Knob chorusDepth { *this, "fsChorusDepth", "Depth" };
    Knob chorusVoices { *this, "fsChorusVoices", "Voices" };

    // Playing: every knob starts where the SoundFont is left as it is.
    Knob attack { *this, "attack", "Attack" };
    Knob release { *this, "release", "Release" };
    Knob velocityCurve { *this, "velocityCurve", "Velocity" };
    Knob transpose { *this, "transpose", "Transpose" };
    Knob fineTune { *this, "fineTune", "Fine" };
    Knob bendRange { *this, "bendRange", "Bend" };
    Knob glide { *this, "glide", "Glide" };
    juce::ComboBox playMode;
    juce::Label playModeLabel { {}, "Mode" };
    juce::AudioProcessorValueTreeState::ComboBoxAttachment playModeAttachment;

    // Starting points for the shared reverb and chorus: they move the four knobs.
    juce::ComboBox reverbPresets, chorusPresets;
    void fillEffectPresets();
    void applyEffectPreset (bool reverb, int index);
    void showEffectPreset (bool reverb);

    // Plays a short note of the chosen instrument.
    juce::ShapeButton previewButton { "Preview", SF2Look::dim, SF2Look::text, SF2Look::accent };
    int previewNote = -1, previewTicks = 0;
    void preview();

    juce::Label warningLabel;
    RangeKeyboard keyboard { player.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };

    std::unique_ptr<juce::FileChooser> chooser;
    // The Guitar panel: shown for a guitar (Auto) or when forced on (the Guitar menu in PLAY's header).
    Knob strum { *this, "strum", "Strum" };
    Knob palmMute { *this, "palmMute", "Palm" };
    Knob humanize { *this, "humanize", "Humanize" };
    Knob doubleMix { *this, "doubleMix", "Double" };
    Knob tremoloDepth { *this, "tremoloDepth", "Tremolo" };
    Knob tremoloRate { *this, "tremoloRate", "Rate" };
    juce::ComboBox guitarPanelBox, guitarModeBox, strumDirectionBox, tremoloSyncBox, patternBox, tuningBox;
    juce::ToggleButton velocityStringsToggle { "Velocity strings" };
    // Starting points for the Guitar panel: they set its controls (Off: all neutral).
    juce::ComboBox guitarPresets;
    void applyGuitarPreset (int index);
    void showGuitarPreset();
    juce::Label guitarModeLabel { {}, "Mode" }, strumDirectionLabel { {}, "Way" };
    juce::AudioProcessorValueTreeState::ComboBoxAttachment guitarPanelAttachment, guitarModeAttachment,
        strumDirectionAttachment, tremoloSyncAttachment, patternAttachment, tuningAttachment;
    juce::AudioProcessorValueTreeState::ButtonAttachment velocityStringsAttachment;
    juce::Label tuningLabel { {}, "Tune" };
    // The five modes as buttons under the neck, Pattern and Direct beside them (the hidden Mode
    // menu holds the parameter); Hold by the display.
    std::array<juce::TextButton, GuitarEngine::modeCount> modeButtons;
    LedButton patternButton { "Pattern" }, directButton { "Direct" }, holdButton { "Hold" };
    juce::AudioProcessorValueTreeState::ButtonAttachment guitarPatternAttachment { player.parameters, "guitarPattern", patternButton },
                                                         directAttachment { player.parameters, "guitarDirect", directButton },
                                                         holdAttachment { player.parameters, "guitarHold", holdButton };
    void showMode();
    // Each mode's options, in the box under the mode buttons (only the ones it uses).
    juce::ComboBox velocityFxBox, harmonyBox, addOnBox;
    juce::Slider fxVelocitySlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 fxTimeSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 stringsTopSlider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft },
                 stringsBottomSlider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft },
                 repeatStringsSlider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    std::array<juce::TextButton, 5> positionButtons;
    // Chords' Strings: the six strings, low E to high E, lit when the strum plays them (a range).
    std::array<juce::TextButton, 6> stringButtons;
    void clickString (int index);
    LedButton chordBassToggle { "Chord/Bass" }, alterBassToggle { "Alter Bass" }, bassMonoToggle { "Bass Mono" };
    juce::Label fxLabel { {}, "Velocity FX" }, fxVelocityLabel { {}, "Velo" }, fxTimeLabel { {}, "Time" },
                positionLabel { {}, "Chord Position" }, stringsLabel { {}, "Strings" }, stringsDash { {}, "-" },
                repeatStringsLabel { {}, "Strings #" }, harmonyLabel { {}, "Interval" }, addOnLabel { {}, "Add-on keys" },
                rhythmLabel { {}, "Rhythm" }, optionsNote;
    juce::AudioProcessorValueTreeState::ComboBoxAttachment velocityFxAttachment { player.parameters, "velocityFx", velocityFxBox },
                                                           harmonyAttachment { player.parameters, "harmonyInterval", harmonyBox },
                                                           addOnAttachment { player.parameters, "addOnKeys", addOnBox };
    juce::AudioProcessorValueTreeState::SliderAttachment fxVelocityAttachment { player.parameters, "fxVelocity", fxVelocitySlider },
                                                         fxTimeAttachment { player.parameters, "fxTime", fxTimeSlider },
                                                         stringsTopAttachment { player.parameters, "stringsTop", stringsTopSlider },
                                                         stringsBottomAttachment { player.parameters, "stringsBottom", stringsBottomSlider },
                                                         repeatStringsAttachment { player.parameters, "repeatStrings", repeatStringsSlider };
    juce::AudioProcessorValueTreeState::ButtonAttachment chordBassAttachment { player.parameters, "chordBass", chordBassToggle },
                                                         alterBassAttachment { player.parameters, "alterBass", alterBassToggle },
                                                         bassMonoAttachment { player.parameters, "bassMono", bassMonoToggle };
    // Solo: H.O. or Slide always on, the key switches (C1-D#1) and the pedal, the bass zone.
    juce::ComboBox soloFxBox, pedalBox;
    std::array<juce::ComboBox, 4> switchBoxes;
    juce::Slider fxStepsSlider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft },
                 legatoStepsSlider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft },
                 legatoTimeSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 bassZoneTopSlider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    LedButton bassZoneToggle { "Bass Zone" };
    juce::Label fxStepsLabel { {}, "Steps" }, permanentLabel { {}, "Permanent" }, legatoStepsLabel { {}, "Steps" },
                legatoTimeLabel { {}, "Time" }, pedalLabel { {}, "Pedal" };
    std::array<juce::Label, 4> switchLabels;
    juce::AudioProcessorValueTreeState::ComboBoxAttachment soloFxAttachment { player.parameters, "soloFx", soloFxBox },
                                                           pedalAttachment { player.parameters, "pedalFx", pedalBox },
                                                           switch1Attachment { player.parameters, "keySwitch1", switchBoxes[0] },
                                                           switch2Attachment { player.parameters, "keySwitch2", switchBoxes[1] },
                                                           switch3Attachment { player.parameters, "keySwitch3", switchBoxes[2] },
                                                           switch4Attachment { player.parameters, "keySwitch4", switchBoxes[3] };
    juce::AudioProcessorValueTreeState::SliderAttachment fxStepsAttachment { player.parameters, "fxSteps", fxStepsSlider },
                                                         legatoStepsAttachment { player.parameters, "legatoSteps", legatoStepsSlider },
                                                         legatoTimeAttachment { player.parameters, "legatoTime", legatoTimeSlider },
                                                         bassZoneTopAttachment { player.parameters, "bassZoneTop", bassZoneTopSlider };
    juce::AudioProcessorValueTreeState::ButtonAttachment bassZoneAttachment { player.parameters, "bassZone", bassZoneToggle };
    // Controllers (shown with the header's Controllers button): the velocity curve, Accent,
    // Release, and what the pitch bender, mod wheel and aftertouch do.
    juce::TextButton controllersButton;
    bool showControllers = false;
    void toggleControllers();
    std::array<juce::TextButton, 4> curveButtons;
    juce::Slider accentSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 releaseTimeSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 pbRangeSlider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft },
                 mwRangeSlider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft },
                 atRangeSlider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::ComboBox pbBox, mwBox, atBox;
    juce::Label curveLabel { {}, "Velo curve" }, accentLabel { {}, "Accent" }, releaseTimeLabel { {}, "Release" },
                pbLabel { {}, "P.B." }, mwLabel { {}, "M.W." }, atLabel { {}, "A.T." };
    juce::AudioProcessorValueTreeState::SliderAttachment accentAttachment { player.parameters, "accent", accentSlider },
                                                         releaseTimeAttachment { player.parameters, "release", releaseTimeSlider },
                                                         pbRangeAttachment { player.parameters, "pbRange", pbRangeSlider },
                                                         mwRangeAttachment { player.parameters, "mwRange", mwRangeSlider },
                                                         atRangeAttachment { player.parameters, "atRange", atRangeSlider };
    juce::AudioProcessorValueTreeState::ComboBoxAttachment pbAttachment { player.parameters, "pbAssign", pbBox },
                                                           mwAttachment { player.parameters, "mwAssign", mwBox },
                                                           atAttachment { player.parameters, "atAssign", atBox };
    juce::Rectangle<int> optionsArea, optionsFrame;   // the options box and its frame (with the mode's name)
    void refreshModeOptions();
    // Beside the instrument: which panel it gets (Auto detects guitars; Guitar or No panel when a
    // SoundFont doesn't follow General MIDI), remembered for each instrument of each SoundFont.
    juce::TextButton panelButton;
    juce::String panelKey;   // the instrument the choice was last applied for
    void showPanelMenu();
    void applyPanelChoice (const SF2PlayerProcessor::State&);
    void refreshPanelButton();
    bool guitarShown = false;
    void refreshGuitarPanel();

    // Sections that fold to their title strip (a click on the title), remembered for every
    // project in the settings file. The window shrinks or grows by the difference.
    enum Section { effectsSection = 0, playSection = 1, guitarSection = 2, keyboardSection = 3 };
    std::array<bool, 4> collapsed {};
    juce::TextButton keysButton;   // folds the keyboard
    static constexpr int keyboardHeight = 64;   // a small keyboard: it shows what's played, a MIDI keyboard plays
    void applyHeight();   // the window's height is its contents': it never stretches
    static constexpr int panelHeight = 118, collapsedHeight = 26, panelGap = 12;
    static constexpr int baseNeckHeight = 250, controllersHeight = 112, guitarHeader = 30;
    // The neck, the mode buttons and the options box, under the knobs (the controllers' rows too when shown).
    int neckHeight() const { return baseNeckHeight + (showControllers ? controllersHeight : 0); }
    int heightOf (Section s) const
    {
        if (collapsed[(size_t) s] || (s == effectsSection && ! showEffectControls)) return collapsedHeight;
        return s == guitarSection ? guitarHeader + neckHeight() : panelHeight;
    }
    GuitarNeck neck { { SF2Look::text, SF2Look::dim, SF2Look::accent, SF2Look::panel, SF2Look::line.brighter (0.35f) } };
    int shownGuitarSequence = 0;
    std::array<int, 6> auditionNotes {};   // the drawn chord, played by a click on its name
    int auditionCount = 0, auditionTicks = 0;
    void stopAudition();
    int guitarExtra() const { return guitarShown ? panelGap + heightOf (guitarSection) : 0; }
    // GENERAL: Gain and Tone, the effects and PLAY (what every instrument has).
    // SOUNDFONT: its title, the file box (and a warning row when there's one), the instrument row.
    int soundHeight() const { return 26 + 28 + (infoLabel.isVisible() ? 22 : 0) + 8 + 28 + 10; }
    int generalHeight() const { return 26 + 24 + 10 + heightOf (effectsSection) + 10 + heightOf (playSection) + 10; }
    int minimumHeight() const;
    void toggleSection (Section);
    void fitToScreen (int keep = -1);   // folds sections until the window fits the screen
    bool fitting = false;
    void showSectionContents();
    void drawSectionTitle (juce::Graphics&, juce::Rectangle<int> panel, const juce::String& title, Section);
    void mouseUp (const juce::MouseEvent&) override;
    // Help: the "?" menu (the guide, About, hints on or off) and the hint line in the header,
    // which explains whatever control the mouse is over.
    juce::TextButton helpButton { "?" };
    juce::Label hintLabel;
    bool showHints = true;
    // Reverb and chorus show only their switches, as the plugin this one follows did; their knobs
    // and presets are an advanced setting in the ? menu.
    bool showEffectControls = false;
    void toggleEffectControls();
    std::unique_ptr<Guide> guide;
    void showHelpMenu();
    void openGuide (Guide::Page);
    void showHint();
    friend struct EditorTestAccess;   // TailTest's snapshots fold sections without touching the settings file

    juce::Rectangle<int> meterArea, reverbPanel, chorusPanel, playPanel, guitarPanel, generalPanel, pianoPanel, soundPanel, keysPanel;
    static constexpr int pianoHeight = 64;   // the PIANO / INSTRUMENT panel: its title and the layer row
    float shownPeak = 0.0f;
    int lastNoteCount = 0, midiFlash = 0;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SF2PlayerEditor)
};
