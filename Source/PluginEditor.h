#pragma once

#include "PluginProcessor.h"
#include "GuitarNeck.h"
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
struct RangeKeyboard : juce::MidiKeyboardComponent
{
    using MidiKeyboardComponent::MidiKeyboardComponent;
    int lowKey = 0, highKey = 127;
    int splitKey = -1;   // Split: the keys below it play the second instrument (tinted)

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
    Knob fretPosition { *this, "fretPosition", "Fret" };
    Knob palmMute { *this, "palmMute", "Palm" };
    Knob humanize { *this, "humanize", "Humanize" };
    Knob doubleMix { *this, "doubleMix", "Double" };
    Knob tremoloDepth { *this, "tremoloDepth", "Tremolo" };
    Knob tremoloRate { *this, "tremoloRate", "Rate" };
    juce::ComboBox guitarPanelBox, guitarModeBox, strumDirectionBox, tremoloSyncBox, patternBox, tuningBox, chordKeyBox, chordScaleBox;
    juce::ToggleButton velocityStringsToggle { "Velocity strings" };
    // Starting points for the Guitar panel: they set its controls (Off: all neutral).
    juce::ComboBox guitarPresets;
    void applyGuitarPreset (int index);
    void showGuitarPreset();
    juce::Label guitarModeLabel { {}, "Mode" }, strumDirectionLabel { {}, "Way" };
    juce::AudioProcessorValueTreeState::ComboBoxAttachment guitarPanelAttachment, guitarModeAttachment,
        strumDirectionAttachment, tremoloSyncAttachment, patternAttachment, tuningAttachment, chordKeyAttachment, chordScaleAttachment;
    juce::AudioProcessorValueTreeState::ButtonAttachment velocityStringsAttachment;
    juce::Label tuningLabel { {}, "Tune" };
    // The mode as a row of buttons under the neck (the hidden Mode menu holds the parameter).
    static constexpr std::array<int, 10> modeOrder { GuitarEngine::solo, GuitarEngine::harmony, GuitarEngine::chords,
        GuitarEngine::autoChords, GuitarEngine::bassAndChord, GuitarEngine::bassAndPick, GuitarEngine::shape,
        GuitarEngine::keyChords, GuitarEngine::strumKeys, GuitarEngine::pattern };
    std::array<juce::TextButton, 10> modeButtons;
    void showMode();
    bool guitarShown = false;
    void refreshGuitarPanel();

    // Sections that fold to their title strip (a click on the title), remembered for every
    // project in the settings file. The window shrinks or grows by the difference.
    enum Section { effectsSection = 0, playSection = 1, guitarSection = 2, keyboardSection = 3 };
    std::array<bool, 4> collapsed {};
    juce::TextButton keysButton;   // folds the keyboard
    int keyboardHeight = 120;      // the keyboard's height when it was last shown
    static constexpr int panelHeight = 118, collapsedHeight = 26, panelGap = 12;
    static constexpr int neckHeight = 182;   // the Guitar panel's neck strip and its mode buttons, under the knobs
    int heightOf (Section s) const
    {
        return collapsed[(size_t) s] ? collapsedHeight : panelHeight + (s == guitarSection ? neckHeight : 0);
    }
    GuitarNeck neck { { SF2Look::text, SF2Look::dim, SF2Look::accent, SF2Look::panel, SF2Look::line.brighter (0.35f) } };
    int shownGuitarSequence = 0;
    int auditionKey = -1, auditionTicks = 0;   // the drawn chord, played by a click on its name
    int guitarExtra() const { return guitarShown ? panelGap + heightOf (guitarSection) : 0; }
    int minimumHeight() const;
    void toggleSection (Section);
    void showSectionContents();
    void drawSectionTitle (juce::Graphics&, juce::Rectangle<int> panel, const juce::String& title, Section);
    void mouseUp (const juce::MouseEvent&) override;
    friend struct EditorTestAccess;   // TailTest's snapshots fold sections without touching the settings file

    juce::Rectangle<int> meterArea, reverbPanel, chorusPanel, playPanel, guitarPanel;
    float shownPeak = 0.0f;
    int lastNoteCount = 0, midiFlash = 0;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SF2PlayerEditor)
};
