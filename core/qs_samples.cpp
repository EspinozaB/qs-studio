#include "qs_samples.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace qs {

namespace {

struct SampleGroupDef {
    const char* model;
    bool drum;
    int group;
    std::vector<const char*> names;
};

#include "qs_samples.inc"

const char* key(RomSet r) { return r == RomSet::QS6 ? "QS6" : "QS78"; }

const SampleGroupDef* findGroup(RomSet r, bool drum, int group) {
    for (const auto& g : kSampleGroups)
        if (std::string(g.model) == key(r) && g.drum == drum && g.group == group) return &g;
    return nullptr;
}

std::string norm(const std::string& s) {
    std::string o;
    for (char c : s)
        if (std::isalnum(static_cast<unsigned char>(c))) o += char(std::tolower(static_cast<unsigned char>(c)));
    return o;
}

// "close": the same letters without vowels, ignoring a trailing L/R channel mark (GrandPiano ~ GrndPianoL).
std::string skeleton(const std::string& s) {
    std::string n = norm(s);
    if (n.size() > 3 && (n.back() == 'l' || n.back() == 'r') && std::isalpha(static_cast<unsigned char>(n[n.size() - 2]))) n.pop_back();
    std::string o;
    for (size_t i = 0; i < n.size(); ++i)
        if (i == 0 || std::string("aeiou").find(n[i]) == std::string::npos) o += n[i];
    return o;
}

struct Match {
    int group = -1, number = -1;
    ConvertNote::Kind kind = ConvertNote::Unmatched;
    std::string name;
};

std::string dropTrailingNumber(std::string n) {
    while (!n.empty() && std::isdigit(static_cast<unsigned char>(n.back()))) n.pop_back();
    return n;
}

// Pass 1: same name. Pass 2: same skeleton (GrandPiano ~ GrndPianoL). Pass 3: same name without a trailing number
// (Spectral 1 ~ Spectral). The source's own group is searched first in every pass.
Match findSample(RomSet to, bool drum, int preferGroup, const std::string& name) {
    const auto n = norm(name), sk = skeleton(name), bare = dropTrailingNumber(norm(name));
    auto groups = sampleGroups(to, drum);
    std::stable_partition(groups.begin(), groups.end(), [&](int g) { return g == preferGroup; });
    for (int pass = 0; pass < 3; ++pass)
        for (int g : groups) {
            const auto* def = findGroup(to, drum, g);
            for (size_t i = 0; def && i < def->names.size(); ++i) {
                const std::string t = def->names[i];
                // Pass 3 may only pair a numbered name with an unnumbered one: "Kick Loop6" is not "Kick Loop1".
                const bool oneUnnumbered = bare == n || dropTrailingNumber(norm(t)) == norm(t);
                const bool hit = pass == 0 ? norm(t) == n
                                 : pass == 1 ? skeleton(t) == sk
                                             : bare.size() >= 4 && oneUnnumbered && dropTrailingNumber(norm(t)) == bare;
                if (hit) return {g, int(i) + 1, pass == 0 ? ConvertNote::Renumbered : ConvertNote::Close, t};
            }
        }
    return {};
}

}  // namespace

const char* romSetName(RomSet r) { return r == RomSet::QS6 ? "QS6" : "QS7/QS8/QSR"; }

RomSet romSetForModel(const std::string& model) { return model == "QS6" ? RomSet::QS6 : RomSet::QS78; }

std::string sampleName(RomSet r, bool drum, int group, int number) {
    const auto* g = findGroup(r, drum, group);
    return g && number >= 1 && number <= int(g->names.size()) ? g->names[size_t(number - 1)] : "";
}

int sampleCount(RomSet r, bool drum, int group) {
    const auto* g = findGroup(r, drum, group);
    return g ? int(g->names.size()) : 0;
}

std::vector<int> sampleGroups(RomSet r, bool drum) {
    std::vector<int> out;
    for (const auto& g : kSampleGroups)
        if (std::string(g.model) == key(r) && g.drum == drum) out.push_back(g.group);
    return out;
}

const char* drumGroupName(RomSet r, int g) {
    static const std::map<int, const char*> qs6{{0, "Kick"}, {1, "Snare"}, {2, "Hi Hat"}, {3, "Toms"}, {4, "Cymbal"}, {5, "Percus"},
                                                 {6, "Percus FX"}, {7, "Wave"}, {8, "Rhythm"}};
    static const std::map<int, const char*> qs78{{0, "Kick"}, {1, "Snare"}, {2, "Toms"}, {3, "Cymbal"}, {4, "Percus"}, {5, "Snd FX"},
                                                  {6, "Wave"}, {13, "Rhythm"}};
    const auto& m = r == RomSet::QS6 ? qs6 : qs78;
    auto it = m.find(g);
    return it != m.end() ? it->second : "";
}

int ConvertedProgram::changed() const {
    return int(std::count_if(notes.begin(), notes.end(), [](auto& n) { return n.kind == ConvertNote::Renumbered || n.kind == ConvertNote::Close; }));
}
int ConvertedProgram::unmatched() const {
    return int(std::count_if(notes.begin(), notes.end(), [](auto& n) { return n.kind == ConvertNote::Unmatched; }));
}

ConvertedProgram convertProgram(const Program& src, RomSet from, RomSet to) {
    ConvertedProgram out{src, {}};
    if (from == to) return out;
    auto& p = out.program;
    const int kg = findKeyboardParam("Sample group"), kn = findKeyboardParam("Sample number");
    const ParamDef* dg = nullptr;
    const ParamDef* dn = nullptr;
    for (int i = 0; i < drumParamCount(); ++i) {
        if (std::string(drumParam(i).name) == "Sample group") dg = &drumParam(i);
        if (std::string(drumParam(i).name) == "Sample number") dn = &drumParam(i);
    }
    for (int s = 0; s < kSoundCount; ++s) {
        if (!p.isEnabled(s)) continue;
        const bool drum = p.isDrum(s);
        for (int d = 0; d < (drum ? kDrumsPerSound : 1); ++d) {
            const auto& gDef = drum ? *dg : keyboardParam(kg);
            const auto& nDef = drum ? *dn : keyboardParam(kn);
            const int g = p.raw(s, gDef, d), n = p.raw(s, nDef, d);
            const auto name = sampleName(from, drum, g, n);
            ConvertNote note;
            note.sound = s;
            note.drum = drum ? d : -1;
            note.from = name.empty() ? "group " + std::to_string(g) + " #" + std::to_string(n) : name;
            if (name.empty()) {  // not in our tables (card samples, out of range): leave untouched
                note.kind = ConvertNote::Unmatched;
                out.notes.push_back(note);
                continue;
            }
            if (norm(sampleName(to, drum, g, n)) == norm(name)) {
                note.kind = ConvertNote::Same;
                note.to = name;
                out.notes.push_back(note);
                continue;
            }
            auto m = findSample(to, drum, g, name);
            note.kind = m.kind;
            note.to = m.name;
            if (m.group >= 0) {
                p.setRaw(s, gDef, m.group, d);
                p.setRaw(s, nDef, m.number, d);
            }
            out.notes.push_back(note);
        }
    }
    return out;
}

ConvertedBank convertBank(const std::vector<Bytes>& msgs, RomSet from, RomSet to) {
    ConvertedBank out;
    for (const auto& m : msgs) {
        const int op = opcodeOf(m);
        if ((op == UserProgramDump || op == EditProgramDump) && m.size() > 6)
            if (auto p = Program::fromDump(m)) {
                auto c = convertProgram(*p, from, to);
                out.messages.push_back(op == UserProgramDump ? c.program.userDump(m[6]) : c.program.editDump());
                out.programs.emplace_back(m[6], std::move(c));
                continue;
            }
        out.messages.push_back(m);
    }
    return out;
}

std::string ConvertedBank::report(RomSet from, RomSet to) const {
    std::ostringstream r;
    int changed = 0, close = 0, missing = 0, affected = 0;
    for (auto& [num, c] : programs) {
        for (auto& n : c.notes) close += n.kind == ConvertNote::Close;
        changed += c.changed();
        missing += c.unmatched();
        affected += c.unmatched() > 0;
    }
    r << "Converted " << programs.size() << " programs from " << romSetName(from) << " to " << romSetName(to) << ".\n"
      << changed << " sample references renumbered (" << close << " by a close name), " << missing << " with no match in "
      << affected << " programs.\n"
      << "Sample tables come from the Alesis reference manuals; the QS6 table is unverified until a QS6 ROM is read.\n\n";
    for (auto& [num, c] : programs) {
        if (c.changed() == 0 && c.unmatched() == 0) continue;
        r << (num < 100 ? (num < 10 ? "00" : "0") : "") << num << "  " << c.program.name() << (c.unmatched() ? "   << has samples with no match" : "") << "\n";
        for (auto& n : c.notes) {
            if (n.kind == ConvertNote::Same) continue;
            r << "      sound " << n.sound + 1;
            if (n.drum >= 0) r << " drum " << n.drum + 1;
            r << ": " << n.from << "  ->  ";
            if (n.kind == ConvertNote::Unmatched) r << "NO MATCH (left as is)";
            else r << n.to << (n.kind == ConvertNote::Close ? "  (close name)" : "");
            r << "\n";
        }
    }
    return r.str();
}

}  // namespace qs
