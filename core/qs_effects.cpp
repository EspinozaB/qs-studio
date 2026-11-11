#include "qs_effects.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>
#include <vector>

namespace qs {

#include "qs_effects_table.inc"

namespace {
constexpr int kConfigBit = 70, kConfigBits = 4;  // "Effect configuration": 9:1-8:6

bool has(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }
}  // namespace

const EffectParam& effectConfigParam() {
    static const EffectParam p{"Configuration", "Effect configuration", -1, 2, 0, 0, 0, 4, kConfigBit, kConfigBits, -1, 0};
    return p;
}

const EffectConfigDef& effectConfig(int c) { return kConfigs[std::clamp(c, 0, kEffectConfigs - 1)]; }

const char* effectConfigName(int c) {
    // The QS6 manual numbers them 1-5 with these names (Chapter 7, Configurations).
    static const char* names[] = {"1 Reverb", "2 Reverbs", "Lezlie and Reverb", "Reverb and EQ", "Overdrive and Lezlie"};
    return names[std::clamp(c, 0, kEffectConfigs - 1)];
}

std::string effectShortName(const EffectParam& p) {
    std::string n = p.name;
    if (n.size() > 7 && n.compare(0, 5, "Send ") == 0 && n[6] == ' ') n = n.substr(7);
    if (!n.empty()) n[0] = char(std::toupper(static_cast<unsigned char>(n[0])));
    return n;
}

// Value names follow the QS's own display text (the strings in its OS ROM, written out in full here) and the manual.
namespace {
std::string pick(std::initializer_list<const char*> names, int raw) {
    if (raw < 0 || raw >= int(names.size())) return std::to_string(raw);
    return *(names.begin() + raw);
}

// Modulation destinations per configuration, in the QS's order.
const std::vector<std::string>& modDestinations(int config) {
    static const std::vector<std::string> fourSends = {
        "Pitch 1 speed", "Pitch 1 depth", "Pitch 1 level", "Delay 1 input balance", "Delay 1 time", "Delay 1 feedback", "Delay 1 level",
        "Reverb 1 balance", "Reverb 1 input", "Pitch 2 speed", "Pitch 2 depth", "Pitch 2 level", "Delay 2 input balance", "Delay 2 time",
        "Delay 2 feedback", "Delay 2 level", "Reverb 2 balance", "Reverb 2 input", "Pitch 3 speed", "Pitch 3 depth", "Pitch 3 level",
        "Delay 3 input balance", "Delay 3 time", "Delay 3 feedback", "Delay 3 level", "Reverb 3 balance", "Reverb 3 input",
        "Delay 4 time", "Delay 4 feedback", "Delay 4 level", "Reverb 4 balance", "Reverb 4 input", "Reverb decay",
        "Reverb low decay", "Reverb high decay", "Reverb diffusion", "Reverb level"};
    static const std::vector<std::string> twoReverbs = {
        "Delay 1 time", "Delay 1 feedback", "Delay 1 level", "Pitch 1 delay input", "Pitch 1 speed", "Pitch 1 depth", "Pitch 1 level",
        "Reverb 1 chorus input", "Reverb 1 decay", "Reverb 1 low decay", "Reverb 1 high decay", "Reverb 1 diffusion", "Reverb 1 level",
        "Pitch 3 speed", "Pitch 3 depth", "Reverb 3 chorus input", "Reverb 3 decay", "Reverb 3 low decay", "Reverb 3 high decay",
        "Reverb 3 diffusion", "Reverb 3 level"};
    static std::vector<std::string> lezlie;
    if (lezlie.empty()) {
        lezlie = fourSends;
        lezlie[1] = "Lezlie 1 motor";
        lezlie[0] = "Lezlie 1 speed";
        lezlie[2] = "Lezlie 1 level";
        lezlie[3] = "Delay 1 input";
    }
    static const std::vector<std::string> overdrive = {
        "Overdrive threshold", "Overdrive brightness", "Overdrive balance", "Overdrive level", "Pitch 1 input balance", "Pitch 1 speed",
        "Pitch 1 depth", "Pitch 1 level", "Delay 1 input balance", "Delay 1 time", "Delay 1 feedback", "Delay 1 level",
        "Reverb input balance", "Reverb input", "Reverb decay", "Reverb low decay", "Reverb high decay", "Reverb diffusion",
        "Reverb level", "Lezlie input balance", "Lezlie level", "Lezlie speed", "Lezlie motor"};
    static const std::vector<std::string> none;
    switch (config) {
        case 0: case 3: return fourSends;
        case 1: return twoReverbs;
        case 2: return lezlie;
        case 4: return overdrive;
        default: return none;
    }
}
}  // namespace

std::string effectValueText(const EffectParam& p, int raw, int config) {
    const std::string n = p.name;
    if (has(n, "pitch type")) {
        if (p.limit == 5) return pick({"Mono chorus", "Stereo chorus", "Mono flange", "Stereo flange", "Detuner", "Resonator"}, raw);
        if (p.limit == 2) return pick({"Mono chorus", "Mono flange", "Resonator"}, raw);
        if (p.limit == 1) return pick({"Mono chorus", "Stereo chorus"}, raw);
    }
    if (has(n, "reverb input 1") && p.limit == 1) return pick({"Pitch out", "Delay out"}, raw);
    if (has(n, "reverb input 2") && p.limit == 2) return pick({"Send in", "Pitch out", "Delay in"}, raw);
    if (has(n, "reverb input 2") && p.limit == 5) return pick({"Send 1", "Send 2", "Send 3", "Send 4", "Overdrive", "Delay in"}, raw);
    if (has(n, "pitch input 2") && p.limit == 3) return pick({"Send 1", "Send 2", "Send 3", "Send 4"}, raw);
    if (has(n, "delay input 2") && p.limit == 5) return pick({"Send 1", "Send 2", "Send 3", "Send 4", "Overdrive", "Pitch in"}, raw);
    if (has(n, "lezlie input 1") && p.limit == 1) return pick({"Reverb", "Delay"}, raw);
    if (has(n, "lezlie input 2") && p.limit == 8)
        return pick({"Send 1", "Send 2", "Send 3", "Send 4", "Overdrive", "Pitch in", "Pitch out", "Delay in", "Reverb in"}, raw);
    if (has(n, "MIDImod destination")) {
        const auto& d = modDestinations(config);
        return raw < int(d.size()) ? d[size_t(raw)] : std::to_string(raw);
    }
    if (has(n, "delay type")) {
        static const char* t[] = {"Mono delay", "Stereo delay", "Ping-pong delay"};
        return t[std::clamp(raw, 0, 2)];
    }
    if (n == "Reverb type") {
        static const char* t[] = {"Plate 1", "Plate 2", "Room", "Hall", "Large", "Gate", "Reverse"};
        return raw <= 6 ? t[raw] : std::to_string(raw);
    }
    if (has(n, "pitch shape")) return raw ? "Square" : "Sine";
    if (has(n, "lezlie speed")) return raw ? "Fast" : "Slow";
    if (has(n, "lezlie motor")) return raw ? "On" : "Off";
    if (has(n, "lezlie horn")) {
        const int v = raw >= 64 ? raw - 128 : raw;  // 7-bit two's complement (spec footnote)
        return (v > 0 ? "+" : "") + std::to_string(v) + " dB";
    }
    if (n == "Low EQ Frequency") return std::to_string(30 + 30 * raw) + " Hz";
    if (n == "Hi EQ Frequency") return std::to_string(3 + raw) + " kHz";
    if (has(n, "EQ Gain")) return "+" + std::to_string(raw) + " dB";
    if (has(n, "MIDImod source")) {
        static const char* t[] = {"Aftertouch", "Mod wheel", "Pitch wheel", "MIDI volume", "Sustain pedal", "Pedal 1",
                                  "Pedal 2", "Controller A", "Controller B", "Controller C", "Controller D"};
        return raw <= 10 ? t[raw] : std::to_string(raw);
    }
    if (has(n, "1ms") || has(n, "10ms")) return std::to_string(raw * (has(n, "10ms") ? 10 : 1)) + " ms";
    if (p.offset == -99 && p.limit == 198) {  // balances and levels shown as <99 .. <0> .. 99>
        const int v = raw - 99;
        return v < 0 ? "<" + std::to_string(-v) : v > 0 ? std::to_string(v) + ">" : "<0>";
    }
    return {};
}

std::optional<Effects> Effects::fromDump(const Bytes& m) {
    const int op = opcodeOf(m);
    if ((op != UserEffectsDump && op != EditEffectsDump) || m.size() < 8 + packedSize(kEffectsBytes)) return std::nullopt;
    return fromData(unpack7(&m[7], packedSize(kEffectsBytes), kEffectsBytes));
}

Effects Effects::fromData(const Bytes& d) {
    Effects e;
    std::copy(d.begin(), d.begin() + long(std::min(d.size(), kEffectsBytes)), e.data.begin());
    return e;
}

int Effects::config() const { return std::clamp(int(readField(data, kConfigBit, kConfigBits)), 0, kEffectConfigs - 1); }
void Effects::setConfig(int c) { writeField(data, kConfigBit, kConfigBits, uint32_t(std::clamp(c, 0, kEffectConfigs - 1))); }

int Effects::raw(const EffectParam& p) const { return int(readField(data, p.lowBit, p.bits)); }
void Effects::setRaw(const EffectParam& p, int r) { writeField(data, p.lowBit, p.bits, uint32_t(std::clamp(r, 0, (1 << p.bits) - 1))); }

bool Effects::visible(int c, int index) const {
    const auto& cfg = effectConfig(c);
    if (index < 0 || index >= cfg.count) return false;
    const auto& p = cfg.params[index];
    if (p.condIndex < 0) return true;
    const int type = raw(cfg.params[p.condIndex]);
    return type < 31 && ((p.condMask >> type) & 1) != 0;
}

static Bytes fxDump(uint8_t op, int n, const Bytes& data) {
    Bytes m{0xF0, 0x00, 0x00, 0x0E, kQsFamily, op, uint8_t(n & 0x7F)};
    const auto packed = pack7(data.data(), kEffectsBytes);
    m.insert(m.end(), packed.begin(), packed.end());
    m.push_back(0xF7);
    return m;
}

Bytes Effects::userDump(int n) const { return fxDump(UserEffectsDump, n, data); }
Bytes Effects::editDump(int edit) const { return fxDump(EditEffectsDump, edit, data); }

Bytes effectsRequest(int n) { return {0xF0, 0x00, 0x00, 0x0E, kQsFamily, UserEffectsRequest, uint8_t(n & 0x7F), 0xF7}; }
Bytes editEffectsRequest(int edit) { return {0xF0, 0x00, 0x00, 0x0E, kQsFamily, EditEffectsRequest, uint8_t(edit & 1), 0xF7}; }

Bytes effectParamEdit(const EffectParam& p, int value) {
    const uint8_t v = uint8_t(value & 0xFF);
    return {0xF0, 0x00, 0x00, 0x0E, kQsFamily, ParamEdit,
            uint8_t((3 << 5) | (p.func & 0x1F)),
            uint8_t(((std::max(0, p.send) & 3) << 5) | (p.page & 0x1F)),
            uint8_t(((p.pot & 3) << 1) | (v >> 7)),
            uint8_t(v & 0x7F), 0xF7};
}

}  // namespace qs
