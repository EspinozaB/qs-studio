#include "qs_program.h"

#include <algorithm>

namespace qs {

#include "qs_params_table.inc"

int keyboardParamCount() { return int(sizeof(kKeyboardSoundParams) / sizeof(ParamDef)); }
const ParamDef& keyboardParam(int i) { return kKeyboardSoundParams[i]; }
int drumParamCount() { return int(sizeof(kDrumParams) / sizeof(ParamDef)); }
const ParamDef& drumParam(int i) { return kDrumParams[i]; }

int findKeyboardParam(const std::string& name) {
    for (int i = 0; i < keyboardParamCount(); ++i)
        if (name == kKeyboardSoundParams[i].name) return i;
    return -1;
}

const char* functionName(int f) {
    static const char* names[] = {"Voice", "Level", "Effect", "Pitch", "Filter", "Amp", "Range", "Mod",
                                  "Name", "Pitch LFO", "Filter LFO", "Amp LFO", "Pitch Env", "Filter Env",
                                  "Amp Env", "Tracking", "Mode"};
    return f >= 0 && f < int(sizeof(names) / sizeof(*names)) ? names[f] : "";
}

const char* sampleGroupName(int g) {
    static const char* names[] = {"Piano", "Chromatic", "Organ", "Guitar", "Bass", "String", "Brass", "Woodwind",
                                  "Synth", "Wave", "Noise", "Voice", "Ethnic", "Drums", "Percussion", "Sound FX"};
    if (g >= 0 && g < 16) return names[g];
    if (g == 24) return "Rhythm";
    return "";
}

const char* lfoWaveName(int v) {
    static const char* names[] = {"Sine", "Triangle", "Square", "Up Saw", "Down Saw", "Random+-", "Noise", "Random+"};
    return v >= 0 && v < 8 ? names[v] : "";
}

static uint32_t readBits(const Bytes& d, int lo, int bits) {
    uint32_t v = 0;
    for (int i = 0; i < bits; ++i) {
        const int b = lo + i;
        if (size_t(b / 8) < d.size() && ((d[size_t(b / 8)] >> (b % 8)) & 1)) v |= 1u << i;
    }
    return v;
}

static void writeBits(Bytes& d, int lo, int bits, uint32_t v) {
    for (int i = 0; i < bits; ++i) {
        const int b = lo + i;
        if (size_t(b / 8) >= d.size()) return;
        auto& byte = d[size_t(b / 8)];
        if ((v >> i) & 1) byte = uint8_t(byte | (1 << (b % 8)));
        else byte = uint8_t(byte & ~(1 << (b % 8)));
    }
}

std::optional<Program> Program::fromData(const Bytes& d) {
    if (d.size() < kProgramBytes) return std::nullopt;
    Program p;
    std::copy(d.begin(), d.begin() + long(kProgramBytes), p.data.begin());
    return p;
}

std::optional<Program> Program::fromDump(const Bytes& m) {
    const int op = opcodeOf(m);
    if ((op != UserProgramDump && op != EditProgramDump) || m.size() < 8 + packedSize(kProgramBytes)) return std::nullopt;
    return fromData(unpack7(&m[7], packedSize(kProgramBytes), kProgramBytes));
}

std::string Program::name() const { return decodeName(data, 8); }

void Program::setName(const std::string& n) {
    for (int k = 0; k < 10; ++k) {
        char c = k < int(n.size()) ? n[size_t(k)] : ' ';
        if (c < 32 || c > 126) c = ' ';
        writeBits(data, 8 + 7 * k, 7, uint32_t(c - 32));
    }
}

bool Program::isDrum(int s) const { return readBits(data, soundByteOffset(s) * 8, 1) != 0; }

bool Program::isEnabled(int s) const {
    // Keyboard sound: "Enable" at 84:3. Drum sound: "Sound enable" at 81:0.
    return readBits(data, soundByteOffset(s) * 8 + (isDrum(s) ? 81 * 8 : 84 * 8 + 3), 1) != 0;
}

int Program::raw(int s, const ParamDef& p, int drum) const { return int(readBits(data, bitBase(s, p, drum), p.bits)); }

void Program::setRaw(int s, const ParamDef& p, int r, int drum) {
    r = std::clamp(r, 0, (1 << p.bits) - 1);
    writeBits(data, bitBase(s, p, drum), p.bits, uint32_t(r));
}

int Program::editLimit(int s, const ParamDef& p, int drum) const {
    int lim = p.limit;
    if (std::string(p.name).find("lfo waveform") != std::string::npos) lim = std::max(lim, 7);  // manual lists 8 waves
    return std::min(std::max(lim, raw(s, p, drum)), (1 << p.bits) - 1);
}

void Program::setValue(int s, const ParamDef& p, int v, int drum) {
    setRaw(s, p, std::clamp(v - p.offset, 0, editLimit(s, p, drum)), drum);
}

static Bytes dump(uint8_t op, int number, const Bytes& data) {
    Bytes m{0xF0, 0x00, 0x00, 0x0E, kQsFamily, op, uint8_t(number & 0x7F)};
    const auto packed = pack7(data.data(), kProgramBytes);
    m.insert(m.end(), packed.begin(), packed.end());
    m.push_back(0xF7);
    return m;  // 408 bytes
}

Bytes Program::editDump() const { return dump(EditProgramDump, 0, data); }
Bytes Program::userDump(int n) const { return dump(UserProgramDump, n, data); }

Bytes paramEdit(int mode, int func, int sound, int page, int channel, int pot, int value) {
    const uint8_t v = uint8_t(value & 0xFF);  // 8-bit two's complement
    return {0xF0, 0x00, 0x00, 0x0E, kQsFamily, ParamEdit,
            uint8_t(((mode & 3) << 5) | (func & 0x1F)),
            uint8_t(((sound & 3) << 5) | (page & 0x1F)),
            uint8_t(((channel & 0xF) << 3) | ((pot & 3) << 1) | (v >> 7)),
            uint8_t(v & 0x7F), 0xF7};
}

Bytes paramEdit(const ParamDef& p, int sound, int value) { return paramEdit(2, p.func, sound, p.page, 0, p.pot, value); }

Bytes editProgramRequest(int n) { return {0xF0, 0x00, 0x00, 0x0E, kQsFamily, EditProgramRequest, uint8_t(n & 0x7F), 0xF7}; }
Bytes modeSelect(int mode) { return {0xF0, 0x00, 0x00, 0x0E, kQsFamily, ModeSelect, uint8_t(mode & 1), 0xF7}; }

}  // namespace qs
