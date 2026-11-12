// Core tests: run with paths to card images and a factory .syx.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>

#include "../core/qs_card.h"
#include "../core/qs_cardbuild.h"
#include "../core/qs_effects.h"
#include "../core/qs_mix.h"
#include "../core/qs_program.h"
#include "../core/qs_samples.h"
#include "../core/qs_sysex.h"

static qs::Bytes readFile(const char* p) {
    std::ifstream f(p, std::ios::binary);
    return qs::Bytes(std::istreambuf_iterator<char>(f), {});
}

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::cerr << "FAIL " << __LINE__ << ": " #c "\n"; ++fails; } } while (0)

// A tiny synthetic sample ROM: header, one voice with one zone of a sine wave. Checks the voice directory parser,
// the loop/region mapping and the byte-order check (both orders).
// Sound tools, init, blend and randomize keep programs valid and do what they say.
static void programToolsTest(const qs::Program& a, const qs::Program& b) {
    auto p = a;
    p.swapSounds(0, 3);
    CHECK(p.sound(0) == a.sound(3) && p.sound(3) == a.sound(0) && p.name() == a.name());
    p.swapSounds(0, 3);
    CHECK(p.bytes() == a.bytes());
    p.setSound(2, a.sound(1));
    CHECK(p.sound(2) == a.sound(1) && p.sound(1) == a.sound(1));
    const auto init = qs::Program::initProgram();
    CHECK(init.name() == "Init Prog " && init.isEnabled(0) && !init.isEnabled(1) && !init.isDrum(0));
    CHECK(init.value(0, qs::keyboardParam(qs::findKeyboardParam("Volume"))) == 99);
    CHECK(qs::blendPrograms(a, b, 0.0).bytes() == a.bytes());
    const auto half = qs::blendPrograms(a, b, 0.5);
    for (int s = 0; s < qs::kSoundCount; ++s)
        for (int i = 0; i < qs::keyboardParamCount(); ++i) {
            const auto& d = qs::keyboardParam(i);
            if (a.isDrum(s) || b.isDrum(s) || d.name[0] == 0) continue;
            CHECK(half.raw(s, d) <= std::max(a.raw(s, d), b.raw(s, d)) && half.raw(s, d) >= std::min(a.raw(s, d), b.raw(s, d)));
        }
    const auto r = qs::randomizeProgram(a, 0.3, false, 12345);
    CHECK(r.name() == a.name() && r.bytes() != a.bytes());
    CHECK(qs::randomizeProgram(a, 0.3, false, 12345).bytes() == r.bytes());  // same seed, same result
    for (int s = 0; s < qs::kSoundCount; ++s)
        if (!a.isDrum(s)) CHECK(r.raw(s, qs::keyboardParam(qs::findKeyboardParam("Sample number"))) ==
                                a.raw(s, qs::keyboardParam(qs::findKeyboardParam("Sample number"))));
    std::printf("    program tools: swap, copy, init, blend, randomize OK\n");
}

static void syntheticRomTest() {
    for (int swapped = 0; swapped < 2; ++swapped) {
        qs::Bytes d(0x20000, 0);
        const uint8_t head[] = {0x00, 0x80, 0x00, 0x00, 0x04};
        std::copy(head, head + 5, d.begin());
        const char* name = "FQS78ROM v20220";
        std::copy(name, name + 15, d.begin() + 5);
        size_t o = 0x14;
        d[o++] = 0;  // group
        d[o++] = 1;  // index
        const char* vn = "Test Sine ";
        std::copy(vn, vn + 10, d.begin() + long(o));
        o += 10;
        d[o++] = 0;     // no splits
        d[o++] = 0;     // one layer
        d[o++] = 0x80;  // velocity splits unused
        d[o++] = 0x80;
        d[o++] = 127;   // top key
        if (o % 2) ++o;
        const uint32_t start = 0x8000, end = start + 4799;  // 100 cycles of 48 samples
        const uint8_t zone[12] = {69, 0, 0, 0, 0, 0, uint8_t(end >> 8), uint8_t(end), uint8_t((end >> 16) & 0x3F),
                                  uint8_t((start >> 16) & 0x3F), uint8_t(start >> 8), uint8_t(start)};
        std::copy(zone, zone + 12, d.begin() + long(o));
        for (uint32_t i = 0; i <= end - start; ++i) {
            const auto v = uint16_t(int16_t(std::lround(12000.0 * std::sin(2.0 * 3.14159265358979 * i / 48.0))));
            const size_t b = size_t(start + i) * 2;
            d[b] = uint8_t(swapped ? v : v >> 8);
            d[b + 1] = uint8_t(swapped ? v >> 8 : v);
        }
        qs::CardImage c;
        std::string err;
        CHECK(qs::loadInternalRom(d, c, err));
        CHECK(c.voices.size() == 1 && c.voices[0].name == "Test Sine ");
        CHECK(c.samplesSwapped == (swapped != 0) && !c.samplesLookWrong);
        const auto regions = qs::voiceRegions(c.voices[0]);
        CHECK(regions.size() == 1 && regions[0].lo == 0 && regions[0].hi == 127 && regions[0].zone->root == 69);
        CHECK(c.sampleAt(start + 12) > 11000);  // a quarter cycle in: near the peak, whichever order it was stored in
        std::printf("    synthetic ROM (%s): %s\n", swapped ? "little-endian" : "big-endian", c.sampleCheck.c_str());
    }
}


// Card builder: note names, WAV reading and resampling, grouping files into voices, and a built card read back by the
// same loader the Sound Player and Cards & ROM use.
static std::vector<int16_t> sine(int n, double cyclesPerSample, double amp = 12000.0) {
    std::vector<int16_t> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) v[size_t(i)] = int16_t(std::lround(amp * std::sin(2.0 * 3.14159265358979 * i * cyclesPerSample)));
    return v;
}

static void cardBuildTest() {
    CHECK(qs::noteFromName("Piano_C3.wav") == 60 && qs::noteFromName("Str F#4.wav") == 78 && qs::noteFromName("bass-36.wav") == 36);
    CHECK(qs::noteFromName("Kick_C-1.wav") == 12 && qs::noteFromName("Pad Bb2.WAV") == 58 && qs::noteFromName("Groove.wav") == -1);
    CHECK(qs::noteFromName("Piano_C3_v2.wav") == -1 && qs::noteFromName("TakeC3.wav") == -1);
    CHECK(qs::baseNameWithoutNote("Piano_C3.wav") == "Piano" && qs::baseNameWithoutNote("Str F#4.wav") == "Str" &&
          qs::baseNameWithoutNote("Groove.wav") == "Groove");

    // 44.1 kHz WAVs with a smpl loop, resampled to 48 kHz.
    std::vector<qs::NamedWav> files;
    const int notes[3] = {48, 60, 72};
    const char* names[3] = {"Tone_C2.wav", "Tone_C3.wav", "Tone_C4.wav"};
    for (int k = 0; k < 3; ++k) {
        const auto pcm = sine(44100, 1.0 / 100.0);  // 441 Hz at 44.1 kHz, exactly 441 cycles
        const auto wav = qs::makeWav(pcm.data(), pcm.size(), 44100, -1, 22050, 44099);
        qs::WavSample ws;
        std::string err;
        CHECK(qs::readWav(wav, ws, err));
        CHECK(std::abs(long(ws.pcm.size()) - 48000) <= 1 && ws.sourceRate == 44100);
        CHECK(ws.loopStart == 24000 && std::abs(ws.loopEnd - 47999) <= 1);
        files.push_back({names[k], ws});
        (void)notes;
    }
    // A one-shot with no note in its name.
    {
        const auto pcm = sine(9000, 1.0 / 30.0);
        qs::WavSample ws;
        std::string err;
        CHECK(qs::readWav(qs::makeWav(pcm.data(), pcm.size(), 48000, -1, -1, -1), ws, err));
        CHECK(ws.pcm.size() == 9000 && ws.loopStart == -1 && ws.pcm[300] == pcm[300]);
        files.push_back({"Hit.wav", ws});
    }
    std::vector<std::string> notesOut;
    auto voices = qs::voicesFromWavs(files, notesOut);
    CHECK(voices.size() == 2 && voices[0].name == "Tone" && voices[0].zones.size() == 3 && voices[1].name == "Hit");
    CHECK(voices[0].zones[0].root == 48 && voices[0].zones[0].hiKey == 54 && voices[0].zones[1].hiKey == 66 && voices[0].zones[2].hiKey == 127);

    qs::CardBuild b;
    b.cardName = "Test card";
    b.groupNames = {"Tests"};
    b.voices = voices;
    qs::BuildResult r;
    std::string err;
    CHECK(qs::buildCard(b, r, err));
    qs::CardImage c;
    CHECK(qs::loadCardImage(r.image, c, err));
    CHECK(qs::voiceGroupName(c, 16) == "Tests" && qs::voiceGroupName(c, 17) == "Card 2");
    CHECK(c.cardName == "Test card" && c.groups.size() == 1 && c.voices.size() == 2 && !c.samplesSwapped && !c.samplesLookWrong);
    CHECK(c.programNames.size() == 128 && c.programNames[0].rfind("Tone", 0) == 0 && c.programNames[1].rfind("Hit", 0) == 0);
    if (c.voices.size() == 2) {
        const auto reg = qs::voiceRegions(c.voices[0]);
        CHECK(reg.size() == 3 && reg[0].lo == 0 && reg[0].hi == 54 && reg[1].lo == 55 && reg[1].hi == 66 && reg[2].lo == 67 && reg[2].hi == 127);
        const auto& z = c.voices[0].zones[1];
        CHECK(z.root == 60 && z.loopOffset == 24000 && z.end - z.start == voices[0].zones[1].pcm.size());
        for (size_t i = 0; i < 2000; i += 37) CHECK(c.sampleAt(z.start + uint32_t(i)) == voices[0].zones[1].pcm[i]);
        CHECK(c.sampleAt(z.end) == c.sampleAt(z.start + z.loopOffset));  // end word repeats the loop start
        const auto& h = c.voices[1].zones[0];
        CHECK(h.loopOffset == 9000 && h.end - h.start == 9064 && c.sampleAt(h.start + 9010) == 0);  // silent tail loop
        auto p = qs::Program::fromData(c.program(1));
        const auto& G = qs::keyboardParam(qs::findKeyboardParam("Sample group"));
        const auto& N = qs::keyboardParam(qs::findKeyboardParam("Sample number"));
        CHECK(p && p->raw(0, G) == 16 && p->raw(0, N) == 2 && p->isEnabled(0));
    }
    std::printf("    card builder: %d voices, %.1f s of audio, %zu notes\n", int(c.voices.size()), r.secondsUsed(), r.notes.size());

    // Rebuild a real card from its own samples (QS_TEST_CARD=path to a card image, not part of the repo).
    if (const char* path = std::getenv("QS_TEST_CARD")) {
        qs::CardImage orig;
        if (!qs::loadCardImage(readFile(path), orig, err)) { std::printf("    QS_TEST_CARD: %s\n", err.c_str()); return; }
        qs::CardBuild rb;
        rb.cardName = orig.cardName;
        rb.groupNames.clear();
        for (int g = 16; g < 24; ++g) rb.groupNames.push_back("G" + std::to_string(g));
        int skipped = 0;
        size_t budget = 0;
        std::set<std::pair<uint32_t, uint32_t>> counted;
        for (const auto& v : orig.voices) {
            if (v.group < 16 || v.group > 23 || v.layers != 1 || budget > 3000000) { ++skipped; continue; }
            // Alesis's cards overlap samples (one zone inside another); a rebuild stores each separately, so only part fits.
            for (const auto& z : v.zones)
                if (counted.insert({z.start, z.end}).second) budget += z.end - z.start;
            qs::BuildVoice bv;
            bv.name = v.name;
            bv.group = v.group;
            const auto regs = qs::voiceRegions(v);
            for (const auto& reg : regs) {
                qs::BuildZone z;
                for (uint32_t w = reg.zone->start; w < reg.zone->end; ++w) z.pcm.push_back(orig.sampleAt(w));
                z.root = reg.zone->root;
                z.loopStart = long(reg.zone->loopOffset);
                z.hiKey = reg.hi;
                bv.zones.push_back(std::move(z));
            }
            rb.voices.push_back(std::move(bv));
        }
        qs::BuildResult rr;
        CHECK(qs::buildCard(rb, rr, err));
        if (!err.empty()) std::printf("    rebuild: %s\n", err.c_str());
        qs::CardImage back;
        CHECK(qs::loadCardImage(rr.image, back, err));
        size_t zones = 0, same = 0;
        for (size_t i = 0, j = 0; i < orig.voices.size() && j < back.voices.size(); ++i) {
            const auto& a = orig.voices[i];
            if (a.name != back.voices[j].name) continue;
            const auto& bvv = back.voices[j++];
            const auto ra = qs::voiceRegions(a), rbr = qs::voiceRegions(bvv);
            CHECK(a.name == bvv.name && a.index == bvv.index && ra.size() == rbr.size());
            for (size_t k = 0; k < std::min(ra.size(), rbr.size()); ++k) {
                ++zones;
                const auto &za = *ra[k].zone, &zb = *rbr[k].zone;
                bool ok = ra[k].lo == rbr[k].lo && ra[k].hi == rbr[k].hi && za.root == zb.root && za.loopOffset == zb.loopOffset &&
                          za.end - za.start == zb.end - zb.start;
                for (uint32_t w = 0; ok && w < za.end - za.start; w += 97) ok = orig.sampleAt(za.start + w) == back.sampleAt(zb.start + w);
                same += ok;
            }
        }
        CHECK(zones > 0 && same == zones);
        std::printf("    rebuilt %s: %zu of %zu zones identical (keys, root, loop, audio), %d voices skipped, %.0f s of audio\n",
                    orig.cardName.c_str(), same, zones, skipped, rr.secondsUsed());
    }
}

int main(int argc, char** argv) {
    syntheticRomTest();
    cardBuildTest();
    // pack/unpack round trip and sizes from the spec
    CHECK(qs::packedSize(350) == 400 && qs::packedSize(138) == 158 && qs::packedSize(65) == 75 &&
          qs::packedSize(20) == 23 && qs::packedSize(1024) == 1171);
    qs::Bytes x(1024);
    for (size_t i = 0; i < x.size(); ++i) x[i] = uint8_t(i * 37 + 11);
    auto p = qs::pack7(x.data(), x.size());
    CHECK(p.size() == 1171);
    for (auto b : p) CHECK(b < 0x80);
    CHECK(qs::unpack7(p.data(), p.size(), 1024) == x);
    auto w = qs::sectorWrite(5, 9, x.data());
    CHECK(w.size() == 1181 && w.front() == 0xF0 && w.back() == 0xF7 && w[5] == 0x12);
    auto sb = qs::parseSectorWrite(w);
    CHECK(sb && sb->sector == 5 && sb->block == 9 && sb->checksumOk && sb->data == x);
    w[100] ^= 1;
    CHECK(!qs::parseSectorWrite(w)->checksumOk);
    // device reply
    qs::Bytes rep{0xF0, 0x7E, 0x7F, 0x06, 0x02, 0, 0, 0x0E, 0x0E, 0, 0x03, 0, '0', '2', '0', '0', 0xF7};
    auto di = qs::parseDeviceReply(rep);
    CHECK(di && di->model == "QS6" && di->version == "2.00");
    rep[10] = 0x0F;
    CHECK(qs::parseDeviceReply(rep)->model == "QS8.1");
    qs::Bytes rom(1024, 0);
    rom[1] = 0x80; rom[8] = 'R'; rom[9] = 'O'; rom[10] = 'M';
    CHECK(qs::looksLikeInternalRom(rom));
    rom[1] = 0;
    CHECK(!qs::looksLikeInternalRom(rom));

    // program table and live edit messages
    CHECK(qs::keyboardParamCount() == 121 && qs::drumParamCount() == 15);
    const int ff = qs::findKeyboardParam("Filter frequency"), det = qs::findKeyboardParam("Pitch detune");
    CHECK(ff >= 0 && det >= 0);
    CHECK(qs::keyboardParam(ff).lowBit == 89 && qs::keyboardParam(ff).bits == 7);   // spec 11:7-11:1
    CHECK(qs::paramEdit(qs::keyboardParam(ff), 1, 50) == (qs::Bytes{0xF0, 0, 0, 0x0E, 0x0E, 0x10, 0x44, 0x20, 0x00, 0x32, 0xF7}));
    auto neg = qs::paramEdit(qs::keyboardParam(det), 0, -5);  // func 3 page 0 pot 2, two's complement 0xFB
    CHECK(neg[6] == 0x43 && neg[7] == 0x00 && neg[8] == ((2 << 1) | 1) && neg[9] == 0x7B);
    {
        qs::Program p;
        p.setName("Test Prog!");
        CHECK(p.name() == "Test Prog!");
        p.setValue(2, qs::keyboardParam(det), -42);
        CHECK(p.value(2, qs::keyboardParam(det)) == -42 && p.value(1, qs::keyboardParam(det)) == -99);
        p.setValue(2, qs::keyboardParam(ff), 500);  // clamps to the spec limit
        CHECK(p.value(2, qs::keyboardParam(ff)) == 99);
        auto back = qs::Program::fromDump(p.editDump());
        CHECK(back && back->bytes() == p.bytes() && p.editDump().size() == 408);
    }
    CHECK(std::string(qs::sampleGroupName(0)) == "Piano" && std::string(qs::sampleGroupName(24)) == "Rhythm");

    // sample tables and the QS6 <-> QS7/8/R converter
    CHECK(qs::sampleName(qs::RomSet::QS78, false, 0, 1) == "GrndPianoL");
    CHECK(qs::sampleName(qs::RomSet::QS78, false, 10, 2) == "Spectral");
    CHECK(qs::sampleName(qs::RomSet::QS6, false, 10, 2) == "Spectral 1");
    CHECK(qs::sampleName(qs::RomSet::QS78, true, 13, 1) == "Psi Beat 1");
    CHECK(qs::sampleCount(qs::RomSet::QS78, false, 24) == 90);
    {
        qs::Program p;
        const auto& en = qs::keyboardParam(qs::findKeyboardParam("Enable"));
        const auto& g = qs::keyboardParam(qs::findKeyboardParam("Sample group"));
        const auto& n = qs::keyboardParam(qs::findKeyboardParam("Sample number"));
        p.setRaw(0, en, 1); p.setRaw(0, g, 10); p.setRaw(0, n, 2);   // QS6 Noise #2 Spectral 1
        p.setRaw(1, en, 1); p.setRaw(1, g, 3);  p.setRaw(1, n, 1);   // Guitar #1 SteelStrng, same on both
        p.setRaw(2, en, 1); p.setRaw(2, g, 24); p.setRaw(2, n, 1);   // QS6 Rhythm #1 SynDrumLp1, not on the QSR
        auto c = qs::convertProgram(p, qs::RomSet::QS6, qs::RomSet::QS78);
        CHECK(c.program.raw(0, g) == 10 && c.program.raw(0, n) == 2);  // -> QSR Noise #2 "Spectral"
        CHECK(c.notes.size() == 3 && c.notes[1].kind == qs::ConvertNote::Same && c.notes[2].kind == qs::ConvertNote::Unmatched);
        CHECK(c.program.raw(2, g) == 24 && c.program.raw(2, n) == 1);  // unmatched left as is
    }

    for (int a = 1; a < argc; ++a) {
        auto raw = readFile(argv[a]);
        std::string s(argv[a]);
        if (s.size() > 4 && (s.substr(s.size() - 4) == ".syx" || s.substr(s.size() - 4) == ".SYX")) {
            auto msgs = qs::splitSysex(raw);
            int progs = 0, roundTrip = 0;
            for (auto& m : msgs)
                if (auto p = qs::Program::fromDump(m)) {
                    ++progs;
                    auto again = qs::Program::fromDump(p->userDump(m[6]));
                    roundTrip += again && again->bytes() == p->bytes() && p->userDump(m[6]) == m;
                }
            CHECK(progs == roundTrip);
            {
                std::vector<qs::Program> ps;
                for (auto& m : msgs)
                    if (auto p = qs::Program::fromDump(m)) ps.push_back(*p);
                if (ps.size() > 40) programToolsTest(ps[0], ps[40]);
            }
            for (auto& m : msgs)
                if (auto gs = qs::parseGlobalDump(m)) {
                    std::printf("    global: controllers A-D CC %d %d %d %d, program select %d\n", gs->controller[0], gs->controller[1],
                                gs->controller[2], gs->controller[3], gs->programSelect);
                    CHECK(gs->controller[2] == 91 && gs->programSelect == 1);
                }
            {
                auto toQs6 = qs::convertBank(msgs, qs::RomSet::QS78, qs::RomSet::QS6);
                int changed = 0, missing = 0;
                for (auto& [num, c] : toQs6.programs) { changed += c.changed(); missing += c.unmatched(); }
                std::printf("    QS7/8/R -> QS6: %d sample refs renumbered, %d with no QS6 match\n", changed, missing);
                CHECK(toQs6.messages.size() == msgs.size());
            }
            {
                int mixes = 0;
                std::string first, last;
                for (auto& m : msgs)
                    if (auto mi = qs::parseMixDump(m)) {
                        ++mixes;
                        CHECK(mi->number < 100 && mi->data.size() == qs::kNewMixBytes);
                        if (first.empty()) first = mi->name;
                        last = mi->name;
                    }
                std::printf("    mixes parsed: %d ('%s' ... '%s')\n", mixes, first.c_str(), last.c_str());
                // Mix editor model: round trip, names, and every enabled channel within the spec's limits.
                int mixOk = 0, outOfRange = 0;
                for (auto& m : msgs)
                    if (auto mx = qs::Mix::fromDump(m)) {
                        mixOk += mx->dump(m[6]) == m && mx->name() == qs::parseMixDump(m)->name;
                        for (int ch = 0; ch < qs::kMixChannels; ++ch)
                            if (mx->enabled(ch))
                                for (int i = 0; i < qs::mixChannelParamCount(); ++i)
                                    outOfRange += mx->channel(ch, qs::mixChannelParam(i)) > qs::mixChannelParam(i).limit;
                        auto edited = *mx;
                        edited.setName("Test Mix");
                        edited.setChannel(3, qs::mixChannelParam(qs::findMixChannelParam("Volume")), 42);
                        CHECK(edited.name() == "Test Mix  " && edited.channel(3, qs::mixChannelParam(qs::findMixChannelParam("Volume"))) == 42);
                        CHECK(edited.program(3) == mx->program(3) && edited.channel(2, qs::mixChannelParam(3)) == mx->channel(2, qs::mixChannelParam(3)));
                    }
                std::printf("    mix model: %d of %d round-trip, %d enabled-channel values over the limit\n", mixOk, mixes, outOfRange);
                CHECK(mixOk == mixes && outOfRange == 0);
                // Effects model: round trip, and every visible parameter within its limit.
                int fxCount = 0, fxOk = 0, fxOver = 0;
                for (auto& m : msgs)
                    if (auto e = qs::Effects::fromDump(m)) {
                        ++fxCount;
                        fxOk += e->userDump(m[6]) == m;
                        const int c = e->config();
                        const auto& cfg = qs::effectConfig(c);
                        for (int i = 0; i < cfg.count; ++i)
                            if (e->visible(c, i) && e->raw(cfg.params[i]) > cfg.params[i].limit && std::string(cfg.params[i].name).find("horn") == std::string::npos) {
                                ++fxOver;
                                std::printf("      over: config %d %s = %d\n", c, cfg.params[i].name, e->raw(cfg.params[i]));
                            }
                    }
                std::printf("    effects model: %d of %d round-trip, %d visible values over the limit\n", fxOk, fxCount, fxOver);
                CHECK(fxOk == fxCount && fxOver == 0);
                auto pe = qs::mixParamEdit(qs::mixChannelParam(qs::findMixChannelParam("Pan")), 4, 3);
                CHECK(pe.size() == 11 && pe[6] == ((1 << 5) | 1) && pe[8] == ((4 << 3) | (1 << 1)) && pe[9] == 3);
                CHECK(mixes == 100);
                auto rq = qs::mixRequest(100);
                CHECK(rq.size() == 8 && rq[5] == 0x0F && rq[6] == 100);
            }
            auto sum = qs::summarize(msgs);
            std::cout << s << ": programs=" << sum.programs << " mixes=" << sum.mixes << " effects=" << sum.effects
                      << " globals=" << sum.globals << " first='" << (sum.programNames.empty() ? "" : sum.programNames[0].second)
                      << "' mix0='" << (sum.mixNames.empty() ? "" : sum.mixNames[0].second) << "'\n";
            continue;
        }
        qs::CardImage c;
        std::string err;
        if (!qs::loadCardImage(raw, c, err)) { std::cout << s << ": ERROR " << err << "\n"; ++fails; continue; }
        size_t zones = 0, bad = 0;
        for (auto& v : c.voices) for (auto& z : v.zones) { ++zones; bad += z.end <= z.start || z.end * 2 >= c.data.size(); }
        std::cout << s.substr(s.find_last_of('/') + 1) << ": '" << c.cardName << "' voices=" << c.voices.size() << " zones=" << zones
                  << " unique=" << c.uniqueSampleCount() << " bad=" << bad << " prog0='" << c.programNames[0] << "' prog127='"
                  << c.programNames[127] << "' mix0='" << c.mixNames[0] << "' mix99='" << c.mixNames[99] << "'\n";
        CHECK(bad == 0);
    }
    std::cout << (fails ? "FAILED " : "ALL OK ") << fails << "\n";
    return fails ? 1 : 0;
}
