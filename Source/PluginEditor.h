#pragma once

#include "PluginProcessor.h"
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
};

// The on-screen keyboard, with the keys the preset doesn't play dimmed.
struct RangeKeyboard : juce::MidiKeyboardComponent
{
    using MidiKeyboardComponent::MidiKeyboardComponent;
    int lowKey = 0, highKey = 127;

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
    void refreshFileBox();
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
    void fillPresetBox();

    // Favourites, kept for every project: "<file name>|<bank>:<program>" in the settings file.
    static constexpr int favouriteIdBase = 100000;   // list ids of the copies under "Favorites"
    juce::String favouriteKey (int presetIndex) const;
    bool isFavourite (int presetIndex) const;
    void toggleFavourite();
    void refreshFavouriteButton();

    juce::Slider gainSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label gainLabel { {}, "Gain" };
    juce::AudioProcessorValueTreeState::SliderAttachment gainAttachment;
    juce::Slider toneSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label toneLabel { {}, "Tone" };
    juce::AudioProcessorValueTreeState::SliderAttachment toneAttachment;
    juce::ToggleButton sendsToggle { "Effects to separate outputs" };
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
    juce::Rectangle<int> meterArea, reverbPanel, chorusPanel, playPanel;
    float shownPeak = 0.0f;
    int lastNoteCount = 0, midiFlash = 0;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SF2PlayerEditor)
};
