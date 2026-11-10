// Factory preset names built into QS Studio (names only, no program data), for the Preset 1-3 and GenMIDI banks.
// From the factory bank files shared by the QS6, QS7, QS8 and QSR (Q678PST1-4). Checked against a QS6 (Preset 2 mix 43).
#pragma once

namespace qs {

// bank: 1-3 = Preset 1-3, 4 = GenMIDI. Returns "" when there is no built-in name.
const char* factoryProgramName(int bank, int program);
const char* factoryMixName(int bank, int mix);

}  // namespace qs
