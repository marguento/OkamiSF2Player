// Plays notes through SF2PlayerProcessor offline, the way a host does, and prints the output level.
#include "../Source/PluginProcessor.h"
#include <juce_events/juce_events.h>
#include <cstdio>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File sf2 (argc > 1 ? juce::String (argv[1]) : juce::String());
    const double rate = 48000.0;
    const int block = 512;

    SF2PlayerProcessor p;
    p.setPlayConfigDetails (0, 2, rate, block);
    p.prepareToPlay (rate, block);
    std::printf ("load: %s, presets: %d\n", p.loadSoundFont (sf2) ? "ok" : "FAILED", (int) p.presets().size());

    for (const auto& preset : p.presets())
        std::printf ("  %03d:%03d %-24s keys %d-%d\n", preset.bank, preset.number, preset.name.toRawUTF8(), preset.lowKey, preset.highKey);
    const auto& info = p.soundFontInfo();
    std::printf ("info: '%s' by '%s'\n", info.name.toRawUTF8(), info.author.toRawUTF8());

    juce::AudioBuffer<float> buffer (2, block);
    float peak = 0.0f;
    for (int b = 0; b < 100; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 1)  midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 10);
        if (b == 60) midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        p.processBlock (buffer, midi);
        peak = juce::jmax (peak, buffer.getMagnitude (0, block));
        if (b % 20 == 0 || b == 2) std::printf ("block %3d  peak %.4f\n", b, buffer.getMagnitude (0, block));
    }
    std::printf ("overall peak %.4f -> %s\n", peak, peak > 0.001f ? "SOUND" : "SILENT");
    return peak > 0.001f ? 0 : 1;
}
