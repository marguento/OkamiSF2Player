// Plays one note, releases it, and prints the output level each half second
// afterwards, with the reverb and the chorus on and off: where a tail of noise comes from.
#include "../Source/PluginProcessor.h"
#include <juce_events/juce_events.h>
#include <cstdio>
#include <cstdlib>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File sf2 (argv[1]);
    const int preset = argc > 2 ? juce::String (argv[2]).getIntValue() : 0;
    const double rate = 48000.0;
    const int block = 480;

    // "scan <dir>": every preset, reverb and chorus off; writes the half second after the
    // release as raw mono float32 to <dir>/<index>.raw and prints its level.
    if (argc > 3 && juce::String (argv[2]) == "scan")
    {
        SF2PlayerProcessor p;
        p.setPlayConfigDetails (0, 2, rate, block);
        p.prepareToPlay (rate, block);
        p.loadSoundFont (sf2);
        p.parameters.getParameter ("fsReverbOn")->setValueNotifyingHost (0.0f);
        p.parameters.getParameter ("fsChorusOn")->setValueNotifyingHost (0.0f);
        const auto presets = p.presets();
        juce::AudioBuffer<float> buffer (2, block);
        for (int i = 0; i < (int) presets.size(); ++i)
        {
            p.selectPreset (i);
            const int key = juce::jlimit (presets[(size_t) i].lowKey, presets[(size_t) i].highKey, 60);
            juce::FileOutputStream out (juce::File (juce::String (argv[3])).getChildFile (juce::String (i) + ".raw"));
            out.setPosition (0); out.truncate();
            double sustain = 0;
            for (int b = 0; b < 300; ++b)   // 3 s: on at 0, off at 1 s
            {
                juce::MidiBuffer midi;
                if (b == 0)   midi.addEvent (juce::MidiMessage::noteOn (1, key, (juce::uint8) 100), 0);
                if (b == 100) midi.addEvent (juce::MidiMessage::noteOff (1, key), 0);
                p.processBlock (buffer, midi);
                if (b >= 50 && b < 100) sustain = juce::jmax (sustain, (double) buffer.getRMSLevel (0, 0, block));
                if (b >= 130 && b < 180)   // 0.3 - 0.8 s after the release
                    for (int s = 0; s < block; ++s)
                    {
                        const float m = 0.5f * (buffer.getSample (0, s) + buffer.getSample (1, s));
                        out.write (&m, sizeof (float));
                    }
            }
            std::printf ("%d\t%d:%d\t%s\t%.1f\n", i, presets[(size_t) i].bank, presets[(size_t) i].number,
                         presets[(size_t) i].name.toRawUTF8(), sustain > 0 ? 20 * std::log10 (sustain) : -200.0);
            juce::MidiBuffer off; off.addEvent (juce::MidiMessage::allSoundOff (1), 0);
            p.processBlock (buffer, off);
        }
        return 0;
    }

    // "<preset> perf": CPU time per block, with 8, 32 and 64 notes held, at 48 kHz in 128-sample blocks.
    if (argc > 3 && juce::String (argv[3]) == "perf")
    {
        for (const bool sends : { false })
            for (const int notes : { 8, 32, 64 })
            {
                SF2PlayerProcessor p;
                auto layout = p.getBusesLayout();
                p.setBusesLayout (layout);
                const int n = 128;
                p.setRateAndBufferSizeDetails (rate, n);
                p.prepareToPlay (rate, n);
                p.loadSoundFont (sf2);
                p.selectPreset (preset);
                p.parameters.getParameter ("sendOutputs")->setValueNotifyingHost (sends ? 1.0f : 0.0f);
                auto* t = p.parameters.getParameter ("tone");
                t->setValueNotifyingHost (t->convertTo0to1 (6000.0f));
                juce::AudioBuffer<float> buffer (6, n);
                const int blocks = (int) (rate * 10 / n);   // 10 s of audio
                double worst = 0, total = 0;
                int maxVoices = 0;
                if (const char* interp = std::getenv ("INTERP")) p.interpolationOverride = std::atoi (interp);
                for (int b = 0; b < blocks; ++b)
                {
                    juce::MidiBuffer midi;
                    if (b % 375 == 0)   // every second, the chord again (keeps the voices sounding)
                        for (int k = 0; k < notes; ++k)
                            midi.addEvent (juce::MidiMessage::noteOn (1 + k % 16, 36 + (k * 7) % 60, (juce::uint8) 100), 0);
                    const auto start = juce::Time::getHighResolutionTicks();
                    p.processBlock (buffer, midi);
                    const double ms = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start) * 1000.0;
                    if (b > 10) { worst = juce::jmax (worst, ms); total += ms; }
                    maxVoices = juce::jmax (maxVoices, p.activeVoices.load());
                }
                const double blockMs = n / rate * 1000.0;
                std::printf ("%-13s %2d notes, %3d voices: average %.3f ms (%4.1f %% of a block), worst %.3f ms (%4.1f %%)\n",
                             sends ? "send outputs" : "built-in fx", notes, maxVoices, total / (blocks - 11),
                             100 * total / (blocks - 11) / blockMs, worst, 100 * worst / blockMs);
            }
        return 0;
    }

    // "<preset> snap <out.png> [sends]": the editor with that instrument, as a picture.
    if (argc > 4 && juce::String (argv[3]) == "snap")
    {
        SF2PlayerProcessor p;
        p.loadSoundFont (sf2);
        p.selectPreset (preset);
        if (argc > 5)
            p.parameters.getParameter ("sendOutputs")->setValueNotifyingHost (1.0f);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        editor->setVisible (true);
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        juce::FileOutputStream out { juce::File { juce::String (argv[4]) } };
        out.setPosition (0); out.truncate();
        juce::PNGImageFormat().writeImageToStream (image, out);
        return 0;
    }

    // "sends": each preset's reverb and chorus sends as the plugin reads them.
    if (argc > 2 && juce::String (argv[2]) == "sends")
    {
        SF2PlayerProcessor p;
        p.loadSoundFont (sf2);
        for (const auto& preset : p.presets())
            std::printf ("%d\t%d\t%.1f\t%.1f\t%.1f\t%.1f\n", preset.bank, preset.number, preset.sends.reverbMin,
                         preset.sends.reverbMax, preset.sends.chorusMin, preset.sends.chorusMax);
        return 0;
    }

    // "check": the send outputs, the tone filter and the saved state.
    if (argc > 3 && juce::String (argv[3]) == "check")
    {
        auto play = [&] (SF2PlayerProcessor& p, int channels, double& mainLevel, double& reverbLevel, double& chorusLevel, double& high)
        {
            juce::AudioBuffer<float> buffer (channels, block);
            mainLevel = reverbLevel = chorusLevel = high = 0;
            float previous = 0;
            for (int b = 0; b < 100; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0);
                if (b == 60) midi.addEvent (juce::MidiMessage::noteOff (1, 67), 0);
                p.processBlock (buffer, midi);
                mainLevel = juce::jmax (mainLevel, (double) buffer.getRMSLevel (0, 0, block));
                if (channels >= 4) reverbLevel = juce::jmax (reverbLevel, (double) buffer.getRMSLevel (2, 0, block));
                if (channels >= 6) chorusLevel = juce::jmax (chorusLevel, (double) buffer.getRMSLevel (4, 0, block));
                for (int i = 0; i < block; ++i)   // first difference: a crude measure of the highs
                {
                    const float x = buffer.getSample (0, i);
                    high += (x - previous) * (x - previous);
                    previous = x;
                }
            }
        };
        auto db = [] (double v) { return v > 0 ? 20 * std::log10 (v) : -200.0; };
        auto make = [&] (int channels, bool sends, float tone)
        {
            auto p = std::make_unique<SF2PlayerProcessor>();
            auto layout = p->getBusesLayout();
            for (int bus = 1; bus < layout.outputBuses.size(); ++bus)
                layout.outputBuses.getReference (bus) = channels > 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
            std::printf ("layout %d channels: %s\n", channels, p->setBusesLayout (layout) ? "ok" : "REFUSED");
            p->setRateAndBufferSizeDetails (rate, block);
            p->prepareToPlay (rate, block);
            p->loadSoundFont (sf2);
            p->selectPreset (preset);
            p->parameters.getParameter ("sendOutputs")->setValueNotifyingHost (sends ? 1.0f : 0.0f);
            auto* t = p->parameters.getParameter ("tone");
            t->setValueNotifyingHost (t->convertTo0to1 (tone));
            return p;
        };
        double m, r, c, h;
        { auto p = make (2, false, 20000); play (*p, 2, m, r, c, h); std::printf ("built-in, 2 ch:   main %.1f dB  highs %.1f dB\n", db (m), 10 * std::log10 (h)); }
        { auto p = make (6, false, 20000); play (*p, 6, m, r, c, h); std::printf ("built-in, 6 ch:   main %.1f  reverb %.1f  chorus %.1f dB\n", db (m), db (r), db (c)); }
        { auto p = make (6, true, 20000);  play (*p, 6, m, r, c, h); std::printf ("send outputs:     main %.1f  reverb %.1f  chorus %.1f dB\n", db (m), db (r), db (c)); }
        { auto p = make (2, false, 6000);  play (*p, 2, m, r, c, h); std::printf ("tone 6 kHz:       main %.1f dB  highs %.1f dB\n", db (m), 10 * std::log10 (h)); }

        // State: saved, then restored in a fresh instance.
        juce::MemoryBlock state;
        {
            auto p = make (2, true, 8000);
            p->selectPreset (40);
            p->parameters.getParameter ("gain")->setValueNotifyingHost (0.3f);
            p->getStateInformation (state);
        }
        {
            SF2PlayerProcessor q;
            q.setStateInformation (state.getData(), (int) state.getSize());
            q.prepareToPlay (rate, block);
            const auto presets = q.presets();
            std::printf ("restored: file %s, preset %d (%s), sends %.0f, tone %.0f Hz, gain %.2f\n",
                         q.soundFontFile().getFileName().toRawUTF8(), q.selectedPreset(),
                         q.selectedPreset() < (int) presets.size() ? presets[(size_t) q.selectedPreset()].name.toRawUTF8() : "-",
                         q.parameters.getRawParameterValue ("sendOutputs")->load(), q.parameters.getRawParameterValue ("tone")->load(),
                         q.parameters.getParameter ("gain")->getValue());
        }
        {
            auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
            xml->setAttribute ("sf2Path", "/nowhere/Gone.sf2");
            juce::MemoryBlock moved;
            juce::AudioProcessor::copyXmlToBinary (*xml, moved);
            SF2PlayerProcessor q;
            q.setStateInformation (moved.getData(), (int) moved.getSize());
            juce::MemoryBlock again;
            q.getStateInformation (again);
            auto back = juce::AudioProcessor::getXmlFromBinary (again.getData(), (int) again.getSize());
            std::printf ("missing file: shown as %s, saved again as %s, bank %s program %s\n",
                         q.missingSoundFont().getFileName().toRawUTF8(), back->getStringAttribute ("sf2Path").toRawUTF8(),
                         back->getStringAttribute ("presetBank").toRawUTF8(), back->getStringAttribute ("presetNumber").toRawUTF8());
        }
        return 0;
    }

    // "<preset> wav <out.wav> <key>": effects off, note held 1 s, 3 s total, written as a WAV.
    if (argc > 5 && juce::String (argv[3]) == "wav")
    {
        SF2PlayerProcessor p;
        p.setPlayConfigDetails (0, 2, rate, block);
        p.prepareToPlay (rate, block);
        p.loadSoundFont (sf2);
        p.selectPreset (preset);
        p.parameters.getParameter ("fsReverbOn")->setValueNotifyingHost (0.0f);
        p.parameters.getParameter ("fsChorusOn")->setValueNotifyingHost (0.0f);
        const int key = juce::String (argv[5]).getIntValue();
        if (const char* interp = std::getenv ("INTERP")) p.interpolationOverride = std::atoi (interp);
        if (argc > 6)
        {
            auto* t = p.parameters.getParameter ("tone");
            t->setValueNotifyingHost (t->convertTo0to1 (juce::String (argv[6]).getFloatValue()));
        }
        juce::AudioBuffer<float> all (2, block * 300), buffer (2, block);
        for (int b = 0; b < 300; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 2)   midi.addEvent (juce::MidiMessage::noteOn (1, key, (juce::uint8) 100), 0);
            if (b == 102) midi.addEvent (juce::MidiMessage::noteOff (1, key), 0);
            p.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch) all.copyFrom (ch, b * block, buffer, ch, 0, block);
        }
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out (juce::File (juce::String (argv[4])).createOutputStream());
        if (auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (2).withBitsPerSample (32)))
            writer->writeFromAudioSampleBuffer (all, 0, all.getNumSamples());
        return 0;
    }

    for (auto [reverb, chorus] : { std::pair { true, true }, std::pair { false, true }, std::pair { true, false }, std::pair { false, false } })
    {
        SF2PlayerProcessor p;
        p.setPlayConfigDetails (0, 2, rate, block);
        p.prepareToPlay (rate, block);
        p.loadSoundFont (sf2);
        p.selectPreset (preset);
        p.parameters.getParameter ("fsReverbOn")->setValueNotifyingHost (reverb ? 1.0f : 0.0f);
        p.parameters.getParameter ("fsChorusOn")->setValueNotifyingHost (chorus ? 1.0f : 0.0f);

        juce::AudioBuffer<float> buffer (2, block);
        std::printf ("reverb %-3s chorus %-3s |", reverb ? "on" : "off", chorus ? "on" : "off");
        double sum = 0; int count = 0;
        for (int b = 0; b < 100 * 12; ++b)   // 12 s in 10 ms blocks
        {
            juce::MidiBuffer midi;
            if (b == 1)  midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            if (b == 50) midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            p.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch) { sum += std::pow (buffer.getRMSLevel (ch, 0, block), 2.0); ++count; }
            if ((b + 1) % 50 == 0)   // every 0.5 s: RMS in dB
            {
                const double rms = std::sqrt (sum / count);
                std::printf (" %5.0f", rms > 0 ? 20 * std::log10 (rms) : -200.0);
                sum = 0; count = 0;
            }
        }
        std::printf ("\n");
    }
    std::printf ("(dB RMS per half second; note on at 0, off at 0.5 s)\n");
}
