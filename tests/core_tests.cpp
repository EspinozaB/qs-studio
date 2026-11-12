// Core tests: run with paths to card images and a factory .syx.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>

#include "../core/qs_card.h"
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

int main(int argc, char** argv) {
    syntheticRomTest();
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
