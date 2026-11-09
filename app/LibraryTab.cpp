#include "LibraryTab.h"

namespace {

void alert(const juce::String& title, const juce::String& msg) {
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, title, msg);
}

// Reads all 128 programs of one bank. User: opcode 01 per program. Other banks: select each program
// (bank select + program change) and request the edit buffer (opcode 03), which needs MIDI Program Select ON.
class ReadBankJob : public juce::ThreadWithProgressWindow {
public:
    ReadBankJob(Session& s, int b, std::function<void(juce::String)> done)
        : ThreadWithProgressWindow("Reading " + juce::String(kBanks[size_t(b)].title) + " from the QS", true, true),
          session(s), bank(b), onDone(std::move(done)) {}

    void run() override {
        auto& midi = session.midi;
        midi.clearQueue();
        int got = 0;
        if (bank != 0) midi.send(qs::modeSelect(0));
        for (int p = 0; p < 128 && !threadShouldExit(); ++p) {
            std::optional<qs::Bytes> reply;
            for (int attempt = 0; attempt < 2 && !reply && !threadShouldExit(); ++attempt) {
                if (bank == 0) {
                    midi.send({0xF0, 0x00, 0x00, 0x0E, 0x0E, qs::UserProgramRequest, uint8_t(p), 0xF7});
                    reply = midi.waitFor([p](const qs::Bytes& m) { return qs::opcodeOf(m) == qs::UserProgramDump && m.size() > 6 && m[6] == p; },
                                         2000, this);
                } else {
                    midi.sendMidi(juce::MidiMessage::controllerEvent(session.channel, 0, kBanks[size_t(bank)].cc0));
                    midi.sendMidi(juce::MidiMessage::programChange(session.channel, p));
                    wait(250);  // let the QS load the program before asking for the edit buffer
                    midi.clearQueue();
                    midi.send(qs::editProgramRequest(0));
                    reply = midi.waitFor([](const qs::Bytes& m) { return qs::opcodeOf(m) == qs::EditProgramDump; }, 2000, this);
                }
            }
            if (!reply) {
                if (threadShouldExit()) break;
                result = got == 0 ? juce::String("The QS did not answer. Check both MIDI cables and the Device tab connection.") +
                                        (bank == 0 ? "" : "\nFor Preset, GenMIDI and card banks, set Global > MIDI Program Select to ON "
                                                          "and make sure the MIDI channel below matches the QS.")
                                  : "The QS stopped answering at program " + juce::String(p) + ".";
                break;
            }
            if (auto prog = qs::Program::fromDump(*reply)) {
                read[size_t(p)] = prog;
                ++got;
            }
            setProgress(double(p + 1) / 128.0);
            setStatusMessage("Program " + juce::String(p + 1) + " of 128" +
                             (read[size_t(p)] ? ":  " + juce::String(read[size_t(p)]->name()).trimEnd() : juce::String()));
        }
        if (result.isEmpty()) result = "Read " + juce::String(got) + " of 128 programs" + (threadShouldExit() ? " (cancelled)." : ".");
    }

    void threadComplete(bool) override {
        for (size_t p = 0; p < 128; ++p)
            if (read[p]) session.library.programs[size_t(bank)][p] = read[p];
        session.library.save();
        if (onDone) onDone(result);
        delete this;
    }

private:
    Session& session;
    int bank;
    std::array<std::optional<qs::Program>, 128> read;
    juce::String result;
    std::function<void(juce::String)> onDone;
};

}  // namespace

LibraryTab::LibraryTab(Session& s) : session(s) {
    addAndMakeVisible(lcd);
    for (size_t b = 0; b < kBanks.size(); ++b) {
        auto* btn = bankButtons.add(new juce::TextButton(kBanks[b].title));
        btn->setClickingTogglesState(false);
        btn->onClick = [this, b] { showBank(int(b)); };
        addAndMakeVisible(btn);
    }
    search.setTextToShowWhenEmpty("Search names...", theme::dim);
    search.onTextChange = [this] { rebuildRows(); };
    search.setFont(theme::uiFont(15.0f));
    addAndMakeVisible(search);

    list.setRowHeight(26);
    list.setColour(juce::ListBox::backgroundColourId, theme::panel);
    list.setOutlineThickness(0);
    list.setWantsKeyboardFocus(true);
    addAndMakeVisible(list);

    for (juce::Component* c : std::initializer_list<juce::Component*>{&prevBtn, &nextBtn, &readBtn, &loadBtn, &editBtn, &sendBtn, &storeBtn, &hint})
        addAndMakeVisible(c);
    prevBtn.onClick = [this] { step(-1); };
    nextBtn.onClick = [this] { step(1); };
    readBtn.onClick = [this] { readBank(); };
    loadBtn.onClick = [this] { loadSyxIntoBank(); };
    editBtn.onClick = [this] { openInEditor(selectedProgram()); };
    sendBtn.onClick = [this] { sendCachedToEditBuffer(); };
    storeBtn.onClick = [this] { storeToUser(); };
    editBtn.setToggleState(false, juce::dontSendNotification);

    hint.setColour(juce::Label::textColourId, theme::dim);
    hint.setFont(theme::uiFont(13.0f));
    hint.setJustificationType(juce::Justification::topLeft);
    hint.setText("Click a program to select it on the QS. Arrow keys step through programs. Double-click opens it in the editor.\n\n"
                 "Names show once a bank is read from the QS (or loaded from a .syx). They are kept in QSStudio.library "
                 "next to the exe, so each bank only needs reading once.\n\n"
                 "Preset, GenMIDI and card banks need Global > MIDI Program Select set to ON on the QS.",
                 juce::dontSendNotification);

    session.currentProgramChanged = [this](int, int) { updateLcd(); };
    showBank(juce::jlimit(0, int(kBanks.size()) - 1, settings().getIntValue("libraryBank", 0)));
}

LibraryTab::~LibraryTab() { session.currentProgramChanged = nullptr; }

void LibraryTab::showBank(int b) {
    bank = b;
    settings().setValue("libraryBank", b);
    for (int i = 0; i < bankButtons.size(); ++i) bankButtons[i]->setToggleState(i == b, juce::dontSendNotification);
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
    // keep the current program highlighted when it is in view
    suppressSend = true;
    list.deselectAllRows();
    if (session.currentBank == bank)
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i] == session.currentProgram) list.selectRow(int(i));
    suppressSend = false;
    const int n = session.library.countRead(bank);
    readBtn.setButtonText(n == 0 ? "Read bank from QS" : "Re-read bank from QS");
}

void LibraryTab::updateLcd() {
    const auto name = session.library.nameOf(session.currentBank, session.currentProgram);
    lcd.setLines(name.isNotEmpty() ? name : juce::String("Program ") + juce::String(session.currentProgram).paddedLeft('0', 3),
                 {"BANK: " + juce::String(kBanks[size_t(session.currentBank)].title).toUpperCase() + "   PROG " +
                      juce::String(session.currentProgram).paddedLeft('0', 3) + "   CH " + juce::String(session.channel).paddedLeft('0', 2),
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
    // QS groups programs in tens: show the group digit apart from the number, like the front panel.
    g.setFont(theme::monoFont(14.0f));
    g.setColour(selected ? theme::text : theme::dim);
    g.drawText(juce::String(p / 10).paddedLeft('0', 2) + " " + juce::String(p % 10), 10, 0, 60, h, juce::Justification::centredLeft);
    const auto name = session.library.nameOf(bank, p);
    g.setFont(theme::uiFont(15.0f, selected));
    g.setColour(name.isEmpty() ? theme::dim.withAlpha(0.6f) : theme::text);
    g.drawText(name.isEmpty() ? juce::String("(not read yet)") : name, 74, 0, w - 110, h, juce::Justification::centredLeft);
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
    if (k == juce::KeyPress::returnKey) { openInEditor(selectedProgram()); return true; }
    return false;
}

void LibraryTab::readBank() {
    if (!session.midi.isOpen()) { alert("Not connected", "Connect to the QS on the Device tab first."); return; }
    const int b = bank;
    (new ReadBankJob(session, b, [this, b](juce::String result) {
        refresh();
        if (session.setStatus) session.setStatus(juce::String(kBanks[size_t(b)].title) + ": " + result);
        if (!result.startsWith("Read 128")) alert("Read bank", result);
    }))->launchThread();
}

void LibraryTab::loadSyxIntoBank() {
    chooser = std::make_unique<juce::FileChooser>("Load programs from a .syx into " + juce::String(kBanks[size_t(bank)].title),
                                                  settingsDir(), "*.syx;*.SYX");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (!f.existsAsFile()) return;
        juce::MemoryBlock mb;
        f.loadFileAsData(mb);
        auto ptr = static_cast<const uint8_t*>(mb.getData());
        int n = 0;
        for (auto& m : qs::splitSysex(qs::Bytes(ptr, ptr + mb.getSize())))
            if (qs::opcodeOf(m) == qs::UserProgramDump && m.size() > 6)
                if (auto p = qs::Program::fromDump(m)) {
                    session.library.programs[size_t(bank)][m[6] & 0x7F] = p;
                    ++n;
                }
        if (n == 0) { alert("No programs", f.getFileName() + " has no QS program dumps."); return; }
        session.library.save();
        refresh();
        if (session.setStatus) session.setStatus("Loaded " + juce::String(n) + " program names from " + f.getFileName());
    });
}

void LibraryTab::openInEditor(int prog) {
    if (!session.openInEditor) return;
    const auto where = juce::String(kBanks[size_t(bank)].title) + " " + juce::String(prog).paddedLeft('0', 3);
    if (session.midi.isOpen()) {
        session.selectProgram(bank, prog);
        // Ask for the edit buffer once the QS has switched programs; the editor picks up the reply.
        juce::Timer::callAfterDelay(250, [this, where] {
            session.midi.send(qs::editProgramRequest(0));
            if (session.setStatus) session.setStatus("Loading " + where + " from the QS edit buffer...");
        });
        session.openInEditor(session.library.programs[size_t(bank)][size_t(prog)].value_or(qs::Program()), where);
        return;
    }
    if (auto& p = session.library.programs[size_t(bank)][size_t(prog)]) session.openInEditor(*p, where + " (offline copy)");
    else alert("Not available", "Connect to the QS, or read this bank first, to open this program.");
}

void LibraryTab::sendCachedToEditBuffer() {
    const int prog = selectedProgram();
    auto& p = session.library.programs[size_t(bank)][size_t(prog)];
    if (!p) { alert("Nothing to send", "This program hasn't been read or loaded yet."); return; }
    if (!session.midi.isOpen()) { alert("Not connected", "Connect to the QS on the Device tab first."); return; }
    session.midi.send(qs::modeSelect(0));
    session.midi.send(p->editDump());
    if (session.setStatus) session.setStatus("Sent " + juce::String(p->name()).trimEnd() + " to the QS edit buffer (not stored).");
}

void LibraryTab::storeToUser() {
    const int prog = selectedProgram();
    auto& p = session.library.programs[size_t(bank)][size_t(prog)];
    if (!p) { alert("Nothing to store", "This program hasn't been read or loaded yet."); return; }
    if (!session.midi.isOpen()) { alert("Not connected", "Connect to the QS on the Device tab first."); return; }
    auto* w = new juce::AlertWindow("Store to User bank",
                                    "Store \"" + juce::String(p->name()).trimEnd() + "\" into which User program?\n"
                                    "This overwrites that User program on the QS. Back up first with SysEx > Ask QS to Send All.",
                                    juce::MessageBoxIconType::WarningIcon);
    w->addTextEditor("num", juce::String(prog), "User program (0-127)");
    w->addButton("Store", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    const auto data = *p;
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w, data](int r) {
        if (r != 1) return;
        const int n = w->getTextEditorContents("num").getIntValue();
        if (n < 0 || n > 127) { alert("Store", "Pick a number from 0 to 127."); return; }
        session.midi.send(data.userDump(n));
        session.library.programs[0][size_t(n)] = data;
        session.library.save();
        refresh();
        if (session.setStatus) session.setStatus("Stored " + juce::String(data.name()).trimEnd() + " to User " + juce::String(n).paddedLeft('0', 3));
    }), true);
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
    auto left = r.removeFromLeft(140);
    for (auto* b : bankButtons) {
        b->setBounds(left.removeFromTop(38));
        left.removeFromTop(6);
    }
    r.removeFromLeft(14);
    auto right = r.removeFromRight(260);
    r.removeFromRight(14);
    auto searchRow = r.removeFromTop(32);
    nextBtn.setBounds(searchRow.removeFromRight(40));
    searchRow.removeFromRight(6);
    prevBtn.setBounds(searchRow.removeFromRight(40));
    searchRow.removeFromRight(10);
    search.setBounds(searchRow);
    r.removeFromTop(8);
    list.setBounds(r);
    for (auto* b : {&readBtn, &loadBtn, &editBtn, &sendBtn, &storeBtn}) {
        b->setBounds(right.removeFromTop(36));
        right.removeFromTop(8);
    }
    right.removeFromTop(10);
    hint.setBounds(right);
}
