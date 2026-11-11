#include "qs_mix.h"

#include <algorithm>
#include <cstring>
#include <iterator>

namespace qs {

namespace {

// Common parameters: absolute bit positions in the 138 bytes.
const MixParam kCommon[] = {
    {"FX program change", "Effect", 0, 1, 0, 1, 5, 0, 2},
    {"FX MIDI channel", "Effect", 1, 4, 1, 15, 5, 0, 3},
};

// Per-channel parameters: bit positions within the channel's 8 bytes (channel 1 starts at byte 10).
const MixParam kChannel[] = {
    {"Program", "Program", 0, 7, 0, 127, 0, 0, 0},
    {"Bank", "Program", 7, 4, 0, 15, 0, 0, 2},
    {"Enable", "Level", 11, 1, 0, 1, 0, 0, 3},
    {"Volume", "Level", 12, 7, 0, 99, 1, 0, 0},
    {"Pan", "Level", 19, 3, 0, 7, 1, 0, 1},
    {"Output", "Level", 22, 2, 0, 3, 1, 0, 2},
    {"Effect level", "Level", 24, 7, 0, 100, 2, 0, 0},
    {"Effect bus", "Level", 31, 3, 0, 4, 2, 0, 1},
    {"Octave", "Pitch", 34, 3, -2, 4, 3, 0, 0},
    {"Semitone", "Pitch", 37, 5, -12, 24, 3, 0, 2},
    {"Low note", "Range", 42, 7, 0, 127, 4, 0, 0},
    {"High note", "Range", 49, 7, 0, 127, 4, 0, 1},
    {"MIDI in", "Keyboard/MIDI", 56, 1, 0, 1, 4, 1, 0},
    {"MIDI out", "Keyboard/MIDI", 57, 1, 0, 1, 4, 1, 2},
    {"Keyboard", "Keyboard/MIDI", 58, 1, 0, 1, 4, 1, 3},  // QSR: MIDI Group
    {"Wheels", "Controllers", 59, 1, 0, 1, 4, 2, 0},
    {"Aftertouch", "Controllers", 60, 1, 0, 1, 4, 2, 1},
    {"Sustain", "Controllers", 61, 1, 0, 1, 4, 2, 2},
    {"Controllers A-D, pedals", "Controllers", 62, 1, 0, 1, 4, 2, 3},
};

constexpr int kChannelBase = 10, kChannelBytes = 8, kNameBit = 5;

int channelBit(int ch, const MixParam& p) { return (kChannelBase + kChannelBytes * ch) * 8 + p.lowBit; }

}  // namespace

int mixCommonParamCount() { return int(std::size(kCommon)); }
const MixParam& mixCommonParam(int i) { return kCommon[i]; }
int mixChannelParamCount() { return int(std::size(kChannel)); }
const MixParam& mixChannelParam(int i) { return kChannel[i]; }

int findMixChannelParam(const std::string& name) {
    for (int i = 0; i < mixChannelParamCount(); ++i)
        if (name == kChannel[i].name) return i;
    return -1;
}

std::string mixValueText(const MixParam& p, int raw) {
    const std::string n = p.name;
    auto onOff = [&] { return std::string(raw ? "On" : "Off"); };
    if (p.bits == 1 && n != "FX program change") return onOff();
    if (n == "FX program change") return onOff();
    if (n == "FX MIDI channel") return "Ch " + std::to_string(raw + 1);
    if (n == "Pan") {
        // QS manuals: "<3 to 3>, or PROG"; stored 0-6 = <3 .. 3>, 7 = PROG.
        static const char* pans[] = {"<3", "<2", "<1", "<0>", "1>", "2>", "3>", "Prog"};
        return pans[std::clamp(raw, 0, 7)];
    }
    if (n == "Output") {
        static const char* outs[] = {"Main", "Aux", "Off", "Prog"};  // QS7/QS8 manual: L/R, Aux, Off, PROG (the QS6 has no Aux)
        return outs[std::clamp(raw, 0, 3)];
    }
    if (n == "Effect level") return raw >= 100 ? "Prog" : std::to_string(raw);
    if (n == "Effect bus") return raw >= 4 ? "Prog" : "Bus " + std::to_string(raw + 1);
    if (n == "Octave" || n == "Semitone") {
        const int v = raw + p.offset;
        return (v > 0 ? "+" : "") + std::to_string(v);
    }
    if (n == "Low note" || n == "High note") {
        static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        return std::string(names[raw % 12]) + std::to_string(raw / 12 - 2);
    }
    return {};
}

std::optional<Mix> Mix::fromDump(const Bytes& m) {
    if (opcodeOf(m) != NewMixDump || m.size() < 8 + packedSize(kNewMixBytes)) return std::nullopt;
    return fromData(unpack7(&m[7], packedSize(kNewMixBytes), kNewMixBytes));
}

Mix Mix::fromData(const Bytes& d) {
    Mix x;
    std::copy(d.begin(), d.begin() + long(std::min(d.size(), kNewMixBytes)), x.data.begin());
    return x;
}

std::string Mix::name() const { return decodeName(data, kNameBit); }

void Mix::setName(const std::string& n) {
    for (int k = 0; k < 10; ++k) {
        char c = k < int(n.size()) ? n[size_t(k)] : ' ';
        if (c < 32 || c > 126) c = ' ';
        writeField(data, kNameBit + 7 * k, 7, uint32_t(c - 32));
    }
}

int Mix::common(const MixParam& p) const { return int(readField(data, p.lowBit, p.bits)); }
void Mix::setCommon(const MixParam& p, int raw) { writeField(data, p.lowBit, p.bits, uint32_t(std::clamp(raw, 0, (1 << p.bits) - 1))); }
int Mix::channel(int ch, const MixParam& p) const { return int(readField(data, channelBit(ch, p), p.bits)); }
void Mix::setChannel(int ch, const MixParam& p, int raw) {
    writeField(data, channelBit(ch, p), p.bits, uint32_t(std::clamp(raw, 0, (1 << p.bits) - 1)));
}

int Mix::program(int ch) const { return channel(ch, kChannel[0]); }
int Mix::bank(int ch) const { return channel(ch, kChannel[1]); }
bool Mix::enabled(int ch) const { return channel(ch, kChannel[2]) != 0; }

Bytes Mix::dump(int number) const {
    Bytes m{0xF0, 0x00, 0x00, 0x0E, kQsFamily, NewMixDump, uint8_t(number & 0x7F)};
    const auto packed = pack7(data.data(), kNewMixBytes);
    m.insert(m.end(), packed.begin(), packed.end());
    m.push_back(0xF7);
    return m;
}

Bytes mixParamEdit(const MixParam& p, int ch, int value) {
    const uint8_t v = uint8_t(value & 0xFF);
    return {0xF0, 0x00, 0x00, 0x0E, kQsFamily, ParamEdit,
            uint8_t((1 << 5) | (p.func & 0x1F)),
            uint8_t(p.page & 0x1F),
            uint8_t(((ch & 0xF) << 3) | ((p.pot & 3) << 1) | (v >> 7)),
            uint8_t(v & 0x7F), 0xF7};
}

}  // namespace qs
