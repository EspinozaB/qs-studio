#include "qs_sysex.h"

#include <map>

namespace qs {

size_t packedSize(size_t n) { return (n * 8 + 6) / 7; }

Bytes pack7(const uint8_t* data, size_t n) {
    Bytes out;
    out.reserve(packedSize(n));
    uint32_t acc = 0;
    int nb = 0;
    for (size_t i = 0; i < n; ++i) {
        acc |= uint32_t(data[i]) << nb;
        nb += 8;
        while (nb >= 7) {
            out.push_back(uint8_t(acc & 0x7F));
            acc >>= 7;
            nb -= 7;
        }
    }
    if (nb > 0) out.push_back(uint8_t(acc & 0x7F));
    return out;
}

Bytes unpack7(const uint8_t* data, size_t n, size_t outBytes) {
    Bytes out;
    out.reserve(outBytes);
    uint32_t acc = 0;
    int nb = 0;
    for (size_t i = 0; i < n && out.size() < outBytes; ++i) {
        acc |= uint32_t(data[i] & 0x7F) << nb;
        nb += 7;
        while (nb >= 8 && out.size() < outBytes) {
            out.push_back(uint8_t(acc & 0xFF));
            acc >>= 8;
            nb -= 8;
        }
    }
    out.resize(outBytes, 0);
    return out;
}

uint32_t getBits(const Bytes& buf, int hiByte, int hiBit, int loByte, int loBit) {
    const int lo = loByte * 8 + loBit, hi = hiByte * 8 + hiBit;
    uint32_t v = 0;
    for (int i = hi; i >= lo; --i) {
        const size_t b = size_t(i / 8);
        const int bit = b < buf.size() ? (buf[b] >> (i % 8)) & 1 : 0;
        v = (v << 1) | uint32_t(bit);
    }
    return v;
}

std::string decodeName(const Bytes& buf, int startBit, int chars) {
    std::string s;
    for (int k = 0; k < chars; ++k) {
        const int lo = startBit + 7 * k;
        const uint32_t v = getBits(buf, (lo + 6) / 8, (lo + 6) % 8, lo / 8, lo % 8);
        s += v < 95 ? char(v + 32) : '?';
    }
    return s;
}

static Bytes header(uint8_t op) { return {0xF0, 0x00, 0x00, 0x0E, kQsFamily, op}; }

Bytes deviceInquiry() { return {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7}; }

Bytes allDumpRequest() { auto m = header(AllDumpRequest); m.push_back(0xF7); return m; }

Bytes sectorErase(int sector) {
    auto m = header(SectorErase);
    m.push_back(uint8_t(sector & 0x7F));
    m.push_back(0xF7);
    return m;
}

Bytes sectorRequest(int sector, int block) {
    auto m = header(SectorRequest);
    m.push_back(uint8_t(sector & 0x7F));
    m.push_back(uint8_t(block & 0x7F));
    m.push_back(0xF7);
    return m;
}

uint8_t flashChecksum(const uint8_t* bytes, size_t n) {
    uint32_t s = 0;
    for (size_t i = 0; i < n; ++i) s += bytes[i];
    return uint8_t(s & 0x7F);
}

Bytes sectorWrite(int sector, int block, const uint8_t* data1024) {
    auto m = header(SectorWrite);
    const size_t bodyStart = m.size();
    m.push_back(uint8_t(sector & 0x7F));
    m.push_back(uint8_t(block & 0x7F));
    const Bytes packed = pack7(data1024, kFlashBlockBytes);  // 1171 bytes
    m.insert(m.end(), packed.begin(), packed.end());
    m.push_back(flashChecksum(m.data() + bodyStart, m.size() - bodyStart));
    m.push_back(0xF7);
    return m;  // 1181 bytes total
}

bool isQsMessage(const Bytes& m) {
    return m.size() >= 7 && m[0] == 0xF0 && m[1] == 0x00 && m[2] == 0x00 && m[3] == 0x0E && m[4] == kQsFamily;
}

int opcodeOf(const Bytes& m) { return isQsMessage(m) ? m[5] : -1; }

std::optional<DeviceInfo> parseDeviceReply(const Bytes& m) {
    // F0 7E <ch> 06 02 00 00 0E <famLo> <famHi> <memLo> <memHi> v v v v F7
    if (m.size() < 15 || m[0] != 0xF0 || m[1] != 0x7E || m[3] != 0x06 || m[4] != 0x02) return std::nullopt;
    if (m[5] != 0x00 || m[6] != 0x00 || m[7] != 0x0E) return std::nullopt;
    DeviceInfo d;
    d.family = m[8] | (m[9] << 7);
    d.member = m[10] | (m[11] << 7);
    static const std::map<int, std::string> names = {{1, "QuadraSynth Plus Piano"}, {3, "QS6"}, {4, "QS8"}, {5, "QS7"}, {6, "QSR"}, {13, "QS6.1"}, {15, "QS8.1"}};
    auto it = names.find(d.member);
    d.model = d.family == 0x0E ? (it != names.end() ? it->second : "QS (member " + std::to_string(d.member) + ")")
                               : "Alesis device family " + std::to_string(d.family);
    std::string v;
    for (int i = 12; i < 16 && i < int(m.size()) - 1; ++i) v += char(m[i]);
    d.version = v.size() == 4 ? v.substr(0, 2) + "." + v.substr(2) : v;
    if (!d.version.empty() && d.version[0] == '0') d.version.erase(0, 1);
    return d;
}

std::optional<SectorBlock> parseSectorWrite(const Bytes& m) {
    if (opcodeOf(m) != SectorWrite) return std::nullopt;
    const size_t packed = packedSize(kFlashBlockBytes);
    if (m.size() < 6 + 2 + packed + 2) return std::nullopt;
    SectorBlock sb;
    sb.sector = m[6];
    sb.block = m[7];
    sb.data = unpack7(&m[8], packed, kFlashBlockBytes);
    const uint8_t sum = m[8 + packed];
    sb.checksumOk = flashChecksum(&m[6], 2 + packed) == sum;
    return sb;
}

std::string nackText(int code) {
    switch (code) {
        case 0: return "No card present, or not a FLASH card";
        case 1: return "Card is write protected";
        case 2: return "Erase failed (FLASH chip timeout)";
        case 3: return "Checksum didn't match";
        case 4: return "Programming failed (block not erased first?)";
        default: return "Unknown error " + std::to_string(code);
    }
}

std::vector<Bytes> splitSysex(const Bytes& raw) {
    std::vector<Bytes> out;
    size_t i = 0;
    while (i < raw.size()) {
        if (raw[i] != 0xF0) { ++i; continue; }
        size_t j = i + 1;
        while (j < raw.size() && raw[j] != 0xF7) ++j;
        if (j >= raw.size()) break;
        out.emplace_back(raw.begin() + long(i), raw.begin() + long(j) + 1);
        i = j + 1;
    }
    return out;
}

DumpSummary summarize(const std::vector<Bytes>& msgs) {
    DumpSummary s;
    for (const auto& m : msgs) {
        switch (opcodeOf(m)) {
            case UserProgramDump: {
                ++s.programs;
                if (m.size() >= 8 + packedSize(kProgramBytes)) {
                    auto p = unpack7(&m[7], packedSize(kProgramBytes), kProgramBytes);
                    s.programNames.emplace_back(m[6], decodeName(p, 8));
                }
                break;
            }
            case EditProgramDump: ++s.editPrograms; break;
            case NewMixDump: {
                ++s.mixes;
                if (m.size() >= 8 + packedSize(kNewMixBytes)) {
                    auto p = unpack7(&m[7], packedSize(kNewMixBytes), kNewMixBytes);
                    s.mixNames.emplace_back(m[6], decodeName(p, 5));
                }
                break;
            }
            case OldMixDump: ++s.oldMixes; break;
            case UserEffectsDump: case EditEffectsDump: ++s.effects; break;
            case GlobalDump: ++s.globals; break;
            default: ++s.other;
        }
    }
    return s;
}

bool isCompleteBackup(const DumpSummary& s) {
    return s.programs == 128 && s.mixes + s.oldMixes == 100 && s.effects == 128 && s.globals == 1;
}

bool looksLikeInternalRom(const Bytes& b) {
    if (b.size() < 32 || b[0] != 0x00 || b[1] != 0x80 || b[2] != 0x00 || b[3] != 0x00) return false;
    const std::string head(b.begin(), b.begin() + 32);
    return head.find("ROM") != std::string::npos;
}

}  // namespace qs
