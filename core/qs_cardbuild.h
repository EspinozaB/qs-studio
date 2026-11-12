// QS Studio - building a QS sound card image from your own samples (the job Alesis Sound Bridge did).
//
// Card format as read from real cards (see core/qs_card.cpp and docs/CARDS.md):
//  - 0x00000: programs and mixes (Z-format: A5 C3 5A A5, 128 x 349-byte programs, 100 x 138-byte mixes, then effects)
//  - 0x10000: sample header 00 70 38 1C, 02 40 00 00 (02 40 seen on 8 MB cards), voice count (16-bit), card name (14),
//             14 group names of 6 characters (slots 0-7 = keyboard groups 16-23)
//  - 0x1006C: voice directory, then 16-bit big-endian samples at 48 kHz
//  - each sample plays from start, then loops from start + loop offset (16-bit, so within the first 65535 words) back
//    to its end word, which holds a copy of the loop start; 8 guard words follow every sample.
// Built cards use only what was verified on Alesis's own cards. Test on a FLASH card you can rewrite.
#pragma once
#include <string>
#include <vector>

#include "qs_sysex.h"

namespace qs {

constexpr int kCardRate = 48000;
constexpr uint32_t kCardWords = 0x400000;  // 8 MB card
constexpr uint32_t kMaxLoopOffset = 0xFFFF;

struct BuildZone {
    std::vector<int16_t> pcm;  // mono, 48 kHz
    int root = 60;             // MIDI note that plays the sample at its own pitch
    long loopStart = -1;       // sample frame; -1 = one-shot (a short silent loop is added after it)
    int hiKey = 127;           // highest MIDI note this zone plays (zones are in rising key order)
    std::string source;        // file name, for messages
};

struct BuildVoice {
    std::string name;  // 10 characters on the QS
    int group = 16;    // 16-23: the card's keyboard groups
    std::vector<BuildZone> zones;
};

struct CardBuild {
    std::string cardName = "QS Studio card";  // 14 characters
    std::vector<std::string> groupNames{"User1"};  // groups 16, 17... (6 characters each)
    std::vector<BuildVoice> voices;
    bool makePrograms = true;  // one program per voice, sound 1 playing it
};

struct BuildResult {
    Bytes image;  // big-endian, ready for Cards & ROM > Write card, the Sound Player or loadCardImage
    std::vector<std::string> notes;
    uint32_t wordsUsed = 0;
    double secondsUsed() const { return wordsUsed / double(kCardRate); }
};

bool buildCard(const CardBuild& build, BuildResult& out, std::string& error);

// A WAV file as card material: any PCM (8/16/24/32-bit) or 32-bit float, mixed to mono, resampled to 48 kHz.
// root and loop come from the smpl chunk when there is one (root = -1, loopStart = -1 otherwise).
struct WavSample {
    std::vector<int16_t> pcm;
    int sourceRate = 0, channels = 0, root = -1;
    long loopStart = -1, loopEnd = -1;  // at 48 kHz
};
bool readWav(const Bytes& file, WavSample& out, std::string& error);

// The note a file name ends with: "Piano_C3.wav", "Str F#4", "bass-36" -> MIDI note (C3 = 60, as the QS shows it; C-2 = 0). -1 if none.
int noteFromName(const std::string& fileName);
// File name without its note and extension: "Piano_C3.wav" -> "Piano".
std::string baseNameWithoutNote(const std::string& fileName);

// Files with the same base name become one multisampled voice, split halfway between their roots.
struct NamedWav {
    std::string fileName;
    WavSample wav;
};
std::vector<BuildVoice> voicesFromWavs(const std::vector<NamedWav>& files, std::vector<std::string>& notes);

}  // namespace qs
