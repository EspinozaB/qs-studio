#include "qs_cardbuild.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>

#include "qs_mix.h"
#include "qs_program.h"

namespace qs {

namespace {

constexpr size_t kHeader = 0x10000, kFirstVoice = 0x1006C, kProgBase = 4, kProgStride = 349;
constexpr int kGuardWords = 8, kSilentTail = 64;

void putName(Bytes& d, size_t at, const std::string& s, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        char c = i < s.size() ? s[i] : ' ';
        if (c < 32 || c > 126) c = ' ';
        d[at + i] = uint8_t(c);
    }
}

uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }

// Band-limited resampling (windowed sinc), good enough for instrument samples.
std::vector<float> resample(const std::vector<float>& in, int fromRate, int toRate) {
    if (fromRate == toRate || in.empty()) return in;
    const double ratio = double(toRate) / fromRate, cutoff = std::min(1.0, ratio);
    const int half = 24;
    const size_t outN = size_t(std::floor(double(in.size()) * ratio));
    std::vector<float> out(outN);
    const double pi = 3.14159265358979323846;
    for (size_t i = 0; i < outN; ++i) {
        const double pos = double(i) / ratio;
        const long c = long(std::floor(pos));
        double acc = 0, wsum = 0;
        for (long k = c - half + 1; k <= c + half; ++k) {
            const double x = pos - double(k);
            const double sx = x * cutoff;
            const double sinc = std::abs(sx) < 1e-9 ? 1.0 : std::sin(pi * sx) / (pi * sx);
            const double wpos = (x + half) / (2.0 * half);
            if (wpos < 0 || wpos > 1) continue;
            const double win = 0.42 - 0.5 * std::cos(2 * pi * wpos) + 0.08 * std::cos(4 * pi * wpos);
            const double w = sinc * win * cutoff;
            wsum += w;
            if (k >= 0 && k < long(in.size())) acc += in[size_t(k)] * w;
        }
        out[i] = float(wsum > 0 ? acc / wsum : acc);
    }
    return out;
}

}  // namespace

bool readWav(const Bytes& f, WavSample& out, std::string& error) {
    out = WavSample{};
    if (f.size() < 44 || std::memcmp(f.data(), "RIFF", 4) != 0 || std::memcmp(f.data() + 8, "WAVE", 4) != 0) {
        error = "not a WAV file";
        return false;
    }
    int format = 0, channels = 0, bits = 0, rate = 0;
    const uint8_t* data = nullptr;
    size_t dataBytes = 0;
    long loopS = -1, loopE = -1;
    for (size_t p = 12; p + 8 <= f.size();) {
        const uint32_t len = rd32(&f[p + 4]);
        const uint8_t* c = &f[p + 8];
        const size_t avail = std::min<size_t>(len, f.size() - p - 8);
        if (!std::memcmp(&f[p], "fmt ", 4) && avail >= 16) {
            format = rd16(c);
            channels = rd16(c + 2);
            rate = int(rd32(c + 4));
            bits = rd16(c + 14);
            if (format == 0xFFFE && avail >= 26) format = rd16(c + 24);  // WAVE_FORMAT_EXTENSIBLE: sub-format
        } else if (!std::memcmp(&f[p], "data", 4)) {
            data = c;
            dataBytes = avail;
        } else if (!std::memcmp(&f[p], "smpl", 4) && avail >= 36) {
            out.root = int(rd32(c + 12));
            if (out.root > 127) out.root = -1;
            if (rd32(c + 28) > 0 && avail >= 60) {
                loopS = long(rd32(c + 36 + 8));
                loopE = long(rd32(c + 36 + 12));
            }
        }
        p += 8 + len + (len & 1);
    }
    if (!data || channels < 1 || rate < 1000) { error = "no audio found"; return false; }
    if (!((format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (format == 3 && bits == 32))) {
        error = "unsupported WAV format (use PCM or 32-bit float)";
        return false;
    }
    const size_t bps = size_t(bits / 8), frames = dataBytes / (bps * size_t(channels));
    std::vector<float> mono(frames);
    for (size_t i = 0; i < frames; ++i) {
        double sum = 0;
        for (int ch = 0; ch < channels; ++ch) {
            const uint8_t* s = data + (i * size_t(channels) + size_t(ch)) * bps;
            double v = 0;
            if (format == 3) { float x; std::memcpy(&x, s, 4); v = x; }
            else if (bits == 8) v = (s[0] - 128) / 128.0;
            else if (bits == 16) v = int16_t(rd16(s)) / 32768.0;
            else if (bits == 24) v = double(int32_t(uint32_t(s[0]) << 8 | uint32_t(s[1]) << 16 | uint32_t(s[2]) << 24) >> 8) / 8388608.0;
            else v = int32_t(rd32(s)) / 2147483648.0;
            sum += v;
        }
        mono[i] = float(sum / channels);
    }
    out.sourceRate = rate;
    out.channels = channels;
    const auto r = resample(mono, rate, kCardRate);
    out.pcm.resize(r.size());
    for (size_t i = 0; i < r.size(); ++i) out.pcm[i] = int16_t(std::lround(std::clamp(r[i], -1.0f, 1.0f) * 32767.0f));
    const double k = double(kCardRate) / rate;
    if (loopS >= 0 && loopE > loopS && size_t(loopS) < frames) {
        out.loopStart = std::lround(loopS * k);
        out.loopEnd = std::min<long>(long(out.pcm.size()) - 1, std::lround((loopE + 1) * k) - 1);
    }
    return true;
}

static std::string stemOf(const std::string& fileName) {
    std::string s = fileName;
    const auto slash = s.find_last_of("/\\");
    if (slash != std::string::npos) s = s.substr(slash + 1);
    const auto dot = s.find_last_of('.');
    return dot == std::string::npos ? s : s.substr(0, dot);
}

// "C3", "F#4", "Bb-1" -> MIDI note (C3 = 60). -1 if t isn't exactly a note name.
static int parseNoteName(const std::string& t) {
    if (t.size() < 2) return -1;
    static const int pcs[7] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
    const char L = char(std::toupper(uint8_t(t[0])));
    if (L < 'A' || L > 'G') return -1;
    int pc = pcs[L - 'A'];
    size_t i = 1;
    if (t[i] == '#' || t[i] == 's') { ++pc; ++i; }
    else if (t[i] == 'b') { --pc; ++i; }
    bool neg = false;
    if (i < t.size() && t[i] == '-') { neg = true; ++i; }
    if (i + 1 != t.size() || !std::isdigit(uint8_t(t[i]))) return -1;  // one octave digit
    const int oct = (neg ? -1 : 1) * (t[i] - '0');
    const int n = (oct + 2) * 12 + pc;
    return n >= 0 && n <= 127 ? n : -1;
}

// Where the note at the end of a stem starts (after a separator), or npos.
static size_t noteSuffix(const std::string& s, int& note) {
    for (size_t len = 2; len <= 4 && len <= s.size(); ++len) {
        const size_t at = s.size() - len;
        if (at > 0 && std::isalnum(uint8_t(s[at - 1]))) continue;
        const int n = parseNoteName(s.substr(at));
        if (n >= 0) { note = n; return at; }
    }
    // Plain MIDI note number: "bass-36", "pad 60".
    size_t at = s.size();
    while (at > 0 && std::isdigit(uint8_t(s[at - 1]))) --at;
    if (at < s.size() && s.size() - at <= 3 && at > 0 && !std::isalnum(uint8_t(s[at - 1]))) {
        const int n = std::atoi(s.c_str() + at);
        if (n <= 127) { note = n; return at; }
    }
    return std::string::npos;
}

int noteFromName(const std::string& fileName) {
    int n = -1;
    noteSuffix(stemOf(fileName), n);
    return n;
}

std::string baseNameWithoutNote(const std::string& fileName) {
    std::string s = stemOf(fileName);
    int n = -1;
    const size_t at = noteSuffix(s, n);
    if (at != std::string::npos) s = s.substr(0, at);
    while (!s.empty() && (s.back() == ' ' || s.back() == '_' || s.back() == '-' || s.back() == '.')) s.pop_back();
    return s.empty() ? stemOf(fileName) : s;
}

std::vector<BuildVoice> voicesFromWavs(const std::vector<NamedWav>& files, std::vector<std::string>& notes) {
    std::map<std::string, std::vector<const NamedWav*>> byName;
    std::vector<std::string> order;
    for (const auto& f : files) {
        const auto b = baseNameWithoutNote(f.fileName);
        if (!byName.count(b)) order.push_back(b);
        byName[b].push_back(&f);
    }
    std::vector<BuildVoice> out;
    for (const auto& name : order) {
        auto list = byName[name];
        struct Z { const NamedWav* f; int root; };
        std::vector<Z> zs;
        for (auto* f : list) {
            int root = f->wav.root >= 0 ? f->wav.root : noteFromName(f->fileName);
            if (root < 0) {
                root = 60;
                if (list.size() > 1) notes.push_back(f->fileName + ": no root note in the file or its name, using C3");
            }
            zs.push_back({f, root});
        }
        std::stable_sort(zs.begin(), zs.end(), [](const Z& a, const Z& b) { return a.root < b.root; });
        BuildVoice v;
        v.name = name;
        for (size_t k = 0; k < zs.size(); ++k) {
            if (k + 1 < zs.size() && zs[k + 1].root == zs[k].root) {
                notes.push_back(zs[k + 1].f->fileName + ": same root as " + zs[k].f->fileName + ", left out");
                zs.erase(zs.begin() + long(k) + 1);
                --k;
                continue;
            }
        }
        for (size_t k = 0; k < zs.size(); ++k) {
            BuildZone z;
            z.pcm = zs[k].f->wav.pcm;
            z.root = zs[k].root;
            z.loopStart = zs[k].f->wav.loopStart;
            z.source = zs[k].f->fileName;
            // Halfway up to the next sample's root; the last zone plays to the top.
            z.hiKey = k + 1 < zs.size() ? (zs[k].root + zs[k + 1].root) / 2 : 127;
            v.zones.push_back(std::move(z));
        }
        out.push_back(std::move(v));
    }
    return out;
}

bool buildCard(const CardBuild& b, BuildResult& out, std::string& error) {
    out = BuildResult{};
    if (b.voices.empty()) { error = "No samples to put on the card"; return false; }
    if (b.voices.size() > 128 * 8) { error = "Too many voices (a card holds 128 per group)"; return false; }
    Bytes d(size_t(kCardWords) * 2, 0);

    // Programs and mixes (Z-format card).
    d[0] = 0xA5; d[1] = 0xC3; d[2] = 0x5A; d[3] = 0xA5;
    std::map<int, int> perGroup;
    std::vector<std::pair<int, int>> ids;  // group, index
    for (const auto& v : b.voices) ids.push_back({v.group, ++perGroup[v.group]});
    for (auto& [g, n] : perGroup) {
        if (g < 16 || g > 23) { error = "Card keyboard groups are 16 to 23"; return false; }
        if (n > 127) { error = "More than 127 voices in one group"; return false; }
    }
    const auto& G = keyboardParam(findKeyboardParam("Sample group"));
    const auto& N = keyboardParam(findKeyboardParam("Sample number"));
    for (int i = 0; i < 128; ++i) {
        Program p = Program::initProgram();
        if (b.makePrograms && i < int(b.voices.size())) {
            p.setName(b.voices[size_t(i)].name);
            p.setRaw(0, G, ids[size_t(i)].first);
            p.setRaw(0, N, ids[size_t(i)].second);
        } else {
            p.setName("Init Prog");
        }
        std::memcpy(&d[kProgBase + size_t(i) * kProgStride], &p.bytes()[1], kProgStride);
    }
    const size_t mixBase = kProgBase + 128 * kProgStride;
    for (int i = 0; i < 100; ++i) {
        Mix m;
        m.setName("Card Mix" + std::string(i < 10 ? "0" : "") + std::to_string(i));
        std::memcpy(&d[mixBase + size_t(i) * kNewMixBytes], m.bytes().data(), kNewMixBytes);
    }

    // Sample header.
    d[kHeader] = 0x00; d[kHeader + 1] = 0x70; d[kHeader + 2] = 0x38; d[kHeader + 3] = 0x1C;
    d[kHeader + 4] = 0x02; d[kHeader + 5] = 0x40;
    d[kHeader + 8] = uint8_t(b.voices.size() >> 8);
    d[kHeader + 9] = uint8_t(b.voices.size());
    putName(d, kHeader + 10, b.cardName, 14);
    for (size_t slot = 0; slot < 14; ++slot) putName(d, kHeader + 24 + slot * 6, slot < 8 && slot < b.groupNames.size() ? b.groupNames[slot] : "", 6);

    // Directory size first, so the samples can follow it.
    size_t dirBytes = 0;
    for (const auto& v : b.voices) {
        if (v.zones.empty()) { error = v.name + ": no samples"; return false; }
        if (v.zones.size() > 128) { error = v.name + ": more than 128 key zones"; return false; }
        size_t n = 16 + (v.zones.size() - 1) + 1;
        n += n % 2;
        dirBytes += n + v.zones.size() * 12;
    }
    uint32_t word = uint32_t((kFirstVoice + dirBytes) / 2 + 64) & ~15u;

    // Voices that use the same sample (same audio and loop) share one copy on the card, as on Alesis's cards.
    struct Stored { std::vector<int16_t> pcm; long loop; uint32_t start, end; };
    std::map<uint64_t, std::vector<Stored>> stored;
    auto hashOf = [](const std::vector<int16_t>& v, long loop) {
        uint64_t h = 1469598103934665603ull ^ uint64_t(loop);
        for (int16_t x : v) h = (h ^ uint16_t(x)) * 1099511628211ull;
        return h;
    };
    size_t o = kFirstVoice;
    for (size_t vi = 0; vi < b.voices.size(); ++vi) {
        const auto& v = b.voices[vi];
        const size_t rec = o;
        d[o++] = uint8_t(ids[vi].first);
        d[o++] = uint8_t(ids[vi].second);
        putName(d, o, v.name, 10);
        o += 10;
        d[o++] = uint8_t(v.zones.size() - 1);  // key splits
        d[o++] = 0;                            // velocity layers - 1
        d[o++] = 128;
        d[o++] = 128;
        int prevHi = -1;
        for (size_t k = 0; k + 1 < v.zones.size(); ++k) {
            const int hi = std::clamp(v.zones[k].hiKey, std::max(12, prevHi + 1), 127);
            if (hi != v.zones[k].hiKey) out.notes.push_back(v.name + ": zone " + std::to_string(k + 1) + " top key moved to " + std::to_string(hi));
            d[o++] = uint8_t(hi - 12);  // the card stores the split 12 notes down (QS Studio reads it back the same way)
            prevHi = hi;
        }
        d[o++] = 128;  // top key, as on Alesis's cards
        if ((o - rec) % 2) d[o++] = 0;
        for (const auto& z : v.zones) {
            std::vector<int16_t> pcm = z.pcm;
            if (pcm.empty()) pcm.assign(16, 0);
            long loop = z.loopStart;
            if (loop < 0 || loop >= long(pcm.size())) {
                // One-shot: the QS always loops, so loop a stretch of silence after the sound.
                loop = long(pcm.size());
                pcm.insert(pcm.end(), kSilentTail, 0);
                if (loop > long(kMaxLoopOffset)) {
                    out.notes.push_back(v.name + " (" + z.source + "): longer than 1.36 s, so after it ends it repeats from 1.36 s. "
                                        "Give the program an amp envelope that fades out before the end.");
                    loop = long(kMaxLoopOffset);
                }
            } else if (loop > long(kMaxLoopOffset)) {
                out.notes.push_back(v.name + " (" + z.source + "): loop starts after 1.36 s, the latest a card allows; moved to 1.36 s");
                loop = long(kMaxLoopOffset);
            }
            const uint64_t h = hashOf(pcm, loop);
            const Stored* same = nullptr;
            for (const auto& st : stored[h])
                if (st.loop == loop && st.pcm == pcm) same = &st;
            const uint32_t start = same ? same->start : word, end = start + uint32_t(pcm.size());  // end word holds the loop start again
            if (size_t(end) + kGuardWords >= kCardWords) { error = "The samples don't fit on an 8 MB card (at " + v.name + ")"; return false; }
            if (!same) {
                for (size_t i = 0; i < pcm.size(); ++i) {
                    d[(start + i) * 2] = uint8_t(uint16_t(pcm[i]) >> 8);
                    d[(start + i) * 2 + 1] = uint8_t(pcm[i]);
                }
                // Guard words: the loop continues past the end so the QS's interpolation reads the right audio.
                const size_t loopLen = pcm.size() - size_t(loop);
                for (int g = 0; g < kGuardWords; ++g) {
                    const int16_t s = pcm[size_t(loop) + size_t(g) % loopLen];
                    d[(end + uint32_t(g)) * 2] = uint8_t(uint16_t(s) >> 8);
                    d[(end + uint32_t(g)) * 2 + 1] = uint8_t(s);
                }
            }
            d[o++] = uint8_t(std::clamp(z.root, 0, 127));
            d[o++] = 0;     // fine tune
            d[o++] = 0;
            d[o++] = 0xFC;  // level, as on most of Alesis's zones
            d[o++] = uint8_t(loop >> 8);
            d[o++] = uint8_t(loop);
            d[o++] = uint8_t(end >> 8);
            d[o++] = uint8_t(end);
            d[o++] = uint8_t((end >> 16) & 0x3F);
            d[o++] = uint8_t((start >> 16) & 0x3F);
            d[o++] = uint8_t(start >> 8);
            d[o++] = uint8_t(start);
            if (!same) {
                stored[h].push_back({pcm, loop, start, end});
                word = (end + kGuardWords + 7) & ~7u;
            }
        }
    }
    out.wordsUsed = word;
    // Trim to the data actually used, rounded up to a 64 KB card sector.
    const size_t used = std::max<size_t>(size_t(word) * 2, kFirstVoice + dirBytes);
    d.resize(std::min(d.size(), (used + 0xFFFF) & ~size_t(0xFFFF)));
    out.image = std::move(d);
    return true;
}

}  // namespace qs
