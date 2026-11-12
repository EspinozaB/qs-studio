// QS Studio - QS mixes (new format, 138 bytes): decode, edit and re-encode.
// Layout from the Alesis "QS6,7,8,R MIDI SYSEX SPECIFICATION", NEW MIX DATA FORMAT; value names from the QS6 and
// QS7/QS8 reference manuals, Chapter 5 (Editing Mixes).
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "qs_sysex.h"

namespace qs {

struct MixParam {
    const char* name;
    const char* group;   // the QS's Mix Edit function the parameter lives in
    int lowBit, bits;    // common params: absolute bit; channel params: bit within the channel's 8 bytes
    int offset, limit;   // displayed = raw + offset; raw 0..limit
    int func, page, pot; // opcode 10 addressing, mode 1 (Mix)
};

constexpr int kMixChannels = 16;

int mixCommonParamCount();
const MixParam& mixCommonParam(int i);
int mixChannelParamCount();
const MixParam& mixChannelParam(int i);
int findMixChannelParam(const std::string& name);  // -1 if not found

// Text for a channel value, from the manuals ("PROG" settings, pan positions, outputs). "" means show the number.
std::string mixValueText(const MixParam& p, int raw);

class Mix {
public:
    Mix() : data(kNewMixBytes, 0) {}
    static std::optional<Mix> fromDump(const Bytes& msg);  // opcode 0E (new format) only
    static Mix fromData(const Bytes& data138);

    std::string name() const;
    void setName(const std::string& n);

    int common(const MixParam& p) const;        // raw
    void setCommon(const MixParam& p, int raw);
    int channel(int ch, const MixParam& p) const;  // ch 0-15, raw
    void setChannel(int ch, const MixParam& p, int raw);

    // Convenience for the channel list.
    int program(int ch) const;
    int bank(int ch) const;        // program "type" field: 0 User, 1-3 Preset 1-3, 4 GenMIDI, 5+ card banks
    bool enabled(int ch) const;

    // A channel's 8 bytes, for copying or swapping channels.
    Bytes channelBlock(int ch) const;
    void setChannelBlock(int ch, const Bytes& block);

    Bytes dump(int number) const;  // opcode 0E; number 0-99 user mix, 100 = mix edit buffer
    const Bytes& bytes() const { return data; }

private:
    Bytes data;
};

// Opcode 10 for one mix parameter (displayed value, two's complement on the wire).
Bytes mixParamEdit(const MixParam& p, int channel, int value);

}  // namespace qs
