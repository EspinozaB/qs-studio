// QS Studio - sample names per model, and converting programs between models whose ROMs number samples differently.
// The QS6 has its own 8 MB sample ROM; the QS7, QS8 and QSR (and the .1/.2 models) share a 16 MB ROM ("QS78").
// A program stores only group + sample number, so the same program can play a different sample on another model.
#pragma once
#include <string>
#include <vector>

#include "qs_program.h"

namespace qs {

enum class RomSet { QS6, QS78 };
const char* romSetName(RomSet r);  // "QS6" or "QS7/QS8/QSR"
RomSet romSetForModel(const std::string& model);  // "QS6" -> QS6, anything else -> QS78

// Sample name for a program's group/number (number is 1-based, as stored). "" if unknown.
std::string sampleName(RomSet r, bool drum, int group, int number);
int sampleCount(RomSet r, bool drum, int group);
std::vector<int> sampleGroups(RomSet r, bool drum);
const char* drumGroupName(RomSet r, int group);

struct ConvertNote {
    int sound = 0, drum = -1;  // drum = -1 for a keyboard sound
    std::string from, to;      // sample names (to empty when unmatched)
    enum Kind { Same, Renumbered, Close, Unmatched } kind = Same;
};

struct ConvertedProgram {
    Program program;
    std::vector<ConvertNote> notes;
    int changed() const;
    int unmatched() const;
};

// Point every enabled sound's samples at the target model's sample with the same (or a very close) name.
// Unmatched samples are left as they were and reported.
ConvertedProgram convertProgram(const Program& p, RomSet from, RomSet to);

// Converts every program dump (opcodes 00/02) in a list of messages; other messages pass through unchanged.
struct ConvertedBank {
    std::vector<Bytes> messages;
    std::vector<std::pair<int, ConvertedProgram>> programs;  // program number, result
    std::string report(RomSet from, RomSet to) const;
};
ConvertedBank convertBank(const std::vector<Bytes>& msgs, RomSet from, RomSet to);

}  // namespace qs
