#include "LibraryTab.h"

namespace {

void alert(const juce::String& title, const juce::String& msg) {
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, title, msg);
}

juce::String bankTitle(int b) { return kBanks[size_t(b)].title; }

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
        if (std::any_of(banks.begin(), banks.end(), [](int b) { return b != 0; })) {
            midi.send(qs::modeSelect(0));
            session.programModeSent = true;
            wait(100);
        }
        for (int bank : banks) {
            if (threadShouldExit()) break;
            auto& out = results[bank];
            int got = 0, sameAsPrevious = 0;
            std::optional<qs::Program> prev;
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
                        if (prog && prev && p > 0 && prog->bytes() == prev->bytes() && attempt < 3) {
                            wait(150);
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
        }
    }

    void threadComplete(bool) override {
        juce::StringArray summary;
        for (int bank : banks) {
            const int got = counts[bank];
            if (got > 0) {
                for (size_t p = 0; p < 128; ++p)
                    if (results[bank][p]) session.library.programs[size_t(bank)][p] = results[bank][p];
                session.library.source[size_t(bank)] = "your " + (session.deviceName.isNotEmpty() ? session.deviceName : juce::String("QS")) +
                                                       " (read " + juce::Time::getCurrentTime().formatted("%Y-%m-%d") + ")";
            }
            summary.add(bankTitle(bank) + ": " + juce::String(got) + " of 128");
        }
        session.library.save();
        if (onDone) onDone(summary.joinIntoString(", ") + (problems.isEmpty() ? "" : "\n\n" + problems.joinIntoString("\n")));
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
    juce::StringArray problems;
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

// Step 1 of "copy a bank to the card": back up the User bank (Send All), then load the chosen bank into User.
class CardCopyPrepJob : public juce::ThreadWithProgressWindow {
public:
    CardCopyPrepJob(Session& s, int src, juce::File backupFile, std::function<void(bool, juce::String)> done)
        : ThreadWithProgressWindow("Copy to card: preparing the QS", true, true), session(s), source(src), backup(backupFile),
          onDone(std::move(done)) {}

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

        setStatusMessage("Loading " + bankTitle(source) + " into the User bank...");
        std::vector<qs::Bytes> msgs;
        for (int p = 0; p < 128; ++p)
            if (auto& prog = session.library.programs[size_t(source)][size_t(p)]) msgs.push_back(prog->userDump(p));
        sendPaced(session, msgs, this);
        ok = !threadShouldExit();
        message = ok ? "Your User bank is backed up to " + backup.getFileName() + " and now holds " + bankTitle(source) + "."
                     : "Cancelled. Your backup is in " + backup.getFullPathName() + " (send it with the SysEx Librarian to restore).";
    }

    void threadComplete(bool) override {
        if (onDone) onDone(ok, message);
        delete this;
    }

private:
    Session& session;
    int source;
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
    addAndMakeVisible(lcd);
    const char* mainBanks[] = {"User", "Preset 1", "Preset 2", "Preset 3", "GenMIDI", "Card"};
    for (int b = 0; b < 6; ++b) {
        auto* btn = bankButtons.add(new juce::TextButton(mainBanks[b]));
        btn->onClick = [this, b] { showBank(b < 5 ? b : kFirstCardBank + cardBankBox.getSelectedId() - 1); };
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

    for (juce::Component* c : std::initializer_list<juce::Component*>{&prevBtn, &nextBtn, &readBtn, &readAllBtn, &importFolderBtn, &importOneBtn,
                                                                    &editBtn, &sendBtn, &storeBtn, &factoryLink, &sourceLabel})
        addAndMakeVisible(c);
    addChildComponent(copyToCardBtn);
    copyToCardBtn.onClick = [this] { copyBankToCard(); };
    copyToCardBtn.setTooltip("Puts a whole bank (for example your QS6 User bank) onto this card bank. QS Studio backs up your User bank, "
                             "loads the bank you pick, tells you which buttons to press on the QS, checks the card, then restores your User bank.");
    prevBtn.onClick = [this] { step(-1); };
    nextBtn.onClick = [this] { step(1); };
    readBtn.onClick = [this] { readBanks({bank}); };
    readAllBtn.onClick = [this] { readBanks({0, 1, 2, 3, 4}); };
    importFolderBtn.onClick = [this] { importFolder(); };
    importOneBtn.onClick = [this] { importOneSyx(); };
    editBtn.onClick = [this] { openInEditor(selectedProgram()); };
    sendBtn.onClick = [this] { sendCachedToEditBuffer(); };
    storeBtn.onClick = [this] { storeToUser(); };

    prevBtn.setTooltip("Previous program");
    nextBtn.setTooltip("Next program");
    readBtn.setTooltip("Asks the QS for every program in the bank shown, so the names appear here. Takes about 40 seconds for a Preset bank. "
                       "Names are kept in QSStudio.library, so you only do this once.");
    readAllBtn.setTooltip("Reads User, Preset 1-3 and GenMIDI in one go (a few minutes). Nothing on the QS is changed.");
    importFolderBtn.setTooltip("Fills banks here from .syx files in a folder (for example the Alesis factory banks). "
                               "You choose which file goes to which bank. Only QS Studio's list changes; nothing is sent to the QS.");
    importOneBtn.setTooltip("Fills the bank shown here from one .syx file, so you can browse its names and open its programs in the editor. "
                            "Only QS Studio's list changes; nothing is sent to the QS. To write a bank to the QS, use the SysEx Librarian tab.");
    editBtn.setTooltip("Selects the program on the QS and loads it into the Editor tab (Enter or double-click does the same).");
    sendBtn.setTooltip("Plays this program on the QS by sending it to the edit buffer. Nothing is stored; selecting another program discards it.");
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
    readBtn.setButtonText("Read " + bankTitle(b) + " from QS");
    copyToCardBtn.setVisible(b >= kFirstCardBank);
    rebuildRows();
    updateLcd();
}

void LibraryTab::refresh() {
    rebuildRows();
    updateLcd();
}

void LibraryTab::rebuildRows() {
    const auto q = search.getText().trim();
    rows.clear();
    for (int p = 0; p < 128; ++p)
        if (q.isEmpty() || session.library.nameOf(bank, p).containsIgnoreCase(q) || juce::String(p) == q) rows.push_back(p);
    list.updateContent();
    list.repaint();
    suppressSend = true;
    list.deselectAllRows();
    if (session.currentBank == bank)
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i] == session.currentProgram) list.selectRow(int(i));
    suppressSend = false;

    const int n = session.library.countRead(bank);
    const auto& src = session.library.source[size_t(bank)];
    juce::String text;
    if (n == 0)
        text = "No names for " + bankTitle(bank) + " yet. Click \"Read " + bankTitle(bank) + " from QS\"" +
               (bank >= 1 && bank <= 4 ? juce::String(", or import the Alesis factory banks.") : juce::String("."));
    else if (src.startsWith("your "))
        text = "Names read from " + src + ".";
    else if (src.isNotEmpty())
        text = "Names imported from " + src + ". If this bank on your QS has been changed, read it from the QS to be sure they match.";
    else
        text = juce::String(n) + " of 128 programs known.";
    sourceLabel.setText(text, juce::dontSendNotification);
}

void LibraryTab::updateLcd() {
    const auto name = session.library.nameOf(session.currentBank, session.currentProgram);
    lcd.setLines(name.isNotEmpty() ? name : juce::String("Program ") + juce::String(session.currentProgram).paddedLeft('0', 3),
                 {"BANK: " + bankTitle(session.currentBank).toUpperCase() + "   PROG " + juce::String(session.currentProgram).paddedLeft('0', 3) +
                      "   CH " + juce::String(session.channel).paddedLeft('0', 2),
                  session.midi.isOpen() ? (session.deviceName.isNotEmpty() ? session.deviceName : juce::String("MIDI connected"))
                                        : juce::String("NOT CONNECTED: see the Device tab")});
}

int LibraryTab::selectedProgram() const {
    const int r = list.getSelectedRow();
    return r >= 0 && r < int(rows.size()) ? rows[size_t(r)] : session.currentProgram;
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
    g.drawText(juce::String(p).paddedLeft('0', 3), 10, 0, 50, h, juce::Justification::centredLeft);
    const auto name = session.library.nameOf(bank, p);
    g.setFont(theme::uiFont(15.0f, selected));
    g.setColour(name.isEmpty() ? theme::dim.withAlpha(0.6f) : theme::text);
    g.drawText(name.isEmpty() ? juce::String("(not read yet)") : name, 64, 0, w - 100, h, juce::Justification::centredLeft);
    if (session.currentBank == bank && session.currentProgram == p) {
        g.setColour(theme::accent);
        g.fillEllipse(float(w - 22), float(h) * 0.5f - 4.0f, 8.0f, 8.0f);
    }
}

void LibraryTab::selectedRowsChanged(int row) {
    if (suppressSend || row < 0 || row >= int(rows.size())) return;
    pendingSelect = rows[size_t(row)];
    startTimer(90);  // debounce while arrowing through the list
}

void LibraryTab::timerCallback() {
    stopTimer();
    if (pendingSelect < 0) return;
    const int p = pendingSelect;
    pendingSelect = -1;
    if (session.midi.isOpen()) session.selectProgram(bank, p);
    else {
        session.currentBank = bank;
        session.currentProgram = p;
        updateLcd();
    }
    list.repaint();
}

void LibraryTab::listBoxItemDoubleClicked(int row, const juce::MouseEvent&) {
    if (row >= 0 && row < int(rows.size())) openInEditor(rows[size_t(row)]);
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

void LibraryTab::readBanks(std::vector<int> banks) {
    if (!session.midi.isOpen()) {
        alert("Not connected", "Connect to the QS on the Device tab first.");
        return;
    }
    (new ReadBanksJob(session, std::move(banks), [this](juce::String result) {
        refresh();
        if (session.setStatus) session.setStatus("Read from QS: " + result.upToFirstOccurrenceOf("\n", false, false));
        if (result.contains("\n")) alert("Read from QS", result);
    }))->launchThread();
}

void LibraryTab::importOneSyx() {
    chooser = std::make_unique<juce::FileChooser>("Import a .syx into " + bankTitle(bank) + " (QS Studio only)",
                                                  juce::File(settings().getValue("importFolder", settingsDir().getFullPathName())), "*.syx;*.SYX");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (!f.existsAsFile()) return;
        const int n = session.library.importFile(f, bank, f.getFileName());
        if (n == 0) {
            alert("No programs", f.getFileName() + " has no QS program dumps.");
            return;
        }
        session.library.save();
        refresh();
        if (session.setStatus) session.setStatus("Imported " + juce::String(n) + " programs from " + f.getFileName() + " into " + bankTitle(bank) + ".");
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
        if (session.library.importFile(dir.getChildFile(name), b, name) > 0) ++banks;
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
        alert("Not connected", "Connect to the QS on the Device tab first.");
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
        alert("Not connected", "Connect to the QS on the Device tab first.");
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
        alert("Not connected", "Connect to the QS on the Device tab first.");
        return;
    }
    const int cardBank = bank;
    juce::StringArray choices;
    juce::Array<int> ids;
    for (int b = 0; b < int(kBanks.size()); ++b)
        if (b != cardBank && session.library.countRead(b) == 128) {
            choices.add(bankTitle(b) + "  (" + (session.library.source[size_t(b)].isNotEmpty() ? session.library.source[size_t(b)] : juce::String("known")) + ")");
            ids.add(b);
        }
    if (ids.isEmpty()) {
        alert("Nothing to copy", "First fill a bank with all 128 programs: read it from a QS, or import a .syx into it.");
        return;
    }
    auto* w = new juce::AlertWindow("Copy a bank onto " + bankTitle(cardBank),
                                    "Which bank should go onto the card?\n\n"
                                    "QS Studio backs up your User bank first and restores it at the end. "
                                    "Card bank numbers count across both slots: with one card in slot A, Card 1 is CrdA Bnk1.",
                                    juce::MessageBoxIconType::QuestionIcon);
    w->addComboBox("src", choices, "Bank to copy");
    w->addButton("Start", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w, ids, cardBank](int r) {
        if (r != 1) return;
        const int src = ids[w->getComboBoxComponent("src")->getSelectedItemIndex()];
        const auto backup = settingsDir().getChildFile("Backups").getChildFile(
            "User_before_card_copy_" + juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S") + ".syx");
        (new CardCopyPrepJob(session, src, backup, [this, src, cardBank, backup](bool ok, juce::String msg) {
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
            step->enterModalState(true, juce::ModalCallbackFunction::create([this, src, cardBank, backup](int r2) {
                if (r2 == 1) finishCardCopy(src, cardBank, backup);
                else
                    (new RestoreJob(session, backup, [this](juce::String m) { alert("Copy to card", "Cancelled. " + m); }))->launchThread();
            }), true);
        }))->launchThread();
    }), true);
}

void LibraryTab::finishCardCopy(int src, int cardBank, juce::File backup) {
    (new ReadBanksJob(session, {cardBank}, [this, src, cardBank, backup](juce::String) {
        int same = 0, known = 0;
        for (int p = 0; p < 128; ++p) {
            auto& a = session.library.programs[size_t(src)][size_t(p)];
            auto& b = session.library.programs[size_t(cardBank)][size_t(p)];
            if (a && b) {
                ++known;
                same += a->bytes() == b->bytes();
            }
        }
        showBank(cardBank);
        const auto verdict = same == 128 ? juce::String("Checked: all 128 programs on ") + bankTitle(cardBank) + " match " + bankTitle(src) + "."
                                         : "Check: " + juce::String(same) + " of 128 programs match (" + juce::String(known) + " read back). "
                                           "If that's low, the Save USER to card step may not have run.";
        (new RestoreJob(session, backup, [this, verdict](juce::String m) { alert("Copy to card", verdict + "\n\n" + m); }))->launchThread();
    }))->launchThread();
}

void LibraryTab::paint(juce::Graphics& g) {
    g.fillAll(theme::bg);
    auto r = getLocalBounds().reduced(14);
    r.removeFromTop(150);
    theme::drawCaption(g, r.removeFromTop(18).removeFromLeft(140), "Bank", juce::Justification::centredLeft);
}

void LibraryTab::resized() {
    auto r = getLocalBounds().reduced(14);
    auto top = r.removeFromTop(140);
    lcd.setBounds(top.removeFromLeft(juce::jmin(620, top.getWidth())));
    r.removeFromTop(28);
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
    nextBtn.setBounds(searchRow.removeFromRight(40));
    searchRow.removeFromRight(6);
    prevBtn.setBounds(searchRow.removeFromRight(40));
    searchRow.removeFromRight(10);
    search.setBounds(searchRow);
    r.removeFromTop(8);
    sourceLabel.setBounds(r.removeFromBottom(40));
    r.removeFromBottom(4);
    list.setBounds(r);
    for (auto* b : {&readBtn, &readAllBtn, &importFolderBtn, &importOneBtn}) {
        b->setBounds(right.removeFromTop(34));
        right.removeFromTop(6);
    }
    factoryLink.setBounds(right.removeFromTop(22));
    right.removeFromTop(16);
    for (auto* b : {&editBtn, &sendBtn, &storeBtn, &copyToCardBtn}) {
        b->setBounds(right.removeFromTop(34));
        right.removeFromTop(6);
    }
}
