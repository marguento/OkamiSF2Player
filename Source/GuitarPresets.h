#pragma once

#include <juce_core/juce_core.h>
#include "GuitarEngine.h"

#include <map>
#include <vector>

// The Guitar panel's presets: every control of the panel at once (a snapshot), the built-in ones
// and the user's own, kept in the app's settings so every project has them.
namespace GuitarPresets
{
// Every parameter a preset holds: the whole Guitar panel, CONTROLLERS included (Release is PLAY's).
inline const juce::StringArray& ids()
{
    static const juce::StringArray list {
        "guitarDirect", "guitarMode", "guitarPattern", "guitarHold", "strumPattern", "chordPosition", "harmonyInterval",
        "velocityFx", "fxVelocity", "fxTime", "fxSteps", "addOnKeys", "stringsTop", "stringsBottom", "repeatStrings",
        "chordBass", "alterBass", "bassMono", "soloFx", "keySwitch1", "keySwitch2", "keySwitch3", "keySwitch4", "pedalFx",
        "legatoSteps", "legatoTime", "bassZone", "bassZoneTop", "strum", "guitarVeloCurve", "accent", "humanize",
        "release", "guitarTuning", "doubleMix", "tremoloDepth", "tremoloRate", "tremoloSync",
        "pbAssign", "mwAssign", "atAssign", "pbRange", "mwRange", "atRange" };
    return list;
}

// A preset: its name and the values it sets. A parameter it doesn't name goes to its default.
struct Preset
{
    juce::String name;
    std::map<juce::String, float> values;
    bool user = false;
};

// What a preset that doesn't name them sets: Direct, Pattern and Hold off (their defaults are on,
// for a first try, but a preset is a sound of its own).
inline const std::map<juce::String, float>& base()
{
    static const std::map<juce::String, float> values { { "guitarDirect", 0 }, { "guitarPattern", 0 }, { "guitarHold", 0 } };
    return values;
}

// The built-in presets, each a different way to play: only what differs from base() and the defaults.
inline const std::vector<Preset>& builtIns()
{
    using E = GuitarEngine;
    static const std::vector<Preset> list {
        { "Off (Direct)", { { "guitarDirect", 1 } } },
        { "Acoustic Strum", { { "guitarDirect", 0 }, { "guitarMode", E::chords }, { "strum", 35 }, { "humanize", 0.35f } } },
        { "Campfire", { { "guitarDirect", 0 }, { "guitarMode", E::chords }, { "strum", 45 }, { "humanize", 0.5f },
                        { "velocityFx", E::slowStrumLow }, { "fxVelocity", 50 }, { "guitarVeloCurve", 1 }, { "release", 1.4f } } },
        { "Double-Tracked", { { "guitarDirect", 0 }, { "guitarMode", E::chords }, { "strum", 30 }, { "humanize", 0.4f },
                              { "doubleMix", 0.8f } } },
        { "Slow Ballad", { { "guitarDirect", 0 }, { "guitarMode", E::chords }, { "strum", 120 }, { "chordPosition", 1 },
                           { "humanize", 0.3f }, { "doubleMix", 0.5f }, { "tremoloDepth", 0.15f }, { "release", 2.0f },
                           { "guitarHold", 1 }, { "guitarVeloCurve", 1 } } },
        { "High Strings", { { "guitarDirect", 0 }, { "guitarMode", E::chords }, { "strum", 20 }, { "chordPosition", 2 },
                            { "stringsTop", 1 }, { "stringsBottom", 4 }, { "humanize", 0.3f } } },
        { "Folk Pattern", { { "guitarDirect", 0 }, { "guitarMode", E::chords }, { "guitarPattern", 1 }, { "strumPattern", 2 },
                            { "strum", 30 }, { "humanize", 0.4f } } },
        { "Muted Rock", { { "guitarDirect", 0 }, { "guitarMode", E::bassAndChord }, { "guitarPattern", 1 }, { "strumPattern", 6 },
                          { "strum", 15 }, { "humanize", 0.25f }, { "accent", 80 }, { "release", 0.6f } } },
        { "Fingerpicking", { { "guitarDirect", 0 }, { "guitarMode", E::chords }, { "guitarPattern", 1 }, { "strumPattern", 10 },
                             { "humanize", 0.45f }, { "release", 1.6f }, { "guitarVeloCurve", 2 } } },
        { "Bass & Pick", { { "guitarDirect", 0 }, { "guitarMode", E::bassAndPick }, { "humanize", 0.35f } } },
        { "Lead Solo", { { "guitarDirect", 0 }, { "guitarMode", E::solo }, { "soloFx", E::artHammer }, { "pbAssign", 1 },
                         { "humanize", 0.3f }, { "guitarVeloCurve", 1 } } },
        { "Octave Lead", { { "guitarDirect", 0 }, { "guitarMode", E::harmony }, { "harmonyInterval", 2 }, { "humanize", 0.3f },
                           { "doubleMix", 0.4f } } },
    };
    return list;
}

// The user's presets as XML (the settings' "guitarUserPresets"), and back.
inline juce::String toXml (const std::vector<Preset>& presets)
{
    juce::XmlElement root ("GuitarPresets");
    for (const auto& preset : presets)
    {
        auto* element = root.createNewChildElement ("Preset");
        element->setAttribute ("name", preset.name);
        for (const auto& [id, value] : preset.values)
        {
            auto* v = element->createNewChildElement ("Value");
            v->setAttribute ("id", id);
            v->setAttribute ("value", (double) value);
        }
    }
    return root.toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
}

inline std::vector<Preset> fromXml (const juce::String& text)
{
    std::vector<Preset> presets;
    const auto root = juce::parseXML (text);
    if (root == nullptr || ! root->hasTagName ("GuitarPresets"))
        return presets;
    for (auto* element : root->getChildWithTagNameIterator ("Preset"))
    {
        Preset preset { element->getStringAttribute ("name").substring (0, 60).trim(), {}, true };
        if (preset.name.isEmpty()) continue;
        for (auto* v : element->getChildWithTagNameIterator ("Value"))
        {
            const auto id = v->getStringAttribute ("id");
            const auto value = (float) v->getDoubleAttribute ("value");
            if (ids().contains (id) && std::isfinite (value)) preset.values[id] = value;   // unknown or broken values are skipped
        }
        presets.push_back (std::move (preset));
        if (presets.size() >= 100) break;
    }
    return presets;
}
} // namespace GuitarPresets
