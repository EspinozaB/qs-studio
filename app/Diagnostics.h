// Check-up: asks the connected QS what it can over MIDI and writes a plain report. Model and OS version against the
// versions QS Studio has seen, how reliably SysEx comes back and how fast, every Global setting with what it means,
// the User bank at a glance and what is in the card slot. Read-only: it never changes anything on the QS.
#pragma once
#include <JuceHeader.h>

#include <map>
#include <set>

#include "../core/qs_card.h"
#include "../core/qs_program.h"
#include "CardJobs.h"
#include "Log.h"
#include "Session.h"
#include "Shell.h"
#include "Theme.h"

namespace diag {

// OS versions seen in the firmware files QS Studio's testers sent (the update files themselves are never shipped).
struct KnownOs { const char* model; const char* versions; };
inline const KnownOs kKnownOs[] = {
    {"QS6", "1.00, 2.00"},
    {"QS7", "1.02"},
    {"QS8", "1.02"},
};

inline juce::String signedText(int raw8) { const int v = raw8 >= 128 ? raw8 - 256 : raw8; return (v > 0 ? "+" : "") + juce::String(v); }

// Every Global parameter (spec: GLOBAL DATA FORMAT), with a note when a value explains a common problem.
inline juce::StringArray describeGlobals(const qs::Bytes& g, const juce::String& model) {
    juce::StringArray out;
    if (g.size() < 20) return out;
    const bool isQsr = model.startsWith("QSR");
    auto line = [&out](const juce::String& name, const juce::String& value, const juce::String& note = {}) {
        out.add("  " + name.paddedRight(' ', 26) + value + (note.isNotEmpty() ? "   <- " + note : juce::String()));
    };
    const int transpose = int(int8_t(g[1])), fine = int(int8_t(g[2])), kbdTranspose = int(int8_t(g[5]));
    line("Pitch transpose", signedText(g[1]), transpose != 0 ? "everything plays transposed" : "");
    line("Pitch fine tune", signedText(g[2]), std::abs(fine) > 10 ? "out of tune with other instruments" : "");
    if (!isQsr) {
        line("Keyboard scaling", juce::String(g[3] & 0x7F));
        static const char* curves[] = {"Weighted", "Plastic", "Maximum"};  // QS6 manual, Global page 3
        line("Keyboard curve", (g[4] & 3) < 3 ? curves[g[4] & 3] : "?");
        line("Keyboard transpose", signedText(g[5]), kbdTranspose != 0 ? "the keys play transposed" : "");
        const int mode = g[6] & 0x1F;
        line("Keyboard mode", mode == 0 ? "Normal" : juce::String(mode), mode != 0 ? "not Normal: the keys may not play the synth" : "");
    }
    for (int i = 0; i < 4; ++i) line("Controller " + juce::String::charToString(juce::juce_wchar('A' + i)) + " number", "CC " + juce::String(g[size_t(7 + i)] & 0x7F));
    line("Pedal 1 controller", "CC " + juce::String(g[11] & 0x7F));
    line("Pedal 2 controller", "CC " + juce::String(g[12] & 0x7F));
    const int ps = g[13] & 0x1F;
    line("MIDI program select", ps == 0 ? "Off" : ps == 1 ? "On" : "On, channel " + juce::String(ps - 1) + " only",
         ps == 0 ? "QS Studio can't change programs: set it On (Global Settings page)" : "");
    if (!model.startsWith("QS6")) line("Clock", (g[15] & 1) ? "External" : "Internal");
    if (isQsr) line("Mix group channel", (g[16] & 0x1F) == 0 ? "Off" : juce::String(g[16] & 0x1F));
    line("General MIDI", (g[17] & 1) ? "On" : "Off", (g[17] & 1) ? "GM mode: the User banks and mixes aren't in use" : "");
    line("A-D controller reset", (g[18] & 1) ? "On" : "Off");
    static const char* adModes[] = {"Local", "MIDI", "Both", "?"};  // the QS's own screen text (OS 2.00)
    line("A-D controller mode", adModes[g[19] & 3]);
    return out;
}

class CheckupJob : public juce::Thread {
public:
    CheckupJob(Session& s, bool readUser, std::function<void(juce::String)> line, std::function<void()> done)
        : juce::Thread("QS check-up"), session(s), readUserBank(readUser), say(std::move(line)), finished(std::move(done)) {}
    ~CheckupJob() override { stopThread(4000); }

    void run() override {
        MidiLink::JobScope scope(session.midi);
        auto& midi = session.midi;
        say("QS Studio " + juce::String(ProjectInfo::versionString) + " check-up, " + juce::Time::getCurrentTime().toString(true, true));
        say("MIDI in: " + midi.inName() + "   out: " + midi.outName());

        // 1. Who is there.
        say("\n1. THE SYNTH");
        midi.clearQueue();
        midi.send(qs::deviceInquiry());
        juce::String model;
        if (auto r = midi.waitFor([](const qs::Bytes& m) { return qs::parseDeviceReply(m).has_value(); }, 2500, this)) {
            const auto d = *qs::parseDeviceReply(*r);
            model = d.model;
            say("  Model: " + juce::String(d.model) + "   OS version: " + juce::String(d.version));
            juce::String seen;
            for (const auto& k : kKnownOs)
                if (juce::String(d.model).upToFirstOccurrenceOf(".", false, false) == k.model) seen = k.versions;
            if (seen.isNotEmpty())
                say("  OS versions QS Studio has seen for the " + juce::String(d.model).upToFirstOccurrenceOf(".", false, false) + ": " + seen +
                    (seen.contains(juce::String(d.version)) ? "" : "   (yours isn't one of them: please send this report)"));
            else
                say("  QS Studio hasn't seen OS files for this model yet.");
        } else {
            say("  No answer to the device inquiry in 2.5 s.");
            say("  Check: both MIDI cables (QS OUT to interface IN, interface OUT to QS IN), the ports on the Connection page,");
            say("  and Global > I/O on the QS set to MIDI (not Serial). Stopping here.");
            finish();
            return;
        }
        if (threadShouldExit()) return finish();

        // 2. SysEx round trips.
        say("\n2. MIDI LINK (10 Global requests, timed)");
        int answered = 0;
        double total = 0, worst = 0, best = 1e9;
        qs::Bytes globals;
        for (int i = 0; i < 10 && !threadShouldExit(); ++i) {
            midi.clearQueue();
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            midi.send(qs::globalRequest());
            if (auto r = midi.waitFor([](const qs::Bytes& m) { return qs::opcodeOf(m) == qs::GlobalDump; }, 1500, this)) {
                const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
                ++answered;
                total += ms;
                worst = std::max(worst, ms);
                best = std::min(best, ms);
                if (r->size() >= 7 + qs::packedSize(qs::kGlobalBytes)) globals = qs::unpack7(&(*r)[7], qs::packedSize(qs::kGlobalBytes), qs::kGlobalBytes);
            }
            juce::Thread::sleep(session.paced(40));
        }
        say("  Answered " + juce::String(answered) + " of 10." +
            (answered ? "  Reply time " + juce::String(juce::roundToInt(best)) + " to " + juce::String(juce::roundToInt(worst)) + " ms, average " + juce::String(juce::roundToInt(total / answered)) + " ms." : ""));
        const double wire = MidiLink::wireSeconds(7 + 7 + qs::packedSize(qs::kGlobalBytes) + 1) * 1000.0;
        say("  (The MIDI cable alone needs about " + juce::String(juce::roundToInt(wire)) + " ms for a request and its answer.)");
        if (answered < 10) say("  Some requests got no answer: try Transfer pace Slow on the Connection page, or another MIDI interface.");
        else if (answered && worst > 400) say("  Slow answers: the interface or its driver is adding delay. Transfers will be slower but work.");
        else say("  Good: SysEx works both ways.");
        if (threadShouldExit()) return finish();

        // 3. Globals.
        say("\n3. GLOBAL SETTINGS");
        if (globals.empty()) say("  Couldn't read them.");
        else for (auto& l : describeGlobals(globals, model)) say(l);

        // 4. The program it is playing.
        say("\n4. EDIT BUFFER");
        midi.clearQueue();
        midi.send(qs::editProgramRequest(0));
        if (auto r = midi.waitFor([](const qs::Bytes& m) { return qs::opcodeOf(m) == qs::EditProgramDump; }, 2000, this)) {
            if (auto p = qs::Program::fromDump(*r)) say("  Program mode edit buffer: \"" + juce::String(p->name()).trimEnd() + "\"");
        } else {
            say("  No answer (the QS answers this only in Program mode).");
        }
        if (threadShouldExit()) return finish();

        // 5. User bank.
        if (readUserBank) userBank();
        if (threadShouldExit()) return finish();

        // 6. Card slot.
        say("\n6. CARD SLOT");
        midi.clearQueue();
        auto b0 = cardjobs::readBlock(midi, 0, 0, this);
        using K = cardjobs::BlockResult;
        if (b0.kind == K::Nack) say("  The QS answered: " + juce::String(qs::nackText(b0.nackCode)) + " (usually: no card, or a RAM program card).");
        else if (b0.kind != K::Ok && b0.kind != K::BadChecksum) say("  No answer to a card read.");
        else if (qs::looksLikeInternalRom(b0.data)) say("  Card reads return the built-in sample ROM on this unit (as on the QSR), so the slot can't be checked this way.");
        else if (qs::looksLikeCard(b0.data)) {
            auto b64 = cardjobs::readBlock(midi, 0, 64, this);  // byte 0x10000: the sample header, on a sample card
            if ((b64.kind == K::Ok || b64.kind == K::BadChecksum) && b64.data.size() > 24 && b64.data[0] == 0x00 && b64.data[1] == 0x70)
                say("  A sample card: " + juce::String(std::string(reinterpret_cast<const char*>(&b64.data[10]), 14)).trimEnd() + ", " +
                    juce::String(b64.data[8] << 8 | b64.data[9]) + " voices.");
            else
                say("  A program card (programs and mixes, no samples), for example a RAM512. Its banks show as Card 1-8 on Programs.");
        } else say("  Something answered, but it isn't a sample card header (a blank FLASH card or a RAM card).");
        if (b0.kind == K::BadChecksum) say("  One block came back with a bad checksum: card reads may need Transfer pace Slow.");

        say("\n7. NOTES");
        say("  The QS has a factory test menu (EPROM, SRAM, MIDI, PC I/O, effects DRAM, sound ROM, switches, pots, audio) in its");
        say("  OS. Alesis didn't publish how to enter it, so QS Studio doesn't send you there.");
        say("  Re-initializing (hold [0] and [3] at power-on on a QS6/7/8, CURSOR on a QSR) resets Globals and clears the edit");
        say("  buffers; back up first (Backup & Restore). It fixes most odd behaviour after a battery change.");
        finish();
    }

private:
    void userBank() {
        say("\n5. USER BANK (reading 128 programs)");
        auto& midi = session.midi;
        const auto& G = qs::keyboardParam(qs::findKeyboardParam("Sample group"));
        int got = 0, drums = 0, cardPrograms = 0, silent = 0;
        std::map<juce::String, int> groups;
        std::map<juce::String, int> names;
        for (int p = 0; p < 128 && !threadShouldExit(); ++p) {
            midi.clearQueue();
            midi.send({0xF0, 0x00, 0x00, 0x0E, 0x0E, qs::UserProgramRequest, uint8_t(p), 0xF7});
            auto r = midi.waitFor([](const qs::Bytes& m) { return qs::opcodeOf(m) == qs::UserProgramDump; }, 1500, this);
            if (!r) continue;
            auto prog = qs::Program::fromDump(*r);
            if (!prog) continue;
            ++got;
            ++names[juce::String(prog->name()).trim()];
            bool any = false, usesCard = false;
            for (int s = 0; s < qs::kSoundCount; ++s) {
                if (!prog->isEnabled(s)) continue;
                any = true;
                if (prog->isDrum(s)) { ++drums; continue; }
                const int g = prog->raw(s, G);
                if (g >= 16 && g < 24) usesCard = true;
                ++groups[g < 16 ? juce::String(qs::sampleGroupName(g)) : g < 24 ? "Card group " + juce::String(g - 15) : "group " + juce::String(g)];
            }
            if (!any) ++silent;
            if (usesCard) ++cardPrograms;
            if (p % 16 == 15) say("  ..." + juce::String(p + 1));
        }
        say("  Read " + juce::String(got) + " of 128." + (got < 128 ? " Missing ones timed out: try Transfer pace Slow." : ""));
        say("  Programs using a card sample: " + juce::String(cardPrograms) + (cardPrograms ? " (they play silent or wrong without that card)" : ""));
        say("  Drum sounds: " + juce::String(drums) + "   programs with every sound off: " + juce::String(silent));
        int dup = 0;
        for (auto& [n, c] : names) if (c > 1) dup += c - 1;
        if (dup) say("  Repeated names: " + juce::String(dup));
        std::vector<std::pair<int, juce::String>> top;
        for (auto& [n, c] : groups) top.push_back({c, n});
        std::sort(top.rbegin(), top.rend());
        juce::StringArray t;
        for (size_t i = 0; i < top.size() && i < 6; ++i) t.add(top[i].second + " " + juce::String(top[i].first));
        if (!t.isEmpty()) say("  Most used sample groups (keyboard sounds): " + t.joinIntoString(", "));
    }

    void finish() {
        say("\nDone.");
        juce::MessageManager::callAsync(finished);
    }

    Session& session;
    bool readUserBank;
    std::function<void(juce::String)> say;
    std::function<void()> finished;
};

}  // namespace diag

class DiagnosticsPage : public juce::Component {
public:
    explicit DiagnosticsPage(Session& s) : session(s) {
        addAndMakeVisible(introSec);
        for (juce::Component* c : std::initializer_list<juce::Component*>{&runBtn, &userToggle, &saveBtn, &report}) addAndMakeVisible(c);
        userToggle.setToggleState(true, juce::dontSendNotification);
        userToggle.setTooltip("Also reads the 128 User programs to count card samples, drums and silent programs (about 10 seconds).");
        report.setMultiLine(true);
        report.setReadOnly(true);
        report.setScrollbarsShown(true);
        report.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
        report.setText("Connect the QS (Connection page), then click Run the check-up.\n\n"
                       "It only asks questions: nothing on the QS changes.");
        runBtn.onClick = [this] { run(); };
        saveBtn.onClick = [this] { save(); };
        saveBtn.setEnabled(false);
        saveBtn.setTooltip("Saves this report as a text file in QS Studio Data/Logs, to attach to a bug report.");
    }
    ~DiagnosticsPage() override { job.reset(); }

    void resized() override {
        auto r = getLocalBounds().reduced(14);
        const int h = 48 + introSec.textHeight(r.getWidth() - 32) + 44;
        auto top = r.removeFromTop(h);
        introSec.setBounds(top);
        auto row = top.withTrimmedTop(h - 50).reduced(16, 0).removeFromTop(34);
        runBtn.setBounds(row.removeFromLeft(170));
        row.removeFromLeft(10);
        userToggle.setBounds(row.removeFromLeft(240));
        saveBtn.setBounds(row.removeFromRight(150));
        r.removeFromTop(12);
        report.setBounds(r);
    }
    void paint(juce::Graphics& g) override { g.fillAll(theme::bg); }

private:
    void run() {
        if (!session.midi.isOpen()) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Not connected", "Connect to the QS on the Connection page first.");
            return;
        }
        if (job && job->isThreadRunning()) return;
        report.clear();
        runBtn.setEnabled(false);
        saveBtn.setEnabled(false);
        qslog::action("Check-up started");
        juce::Component::SafePointer<DiagnosticsPage> safe(this);
        job = std::make_unique<diag::CheckupJob>(
            session, userToggle.getToggleState(),
            [safe](juce::String line) {
                juce::MessageManager::callAsync([safe, line] {
                    if (!safe) return;
                    safe->report.moveCaretToEnd();
                    safe->report.insertTextAtCaret(line + "\n");
                });
            },
            [safe] {
                if (!safe) return;
                safe->runBtn.setEnabled(true);
                safe->saveBtn.setEnabled(true);
                qslog::action("Check-up finished");
            });
        job->startThread();
    }

    void save() {
        auto dir = settingsDir().getChildFile("Logs");
        dir.createDirectory();
        auto f = dir.getNonexistentChildFile("checkup-" + juce::Time::getCurrentTime().formatted("%Y-%m-%d"), ".txt");
        f.replaceWithText(report.getText());
        f.revealToUser();
    }

    Session& session;
    Section introSec{"Check-up", "Asks the connected QS everything QS Studio can over MIDI and explains the answers: model and OS version, "
                                 "whether SysEx comes back reliably and how fast, every Global setting (with warnings for the ones that "
                                 "cause silence or ignored program changes), the User bank and the card slot. It changes nothing."};
    juce::TextButton runBtn{"Run the check-up"}, saveBtn{"Save the report"};
    juce::ToggleButton userToggle{"Include the User bank (10 s)"};
    juce::TextEditor report;
    std::unique_ptr<diag::CheckupJob> job;
};
