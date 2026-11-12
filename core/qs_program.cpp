#include "qs_program.h"

#include <algorithm>
#include <cmath>

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

Bytes Program::editDump(int edit) const { return dump(EditProgramDump, edit, data); }
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

std::optional<GlobalSettings> parseGlobalDump(const Bytes& m) {
    if (opcodeOf(m) != GlobalDump || m.size() < 8 + packedSize(20)) return std::nullopt;
    const auto g = unpack7(&m[7], packedSize(20), 20);
    GlobalSettings s;
    for (int i = 0; i < 4; ++i) s.controller[i] = g[size_t(7 + i)] & 0x7F;
    s.pedal[0] = g[11] & 0x7F;
    s.pedal[1] = g[12] & 0x7F;
    s.programSelect = g[13] & 0x1F;
    s.generalMidi = (g[17] & 1) != 0;
    return s;
}

Bytes globalRequest() { return {0xF0, 0x00, 0x00, 0x0E, kQsFamily, GlobalRequest, 0xF7}; }

Bytes editProgramRequest(int n) { return {0xF0, 0x00, 0x00, 0x0E, kQsFamily, EditProgramRequest, uint8_t(n & 0x7F), 0xF7}; }
Bytes modeSelect(int mode) { return {0xF0, 0x00, 0x00, 0x0E, kQsFamily, ModeSelect, uint8_t(mode & 1), 0xF7}; }

}  // namespace qs

namespace qs {

Bytes Program::sound(int s) const {
    const auto o = data.begin() + soundByteOffset(s);
    return Bytes(o, o + kSoundBytes);
}

void Program::setSound(int s, const Bytes& block) {
    if (block.size() != size_t(kSoundBytes)) return;
    std::copy(block.begin(), block.end(), data.begin() + soundByteOffset(s));
}

void Program::swapSounds(int a, int b) {
    const auto sa = sound(a), sb = sound(b);
    setSound(a, sb);
    setSound(b, sa);
}

void Program::initSound(int s, bool enabled) {
    std::fill(data.begin() + soundByteOffset(s), data.begin() + soundByteOffset(s) + kSoundBytes, uint8_t(0));
    for (int i = 0; i < keyboardParamCount(); ++i) {
        const auto& p = keyboardParam(i);
        if (p.name[0] != 0) setValue(s, p, 0);  // signed amounts centred, everything else at its first choice
    }
    auto set = [&](const char* name, int v) {
        const int i = findKeyboardParam(name);
        if (i >= 0) setValue(s, keyboardParam(i), v);
    };
    set("Sample number", 1);
    set("Volume", 99);
    set("Filter frequency", 99);
    set("Filter keyboard track", 0);
    set("High note limit", 127);
    set("Amp env attack", 0);
    set("Amp env decay", 0);
    set("Amp env sustain", 99);
    set("Amp env release", 10);
    set("Amp env level", 99);
    set("Pitch lfo speed", 50);
    set("Filter lfo speed", 50);
    set("Amp lfo speed", 50);
    for (int k = 0; k <= 10; ++k) set(("Tracking point " + std::to_string(k)).c_str(), k * 10);
    set("Enable", enabled ? 1 : 0);
}

Program Program::initProgram() {
    Program p;
    p.setName("Init Prog");
    for (int s = 0; s < kSoundCount; ++s) p.initSound(s, s == 0);
    return p;
}

bool isChoiceParam(const ParamDef& p) {
    const std::string n = p.name;
    for (const char* w : {"group", "number", "waveform", "mode", "source", "destination", "trig", "type", "gate", "Output",
                          "bus", "Enable", "sustain pedal", "Tracking input", "Portamento mode", "curve", "Overlap"})
        if (n.find(w) != std::string::npos) return true;
    return p.limit <= 3;
}

namespace {
template <typename F>
void forEachParam(const Program& ref, int s, F f) {
    if (ref.isDrum(s)) {
        for (int i = 0; i < drumParamCount(); ++i)
            for (int d = 0; d < kDrumsPerSound; ++d) f(drumParam(i), d);
    } else {
        for (int i = 0; i < keyboardParamCount(); ++i)
            if (keyboardParam(i).name[0] != 0) f(keyboardParam(i), 0);
    }
}
}  // namespace

Program blendPrograms(const Program& a, const Program& b, double t) {
    Program out = t < 0.5 ? a : b;
    for (int s = 0; s < kSoundCount; ++s) {
        if (a.isDrum(s) != b.isDrum(s)) continue;  // a drum sound and a keyboard sound don't blend: keep the nearer one
        forEachParam(a, s, [&](const ParamDef& p, int d) {
            if (isChoiceParam(p)) return;
            const double va = a.raw(s, p, d), vb = b.raw(s, p, d);
            out.setRaw(s, p, int(std::lround(va + (vb - va) * t)), d);
        });
    }
    return out;
}

Program randomizeProgram(const Program& src, double amount, bool includeChoices, uint32_t seed) {
    Program out = src;
    uint32_t x = seed ? seed : 0x9E3779B9u;
    auto rnd = [&] {  // xorshift: deterministic per seed, so a result can be reproduced
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return double(x) / 4294967296.0;
    };
    for (int s = 0; s < kSoundCount; ++s) {
        if (!src.isEnabled(s)) continue;
        forEachParam(src, s, [&](const ParamDef& p, int d) {
            const std::string n = p.name;
            if (n.find("Sample") != std::string::npos || n.find("Enable") != std::string::npos || n.find("mode (=") != std::string::npos ||
                n.find("note limit") != std::string::npos || n.find("Volume") != std::string::npos)
                return;  // keep the sound playable and recognisable
            const int lim = src.editLimit(s, p, d);
            if (isChoiceParam(p)) {
                if (includeChoices && rnd() < amount) out.setRaw(s, p, int(rnd() * (lim + 1)) % (lim + 1), d);
                return;
            }
            const int v = src.raw(s, p, d) + int(std::lround((rnd() * 2.0 - 1.0) * amount * lim));
            out.setRaw(s, p, std::clamp(v, 0, lim), d);
        });
    }
    return out;
}

}  // namespace qs
