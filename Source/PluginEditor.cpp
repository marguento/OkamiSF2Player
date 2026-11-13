#include "PluginEditor.h"
#include <algorithm>
#include <iterator>
#include <numeric>
#include <tuple>

namespace
{
// Direct, mode, pattern, strum (ms), chord position, humanize, double, tremolo, rhythm. Way, Palm and
// Velocity strings aren't in the panel any more: a preset sets them back to neutral.
struct GuitarPreset { const char* name; float values[9]; };
const GuitarPreset guitarPresetList[] = {
    { "Off (Direct)",   { 1, 2, 0,   0, 0, 0.00f, 0.0f, 0.00f, 2 } },
    { "Acoustic Strum", { 0, 2, 0,  35, 0, 0.35f, 0.0f, 0.00f, 2 } },
    { "Campfire",       { 0, 2, 0,  45, 0, 0.50f, 0.0f, 0.00f, 2 } },
    { "Double-Tracked", { 0, 2, 0,  30, 0, 0.40f, 0.8f, 0.00f, 2 } },
    { "Slow Ballad",    { 0, 2, 0, 120, 1, 0.30f, 0.5f, 0.15f, 2 } },
    { "High Strings",   { 0, 2, 0,  20, 2, 0.30f, 0.0f, 0.00f, 2 } },
    { "Folk Pattern",   { 0, 2, 1,  30, 0, 0.40f, 0.0f, 0.00f, 2 } },
    { "Fingerpicking",  { 0, 2, 1,   0, 0, 0.45f, 0.0f, 0.00f, 10 } },
    { "Bass & Pick",    { 0, 4, 0,   0, 0, 0.35f, 0.0f, 0.00f, 2 } },
};
const char* const guitarPresetIds[] = { "guitarDirect", "guitarMode", "guitarPattern", "strum", "chordPosition",
                                        "humanize", "doubleMix", "tremoloDepth", "strumPattern" };
const char* const retiredGuitarIds[] = { "strumDirection", "palmMute", "strumByVelocity" };   // kept neutral
// The neck shows played chords around this fret, by Chord Position (I to IV, Kbd).
constexpr int positionFret[] = { 1, 3, 5, 7, 3 };
} // namespace

// ---------------------------------------------------------------------------- look

SF2Look::SF2Look()
{
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::Label::textColourId, text);
    setColour (juce::ComboBox::backgroundColourId, panel);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, dim);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, text);
    setColour (juce::TextButton::buttonColourId, panel);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::ToggleButton::textColourId, dim);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::ToggleButton::tickDisabledColourId, dim);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::backgroundColourId, line);
    setColour (juce::Slider::thumbColourId, text);
    setColour (juce::Slider::textBoxTextColourId, dim);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
}

void SF2Look::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                                float startAngle, float endAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + position * (endAngle - startAngle);
    const float thickness = 3.0f;

    juce::Path track, value;
    track.addCentredArc (centre.x, centre.y, radius - thickness, radius - thickness, 0.0f, startAngle, endAngle, true);
    value.addCentredArc (centre.x, centre.y, radius - thickness, radius - thickness, 0.0f, startAngle, angle, true);
    const juce::PathStrokeType stroke (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (line);
    g.strokePath (track, stroke);
    g.setColour (accent);
    g.strokePath (value, stroke);

    const auto tip = centre.getPointOnCircumference (radius - thickness - 7.0f, angle);
    g.setColour (text);
    g.drawLine ({ centre.getPointOnCircumference (radius * 0.25f, angle), tip }, 2.0f);
}

void SF2Look::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                 bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& itemText,
                                 const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour)
{
    const int split = numberStart (itemText);
    if (isSeparator || split < 0)
    {
        LookAndFeel_V4::drawPopupMenuItem (g, area, isSeparator, isActive, isHighlighted, isTicked, hasSubMenu,
                                           itemText, shortcutKeyText, icon, textColour);
        return;
    }

    const auto name = itemText.substring (0, split);
    LookAndFeel_V4::drawPopupMenuItem (g, area, false, isActive, isHighlighted, isTicked, hasSubMenu,
                                       name, shortcutKeyText, icon, textColour);

    // Where V4 put the name (its own geometry): after an icon column one font-height wide.
    auto r = area.reduced (1);
    r.reduce (juce::jmin (5, area.getWidth() / 20), 0);
    auto font = getPopupMenuFont();
    const float maxFontHeight = (float) r.getHeight() / 1.3f;
    if (font.getHeight() > maxFontHeight)
        font.setHeight (maxFontHeight);
    r.removeFromLeft (juce::roundToInt (maxFontHeight));
    const int nameWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, name)) + 4;   // slack: drawText elides at the exact width
    g.setFont (font.withHeight (font.getHeight() * 0.85f));
    g.setColour (dim.withMultipliedAlpha (isActive ? 0.8f : 0.4f));
    g.drawText (itemText.substring (split), r.withTrimmedLeft (nameWidth), juce::Justification::centredLeft, true);
}

void SF2Look::drawLabel (juce::Graphics& g, juce::Label& label)
{
    const auto labelText = label.getText();
    const int split = numberStart (labelText);
    if (label.isBeingEdited() || split < 0 || dynamic_cast<juce::ComboBox*> (label.getParentComponent()) == nullptr)
    {
        LookAndFeel_V4::drawLabel (g, label);
        return;
    }

    // A combo box's text: the name, then its number dimmer.
    g.fillAll (label.findColour (juce::Label::backgroundColourId));
    const auto font = getLabelFont (label);
    auto area = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());
    const auto name = labelText.substring (0, split);
    const int nameWidth = juce::jmin (area.getWidth(), juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, name)) + 4);
    g.setFont (font);
    g.setColour (label.findColour (juce::Label::textColourId));
    g.drawText (name, area.removeFromLeft (nameWidth), juce::Justification::centredLeft, true);
    g.setFont (font.withHeight (font.getHeight() * 0.85f));
    g.setColour (dim.withMultipliedAlpha (0.8f));
    g.drawText (labelText.substring (split), area, juce::Justification::centredLeft, true);
}

// ---------------------------------------------------------------------------- keyboard

void RangeKeyboard::drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                   juce::Colour, juce::Colour)
{
    const bool playable = note >= lowKey && note <= highKey;
    auto fill = playable ? juce::Colour (0xffd9d9de) : juce::Colour (0xff4a4a52);
    if (note < splitKey)   // played by the second instrument
        fill = juce::Colour (0xffb9c9d4);
    if (repeatZones && GuitarEngine::inRepeat (note))   // the Guitar panel's Repeat keys
        fill = juce::Colour (0xffd8c39a);
    if (isOver) fill = fill.brighter (0.1f);
    const auto key = area.reduced (0.5f, 0.0f);
    if (isDown)   // played: lit from the top, like a key glowing under the finger
        g.setGradientFill (juce::ColourGradient::vertical (SF2Look::accent.brighter (0.35f), key.getY(), SF2Look::accent, key.getBottom()));
    else
        g.setColour (fill);
    g.fillRect (key);
    g.setColour (SF2Look::background);
    g.drawVerticalLine ((int) area.getRight(), area.getY(), area.getBottom());
    if (isSounding (note)) drawSoundingDot (g, area, true);
    // Its name at the bottom: every white key's letter, each C with its octave (C3 is middle C, as in Ableton).
    if (area.getWidth() >= 9.0f)
    {
        const bool c = note % 12 == 0;
        const auto name = c ? juce::MidiMessage::getMidiNoteName (note, true, true, 3) : juce::MidiMessage::getMidiNoteName (note, true, false, 3);
        g.setColour (juce::Colours::black.withAlpha (isDown ? 0.75f : c ? 0.62f : 0.22f));
        g.setFont (juce::FontOptions (c ? 8.5f : 7.5f));
        g.drawFittedText (name, key.withTop (key.getBottom() - 12.0f).toNearestInt(), juce::Justification::centred, 1, 0.55f);
    }
}

void RangeKeyboard::drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                   juce::Colour)
{
    const bool playable = note >= lowKey && note <= highKey;
    auto fill = playable ? juce::Colour (0xff222226) : juce::Colour (0xff2e2e34);
    if (note < splitKey)
        fill = juce::Colour (0xff2a3a46);
    if (repeatZones && GuitarEngine::inRepeat (note))
        fill = juce::Colour (0xff4a3a22);
    if (isOver) fill = fill.brighter (0.2f);
    if (isDown)
        g.setGradientFill (juce::ColourGradient::vertical (SF2Look::accent, area.getY(), SF2Look::accent.darker (0.35f), area.getBottom()));
    else
        g.setColour (fill);
    g.fillRoundedRectangle (area, 1.5f);
    if (isSounding (note)) drawSoundingDot (g, area, false);
}

// A key the guitar sounds, in the neck's green: a white key's lower part (the bit you see below
// the black keys) glows, a black key turns green.
void RangeKeyboard::drawSoundingDot (juce::Graphics& g, juce::Rectangle<float> key, bool white) const
{
    const juce::Colour green (0xff62d86e);
    if (white)
    {
        const auto band = key.reduced (1.5f, 0.0f).withTop (key.getBottom() - key.getHeight() * 0.42f).withTrimmedBottom (13.0f);
        g.setGradientFill (juce::ColourGradient::vertical (green.withAlpha (0.25f * soundingAlpha), band.getY(),
                                                           green.withAlpha (0.85f * soundingAlpha), band.getBottom()));
        g.fillRoundedRectangle (band, 2.0f);
    }
    else
    {
        g.setGradientFill (juce::ColourGradient::vertical (green.darker (0.5f).withAlpha (0.9f * soundingAlpha), key.getY(),
                                                           green.withAlpha (0.95f * soundingAlpha), key.getBottom()));
        g.fillRoundedRectangle (key.reduced (0.5f), 1.5f);
    }
}

// ---------------------------------------------------------------------------- knobs

SF2PlayerEditor::Knob::Knob (SF2PlayerEditor& editor, const juce::String& parameterID, const juce::String& name)
    : attachment (editor.player.parameters, parameterID, slider)
{
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (12.0f));
    label.setColour (juce::Label::textColourId, SF2Look::dim);
    editor.addAndMakeVisible (slider);
    editor.addAndMakeVisible (label);
}

void SF2PlayerEditor::Knob::setBounds (juce::Rectangle<int> area)
{
    label.setBounds (area.removeFromTop (16));
    slider.setBounds (area);
}

// ---------------------------------------------------------------------------- editor

SF2PlayerEditor::SF2PlayerEditor (SF2PlayerProcessor& p)
    : AudioProcessorEditor (p), player (p),
      gainAttachment (p.parameters, "gain", gainSlider),
      toneAttachment (p.parameters, "tone", toneSlider),
      layerModeAttachment (p.parameters, "layerMode", layerModeBox),
      splitAttachment (p.parameters, "splitKey", splitSlider),
      layerLevelAttachment (p.parameters, "layerLevel", layerLevelSlider),
      sendsAttachment (p.parameters, "sendOutputs", sendsToggle),
      reverbOnAttachment (p.parameters, "fsReverbOn", reverbToggle),
      chorusOnAttachment (p.parameters, "fsChorusOn", chorusToggle),
      playModeAttachment (p.parameters, "playMode", playMode),
      guitarPanelAttachment (p.parameters, "guitarPanel", guitarPanelBox),
      guitarModeAttachment (p.parameters, "guitarMode", guitarModeBox),
      strumDirectionAttachment (p.parameters, "strumDirection", strumDirectionBox),
      tremoloSyncAttachment (p.parameters, "tremoloSync", tremoloSyncBox),
      patternAttachment (p.parameters, "strumPattern", patternBox),
      tuningAttachment (p.parameters, "guitarTuning", tuningBox),
      velocityStringsAttachment (p.parameters, "strumByVelocity", velocityStringsToggle)
{
    setLookAndFeel (&look);

    juce::PropertiesFile::Options options;
    options.applicationName = "SF2 Player";
    // The test tools open editors too: they use a folder of their own, so a snapshot never adds
    // its test SoundFont to the user's recent files (it did, Nov 11).
    const char* testFolder = std::getenv ("SF2PLAYER_SETTINGS_FOLDER");
    options.folderName = testFolder != nullptr ? juce::String (testFolder) : juce::String ("SF2 Player");
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (options);
    recentFiles.setMaxNumberOfItems (12);
    recentFiles.restoreFromString (settings->getValue ("recentFiles"));
    showHints = settings->getBoolValue ("showHints", true);
    showControllers = settings->getBoolValue ("guitarControllers", false);
    showEffectControls = settings->getBoolValue ("showEffectControls", false);

    fileBox.setTextWhenNothingSelected ("Choose a SoundFont...");
    fileBox.onChange = [this]
    {
        const int id = fileBox.getSelectedId();
        if (id == openItemId)
            chooseFile();
        else if (id > 0)
            load (recentFiles.getFile (id - 1));
        refreshFileBox (*player.state());
    };
    addAndMakeVisible (fileBox);

    infoLabel.setFont (juce::FontOptions (12.0f));
    infoLabel.setColour (juce::Label::textColourId, SF2Look::dim);
    addAndMakeVisible (infoLabel);
    authorLabel.setFont (juce::FontOptions (12.0f, juce::Font::italic));
    authorLabel.setColour (juce::Label::textColourId, SF2Look::dim);
    authorLabel.setInterceptsMouseClicks (false, false);   // clicks reach the file box under it
    addAndMakeVisible (authorLabel);

    favouriteButton.setTooltip ("Add this instrument to your favorites (kept for every project)");
    favouriteButton.onClick = [this] { toggleFavourite(); };
    addAndMakeVisible (favouriteButton);

    previousButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    addAndMakeVisible (previousButton);
    addAndMakeVisible (nextButton);

    searchBox.setTextToShowWhenEmpty ("Search", SF2Look::dim);
    searchBox.setColour (juce::TextEditor::backgroundColourId, SF2Look::panel);
    searchBox.setColour (juce::TextEditor::outlineColourId, SF2Look::line);
    searchBox.setColour (juce::TextEditor::focusedOutlineColourId, SF2Look::accent);
    searchBox.setColour (juce::TextEditor::textColourId, SF2Look::text);
    searchBox.setFont (juce::FontOptions (14.0f));
    searchBox.setIndents (8, 6);
    searchBox.onTextChange = [this] { fillPresetBox(); };
    searchBox.onReturnKey = [this]   // the first match
    {
        if (presetBox.getNumItems() > 0)
            presetBox.setSelectedItemIndex (0);
    };
    searchBox.onEscapeKey = [this] { searchBox.clear(); fillPresetBox(); };
    addAndMakeVisible (searchBox);

    presetBox.setTextWhenNothingSelected ("No instruments");
    presetBox.onChange = [this]
    {
        if (const int id = presetBox.getSelectedId(); id > 0)
            player.selectPreset (presetIndexOf (id));
    };
    addAndMakeVisible (presetBox);

    gainLabel.setColour (juce::Label::textColourId, SF2Look::dim);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    gainSlider.setTextValueSuffix (" dB");
    gainSlider.setColour (juce::Slider::textBoxOutlineColourId, SF2Look::line);
    addAndMakeVisible (gainLabel);
    addAndMakeVisible (gainSlider);

    toneLabel.setColour (juce::Label::textColourId, SF2Look::dim);
    toneSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    toneSlider.setColour (juce::Slider::textBoxOutlineColourId, SF2Look::line);
    toneSlider.setTooltip ("High cut: softens the hiss some recordings carry. Off at the right end.");
    addAndMakeVisible (toneLabel);
    addAndMakeVisible (toneSlider);

    sendsToggle.setTooltip ("Off: the reverb and chorus play here. On: their sends leave on the plugin's "
                            "Reverb Send and Chorus Send outputs, dry, for Ableton's own effects.");
    sendsToggle.onClick = [this] { repaint(); };
    addAndMakeVisible (sendsToggle);

    chorusRate.slider.setTextValueSuffix (" Hz");
    chorusDepth.slider.setTextValueSuffix (" ms");
    reverbToggle.setTooltip ("Reverb on or off");
    chorusToggle.setTooltip ("Chorus on or off");
    addAndMakeVisible (reverbToggle);
    addAndMakeVisible (chorusToggle);

    playMode.addItemList ({ "Poly", "Mono", "Legato" }, 1);
    playMode.setSelectedItemIndex (juce::roundToInt (p.parameters.getRawParameterValue ("playMode")->load()), juce::dontSendNotification);
    playMode.setTooltip ("Poly: as the SoundFont plays. Mono: one note at a time. Legato: one note, connected notes don't restart.");
    playModeLabel.setJustificationType (juce::Justification::centred);
    playModeLabel.setFont (juce::FontOptions (12.0f));
    playModeLabel.setColour (juce::Label::textColourId, SF2Look::dim);
    addAndMakeVisible (playMode);
    addAndMakeVisible (playModeLabel);

    // Guitar panel.
    auto setUpCombo = [this] (juce::ComboBox& box, const juce::StringArray& items, const char* id, const juce::String& tip)
    {
        box.addItemList (items, 1);
        box.setSelectedItemIndex (juce::roundToInt (player.parameters.getRawParameterValue (id)->load()), juce::dontSendNotification);
        box.setTooltip (tip);
        addAndMakeVisible (box);
    };
    setUpCombo (guitarPanelBox, { "Auto", "On", "Off" }, "guitarPanel", {});
    guitarPanelBox.setVisible (false);   // holds the parameter; the panel button beside the instrument sets it
    panelButton.setTooltip ("Which panel this instrument gets: detected automatically (guitars get the Guitar panel), or chosen "
                            "here when the detection is wrong (remembered for this instrument)");
    panelButton.setColour (juce::TextButton::buttonColourId, SF2Look::panel);
    panelButton.onClick = [this] { showPanelMenu(); };
    addAndMakeVisible (panelButton);
    setUpCombo (guitarModeBox, { "Solo", "Harmony", "Chords", "Bass & Chord", "Bass & Pick" }, "guitarMode", "The playing mode");
    {
        juce::StringArray tunings;
        for (const char* name : GuitarEngine::tuningNames) tunings.add (name);
        setUpCombo (tuningBox, tunings, "guitarTuning", "The guitar's tuning: how chords are voiced, drawn and shown on the neck");
    }
    velocityStringsToggle.setTooltip ("Soft strums catch only the high strings, hard ones all six");
    {
        int id = 1;
        for (const auto& preset : guitarPresetList) guitarPresets.addItem (preset.name, id++);
        guitarPresets.setTooltip ("Starting points for the Guitar panel: they set its controls (Off: the SoundFont as it is)");
        guitarPresets.onChange = [this] { if (guitarPresets.getSelectedId() > 0) applyGuitarPreset (guitarPresets.getSelectedId() - 1); };
        addAndMakeVisible (guitarPresets);
    }
    addAndMakeVisible (velocityStringsToggle);
    {
        juce::StringArray rhythms;
        for (const auto& rhythm : GuitarEngine::rhythms) rhythms.add (rhythm.name);
        setUpCombo (patternBox, rhythms, "strumPattern", "Pattern mode's rhythm, in time with Ableton");
    }
    {
        juce::StringArray names;
        for (const char* name : GuitarEngine::velocityFxNames) names.add (name);
        setUpCombo (velocityFxBox, names, "velocityFx",
                    "Velocity Switch FX: a slow strum above or below the velocity set (chord modes), or soft notes muted (Velo Mute)");
        names.clear();
        for (const char* name : GuitarEngine::harmonyNames) names.add (name);
        setUpCombo (harmonyBox, names, "harmonyInterval", "What each key adds to its note");
        names.clear();
        for (const char* name : GuitarEngine::addOnNames) names.add (name);
        setUpCombo (addOnBox, names, "addOnKeys",
                    "The black keys among Bass & Pick's string keys: Unison doubles the strings, Chromatic plays a semitone below them");
    }
    fxVelocitySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 34, 18);
    fxTimeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 18);
    addAndMakeVisible (fxVelocitySlider);
    addAndMakeVisible (fxTimeSlider);
    for (auto* slider : { &stringsTopSlider, &stringsBottomSlider, &repeatStringsSlider })
        addAndMakeVisible (*slider);
    fxVelocitySlider.setTooltip ("The velocity the Velocity FX switches at");
    fxTimeSlider.setTooltip ("How long a slow strum takes");
    stringsTopSlider.setTooltip ("Chords: the highest string strummed (1: the high E)");
    stringsBottomSlider.setTooltip ("Chords: the lowest string strummed (6: the low E)");
    repeatStringsSlider.setTooltip ("How many upper strings the Repeat keys strum");
    chordBassToggle.setTooltip ("A major or minor chord over another bass is played as such (Am/G)");
    alterBassToggle.setTooltip ("The same chord again plays its second bass (the fifth), alternating");
    bassMonoToggle.setTooltip ("The second bass stops the first");
    for (auto* toggle : { &chordBassToggle, &alterBassToggle, &bassMonoToggle })
        addAndMakeVisible (*toggle);
    for (int i = 0; i < (int) positionButtons.size(); ++i)
    {
        auto& button = positionButtons[(size_t) i];
        button.setButtonText (GuitarEngine::positionNames[i]);
        button.getProperties().set ("guitarMode", true);
        button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff4a3526));
        button.setColour (juce::TextButton::buttonOnColourId, SF2Look::accent);
        button.setColour (juce::TextButton::textColourOffId, juce::Colour (0xfff1e6c8));
        button.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1d140d));
        button.setTooltip ("Chord Position: where chords lie on the neck (I: open, up to IV), or Kbd: in the octave you play them");
        button.onClick = [this, i]
        {
            if (auto* parameter = player.parameters.getParameter ("chordPosition"))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) i));
        };
        addAndMakeVisible (button);
    }
    {
        juce::StringArray names;
        for (const char* name : GuitarEngine::articulationNames) names.add (name);
        const char* keys[] = { "C1", "C#1", "D1", "D#1" };
        for (int i = 0; i < 4; ++i)
        {
            setUpCombo (switchBoxes[(size_t) i], names, ("keySwitch" + juce::String (i + 1)).toRawUTF8(),
                        juce::String ("Solo: holding ") + keys[i] + " plays the Main zone with this articulation");
            switchLabels[(size_t) i].setText (keys[i], juce::dontSendNotification);
        }
        setUpCombo (pedalBox, names, "pedalFx", "Solo: holding the sustain pedal plays with this articulation (Off: the pedal sustains)");
        setUpCombo (soloFxBox, { "Off", "H.O.", "Slide" }, "soloFx",
                    "Solo, always: H.O. (hammer-on, a pull-off when let go) or Slide between legato notes up to Steps apart");
        setUpCombo (pbBox, { "Pitch", "Slide", "Mono Bend" }, "pbAssign",
                    "The pitch bender: Pitch (as usual), Slide (by semitones, along the frets) or Mono Bend (the lowest note only)");
        setUpCombo (mwBox, { "Modulation", "Pitch", "Slide", "Mono Bend" }, "mwAssign", "The mod wheel's job");
        setUpCombo (atBox, { "Off", "Pitch", "Modulation" }, "atAssign", "Aftertouch's job");
    }
    // Small numbers as bars: drag (or double-click to type) to change them.
    for (auto* slider : { &fxStepsSlider, &legatoStepsSlider, &bassZoneTopSlider, &pbRangeSlider, &mwRangeSlider, &atRangeSlider,
                          &stringsTopSlider, &stringsBottomSlider, &repeatStringsSlider })
    {
        slider->setColour (juce::Slider::trackColourId, juce::Colour (0xff6b4c33));
        slider->setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xfff1e6c8));
        slider->setColour (juce::Slider::backgroundColourId, juce::Colour (0xff2a1f17));
        slider->setMouseDragSensitivity (120);
        slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff4a3526));
        addAndMakeVisible (*slider);
    }
    for (auto* slider : { &legatoTimeSlider, &accentSlider, &releaseTimeSlider })
    {
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, slider == &accentSlider ? 34 : 50, 18);
        addAndMakeVisible (*slider);
    }
    fxStepsSlider.setTooltip ("How far a slide or bend goes, in semitones");
    legatoStepsSlider.setTooltip ("The widest step H.O. and Slide take, in semitones");
    legatoTimeSlider.setTooltip ("How long a slide takes");
    bassZoneToggle.setTooltip ("Notes up to the key next to it play firmer and without articulations: a bass line under the melody");
    bassZoneTopSlider.setTooltip ("The bass zone's highest key");
    accentSlider.setTooltip ("Accent: the velocity where soft playing turns hard");
    releaseTimeSlider.setTooltip ("Release time: how long notes ring after they're let go (the same as PLAY's Release)");
    for (auto* range : { &pbRangeSlider, &mwRangeSlider, &atRangeSlider }) range->setTooltip ("How far, in semitones");
    addAndMakeVisible (bassZoneToggle);
    for (int i = 0; i < 4; ++i)
    {
        auto& button = curveButtons[(size_t) i];
        static const char* const glyphs[] = { "/", ")", "(", "S" };
        button.setButtonText (glyphs[i]);
        button.getProperties().set ("guitarMode", true);
        button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff4a3526));
        button.setColour (juce::TextButton::buttonOnColourId, SF2Look::accent);
        button.setColour (juce::TextButton::textColourOffId, juce::Colour (0xfff1e6c8));
        button.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1d140d));
        button.setTooltip (juce::String ("Velocity curve: ") + GuitarEngine::velocityCurveNames[i]);
        button.onClick = [this, i]
        {
            if (auto* parameter = player.parameters.getParameter ("guitarVeloCurve"))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) i));
        };
        addAndMakeVisible (button);
    }
    controllersButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    controllersButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffb8ab90));
    controllersButton.setTooltip ("Show or hide the velocity curve, Accent, Release and the wheels' jobs");
    controllersButton.onClick = [this] { toggleControllers(); };
    addAndMakeVisible (controllersButton);
    for (int i = 0; i < 6; ++i)
    {
        auto& button = stringButtons[(size_t) i];
        static const char* const names[] = { "E", "A", "D", "G", "B", "e" };
        button.setButtonText (names[i]);
        button.getProperties().set ("guitarMode", true);
        button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a1f17));
        button.setColour (juce::TextButton::buttonOnColourId, SF2Look::accent);
        button.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff8a7a62));
        button.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1d140d));
        button.setTooltip ("Strings: the ones lit are strummed (low E to high E). Click one to leave it out or bring it back");
        button.onClick = [this, i] { clickString (i); };
        addAndMakeVisible (button);
    }
    for (auto* label : { &fxLabel, &fxVelocityLabel, &fxTimeLabel, &positionLabel, &stringsLabel, &stringsDash, &repeatStringsLabel,
                         &harmonyLabel, &addOnLabel, &rhythmLabel, &optionsNote, &fxStepsLabel, &permanentLabel, &legatoStepsLabel,
                         &legatoTimeLabel, &pedalLabel, &switchLabels[0], &switchLabels[1], &switchLabels[2], &switchLabels[3],
                         &keySwitchesLabel, &wheelsLabel,
                         &curveLabel, &accentLabel, &releaseTimeLabel, &pbLabel, &mwLabel, &atLabel })
    {
        label->setFont (juce::FontOptions (12.0f));
        label->setColour (juce::Label::textColourId, SF2Look::dim);
        label->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (*label);
    }
    stringsDash.setJustificationType (juce::Justification::centred);
    optionsNote.setJustificationType (juce::Justification::centredLeft);
    setUpCombo (strumDirectionBox, { "Down", "Up", "Alternate" }, "strumDirection",
                "Down: low string to high. Up: high to low. Alternate: down, up, down...");
    setUpCombo (tremoloSyncBox, { "Free", "1/2", "1/4", "1/8", "1/16", "1/4T", "1/8T" }, "tremoloSync",
                "Tremolo at Rate (Free) or locked to Ableton's tempo");
    for (auto* label : { &guitarModeLabel, &strumDirectionLabel, &tuningLabel })
    {
        label->setJustificationType (juce::Justification::centred);
        label->setFont (juce::FontOptions (12.0f));
        label->setColour (juce::Label::textColourId, SF2Look::dim);
        addAndMakeVisible (*label);
    }
    strum.slider.setTooltip ("Strum time: how long a chord takes from the first string to the last (Off: a pick\'s natural 18 ms)");
    palmMute.slider.setTooltip ("Notes under velocity 45 are stopped by the hand: shorter and darker");
    humanize.slider.setTooltip ("Small random changes of velocity, timing and tuning on every note");
    doubleMix.slider.setTooltip ("A second take: the same notes 18 ms later, a little sharp, on the right; the original on the left");
    tremoloDepth.slider.setTooltip ("Stereo tremolo depth (0: off)");
    tremoloRate.slider.setTooltip ("Tremolo speed when Sync is Free");
    addAndMakeVisible (neck);
    // Strum in the title bar; Double, Humanize and the tremolo in the Controllers rows: sliders, not knobs.
    for (auto* knob : { &strum, &doubleMix, &humanize, &tremoloDepth, &tremoloRate })
    {
        knob->slider.setSliderStyle (juce::Slider::LinearHorizontal);
        knob->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
        knob->label.setFont (juce::FontOptions (12.0f));
        knob->label.setColour (juce::Label::textColourId, SF2Look::dim);
        knob->label.setJustificationType (juce::Justification::centredRight);
    }
    helpButton.setTooltip ("The guide, About, and the hints at the top");
    helpButton.onClick = [this] { showHelpMenu(); };
    addAndMakeVisible (helpButton);
    hintLabel.setFont (juce::FontOptions (12.0f));
    hintLabel.setColour (juce::Label::textColourId, SF2Look::dim);
    hintLabel.setJustificationType (juce::Justification::centredLeft);
    hintLabel.setMinimumHorizontalScale (0.8f);
    hintLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (hintLabel);
    {
        static const char* const modeNames[] = { "Solo", "Harmony", "Chords", "Bass & Chord", "Bass & Pick" };
        static const char* const modeTips[] = {
            "Solo: notes as you play them. C5-C6 repeat the notes held (black keys muted)",
            "Harmony: one note at a time, with the interval chosen below. C5-C6 repeat it",
            "Chords: the chord taken in the Main zone (E1-B4) is recognized and strummed. C1-D#1 and C5-C6 strum it again: "
            "C, E, G up; D, F, A, B down; black keys muted",
            "Bass & Chord: the chord's bass plays; C5 the bass, D5 the second bass, the other Repeat keys strum the upper strings",
            "Bass & Pick: the chord's bass plays; C5 bass, D5 second bass, E5 F5 G5 A5 the 4th to 1st strings; C1-D#1 strum the upper four" };
        for (int mode = 0; mode < GuitarEngine::modeCount; ++mode)
        {
            auto& button = modeButtons[(size_t) mode];
            button.setButtonText (modeNames[mode]);
            button.getProperties().set ("guitarMode", true);
            button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff4a3526));
            button.setColour (juce::TextButton::buttonOnColourId, SF2Look::accent);
            button.setColour (juce::TextButton::textColourOffId, juce::Colour (0xfff1e6c8));
            button.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1d140d));
            button.setTooltip (modeTips[mode]);
            button.onClick = [this, mode]
            {
                guitarModeBox.setSelectedItemIndex (mode, juce::sendNotificationSync);
                if (directButton.getToggleState()) directButton.setToggleState (false, juce::sendNotificationSync);   // a mode means playing it
                showMode();
            };
            addAndMakeVisible (button);
        }
        for (auto* button : { &patternButton, &directButton, &holdButton })
        {
            button->setClickingTogglesState (true);
            button->getProperties().set ("guitarMode", true);
            button->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a1f17));
            button->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff6fbf5a));
            button->setColour (juce::TextButton::textColourOffId, juce::Colour (0xffb8ab90));
            button->setColour (juce::TextButton::textColourOnId, juce::Colour (0xff10180c));
            button->onClick = [this] { showMode(); };
            addAndMakeVisible (*button);
        }
        // Velocity FX in view: short names on the switches, the full ones in their tooltips.
        static const char* const fxShort[] = { "Off", "Slow if Hard", "Slow if Soft", "Slide Up", "Slide Down", "Bend", "Rev. Bend", "Velo Mute" };
        for (int i = 0; i < (int) std::size (fxShort); ++i)
        {
            auto button = std::make_unique<LedButton> (fxShort[i]);
            button->setClickingTogglesState (false);
            button->setTooltip (juce::String ("Velocity FX: ") + GuitarEngine::velocityFxNames[i]);
            button->onClick = [this, i] { velocityFxBox.setSelectedItemIndex (i, juce::sendNotificationSync); showMode(); };
            addChildComponent (*button);
            fxButtons.push_back (std::move (button));
        }
        for (int i = 0; i < GuitarEngine::rhythmCount; ++i)
        {
            auto button = std::make_unique<LedButton> (GuitarEngine::rhythms[i].name);
            button->setClickingTogglesState (false);   // one lit at a time: the one chosen
            button->setTooltip (juce::String ("Pattern plays ") + GuitarEngine::rhythms[i].name);
            button->onClick = [this, i] { patternBox.setSelectedItemIndex (i, juce::sendNotificationSync); showMode(); };
            addChildComponent (*button);
            rhythmButtons.push_back (std::move (button));
        }
        patternButton.setTooltip ("Pattern: the chord held in the Main zone plays the rhythm chosen below by itself, in time with Ableton");
        directButton.setTooltip ("Direct: a plain sampler, every note as it comes (the SoundFont as it is). A mode button turns it off");
        holdButton.setTooltip ("Hold: chords keep sounding after the keys are let go, until the next chord");
        guitarModeBox.onChange = [this, previous = guitarModeBox.onChange]
        {
            if (previous) previous();
            showMode();
        };
        showMode();
    }
    keysButton.setTooltip ("Show or hide the keyboard");
    keysButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    keysButton.setColour (juce::TextButton::textColourOffId, SF2Look::dim);
    keysButton.onClick = [this] { toggleSection (keyboardSection); };
    addAndMakeVisible (keysButton);
    neck.setTooltip ("Click a string at a fret to draw a chord and see its name; click the name to hear it");
    neck.onShapeChanged = [this] (const std::array<int, 6>& frets) { player.setGuitarShape (frets); };
    neck.onAudition = [this]
    {
        std::array<int, 6> notes {};
        const int count = GuitarEngine::shapeNotes (player.guitarShapeFrets(), notes.data(), GuitarEngine::tuningOf (player.guitarTuning()));
        if (count == 0) return;
        stopAudition();
        auditionNotes = notes;
        auditionCount = count;
        auditionTicks = 30;   // 1.5 s at 20 Hz
        for (int i = 0; i < count; ++i) player.keyboardState.noteOn (1, notes[(size_t) i], 0.8f);
    };
    attack.slider.setTooltip ("How long notes take to start: x1 is the SoundFont's own");
    release.slider.setTooltip ("How long notes take to fade after the key is let go: x1 is the SoundFont's own");
    velocityCurve.slider.setTooltip ("+ makes soft playing louder, - makes it softer. Linear: velocities as played");
    glide.slider.setTooltip ("Slide from one note to the next (Legato: only between connected notes)");
    glide.slider.setSkewFactorFromMidPoint (250.0);   // half a turn is 250 ms: short glides need the room

    layerLabel.setColour (juce::Label::textColourId, SF2Look::dim);
    layerLevelLabel.setColour (juce::Label::textColourId, SF2Look::dim);
    layerModeBox.addItemList ({ "Off", "Layer", "Split" }, 1);
    layerModeBox.setSelectedItemIndex (juce::roundToInt (p.parameters.getRawParameterValue ("layerMode")->load()), juce::dontSendNotification);
    layerModeBox.setTooltip ("Layer: a second instrument plays every note too. Split: it plays the keys below the split.");
    layerModeBox.onChange = [this] { refreshLayerControls(); };
    layerPresetBox.setTextWhenNothingSelected ("Second instrument");
    layerPresetBox.onChange = [this]
    {
        if (const int id = layerPresetBox.getSelectedId(); id > 0)
            player.selectLayerPreset (presetIndexOf (id));
    };
    splitSlider.setTooltip ("Split: keys below this one play the second instrument");
    splitSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 22);
    splitSlider.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    splitSlider.onValueChange = [this] { refreshLayerControls(); };
    layerLevelSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    layerLevelSlider.setTextValueSuffix (" dB");
    layerLevelSlider.setColour (juce::Slider::textBoxOutlineColourId, SF2Look::line);
    layerLevelSlider.setTooltip ("The second instrument's level");
    for (auto* c : std::initializer_list<juce::Component*> { &layerLabel, &layerModeBox, &layerPresetBox, &splitSlider, &layerLevelLabel, &layerLevelSlider })
        addAndMakeVisible (c);

    fillEffectPresets();
    for (auto* box : { &reverbPresets, &chorusPresets })
    {
        box->setColour (juce::ComboBox::backgroundColourId, SF2Look::background);
        box->setTooltip ("Starting points: they set the four knobs");
        addAndMakeVisible (*box);
    }
    reverbPresets.onChange = [this] { if (reverbPresets.getSelectedId() > 0) applyEffectPreset (true, reverbPresets.getSelectedId() - 1); };
    chorusPresets.onChange = [this] { if (chorusPresets.getSelectedId() > 0) applyEffectPreset (false, chorusPresets.getSelectedId() - 1); };

    {
        juce::Path triangle;
        triangle.addTriangle (0.0f, 0.0f, 0.0f, 10.0f, 8.7f, 5.0f);
        previewButton.setShape (triangle, false, true, false);
        previewButton.setBorderSize (juce::BorderSize<int> (8, 9, 8, 7));
        previewButton.setTooltip ("Play a note of this instrument");
        previewButton.onClick = [this] { preview(); };
        addAndMakeVisible (previewButton);
    }

    warningLabel.setFont (juce::FontOptions (12.0f));
    warningLabel.setColour (juce::Label::textColourId, SF2Look::warning);
    addAndMakeVisible (warningLabel);

    keyboard.setAvailableRange (21, 108);   // a piano's 88 keys
    keyboard.setVelocity (0.8f, true);      // clicking lower on a key plays louder
    keyboard.setScrollButtonsVisible (false);
    // The computer's keys belong to the host (Ableton's own computer keyboard): JUCE's default
    // mapping (A W S E D...) played them here too once the window had focus, and a key let go
    // after focus moved never sent its note-off, so the note stuck.
    keyboard.clearKeyMappings();
    keyboard.setWantsKeyboardFocus (false);
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, juce::Colours::transparentBlack);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (keyboard);

    player.addChangeListener (this);
    refresh();
    startTimerHz (20);
    setResizable (true, true);
    {
        const auto folded = juce::StringArray::fromTokens (settings->getValue ("collapsedSections"), ",", "");
        collapsed = { folded.contains ("effects"), folded.contains ("play"), folded.contains ("guitar"), folded.contains ("keyboard") };
    }
    applyHeight();
    showSectionContents();
    refreshLayerControls();
    refreshGuitarPanel();
    refreshPanelButton();
    neck.setShape (player.guitarShapeFrets(), GuitarEngine::tuningOf (player.guitarTuning()));
    showGuitarPreset();
    // A value beside a slider is shown, not typed into (a box that took a click for a text field
    // only confused); a double-click on the slider takes it back to neutral.
    for (auto* child : getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*> (child))
        {
            slider->setTextBoxIsEditable (false);
            if (slider->getSliderStyle() == juce::Slider::LinearHorizontal)
                slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        }
    gainSlider.setDoubleClickReturnValue (true, 0.0);
    toneSlider.setDoubleClickReturnValue (true, toneSlider.getMaximum());
    layerLevelSlider.setDoubleClickReturnValue (true, 0.0);
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SF2PlayerEditor> (this)]
                                     { if (safe != nullptr) safe->fitToScreen(); });
}

SF2PlayerEditor::~SF2PlayerEditor()
{
    if (previewNote >= 0)
        player.keyboardState.noteOff (1, previewNote, 0.0f);
    stopAudition();   // a drawn chord still sounding from a click on its name
    player.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

bool SF2PlayerEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && (files[0].endsWithIgnoreCase (".sf2") || files[0].endsWithIgnoreCase (".sf3"));
}

void SF2PlayerEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragging = false;
    repaint();
    if (isInterestedInFileDrag (files))
        load (juce::File (files[0]));
}

void SF2PlayerEditor::chooseFile()
{
    const auto st = player.state();   // one consistent snapshot
    chooser = std::make_unique<juce::FileChooser> ("Choose a SoundFont", st->file, "*.sf2;*.sf3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  load (fc.getResult());
                          });
}

void SF2PlayerEditor::load (const juce::File& file)
{
    if (! file.existsAsFile())
    {
        recentFiles.removeFile (file);
        refreshFileBox (*player.state());
        return;
    }
    if (! player.loadSoundFont (file))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Okami SF2 Player",
                                                file.getFileName() + " couldn't be read as a SoundFont.");
}

void SF2PlayerEditor::stepPreset (int delta)
{
    const int count = presetBox.getNumItems();
    if (count > 1)
        presetBox.setSelectedItemIndex ((presetBox.getSelectedItemIndex() + delta + count) % count);
}

void SF2PlayerEditor::refreshFileBox (const SF2PlayerProcessor::State& state)
{
    const auto* st = &state;
    // The open file's entry also says how many instruments it holds: "HQ_Orchestral (195 instruments)".
    const auto count = st->presets.size();
    const auto instruments = count == 1 ? juce::String ("1 instrument") : juce::String ((int) count) + " instruments";
    fileBox.clear (juce::dontSendNotification);
    for (int i = 0; i < recentFiles.getNumFiles(); ++i)
    {
        const auto file = recentFiles.getFile (i);
        const bool open = file == st->file && count > 0;
        fileBox.addItem (file.getFileNameWithoutExtension() + (open ? "  (" + instruments + ")" : juce::String()), i + 1);
    }
    if (recentFiles.getNumFiles() > 0)
        fileBox.addSeparator();
    fileBox.addItem ("Open SoundFont...", openItemId);

    int current = -1;
    for (int i = 0; i < recentFiles.getNumFiles() && current < 0; ++i)
        if (recentFiles.getFile (i) == st->file)
            current = i;
    fileBox.setSelectedId (current >= 0 ? current + 1 : 0, juce::dontSendNotification);
    fileBox.setTooltip (st->file.getFullPathName());
}

void SF2PlayerEditor::refresh()
{
    const auto st = player.state();   // one consistent snapshot
    const auto file = st->file;
    if (file.existsAsFile())
    {
        recentFiles.addFile (file);
        settings->setValue ("recentFiles", recentFiles.toString());
        settings->saveIfNeeded();
    }
    refreshFileBox (*st);   // the same snapshot all the way down, never a newer one halfway

    const auto& presets = st->presets;
    fillPresetBox (*st);

    // "by <author>" when the font names one (the file box already names the file and its count)
    const auto missing = st->missing;
    infoLabel.setColour (juce::Label::textColourId, missing != juce::File() ? SF2Look::warning : SF2Look::dim);
    if (missing != juce::File())
    {
        infoLabel.setText (missing.existsAsFile() ? "Can't open " + missing.getFileName() + " (damaged, or not a SoundFont?). Choose another above."
                                                  : "Can't find " + missing.getFileName() + " (moved or renamed?). Choose it again above.",
                           juce::dontSendNotification);
        infoLabel.setTooltip (missing.getFullPathName());
    }
    else if (! file.existsAsFile())
    {
        infoLabel.setText ("Drop an .sf2 or .sf3 file here, or choose one above.", juce::dontSendNotification);
    }
    else
    {
        infoLabel.setText ({}, juce::dontSendNotification);   // the author sits in the file box now
    }
    {
        const auto& info = st->info;
        authorLabel.setText (file.existsAsFile() && info.author.isNotEmpty() ? "by " + info.author : juce::String(), juce::dontSendNotification);
        fileBox.setTooltip (st->file.getFullPathName() + (info.comment.isNotEmpty() ? "\n\n" + info.comment : juce::String()));
        placeAuthor();
    }

    // Without anything to say the row goes, and the window gets that much shorter.
    const bool showInfo = infoLabel.getText().isNotEmpty();
    if (showInfo != infoLabel.isVisible() || getHeight() == 0)
    {
        // The row comes or goes: the window grows or shrinks by its height, keeping the user's size otherwise.
        const bool first = getHeight() == 0;
        infoLabel.setVisible (showInfo);
        if (first) setSize (620, minimumHeight());
        applyHeight();
    }

    const int index = st->preset;
    if (index >= 0 && index < (int) presets.size())
    {
        keyboard.lowKey = presets[(size_t) index].lowKey;
        keyboard.highKey = presets[(size_t) index].highKey;
    }
    keyboard.repaint();
    repaint();   // the panels say what this instrument sends
}

// The instruments, under a heading per bank; with text in the search box, only the matches.
// General MIDI's sixteen families of eight programs, plus the drum kits (bank 128).
static const char* const familyNames[] = {
    "Piano", "Chromatic Percussion", "Organ", "Guitar", "Bass", "Strings", "Ensemble & Choir", "Brass",
    "Reed (Sax, Oboe, Clarinet)", "Pipe (Flutes)", "Synth Lead", "Synth Pad", "Synth Effects", "Ethnic",
    "Percussive", "Sound Effects", "Drum Kits" };

// Bank 0 of a General MIDI collection follows the standard, so its program number is its family.
// Variation banks often don't (this collection keeps a trombone on 1:023, an organ slot), so
// their names decide, and the program number only when no word matches.
static int familyOf (const SF2PlayerProcessor::Preset& p)
{
    if (p.bank == 128)
        return 16;
    const int byProgram = juce::jlimit (0, 15, p.number / 8);
    if (p.bank == 0)
        return byProgram;

    // Plain arrays: an initializer_list here would point at temporaries gone after initialisation.
    struct Rule { int family; const char* words[8]; };
    static const Rule rules[] = {
        { 6,  { "strings", "choir", "orchestra" } },
        { 0,  { "piano", "rhodes", " ep", "harpsi", "clavinet" } },   // before "harp"
        { 9,  { "flute", "piccolo", "recorder", "ocarina", "whistle", "shakuhachi" } },   // before "(wws)"
        { 5,  { "(str)", "violin", "viola", "cello", "contrabass", "harp" } },
        { 8,  { "(wws)", "sax", "oboe", "clarinet", "bassoon", "english horn" } },
        { 7,  { "brass", "trumpet", "trombone", "tuba", "french horn" } },
        { 10, { "lead", "square", "saw" } },
        { 3,  { "guitar", "gt", "ukulele", "mandolin" } },
        { 2,  { "organ", "accordion" } },
    };
    const auto name = p.name.toLowerCase();
    for (const auto& rule : rules)
        for (const auto* word : rule.words)
            if (word != nullptr && name.contains (word))
                return rule.family;
    return byProgram;
}

// "(STR) Violin  [040]"; a variation bank's preset shows its bank: "(STR) Pizz Violins  [1:001]".
// The number is drawn dimmer (SF2Look).
static juce::String presetLabel (const SF2PlayerProcessor::Preset& p)
{
    const auto number = juce::String (p.number).paddedLeft ('0', 3);
    return p.name + "  [" + (p.bank == 0 || p.bank == 128 ? number : juce::String (p.bank) + ":" + number) + "]";
}

// A General MIDI collection (most of bank 0 filled) is listed by family, its variation banks
// beside their originals; any other font under a heading per bank. The search matches names
// and family names ("brass", "flutes").
// Fills `box` with the instruments: favorites first (copies, ids from favouriteIdBase), then a
// General MIDI collection (most of bank 0 filled) by family, its variation banks beside their
// originals; any other font under a heading per bank. The query matches names and family names
// ("brass", "flutes"). Returns how many instruments it lists, favorites not counted twice.
int SF2PlayerEditor::addInstruments (juce::ComboBox& box, const juce::String& query, const SF2PlayerProcessor::State& state,
                                     const juce::StringArray& favourite) const
{
    const auto* st = &state;
    const auto& presets = st->presets;
    box.clear (juce::dontSendNotification);

    const auto bankZero = std::count_if (presets.begin(), presets.end(), [] (const auto& p) { return p.bank == 0; });
    const bool byFamily = bankZero >= 64;

    std::vector<size_t> order (presets.size());
    std::iota (order.begin(), order.end(), size_t { 0 });
    if (byFamily)
        std::stable_sort (order.begin(), order.end(), [&] (size_t a, size_t b)
        {
            const auto& x = presets[a]; const auto& y = presets[b];
            return std::tuple (familyOf (x), x.number, x.bank) < std::tuple (familyOf (y), y.number, y.bank);
        });

    bool favouritesHeading = false;
    for (const auto i : order)
    {
        const auto& p = presets[i];
        if (! favourite.contains (favouriteKey (*st, (int) i)) || (query.isNotEmpty() && ! p.name.containsIgnoreCase (query)))
            continue;
        if (! favouritesHeading)
            box.addSectionHeading (juce::String::fromUTF8 ("\xe2\x98\x85 Favorites")), favouritesHeading = true;
        box.addItem (presetLabel (p), favouriteIdBase + (int) i + 1);
    }

    int lastGroup = -1, shown = 0;
    for (const auto i : order)
    {
        const auto& p = presets[i];
        const int group = byFamily ? familyOf (p) : p.bank;
        const juce::String heading = byFamily ? juce::String (familyNames[group])
                                   : p.bank == 128 ? juce::String ("Bank 128 (drums)") : "Bank " + juce::String (p.bank);
        if (query.isNotEmpty() && ! p.name.containsIgnoreCase (query) && ! (byFamily && heading.containsIgnoreCase (query)))
            continue;
        if (group != lastGroup)
        {
            box.addSectionHeading (heading);
            lastGroup = group;
        }
        box.addItem (presetLabel (p), (int) i + 1);
        ++shown;
    }
    return shown;
}

void SF2PlayerEditor::fillPresetBox()
{
    fillPresetBox (*player.state());
}

void SF2PlayerEditor::fillPresetBox (const SF2PlayerProcessor::State& state)
{
    const auto* st = &state;
    const auto& presets = st->presets;
    const auto favourite = favourites();   // read once, not per instrument
    const int shown = addInstruments (presetBox, searchBox.getText().trim(), state, favourite);

    presetBox.setSelectedId (st->preset + 1, juce::dontSendNotification);
    if (presetBox.getSelectedId() == 0 && ! presets.empty())   // the current one is filtered out
    {
        const auto& p = presets[(size_t) juce::jlimit (0, (int) presets.size() - 1, st->preset)];
        presetBox.setText (presetLabel (p) + "   (" + juce::String (shown) + " found)", juce::dontSendNotification);
    }
    previousButton.setEnabled (shown > 1);
    nextButton.setEnabled (shown > 1);
    refreshFavouriteButton (state, favourite);
    applyPanelChoice (state);
    refreshPanelButton();

    addInstruments (layerPresetBox, {}, state, favourite);
    layerPresetBox.setSelectedId (st->layer + 1, juce::dontSendNotification);
}

juce::String SF2PlayerEditor::favouriteKey (const SF2PlayerProcessor::State& st, int presetIndex)
{
    if (! juce::isPositiveAndBelow (presetIndex, (int) st.presets.size()))
        return {};
    const auto& p = st.presets[(size_t) presetIndex];
    return st.file.getFileName() + "|" + juce::String (p.bank) + ":" + juce::String (p.number);
}

juce::StringArray SF2PlayerEditor::favourites() const
{
    auto list = juce::StringArray::fromLines (settings->getValue ("favorites"));
    list.removeEmptyStrings();   // an empty key (no instrument) must never match a blank line
    return list;
}

void SF2PlayerEditor::toggleFavourite()
{
    const auto st = player.state();   // one consistent snapshot
    const auto key = favouriteKey (*st, st->preset);
    if (key.isEmpty())
        return;
    auto favourites = this->favourites();
    if (favourites.contains (key))
        favourites.removeString (key);
    else
        favourites.add (key);
    settings->setValue ("favorites", favourites.joinIntoString ("\n"));
    settings->saveIfNeeded();
    fillPresetBox();
}

void SF2PlayerEditor::refreshFavouriteButton (const SF2PlayerProcessor::State& st, const juce::StringArray& favourite)
{
    const auto key = favouriteKey (st, st.preset);
    const bool on = key.isNotEmpty() && favourite.contains (key);
    juce::Path star;
    star.addStar ({ 0.0f, 0.0f }, 5, 4.2f, 10.0f);   // a point up
    const auto fill = on ? SF2Look::accent : juce::Colours::transparentBlack;
    favouriteButton.setColours (fill, on ? SF2Look::accent.brighter (0.2f) : SF2Look::accent.withAlpha (0.2f), fill);
    favouriteButton.setOutline (on ? SF2Look::accent : SF2Look::dim, 1.5f);
    favouriteButton.setShape (star, false, true, false);
    favouriteButton.setBorderSize (juce::BorderSize<int> (6));
    favouriteButton.setTooltip (on ? "Remove from favorites" : "Add this instrument to your favorites (kept for every project)");
    favouriteButton.setEnabled (key.isNotEmpty());
}

// ---------------------------------------------------------------------------- effect presets

namespace
{
struct EffectPreset { const char* name; float values[4]; };
// Level, room, damping, width. "Default" is FluidSynth's own.
const EffectPreset reverbPresetList[] = {
    { "Default",   { 0.70f, 0.50f, 0.20f, 0.80f } },
    { "Room",      { 0.50f, 0.30f, 0.50f, 0.60f } },
    { "Studio",    { 0.45f, 0.40f, 0.40f, 0.80f } },
    { "Hall",      { 0.65f, 0.80f, 0.30f, 1.00f } },
    { "Church",    { 0.70f, 0.92f, 0.20f, 1.00f } },
    { "Cathedral", { 0.75f, 1.00f, 0.10f, 1.00f } },
    { "Short Room", { 0.45f, 0.25f, 0.25f, 0.70f } },   // a small bright room, for acoustic guitar
};
// Level, rate (Hz), depth (ms), voices.
const EffectPreset chorusPresetList[] = {
    { "Default",  { 0.60f, 0.20f, 4.25f, 3.0f } },
    { "Subtle",   { 0.40f, 0.30f, 2.00f, 2.0f } },
    { "Wide",     { 0.80f, 0.40f, 6.00f, 4.0f } },
    { "Ensemble", { 1.00f, 0.60f, 8.00f, 6.0f } },
    { "Bright",   { 0.90f, 0.70f, 5.00f, 3.0f } },   // a clear, quick shimmer, for acoustic guitar
};
const char* const reverbIds[] = { "fsReverbLevel", "fsReverbRoom", "fsReverbDamping", "fsReverbWidth" };
const char* const chorusIds[] = { "fsChorusLevel", "fsChorusRate", "fsChorusDepth", "fsChorusVoices" };
} // namespace

void SF2PlayerEditor::fillEffectPresets()
{
    int id = 1;
    for (const auto& preset : reverbPresetList) reverbPresets.addItem (preset.name, id++);
    id = 1;
    for (const auto& preset : chorusPresetList) chorusPresets.addItem (preset.name, id++);
    showEffectPreset (true);
    showEffectPreset (false);
}

void SF2PlayerEditor::applyEffectPreset (bool reverb, int index)
{
    const auto& preset = reverb ? reverbPresetList[index] : chorusPresetList[index];
    const auto* const* ids = reverb ? reverbIds : chorusIds;
    for (int i = 0; i < 4; ++i)
    {
        auto* parameter = player.parameters.getParameter (ids[i]);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (preset.values[i]));
        parameter->endChangeGesture();
    }
}

// The menu names the preset the knobs match, or says "Custom".
void SF2PlayerEditor::showEffectPreset (bool reverb)
{
    auto& box = reverb ? reverbPresets : chorusPresets;
    const auto* const* ids = reverb ? reverbIds : chorusIds;
    const int count = reverb ? (int) std::size (reverbPresetList) : (int) std::size (chorusPresetList);
    int match = 0;
    for (int p = 0; p < count && match == 0; ++p)
    {
        const auto& preset = reverb ? reverbPresetList[p] : chorusPresetList[p];
        bool same = true;
        for (int i = 0; i < 4; ++i)
            same = same && std::abs (player.parameters.getRawParameterValue (ids[i])->load() - preset.values[i]) < 0.006f;
        if (same)
            match = p + 1;
    }
    if (match != box.getSelectedId() || (match == 0 && box.getText() != "Custom"))
    {
        box.setSelectedId (match, juce::dontSendNotification);
        if (match == 0)
            box.setText ("Custom", juce::dontSendNotification);
    }
}

// A note in the instrument's range (middle C when it plays it), 0.7 s long, through the
// on-screen keyboard's state so it plays like a click on a key.
void SF2PlayerEditor::preview()
{
    const auto st = player.state();   // one consistent snapshot
    const auto& presets = st->presets;
    const int index = st->preset;
    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return;
    if (previewNote >= 0)
        player.keyboardState.noteOff (1, previewNote, 0.0f);
    const auto& p = presets[(size_t) index];
    previewNote = (60 >= p.lowKey && 60 <= p.highKey) ? 60 : (p.lowKey + p.highKey) / 2;
    player.keyboardState.noteOn (1, previewNote, 0.8f);
    previewTicks = 14;   // 20 Hz timer: 0.7 s
}

// The layer's controls fade when it's off; the split key only matters in Split; the keyboard
// tints the keys the second instrument plays.
void SF2PlayerEditor::refreshLayerControls()
{
    const int mode = layerModeBox.getSelectedItemIndex();   // 0 off, 1 layer, 2 split
    layerPresetBox.setAlpha (mode > 0 ? 1.0f : 0.4f);
    layerLevelSlider.setAlpha (mode > 0 ? 1.0f : 0.4f);
    layerLevelLabel.setAlpha (mode > 0 ? 1.0f : 0.4f);
    splitSlider.setAlpha (mode == 2 ? 1.0f : 0.4f);
    keyboard.splitKey = mode == 2 ? juce::roundToInt (splitSlider.getValue()) : -1;
    keyboard.repaint();
}

void SF2PlayerEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}


void SF2PlayerEditor::applyGuitarPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) std::size (guitarPresetList)))
        return;
    for (size_t i = 0; i < std::size (guitarPresetIds); ++i)
        if (auto* parameter = player.parameters.getParameter (guitarPresetIds[i]))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (guitarPresetList[index].values[i]));
    for (const char* id : retiredGuitarIds)
        if (auto* parameter = player.parameters.getParameter (id))
            parameter->setValueNotifyingHost (parameter->getDefaultValue());
}

// The menu names the preset the controls match, else invites a choice ("Presets...", dimmed).
void SF2PlayerEditor::showGuitarPreset()
{
    for (int p = 0; p < (int) std::size (guitarPresetList); ++p)
    {
        bool matches = true;
        for (size_t i = 0; i < std::size (guitarPresetIds) && matches; ++i)
            matches = std::abs (player.parameters.getRawParameterValue (guitarPresetIds[i])->load() - guitarPresetList[p].values[i]) < 0.01f;
        if (matches)
        {
            if (guitarPresets.getSelectedId() != p + 1) guitarPresets.setSelectedId (p + 1, juce::dontSendNotification);
            return;
        }
    }
    if (guitarPresets.getSelectedId() != 0 || guitarPresets.getTextWhenNothingSelected().isEmpty())
    {
        guitarPresets.setTextWhenNothingSelected (juce::String::fromUTF8 ("Presets\xe2\x80\xa6"));
        guitarPresets.setSelectedId (0, juce::dontSendNotification);
    }
}

// Shows or hides the Guitar panel (Auto follows the instrument), growing the window by its height.
void SF2PlayerEditor::refreshGuitarPanel()
{
    const bool show = player.guitarPanelActive();
    tremoloRate.slider.setEnabled (tremoloSyncBox.getSelectedItemIndex() <= 0);
    if (show == guitarShown)
        return;
    // The height first: setResizeLimits already moves the window to a new minimum, and the
    // panel's height was then added on top of that (the window came out far too tall).
    guitarShown = show;
    showSectionContents();
    applyHeight();
    resized();
    repaint();
    if (show)   // a guitar: what's played with comes first, the effects and PLAY fold away
    {
        // The layer has no place on a guitar (its zones take the keyboard): it goes off.
        if (auto* layer = player.parameters.getParameter ("layerMode"); layer != nullptr && layer->getValue() > 0.0f)
        {
            layer->setValueNotifyingHost (0.0f);
            layerModeBox.setSelectedItemIndex (0, juce::dontSendNotification);
            refreshLayerControls();
        }
        for (auto section : { effectsSection, playSection })
            if (! collapsed[(size_t) section]) toggleSection (section);
        fitToScreen (guitarSection);
    }
}

// Folds the effects, then PLAY, then the controllers' rows (never `keep`) until the window fits
// the screen it's on, then shrinks it to fit.
void SF2PlayerEditor::fitToScreen (int keep)
{
    if (fitting) return;
    const juce::ScopedValueSetter<bool> busy (fitting, true);
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = isShowing() ? displays.getDisplayForRect (getScreenBounds()) : displays.getPrimaryDisplay();
    if (display == nullptr) return;
    const int available = display->userArea.getHeight() - 60;   // the host's title bar and a margin
    for (auto section : { effectsSection, playSection })
        if (minimumHeight() > available && section != keep && ! collapsed[(size_t) section])
            toggleSection (section);
    if (minimumHeight() > available && showControllers && keep != -2)
        toggleControllers();
}

void SF2PlayerEditor::applyHeight()
{
    const int height = minimumHeight();
    setResizeLimits (620, height, 1600, height);
    if (getHeight() != height) setSize (getWidth(), height);
}

// The window's height: exactly what it shows (the same pieces as resized()).
int SF2PlayerEditor::minimumHeight() const
{
    int height = 46 + soundHeight();   // the title, then SOUNDFONT
    height += 12 + generalHeight();
    height += 12 + (guitarShown ? heightOf (guitarSection) : pianoHeight);
    height += 12 + (collapsed[keyboardSection] ? collapsedHeight : 26 + zoneBand + keyboardHeight + 10);   // PIANO
    return height + 14;
}

void SF2PlayerEditor::toggleSection (Section s)
{
    collapsed[(size_t) s] = ! collapsed[(size_t) s];
    const bool opened = ! collapsed[(size_t) s];
    juce::StringArray folded;
    for (auto [section, name] : { std::pair { effectsSection, "effects" }, std::pair { playSection, "play" },
                                  std::pair { guitarSection, "guitar" }, std::pair { keyboardSection, "keyboard" } })
        if (collapsed[(size_t) section]) folded.add (name);
    settings->setValue ("collapsedSections", folded.joinIntoString (","));
    settings->saveIfNeeded();
    showSectionContents();
    applyHeight();
    resized();
    repaint();
    if (opened) fitToScreen (s);   // opened: the others make room
}

// What a folded section hides: its knobs and menus. Its title strip keeps the on/off switches,
// the presets and the Guitar menu, so they stay at hand.
void SF2PlayerEditor::showSectionContents()
{
    keyboard.setVisible (! collapsed[keyboardSection]);
    keysButton.setButtonText (juce::String::fromUTF8 (collapsed[keyboardSection] ? "\xe2\x96\xb8" : "\xe2\x96\xbe"));
    const bool effects = ! collapsed[effectsSection] && showEffectControls;
    reverbPresets.setVisible (showEffectControls);
    chorusPresets.setVisible (showEffectControls);
    for (auto* knob : { &reverbLevel, &reverbRoom, &reverbDamping, &reverbWidth, &chorusLevel, &chorusRate, &chorusDepth, &chorusVoices })
        knob->slider.setVisible (effects), knob->label.setVisible (effects);
    const bool play = ! collapsed[playSection];
    for (auto* knob : { &attack, &release, &velocityCurve, &transpose, &fineTune, &bendRange, &glide })
        knob->slider.setVisible (play), knob->label.setVisible (play);
    playMode.setVisible (play);
    playModeLabel.setVisible (play);
    const bool guitar = guitarShown && ! collapsed[guitarSection];
    strum.slider.setVisible (guitar);
    strum.label.setVisible (guitar);
    for (juce::Component* gone : { (juce::Component*) &palmMute.slider, (juce::Component*) &palmMute.label,
                                   (juce::Component*) &strumDirectionBox, (juce::Component*) &strumDirectionLabel,
                                   (juce::Component*) &velocityStringsToggle })
        gone->setVisible (false);   // not in the panel any more (neutral)
    for (auto& button : modeButtons) button.setVisible (guitar);
    for (auto* button : { &patternButton, &directButton, &holdButton }) button->setVisible (guitar);
    guitarModeBox.setVisible (false);
    guitarModeLabel.setVisible (false);
    neck.setVisible (guitar);
    guitarPresets.setVisible (guitar);
    refreshModeOptions();
}

void SF2PlayerEditor::mouseUp (const juce::MouseEvent& e)
{
    const auto at = e.getPosition();
    auto title = [] (juce::Rectangle<int> panel) { return panel.withHeight (collapsedHeight); };
    // The effects' title (or a right-click anywhere on their bars): their controls, shown or hidden.
    if (title (reverbPanel).contains (at) || title (chorusPanel).contains (at)
        || (e.mods.isPopupMenu() && (reverbPanel.contains (at) || chorusPanel.contains (at))))
        toggleEffectControls();
    else if (title (playPanel).contains (at)) toggleSection (playSection);
    else if (guitarShown && title (guitarPanel).contains (at)) toggleSection (guitarSection);
    else if (title (keysPanel).contains (at)) toggleSection (keyboardSection);
}

// A section's title with its fold arrow: ▾ open, ▸ folded.
void SF2PlayerEditor::drawSectionTitle (juce::Graphics& g, juce::Rectangle<int> panel, const juce::String& title, Section s)
{
    g.setColour (SF2Look::dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    const bool folded = s == effectsSection ? ! showEffectControls || collapsed[(size_t) s] : collapsed[(size_t) s];
    g.drawText (juce::String::fromUTF8 (folded ? "\xe2\x96\xb8" : "\xe2\x96\xbe"), panel.getX() + 8, panel.getY() + 7, 12, 14,
                juce::Justification::centred);
    g.drawText (title, panel.getX() + 22, panel.getY() + 8, 100, 14, juce::Justification::centredLeft);
}

void SF2PlayerEditor::timerCallback()
{
    showHint();
    refreshGuitarPanel();
    refreshPanelButton();
    // The neck: what the Guitar panel's engine played last, and its lights fading.
    auto& view = player.guitarView;
    if (const int sequence = view.sequence.load (std::memory_order_acquire); sequence != shownGuitarSequence)
    {
        shownGuitarSequence = sequence;
        std::array<int, 6> notes {};
        for (size_t i = 0; i < 6; ++i) notes[i] = view.notes[i].load (std::memory_order_relaxed);
        neck.show (notes.data(), view.count.load(), view.pitchClasses.load(), view.bass.load(), view.up.load(),
                   view.strummed.load(), view.palm.load(), positionFret[juce::jlimit (0, 4, juce::roundToInt (player.parameters.getRawParameterValue ("chordPosition")->load()))],
                   GuitarEngine::tuningOf (player.guitarTuning()));
        // The keyboard marks the same notes, in the neck's green.
        const int count = juce::jlimit (0, 6, view.count.load());
        for (int i = 0; i < 6; ++i) keyboard.sounding[(size_t) i] = i < count ? notes[(size_t) i] : -1;
        soundingAge = 0.0f;
    }
    {
        // Bright for the neck's three seconds, then fading out over one (only with the Guitar panel playing).
        soundingAge += 1.0f / 20.0f;
        const float alpha = ! guitarShown || directButton.getToggleState() ? 0.0f
                          : soundingAge < 3.0f ? 1.0f : juce::jmax (0.0f, 1.0f - (soundingAge - 3.0f));
        if (std::abs (alpha - keyboard.soundingAlpha) > 0.01f)
        {
            keyboard.soundingAlpha = alpha;
            keyboard.repaint();
        }
    }
    {
        // The board shades the Chord Position while a chord mode plays (not Kbd, not Direct).
        const int mode = guitarModeBox.getSelectedItemIndex();
        const int position = juce::roundToInt (player.parameters.getRawParameterValue ("chordPosition")->load());
        const bool chordMode = mode == GuitarEngine::chords || mode == GuitarEngine::bassAndChord || mode == GuitarEngine::bassAndPick;
        neck.setPosition (chordMode && ! directButton.getToggleState() && position < 4 ? position : -1);
        neck.setPattern (patternButton.getToggleState() && chordMode && ! directButton.getToggleState() ? patternBox.getText() : juce::String());
    }
    neck.tick (1.0f / 20.0f);
    {
        // The keys held now, for the PIANO title: "C E G · C".
        juce::StringArray names;
        int classes = 0, lowest = -1;
        for (int n = 0; n < 128; ++n)
            if (player.keyboardState.isNoteOnForChannels (0xffff, n))
            {
                names.add (juce::MidiMessage::getMidiNoteName (n, true, false, 3));
                classes |= 1 << (n % 12);
                if (lowest < 0) lowest = n;
            }
        juce::String text;
        if (! names.isEmpty())
        {
            text = names.joinIntoString (" ");
            if (names.size() > 1)
                if (const auto chord = ChordName::name (classes, lowest % 12); chord.isNotEmpty())
                    text << juce::String::fromUTF8 ("  \xc2\xb7  ") << chord;
        }
        if (text != heldText) { heldText = text; repaint (keysPanel.withHeight (26)); }
    }
    if (guitarShown) showGuitarPreset();
    neck.setShape (player.guitarShapeFrets(), GuitarEngine::tuningOf (player.guitarTuning()));
    {
        // The host or a preset may have changed the mode, Pattern, Direct or the position.
        showMode();
        const int tint = layerModeBox.getSelectedItemIndex() == 2 ? juce::roundToInt (splitSlider.getValue()) : -1;
        const bool zones = guitarShown && ! directButton.getToggleState();
        if (keyboard.splitKey != tint || keyboard.repeatZones != zones) { keyboard.splitKey = tint; keyboard.repeatZones = zones; keyboard.repaint(); }
    }
    if (auditionCount > 0 && --auditionTicks <= 0)
        stopAudition();
    const float peak = player.outputPeak.exchange (0.0f);
    shownPeak = juce::jmax (peak, shownPeak * 0.85f);

    const int notes = player.notesReceived.load();
    if (notes != lastNoteCount)
        midiFlash = 4, lastNoteCount = notes;
    else if (midiFlash > 0)
        --midiFlash;

    // Velocity 1-15 is nearly silent: say so, it isn't the plugin.
    const int velocity = player.lastVelocity.load();
    const bool soft = notes > 0 && velocity > 0 && velocity < 16;
    warningLabel.setText (soft ? "Notes are arriving very soft (velocity " + juce::String (velocity)
                                     + "). Raise their velocity in the clip."
                               : juce::String(),
                          juce::dontSendNotification);

    // A switched-off effect's knobs fade, and so do both panels' when the sends go out to Ableton.
    const bool sends = sendsToggle.getToggleState();
    for (auto [on, knobs] : { std::pair { reverbToggle.getToggleState() && ! sends, std::array<Knob*, 4> { &reverbLevel, &reverbRoom, &reverbDamping, &reverbWidth } },
                              std::pair { chorusToggle.getToggleState() && ! sends, std::array<Knob*, 4> { &chorusLevel, &chorusRate, &chorusDepth, &chorusVoices } } })
        for (auto* knob : knobs)
            knob->slider.setAlpha (on ? 1.0f : 0.35f);

    showEffectPreset (true);
    showEffectPreset (false);
    if (previewNote >= 0 && --previewTicks <= 0)
    {
        player.keyboardState.noteOff (1, previewNote, 0.0f);
        previewNote = -1;
    }

    repaint (meterArea.expanded (20, 4));
}

void SF2PlayerEditor::paint (juce::Graphics& g)
{
    const auto st = player.state();   // one consistent snapshot
    g.fillAll (SF2Look::background);

    g.setColour (SF2Look::text);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText ("Okami SF2 Player", 16, 12, 260, 22, juce::Justification::centredLeft);
    g.setColour (SF2Look::dim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String ("v") + JucePlugin_VersionString, 166, 15, 60, 18, juce::Justification::centredLeft);


    // SOUNDFONT: graphite under the file and the instrument.
    {
        const auto area = soundPanel.toFloat();
        g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff26252c), area.getY(), juce::Colour (0xff1d1c22), area.getBottom()));
        g.fillRoundedRectangle (area, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (SF2Look::dim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("SOUNDFONT", soundPanel.getX() + 12, soundPanel.getY() + 7, 120, 14, juce::Justification::centredLeft);
    }
    // GENERAL: a cool slate under everything every instrument has.
    {
        const auto area = generalPanel.toFloat();
        g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff262b34), area.getY(), juce::Colour (0xff1c2027), area.getBottom()));
        g.fillRoundedRectangle (area, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (SF2Look::dim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("GENERAL", generalPanel.getX() + 12, generalPanel.getY() + 7, 120, 14, juce::Justification::centredLeft);
    }
    // The MIDI light and the output meter, in GENERAL's title bar.
    const auto dot = juce::Rectangle<float> ((float) meterArea.getX() - 16.0f, (float) meterArea.getCentreY() - 3.5f, 7.0f, 7.0f);
    g.setColour (midiFlash > 0 ? SF2Look::accent : juce::Colour (0xff12151a));
    g.fillEllipse (dot);

    g.setColour (juce::Colour (0xff12151a));
    g.fillRoundedRectangle (meterArea.toFloat(), 2.0f);
    const float db = juce::Decibels::gainToDecibels (shownPeak, -60.0f);
    const float fraction = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    g.setColour (db > -1.0f ? SF2Look::warning : SF2Look::accent);
    g.fillRoundedRectangle (meterArea.toFloat().withWidth ((float) meterArea.getWidth() * fraction), 2.0f);

    // PIANO (or INSTRUMENT): ink blue, for what a non-guitar instrument has of its own.
    if (! guitarShown && ! pianoPanel.isEmpty())
    {
        const auto area = pianoPanel.toFloat();
        g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff1f2742), area.getY(), juce::Colour (0xff161b2e), area.getBottom()));
        g.fillRoundedRectangle (area, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (juce::Colour (0xffa9b4d6));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("INSTRUMENT", pianoPanel.getX() + 12, pianoPanel.getY() + 7, 160, 14, juce::Justification::centredLeft);
    }
    // PIANO: a deep green-black under the keyboard.
    {
        const auto area = keysPanel.toFloat();
        g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff1f2a27), area.getY(), juce::Colour (0xff171f1d), area.getBottom()));
        g.fillRoundedRectangle (area, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);
        drawSectionTitle (g, keysPanel, "PIANO", keyboardSection);
        // What's held now: its notes, and the chord they make.
        if (heldText.isNotEmpty())
        {
            g.setColour (SF2Look::text.withAlpha (0.85f));
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            // At the right end of the title row, clear of the zones' names.
            g.drawFittedText (heldText, keysPanel.getRight() - 10 - 150, keysPanel.getY() + 7, 150, 14, juce::Justification::centredRight, 1, 0.7f);
        }
        // With the Guitar panel: what its zones are, over them.
        if (keyboard.repeatZones && keyboard.isVisible())
        {
            auto spanX = [this] (int from, int to)   // the keys' extent, in the editor
            {
                const float x0 = (float) keyboard.getX() + keyboard.getKeyStartPosition (from);
                const float x1 = (float) keyboard.getX() + keyboard.getKeyStartPosition (to) + keyboard.getKeyWidth();
                return std::pair { x0, x1 };
            };
            const float y = (float) keysPanel.getY() + 6.0f;   // in the title row, over the keys
            auto zone = [&] (int from, int to, const juce::String& label, juce::Colour colour)
            {
                const auto [x0, x1] = spanX (from, to);
                g.setColour (colour);
                g.fillRoundedRectangle (x0 + 1.0f, (float) keyboard.getY() - 4.0f, x1 - x0 - 2.0f, 2.5f, 1.0f);
                g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
                g.drawText (label, juce::Rectangle<float> (x0 - 30.0f, y, x1 - x0 + 60.0f, 10.0f), juce::Justification::centred);
            };
            const auto beige = juce::Colour (0xffd8c39a), green = juce::Colour (0xff8fbf9a);
            zone (GuitarEngine::leftRepeatLow, GuitarEngine::leftRepeatHigh,
                  guitarModeBox.getSelectedItemIndex() == GuitarEngine::solo ? "KEY SWITCHES" : "REPEAT", beige);
            zone (GuitarEngine::mainLow, GuitarEngine::mainHigh, "MAIN", green);
            zone (GuitarEngine::rightRepeatLow, GuitarEngine::rightRepeatHigh, "REPEAT", beige);
        }
    }
    g.setColour (juce::Colour (0xff2c313b));
    g.fillRoundedRectangle (playPanel.toFloat(), 6.0f);
    drawSectionTitle (g, playPanel, "PLAY", playSection);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("x1, Linear, 0, Poly and Off leave the SoundFont as it is", playPanel.getX() + 64, playPanel.getY() + 8,
                playPanel.getWidth() - 76 - 120, 14, juce::Justification::centredLeft);

    if (guitarShown)
    {
        // Warm wood under the guitar's controls, darker towards the neck.
        const auto wood = guitarPanel.toFloat();
        g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff3d2b1e), wood.getY(), juce::Colour (0xff221912), wood.getBottom()));
        g.fillRoundedRectangle (wood, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawRoundedRectangle (wood.reduced (0.5f), 6.0f, 1.0f);
        drawSectionTitle (g, guitarPanel, "GUITAR", guitarSection);
        if (! collapsed[guitarSection] && ! optionsFrame.isEmpty())
        {
            // The mode's options in a box of their own, named after it.
            // The chosen tab's folder: its options (the tab above names the mode).
            const auto box = optionsFrame.toFloat();
            g.setColour (FolderTab::box);
            g.fillRoundedRectangle (box, 6.0f);
            g.setColour (juce::Colour (0xfff1e6c8).withAlpha (0.14f));
            g.drawRoundedRectangle (box.reduced (0.5f), 6.0f, 1.0f);
            if (! patternFrame.isEmpty())
            {
                const auto pattern = patternFrame.toFloat();
                g.setColour (juce::Colours::black.withAlpha (0.18f));
                g.fillRoundedRectangle (pattern, 6.0f);
                g.setColour (juce::Colour (0xff6fe07a).withAlpha (0.18f));
                g.drawRoundedRectangle (pattern.reduced (0.5f), 6.0f, 1.0f);
                g.setColour (juce::Colour (0xffb8ab90));
                g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
                g.drawText ("PATTERN RHYTHM", patternFrame.getX() + 10, patternFrame.getY() + 5, 240, 14, juce::Justification::centredLeft);
            }
        }
    }

    const bool sends = sendsToggle.getToggleState();
    for (auto [area, title] : { std::pair { reverbPanel, "REVERB" }, std::pair { chorusPanel, "CHORUS" } })
    {
        g.setColour (juce::Colour (0xff2c313b));
        g.fillRoundedRectangle (area.toFloat(), 6.0f);
        drawSectionTitle (g, area, title, effectsSection);
        // What the chosen instrument sends to this effect, and where it goes.
        const auto& presets = st->presets;
        const int index = st->preset;
        if (juce::isPositiveAndBelow (index, (int) presets.size()))
        {
            const auto& amounts = presets[(size_t) index].sends;
            const bool reverb = juce::String (title) == "REVERB";
            const float low = reverb ? amounts.reverbMin : amounts.chorusMin;
            const float high = reverb ? amounts.reverbMax : amounts.chorusMax;
            auto percent = [] (float v) { return juce::String (v, v < 10.0f && ! juce::exactlyEqual (v, std::floor (v)) ? 1 : 0) + " %"; };
            // "sends 10 %"; with the separate outputs on, "sends 10 % → out" (beside the preset menu, it must fit).
            const auto amount = high <= 0.0f ? juce::String ("nothing")
                              : low < high ? juce::String (juce::roundToInt (low)) + juce::String::fromUTF8 ("\xe2\x80\x93") + percent (high)
                                           : percent (high);
            const auto text = sends ? "sends " + amount + juce::String::fromUTF8 (" \xe2\x86\x92 out")
                                    : "sends " + amount;
            g.setColour (high > 0.0f ? (sends ? SF2Look::accent : SF2Look::text) : SF2Look::dim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (text, area.getX() + 84, area.getY() + 8, area.getWidth() - 84 - 32 - 96, 14, juce::Justification::centredLeft);
        }
    }

    if (dragging)
    {
        g.setColour (SF2Look::accent);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 8.0f, 2.0f);
    }
}

void SF2PlayerEditor::resized()
{
    auto area = getLocalBounds().reduced (16, 0);

    auto header = area.removeFromTop (46);
    helpButton.setBounds (header.removeFromRight (24).withSizeKeepingCentre (24, 22));
    hintLabel.setBounds (header.withTrimmedLeft (220).withTrimmedRight (10).withSizeKeepingCentre (header.getWidth() - 230, 30));
    if (guide != nullptr) guide->setBounds (getLocalBounds());

    // SOUNDFONT: the file and the instrument chosen from it.
    soundPanel = area.removeFromTop (soundHeight());
    auto sound = soundPanel.reduced (10, 0).withTrimmedTop (26);
    fileBox.setBounds (sound.removeFromTop (28));
    placeAuthor();
    if (infoLabel.isVisible())
    {
        sound.removeFromTop (2);
        infoLabel.setBounds (sound.removeFromTop (20).withTrimmedLeft (-4));
    }
    sound.removeFromTop (8);
    auto row = sound.removeFromTop (28);
    previousButton.setBounds (row.removeFromLeft (28));
    nextButton.setBounds (row.removeFromRight (28));
    row.removeFromRight (6);
    favouriteButton.setBounds (row.removeFromRight (28));
    previewButton.setBounds (row.removeFromRight (28));
    row.removeFromRight (4);
    panelButton.setBounds (row.removeFromRight (104).reduced (0, 2));
    row.reduce (6, 0);
    searchBox.setBounds (row.removeFromLeft (130));
    row.removeFromLeft (6);
    presetBox.setBounds (row);

    // GENERAL: what every instrument has. Gain and Tone, the effects, PLAY; the separate outputs
    // switch in its title bar.
    area.removeFromTop (panelGap);
    generalPanel = area.removeFromTop (generalHeight());
    auto general = generalPanel.reduced (10, 0);
    sendsToggle.setBounds (general.getRight() - 210, generalPanel.getY() + 2, 210, 22);
    // The MIDI light and the output meter: what comes in and what goes out, after the title.
    meterArea = juce::Rectangle<int> (generalPanel.getX() + 112, generalPanel.getY() + 11, 110, 6);
    general.removeFromTop (26);
    row = general.removeFromTop (24);
    auto toneRow = row.removeFromRight ((row.getWidth() - 16) / 2);
    row.removeFromRight (16);
    gainLabel.setBounds (row.removeFromLeft (40));
    gainSlider.setBounds (row);
    toneLabel.setBounds (toneRow.removeFromLeft (40));
    toneSlider.setBounds (toneRow);
    general.removeFromTop (10);
    auto panels = general.removeFromTop (heightOf (effectsSection));
    reverbPanel = panels.removeFromLeft ((panels.getWidth() - 12) / 2);
    panels.removeFromLeft (12);
    chorusPanel = panels;
    for (auto [panel, toggle, knobs] : { std::tuple { reverbPanel, &reverbToggle, std::array<Knob*, 4> { &reverbLevel, &reverbRoom, &reverbDamping, &reverbWidth } },
                                         std::tuple { chorusPanel, &chorusToggle, std::array<Knob*, 4> { &chorusLevel, &chorusRate, &chorusDepth, &chorusVoices } } })
    {
        toggle->setBounds (panel.getRight() - 32, panel.getY() + 4, 26, 22);
        (toggle == &reverbToggle ? reverbPresets : chorusPresets).setBounds (panel.getRight() - 32 - 92, panel.getY() + 6, 88, 18);
        if (collapsed[effectsSection] || ! showEffectControls)
            continue;
        auto inner = panel.reduced (6).withTrimmedTop (18);
        const int width = inner.getWidth() / 4;
        for (auto* knob : knobs)
            knob->setBounds (inner.removeFromLeft (width));
    }

    general.removeFromTop (10);
    playPanel = general.removeFromTop (heightOf (playSection));
    if (! collapsed[playSection])
    {
        auto inner = playPanel.reduced (6).withTrimmedTop (18);
        const int width = inner.getWidth() / 8;
        for (auto* knob : { &attack, &release, &velocityCurve, &transpose, &fineTune, &bendRange, &glide })
            knob->setBounds (inner.removeFromLeft (width));
        auto modeArea = inner;
        playModeLabel.setBounds (modeArea.removeFromTop (16));
        playMode.setBounds (modeArea.withSizeKeepingCentre (juce::jmin (modeArea.getWidth() - 4, 84), 24).translated (0, -6));
    }





    // The instrument's own panel: GUITAR, or PIANO / INSTRUMENT (the layer).
    area.removeFromTop (panelGap);
    if (guitarShown)
    {
        guitarPanel = area.removeFromTop (heightOf (guitarSection));
        pianoPanel = {};
        // The title bar: presets, the Controllers button, Strum and Hold.
        guitarPresets.setBounds (guitarPanel.getX() + 82, guitarPanel.getY() + 5, 150, 18);
        controllersButton.setBounds (guitarPresets.getRight() + 8, guitarPanel.getY() + 4, 100, 20);
        holdButton.setBounds (guitarPanel.getRight() - 8 - 58, guitarPanel.getY() + 4, 58, 21);
        strum.slider.setBounds (holdButton.getX() - 10 - 130, guitarPanel.getY() + 4, 130, 21);
        strum.label.setBounds (strum.slider.getX() - 44, guitarPanel.getY() + 4, 42, 21);
        auto inner = guitarPanel.reduced (6).withTrimmedTop (guitarHeader - 6);
        auto strip = inner.removeFromBottom (neckHeight() - 6);
        if (patternBoxShown)   // Pattern's rhythms, under the mode's options
        {
            patternFrame = strip.removeFromBottom (patternBoxHeight).reduced (8, 0);
            strip.removeFromBottom (6);
            auto grid = patternFrame.reduced (10, 0).withTrimmedTop (22).withTrimmedBottom (10);
            const int columns = 4, gap = 4;
            const int width = (grid.getWidth() - gap * (columns - 1)) / columns;
            for (int i = 0; i < (int) rhythmButtons.size(); ++i)
                rhythmButtons[(size_t) i]->setBounds (grid.getX() + (i % columns) * (width + gap), grid.getY() + (i / columns) * (24 + 4), width, 24);
        }
        else
            patternFrame = {};
        optionsFrame = strip.removeFromBottom (optionsHeight (optionRows)).reduced (8, 0);
        optionsArea = optionsFrame.reduced (8, 0).withTrimmedTop (10).withTrimmedBottom (8);
        // The mode tabs sit on the box (the chosen one runs into it, over its top edge).
        auto modes = strip.removeFromBottom (28).reduced (8, 0).translated (0, 1);   // Pattern, the five tabs, Direct
        patternButton.setBounds (modes.removeFromLeft (86).withTrimmedBottom (6).withTrimmedTop (1));
        directButton.setBounds (modes.removeFromRight (78).withTrimmedBottom (6).withTrimmedTop (1));
        modes.reduce (8, 0);
        const int gap = 3, buttonWidth = (modes.getWidth() - gap * (GuitarEngine::modeCount - 1)) / GuitarEngine::modeCount;
        for (auto& button : modeButtons)
        {
            button.setBounds (modes.removeFromLeft (buttonWidth));
            modes.removeFromLeft (gap);
        }
        strip.removeFromBottom (6);
        neck.setBounds (strip.reduced (8, 0));
        refreshModeOptions();
    }
    else
    {
        guitarPanel = {};
        pianoPanel = area.removeFromTop (pianoHeight);
        auto inner = pianoPanel.reduced (10, 0).withTrimmedTop (28).withHeight (26);
        layerLabel.setBounds (inner.removeFromLeft (44));
        layerModeBox.setBounds (inner.removeFromLeft (84));
        inner.removeFromLeft (6);
        layerLevelSlider.setBounds (inner.removeFromRight (140));
        layerLevelLabel.setBounds (inner.removeFromRight (40));
        inner.removeFromRight (6);
        splitSlider.setBounds (inner.removeFromRight (100));
        inner.removeFromRight (6);
        layerPresetBox.setBounds (inner);
    }
    for (auto* c : std::initializer_list<juce::Component*> { &layerLabel, &layerModeBox, &layerPresetBox, &splitSlider, &layerLevelLabel, &layerLevelSlider })
        c->setVisible (! guitarShown);   // a guitar's keyboard belongs to its zones
    // PIANO: the keyboard, folding to its title (a click on it).
    area.removeFromTop (panelGap);
    keysPanel = area.removeFromTop (collapsed[keyboardSection] ? collapsedHeight : 26 + zoneBand + keyboardHeight + 10);
    keysButton.setBounds ({});
    warningLabel.setBounds (keysPanel.getX() + 80, keysPanel.getY() + 2, keysPanel.getWidth() - 90, 22);
    auto keys = keysPanel.reduced (10, 0).withTrimmedTop (26 + zoneBand).withHeight (keyboardHeight);
    keyboard.setBounds (keys);
    keyboard.setKeyWidth ((float) keys.getWidth() / 52.0f);   // 52 white keys in 88
    keyboard.setLowestVisibleKey (21);
}

// The mode's button lit, the others not; dimmed while Direct plays the SoundFont as it is.
void SF2PlayerEditor::showMode()
{
    const int mode = guitarModeBox.getSelectedItemIndex();
    const bool direct = directButton.getToggleState();
    for (int i = 0; i < GuitarEngine::modeCount; ++i)
    {
        auto& button = modeButtons[(size_t) i];
        const bool lit = i == mode && ! direct;   // Direct: no mode plays, none lit
        if (button.getToggleState() != lit) button.setToggleState (lit, juce::dontSendNotification);
        if (button.getAlpha() != (direct ? 0.45f : 1.0f)) button.setAlpha (direct ? 0.45f : 1.0f);
    }
    const int curve = juce::roundToInt (player.parameters.getRawParameterValue ("guitarVeloCurve")->load());
    for (int i = 0; i < (int) curveButtons.size(); ++i)
        if (curveButtons[(size_t) i].getToggleState() != (i == curve))
            curveButtons[(size_t) i].setToggleState (i == curve, juce::dontSendNotification);
    {
        // Strings are numbered from the high E (1) to the low E (6); the buttons go low to high.
        const int top = juce::roundToInt (player.parameters.getRawParameterValue ("stringsTop")->load());
        const int bottom = juce::roundToInt (player.parameters.getRawParameterValue ("stringsBottom")->load());
        for (int i = 0; i < 6; ++i)
        {
            const int number = 6 - i;
            const bool on = number >= top && number <= bottom;
            if (stringButtons[(size_t) i].getToggleState() != on) stringButtons[(size_t) i].setToggleState (on, juce::dontSendNotification);
        }
    }
    const int position = juce::roundToInt (player.parameters.getRawParameterValue ("chordPosition")->load());
    for (int i = 0; i < (int) positionButtons.size(); ++i)
        if (positionButtons[(size_t) i].getToggleState() != (i == position))
            positionButtons[(size_t) i].setToggleState (i == position, juce::dontSendNotification);
    refreshModeOptions();
    repaint (optionsFrame);
}

void SF2PlayerEditor::stopAudition()
{
    for (int i = 0; i < auditionCount; ++i) player.keyboardState.noteOff (1, auditionNotes[(size_t) i], 0.0f);
    auditionCount = 0;
}

// The options box: only what the chosen mode uses. The first row: the Velocity Switch FX (and
// Pattern's rhythm); the second, the mode's own.
void SF2PlayerEditor::refreshModeOptions()
{
    const int mode = guitarModeBox.getSelectedItemIndex();
    const bool open = guitarShown && ! collapsed[guitarSection];
    const bool chordMode = mode == GuitarEngine::chords || mode == GuitarEngine::bassAndChord || mode == GuitarEngine::bassAndPick;
    // A form: each row's name in the left column, its controls after it, extras at the right edge.
    struct Row { juce::Label* label = nullptr; std::vector<std::pair<juce::Component*, int>> left, right; bool indent = false; };
    std::vector<Row> rows;
    rows.reserve (12);
    auto row = [&rows] (juce::Label* label, bool indent = false) -> size_t { rows.push_back ({ label, {}, {}, indent }); return rows.size() - 1; };
    auto add = [&rows] (size_t r, juce::Component& c, int width) { rows[r].left.push_back ({ &c, width }); };
    auto addRight = [&rows] (size_t r, juce::Component& c, int width) { rows[r].right.push_back ({ &c, width }); };
    const int fx = velocityFxBox.getSelectedItemIndex();
    const bool rhythm = patternButton.getToggleState() && chordMode;
    auto fxDetails = [&] (size_t r)
    {
        add (r, fxVelocityLabel, 30);
        add (r, fxVelocitySlider, 84);
        if (fx != GuitarEngine::veloMute) { add (r, fxTimeLabel, 30); add (r, fxTimeSlider, 104); }
        if (fx >= GuitarEngine::slideUp && fx <= GuitarEngine::reverseBend) { add (r, fxStepsLabel, 34); add (r, fxStepsSlider, 44); }
    };
    // Pattern's own box: its rhythms, while it's on in a chord mode.
    {
        const bool shown = rhythm && guitarShown;
        if (shown != patternBoxShown)
        {
            patternBoxShown = shown;
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SF2PlayerEditor> (this)]
                                             { if (safe != nullptr) { safe->applyHeight(); safe->resized(); safe->repaint(); } });
        }
        const int current = patternBox.getSelectedItemIndex();
        for (int i = 0; i < (int) rhythmButtons.size(); ++i)
        {
            auto& button = *rhythmButtons[(size_t) i];
            if (button.isVisible() != (shown && open)) button.setVisible (shown && open);
            if (button.getToggleState() != (i == current)) button.setToggleState (i == current, juce::dontSendNotification);
        }
    }
    {
        // Velocity FX: its eight choices in two rows (the strum's, the pitch's), then its settings.
        static constexpr int order[2][4] = { { GuitarEngine::fxOff, GuitarEngine::slowStrumHigh, GuitarEngine::slowStrumLow, GuitarEngine::veloMute },
                                             { GuitarEngine::slideUp, GuitarEngine::slideDown, GuitarEngine::bend, GuitarEngine::reverseBend } };
        const int columnWidth = (optionsArea.getWidth() - 104 - 3 * 3) / 4;
        for (int line = 0; line < 2; ++line)
        {
            const auto f = line == 0 ? row (&fxLabel) : row (nullptr, true);
            for (int k = 0; k < 4; ++k) add (f, *fxButtons[(size_t) order[line][k]], columnWidth);
        }
        if (fx > 0) fxDetails (row (nullptr, true));
        for (int i = 0; i < (int) fxButtons.size(); ++i)
            if (fxButtons[(size_t) i]->getToggleState() != (i == fx)) fxButtons[(size_t) i]->setToggleState (i == fx, juce::dontSendNotification);
    }
    if (mode == GuitarEngine::solo)
    {
        const auto p = row (&permanentLabel);
        add (p, soloFxBox, 72);
        add (p, legatoStepsLabel, 34);
        add (p, legatoStepsSlider, 44);
        add (p, legatoTimeLabel, 30);
        add (p, legatoTimeSlider, 96);
        addRight (p, bassZoneToggle, 92);
        addRight (p, bassZoneTopSlider, 44);
        const auto k = row (nullptr);   // the key switches take the whole row
        for (int i = 0; i < 4; ++i) { add (k, switchLabels[(size_t) i], i == 0 ? 22 : 28); add (k, switchBoxes[(size_t) i], 74); }
        add (k, pedalLabel, 36);
        add (k, pedalBox, 66);
    }
    else if (mode == GuitarEngine::harmony)
    {
        const auto h = row (&harmonyLabel);
        add (h, harmonyBox, 150);
        const auto n = row (nullptr);
        add (n, optionsNote, optionsArea.getWidth());
        optionsNote.setText ("One note at a time with its interval; C5-C6 repeat it (black keys muted).", juce::dontSendNotification);
    }
    else
    {
        const auto p = row (&positionLabel);
        for (auto& button : positionButtons) add (p, button, button.getButtonText() == "Kbd" ? 40 : 30);
        addRight (p, chordBassToggle, 104);
        if (mode == GuitarEngine::chords)
        {
            const auto st = row (&stringsLabel);
            for (auto& button : stringButtons) add (st, button, 26);
        }
        else
        {
            const auto t = mode == GuitarEngine::bassAndChord ? row (&repeatStringsLabel) : row (&addOnLabel);
            if (mode == GuitarEngine::bassAndChord) add (t, repeatStringsSlider, 44);
            else add (t, addOnBox, 100);
            addRight (t, alterBassToggle, 98);
            addRight (t, bassMonoToggle, 98);
        }
    }
    if (showControllers)
    {
        const auto c = row (&curveLabel);
        for (auto& button : curveButtons) add (c, button, 28);
        addRight (c, accentLabel, 46);
        addRight (c, accentSlider, 130);
        const auto r = row (&releaseTimeLabel);
        add (r, releaseTimeSlider, 150);
        addRight (r, tuningLabel, 36);
        addRight (r, tuningBox, 110);
        const auto w = row (&wheelsLabel);
        for (auto [label, box, range] : { std::tuple { &pbLabel, &pbBox, &pbRangeSlider }, std::tuple { &mwLabel, &mwBox, &mwRangeSlider },
                                          std::tuple { &atLabel, &atBox, &atRangeSlider } })
        {
            add (w, *label, 28);
            add (w, *box, 82);
            add (w, *range, 28);
        }
        const auto d = row (&doubleMix.label);
        add (d, doubleMix.slider, 150);
        addRight (d, humanize.label, 64);
        addRight (d, humanize.slider, 140);
        const auto t = row (&tremoloDepth.label);
        add (t, tremoloDepth.slider, 150);
        addRight (t, tremoloRate.label, 36);
        addRight (t, tremoloRate.slider, 120);
        addRight (t, tremoloSyncBox, 74);
    }
    optionsNote.setColour (juce::Label::textColourId, SF2Look::dim);

    {
        const int count = juce::jmax (1, (int) rows.size());
        if (count != optionRows && guitarShown)
        {
            optionRows = count;
            // The box changed size: the window follows, once this layout is over.
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SF2PlayerEditor> (this)]
                                             { if (safe != nullptr) { safe->applyHeight(); safe->resized(); safe->repaint(); } });
        }
    }
    std::vector<juce::Component*> all { &fxLabel, &velocityFxBox, &fxVelocityLabel, &fxVelocitySlider, &fxTimeLabel, &fxTimeSlider, &rhythmLabel,
                                        &patternBox, &optionsNote, &harmonyLabel, &harmonyBox, &positionLabel, &stringsLabel, &stringsTopSlider,
                                        &stringsDash, &stringsBottomSlider, &repeatStringsLabel, &repeatStringsSlider, &addOnLabel, &addOnBox,
                                        &chordBassToggle, &alterBassToggle, &bassMonoToggle, &soloFxBox, &pedalBox, &fxStepsSlider,
                                        &legatoStepsSlider, &legatoTimeSlider, &bassZoneTopSlider, &bassZoneToggle, &fxStepsLabel,
                                        &permanentLabel, &legatoStepsLabel, &legatoTimeLabel, &pedalLabel, &accentSlider, &releaseTimeSlider,
                                        &pbRangeSlider, &mwRangeSlider, &atRangeSlider, &pbBox, &mwBox, &atBox, &curveLabel, &accentLabel,
                                        &releaseTimeLabel, &pbLabel, &mwLabel, &atLabel, &doubleMix.label, &doubleMix.slider,
                                        &humanize.label, &humanize.slider, &tuningLabel, &tuningBox, &tremoloDepth.label,
                                        &tremoloDepth.slider, &tremoloRate.label, &tremoloRate.slider, &tremoloSyncBox,
                                        &keySwitchesLabel, &wheelsLabel };
    for (auto& button : fxButtons) all.push_back (button.get());
    for (auto& box : switchBoxes) all.push_back (&box);
    for (auto& label : switchLabels) all.push_back (&label);
    for (auto& button : curveButtons) all.push_back (&button);
    for (auto& button : stringButtons) all.push_back (&button);
    for (auto& button : positionButtons) all.push_back (&button);
    for (auto* c : all)
    {
        bool shown = false;
        for (const auto& r : rows)
        {
            shown = shown || r.label == c;
            for (auto& item : r.left) shown = shown || item.first == c;
            for (auto& item : r.right) shown = shown || item.first == c;
        }
        shown = shown && open;
        if (c->isVisible() != shown) c->setVisible (shown);
    }
    constexpr int labelColumn = 104;
    auto setBounds = [] (juce::Component* c, juce::Rectangle<int> bounds) { if (c->getBounds() != bounds) c->setBounds (bounds); };
    auto area = optionsArea;
    for (const auto& r : rows)
    {
        auto line = area.removeFromTop (24);
        area.removeFromTop (4);
        if (r.label != nullptr)
        {
            r.label->setJustificationType (juce::Justification::centredLeft);
            setBounds (r.label, line.removeFromLeft (labelColumn));
        }
        else if (r.indent)
            line.removeFromLeft (labelColumn);
        if (! r.right.empty())   // at the right edge, read left to right
        {
            int total = -6;
            for (auto& item : r.right) total += item.second + 6;
            auto right = line.removeFromRight (juce::jmin (total, line.getWidth()));
            line.removeFromRight (6);
            for (auto [component, width] : r.right)
            {
                if (auto* label = dynamic_cast<juce::Label*> (component)) label->setJustificationType (juce::Justification::centredRight);
                setBounds (component, right.removeFromLeft (juce::jmin (width, right.getWidth())).reduced (0, 1));
                right.removeFromLeft (6);
            }
        }
        for (auto [component, width] : r.left)
        {
            if (auto* label = dynamic_cast<juce::Label*> (component); label != nullptr && component != &optionsNote)
                label->setJustificationType (juce::Justification::centredRight);
            setBounds (component, line.removeFromLeft (juce::jmin (width, line.getWidth())).reduced (0, 1));
            line.removeFromLeft (dynamic_cast<juce::TextButton*> (component) != nullptr ? 3 : 5);
        }
    }
    controllersButton.setButtonText (juce::String::fromUTF8 (showControllers ? "\xe2\x96\xbe Controllers" : "\xe2\x96\xb8 Controllers"));
    controllersButton.setVisible (open);
}

// The controllers' rows in the options box, shown or hidden; the window grows or shrinks by them.
void SF2PlayerEditor::toggleControllers()
{
    showControllers = ! showControllers;
    settings->setValue ("guitarControllers", showControllers);
    settings->saveIfNeeded();
    applyHeight();
    resized();
    repaint();
    if (showControllers) fitToScreen (-2);
}

void SF2PlayerEditor::showHelpMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Guide");
    menu.addItem (2, "About Okami SF2 Player");
    menu.addSeparator();
    menu.addItem (3, "Show hints at the top", true, showHints);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (helpButton), [this] (int chosen)
    {
        if (chosen == 1) openGuide (Guide::start);
        else if (chosen == 2) openGuide (Guide::about);
        else if (chosen == 3)
        {
            showHints = ! showHints;
            settings->setValue ("showHints", showHints);
            hintLabel.setText ({}, juce::dontSendNotification);
        }
    });
}

void SF2PlayerEditor::openGuide (Guide::Page page)
{
    if (guide == nullptr)
    {
        guide = std::make_unique<Guide> (Guide::Colours { SF2Look::background, SF2Look::panel, SF2Look::line, SF2Look::text,
                                                          SF2Look::dim, SF2Look::accent },
                                         juce::String (JucePlugin_VersionString));
        guide->onClose = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this)]
                                                                   { if (safe != nullptr) safe->guide.reset(); }); };
        addAndMakeVisible (*guide);
        guide->setBounds (getLocalBounds());
    }
    guide->show (page);
    guide->toFront (true);
    guide->grabKeyboardFocus();
}

// The hint line: the tooltip of the control under the mouse, without waiting.
void SF2PlayerEditor::showHint()
{
    juce::String hint;
    if (showHints && guide == nullptr)
        for (auto* c = juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();
             c != nullptr && c != this && isParentOf (c); c = c->getParentComponent())
            if (auto* client = dynamic_cast<juce::TooltipClient*> (c))
                if (hint = client->getTooltip(); hint.isNotEmpty()) break;
    if (hintLabel.getText() != hint) hintLabel.setText (hint, juce::dontSendNotification);
}

// The reverb's and chorus's knobs and presets, shown or not; the window grows or shrinks by them.
void SF2PlayerEditor::toggleEffectControls()
{
    // Shown but folded away (to fit the screen): open them; else show or hide.
    if (! (showEffectControls && collapsed[effectsSection]))
        showEffectControls = ! showEffectControls;
    settings->setValue ("showEffectControls", showEffectControls);
    settings->saveIfNeeded();
    collapsed[effectsSection] = false;   // the controls' visibility is this switch alone
    showSectionContents();
    applyHeight();
    resized();
    repaint();
    if (showEffectControls) fitToScreen (effectsSection);
}

// ---------------------------------------------------------------------------- panel choice

// A new instrument: its remembered choice (Guitar or No panel), else Auto.
void SF2PlayerEditor::applyPanelChoice (const SF2PlayerProcessor::State& st)
{
    const auto key = favouriteKey (st, st.preset);
    if (key.isEmpty() || key == panelKey) return;
    panelKey = key;
    const auto choices = juce::StringArray::fromLines (settings->getValue ("panelChoices"));
    float choice = 0.0f;   // Auto
    for (const auto& line : choices)
        if (line.upToLastOccurrenceOf ("=", false, false) == key)
            choice = (float) line.fromLastOccurrenceOf ("=", false, false).getIntValue();
    if (auto* parameter = player.parameters.getParameter ("guitarPanel"))
        if (juce::roundToInt (player.parameters.getRawParameterValue ("guitarPanel")->load()) != (int) choice)
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (choice));
}

void SF2PlayerEditor::showPanelMenu()
{
    const int current = juce::roundToInt (player.parameters.getRawParameterValue ("guitarPanel")->load());
    juce::PopupMenu menu;
    menu.addSectionHeader ("Panel for this instrument");
    menu.addItem (1, juce::String ("Automatic (detected: ") + (player.detectedGuitar() ? "guitar" : "standard instrument") + ")", true, current == 0);
    menu.addItem (2, "Guitar panel", true, current == 1);
    menu.addItem (3, "Standard (no extra panel)", true, current == 2);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (panelButton), [this] (int chosen)
    {
        if (chosen <= 0) return;
        const int choice = chosen - 1;   // 0 Auto, 1 Guitar (On), 2 No panel (Off)
        if (auto* parameter = player.parameters.getParameter ("guitarPanel"))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) choice));
        // Remembered for this instrument: Auto needs no entry.
        auto choices = juce::StringArray::fromLines (settings->getValue ("panelChoices"));
        choices.removeEmptyStrings();
        for (int i = choices.size(); --i >= 0;)
            if (choices[i].upToLastOccurrenceOf ("=", false, false) == panelKey) choices.remove (i);
        if (choice != 0 && panelKey.isNotEmpty()) choices.add (panelKey + "=" + juce::String (choice));
        settings->setValue ("panelChoices", choices.joinIntoString ("\n"));
        settings->saveIfNeeded();
        refreshPanelButton();
    });
}

// The button says what the instrument gets, Guitar (lit) or Standard, and whether that was detected.
void SF2PlayerEditor::refreshPanelButton()
{
    const bool guitar = player.guitarPanelActive();
    const bool automatic = juce::roundToInt (player.parameters.getRawParameterValue ("guitarPanel")->load()) == 0;
    const auto text = juce::String (guitar ? "Guitar" : "Standard") + (automatic ? juce::String::fromUTF8 (" \xc2\xb7 auto") : juce::String());
    if (panelButton.getButtonText() != text)
    {
        panelButton.setButtonText (text);
        panelButton.setColour (juce::TextButton::textColourOffId, guitar ? SF2Look::accent : SF2Look::dim);
    }
}

// A string clicked: a lit one at either end goes out, an unlit one comes in (with the ones between),
// a lit one inside moves the nearer end to it. At least one string stays.
void SF2PlayerEditor::clickString (int index)
{
    int top = juce::roundToInt (player.parameters.getRawParameterValue ("stringsTop")->load());
    int bottom = juce::roundToInt (player.parameters.getRawParameterValue ("stringsBottom")->load());
    const int number = 6 - index;
    if (number < top) top = number;
    else if (number > bottom) bottom = number;
    else if (top == bottom) return;
    else if (number == top) top = number + 1;
    else if (number == bottom) bottom = number - 1;
    else if (number - top < bottom - number) top = number;
    else bottom = number;
    for (auto [id, value] : { std::pair { "stringsTop", top }, std::pair { "stringsBottom", bottom } })
        if (auto* parameter = player.parameters.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) value));
    showMode();
}

// The author's name right after the file's name and count, inside the file box (clear of its arrow).
void SF2PlayerEditor::placeAuthor()
{
    if (authorLabel.getText().isEmpty() || fileBox.getWidth() == 0) { authorLabel.setBounds ({}); return; }
    const auto font = look.getComboBoxFont (fileBox);
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, fileBox.getText(), 0.0f, 0.0f);
    const int textEnd = fileBox.getX() + 6 + juce::roundToInt (glyphs.getBoundingBox (0, -1, true).getWidth()) + 10;
    const int right = fileBox.getRight() - 30;   // the arrow
    authorLabel.setBounds (textEnd, fileBox.getY(), juce::jmax (0, right - textEnd), fileBox.getHeight());
    authorLabel.toFront (false);
}
