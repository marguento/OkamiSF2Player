#pragma once

#include <juce_core/juce_core.h>
#include <map>
#include <optional>
#include <utility>
#include <vector>

// What the plugin shows about an .sf2 beyond what FluidSynth exposes: the INFO
// list (name, author, comment) and each preset's key range, from the pdta chunks.
struct SoundFontInfo
{
    juce::String name, author, comment;
    std::map<std::pair<int, int>, std::pair<int, int>> keyRanges;   // (bank, program) -> (low key, high key)

    // How much a preset sends to the reverb and the chorus, in percent (generators 16 and 15),
    // lowest and highest over its zones: what plays with CC91 and CC93 at 0.
    struct Sends { float reverbMin = 0, reverbMax = 0, chorusMin = 0, chorusMax = 0; };
    std::map<std::pair<int, int>, Sends> sends;

    static SoundFontInfo read (const juce::File& file)
    {
        SoundFontInfo info;
        juce::FileInputStream in (file);
        if (! in.openedOk())
            return info;

        auto readId = [&in] { char id[5] = {}; in.read (id, 4); return juce::String (id); };
        if (readId() != "RIFF")
            return info;
        const auto riffEnd = juce::jmin ((juce::int64) in.readInt() + 8, in.getTotalLength());
        if (readId() != "sfbk")
            return info;

        // LIST chunks: INFO, sdta (the samples, skipped), pdta.
        while (in.getPosition() + 12 <= riffEnd)
        {
            const auto id = readId();
            const juce::int64 size = (juce::uint32) in.readInt();
            const auto end = in.getPosition() + size + (size & 1);
            if (id == "LIST")
            {
                const auto type = readId();
                if (type == "INFO")
                    info.readInfo (in, end);
                else if (type == "pdta")
                    info.readPresets (in, end);
            }
            if (! in.setPosition (end))
                break;
        }
        return info;
    }

private:
    static std::vector<std::pair<juce::String, juce::MemoryBlock>> subChunks (juce::InputStream& in, juce::int64 end)
    {
        std::vector<std::pair<juce::String, juce::MemoryBlock>> chunks;
        while (in.getPosition() + 8 <= end)
        {
            char id[5] = {};
            in.read (id, 4);
            const juce::int64 size = (juce::uint32) in.readInt();
            if (in.getPosition() + size > end)
                break;
            juce::MemoryBlock data;
            in.readIntoMemoryBlock (data, (ssize_t) size);
            if (size & 1)
                in.skipNextBytes (1);
            chunks.emplace_back (juce::String (id), std::move (data));
        }
        return chunks;
    }

    void readInfo (juce::InputStream& in, juce::int64 end)
    {
        for (auto& [id, data] : subChunks (in, end))
        {
            const auto* chars = static_cast<const char*> (data.getData());
            const auto text = juce::String::fromUTF8 (chars, (int) strnlen (chars, data.getSize())).trim();
            if (id == "INAM")      name = text;
            else if (id == "IENG") author = text;
            else if (id == "ICMT") comment = text;
        }
    }

    // A preset plays the keys where a preset zone's range meets one of its
    // instrument's zone ranges (generator 43); generator 41 picks the instrument.
    void readPresets (juce::InputStream& in, juce::int64 end)
    {
        std::map<juce::String, juce::MemoryBlock> chunks;
        for (auto& [id, data] : subChunks (in, end))
            chunks[id] = std::move (data);

        auto u16 = [] (const juce::MemoryBlock& b, size_t offset)
        {
            return offset + 2 <= b.getSize() ? (int) juce::ByteOrder::littleEndianShort (static_cast<const char*> (b.getData()) + offset) : 0;
        };
        auto byte = [] (const juce::MemoryBlock& b, size_t offset)
        {
            return offset < b.getSize() ? (int) static_cast<const juce::uint8*> (b.getData())[offset] : 0;
        };
        const auto& phdr = chunks["phdr"]; const auto& pbag = chunks["pbag"]; const auto& pgen = chunks["pgen"];
        const auto& inst = chunks["inst"]; const auto& ibag = chunks["ibag"]; const auto& igen = chunks["igen"];
        const int presets = (int) phdr.getSize() / 38 - 1, instruments = (int) inst.getSize() / 22 - 1;

        struct Zone
        {
            int low = 0, high = 127, link = -1;
            bool hasRange = false;   // generator 43 present in this zone
            std::optional<int> reverb, chorus;   // generators 16 and 15, in 0.1 %
        };
        // A hostile file can point every preset at every bag: indices are clamped to the chunks'
        // real sizes, and the whole walk stops after a budget of generators (a valid file needs
        // a few per zone; the 522 MB HQ collection reads under 100,000).
        long budget = 20'000'000;

        // The zones in bags [first, last): each zone's key range and its link (instrument or sample).
        auto zones = [&] (const juce::MemoryBlock& bags, const juce::MemoryBlock& gens, int first, int last, int linkGenerator)
        {
            std::vector<Zone> list;
            const int bagCount = (int) bags.getSize() / 4, genCount = (int) gens.getSize() / 4;
            last = juce::jmin (last, bagCount - 1);   // each bag reads the next one's start
            for (int bag = first; bag < last && budget > 0; ++bag)
            {
                Zone zone;
                const int genFirst = u16 (bags, (size_t) bag * 4);
                const int genLast = juce::jmin (u16 (bags, (size_t) (bag + 1) * 4), genCount);
                budget -= juce::jmax (0, genLast - genFirst) + 1;
                for (int g = genFirst; g < genLast; ++g)
                {
                    const int oper = u16 (gens, (size_t) g * 4);
                    if (oper == 43)
                        zone.low = byte (gens, (size_t) g * 4 + 2), zone.high = byte (gens, (size_t) g * 4 + 3), zone.hasRange = true;
                    else if (oper == linkGenerator)
                        zone.link = u16 (gens, (size_t) g * 4 + 2);
                    else if (oper == 16)
                        zone.reverb = (int) (juce::int16) u16 (gens, (size_t) g * 4 + 2);
                    else if (oper == 15)
                        zone.chorus = (int) (juce::int16) u16 (gens, (size_t) g * 4 + 2);
                }
                list.push_back (zone);
            }
            return list;
        };

        for (int p = 0; p < presets; ++p)
        {
            const size_t at = (size_t) p * 38;
            const int number = u16 (phdr, at + 20), bank = u16 (phdr, at + 22);
            int low = 128, high = -1;
            float reverbMin = 100, reverbMax = 0, chorusMin = 100, chorusMax = 0;
            const auto presetZones = zones (pbag, pgen, u16 (phdr, at + 24), u16 (phdr, at + 38 + 24), 41);
            // A first zone without an instrument is the global one: its values are the others' defaults.
            const Zone presetGlobal = ! presetZones.empty() && presetZones.front().link < 0 ? presetZones.front() : Zone {};
            for (const auto& presetZone : presetZones)
            {
                if (presetZone.link < 0 || presetZone.link >= instruments)
                    continue;   // the global zone
                const size_t instAt = (size_t) presetZone.link * 22;
                const auto instZones = zones (ibag, igen, u16 (inst, instAt + 20), u16 (inst, instAt + 22 + 20), 53);
                const Zone instGlobal = ! instZones.empty() && instZones.front().link < 0 ? instZones.front() : Zone {};
                for (const auto& instZone : instZones)
                {
                    if (instZone.link < 0)
                        continue;
                    // The instrument's value, plus the preset's offset (SF2 2.04, 9.4).
                    auto amount = [] (const Zone& i, const Zone& ig, const Zone& pz, const Zone& pg, std::optional<int> Zone::* gen)
                    {
                        const int value = (i.*gen).value_or ((ig.*gen).value_or (0)) + (pz.*gen).value_or ((pg.*gen).value_or (0));
                        return (float) juce::jlimit (0, 1000, value) / 10.0f;
                    };
                    const float reverb = amount (instZone, instGlobal, presetZone, presetGlobal, &Zone::reverb);
                    const float chorus = amount (instZone, instGlobal, presetZone, presetGlobal, &Zone::chorus);
                    reverbMin = juce::jmin (reverbMin, reverb), reverbMax = juce::jmax (reverbMax, reverb);
                    chorusMin = juce::jmin (chorusMin, chorus), chorusMax = juce::jmax (chorusMax, chorus);
                    // A zone without its own key range takes its global zone's (SF2 2.04, 9.4).
                    const Zone& presetRange = presetZone.hasRange ? presetZone : presetGlobal;
                    const Zone& instRange = instZone.hasRange ? instZone : instGlobal;
                    const int zoneLow = juce::jmax (presetRange.low, instRange.low), zoneHigh = juce::jmin (presetRange.high, instRange.high);
                    if (zoneLow <= zoneHigh)
                        low = juce::jmin (low, zoneLow), high = juce::jmax (high, zoneHigh);
                }
            }
            if (low <= high)
            {
                keyRanges[{ bank, number }] = { low, high };
                sends[{ bank, number }] = { reverbMin, reverbMax, chorusMin, chorusMax };
            }
        }
    }
};
