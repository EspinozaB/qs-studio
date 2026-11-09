// Core tests: run with paths to card images and a factory .syx.
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>

#include "../core/qs_card.h"
#include "../core/qs_sysex.h"

static qs::Bytes readFile(const char* p) {
    std::ifstream f(p, std::ios::binary);
    return qs::Bytes(std::istreambuf_iterator<char>(f), {});
}

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::cerr << "FAIL " << __LINE__ << ": " #c "\n"; ++fails; } } while (0)

int main(int argc, char** argv) {
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

    for (int a = 1; a < argc; ++a) {
        auto raw = readFile(argv[a]);
        std::string s(argv[a]);
        if (s.size() > 4 && (s.substr(s.size() - 4) == ".syx" || s.substr(s.size() - 4) == ".SYX")) {
            auto sum = qs::summarize(qs::splitSysex(raw));
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
