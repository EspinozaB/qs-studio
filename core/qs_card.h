// QS Studio - Alesis QCard / QuadraCard / Flash sound card images.
#pragma once
#include <string>
#include <vector>

#include "qs_sysex.h"

namespace qs {

struct Zone {
    int root = 60, finetune = 0, unk2 = 0, level = 0;
    uint32_t start = 0, end = 0;  // 22-bit word addresses, inclusive end
    uint32_t loopOffset = 0;      // words from start; 0 = one-shot
    int flagsA = 0, flagsB = 0;
};

struct Voice {
    size_t offset = 0;
    int group = 0, index = 0;
    std::string name;
    std::vector<int> splits;  // raw split bytes (MIDI note = split + 12, unverified)
    int topKey = 0, layers = 1, velSplit1 = 128, velSplit2 = 128;
    std::vector<Zone> zones;  // interleaved: zone[k * layers + layer]
};

struct CardImage {
    Bytes data;                       // normalized: big-endian words
    std::vector<std::string> fixes;   // what load() corrected
    std::string cardName;
    std::vector<std::string> groups;
    std::vector<Voice> voices;
    std::vector<std::string> programNames;  // 128
    std::vector<std::string> mixNames;      // 100

    bool isRom = false;               // an internal sample ROM dump rather than a card
    bool valid() const { return !voices.empty() || !programNames.empty(); }
    // In a ROM, a zone's start-address bit 22 (flagsB bit 0) picks the second 8 MB chip. A dump of the first chip alone
    // can't play those zones.
    uint32_t zoneWordBase(const Zone& z) const { return isRom && (z.flagsB & 1) ? 0x400000u : 0u; }
    bool zoneAvailable(const Zone& z) const { return size_t(zoneWordBase(z) + z.end) * 2 + 1 < data.size(); }
    size_t uniqueSampleCount() const;
    int16_t sampleAt(uint32_t word) const;

    // Unpacked 350-byte program as in SysEx (card stores bytes 1..349).
    Bytes program(int i) const;
    Bytes mix(int i) const;  // 138 bytes (new format) or 123 bytes (R-series old format)
};

// Where each zone of a voice plays: key range (MIDI notes) and velocity range. Same mapping as the SFZ export.
struct Region {
    int lo = 0, hi = 127, lovel = 1, hivel = 127;
    const Zone* zone = nullptr;
};
std::vector<Region> voiceRegions(const Voice& v);

// Name of a voice directory group: 0-15 the keyboard groups, 16-23 card groups, 24 Rhythm, 48-61 the drum groups
// (the same groups the QS shows when you pick a sample).
std::string voiceGroupName(const CardImage& c, int group);

// Load and normalize an image (.img/.bin). Returns false with error text on failure.
bool loadCardImage(const Bytes& raw, CardImage& out, std::string& error);
// A QS7/QS8/QSR internal sample ROM dump (from Cards & ROM): same voice directory as a card, at the start of the ROM.
bool loadInternalRom(const Bytes& raw, CardImage& out, std::string& error);
// Program-bank only check for RAM cards and partial reads.
bool looksLikeCard(const Bytes& data);

// WAV (16-bit mono 48 kHz) with smpl chunk carrying root key and loop.
Bytes makeWav(const int16_t* pcm, size_t n, int rate, int rootKey, long loopStart, long loopEnd);

struct ExportedSample { std::string fileName; Bytes wav; };
std::vector<ExportedSample> exportSamples(const CardImage& card);
// One .sfz per voice referencing samples/<file>.wav. Returns {fileName, text}.
std::vector<std::pair<std::string, std::string>> exportSfz(const CardImage& card);

std::string safeName(const std::string& s);

}  // namespace qs
