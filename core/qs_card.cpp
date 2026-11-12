#include "qs_card.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <tuple>

namespace qs {

static constexpr size_t kHeader = 0x10000, kFirstVoice = 0x1006C;
// Program bank layout differs by card generation:
//  Z-series QCards (marker A5 C3 5A A5): 128 programs x 349 bytes (program byte 0 omitted), 100 new mixes x 138
//  R-series QuadraCards (marker 5A A5 A5 C3): 128 programs x 350 bytes, 100 old (pre-2.00) mixes x 123
static constexpr size_t kProgBase = 4;
static bool isOldFormat(const Bytes& d) { return d.size() > 4 && d[0] == 0x5A && d[1] == 0xA5; }
static size_t progStride(const Bytes& d) { return isOldFormat(d) ? 350 : 349; }
static size_t mixStride(const Bytes& d) { return isOldFormat(d) ? kOldMixBytes : kNewMixBytes; }
static size_t mixBase(const Bytes& d) { return kProgBase + 128 * progStride(d); }
static constexpr int kSplitOffset = 12;

bool looksLikeCard(const Bytes& d) {
    if (d.size() < 4) return false;
    return (d[0] == 0xA5 && d[1] == 0xC3 && d[2] == 0x5A && d[3] == 0xA5) ||
           (d[0] == 0x5A && d[1] == 0xA5 && d[2] == 0xA5 && d[3] == 0xC3);
}

int16_t CardImage::sampleAt(uint32_t w) const {
    const size_t b = size_t(w) * 2;
    if (b + 1 >= data.size()) return 0;
    return samplesSwapped ? int16_t(uint16_t(data[b + 1] << 8 | data[b])) : int16_t(uint16_t(data[b] << 8 | data[b + 1]));
}

void checkSampleByteOrder(CardImage& c) {
    c.samplesSwapped = false;
    double be = 0, le = 0;
    int zones = 0;
    for (const auto& v : c.voices)
        for (const auto& z : v.zones) {
            if (zones >= 200 || z.end <= z.start + 16 || !c.zoneAvailable(z)) continue;
            const size_t from = size_t(c.zoneWordBase(z) + z.start) * 2, n = std::min<size_t>(z.end - z.start, 2000);
            if (from + n * 2 + 2 > c.data.size()) continue;
            for (size_t i = 1; i < n; ++i) {
                const uint8_t* p = &c.data[from + i * 2];
                const uint8_t* q = p - 2;
                be += std::abs(int(int16_t(uint16_t(p[0] << 8 | p[1]))) - int(int16_t(uint16_t(q[0] << 8 | q[1]))));
                le += std::abs(int(int16_t(uint16_t(p[1] << 8 | p[0]))) - int(int16_t(uint16_t(q[1] << 8 | q[0]))));
            }
            ++zones;
        }
    if (zones == 0) {
        c.sampleCheck = "no samples to check";
        return;
    }
    c.samplesSwapped = le * 1.5 < be;
    // Neither order smooth: the addresses don't point at audio (an unknown card layout), so don't play it at full blast.
    c.samplesLookWrong = std::min(be, le) / zones / 1999.0 > 8000.0;
    char buf[200];
    std::snprintf(buf, sizeof buf, "sample byte order: %s (average step %.0f big-endian, %.0f little-endian, %d zones)",
                  c.samplesSwapped ? "little-endian, swapped" : "big-endian", be / zones / 1999.0, le / zones / 1999.0, zones);
    if (c.samplesLookWrong) c.sampleCheck += "; neither order looks like audio";
    c.sampleCheck = buf;
}

Bytes CardImage::program(int i) const {
    Bytes p(kProgramBytes, 0);
    const size_t st = progStride(data), o = kProgBase + size_t(i) * st;
    if (o + st <= data.size()) std::memcpy(&p[kProgramBytes - st], &data[o], st);
    return p;
}

Bytes CardImage::mix(int i) const {
    const size_t st = mixStride(data), o = mixBase(data) + size_t(i) * st;
    Bytes m(st, 0);
    if (o + st <= data.size()) std::memcpy(m.data(), &data[o], st);
    return m;
}

size_t CardImage::uniqueSampleCount() const {
    std::set<std::pair<uint32_t, uint32_t>> u;
    for (const auto& v : voices)
        for (const auto& z : v.zones) u.insert({z.start, z.end});
    return u.size();
}

static bool printable(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (p[i] < 32 || p[i] > 126) return false;
    return true;
}

static void parseVoices(CardImage& c, size_t o = kFirstVoice) {
    const Bytes& d = c.data;
    while (o + 16 <= d.size()) {
        Voice v;
        v.offset = o;
        v.group = d[o];
        v.index = d[o + 1];
        if (v.index == 0 || !printable(&d[o + 2], 10)) break;
        v.name.assign(reinterpret_cast<const char*>(&d[o + 2]), 10);
        const int nsp = d[o + 12];
        v.layers = d[o + 13] + 1;
        v.velSplit1 = d[o + 14];
        v.velSplit2 = d[o + 15];
        if (v.layers > 3) break;
        size_t p = o + 16;
        if (p + size_t(nsp) + 2 > d.size()) break;
        v.splits.assign(d.begin() + long(p), d.begin() + long(p) + nsp);
        p += size_t(nsp);
        v.topKey = d[p++];
        if ((p - o) % 2) ++p;
        const size_t nz = size_t(nsp + 1) * size_t(v.layers);
        if (p + nz * 12 > d.size()) break;
        for (size_t k = 0; k < nz; ++k, p += 12) {
            const uint8_t* z = &d[p];
            Zone zz;
            zz.root = z[0];
            zz.finetune = z[1];
            zz.unk2 = z[2];
            zz.level = z[3];
            zz.loopOffset = uint32_t(z[4] << 8 | z[5]);
            zz.end = uint32_t((z[8] & 0x3F) << 16 | z[6] << 8 | z[7]);
            zz.start = uint32_t((z[9] & 0x3F) << 16 | z[10] << 8 | z[11]);
            zz.flagsA = z[8] >> 6;
            zz.flagsB = z[9] >> 6;
            v.zones.push_back(zz);
        }
        c.voices.push_back(std::move(v));
        o = p;
    }
}

bool loadInternalRom(const Bytes& raw, CardImage& c, std::string& error) {
    c = CardImage{};
    c.data = raw;
    if (!looksLikeInternalRom(c.data)) { error = "Not a QS internal sample ROM dump (no ROM header)"; return false; }
    // Header: 00 80 00 00 04 'F' 'QS78ROM v20220' 00, then the voice directory in the card format.
    c.cardName.assign(reinterpret_cast<const char*>(&c.data[5]), 15);
    while (!c.cardName.empty() && (c.cardName.back() == ' ' || c.cardName.back() == 0)) c.cardName.pop_back();
    c.isRom = true;
    parseVoices(c, 0x14);
    if (c.voices.empty()) { error = "The ROM header is there but no voices were found"; return false; }
    checkSampleByteOrder(c);
    return true;
}

bool loadCardImage(const Bytes& raw, CardImage& c, std::string& error) {
    c = CardImage{};
    c.data = raw;
    Bytes& d = c.data;
    if (d.size() % 2) { d.pop_back(); c.fixes.push_back("dropped 1 trailing byte (swap.exe bug)"); }
    if (d.size() > kHeader + 4 && d[kHeader] == 0x70 && d[kHeader + 1] == 0x00 && d[kHeader + 2] == 0x1C) {
        for (size_t i = 0; i + 1 < d.size(); i += 2) std::swap(d[i], d[i + 1]);
        c.fixes.push_back("byte-swapped (little-endian dump)");
    }
    if (!looksLikeCard(d)) { error = "Not a QS card image (card marker not found)"; return false; }

    if (mixBase(d) + 100 * mixStride(d) <= d.size()) {
        const int mixNameBit = isOldFormat(d) ? 13 : 5;
        for (int i = 0; i < 128; ++i) c.programNames.push_back(decodeName(c.program(i), 8));
        for (int i = 0; i < 100; ++i) c.mixNames.push_back(decodeName(c.mix(i), mixNameBit));
    }
    const bool hasSampleHeader = d.size() > kFirstVoice && d[kHeader] == 0x00 && d[kHeader + 1] == 0x70 &&
                                 d[kHeader + 2] == 0x38 && d[kHeader + 3] == 0x1C;
    if (hasSampleHeader) {
        c.cardName.assign(reinterpret_cast<const char*>(&d[kHeader + 10]), 14);
        while (!c.cardName.empty() && c.cardName.back() == ' ') c.cardName.pop_back();
        for (size_t i = kHeader + 24; i + 6 <= kFirstVoice; i += 6) {
            std::string g(reinterpret_cast<const char*>(&d[i]), 6);
            if (g.find_first_not_of(' ') != std::string::npos && printable(&d[i], 6)) c.groups.push_back(g);
        }
        parseVoices(c);
        checkSampleByteOrder(c);
    } else {
        c.cardName = "(program-only card)";
    }
    return true;
}

static void put16(Bytes& b, uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
static void put32(Bytes& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); }
static void putTag(Bytes& b, const char* t) { b.insert(b.end(), t, t + 4); }

Bytes makeWav(const int16_t* pcm, size_t n, int rate, int root, long loopStart, long loopEnd) {
    Bytes fmt, smpl, out;
    put16(fmt, 1); put16(fmt, 1); put32(fmt, uint32_t(rate)); put32(fmt, uint32_t(rate) * 2); put16(fmt, 2); put16(fmt, 16);
    const bool loop = loopStart >= 0 && loopEnd > loopStart;
    for (uint32_t v : {0u, 0u, uint32_t(1000000000 / rate), uint32_t(root), 0u, 0u, 0u, loop ? 1u : 0u, 0u}) put32(smpl, v);
    if (loop) for (uint32_t v : {0u, 0u, uint32_t(loopStart), uint32_t(loopEnd), 0u, 0u}) put32(smpl, v);
    const uint32_t dataBytes = uint32_t(n * 2);
    putTag(out, "RIFF"); put32(out, uint32_t(4 + 8 + fmt.size() + 8 + smpl.size() + 8 + dataBytes)); putTag(out, "WAVE");
    putTag(out, "fmt "); put32(out, uint32_t(fmt.size())); out.insert(out.end(), fmt.begin(), fmt.end());
    putTag(out, "smpl"); put32(out, uint32_t(smpl.size())); out.insert(out.end(), smpl.begin(), smpl.end());
    putTag(out, "data"); put32(out, dataBytes);
    for (size_t i = 0; i < n; ++i) put16(out, uint16_t(pcm[i]));
    return out;
}

std::string safeName(const std::string& s) {
    std::string o;
    for (char ch : s) {
        const bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '+';
        if (ok) o += ch;
        else if (!o.empty() && o.back() != '_') o += '_';
    }
    while (!o.empty() && o.back() == '_') o.pop_back();
    return o.empty() ? "x" : o;
}

static char hexd(int v) { return "0123456789abcdef"[v & 15]; }

static std::string sampleFileName(const Voice& v, size_t zi, const Zone& z) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "_%03d_", v.index);
    std::string zs = std::to_string(zi);
    if (zs.size() < 2) zs = "0" + zs;
    return std::string("g") + hexd(v.group >> 4) + hexd(v.group) + buf + safeName(v.name) + "_z" + zs + "_n" +
           std::to_string(z.root) + ".wav";
}

using Key = std::tuple<uint32_t, uint32_t, uint32_t>;

static std::map<Key, std::string> sampleNames(const CardImage& c) {
    std::map<Key, std::string> names;
    for (const auto& v : c.voices)
        for (size_t i = 0; i < v.zones.size(); ++i) {
            const auto& z = v.zones[i];
            if (z.end <= z.start || !c.zoneAvailable(z)) continue;
            const uint32_t base = c.zoneWordBase(z);
            Key k{base + z.start, base + z.end, z.loopOffset};
            if (!names.count(k)) names[k] = sampleFileName(v, i, z);
        }
    return names;
}

std::vector<ExportedSample> exportSamples(const CardImage& c) {
    std::vector<ExportedSample> out;
    for (const auto& [k, name] : sampleNames(c)) {
        const auto [start, end, loop] = k;
        const size_t n = size_t(end - start + 1);
        std::vector<int16_t> pcm(n);
        for (size_t i = 0; i < n; ++i) pcm[i] = c.sampleAt(start + uint32_t(i));
        int root = 60;
        for (const auto& v : c.voices)
            for (const auto& z : v.zones)
                if (c.zoneWordBase(z) + z.start == start && c.zoneWordBase(z) + z.end == end) root = z.root;
        // The last word repeats the loop-start word, so the loop ends one word early.
        // Every QS sample loops from its loop offset to the end; offset 0 (single-cycle waves) loops the whole sample.
        const bool loops = long(loop) + 2 < long(n);
        out.push_back({name, makeWav(pcm.data(), n, 48000, root, loops ? long(loop) : -1, loops ? long(n) - 2 : -1)});
    }
    return out;
}

std::string voiceGroupName(const CardImage& c, int g) {
    static const char* keys[] = {"Piano", "Chromatic", "Organ", "Guitar", "Bass", "String", "Brass", "Woodwind",
                                 "Synth", "Wave", "Noise", "Voice", "Ethnic", "Drums", "Percussion", "Sound FX"};
    static const char* drums[] = {"Kick", "Snare", "Toms", "Cymbal", "Percussion", "Sound FX", "Wave"};
    if (g >= 0 && g < 16) return keys[g];
    if (!c.isRom && g >= 0 && g < int(c.groups.size())) {
        std::string n = c.groups[size_t(g)];
        while (!n.empty() && n.back() == ' ') n.pop_back();
        if (!n.empty()) return n;
    }
    if (g >= 16 && g < 24) return "Card " + std::to_string(g - 15);
    if (g == 24) return "Rhythm";
    if (g >= 48 && g < 55) return std::string("Drum ") + drums[g - 48];
    if (g >= 55 && g < 61) return "Card drums " + std::to_string(g - 54);
    if (g == 61) return "Drum rhythm";
    return "Group " + std::to_string(g);
}

std::vector<Region> voiceRegions(const Voice& v) {
    std::vector<Region> out;
    const int nz = int(v.splits.size()) + 1;
    std::vector<int> vb{0};
    if (v.layers > 1 && v.velSplit1 < 128) vb.push_back(v.velSplit1);
    if (v.layers > 2 && v.velSplit2 < 128) vb.push_back(v.velSplit2);
    vb.push_back(128);
    for (int L = 0; L < v.layers; ++L)
        for (int k = 0; k < nz; ++k) {
            const size_t zi = size_t(k * v.layers + L);
            if (zi >= v.zones.size()) continue;
            Region r;
            r.zone = &v.zones[zi];
            r.lo = k == 0 ? 0 : std::min(127, v.splits[size_t(k - 1)] + 1 + kSplitOffset);
            r.hi = k == nz - 1 ? 127 : std::min(127, v.splits[size_t(k)] + kSplitOffset);
            if (r.lo > r.hi) continue;
            if (v.layers > 1 && L + 1 < int(vb.size())) {
                r.lovel = std::max(1, vb[size_t(L)]);
                r.hivel = vb[size_t(L) + 1] - 1;
            }
            out.push_back(r);
        }
    return out;
}

std::vector<std::pair<std::string, std::string>> exportSfz(const CardImage& c) {
    std::vector<std::pair<std::string, std::string>> out;
    const auto names = sampleNames(c);
    for (const auto& v : c.voices) {
        const int nz = int(v.splits.size()) + 1;
        std::vector<int> vb{0};
        if (v.layers > 1 && v.velSplit1 < 128) vb.push_back(v.velSplit1);
        if (v.layers > 2 && v.velSplit2 < 128) vb.push_back(v.velSplit2);
        vb.push_back(128);
        std::string t = "// " + c.cardName + " / " + v.name + "\n// raw voice map only: no QS program, envelopes or effects\n"
                        "<control> default_path=samples/\n<global> ampeg_release=0.3\n";
        for (int L = 0; L < v.layers; ++L)
            for (int k = 0; k < nz; ++k) {
                const size_t zi = size_t(k * v.layers + L);
                if (zi >= v.zones.size()) continue;
                const auto& z = v.zones[zi];
                auto it = names.find(Key{c.zoneWordBase(z) + z.start, c.zoneWordBase(z) + z.end, z.loopOffset});
                if (it == names.end()) continue;
                const int lo = k == 0 ? 0 : std::min(127, v.splits[size_t(k - 1)] + 1 + kSplitOffset);
                const int hi = k == nz - 1 ? 127 : std::min(127, v.splits[size_t(k)] + kSplitOffset);
                if (lo > hi) continue;
                t += "<region> sample=" + it->second + " lokey=" + std::to_string(lo) + " hikey=" + std::to_string(hi) +
                     " pitch_keycenter=" + std::to_string(z.root);
                if (v.layers > 1 && L + 1 < int(vb.size()))
                    t += " lovel=" + std::to_string(std::max(1, vb[size_t(L)])) + " hivel=" + std::to_string(vb[size_t(L) + 1] - 1);
                t += z.loopOffset + 2 < z.end - z.start ? " loop_mode=loop_continuous\n" : " loop_mode=one_shot\n";
            }
        char pre[16];
        std::snprintf(pre, sizeof pre, "g%02x_%03d_", v.group, v.index);
        out.emplace_back(pre + safeName(v.name) + ".sfz", t);
    }
    return out;
}

}  // namespace qs
