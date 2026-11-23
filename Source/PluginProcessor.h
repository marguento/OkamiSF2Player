#pragma once
#include <bitset>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "SoundFontInfo.h"
#include "GuitarEngine.h"
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

// A SoundFont (.sf2) instrument played by FluidSynth, the reference SF2
// player: its modulators, interpolation, reverb and chorus. The file is chosen
// in the editor and remembered in the host's project.
class SF2PlayerProcessor : public juce::AudioProcessor,
                           public juce::ChangeBroadcaster,
                           private juce::Timer
{
public:
    SF2PlayerProcessor();
    ~SF2PlayerProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}   // deferred note-offs still belong to sounding voices
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // Bypassed: silence and no MIDI out (JUCE's default would pass the host's notes out as ours),
    // but the block still runs, so a key let go meanwhile still ends its note and nothing waits.
    void processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        bypassedBlock = true;
        processBlock (buffer, midi);
        bypassedBlock = false;
        buffer.clear();
        if (wrapperType == wrapperType_Standalone) { midi.clear(); return; }   // no room reserved there: sends nothing (as SendPlayed)
        // Only the window's key-ups go out: a note it sent before the bypass still ends on the track
        // recording it. Within the room MIDI out has anyway (see SendPlayed): no allocation.
        const int room = juce::jmax (midi.data.size(), 2048);
        midi.clear();
        for (const auto metadata : playedHere)
            if (isNoteOffBytes (metadata.data, metadata.numBytes) && midi.data.size() + 3 + (int) sizeof (int32_t) + (int) sizeof (uint16_t) <= room)
                midi.addEvent (metadata.data, metadata.numBytes, metadata.samplePosition);
    }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Okami SF2 Player"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }   // what's played in the window, for the host to record
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    struct Preset
    {
        juce::String name;
        int bank = 0, number = 0, lowKey = 0, highKey = 127;
        std::bitset<128> keys = std::bitset<128>().set();   // the keys a zone covers (all, when unknown)
        bool drumKit = false;   // bank 128 or named a kit (DrumKit::isKit): the DRUM KIT panel's pads
        SoundFontInfo::Sends sends;   // what it sends to the reverb and the chorus, in percent
    };

    // Message thread only.
    bool loadSoundFont (const juce::File& file);
    void selectPreset (int presetIndex);
    // The second instrument, for Layer and Split (playMode-like parameter "layerMode").
    void selectLayerPreset (int presetIndex);
    // Everything about the SoundFont that readers need, as one immutable snapshot: whoever
    // changes the state publishes a new one, and readers (the editor, getStateInformation, any
    // thread) take the current one at once, never waiting for a load and never seeing half a
    // change. Take it once per operation: `const auto st = player.state();`.
    struct State
    {
        juce::File file;          // the open SoundFont (none when missing)
        juce::File missing;       // the SoundFont the project names when it isn't loaded: moved, renamed or unreadable
        std::vector<Preset> presets;
        SoundFontInfo info;
        int preset = 0, layer = 0;   // indices into presets
        int bank = -1, number = -1, layerBank = -1, layerNumber = -1;   // the instruments by bank:program, as saved
    };
    std::shared_ptr<const State> state() const
    {
        const juce::SpinLock::ScopedLockType lock (publishedLock);
        return published;
    }
    // Shorthands for the tests, which run on one thread. Each takes its own snapshot, so code
    // that reads more than one value takes state() once instead.
    int selectedLayerPreset() const { return state()->layer; }
    std::vector<Preset> presets() const { return state()->presets; }
    juce::File soundFontFile() const { return state()->file; }
    SoundFontInfo soundFontInfo() const { return state()->info; }
    int selectedPreset() const { return state()->preset; }
    juce::File missingSoundFont() const { return state()->missing; }

    // Message-thread housekeeping, ten times a second (a timer; tests call it directly):
    // keeps in memory the instruments a clip switched to (so switching back reads nothing from
    // disk), and loads a missing file again once it's back (a drive plugged in), or an
    // unreadable one when it changes (with a growing wait between tries).
    void runMessageThreadTasks();

    // The Guitar panel shows (and its controls act) for this instrument: Auto follows the
    // instrument, On and Off force it.
    bool guitarPanelActive() const;
    void markProjectChanged();   // a change that isn't a parameter: the host is told the project changed
    bool detectedGuitar() const { return guitarInstrument.load(); }   // what Auto sees in the instrument
    static bool isGuitar (const Preset&);

    // The last chord or note the Guitar panel's engine started, for the editor's neck, chord
    // name, strum arrow and palm light (written by the audio thread, `sequence` last).
    struct GuitarView
    {
        std::atomic<int> sequence { 0 };
        std::array<std::atomic<int>, 6> notes {};   // the lowest six, low to high
        std::atomic<int> count { 0 };
        std::atomic<int> pitchClasses { 0 };        // bit n: pitch class n sounds
        std::atomic<int> bass { -1 };
        std::atomic<bool> up { false }, strummed { false }, palm { false };
        std::atomic<int> fx { 0 };                  // the Velocity Switch FX it fired (0: none)
        std::atomic<int> position { 0 };            // the Chord Position voiced (Kbd's choice)
    };
    GuitarEngine::Settings guitarSettingsNow() const { return currentGuitarSettings(); }   // for the editor
    int guitarTuning() const { return juce::roundToInt (parameters.getRawParameterValue ("guitarTuning")->load()); }
    GuitarView guitarView;
    // A new instance (no saved state) gets the Guitar panel ready to try the first time it shows:
    // Chords, Pattern and Hold. The parameters' own defaults stay neutral (Direct), so a project
    // saved before they existed plays as it did. Only while the panel hasn't played yet (a start
    // given later would change what was recorded). Atomics: hosts restore state on any thread.
    std::atomic<bool> stateRestored { false }, guitarStartGiven { false };
    // DRUM KIT: the instrument is a kit, and the user's Drum pads choice for it (0 auto, 1 on,
    // 2 off; the editor keeps it per instrument and hands it over, the project saves it), so
    // Humanize plays exactly where the panel shows, with or without the window open.
    std::atomic<bool> presetIsKit { false };
    std::atomic<int> presetId { -1 };   // bank * 128 + program of the instrument
    std::atomic<int> kitGeneration { 0 };   // a new instrument: channels a clip moved elsewhere play the kit again
    std::atomic<int> drumChoice { -1 };   // (instrument id) * 4 + choice, in one atomic so the pair is never torn; -1: none
    void refreshKitFlag()   // message thread, after the preset changes
    {
        const bool valid = juce::isPositiveAndBelow (currentPreset, (int) presetList.size());
        presetIsKit.store (valid && presetList[(size_t) currentPreset].drumKit);
        presetId.store (valid ? presetList[(size_t) currentPreset].bank * 128 + presetList[(size_t) currentPreset].number : -1);
    }
    // Message thread. byUser: the user chose it (the project changed); otherwise the window only
    // hands over what its settings hold (opening it doesn't make a project unsaved).
    void setDrumChoice (int bank, int number, int choice, bool byUser)
    {
        const int packed = packDrumChoice (bank, number, choice);
        if (drumChoice.exchange (packed) != packed && byUser)
            markProjectChanged();
    }
    // The window's choice, written only if the project's is still `expected` (what the window last
    // saw); false: something else changed it meanwhile (a state restored), so it's read again.
    bool offerDrumChoice (int expected, int bank, int number, int choice)
    {
        return drumChoice.compare_exchange_strong (expected, packDrumChoice (bank, number, choice));
    }
    static int packDrumChoice (int bank, int number, int choice) { return (bank * 128 + number) * 4 + juce::jlimit (0, 2, choice); }
    // The choice the project holds for that instrument (-1: none, the editor's settings decide).
    int drumChoiceOf (int bank, int number) const
    {
        const int packed = drumChoice.load();
        return packed >= 0 && packed / 4 == bank * 128 + number ? packed % 4 : -1;
    }
    void seedHumanize (juce::int64 seed) { humanRandom.setSeed (seed); }   // tests: a known sequence of delays
    bool drumsActive() const   // any thread
    {
        const int packed = drumChoice.load();
        const int choice = packed >= 0 && packed / 4 == presetId.load() ? packed % 4 : 0;
        return choice == 1 || (choice == 0 && presetIsKit.load());
    }
    std::atomic<bool> playedGuitar { false };   // the panel already played notes: too late for the start
    juce::CriticalSection startLock;   // the start's check and edits vs. a state being restored (never the audio thread)
    // The Guitar panel is playing (shown and not Direct): what the processor's blocks go by.
    bool guitarPlaying() const { return guitarPanelActive() && ! (parameters.getRawParameterValue ("guitarDirect")->load() > 0.5f); }   // as processBlock reads it

    // The chord drawn on the neck (Shape mode): a fret per string from the low E, -1 not played.
    // Saved with the project. Message thread sets it; the audio thread reads the packed copy.
    void setGuitarShape (const std::array<int, 6>& frets);
    std::array<int, 6> guitarShapeFrets() const { return GuitarEngine::unpackShape (guitarShape.load()); }
    // The editor asks to hear the drawn chord through the guitar (the audio thread plays it).
    std::atomic<int> auditionRequests { 0 };
    std::atomic<int> previewRequest { 0 };   // the Preview button's note + 1, through the guitar (0: none)
    std::atomic<juce::uint32> guitarShape { GuitarEngine::packShape ({ -1, -1, -1, -1, -1, -1 }) };

    // Voices sounding after the last block (for the editor and tests).
    std::atomic<int> activeVoices { 0 };
    // Tests: render with another interpolation (FLUID_INTERP_*); -1 keeps the 7th order.
    int interpolationOverride = -1;

    juce::AudioProcessorValueTreeState parameters;

    // For the editor: note-ons received, the latest velocity, the output's peak since last read.
    std::atomic<int> notesReceived { 0 };
    std::atomic<int> lastVelocity { 0 };
    std::atomic<int> lastPlayedVelocity { 0 };   // the last note-on the Guitar panel took, as played
    std::atomic<float> outputPeak { 0.0f };

    // Notes from the host light the on-screen keyboard; its clicks play the synth.
    juce::MidiKeyboardState keyboardState;   // the window's notes (its keyboard, the drums): what it plays and sends out
    juce::MidiKeyboardState hostNotes;       // the host's notes, only to show them (keys, lights); nothing plays from it
    juce::MidiKeyboardState previewNotes;    // a preview or a chord audition: plays, but isn't played by the user (not sent out, not recorded)

    struct Engine;   // a FluidSynth instance with the SoundFont loaded

    // FluidSynth runs 32 channels: 0-15 play the instrument, 16-31 the layer (channel + 16).
    // 0-15 the instrument, 16-31 the layer, 32-47 the Guitar panel's Double (the instrument again,
    // so a doubled note never cuts its twin: FluidSynth ends a note repeated on its channel).
    static constexpr int layerChannelOffset = 16, doubleChannelOffset = 32, numSynthChannels = 48;
    static bool isLayerChannel (int channel) { return channel >= layerChannelOffset && channel < doubleChannelOffset; }

private:
    static constexpr int numMidiChannels = 16;

    void handleMidi (const juce::MidiMessage&);
    void render (int start, int end);
    void writeOutputs (juce::AudioBuffer<float>&, int numSamples, bool sendOutputs);
    void applyEffectSettings (bool force);
    void applyPlaySettings (bool force, bool guitarPlays);
    void applyPreset (Engine&, int presetIndex);
    void applyLayerPreset (Engine&, int presetIndex);
    int layerPreset = 0;
    int savedLayerBank = -1, savedLayerNumber = -1;
    void swapEngine (std::unique_ptr<Engine>);

    juce::SpinLock engineLock;   // guards `engine` between the audio and message threads

    // Events of a block that couldn't take the engine, played at the start of the next one, so
    // a note-off, a pedal release or a bend's return is never lost. A fixed array of short
    // messages (no allocation, O(1) per event): past `deferredNormal` only events that change
    // lasting state are kept, and pitch bends collapse to the last one per channel.
    static constexpr int deferredCapacity = 4096, deferredNormal = 3584;
    std::array<juce::MidiMessage, deferredCapacity> deferredEvents;
    int numDeferred = 0;
    // Past `deferredNormal`, continuous values keep only their latest per channel (-1: none).
    std::array<int, 16> deferredBend, deferredPressure;
    std::array<std::array<juce::int8, 128>, 16> deferredController;
    void deferMidi (const juce::MidiBuffer&);
    // `guitar`: the Guitar panel's settings when its engine is in use, so kept notes go through it.
    struct Humanize { float velocity = 0; double maxDelay = 0; int splitBelow = -1; bool panelShown = false; };
    void playOrWait (juce::MidiMessage, juce::int64 now, const Humanize&);
    void playDeferredMidi (const GuitarEngine::Settings*, juce::int64 at, const Humanize& human);
    // Ends every note the Guitar panel's engine started (its twins included) and forgets it all.
    void flushGuitar();
    std::array<std::array<juce::int16, 128>, 16> twinKey;   // the Double's twin, as transposed at its note-on (-1: none)
    void clearDeferredMidi();

    void timerCallback() override { runMessageThreadTasks(); }
    // Audio thread -> message thread: presets a clip switched to (bank * 128 + program).
    // A channel's request is queued only when it differs from that channel's last one.
    juce::AbstractFifo pinFifo { 256 };
    std::array<int, 256> pinQueue {};
    std::array<int, 16> lastPinRequest;   // audio thread only
    std::atomic<int> pinGeneration { 0 };   // bumped when the engine or a channel's preset changes: pin afresh
    int seenGeneration = 0;                    // audio thread only
    juce::MidiBuffer shortMidi;   // a block's MIDI without sysex (reserved in prepareToPlay)
    juce::MidiBuffer blockMidi;     // the host's MIDI plus the window's notes, as played (reserved in prepareToPlay)
    std::array<juce::uint8*, 4096> patchedSoundOffs {};   // CC120 bytes shown as CC123 for one read (more than a flood of them carries)
    juce::MidiBuffer previewHere;   // this block's preview notes (reserved in prepareToPlay)
    bool bypassedBlock = false;     // the block runs for processBlockBypassed: nothing goes into a take or the meter

    // Guitar panel (audio thread, under engineLock): the strum timeline and its clock.
    GuitarEngine guitar;
    juce::int64 sampleClock = 0;   // samples since prepareToPlay
    double takeSeconds = 0;   // REC's clock: seconds of audio processed, never set back (a take may span an audio restart)
    bool guitarEngaged = false;    // notes are going through `guitar`
    // Whether the chosen instrument is a guitar (General MIDI 24-31, or named one): set when
    // the state is published, read by the audio thread for the panel's Auto setting.
    std::atomic<bool> guitarInstrument { false };
    GuitarEngine::Settings currentGuitarSettings() const;
    // A guitar note's voices just started: detune them (Humanize) or darken them (palm mute).
    void tweakNewVoices (int channel, int detuneCents, bool palmMuted, int pan, float attenuationCb, juce::int64 sinceVoiceId);
    static constexpr int doublePan = 350;   // Double: original and twin this far apart (of 500)
    juce::int64 newestVoiceId() const;
    double tremoloPhase = 0.0;
    // Pattern mode: plays the rhythm's steps that fall in this block, on every channel holding a chord.
    void runPattern (const GuitarEngine::Settings&, const juce::Optional<juce::AudioPlayHead::PositionInfo>&, juce::int64 blockStart, int numSamples);
    // Pitch per voice: the engine's slides and bends, and the wheels and aftertouch when they're
    // given to Pitch, Slide or Mono Bend. Each voice is retuned by what changed since last time.
    void applyPitch (juce::int64 now, juce::int64 sinceVoiceId);
    bool guitarController (const juce::MidiMessage&, const GuitarEngine::Settings&, int channel, juce::int64 at);
    void refreshWheelOffsets();
    void clearWheels();
    void noteWheelPosition (const juce::MidiMessage&);
    int lastPbAssign = 0, lastMwAssign = 0, lastAtAssign = 0;   // refreshWheelOffsets: a job just taken rests the synth's own wheel
    int blockPb = 0, blockMw = 0, blockAt = 0;   // the wheels' jobs as read at this block's start (audio thread)
    static bool changesLastingState (const juce::MidiMessage&);
    bool hostWasPlaying = false;   // the transport's state at the last block (audio thread)
    juce::Optional<juce::AudioPlayHead::PositionInfo> blockPosition;   // this block's playhead (audio thread)
    // A guitar block's events (the clip's and the engine's), sorted by time. Audio thread.
    struct GuitarItem { int position, order; juce::uint8 bytes[3]; int size; GuitarEngine::Event note; bool fromGuitar; };
    std::array<GuitarItem, 2048> guitarItems;
    juce::MidiBuffer playedHere;   // this block's notes played in the window (reserved in prepareToPlay)
public:
    // A note-on in raw bytes (velocity 0 is a note-off): the processor and REC count notes alike.
    static bool isNoteOnBytes (const juce::uint8* data, int size) { return size == 3 && (data[0] & 0xf0) == 0x90 && data[2] > 0; }
    // REC: while on, what's played in the window goes to the editor (a FIFO, no locks), which
    // makes a MIDI file of it to drag into the host. Times both in seconds and, while the host's
    // transport runs, in quarter notes of its timeline (for a take that lands on its bars).
    // ppq: where the host's transport was (onGrid); barStart: the ppq its current bar began at
    // (-1: the host didn't say), so time-signature changes before the take still land right.
    // bpm, barQuarters: the host's tempo and bar length when it was played (the take keeps the first note's).
    struct TakeEvent { double seconds = 0, ppq = 0, barStart = -1, bpm = 120, barQuarters = 4; bool onGrid = false; juce::uint8 bytes[3] {}; int size = 0; };
    // REC's state, one atomic so the window and the audio thread never disagree: idle, waiting
    // for the host's play (the take starts with the transport), recording with the transport
    // (it ends when the host stops), recording on its own, or ended by the host (the window
    // then hands the take over).
    enum TakeState { takeIdle = 0, takeArmed, takeWithHost, takeFree, takeEndedByHost };
    std::atomic<int> takeState { takeIdle };
    std::atomic<int> takeWriters { 0 };
    juce::File takesFolder;   // message thread: where takes are written (tests point it elsewhere); empty: Application Support   // audio blocks between reading takeState and writing the take's events
    struct Grid { bool known = false; double ppq = 0, seconds = 0, bpm = 120, barQuarters = 4, barStart = -1; } lastGrid;   // audio thread
    std::bitset<16 * 128> takeHeld;   // audio thread: notes a take holds (channel * 128 + key), so a bypassed key-up can end them
    bool hostTransportSeen = false;   // audio thread: a block ever came with the host's position
    bool velocityRecorded = false;    // audio thread: handleMidi leaves lastVelocity alone (already the note's own)
    juce::File lastTakeFile;   // message thread: the last take, kept while the window is closed
    juce::String lastTakeText;
    juce::AbstractFifo takeFifo { 8192 };
    std::vector<TakeEvent> takeEvents = std::vector<TakeEvent> (8192);   // on the heap: hosts and tests may keep the processor on the stack
    std::atomic<double> takeBpm { 120.0 }, takeBarQuarters { 4.0 };
private:
    // Drum Humanize's late hits and the notes of the same key that must follow them, by absolute
    // sample. Audio thread.
    struct LateNote { juce::int64 at; juce::uint64 order; juce::uint8 bytes[3]; int size; int slot; };   // slot: the key slot it waits in, as queued
    static int channelOf (const LateNote& late) { return late.bytes[0] & 0x0f; }
    // General MIDI's keys that cut each other off (exclusive classes) share one waiting slot.
    static int waitSlot (int key)
    {
        switch (key)
        {
            case 44: case 46: return 42;   // pedal, open hi-hat: with the closed one
            case 81: return 80;            // open triangle: with the muted one
            case 72: return 71;            // long whistle: with the short one
            case 74: return 73;            // long guiro: with the short one
            case 79: return 78;            // open cuica: with the muted one
            default: return key;
        }
    }
    // A key's slot: coupled only on the kit's side of a Split (a lower instrument's key waits alone).
    // By the key played (a Transpose changed mid-hit can't move a wait to another slot). On the kit's
    // side of a Split only: a group cut by the split key waits as its lowest member above it (the
    // pedal and open hats together when the closed one plays the lower instrument).
    static int slotOf (int key, int splitBelow)
    {
        if (key < splitBelow) return key;
        const int slot = waitSlot (key);
        if (slot == key || slot >= splitBelow) return slot;   // the usual case, at once
        for (int k = splitBelow; k <= key; ++k)   // a group the split key cuts: a few keys at most
            if (waitSlot (k) == slot) return k;
        return key;
    }
    // An all-notes-off or all-sound-off, in raw bytes (one definition for every place that asks).
    static bool isAllOffBytes (const juce::uint8* data, int size)
    {
        return size == 3 && (data[0] & 0xf0) == 0xb0 && (data[1] == 120 || data[1] == 123);
    }
    static bool earlier (const LateNote& a, const LateNote& b) { return a.at < b.at || (a.at == b.at && a.order < b.order); }
    static bool isNoteOffBytes (const juce::uint8* data, int size)
    {
        return size == 3 && ((data[0] & 0xf0) == 0x80 || ((data[0] & 0xf0) == 0x90 && data[2] == 0));
    }
    // When each key's last waiting message is due (a time already past: nothing waits). Cleared
    // where messages leave the list early (playLateNotesNow, an all-notes-off, playKeyNow) and in
    // prepareToPlay, whose kept note-offs wait at time 0, ahead of anything new.
    std::array<std::array<juce::int64, 128>, 16> keyDue {};
    std::array<std::array<juce::int32, 128>, 16> keyDelay {};   // the Humanize delay of each key's last hit (its note-off waits as long)
    std::array<LateNote, 512> lateNotes {};
    int lateCount = 0;
    juce::uint64 lateOrder = 0;   // arrival order, for messages waiting until the same sample
    juce::int64 nextLateAt = std::numeric_limits<juce::int64>::max();   // the earliest waiting time (none: max)
    juce::Random humanRandom;
    std::bitset<16> channelMoved;   // a clip's program change took this channel off the kit (no Humanize there)
    int seenKitGeneration = -1;
    void playLateNotesNow();
    bool takeEarliestLate (juce::int64 until, LateNote& out);
    void refreshNextLate();   // after taking messages off the list other than through takeEarliestLate
    void playKeyNow (int channel, int key);
    void showHostNotes (juce::MidiBuffer& fromHost, int numSamples);
    void dropLateHits();
    void clearLateNotes();
    void forgetKeyWaits (int channel = -1);
    void playLateNotes (juce::int64 until, juce::int64 blockStart, int& position);
    std::array<int, 16384> overflowIndices {};   // a flood's messages past guitarItems, by their place in the block
    std::array<std::bitset<128>, 16> lateNoteOffs {};   // note-offs past even overflowIndices, played at the block's end
    // What each voice was retuned by, keyed by FluidSynth's voice (a fixed pool, so a slot per pool
    // entry, found by linear probing and never shared); a new note on that voice (another id)
    // starts from 0. Cleared with a new engine.
    struct AppliedPitch { const void* voice = nullptr; juce::int64 id = -1; float cents = 0.0f; };
    std::array<AppliedPitch, 1024> appliedPitch {};
    AppliedPitch* appliedPitchFor (const void* voice, juce::int64 id);
    std::array<float, 16> channelCents {}, monoCents {};   // the wheels' offsets, per channel
    std::array<float, 16> wheelBend {}, wheelMod {}, wheelPressure {};   // -1..1, 0..1, 0..1
    void applyTremolo (int numSamples);
    int housekeepingTicks = 0;

    // A file that failed to load is retried when it changes (time or size), or after a wait
    // that doubles from one minute to thirty.
    juce::File failedFile;
    juce::Time failedFileTime;
    juce::int64 failedFileSize = -1;
    juce::uint32 failedAtMs = 0, retryWaitMs = 60000;
    void recordFailure (const juce::File&);
    bool shouldRetry (const juce::File&) const;

    // The published snapshot (see State). Writers change the members below under stateMutex,
    // always through a StateWriter, which publishes when the outermost one ends.
    std::shared_ptr<const State> published = std::make_shared<State>();
    mutable juce::SpinLock publishedLock;
    int writerDepth = 0;
    bool stateChanged = false;   // a writer changed what the editor shows: sent once published
    void publish();
    void exchangePublished (std::shared_ptr<const State>);
    // The outermost writer publishes, then tells the editor: a change message sent before the
    // publish could reach the message thread first and show the old snapshot.
    struct StateWriter
    {
        explicit StateWriter (SF2PlayerProcessor& p) : owner (p), lock (p.stateMutex) { ++owner.writerDepth; }
        ~StateWriter()
        {
            if (--owner.writerDepth != 0)
                return;
            owner.publish();
            if (std::exchange (owner.stateChanged, false))
                owner.sendChangeMessage();
        }
        SF2PlayerProcessor& owner;
        std::lock_guard<std::recursive_mutex> lock;
    };

    // No SoundFont, the file kept for the project (shown as missing, or unreadable). The one
    // way into that state; `keepInstruments` saves the current ones by bank:program first.
    void enterMissingState (const juce::File& file, bool keepInstruments, bool unreadable);

    // Serializes the writers of the members below (the message thread, and prepareToPlay or
    // setStateInformation, which a host may call from another thread): the file, the preset
    // list and choices, the sample rate. Taken through StateWriter; never on the audio thread,
    // never by readers (they use state()), always before engineLock.
    std::recursive_mutex stateMutex;
    // Notifies the host (VST3 "dirty") outside stateMutex, so its handler can't deadlock with
    // a prepareToPlay waiting for the mutex.
    std::atomic<bool> projectChangePending { false };
    void flushProjectChanged();
    // Past the deferred reserve, true once a value was collapsed (else nothing to replay).
    bool anyCollapsed = false;

    std::unique_ptr<Engine> engine;

    juce::File currentFile, missingFile;
    int savedBank = -1, savedNumber = -1;   // the project's instrument, found again by bank:program on load
    std::atomic<bool> restoringState { false };   // read by markProjectChanged from any thread
    struct Restoring   // restoringState for a scope
    {
        std::atomic<bool>& flag; bool was;
        explicit Restoring (std::atomic<bool>& f) : flag (f), was (f.exchange (true)) {}
        ~Restoring() { flag.store (was); }
    };
    SoundFontInfo currentInfo;
    std::vector<Preset> presetList;
    int currentPreset = 0;
    double sampleRate = 44100.0;
    // What FluidSynth renders: main L/R, reverb L/R, chorus L/R.
    juce::AudioBuffer<float> work;
    float lastGain = 1.0f;

    // Tone: a 4-pole Butterworth low-pass (two biquads) on every channel of `work`.
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void setLowPass (double frequency, double rate, double q);
        void process (float* samples, int count) noexcept;
    };
    std::array<std::array<Biquad, 2>, 6> toneFilters;
    float toneFrequency = 0.0f;   // applied; 0 = not yet

    struct EffectSettings
    {
        bool reverbOn = true, chorusOn = true, sendOutputs = false;
        float reverbLevel = 0, reverbRoom = 0, reverbDamping = 0, reverbWidth = 0;
        float chorusLevel = 0, chorusRate = 0, chorusDepth = 0;
        int chorusVoices = 0;
        bool operator== (const EffectSettings& o) const   // exact on purpose: any change re-applies
        {
            using juce::exactlyEqual;
            return reverbOn == o.reverbOn && chorusOn == o.chorusOn && sendOutputs == o.sendOutputs && chorusVoices == o.chorusVoices
                && exactlyEqual (reverbLevel, o.reverbLevel) && exactlyEqual (reverbRoom, o.reverbRoom)
                && exactlyEqual (reverbDamping, o.reverbDamping) && exactlyEqual (reverbWidth, o.reverbWidth)
                && exactlyEqual (chorusLevel, o.chorusLevel) && exactlyEqual (chorusRate, o.chorusRate)
                && exactlyEqual (chorusDepth, o.chorusDepth);
        }
    };
    EffectSettings appliedEffects;
    EffectSettings currentEffectSettings() const;

    // How the instrument is played. Every default leaves the SoundFont as it is.
    struct PlaySettings
    {
        float attack = 1, release = 1;   // envelope time multipliers
        int fineTune = 0, bendRange = 2, mode = 0, glide = 0;   // cents, semitones, Poly/Mono/Legato, ms
        int layerAttenuation = 0;   // centibels, on the layer's channels
        bool operator== (const PlaySettings& o) const
        {
            return juce::exactlyEqual (attack, o.attack) && juce::exactlyEqual (release, o.release)
                && fineTune == o.fineTune && bendRange == o.bendRange && mode == o.mode && glide == o.glide
                && layerAttenuation == o.layerAttenuation;
        }
    };
    PlaySettings appliedPlay;
    // The plugin's per-channel settings (generator offsets, glide), again: a clip's Reset All
    // Controllers (CC121) clears them in FluidSynth.
    void applyChannelSettings (int channel, const PlaySettings&);
    bool playApplied = false;
    PlaySettings currentPlaySettings (bool guitarPlays) const;

    // Notes as played after transposing, so a note-off finds its note even if the transpose moved meanwhile.
    std::array<std::array<juce::int16, 128>, 16> playedNote;   // -1: none
    int transposeFor (int note) const;
    int curvedVelocity (int velocity) const;

    friend struct PluginTestsAccess;   // the tests hold engineLock to make a block skip
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SF2PlayerProcessor)
};
