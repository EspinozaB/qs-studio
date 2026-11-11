#include "LibraryTab.h"

#include "../core/qs_samples.h"

namespace {

void alert(const juce::String& title, const juce::String& msg) {
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, title, msg);
}

juce::String bankTitle(int b) { return kBanks[size_t(b)].title; }

// Every read from the synth leaves a short log in QS Studio Data/Logs, to help when something didn't come through.
void writeReadLog(const juce::String& what, size_t bankCount, const juce::StringArray& lines, const juce::StringArray& problems) {
    auto dir = settingsDir().getChildFile("Logs");
    dir.createDirectory();
    auto f = dir.getChildFile("read-" + juce::Time::getCurrentTime().formatted("%Y-%m-%d") + ".txt");
    juce::String text = "\n[" + juce::Time::getCurrentTime().formatted("%H:%M:%S") + "] read " + what + " from " + juce::String(int(bankCount)) +
                        " bank(s), QS Studio " + versionLabel() + "\n";
    for (auto& l : lines) text << "  " << l << "\n";
    for (auto& p : problems) text << "  problem: " << p << "\n";
    f.appendText(text);
}

// Looks for a card: notes the edit buffer, asks for card bank 1 program 0, and sees whether the QS changed program.
// With no card the QS ignores the bank change and nothing changes.
class CardProbeJob : public juce::ThreadWithProgressWindow {
public:
    CardProbeJob(Session& s, std::function<void(int)> done)  // 1 = card, 0 = none, -1 = no answer
        : ThreadWithProgressWindow("Looking for a card in the QS", false, false), session(s), onDone(std::move(done)) {}
    void run() override {
        auto& midi = session.midi;
        if (!session.programModeSent) {
            midi.send(qs::modeSelect(0));
            session.programModeSent = true;
            wait(100);
        }
        auto editBuffer = [&]() -> std::optional<qs::Bytes> {
            midi.clearQueue();
            midi.send(qs::editProgramRequest(0));
            if (auto r = midi.waitFor([](const qs::Bytes& m) { return qs::opcodeOf(m) == qs::EditProgramDump; }, 1500, this))
                return qs::Bytes(r->begin() + 7, r->end());
            return std::nullopt;
        };
        const auto before = editBuffer();
        midi.sendMidi(juce::MidiMessage::controllerEvent(session.channel, 0, kBanks[size_t(kFirstCardBank)].cc0));
        midi.sendMidi(juce::MidiMessage::programChange(session.channel, 0));
        wait(400);
        const auto after = editBuffer();
        result = !before || !after ? -1 : (*before != *after ? 1 : 0);
        if (result == 1) {
            session.currentBank = kFirstCardBank;
            session.currentProgram = 0;
        }
    }
    void threadComplete(bool) override {
        if (onDone) onDone(result);
        delete this;
    }

private:
    Session& session;
    int result = -1;
    std::function<void(int)> onDone;
};

// Picks what to read from the synth: which banks, and programs and/or mixes.
class ReadChooser : public juce::Component {
public:
    ReadChooser(int currentBank, bool mixes, bool cardBanks, std::function<void(std::vector<int>, bool, bool)> go) : onGo(std::move(go)) {
        intro.setText("QS Studio asks the synth for each program or mix and keeps a copy here, so names and programs show up "
                      "even when the synth is off. Nothing on the synth is changed.",
                      juce::dontSendNotification);
        intro.setColour(juce::Label::textColourId, theme::dim);
        intro.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(intro);
        const int n = cardBanks ? int(kBanks.size()) : kFirstCardBank;
        for (int b = 0; b < n; ++b) {
            auto* t = bankToggles.add(new juce::ToggleButton(bankTitle(b)));
            t->setToggleState(b == currentBank, juce::dontSendNotification);
            t->onClick = [this] { updateEstimate(); };
            addAndMakeVisible(t);
        }
        progs.setToggleState(!mixes, juce::dontSendNotification);
        mixesT.setToggleState(mixes, juce::dontSendNotification);
        for (auto* t : {&progs, &mixesT}) {
            t->onClick = [this] { updateEstimate(); };
            addAndMakeVisible(t);
        }
        for (auto* b : {&allMain, &none, &readBtn, &cancelBtn}) addAndMakeVisible(b);
        allMain.onClick = [this] {
            for (int b = 0; b < bankToggles.size(); ++b) bankToggles[b]->setToggleState(b < kFirstCardBank, juce::dontSendNotification);
            updateEstimate();
        };
        none.onClick = [this] {
            for (auto* t : bankToggles) t->setToggleState(false, juce::dontSendNotification);
            updateEstimate();
        };
        readBtn.onClick = [this] {
            std::vector<int> banks;
            for (int b = 0; b < bankToggles.size(); ++b)
                if (bankToggles[b]->getToggleState()) banks.push_back(b);
            auto go = onGo;
            const bool p = progs.getToggleState(), m = mixesT.getToggleState();
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(0);
            if (go && !banks.empty() && (p || m)) go(banks, p, m);
        };
        cancelBtn.onClick = [this] {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(0);
        };
        estimate.setColour(juce::Label::textColourId, theme::dim);
        addAndMakeVisible(estimate);
        updateEstimate();
        setSize(460, 250 + ((n + 2) / 3) * 30);
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(theme::panel);
        theme::drawCaption(g, {16, banksY, 200, 18}, "Banks", juce::Justification::centredLeft);
        theme::drawCaption(g, {16, whatY, 200, 18}, "What to read", juce::Justification::centredLeft);
    }
    void resized() override {
        auto r = getLocalBounds().reduced(16);
        intro.setBounds(r.removeFromTop(44));
        banksY = r.getY() + 2;
        r.removeFromTop(24);
        const int rows = (bankToggles.size() + 2) / 3;
        auto grid = r.removeFromTop(rows * 30);
        for (int i = 0; i < bankToggles.size(); ++i)
            bankToggles[i]->setBounds(grid.getX() + (i % 3) * (grid.getWidth() / 3), grid.getY() + (i / 3) * 30, grid.getWidth() / 3, 28);
        auto row = r.removeFromTop(30);
        allMain.setBounds(row.removeFromLeft(190));
        row.removeFromLeft(8);
        none.setBounds(row.removeFromLeft(80));
        r.removeFromTop(8);
        whatY = r.getY();
        r.removeFromTop(22);
        row = r.removeFromTop(28);
        progs.setBounds(row.removeFromLeft(140));
        mixesT.setBounds(row.removeFromLeft(140));
        estimate.setBounds(r.removeFromTop(24));
        auto b = r.removeFromBottom(34);
        cancelBtn.setBounds(b.removeFromRight(100));
        b.removeFromRight(8);
        readBtn.setBounds(b.removeFromRight(120));
    }

private:
    void updateEstimate() {
        int banks = 0;
        for (auto* t : bankToggles) banks += t->getToggleState();
        const int seconds = banks * ((progs.getToggleState() ? 40 : 0) + (mixesT.getToggleState() ? 25 : 0));
        estimate.setText(banks == 0 ? "Pick at least one bank." : "About " + juce::String(juce::jmax(1, seconds / 60)) + (seconds < 120 ? " minute." : " minutes."),
                         juce::dontSendNotification);
        readBtn.setEnabled(banks > 0 && (progs.getToggleState() || mixesT.getToggleState()));
    }
    juce::Label intro, estimate;
    int banksY = 0, whatY = 0;
    juce::OwnedArray<juce::ToggleButton> bankToggles;
    juce::ToggleButton progs{"Programs"}, mixesT{"Mixes"};
    juce::TextButton allMain{"User, Presets and GenMIDI"}, none{"None"}, readBtn{"Read"}, cancelBtn{"Cancel"};
    std::function<void(std::vector<int>, bool, bool)> onGo;
};

// Reads every program of one or more banks.
// User: opcode 01 per program (fast). Other banks: program change, then ask for the edit buffer (opcode 03).
// The QS needs a moment to load a program; if the answer is still the previous program, ask again.
class ReadBanksJob : public juce::ThreadWithProgressWindow {
public:
    ReadBanksJob(Session& s, std::vector<int> b, std::function<void(juce::String)> done)
        : ThreadWithProgressWindow("Reading from the QS", true, true), session(s), banks(std::move(b)), onDone(std::move(done)) {}

    void run() override {
        auto& midi = session.midi;
        const int total = int(banks.size()) * 128;
        int doneCount = 0;
        // What the QS has in its edit buffer before we start, so the first program of a bank can be checked against
        // something (a stale answer would otherwise be taken for program 0). Kept across banks for the same reason.
        std::optional<qs::Program> prev;
        if (std::any_of(banks.begin(), banks.end(), [](int b) { return b != 0; })) {
            midi.send(qs::modeSelect(0));
            session.programModeSent = true;
            wait(100);
            midi.clearQueue();
            midi.send(qs::editProgramRequest(0));
            if (auto r = midi.waitFor([](const qs::Bytes& m) { return qs::opcodeOf(m) == qs::EditProgramDump; }, 1500, this))
                prev = qs::Program::fromDump(*r);
        }
        for (int bank : banks) {
            if (threadShouldExit()) break;
            auto& out = results[bank];
            int got = 0, sameAsPrevious = 0, retries = 0;
            for (int p = 0; p < 128 && !threadShouldExit(); ++p) {
                setStatusMessage(bankTitle(bank) + ": program " + juce::String(p + 1) + " of 128");
                std::optional<qs::Program> prog;
                if (bank == 0) {
                    for (int attempt = 0; attempt < 3 && !prog && !threadShouldExit(); ++attempt) {
                        midi.send({0xF0, 0x00, 0x00, 0x0E, 0x0E, qs::UserProgramRequest, uint8_t(p), 0xF7});
                        if (auto r = midi.waitFor([p](const qs::Bytes& m) { return qs::opcodeOf(m) == qs::UserProgramDump && m.size() > 6 && m[6] == p; },
                                                  1500, this))
                            prog = qs::Program::fromDump(*r);
                    }
                } else {
                    midi.sendMidi(juce::MidiMessage::controllerEvent(session.channel, 0, kBanks[size_t(bank)].cc0));
                    midi.sendMidi(juce::MidiMessage::programChange(session.channel, p));
                    wait(60);
                    for (int attempt = 0; attempt < 4 && !threadShouldExit(); ++attempt) {
                        midi.clearQueue();
                        midi.send(qs::editProgramRequest(0));
                        auto r = midi.waitFor([](const qs::Bytes& m) { return qs::opcodeOf(m) == qs::EditProgramDump; }, 1500, this);
                        if (!r) continue;
                        prog = qs::Program::fromDump(*r);
                        // Still the previous program? The QS hadn't switched yet: wait and ask again.
                        if (prog && prev && prog->bytes() == prev->bytes() && attempt < 3) {
                            ++retries;
                            wait(150 + 100 * attempt);
                            continue;
                        }
                        break;
                    }
                    if (prog && prev && prog->bytes() == prev->bytes()) ++sameAsPrevious;
                }
                if (!prog) {
                    if (threadShouldExit()) break;
                    problems.add(got == 0 ? bankTitle(bank) + ": the QS did not answer." + (bank == 0 ? juce::String() : programSelectHint())
                                          : bankTitle(bank) + ": the QS stopped answering at program " + juce::String(p) + ".");
                    break;
                }
                // Every program identical: the QS is ignoring program changes.
                if (p == 3 && sameAsPrevious >= 3) {
                    problems.add(bankTitle(bank) + ": every program came back the same, so the QS isn't switching programs." + programSelectHint());
                    out = {};
                    got = 0;
                    break;
                }
                out[size_t(p)] = prog;
                prev = prog;
                ++got;
                setProgress(double(++doneCount) / total);
            }
            counts[bank] = got;
            log.add(bankTitle(bank) + ": " + juce::String(got) + " of 128 programs, " + juce::String(retries) + " re-asks, " +
                    juce::String(sameAsPrevious) + " identical to the one before");
        }
    }

    void threadComplete(bool) override {
        writeReadLog("programs", banks.size(), log, problems);
        juce::StringArray summary;
        for (int bank : banks) {
            const int got = counts[bank];
            if (got > 0) {
                for (size_t p = 0; p < 128; ++p)
                    if (results[bank][p]) session.library.programs[size_t(bank)][p] = results[bank][p];
                session.library.source[size_t(bank)] = "your " + (session.deviceName.isNotEmpty() ? session.deviceName : juce::String("QS")) +
                                                       " (read " + juce::Time::getCurrentTime().formatted("%Y-%m-%d") + ")";
            }
            summary.add(bankTitle(bank) + ": " + juce::String(got) + " of 128 programs" + (got == 128 ? "" : (got == 0 ? "  (not read)" : "  (incomplete)")));
        }
        session.library.save();
        if (onDone) onDone(summary.joinIntoString("\n") + (problems.isEmpty() ? "" : "\n\n" + problems.joinIntoString("\n")));
        delete this;
    }

private:
    static juce::String programSelectHint() {
        return "\nCheck that Global > MIDI Program Select is ON and the MIDI channel in QS Studio matches the QS. "
               "Card banks only answer when a card with that many banks is inserted.";
    }

    Session& session;
    std::vector<int> banks;
    std::map<int, std::array<std::optional<qs::Program>, 128>> results;
    std::map<int, int> counts;
    juce::StringArray problems, log;
    std::function<void(juce::String)> onDone;
};

// Reads the mixes of one or more banks.
// User: opcode 0F per mix. Other banks: the QS only changes mixes on program changes when Global > MIDI Program Select is
// set to a channel, so this sets it to QS Studio's channel for the read, selects each mix (Mix mode, bank select, program
// change) and asks for the mix edit buffer (0F 100). Program Select and Program mode are put back afterwards.
class ReadMixesJob : public juce::ThreadWithProgressWindow {
public:
    ReadMixesJob(Session& s, std::vector<int> b, std::function<void(juce::String)> done)
        : ThreadWithProgressWindow("Reading mixes from the QS", true, true), session(s), banks(std::move(b)), onDone(std::move(done)) {}

    void run() override {
        auto& midi = session.midi;
        const int total = int(banks.size()) * 100;
        int doneCount = 0;
        const bool needSelect = std::any_of(banks.begin(), banks.end(), [](int b) { return b != 0; });
        if (needSelect) {
            if (!session.globalKnown) {  // we need the current Program Select to put it back
                midi.clearQueue();
                midi.send(qs::globalRequest());
                if (auto r = midi.waitFor([](const qs::Bytes& m) { return qs::opcodeOf(m) == qs::GlobalDump; }, 2000, this))
                    if (auto g = qs::parseGlobalDump(*r)) {
                        session.global = *g;
                        session.globalKnown = true;
                    }
                if (!session.globalKnown) {
                    problems.add("The QS didn't send its Global settings, so mixes in Preset and card banks weren't read.");
                    banks.erase(std::remove_if(banks.begin(), banks.end(), [](int b) { return b != 0; }), banks.end());
                }
            }
            if (session.globalKnown) {
                originalSelect = session.global.programSelect;
                if (originalSelect != session.channel + 1) {
                    session.setProgramSelect(session.channel + 1);
                    changedSelect = true;
                    wait(100);
                }
                midi.send(qs::modeSelect(1));
                session.programModeSent = false;
                session.mixModeSent = true;
                wait(150);
            }
        }
        qs::Bytes prev;  // kept across banks, and seeded with the mix the QS is on, so mix 0 of a bank is checked too
        if (needSelect && session.globalKnown) {
            midi.clearQueue();
            midi.send(qs::mixRequest(100));
            if (auto r = midi.waitFor([](const qs::Bytes& m) { return qs::opcodeOf(m) == qs::NewMixDump || qs::opcodeOf(m) == qs::OldMixDump; }, 1500, this))
                prev = payload(*r);
        }
        for (int bank : banks) {
            if (threadShouldExit()) break;
            auto& out = results[bank];
            int got = 0, same = 0;
            for (int n = 0; n < 100 && !threadShouldExit(); ++n) {
                setStatusMessage(bankTitle(bank) + ": mix " + juce::String(n + 1) + " of 100");
                std::optional<qs::Bytes> msg;
                auto isMix = [](int want) {
                    return [want](const qs::Bytes& m) {
                        const int op = qs::opcodeOf(m);
                        return (op == qs::NewMixDump || op == qs::OldMixDump) && m.size() > 6 && m[6] == want;
                    };
                };
                if (bank == 0) {
                    for (int attempt = 0; attempt < 3 && !msg && !threadShouldExit(); ++attempt) {
                        midi.send(qs::mixRequest(n));
                        msg = midi.waitFor(isMix(n), 1500, this);
                    }
                } else {
                    midi.sendMidi(juce::MidiMessage::controllerEvent(session.channel, 0, kBanks[size_t(bank)].cc0));
                    midi.sendMidi(juce::MidiMessage::programChange(session.channel, n));
                    wait(80);
                    for (int attempt = 0; attempt < 4 && !threadShouldExit(); ++attempt) {
                        midi.clearQueue();
                        midi.send(qs::mixRequest(100));
                        msg = midi.waitFor(isMix(100), 1500, this);
                        if (!msg) continue;
                        if (!prev.empty() && payload(*msg) == prev && attempt < 3) {  // not switched yet
                            wait(150 + 100 * attempt);
                            continue;
                        }
                        break;
                    }
                    if (msg && n > 0 && payload(*msg) == prev) ++same;
                }
                if (!msg) {
                    if (threadShouldExit()) break;
                    problems.add(got == 0 ? bankTitle(bank) + " mixes: the QS did not answer."
                                          : bankTitle(bank) + " mixes: the QS stopped answering at mix " + juce::String(n) + ".");
                    break;
                }
                if (n == 3 && same >= 3) {
                    problems.add(bankTitle(bank) + " mixes: every mix came back the same, so the QS isn't switching mixes. "
                                 "Check that the MIDI channel in QS Studio matches the QS.");
                    out = {};
                    got = 0;
                    break;
                }
                out[size_t(n)] = *msg;
                prev = payload(*msg);
                ++got;
                setProgress(double(++doneCount) / total);
            }
            counts[bank] = got;
            log.add(bankTitle(bank) + ": " + juce::String(got) + " of 100 mixes, " + juce::String(same) + " identical to the one before");
        }
        // Put the QS back the way it was.
        if (changedSelect) {
            session.setProgramSelect(originalSelect);
            wait(100);
        }
        if (needSelect) {
            midi.send(qs::modeSelect(0));
            session.programModeSent = true;
            session.mixModeSent = false;
        }
    }

    void threadComplete(bool) override {
        writeReadLog("mixes", banks.size(), log, problems);
        juce::StringArray summary;
        for (int bank : banks) {
            const int got = counts[bank];
            if (got > 0) {
                for (int n = 0; n < 100; ++n)
                    if (!results[bank][size_t(n)].empty()) session.library.setMix(bank, n, results[bank][size_t(n)]);
                session.library.mixSource[size_t(bank)] = "your " + (session.deviceName.isNotEmpty() ? session.deviceName : juce::String("QS")) +
                                                          " (read " + juce::Time::getCurrentTime().formatted("%Y-%m-%d") + ")";
            }
            summary.add(bankTitle(bank) + ": " + juce::String(got) + " of 100 mixes" + (got == 100 ? "" : (got == 0 ? "  (not read)" : "  (incomplete)")));
        }
        if (changedSelect)
            problems.add("MIDI Program Select was set to channel " + juce::String(session.channel) + " during the read and has been put back.");
        session.library.save();
        if (onDone) onDone(summary.joinIntoString("\n") + (problems.isEmpty() ? "" : "\n\n" + problems.joinIntoString("\n")));
        delete this;
    }

private:
    static qs::Bytes payload(const qs::Bytes& m) { return m.size() > 7 ? qs::Bytes(m.begin() + 7, m.end()) : m; }

    Session& session;
    std::vector<int> banks;
    std::map<int, std::array<qs::Bytes, 100>> results;
    std::map<int, int> counts;
    juce::StringArray problems, log;
    int originalSelect = 1;
    bool changedSelect = false;
    std::function<void(juce::String)> onDone;
};

// Sends a list of SysEx messages, paced so the QS keeps up.
void sendPaced(Session& s, const std::vector<qs::Bytes>& msgs, juce::ThreadWithProgressWindow* t) {
    for (size_t i = 0; i < msgs.size() && !t->threadShouldExit(); ++i) {
        s.midi.send(msgs[i]);
        t->setProgress(double(i + 1) / double(msgs.size()));
        juce::Thread::sleep(int(MidiLink::wireSeconds(msgs[i].size()) * 1000) + 20);
    }
}

// Step 1 of "copy a bank to the card": back up the User bank (Send All), then load the chosen data into User.
class CardCopyPrepJob : public juce::ThreadWithProgressWindow {
public:
    CardCopyPrepJob(Session& s, std::vector<qs::Bytes> toSend, juce::String what, juce::File backupFile, std::function<void(bool, juce::String)> done)
        : ThreadWithProgressWindow("Copy to card: preparing the QS", true, true), session(s), msgs(std::move(toSend)), label(std::move(what)),
          backup(backupFile), onDone(std::move(done)) {}

    void run() override {
        auto& midi = session.midi;
        setStatusMessage("Backing up your User bank (Send All)...");
        midi.clearQueue();
        midi.send({0xF0, 0x00, 0x00, 0x0E, 0x0E, qs::AllDumpRequest, 0xF7});
        qs::Bytes all;
        int count = 0;
        while (!threadShouldExit()) {
            auto m = midi.waitFor([](const qs::Bytes& x) { return qs::opcodeOf(x) >= 0; }, count == 0 ? 5000 : 2500, this);
            if (!m) break;
            all.insert(all.end(), m->begin(), m->end());
            ++count;
            setProgress(juce::jmin(0.99, count / 357.0));
        }
        if (count < 357) {
            message = "The User bank backup was incomplete (" + juce::String(count) + " of 357 parts), so nothing was changed. "
                      "Check the MIDI connection and try again.";
            return;
        }
        backup.getParentDirectory().createDirectory();
        backup.replaceWithData(all.data(), all.size());

        setStatusMessage("Loading " + label + " into the User bank...");
        sendPaced(session, msgs, this);
        ok = !threadShouldExit();
        message = ok ? "Your User bank is backed up to " + backup.getFileName() + " and now holds " + label + "."
                     : "Cancelled. Your backup is in " + backup.getFullPathName() + " (send it with the SysEx Librarian to restore).";
    }

    void threadComplete(bool) override {
        if (onDone) onDone(ok, message);
        delete this;
    }

private:
    Session& session;
    std::vector<qs::Bytes> msgs;
    juce::String label;
    juce::File backup;
    bool ok = false;
    juce::String message;
    std::function<void(bool, juce::String)> onDone;
};

// Restores a saved .syx (the User bank backup) to the QS.
class RestoreJob : public juce::ThreadWithProgressWindow {
public:
    RestoreJob(Session& s, juce::File f, std::function<void(juce::String)> done)
        : ThreadWithProgressWindow("Restoring your User bank", true, false), session(s), file(f), onDone(std::move(done)) {}
    void run() override {
        juce::MemoryBlock mb;
        if (!file.loadFileAsData(mb)) { message = "Couldn't read " + file.getFullPathName(); return; }
        auto ptr = static_cast<const uint8_t*>(mb.getData());
        sendPaced(session, qs::splitSysex(qs::Bytes(ptr, ptr + mb.getSize())), this);
        message = "Your User bank has been restored from " + file.getFileName() + ".";
    }
    void threadComplete(bool) override {
        if (onDone) onDone(message);
        delete this;
    }

private:
    Session& session;
    juce::File file;
    juce::String message;
    std::function<void(juce::String)> onDone;
};

}  // namespace

LibraryTab::LibraryTab(Session& s) : session(s) {
    FrontPanel::Actions act;
    act.selectProgram = [this](int p) { selectProgram(p); };
    act.stepBank = [this](int d) {
        const int b = juce::jlimit(0, int(kBanks.size()) - 1, bank + d);
        if (b == bank) return;
        const int p = selectedProgram();
        showBank(b);
        selectProgram(p);
    };
    act.edit = [this] { openInEditor(selectedProgram()); };
    act.store = [this] { storeToUser(); };
    act.currentProgram = [this] { return selectedProgram(); };
    act.highestNumber = [this] { return mixView ? 99 : 127; };
    act.stepChannel = [this](int d) {
        session.setChannel(juce::jlimit(1, 16, session.channel + d));
        if (session.channelChanged) session.channelChanged();
        updateLcd();
    };
    act.setMode = [this](bool mix) { setMixView(mix, true); };
    act.showCards = [this] { showBank(kFirstCardBank + cardBankBox.getSelectedItemIndex()); };
    act.refreshDisplay = [this] { updateLcd(); };
    panel = std::make_unique<FrontPanel>(session.volume, act);
    addAndMakeVisible(*panel);
    const char* mainBanks[] = {"User", "Preset 1", "Preset 2", "Preset 3", "GenMIDI", "Card"};
    for (int b = 0; b < 6; ++b) {
        auto* btn = bankButtons.add(new juce::TextButton(mainBanks[b]));
        btn->onClick = [this, b] {
            const int target = b < 5 ? b : kFirstCardBank + cardBankBox.getSelectedId() - 1;
            if (b == 5 && session.cardState == Session::CardState::Unknown && session.midi.isOpen() && !cardAnyway.getToggleState())
                probeCard([this, target] { showBank(target); });  // first visit: check there's a card
            else
                showBank(target);
        };
        addAndMakeVisible(btn);
    }
    bankButtons[5]->setTooltip("Banks on the inserted card(s). A RAM512 has 8 banks; a sound QCard usually has 1.");
    for (int i = 1; i <= 11; ++i) cardBankBox.addItem("Card bank " + juce::String(i), i);
    cardBankBox.setSelectedId(1, juce::dontSendNotification);
    cardBankBox.onChange = [this] { showBank(kFirstCardBank + cardBankBox.getSelectedId() - 1); };
    cardBankBox.setTooltip("Card banks are numbered across both slots. With no card inserted the QS ignores these and stays where it was.");
    addAndMakeVisible(cardBankBox);

    search.setTextToShowWhenEmpty("Search names...", theme::dim);
    search.onTextChange = [this] { rebuildRows(); };
    search.setFont(theme::uiFont(15.0f));
    search.setIndents(8, 7);
    addAndMakeVisible(search);

    list.setRowHeight(26);
    list.setColour(juce::ListBox::backgroundColourId, theme::panel);
    list.setOutlineThickness(0);
    list.setWantsKeyboardFocus(true);
    addAndMakeVisible(list);

    for (juce::Component* c : std::initializer_list<juce::Component*>{&prevBtn, &nextBtn, &readBtn, &importFolderBtn, &importOneBtn, &loadNote,
                                                                    &editBtn, &storeBtn, &factoryLink, &sourceLabel})
        addAndMakeVisible(c);
    addChildComponent(copyToCardBtn);
    addChildComponent(sendBtn);
    addChildComponent(cardCheckBtn);
    addChildComponent(cardAnyway);
    readBtn.setButtonText("Read from the synth...");
    importOneBtn.setButtonText("Load one .syx file into the list...");
    importFolderBtn.setButtonText("Load a folder of .syx files...");
    editBtn.setButtonText("Edit this program");
    storeBtn.setButtonText("Copy it to a User slot on the QS...");
    sendBtn.setButtonText("Hear the file's version (not stored)");
    copyToCardBtn.setButtonText("Put a whole bank on this card...");
    loadNote.setText("Loading a file only fills this list. Nothing is sent to the synth.", juce::dontSendNotification);
    loadNote.setFont(theme::uiFont(12.5f));
    loadNote.setColour(juce::Label::textColourId, theme::dim);
    loadNote.setJustificationType(juce::Justification::topLeft);
    cardCheckBtn.onClick = [this] { probeCard(nullptr); };
    cardCheckBtn.setTooltip("Asks the QS whether a card is in. Power the QS off before inserting a card.");
    cardAnyway.setToggleState(settings().getBoolValue("cardsWithoutCheck", false), juce::dontSendNotification);
    cardAnyway.setTooltip("For cards QS Studio doesn't recognise, or setups where the check doesn't work. The QS ignores card banks with no card in.");
    cardAnyway.onClick = [this] {
        settings().setValue("cardsWithoutCheck", cardAnyway.getToggleState());
        updateCardButtons();
    };
    convertBtn.setTooltip("Rewrites a bank file (for example your QS6 Send All backup) so each program points at the same-named samples "
                          "on the other model. Saves a new .syx and a report of every change and every sample with no match.");
    copyToCardBtn.onClick = [this] { copyBankToCard(); };
    copyToCardBtn.setTooltip("Puts a whole bank (for example your QS6 User bank) onto this card bank. QS Studio backs up your User bank, "
                             "loads the bank you pick, tells you which buttons to press on the QS, checks the card, then restores your User bank.");
    prevBtn.onClick = [this] { step(-1); };
    nextBtn.onClick = [this] { step(1); };
    readBtn.onClick = [this] { chooseWhatToRead(); };
    for (auto* b : {&progViewBtn, &mixViewBtn}) {
        b->setClickingTogglesState(false);
        b->setConnectedEdges(b == &progViewBtn ? juce::Button::ConnectedOnRight : juce::Button::ConnectedOnLeft);
        addAndMakeVisible(b);
    }
    progViewBtn.onClick = [this] { setMixView(false, true); };
    mixViewBtn.onClick = [this] { setMixView(true, true); };
    progViewBtn.setTooltip("List the 128 programs of this bank (same as PROGRAM on the panel)");
    mixViewBtn.setTooltip("List the 100 mixes of this bank (same as MIX on the panel)");
    progViewBtn.setToggleState(true, juce::dontSendNotification);
    importFolderBtn.onClick = [this] { importFolder(); };
    importOneBtn.onClick = [this] { importOneSyx(); };
    editBtn.onClick = [this] { openInEditor(selectedProgram()); };
    sendBtn.onClick = [this] { sendCachedToEditBuffer(); };
    storeBtn.onClick = [this] { storeToUser(); };

    prevBtn.setTooltip("Previous program");
    nextBtn.setTooltip("Next program");
    readBtn.setTooltip("Choose banks, then QS Studio asks the synth for their programs and/or mixes and keeps a copy here. "
                       "About 40 seconds a bank for programs. You only need to do it once.");
    readAllBtn.setTooltip("Reads User, Preset 1-3 and GenMIDI in one go (a few minutes). Nothing on the QS is changed.");
    importFolderBtn.setTooltip("Fills banks here from .syx files in a folder (for example the Alesis factory banks). "
                               "You choose which file goes to which bank. Only QS Studio's list changes; nothing is sent to the QS.");
    importOneBtn.setTooltip("Fills the bank shown here from one .syx file, so you can browse its names and open its programs in the editor. "
                            "Only QS Studio's list changes; nothing is sent to the QS. To write a bank to the QS, use Backup & Restore.");
    editBtn.setTooltip("Selects the program on the QS and opens it in the Program Editor (Enter or double-click does the same).");
    sendBtn.setTooltip("This program came from a .syx file, so the synth's own program in this slot may be different. This plays the file's "
                       "version on the synth without storing it; picking another program puts things back.");
    storeBtn.setTooltip("Saves this program permanently into one of the QS's 128 User program slots, replacing what was there. "
                        "Use it to copy a Preset or card program into your User bank.");
    factoryLink.setColour(juce::HyperlinkButton::textColourId, theme::accent);
    factoryLink.setFont(theme::uiFont(13.0f), false, juce::Justification::centredLeft);
    sourceLabel.setFont(theme::uiFont(13.0f));
    sourceLabel.setColour(juce::Label::textColourId, theme::dim);
    sourceLabel.setJustificationType(juce::Justification::topLeft);

    session.currentProgramChanged = [this](int, int) { updateLcd(); };
    showBank(juce::jlimit(0, int(kBanks.size()) - 1, settings().getIntValue("libraryBank", 0)));
}

LibraryTab::~LibraryTab() { session.currentProgramChanged = nullptr; }

void LibraryTab::showBank(int b) {
    bank = b;
    settings().setValue("libraryBank", b);
    if (b >= kFirstCardBank) cardBankBox.setSelectedId(b - kFirstCardBank + 1, juce::dontSendNotification);
    for (int i = 0; i < bankButtons.size(); ++i)
        bankButtons[i]->setToggleState(i < 5 ? i == b : b >= kFirstCardBank, juce::dontSendNotification);
    copyToCardBtn.setVisible(b >= kFirstCardBank);
    updateCardButtons();
    rebuildRows();
    updateLcd();
}

void LibraryTab::refresh() {
    rebuildRows();
    updateLcd();
    updateCardButtons();
}

void LibraryTab::rebuildRows() {
    const auto q = search.getText().trim();
    rows.clear();
    for (int p = 0; p < count(); ++p)
        if (q.isEmpty() || nameAt(p).containsIgnoreCase(q) || juce::String(p) == q) rows.push_back(p);
    list.updateContent();
    list.repaint();
    suppressSend = true;
    list.deselectAllRows();
    if (currentBankShown() == bank)
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i] == currentNumber()) list.selectRow(int(i));
    suppressSend = false;

    const int n = mixView ? session.library.countMixesRead(bank) : session.library.countRead(bank);
    const auto& src = mixView ? session.library.mixSource[size_t(bank)] : session.library.source[size_t(bank)];
    const juce::String what = mixView ? "mixes" : "programs";
    const auto readLabel = "\"Read " + bankTitle(bank) + (mixView ? " mixes" : "") + " from QS\"";
    juce::String text;
    if (n == 0 && session.library.hasBuiltInNames(bank))
        text = juce::String("Factory ") + (mixView ? "mix" : "program") + " names (built into QS Studio). If you've changed this bank, click " + readLabel + " to get your own.";
    else if (n == 0)
        text = "No " + what + " names for " + bankTitle(bank) + " yet. Click " + readLabel + ".";
    else if (src.startsWith("your "))
        text = "Names read from " + src + ".";
    else if (src.isNotEmpty())
        text = "Names imported from " + src + ". If this bank on your QS has been changed, read it from the QS to be sure they match.";
    else
        text = juce::String(n) + " of " + juce::String(count()) + " " + what + " known.";
    sourceLabel.setText(text + "  Saved for your " + session.library.model + ".", juce::dontSendNotification);
    updateProgramButtons();
}

void LibraryTab::selectProgram(int prog) {
    auto it = std::find(rows.begin(), rows.end(), prog);
    if (it == rows.end() && search.getText().isNotEmpty()) {
        search.clear();
        rebuildRows();
        it = std::find(rows.begin(), rows.end(), prog);
    }
    if (it == rows.end()) return;
    const int r = int(it - rows.begin());
    list.selectRow(r);
    list.scrollToEnsureRowIsOnscreen(r);
}

void LibraryTab::updateLcd() {
    if (!panel) return;
    panel->setModel(session.panelModel());
    panel->sync();
    // What the synth's own display would show (QS6: "PROG PRESET1 00" / "GrandPiano Ch01"; QSR: number, name, "Bank: Preset1").
    FrontPanel::Display d;
    d.mix = mixView;
    const int b = mixView ? session.currentMixBank : session.currentBank;
    d.number = mixView ? session.currentMix : session.currentProgram;
    d.name = mixView ? session.library.mixNameOf(b, d.number) : session.library.nameOf(b, d.number);
    const auto title = bankTitle(b);
    d.bankQsr = title.removeCharacters(" ");
    d.bank = title == "GenMIDI" ? title : d.bankQsr.toUpperCase();
    d.channel = mixView && session.canSelectMixes() ? session.mixSelectChannel() : session.channel;
    panel->setDisplay(d);
}

int LibraryTab::selectedProgram() const {
    const int r = list.getSelectedRow();
    return r >= 0 && r < int(rows.size()) ? rows[size_t(r)] : currentNumber();
}

void LibraryTab::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) {
    if (row < 0 || row >= int(rows.size())) return;
    const int p = rows[size_t(row)];
    if (selected) {
        g.setColour(theme::accentDim);
        g.fillRect(0, 0, w, h);
    } else if (row % 2) {
        g.setColour(theme::panelHi.withAlpha(0.35f));
        g.fillRect(0, 0, w, h);
    }
    g.setFont(theme::monoFont(14.0f));
    g.setColour(selected ? theme::text : theme::dim);
    g.drawText(juce::String(p).paddedLeft('0', mixView ? 2 : 3), 10, 0, 50, h, juce::Justification::centredLeft);
    const auto name = nameAt(p);
    g.setFont(theme::uiFont(15.0f, selected));
    g.setColour(name.isEmpty() ? theme::dim.withAlpha(0.6f) : theme::text);
    g.drawText(name.isEmpty() ? juce::String("(not read yet)") : name, 64, 0, w - 100, h, juce::Justification::centredLeft);
    if (currentBankShown() == bank && currentNumber() == p) {
        g.setColour(theme::accent);
        g.fillEllipse(float(w - 22), float(h) * 0.5f - 4.0f, 8.0f, 8.0f);
    }
}

void LibraryTab::selectedRowsChanged(int row) {
    if (suppressSend || row < 0 || row >= int(rows.size())) return;
    pendingSelect = rows[size_t(row)];
    if (panel) panel->sync();
    updateProgramButtons();
    startTimer(90);  // debounce while arrowing through the list
}

void LibraryTab::timerCallback() {
    stopTimer();
    if (pendingSelect < 0) return;
    const int p = pendingSelect;
    pendingSelect = -1;
    if (mixView) selectMixOnQs(p);
    else if (session.midi.isOpen()) session.selectProgram(bank, p);
    else {
        session.currentBank = bank;
        session.currentProgram = p;
        updateLcd();
    }
    list.repaint();
}

void LibraryTab::listBoxItemDoubleClicked(int row, const juce::MouseEvent&) {
    if (!mixView && row >= 0 && row < int(rows.size())) openInEditor(rows[size_t(row)]);
}

int LibraryTab::count() const { return mixView ? 100 : 128; }
juce::String LibraryTab::nameAt(int n) const { return mixView ? session.library.mixNameOf(bank, n) : session.library.nameOf(bank, n); }
int LibraryTab::currentBankShown() const { return mixView ? session.currentMixBank : session.currentBank; }
int LibraryTab::currentNumber() const { return mixView ? session.currentMix : session.currentProgram; }

void LibraryTab::setMixView(bool mix, bool switchQs) {
    mixView = mix;
    progViewBtn.setToggleState(!mix, juce::dontSendNotification);
    mixViewBtn.setToggleState(mix, juce::dontSendNotification);
    for (auto* b : {&editBtn, &sendBtn, &storeBtn}) b->setEnabled(!mix);
    editBtn.setButtonText(mix ? "Edit this mix (coming later)" : "Edit this program");
    if (panel) panel->setMixMode(mix);
    showBank(bank);
    if (!switchQs || !session.midi.isOpen()) return;
    if (mix) {
        if (session.canSelectMixes()) session.selectMix(session.currentMixBank, session.currentMix);
        else {
            session.midi.send(qs::modeSelect(1));  // Mix mode, on whichever mix the QS was on
            session.mixModeSent = true;
            session.programModeSent = false;
        }
    } else {
        session.programModeSent = false;
        session.selectProgram(session.currentBank, session.currentProgram);
    }
    updateLcd();
}

// Picking a mix: Session::selectMix sets Program Select to a channel just for the change. It needs the Global settings first.
void LibraryTab::selectMixOnQs(int mix) {
    session.currentMixBank = bank;
    session.currentMix = mix;
    if (!session.midi.isOpen()) {
        updateLcd();
        return;
    }
    if (!session.globalKnown) {
        session.midi.send(qs::globalRequest());
        if (session.setStatus) session.setStatus("Reading the QS's Global settings first... click the mix again in a moment.");
        updateLcd();
        return;
    }
    session.selectMix(bank, mix);
}

void LibraryTab::readMixes(std::vector<int> banks) {
    if (!session.midi.isOpen()) {
        alert("Not connected", "Connect to the QS on the Connection page first.");
        return;
    }
    (new ReadMixesJob(session, std::move(banks), [this](juce::String result) {
        refresh();
        if (session.setStatus) session.setStatus("Read mixes from the synth. Details in QS Studio Data/Logs.");
        alert("Read from the synth", (pendingReport.isNotEmpty() ? pendingReport + "\n\n" : juce::String()) + result);
        pendingReport.clear();
    }))->launchThread();
}

void LibraryTab::step(int delta) {
    int r = list.getSelectedRow();
    r = r < 0 ? 0 : juce::jlimit(0, int(rows.size()) - 1, r + delta);
    list.selectRow(r);
    list.scrollToEnsureRowIsOnscreen(r);
}

bool LibraryTab::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::returnKey) {
        openInEditor(selectedProgram());
        return true;
    }
    return false;
}

// Only programs that came from a .syx file can differ from what's in the synth, so only they get "hear the file's version".
void LibraryTab::updateProgramButtons() {
    const int p = selectedProgram();
    const bool fromFile = !mixView && p >= 0 && p < 128 && session.library.programs[size_t(bank)][size_t(p)].has_value() &&
                          !session.library.source[size_t(bank)].startsWith("your ");
    sendBtn.setVisible(fromFile);
}

void LibraryTab::updateCardButtons() {
    // Card banks need a card: found by the check, or allowed by the option. Offline QS Studio can't check, so they're greyed out.
    const bool connected = session.midi.isOpen();
    const bool allowed = cardAnyway.getToggleState() || session.cardState == Session::CardState::Present ||
                         (connected && session.cardState == Session::CardState::Unknown);
    bankButtons[5]->setEnabled(allowed);
    cardBankBox.setEnabled(allowed);
    cardCheckBtn.setVisible(session.cardState == Session::CardState::Absent && session.midi.isOpen());
    cardAnyway.setVisible(!allowed || cardAnyway.getToggleState());
    bankButtons[5]->setTooltip(allowed ? juce::String("Banks on the inserted card(s). A RAM512 has 8 banks; a sound QCard usually has 1.")
                               : !connected ? juce::String("Connect to the QS so QS Studio can check for a card.")
                                            : juce::String("No card found in the QS. Power it off, insert a card, then click Check for a card."));
    if (!allowed && bank >= kFirstCardBank) showBank(0);  // don't leave a card bank on screen that the QS can't play
}

void LibraryTab::probeCard(std::function<void()> thenIfFound) {
    if (!session.midi.isOpen()) return;
    (new CardProbeJob(session, [this, thenIfFound](int r) {
        session.cardState = r == 1 ? Session::CardState::Present : r == 0 ? Session::CardState::Absent : Session::CardState::Unknown;
        updateCardButtons();
        if (r == 1 && thenIfFound) thenIfFound();
        if (session.setStatus)
            session.setStatus(r == 1 ? "Card found." : r == 0 ? "No card in the QS: card banks are greyed out." : "The QS didn't answer the card check.");
    }))->launchThread();
}

void LibraryTab::chooseWhatToRead() {
    // QS_STUDIO_UI_TEST lets the dialog open without a synth, for screenshots of the layout.
    if (!session.midi.isOpen() && juce::SystemStats::getEnvironmentVariable("QS_STUDIO_UI_TEST", {}).isEmpty()) {
        alert("Not connected", "Connect to the QS on the Connection page first.");
        return;
    }
    const bool cards = session.cardState == Session::CardState::Present || cardAnyway.getToggleState();
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned(new ReadChooser(bank, mixView, cards, [this](std::vector<int> banks, bool programs, bool mixes) {
        if (programs)
            readBanks(banks, mixes ? std::function<void()>([this, banks] { readMixes(banks); }) : std::function<void()>());
        else
            readMixes(banks);
    }));
    o.dialogTitle = "Read from the synth";
    o.dialogBackgroundColour = theme::panel;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void LibraryTab::readBanks(std::vector<int> banks, std::function<void()> then) {
    if (!session.midi.isOpen()) {
        alert("Not connected", "Connect to the QS on the Connection page first.");
        return;
    }
    (new ReadBanksJob(session, std::move(banks), [this, then](juce::String result) {
        refresh();
        if (session.setStatus) session.setStatus("Read programs from the synth. Details in QS Studio Data/Logs.");
        if (then) {  // more to read (mixes): show this report when everything is done
            pendingReport = result;
            then();
        } else {
            alert("Read from the synth", result);
        }
    }))->launchThread();
}

void LibraryTab::importOneSyx() {
    chooser = std::make_unique<juce::FileChooser>("Import a .syx into " + bankTitle(bank) + " (QS Studio only)",
                                                  juce::File(settings().getValue("importFolder", settingsDir().getFullPathName())), "*.syx;*.SYX");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (!f.existsAsFile()) return;
        int mixes = 0;
        const int n = session.library.importFile(f, bank, f.getFileName(), &mixes);
        if (n == 0 && mixes == 0) {
            alert("Nothing to import", f.getFileName() + " has no QS program or mix dumps.");
            return;
        }
        session.library.save();
        refresh();
        if (session.setStatus)
            session.setStatus("Imported " + juce::String(n) + " programs and " + juce::String(mixes) + " mixes from " + f.getFileName() + " into " +
                              bankTitle(bank) + ".");
    });
}

void LibraryTab::importFolder() {
    chooser = std::make_unique<juce::FileChooser>("Folder with QS bank .syx files",
                                                  juce::File(settings().getValue("importFolder", settingsDir().getFullPathName())));
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
        auto dir = fc.getResult();
        if (!dir.isDirectory()) return;
        auto files = dir.findChildFiles(juce::File::findFiles, false, "*.syx;*.SYX;*.Syx");
        files.sort();
        if (files.isEmpty()) {
            alert("No .syx files", "That folder has no .syx files.");
            return;
        }
        // Ask which bank each file goes to. Factory names (PST1-4) are pre-selected; anything else defaults to Skip.
        auto* w = new juce::AlertWindow("Import banks", "Choose a bank for each file. Only QS Studio's list changes; nothing is sent to the QS.",
                                        juce::MessageBoxIconType::QuestionIcon);
        juce::StringArray choices{"Skip"};
        for (auto& b : kBanks) choices.add(b.title);
        const juce::StringPairArray saved = [] {
            juce::StringPairArray m;
            for (auto& kv : juce::StringArray::fromTokens(settings().getValue("importMap"), "|", ""))
                if (kv.contains("=")) m.set(kv.upToFirstOccurrenceOf("=", false, false), kv.fromFirstOccurrenceOf("=", false, false));
            return m;
        }();
        const int shown = juce::jmin(16, files.size());
        for (int i = 0; i < shown; ++i) {
            const auto name = files[i].getFileName();
            w->addComboBox(name, choices, name);
            int pick = saved.containsKey(name) ? saved[name].getIntValue() + 1 : Library::factoryBankForName(name) + 1;
            w->getComboBoxComponent(name)->setSelectedItemIndex(juce::jmax(0, pick), juce::dontSendNotification);
        }
        if (files.size() > shown) w->addTextBlock("Only the first 16 files are listed.");
        w->addButton("Import", 1, juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([this, w, dir, files, shown](int r) {
            if (r != 1) return;
            juce::StringPairArray map;
            for (int i = 0; i < shown; ++i) {
                const auto name = files[i].getFileName();
                const int idx = w->getComboBoxComponent(name)->getSelectedItemIndex();
                if (idx > 0) map.set(name, juce::String(idx - 1));
            }
            importFolderWithMap(dir, map);
        }), true);
    });
}

void LibraryTab::importFolderWithMap(const juce::File& dir, const juce::StringPairArray& map) {
    int banks = 0;
    for (auto& name : map.getAllKeys()) {
        const int b = map[name].getIntValue();
        if (session.library.importFile(dir.getChildFile(name), b, name) > 0) ++banks;  // programs and mixes
    }
    juce::StringArray kv;
    for (auto& name : map.getAllKeys()) kv.add(name + "=" + map[name]);
    settings().setValue("importFolder", dir.getFullPathName());
    settings().setValue("importMap", kv.joinIntoString("|"));
    settings().saveIfNeeded();
    session.library.save();
    refresh();
    if (session.setStatus) session.setStatus("Imported " + juce::String(banks) + " bank(s) from " + dir.getFileName() + ".");
}

void LibraryTab::applySavedImports(bool onlyEmptyBanks) {
    const juce::File dir(settings().getValue("importFolder"));
    if (!dir.isDirectory()) return;
    for (auto& kv : juce::StringArray::fromTokens(settings().getValue("importMap"), "|", "")) {
        const auto name = kv.upToFirstOccurrenceOf("=", false, false);
        const int b = kv.fromFirstOccurrenceOf("=", false, false).getIntValue();
        if (b < 0 || b >= int(kBanks.size()) || (onlyEmptyBanks && session.library.countRead(b) > 0)) continue;
        session.library.importFile(dir.getChildFile(name), b, name);
    }
    session.library.save();
    refresh();
}

void LibraryTab::openInEditor(int prog) {
    if (!session.openInEditor) return;
    const auto where = bankTitle(bank) + " " + juce::String(prog).paddedLeft('0', 3);
    const auto cached = session.library.programs[size_t(bank)][size_t(prog)];
    if (session.midi.isOpen()) {
        session.selectProgram(bank, prog);
        // The editor asks the QS for its edit buffer (with retries) and shows the cached copy until the answer arrives.
        session.openInEditor(cached.value_or(qs::Program()), where);
        return;
    }
    if (cached) session.openInEditor(*cached, where + " (offline copy)");
    else alert("Not available", "Connect to the QS, or read this bank first, to open this program.");
}

void LibraryTab::sendCachedToEditBuffer() {
    const int prog = selectedProgram();
    auto& p = session.library.programs[size_t(bank)][size_t(prog)];
    if (!p) {
        alert("Nothing to send", "This program hasn't been read or imported yet.");
        return;
    }
    if (!session.midi.isOpen()) {
        alert("Not connected", "Connect to the QS on the Connection page first.");
        return;
    }
    session.midi.send(qs::modeSelect(0));
    session.programModeSent = true;
    session.midi.send(p->editDump());
    if (session.setStatus) session.setStatus("Auditioning " + juce::String(p->name()).trimEnd() + " on the QS (not stored).");
}

void LibraryTab::storeToUser() {
    const int prog = selectedProgram();
    auto& p = session.library.programs[size_t(bank)][size_t(prog)];
    if (!p) {
        alert("Nothing to store", "This program hasn't been read or imported yet.");
        return;
    }
    if (!session.midi.isOpen()) {
        alert("Not connected", "Connect to the QS on the Connection page first.");
        return;
    }
    auto* w = new juce::AlertWindow("Store to User bank",
                                    "Save \"" + juce::String(p->name()).trimEnd() + "\" permanently into which User program slot?\n"
                                    "This replaces the program in that slot on the QS. Back up first with SysEx Librarian > Ask QS to Send All.",
                                    juce::MessageBoxIconType::WarningIcon);
    w->addTextEditor("num", juce::String(bank == 0 ? prog : 0), "User slot (0-127)");
    w->addButton("Store", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    const auto data = *p;
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w, data](int r) {
        if (r != 1) return;
        const int n = w->getTextEditorContents("num").getIntValue();
        if (n < 0 || n > 127) {
            alert("Store", "Pick a slot from 0 to 127.");
            return;
        }
        session.midi.send(data.userDump(n));
        session.library.programs[0][size_t(n)] = data;
        session.library.save();
        refresh();
        if (session.setStatus) session.setStatus("Stored " + juce::String(data.name()).trimEnd() + " to User " + juce::String(n).paddedLeft('0', 3));
    }), true);
}

void LibraryTab::copyBankToCard() {
    if (!session.midi.isOpen()) {
        alert("Not connected", "Connect to the QS on the Connection page first.");
        return;
    }
    const int cardBank = bank;
    juce::StringArray choices;
    juce::Array<int> ids;
    for (int b = 0; b < int(kBanks.size()); ++b)
        if (b != cardBank && session.library.countRead(b) == 128) {
            choices.add(bankTitle(b) + "  (programs only, " +
                        (session.library.source[size_t(b)].isNotEmpty() ? session.library.source[size_t(b)] : juce::String("known")) + ")");
            ids.add(b);
        }
    choices.add("A .syx bank file: programs, mixes and effects (best)...");
    ids.add(-1);
    auto* w = new juce::AlertWindow("Copy a bank onto " + bankTitle(cardBank),
                                    "What should go onto the card?\n\n"
                                    "A full bank file (a Send All backup, or one made by Convert) carries each program's effects too. "
                                    "QS Studio backs up your User bank first and restores it at the end. "
                                    "Card bank numbers count across both slots: with one card in slot A, Card 1 is CrdA Bnk1.",
                                    juce::MessageBoxIconType::QuestionIcon);
    w->addComboBox("src", choices, "Copy");
    w->getComboBoxComponent("src")->setSelectedItemIndex(choices.size() - 1, juce::dontSendNotification);
    w->addButton("Start", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w, ids, cardBank](int r) {
        if (r != 1) return;
        const int src = ids[w->getComboBoxComponent("src")->getSelectedItemIndex()];
        if (src >= 0) {
            std::vector<qs::Bytes> msgs;
            std::array<std::optional<qs::Program>, 128> expected;
            for (int p = 0; p < 128; ++p)
                if (auto& prog = session.library.programs[size_t(src)][size_t(p)]) {
                    msgs.push_back(prog->userDump(p));
                    expected[size_t(p)] = prog;
                }
            startCardCopy(std::move(msgs), expected, bankTitle(src), cardBank);
            return;
        }
        chooser = std::make_unique<juce::FileChooser>("Bank file to put on the card",
                                                      juce::File(settings().getValue("importFolder", settingsDir().getFullPathName())), "*.syx;*.SYX");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, cardBank](const juce::FileChooser& fc) {
            auto f = fc.getResult();
            juce::MemoryBlock mb;
            if (!f.existsAsFile() || !f.loadFileAsData(mb)) return;
            auto ptr = static_cast<const uint8_t*>(mb.getData());
            std::vector<qs::Bytes> msgs;
            std::array<std::optional<qs::Program>, 128> expected;
            for (auto& m : qs::splitSysex(qs::Bytes(ptr, ptr + mb.getSize()))) {
                const int op = qs::opcodeOf(m);
                // User programs, mixes and effects; never Global (it would change the QS's MIDI settings)
                if (op == qs::UserProgramDump || op == qs::NewMixDump || op == qs::OldMixDump || op == qs::UserEffectsDump) msgs.push_back(m);
                if (op == qs::UserProgramDump && m.size() > 6)
                    if (auto p = qs::Program::fromDump(m)) expected[m[6] & 0x7F] = p;
            }
            if (msgs.empty()) {
                alert("Nothing to copy", f.getFileName() + " has no QS programs, mixes or effects.");
                return;
            }
            startCardCopy(std::move(msgs), expected, f.getFileName(), cardBank);
        });
    }), true);
}

void LibraryTab::startCardCopy(std::vector<qs::Bytes> msgs, std::array<std::optional<qs::Program>, 128> expected, juce::String label, int cardBank) {
    const auto backup = settingsDir().getChildFile("Backups").getChildFile(
        "User_before_card_copy_" + juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S") + ".syx");
    (new CardCopyPrepJob(session, std::move(msgs), label, backup, [this, expected, label, cardBank, backup](bool ok, juce::String msg) {
        if (!ok) {
            alert("Copy to card", msg);
            return;
        }
        const int n = cardBank - kFirstCardBank + 1;
        auto* step = new juce::AlertWindow("Now on the QS",
                                           msg + "\n\nOn the QS:\n"
                                           "1. Press STORE.\n"
                                           "2. Page right until the display shows  Save USER to CrdA Bnk" + juce::String(n) + "\n"
                                           "   (use the value buttons to change the bank number if needed).\n"
                                           "3. Press STORE again and wait for it to finish.\n\n"
                                           "Then click Done. QS Studio will read the card bank back to check it and restore your User bank.",
                                           juce::MessageBoxIconType::InfoIcon);
        step->addButton("Done", 1, juce::KeyPress(juce::KeyPress::returnKey));
        step->addButton("Cancel and restore my User bank", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        step->enterModalState(true, juce::ModalCallbackFunction::create([this, expected, label, cardBank, backup](int r2) {
            if (r2 == 1) finishCardCopy(expected, label, cardBank, backup);
            else (new RestoreJob(session, backup, [this](juce::String m) { alert("Copy to card", "Cancelled. " + m); }))->launchThread();
        }), true);
    }))->launchThread();
}

void LibraryTab::finishCardCopy(std::array<std::optional<qs::Program>, 128> expected, juce::String label, int cardBank, juce::File backup) {
    (new ReadBanksJob(session, {cardBank}, [this, expected, label, cardBank, backup](juce::String) {
        int same = 0, known = 0;
        for (int p = 0; p < 128; ++p) {
            auto& a = expected[size_t(p)];
            auto& b = session.library.programs[size_t(cardBank)][size_t(p)];
            if (a && b) {
                ++known;
                same += a->bytes() == b->bytes();
            }
        }
        session.library.source[size_t(cardBank)] = label + " (copied to the card)";
        session.library.save();
        showBank(cardBank);
        const auto verdict = same == known && known > 0 ? "Checked: all " + juce::String(known) + " programs on " + bankTitle(cardBank) + " match " + label + "."
                                                        : "Check: " + juce::String(same) + " of " + juce::String(known) + " programs match. "
                                                          "If that's low, the Save USER to card step may not have run.";
        (new RestoreJob(session, backup, [this, verdict](juce::String m) { alert("Copy to card", verdict + "\n\n" + m); }))->launchThread();
    }))->launchThread();
}

void LibraryTab::convertBankFile() {
    chooser = std::make_unique<juce::FileChooser>("Bank file to convert (a Send All backup works best)",
                                                  juce::File(settings().getValue("importFolder", settingsDir().getFullPathName())), "*.syx;*.SYX");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        juce::MemoryBlock mb;
        if (!f.existsAsFile() || !f.loadFileAsData(mb)) return;
        auto* w = new juce::AlertWindow("Convert " + f.getFileName(),
                                        "Which synth was this bank made on, and which synth should it play on?\n"
                                        "The QS6 has its own sample ROM. The QS7, QS8 and QSR share another one.",
                                        juce::MessageBoxIconType::QuestionIcon);
        w->addComboBox("from", {"QS6", "QS7 / QS8 / QSR"}, "Made on");
        w->addComboBox("to", {"QS6", "QS7 / QS8 / QSR"}, "Play on");
        const bool looksQs6 = f.getFileName().containsIgnoreCase("QS6");
        w->getComboBoxComponent("from")->setSelectedItemIndex(looksQs6 || !f.getFileName().containsIgnoreCase("QSR") ? 0 : 1, juce::dontSendNotification);
        w->getComboBoxComponent("to")->setSelectedItemIndex(w->getComboBoxComponent("from")->getSelectedItemIndex() == 0 ? 1 : 0, juce::dontSendNotification);
        w->addButton("Convert", 1, juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        const auto bytes = qs::Bytes(static_cast<const uint8_t*>(mb.getData()), static_cast<const uint8_t*>(mb.getData()) + mb.getSize());
        w->enterModalState(true, juce::ModalCallbackFunction::create([this, w, f, bytes](int r) {
            if (r != 1) return;
            const auto from = w->getComboBoxComponent("from")->getSelectedItemIndex() == 0 ? qs::RomSet::QS6 : qs::RomSet::QS78;
            const auto to = w->getComboBoxComponent("to")->getSelectedItemIndex() == 0 ? qs::RomSet::QS6 : qs::RomSet::QS78;
            if (from == to) {
                alert("Convert", "Pick two different synths.");
                return;
            }
            auto bank = qs::convertBank(qs::splitSysex(bytes), from, to);
            if (bank.programs.empty()) {
                alert("Convert", f.getFileName() + " has no QS programs.");
                return;
            }
            const juce::String suffix = to == qs::RomSet::QS6 ? "_for_QS6" : "_for_QSR";
            const auto outSyx = f.getSiblingFile(f.getFileNameWithoutExtension() + suffix + ".syx");
            const auto outTxt = f.getSiblingFile(f.getFileNameWithoutExtension() + suffix + "_report.txt");
            qs::Bytes all;
            for (auto& m : bank.messages) all.insert(all.end(), m.begin(), m.end());
            outSyx.replaceWithData(all.data(), all.size());
            const auto report = juce::String(bank.report(from, to));
            outTxt.replaceWithText(report);
            auto* done = new juce::AlertWindow("Converted",
                                               report.upToFirstOccurrenceOf("\n\n", false, false) + "\n\nSaved:\n" + outSyx.getFileName() + "\n" +
                                                   outTxt.getFileName() +
                                                   "\n\nTo use it: send it with the SysEx Librarian, or pick Card > a card bank > "
                                                   "Copy a bank onto this card bank and choose this file.",
                                               juce::MessageBoxIconType::InfoIcon);
            done->addButton("Open report", 1);
            done->addButton("Close", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            done->enterModalState(true, juce::ModalCallbackFunction::create([outTxt](int r2) {
                if (r2 == 1) outTxt.startAsProcess();
            }), true);
            if (session.setStatus) session.setStatus("Converted " + juce::String(int(bank.programs.size())) + " programs to " + outSyx.getFileName());
        }), true);
    });
}

void LibraryTab::paint(juce::Graphics& g) {
    g.fillAll(theme::bg);
    auto r = getLocalBounds().reduced(14);
    r.removeFromTop(panel->getHeight() + 6);
    theme::drawCaption(g, r.removeFromTop(18).removeFromLeft(140), "Bank", juce::Justification::centredLeft);
    const int rx = readBtn.getX(), rw = readBtn.getWidth();
    theme::drawCaption(g, {rx, sectionNames, rw, 18}, "Names in the list", juce::Justification::centredLeft);
    theme::drawCaption(g, {rx, sectionThis, rw, 18}, mixView ? "This mix" : "This program", juce::Justification::centredLeft);
    if (copyToCardBtn.isVisible()) theme::drawCaption(g, {rx, sectionCard, rw, 18}, "This card", juce::Justification::centredLeft);
}

void LibraryTab::resized() {
    auto r = getLocalBounds().reduced(14);
    panel->setBounds(r.removeFromTop(panel->heightForWidth(r.getWidth())));
    r.removeFromTop(26);
    auto left = r.removeFromLeft(150);
    for (auto* b : bankButtons) {
        b->setBounds(left.removeFromTop(38));
        left.removeFromTop(6);
    }
    cardBankBox.setBounds(left.removeFromTop(30));
    r.removeFromLeft(14);
    auto right = r.removeFromRight(280);
    r.removeFromRight(14);
    auto searchRow = r.removeFromTop(34);
    progViewBtn.setBounds(searchRow.removeFromLeft(100));
    mixViewBtn.setBounds(searchRow.removeFromLeft(80));
    searchRow.removeFromLeft(10);
    nextBtn.setBounds(searchRow.removeFromRight(40));
    searchRow.removeFromRight(6);
    prevBtn.setBounds(searchRow.removeFromRight(40));
    searchRow.removeFromRight(10);
    search.setBounds(searchRow);
    r.removeFromTop(8);
    sourceLabel.setBounds(r.removeFromBottom(40));
    r.removeFromBottom(4);
    list.setBounds(r);
    // Right column, in labelled groups (captions are painted).
    right.removeFromTop(20);
    sectionNames = right.getY() - 20;
    readBtn.setBounds(right.removeFromTop(34));
    right.removeFromTop(6);
    for (auto* b : {&importOneBtn, &importFolderBtn}) {
        b->setBounds(right.removeFromTop(34));
        right.removeFromTop(6);
    }
    loadNote.setBounds(right.removeFromTop(34));
    factoryLink.setBounds(right.removeFromTop(20));
    right.removeFromTop(18);
    sectionThis = right.getY();
    right.removeFromTop(20);
    for (auto* b : {&editBtn, &storeBtn, &sendBtn}) {
        b->setBounds(right.removeFromTop(34));
        right.removeFromTop(6);
    }
    right.removeFromTop(12);
    sectionCard = right.getY();
    right.removeFromTop(20);
    copyToCardBtn.setBounds(right.removeFromTop(34));
    // Under the bank buttons: card check and the "use anyway" option.
    cardCheckBtn.setBounds(left.removeFromTop(36).withTrimmedTop(6));
    cardAnyway.setBounds(left.removeFromTop(48).withTrimmedTop(4));
}
