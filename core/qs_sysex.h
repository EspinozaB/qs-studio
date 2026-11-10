// QS Studio - Alesis QS series SysEx protocol (QS6/7/8/QSR).
// Based on the Alesis "QS6,7,8,R MIDI SYSEX SPECIFICATION" (Feb 3 1997).
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace qs {

using Bytes = std::vector<uint8_t>;

// Alesis header: F0 00 00 0E 0E <opcode> ...
constexpr uint8_t kAlesisId[3] = {0x00, 0x00, 0x0E};
constexpr uint8_t kQsFamily = 0x0E;

enum Opcode : uint8_t {
    UserProgramDump = 0x00, UserProgramRequest = 0x01,
    EditProgramDump = 0x02, EditProgramRequest = 0x03,
    OldMixDump = 0x04, OldMixRequest = 0x05,
    UserEffectsDump = 0x06, UserEffectsRequest = 0x07,
    EditEffectsDump = 0x08, EditEffectsRequest = 0x09,
    GlobalDump = 0x0A, GlobalRequest = 0x0B,
    AllDumpRequest = 0x0C, ModeSelect = 0x0D,
    NewMixDump = 0x0E, NewMixRequest = 0x0F,
    ParamEdit = 0x10,
    SectorErase = 0x11, SectorWrite = 0x12, SectorRequest = 0x13,
    FlashAck = 0x14, FlashNack = 0x15,
};

// Sizes of unpacked data (bytes) and packed MIDI data (bytes).
constexpr size_t kProgramBytes = 350, kNewMixBytes = 138, kOldMixBytes = 123,
                 kEffectsBytes = 65, kGlobalBytes = 20, kFlashBlockBytes = 1024;

// The QS packs data as one continuous little-endian bit stream cut into 7-bit MIDI bytes.
size_t packedSize(size_t unpackedBytes);
Bytes pack7(const uint8_t* data, size_t n);
Bytes unpack7(const uint8_t* data, size_t n, size_t outBytes);

// Generic bitfield read using the spec's "hiByte:hiBit-loByte:loBit" addressing.
uint32_t getBits(const Bytes& buf, int hiByte, int hiBit, int loByte, int loBit);
// 10-character QS name stored as 7-bit chars (value + 32) starting at a bit offset.
std::string decodeName(const Bytes& buf, int startBit, int chars = 10);

// ---- Message builders ----
Bytes deviceInquiry();                                  // F0 7E 7F 06 01 F7
Bytes allDumpRequest();                                 // opcode 0C
Bytes mixRequest(int mix);                              // opcode 0F: 0-99 user mixes, 100 = mix edit buffer

// A mix from a New (0E) or Old (04) Mix Dump: its number (0-99, 100 = edit buffer), name and unpacked data.
struct MixInfo {
    int number = 0;
    std::string name;
    Bytes data;
    bool oldFormat = false;
};
std::optional<MixInfo> parseMixDump(const Bytes& msg);
Bytes sectorErase(int sector);                          // opcode 11
Bytes sectorRequest(int sector, int block);             // opcode 13
Bytes sectorWrite(int sector, int block, const uint8_t* data1024);  // opcode 12

// Checksum for sector writes: 7-bit sum of sector#, block# and the packed data.
// (Spec: "seven bit checksum of previous 1173 bytes". Verified by reading before writing.)
uint8_t flashChecksum(const uint8_t* bytes, size_t n);

// ---- Message parsing ----
struct DeviceInfo { int family = 0, member = 0; std::string model, version; };
std::optional<DeviceInfo> parseDeviceReply(const Bytes& msg);

bool isQsMessage(const Bytes& msg);
int opcodeOf(const Bytes& msg);  // -1 if not a QS message

struct SectorBlock { int sector = 0, block = 0; Bytes data; bool checksumOk = false; };
std::optional<SectorBlock> parseSectorWrite(const Bytes& msg);

std::string nackText(int code);

// Split a .syx file or raw buffer into individual F0..F7 messages.
std::vector<Bytes> splitSysex(const Bytes& raw);

// Human-readable summary of a QS dump (counts per type, first names).
struct DumpSummary {
    int programs = 0, editPrograms = 0, mixes = 0, oldMixes = 0, effects = 0, globals = 0, other = 0;
    std::vector<std::pair<int, std::string>> programNames, mixNames;
};
DumpSummary summarize(const std::vector<Bytes>& msgs);

// A complete Send All (opcode 0C): 128 programs + 100 mixes + 128 effects + 1 global.
constexpr int kFullBackupMessages = 357;
bool isCompleteBackup(const DumpSummary& s);

// Some QS units answer a card read (opcode 13) with their built-in sound ROM instead of the card.
// That block starts 00 80 00 00 and has "ROM" in its header text (observed by Project Q, github.com/brianpeat/project-q).
bool looksLikeInternalRom(const Bytes& block);

}  // namespace qs
