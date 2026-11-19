#pragma once

#include "PluginProcessor.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <vector>

// REC and Drag MIDI, in the window's title row: hosts don't record what an instrument plays
// inside itself on its own track (Ableton needs a second track, and an Audio Unit has no MIDI
// out at all), so the plugin keeps a take of what's played in its window (the keyboard, the
// drums' keys, kit and pads) and offers it as a MIDI file to drag onto a track.
namespace Take
{
    using Event = SF2PlayerProcessor::TakeEvent;

    // A take as a Standard MIDI File, 960 ticks a quarter. Played with the host's transport
    // running (and not looping back), it keeps its place in the bars: it starts at the bar the
    // first note fell in (drop it at that bar). Otherwise the first note is at 0, timed at the host's tempo. Notes still
    // held when it stopped end a sixteenth after the last event.
    inline juce::MidiFile build (const std::vector<Event>& events, double bpm, double barQuarters, int& notes)
    {
        constexpr int tpq = 960;
        notes = 0;
        juce::MidiMessageSequence sequence;
        sequence.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60'000'000.0 / juce::jlimit (20.0, 400.0, bpm))), 0.0);
        if (events.empty())
        {
            juce::MidiFile file;
            file.setTicksPerQuarterNote (tpq);
            file.addTrack (sequence);
            return file;
        }
        // On the grid only while the host's position only moved forward: a loop (or a jump back)
        // during the take would put later notes before earlier ones, so it's timed by the clock.
        bool grid = std::all_of (events.begin(), events.end(), [] (const Event& e) { return e.onGrid; });
        for (size_t i = 1; grid && i < events.size(); ++i)
            grid = events[i].ppq >= events[i - 1].ppq - 1.0e-6;
        const auto& first = events.front();
        const double base = ! grid ? 0.0
                          : first.barStart >= 0.0 && first.barStart <= first.ppq   // the host's own bar (a new one may start inside the block)
                              ? first.barStart + std::floor ((first.ppq - first.barStart) / barQuarters) * barQuarters
                          : std::floor (first.ppq / barQuarters) * barQuarters;
        auto ticksOf = [&] (const Event& e)
        {
            const double quarters = grid ? e.ppq - base : (e.seconds - events.front().seconds) * bpm / 60.0;
            return juce::jmax (0.0, quarters * tpq);
        };
        std::map<int, double> open;   // channel * 128 + note -> its note-on's tick
        double last = 0.0;
        for (const auto& e : events)
        {
            if (e.size < 1 || e.size > 3) continue;
            const juce::MidiMessage message (e.bytes, e.size);
            const double tick = ticksOf (e);
            last = juce::jmax (last, tick);
            if (message.isNoteOn()) { open[message.getChannel() * 128 + message.getNoteNumber()] = tick; ++notes; }
            else if (message.isNoteOff()) open.erase (message.getChannel() * 128 + message.getNoteNumber());
            sequence.addEvent (message, tick);
        }
        for (const auto& [key, tick] : open)
            sequence.addEvent (juce::MidiMessage::noteOff (key / 128, key % 128), last + tpq / 4);
        sequence.sort();
        sequence.updateMatchedPairs();
        juce::MidiFile file;
        file.setTicksPerQuarterNote (tpq);
        file.addTrack (sequence);
        return file;
    }

    // The chip that carries the take: dragged out, it's a .mid file the host reads as a clip.
    struct DragChip : juce::Component, juce::TooltipClient
    {
        juce::File file;
        juce::String text;
        bool recording = false;
        void paint (juce::Graphics& g) override
        {
            if (text.isEmpty()) return;
            const auto r = getLocalBounds().toFloat().reduced (0.5f);
            const juce::Colour accent (0xffe0a64b);
            g.setColour (recording ? juce::Colour (0x33e05050) : accent.withAlpha (file.existsAsFile() ? 0.18f : 0.08f));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (recording ? juce::Colour (0xffe05050) : accent.withAlpha (0.8f));
            g.drawRoundedRectangle (r, 5.0f, 1.0f);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawFittedText (text, getLocalBounds().reduced (6, 0), juce::Justification::centred, 1, 0.75f);
        }
        juce::String getTooltip() override
        {
            return ! recording && file.existsAsFile() ? "Drag this onto a MIDI track to make a clip of the take (drop it at the bar it started on)"
                                       : juce::String();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! dragging && ! recording && file.existsAsFile() && e.getDistanceFromDragStart() > 4)
            {
                dragging = true;
                juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                             [this] { dragging = false; });
            }
        }
        void mouseUp (const juce::MouseEvent&) override { dragging = false; }
        bool dragging = false;
    };

    // REC (a toggle) and the chip, driven by a timer that drains the processor's FIFO.
    struct Recorder : juce::Component, private juce::Timer
    {
        explicit Recorder (SF2PlayerProcessor& p) : player (p)
        {
            rec.setClickingTogglesState (true);
            rec.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffb83232));
            rec.setTooltip ("Record what you play in this window (keyboard, drums), then drag it onto a track as a MIDI clip");
            rec.onClick = [this] { rec.getToggleState() ? start() : stop(); };
            addAndMakeVisible (rec);
            addAndMakeVisible (chip);
        }
        ~Recorder() override { player.takeRecording.store (false); }

        void resized() override
        {
            auto r = getLocalBounds();
            rec.setBounds (r.removeFromLeft (58).withSizeKeepingCentre (58, 22));
            r.removeFromLeft (6);
            chip.setBounds (r.withSizeKeepingCentre (r.getWidth(), 22));
        }

        void start()
        {
            drain();   // anything left from before is dropped
            events.clear();
            chip.file = juce::File();   // the last take's file isn't offered while a new one records
            player.takeRecording.store (true);
            chip.recording = true;
            started = juce::Time::getMillisecondCounterHiRes();
            update();
            startTimerHz (10);
        }
        void stop()
        {
            player.takeRecording.store (false);
            drain();
            stopTimer();
            chip.recording = false;
            int notes = 0;
            const auto midi = build (events, player.takeBpm.load(), player.takeBarQuarters.load(), notes);
            chip.file = juce::File();
            if (notes > 0)
            {
                auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Okami SF2 Player takes");
                folder.createDirectory();
                for (const auto& old : folder.findChildFiles (juce::File::findFiles, false, "*.mid"))   // a day's worth kept
                    if (old.getLastModificationTime() < juce::Time::getCurrentTime() - juce::RelativeTime::days (1)) old.deleteFile();
                auto file = folder.getChildFile ("Okami Take " + juce::Time::getCurrentTime().formatted ("%H-%M-%S") + ".mid")
                                .getNonexistentSibling();   // two takes in one second never share a file
                if (juce::FileOutputStream out (file); out.openedOk())
                {
                    out.setPosition (0);
                    out.truncate();
                    if (midi.writeTo (out)) chip.file = file;
                }
            }
            chip.text = chip.file.existsAsFile() ? juce::String::fromUTF8 ("\xe2\x87\xa9 Drag MIDI \xc2\xb7 ") + juce::String (notes) + (notes == 1 ? " note" : " notes")
                                                 : juce::String ("Nothing played");
            chip.repaint();
        }

        const juce::File& takeFile() const { return chip.file; }   // what dragging the chip hands over

    private:
        SF2PlayerProcessor& player;
        juce::TextButton rec { juce::String::fromUTF8 ("\xe2\x97\x8f REC") };
        DragChip chip;
        std::vector<Event> events;
        double started = 0;

        void drain()
        {
            auto& fifo = player.takeFifo;
            fifo.read (fifo.getNumReady()).forEach ([&] (int index) { events.push_back (player.takeEvents[(size_t) index]); });
        }
        void update()
        {
            int notes = 0;
            for (const auto& e : events) notes += (e.size == 3 && (e.bytes[0] & 0xf0) == 0x90 && e.bytes[2] > 0) ? 1 : 0;
            const int seconds = (int) ((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0);
            chip.text = juce::String::fromUTF8 ("\xe2\x97\x8f ") + juce::String (seconds / 60) + ":" + juce::String (seconds % 60).paddedLeft ('0', 2)
                      + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (notes) + (notes == 1 ? " note" : " notes");
            chip.repaint();
        }
        void timerCallback() override { drain(); update(); }
    };
}
