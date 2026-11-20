#pragma once

#include "PluginProcessor.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>
#include <thread>
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
    inline juce::MidiFile build (const std::vector<Event>& allEvents, double bpm, double barQuarters, int& notes)
    {
        // The take starts at its first note: key-ups before it (a key held as REC was clicked) are left out.
        const auto firstNote = std::find_if (allEvents.begin(), allEvents.end(),
                                             [] (const Event& e) { return SF2PlayerProcessor::isNoteOnBytes (e.bytes, e.size); });
        const std::vector<Event> events (firstNote, allEvents.end());
        if (! events.empty())   // the tempo and bar in force at the first note, not at the take's end
        {
            bpm = events.front().bpm;
            barQuarters = events.front().barQuarters;
        }
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
        // On the host's bars, a take starts at the bar the first note fell in: the host's own bar
        // (a new one may start inside the block), else counted from 0 in the current signature.
        // Its notes keep the host's positions while those only moved forward; a loop (or a jump
        // back) during the take would put later notes before earlier ones, so they're timed by
        // the clock from the first note instead, which still sits where it fell in its bar.
        const bool onBars = std::all_of (events.begin(), events.end(), [] (const Event& e) { return e.onGrid; });
        bool forward = onBars;
        for (size_t i = 1; forward && i < events.size(); ++i)
            forward = events[i].ppq >= events[i - 1].ppq - 1.0e-6;
        const auto& first = events.front();
        const double bar = ! onBars ? 0.0
                         : first.barStart >= 0.0 && first.barStart <= first.ppq
                             ? first.barStart + std::floor ((first.ppq - first.barStart) / barQuarters) * barQuarters
                             : std::floor (first.ppq / barQuarters) * barQuarters;
        const double firstInBar = onBars ? first.ppq - bar : 0.0;
        auto ticksOf = [&] (const Event& e)
        {
            const double quarters = forward ? e.ppq - bar : firstInBar + (e.seconds - first.seconds) * bpm / 60.0;
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
                                                                             [safe = juce::Component::SafePointer<DragChip> (this)]
                                                                             { if (safe != nullptr) safe->dragging = false; });   // the window may close mid-drag
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
            rec.setTooltip ("Record what you play in this window (keyboard, drums) along with the song: REC waits for the host's play "
                            "and stops with it. Then drag the take onto a track as a MIDI clip");
            rec.onClick = [this] { rec.getToggleState() ? start() : stop(); };
            addAndMakeVisible (rec);
            addAndMakeVisible (chip);
            if (player.lastTakeFile.existsAsFile())
            {
                chip.file = player.lastTakeFile;
                chip.text = player.lastTakeText;
            }
        }
        // Closing the window mid-take ends it (written like REC would), and the last take is kept
        // with the plugin, so the chip is still there to drag when the window opens again.
        ~Recorder() override
        {
            if (player.takeState.load() != SF2PlayerProcessor::takeIdle)
                stop();
            player.lastTakeFile = chip.file;
            player.lastTakeText = chip.text;
        }

        void resized() override
        {
            auto r = getLocalBounds();
            rec.setBounds (r.removeFromLeft (58).withSizeKeepingCentre (58, 22));
            r.removeFromLeft (6);
            chip.setBounds (r.withSizeKeepingCentre (r.getWidth(), 22));
        }

        // REC in a host with a transport waits for its play (at once if it's playing: the audio
        // thread decides, from the next block), so the take is played along with the song and
        // lands on its bars; stopping the song ends it. Played before any play, it's a take of its
        // own at once, ended with REC. A host without a transport records at once.
        std::function<void()> onSongStarted;   // the take started with the host's play: the window takes the keys back

        void start()
        {
            drain();   // anything left from before is dropped
            events.clear();
            notesSoFar = 0;
            lastText = chip.text;   // the last take stays on disk; offered again if this one is called off before it starts
            chip.recording = true;   // no drag while recording
            player.takeState.store (SF2PlayerProcessor::takeArmed);   // the audio thread starts it at once in a host without a transport
            seenState = player.takeState.load();
            started = juce::Time::getMillisecondCounterHiRes();
            update();
            startTimerHz (20);
        }
        void stop()
        {
            const int was = player.takeState.exchange (SF2PlayerProcessor::takeIdle);
            // A block that read "recording" just before is still writing: its notes are the take's.
            for (int spins = 0; player.takeWriters.load() > 0 && spins < 100000; ++spins)
                std::this_thread::yield();
            rec.setToggleState (false, juce::dontSendNotification);
            drain();
            stopTimer();
            chip.recording = false;
            if (was == SF2PlayerProcessor::takeArmed)   // called off before it started: the last take is offered again
            {
                chip.text = chip.file.existsAsFile() ? lastText : juce::String();
                chip.repaint();
                return;
            }
            int notes = 0;
            const auto midi = build (events, player.takeBpm.load(), player.takeBarQuarters.load(), notes);
            if (notes > 0)   // a take with nothing in it leaves the last one to drag
            {
                const auto previous = chip.file;   // the take this window offered until now: kept (and offered still if this one can't be saved)
                auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Okami SF2 Player takes");
                folder.createDirectory();
                // A week's worth kept (another window may still offer one of them), never this window's last take.
                for (const auto& old : folder.findChildFiles (juce::File::findFiles, false, "Okami Take *.mid"))
                    if (old != previous && old != player.lastTakeFile
                        && old.getLastModificationTime() < juce::Time::getCurrentTime() - juce::RelativeTime::days (7))
                        old.deleteFile();
                auto file = folder.getChildFile ("Okami Take " + juce::Time::getCurrentTime().formatted ("%H-%M-%S") + ".mid")
                                .getNonexistentSibling();   // two takes in one second never share a file
                bool saved = false;
                if (juce::FileOutputStream out (file); out.openedOk())
                {
                    out.setPosition (0);
                    out.truncate();
                    saved = midi.writeTo (out);
                    out.flush();   // a full disk shows here, not in writeTo (it only fills the stream's buffer)
                    saved = saved && out.getStatus().wasOk();
                }
                if (saved) chip.file = file;
                else file.deleteFile();   // nothing half-written left behind
                chip.text = chip.file != previous && chip.file.existsAsFile()
                              ? juce::String::fromUTF8 ("\xe2\x87\xa9 Drag MIDI \xc2\xb7 ") + juce::String (notes) + (notes == 1 ? " note" : " notes")
                              : juce::String ("Couldn't save the take") + (previous.existsAsFile() ? " (the last one stays)" : "");
            }
            else
                chip.text = chip.file.existsAsFile() ? lastText : juce::String ("Nothing played");
            chip.repaint();
        }

        juce::File takeFile() const { return chip.recording ? juce::File() : chip.file; }   // the take on offer (none while recording); for tests

    private:
        SF2PlayerProcessor& player;
        juce::TextButton rec { juce::String::fromUTF8 ("\xe2\x97\x8f REC") };
        DragChip chip;
        std::vector<Event> events;
        double started = 0;
        juce::String lastText;
        int seenState = SF2PlayerProcessor::takeIdle;
        int notesSoFar = 0;

        void drain()
        {
            auto& fifo = player.takeFifo;
            fifo.read (fifo.getNumReady()).forEach ([&] (int index)
            {
                const auto& e = player.takeEvents[(size_t) index];
                events.push_back (e);
                notesSoFar += SF2PlayerProcessor::isNoteOnBytes (e.bytes, e.size) ? 1 : 0;
            });
        }
        void update()
        {
            const int take = player.takeState.load();
            if (take == SF2PlayerProcessor::takeArmed)
            {
                chip.text = juce::String::fromUTF8 ("\xe2\x97\x8f Waiting for play");
                chip.repaint();
                return;
            }
            const int notes = notesSoFar;   // counted as they come in, not again every tick
            const int seconds = (int) ((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0);
            chip.text = juce::String::fromUTF8 ("\xe2\x97\x8f ") + juce::String (seconds / 60) + ":" + juce::String (seconds % 60).paddedLeft ('0', 2)
                      + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (notes) + (notes == 1 ? " note" : " notes");
            chip.repaint();
        }
        void timerCallback() override
        {
            drain();
            const int state = player.takeState.load();
            if (state == SF2PlayerProcessor::takeEndedByHost) { stop(); return; }   // the song stopped: the take is ready
            if (seenState == SF2PlayerProcessor::takeArmed && state != SF2PlayerProcessor::takeArmed)
            {
                started = juce::Time::getMillisecondCounterHiRes();   // the clock starts with the take
                if (state == SF2PlayerProcessor::takeWithHost && onSongStarted) onSongStarted();
            }
            seenState = state;
            update();
        }
    };
}
