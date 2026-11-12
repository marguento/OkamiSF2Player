#include "PluginEditor.h"
#include <algorithm>
#include <iterator>
#include <numeric>
#include <tuple>

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
    if (isOver) fill = fill.brighter (0.1f);
    if (isDown) fill = SF2Look::accent;
    g.setColour (fill);
    g.fillRect (area.reduced (0.5f, 0.0f));
    g.setColour (SF2Look::background);
    g.drawVerticalLine ((int) area.getRight(), area.getY(), area.getBottom());
}

void RangeKeyboard::drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                   juce::Colour)
{
    const bool playable = note >= lowKey && note <= highKey;
    auto fill = playable ? juce::Colour (0xff222226) : juce::Colour (0xff2e2e34);
    if (note < splitKey)
        fill = juce::Colour (0xff2a3a46);
    if (isOver) fill = fill.brighter (0.2f);
    if (isDown) fill = SF2Look::accent.darker (0.2f);
    g.setColour (fill);
    g.fillRoundedRectangle (area, 1.5f);
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
      patternAttachment (p.parameters, "strumPattern", patternBox)
{
    setLookAndFeel (&look);

    juce::PropertiesFile::Options options;
    options.applicationName = "SF2 Player";
    options.folderName = "SF2 Player";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (options);
    recentFiles.setMaxNumberOfItems (12);
    recentFiles.restoreFromString (settings->getValue ("recentFiles"));

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
    setUpCombo (guitarPanelBox, { "Guitar: Auto", "Guitar: On", "Guitar: Off" }, "guitarPanel",
                "The Guitar panel: Auto shows it for guitars (General MIDI 24-31, or named so); On or Off for any instrument");
    setUpCombo (guitarModeBox, { "Solo", "Chords", "Bass & Pick", "Shape", "Strum Keys", "Pattern" }, "guitarMode",
                "Solo: notes as played. Chords: notes played together are strummed. Bass & Pick: the lowest note first, then the rest picked. "
                "Shape: every key strums the chord drawn on the neck. Strum Keys: hold a chord below C4, play the pick with the white keys from C4 "
                "(C down, D up, E/F muted, G bass, A/B high strings, black keys damp). Pattern: hold a chord and it plays the rhythm chosen at the top");
    {
        juce::StringArray rhythms;
        for (const auto& rhythm : GuitarEngine::rhythms) rhythms.add (rhythm.name);
        setUpCombo (patternBox, rhythms, "strumPattern", "Pattern mode's rhythm, in time with Ableton");
    }
    setUpCombo (strumDirectionBox, { "Down", "Up", "Alternate" }, "strumDirection",
                "Down: low string to high. Up: high to low. Alternate: down, up, down...");
    setUpCombo (tremoloSyncBox, { "Free", "1/2", "1/4", "1/8", "1/16", "1/4T", "1/8T" }, "tremoloSync",
                "Tremolo at Rate (Free) or locked to Ableton's tempo");
    for (auto* label : { &guitarModeLabel, &strumDirectionLabel })
    {
        label->setJustificationType (juce::Justification::centred);
        label->setFont (juce::FontOptions (12.0f));
        label->setColour (juce::Label::textColourId, SF2Look::dim);
        addAndMakeVisible (*label);
    }
    strum.slider.setTooltip ("How long a chord takes from the first string to the last (Off: all at once)");
    fretPosition.slider.setTooltip ("Chords voiced on six strings at this fret (Off: as you play them)");
    palmMute.slider.setTooltip ("Notes under velocity 45 are stopped by the hand: shorter and darker");
    humanize.slider.setTooltip ("Small random changes of velocity, timing and tuning on every note");
    doubleMix.slider.setTooltip ("A second take: the same notes 18 ms later, a little sharp, on the right; the original on the left");
    tremoloDepth.slider.setTooltip ("Stereo tremolo depth (0: off)");
    tremoloRate.slider.setTooltip ("Tremolo speed when Sync is Free");
    addAndMakeVisible (neck);
    keysButton.setTooltip ("Show or hide the keyboard");
    keysButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    keysButton.setColour (juce::TextButton::textColourOffId, SF2Look::dim);
    keysButton.onClick = [this] { toggleSection (keyboardSection); };
    addAndMakeVisible (keysButton);
    neck.setTooltip ("Click a string at a fret to draw a chord (Shape mode plays it on every key); click the name to hear it");
    neck.onShapeChanged = [this] (const std::array<int, 6>& frets)
    {
        player.setGuitarShape (frets);
        // Drawing means playing the drawing: switch to Shape mode.
        if (guitarModeBox.getSelectedItemIndex() != GuitarEngine::shape)
            guitarModeBox.setSelectedItemIndex (GuitarEngine::shape, juce::sendNotificationSync);
    };
    neck.onAudition = [this]
    {
        std::array<int, 6> notes {};
        if (GuitarEngine::shapeNotes (player.guitarShapeFrets(), 0, notes.data()) == 0) return;
        if (guitarModeBox.getSelectedItemIndex() != GuitarEngine::shape)
            guitarModeBox.setSelectedItemIndex (GuitarEngine::shape, juce::sendNotificationSync);
        // The key that puts the shape where it was drawn: its own lowest note.
        int lowest = 127;
        const auto frets = player.guitarShapeFrets();
        for (size_t s = 0; s < 6; ++s) if (frets[s] >= 0) lowest = juce::jmin (lowest, GuitarEngine::strings[s] + frets[s]);
        if (auditionKey >= 0) player.keyboardState.noteOff (1, auditionKey, 0.0f);
        auditionKey = lowest;
        auditionTicks = 30;   // 1.5 s at 20 Hz
        player.keyboardState.noteOn (1, auditionKey, 0.8f);
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
    setResizeLimits (620, minimumHeight(), 1600, 1400);
    if (getHeight() == 0)
        setSize (620, 628 - (collapsed[effectsSection] ? panelHeight - collapsedHeight : 0)
                          - (collapsed[playSection] ? panelHeight - collapsedHeight : 0)
                          - (collapsed[keyboardSection] ? keyboardHeight : 0));
    showSectionContents();
    refreshLayerControls();
    refreshGuitarPanel();
    neck.setShape (player.guitarShapeFrets(), guitarModeBox.getSelectedItemIndex() == GuitarEngine::shape);
}

SF2PlayerEditor::~SF2PlayerEditor()
{
    if (previewNote >= 0)
        player.keyboardState.noteOff (1, previewNote, 0.0f);
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
        // The file box already names the file: this row only shows an author, when there is one.
        const auto& info = st->info;
        infoLabel.setText (info.author.isNotEmpty() ? "by " + info.author : juce::String(), juce::dontSendNotification);
        infoLabel.setTooltip (info.comment);
    }

    // Without anything to say the row goes, and the window gets that much shorter.
    const bool showInfo = infoLabel.getText().isNotEmpty();
    if (showInfo != infoLabel.isVisible() || getHeight() == 0)
    {
        // The row comes or goes: the window grows or shrinks by its height, keeping the user's size otherwise.
        const bool first = getHeight() == 0;
        infoLabel.setVisible (showInfo);
        if (first)
            setSize (620, showInfo ? 628 : 606);
        else
            setSize (getWidth(), juce::jmax (606, getHeight() + (showInfo ? 22 : -22)));
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

// Shows or hides the Guitar panel (Auto follows the instrument), growing the window by its height.
void SF2PlayerEditor::refreshGuitarPanel()
{
    const bool show = player.guitarPanelActive();
    tremoloRate.slider.setEnabled (tremoloSyncBox.getSelectedItemIndex() <= 0);
    if (show == guitarShown)
        return;
    const int before = guitarExtra();
    guitarShown = show;
    showSectionContents();
    setResizeLimits (620, minimumHeight(), 1600, 1400);
    setSize (getWidth(), juce::jmax (minimumHeight(), getHeight() + guitarExtra() - before));
    resized();
    repaint();
}

// The smallest window: 606 with every section open and no Guitar panel.
int SF2PlayerEditor::minimumHeight() const
{
    int height = 606 + guitarExtra();
    for (auto s : { effectsSection, playSection })
        if (collapsed[(size_t) s])
            height -= panelHeight - collapsedHeight;
    if (collapsed[keyboardSection])
        height -= 120;   // the keyboard's smallest height
    return height;
}

void SF2PlayerEditor::toggleSection (Section s)
{
    if (s == keyboardSection && ! collapsed[keyboardSection])
        keyboardHeight = juce::jmax (120, keyboard.getHeight());
    const int delta = s == keyboardSection ? (collapsed[(size_t) s] ? keyboardHeight : -keyboardHeight)
                    : collapsed[(size_t) s] ? panelHeight - collapsedHeight : collapsedHeight - panelHeight;
    collapsed[(size_t) s] = ! collapsed[(size_t) s];
    juce::StringArray folded;
    for (auto [section, name] : { std::pair { effectsSection, "effects" }, std::pair { playSection, "play" },
                                  std::pair { guitarSection, "guitar" }, std::pair { keyboardSection, "keyboard" } })
        if (collapsed[(size_t) section]) folded.add (name);
    settings->setValue ("collapsedSections", folded.joinIntoString (","));
    settings->saveIfNeeded();
    showSectionContents();
    setResizeLimits (620, minimumHeight(), 1600, 1400);
    if (s != guitarSection || guitarShown)
        setSize (getWidth(), juce::jmax (minimumHeight(), getHeight() + delta));
    resized();
    repaint();
}

// What a folded section hides: its knobs and menus. Its title strip keeps the on/off switches,
// the presets and the Guitar menu, so they stay at hand.
void SF2PlayerEditor::showSectionContents()
{
    keyboard.setVisible (! collapsed[keyboardSection]);
    keysButton.setButtonText (juce::String::fromUTF8 (collapsed[keyboardSection] ? "\xe2\x96\xb8 Keyboard" : "\xe2\x96\xbe Keyboard"));
    const bool effects = ! collapsed[effectsSection];
    for (auto* knob : { &reverbLevel, &reverbRoom, &reverbDamping, &reverbWidth, &chorusLevel, &chorusRate, &chorusDepth, &chorusVoices })
        knob->slider.setVisible (effects), knob->label.setVisible (effects);
    const bool play = ! collapsed[playSection];
    for (auto* knob : { &attack, &release, &velocityCurve, &transpose, &fineTune, &bendRange, &glide })
        knob->slider.setVisible (play), knob->label.setVisible (play);
    playMode.setVisible (play);
    playModeLabel.setVisible (play);
    const bool guitar = guitarShown && ! collapsed[guitarSection];
    for (auto* knob : { &strum, &fretPosition, &palmMute, &humanize, &doubleMix, &tremoloDepth, &tremoloRate })
        knob->slider.setVisible (guitar), knob->label.setVisible (guitar);
    for (juce::Component* c : { (juce::Component*) &guitarModeBox, (juce::Component*) &strumDirectionBox,
                                (juce::Component*) &tremoloSyncBox, (juce::Component*) &guitarModeLabel,
                                (juce::Component*) &strumDirectionLabel, (juce::Component*) &neck })
        c->setVisible (guitar);
    patternBox.setVisible (guitar && guitarModeBox.getSelectedItemIndex() == GuitarEngine::pattern);
}

void SF2PlayerEditor::mouseUp (const juce::MouseEvent& e)
{
    const auto at = e.getPosition();
    auto title = [] (juce::Rectangle<int> panel) { return panel.withHeight (collapsedHeight); };
    if (title (reverbPanel).contains (at) || title (chorusPanel).contains (at)) toggleSection (effectsSection);
    else if (title (playPanel).contains (at)) toggleSection (playSection);
    else if (guitarShown && title (guitarPanel).contains (at)) toggleSection (guitarSection);
}

// A section's title with its fold arrow: ▾ open, ▸ folded.
void SF2PlayerEditor::drawSectionTitle (juce::Graphics& g, juce::Rectangle<int> panel, const juce::String& title, Section s)
{
    g.setColour (SF2Look::dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (juce::String::fromUTF8 (collapsed[(size_t) s] ? "\xe2\x96\xb8" : "\xe2\x96\xbe"), panel.getX() + 8, panel.getY() + 7, 12, 14,
                juce::Justification::centred);
    g.drawText (title, panel.getX() + 22, panel.getY() + 8, 100, 14, juce::Justification::centredLeft);
}

void SF2PlayerEditor::timerCallback()
{
    refreshGuitarPanel();
    // The neck: what the Guitar panel's engine played last, and its lights fading.
    auto& view = player.guitarView;
    if (const int sequence = view.sequence.load (std::memory_order_acquire); sequence != shownGuitarSequence)
    {
        shownGuitarSequence = sequence;
        std::array<int, 6> notes {};
        for (size_t i = 0; i < 6; ++i) notes[i] = view.notes[i].load (std::memory_order_relaxed);
        neck.show (notes.data(), view.count.load(), view.pitchClasses.load(), view.bass.load(), view.up.load(),
                   view.strummed.load(), view.palm.load(), juce::roundToInt (player.parameters.getRawParameterValue ("fretPosition")->load()));
    }
    neck.tick (1.0f / 20.0f);
    neck.setShape (player.guitarShapeFrets(), guitarModeBox.getSelectedItemIndex() == GuitarEngine::shape);
    {
        // Pattern mode shows its rhythm menu; Strum Keys tints the chord hand's keys.
        const int mode = guitarModeBox.getSelectedItemIndex();
        const bool showPattern = guitarShown && ! collapsed[guitarSection] && mode == GuitarEngine::pattern;
        if (patternBox.isVisible() != showPattern) patternBox.setVisible (showPattern);
        const int tint = guitarShown && mode == GuitarEngine::strumKeys ? GuitarEngine::strumKeysFrom
                       : layerModeBox.getSelectedItemIndex() == 2 ? juce::roundToInt (splitSlider.getValue()) : -1;
        if (keyboard.splitKey != tint) { keyboard.splitKey = tint; keyboard.repaint(); }
    }
    if (auditionKey >= 0 && --auditionTicks <= 0)
    {
        player.keyboardState.noteOff (1, auditionKey, 0.0f);
        auditionKey = -1;
    }
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

    // MIDI light and output meter, top right.
    const auto dot = juce::Rectangle<float> ((float) meterArea.getX() - 16.0f, (float) meterArea.getCentreY() - 3.5f, 7.0f, 7.0f);
    g.setColour (midiFlash > 0 ? SF2Look::accent : SF2Look::line);
    g.fillEllipse (dot);

    g.setColour (SF2Look::line);
    g.fillRoundedRectangle (meterArea.toFloat(), 2.0f);
    const float db = juce::Decibels::gainToDecibels (shownPeak, -60.0f);
    const float fraction = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    g.setColour (db > -1.0f ? SF2Look::warning : SF2Look::accent);
    g.fillRoundedRectangle (meterArea.toFloat().withWidth ((float) meterArea.getWidth() * fraction), 2.0f);

    g.setColour (SF2Look::panel);
    g.fillRoundedRectangle (playPanel.toFloat(), 6.0f);
    drawSectionTitle (g, playPanel, "PLAY", playSection);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("x1, Linear, 0, Poly and Off leave the SoundFont as it is", playPanel.getX() + 64, playPanel.getY() + 8,
                playPanel.getWidth() - 76 - 120, 14, juce::Justification::centredLeft);

    if (guitarShown)
    {
        g.setColour (SF2Look::panel);
        g.fillRoundedRectangle (guitarPanel.toFloat(), 6.0f);
        drawSectionTitle (g, guitarPanel, "GUITAR", guitarSection);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Off and 0 leave the SoundFont as it is", guitarPanel.getX() + 82, guitarPanel.getY() + 8,
                    guitarPanel.getWidth() - 82 - 100, 14, juce::Justification::centredLeft);
    }

    const bool sends = sendsToggle.getToggleState();
    for (auto [area, title] : { std::pair { reverbPanel, "REVERB" }, std::pair { chorusPanel, "CHORUS" } })
    {
        g.setColour (SF2Look::panel);
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
    meterArea = header.removeFromRight (110).withSizeKeepingCentre (110, 6);

    fileBox.setBounds (area.removeFromTop (28));
    if (infoLabel.isVisible())
    {
        area.removeFromTop (2);
        infoLabel.setBounds (area.removeFromTop (20).withTrimmedLeft (-4));
    }

    area.removeFromTop (8);
    auto row = area.removeFromTop (28);
    previousButton.setBounds (row.removeFromLeft (28));
    nextButton.setBounds (row.removeFromRight (28));
    row.removeFromRight (6);
    favouriteButton.setBounds (row.removeFromRight (28));
    previewButton.setBounds (row.removeFromRight (28));
    row.reduce (6, 0);
    searchBox.setBounds (row.removeFromLeft (130));
    row.removeFromLeft (6);
    presetBox.setBounds (row);

    area.removeFromTop (10);
    row = area.removeFromTop (24);
    auto toneRow = row.removeFromRight ((row.getWidth() - 16) / 2);
    row.removeFromRight (16);
    gainLabel.setBounds (row.removeFromLeft (40));
    gainSlider.setBounds (row);
    toneLabel.setBounds (toneRow.removeFromLeft (40));
    toneSlider.setBounds (toneRow);

    area.removeFromTop (10);
    row = area.removeFromTop (26);
    layerLabel.setBounds (row.removeFromLeft (44));
    layerModeBox.setBounds (row.removeFromLeft (84));
    row.removeFromLeft (6);
    layerLevelSlider.setBounds (row.removeFromRight (140));
    layerLevelLabel.setBounds (row.removeFromRight (40));
    row.removeFromRight (6);
    splitSlider.setBounds (row.removeFromRight (100));
    row.removeFromRight (6);
    layerPresetBox.setBounds (row);

    area.removeFromTop (panelGap);
    auto panels = area.removeFromTop (heightOf (effectsSection));
    reverbPanel = panels.removeFromLeft ((panels.getWidth() - 12) / 2);
    panels.removeFromLeft (12);
    chorusPanel = panels;
    for (auto [panel, toggle, knobs] : { std::tuple { reverbPanel, &reverbToggle, std::array<Knob*, 4> { &reverbLevel, &reverbRoom, &reverbDamping, &reverbWidth } },
                                         std::tuple { chorusPanel, &chorusToggle, std::array<Knob*, 4> { &chorusLevel, &chorusRate, &chorusDepth, &chorusVoices } } })
    {
        toggle->setBounds (panel.getRight() - 32, panel.getY() + 4, 26, 22);
        (toggle == &reverbToggle ? reverbPresets : chorusPresets).setBounds (panel.getRight() - 32 - 92, panel.getY() + 6, 88, 18);
        if (collapsed[effectsSection])
            continue;
        auto inner = panel.reduced (6).withTrimmedTop (18);
        const int width = inner.getWidth() / 4;
        for (auto* knob : knobs)
            knob->setBounds (inner.removeFromLeft (width));
    }

    area.removeFromTop (panelGap);
    playPanel = area.removeFromTop (heightOf (playSection));
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
    guitarPanelBox.setBounds (playPanel.getRight() - 6 - 112, playPanel.getY() + 5, 112, 18);

    if (guitarShown)
    {
        area.removeFromTop (panelGap);
        guitarPanel = area.removeFromTop (heightOf (guitarSection));
        tremoloSyncBox.setBounds (guitarPanel.getRight() - 6 - 70, guitarPanel.getY() + 5, 70, 18);
        patternBox.setBounds (tremoloSyncBox.getX() - 8 - 130, guitarPanel.getY() + 5, 130, 18);
        auto inner = guitarPanel.reduced (6).withTrimmedTop (18);
        neck.setBounds (inner.removeFromBottom (neckHeight - 6).reduced (8, 0));
        const int width = inner.getWidth() / 9;
        // Two rows on the left: the mode, and which way chords are strummed.
        auto combos = inner.removeFromLeft (width * 2).reduced (2, 6);
        for (auto [label, box] : { std::pair { &guitarModeLabel, &guitarModeBox }, std::pair { &strumDirectionLabel, &strumDirectionBox } })
        {
            auto row = combos.removeFromTop (combos.getHeight() / (label == &guitarModeLabel ? 2 : 1)).withSizeKeepingCentre (combos.getWidth(), 24);
            label->setJustificationType (juce::Justification::centredLeft);
            label->setBounds (row.removeFromLeft (40));
            box->setBounds (row);
        }
        for (auto* knob : { &strum, &fretPosition, &palmMute, &humanize, &doubleMix, &tremoloDepth, &tremoloRate })
            knob->setBounds (inner.removeFromLeft (width));
    }
    else
        guitarPanel = {};

    area.removeFromTop (4);
    auto notes = area.removeFromTop (22);
    sendsToggle.setBounds (notes.removeFromRight (210));
    keysButton.setBounds (notes.removeFromLeft (84));
    warningLabel.setBounds (notes.withTrimmedLeft (4));

    area.removeFromTop (6);
    auto keys = area.withTrimmedBottom (14);
    keyboard.setBounds (keys);
    keyboard.setKeyWidth ((float) keys.getWidth() / 52.0f);   // 52 white keys in 88
    keyboard.setLowestVisibleKey (21);
}
