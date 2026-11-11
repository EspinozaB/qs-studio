#include "Pages.h"

#include "LibraryTab.h"

namespace {

void alert(const juce::String& title, const juce::String& msg) {
    qslog::action("Message: " + title + ": " + msg.replace("\n", " / "));
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, title, msg);
}

void styleLog(juce::TextEditor& t) {
    t.setMultiLine(true);
    t.setReadOnly(true);
    t.setScrollbarsShown(true);
    t.setFont(theme::monoFont(13.0f));
}

void addLine(juce::TextEditor& t, const juce::String& s) {
    t.moveCaretToEnd();
    t.insertTextAtCaret(s + "\n");
}

// Asks the QS for everything (opcode 0C) and collects the answer: 128 programs, 100 mixes, 128 effects, 1 global.
class BackupJob : public juce::ThreadWithProgressWindow {
public:
    BackupJob(Session& s, juce::File f, std::function<void(bool, juce::String, std::vector<qs::Bytes>)> done)
        : ThreadWithProgressWindow("Backing up the QS", true, true), session(s), file(f), onDone(std::move(done)) {}
    void run() override {
        MidiLink::JobScope jobScope(session.midi);
        auto& midi = session.midi;
        midi.clearQueue();
        midi.send(qs::allDumpRequest());
        setStatusMessage("Waiting for the QS...");
        while (!threadShouldExit()) {
            auto m = midi.waitFor([](const qs::Bytes& x) {
                const int op = qs::opcodeOf(x);
                return op >= 0 && op < qs::SectorErase;
            }, msgs.empty() ? 5000 : 2500, this);
            if (!m) break;
            msgs.push_back(*m);
            setProgress(juce::jmin(0.99, msgs.size() / 357.0));
            setStatusMessage("Received " + juce::String(int(msgs.size())) + " of 357 parts");
        }
        const auto sum = qs::summarize(msgs);
        ok = qs::isCompleteBackup(sum);
        if (msgs.empty()) {
            message = "The QS didn't answer. Check the connection on the Connection page.";
            return;
        }
        qs::Bytes all;
        for (auto& m : msgs) all.insert(all.end(), m.begin(), m.end());
        file.getParentDirectory().createDirectory();
        file.replaceWithData(all.data(), all.size());
        message = ok ? "Saved a complete backup (all 357 parts) to:\n" + file.getFullPathName()
                     : "Saved, but the backup is INCOMPLETE (" + juce::String(int(msgs.size())) + " of 357 parts) in:\n" + file.getFullPathName() +
                           "\n\nTry again; some messages were probably lost on the way.";
    }
    void threadComplete(bool) override {
        if (onDone) onDone(ok, message, msgs);
        delete this;
    }

private:
    Session& session;
    juce::File file;
    std::vector<qs::Bytes> msgs;
    bool ok = false;
    juce::String message;
    std::function<void(bool, juce::String, std::vector<qs::Bytes>)> onDone;
};

class SendJob : public juce::ThreadWithProgressWindow {
public:
    SendJob(Session& s, std::vector<qs::Bytes> m, juce::String title, std::function<void(bool)> done)
        : ThreadWithProgressWindow(title, true, true), session(s), msgs(std::move(m)), onDone(std::move(done)) {}
    void run() override {
        MidiLink::JobScope jobScope(session.midi);
        for (size_t i = 0; i < msgs.size() && !threadShouldExit(); ++i) {
            session.midi.send(msgs[i]);
            setProgress(double(i + 1) / double(msgs.size()));
            juce::Thread::sleep(int(MidiLink::wireSeconds(msgs[i].size()) * 1000) + 20);  // let the QS keep up
        }
        ok = !threadShouldExit();
    }
    void threadComplete(bool) override {
        if (onDone) onDone(ok);
        delete this;
    }

private:
    Session& session;
    std::vector<qs::Bytes> msgs;
    bool ok = false;
    std::function<void(bool)> onDone;
};

juce::File defaultBackupFile(const Session& s) {
    const auto model = s.deviceName.isNotEmpty() ? s.deviceName.upToFirstOccurrenceOf(" ", false, false) : juce::String("QS");
    return settingsDir().getChildFile("Backups").getChildFile(model + "_backup_" + juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H%M") + ".syx");
}

}  // namespace

// ---------------- Connection ----------------

ConnectionPage::ConnectionPage(Session& s) : session(s) {
    addAndMakeVisible(steps);
    for (juce::Component* c : std::initializer_list<juce::Component*>{&inLabel, &outLabel, &status, &inBox, &outBox, &refreshBtn, &connectBtn,
                                                                    &identifyBtn, &logBox})
        addAndMakeVisible(c);
    for (auto* l : {&inLabel, &outLabel}) {
        l->setColour(juce::Label::textColourId, theme::dim);
        l->setFont(theme::uiFont(13.0f, true));
    }
    status.setFont(theme::uiFont(18.0f, true));
    status.setText("Not connected", juce::dontSendNotification);
    styleLog(logBox);
    refreshBtn.onClick = [this] { refreshDevices(); };
    connectBtn.onClick = [this] { connect(); };
    identifyBtn.onClick = [this] {
        if (!session.midi.isOpen()) {
            alert("Not connected", "Pick the MIDI In and Out and click Connect first.");
            return;
        }
        log("> Asking the QS what it is (Universal Device Inquiry)");
        session.midi.send(qs::deviceInquiry());
    };
    refreshBtn.setTooltip("Lists the MIDI ports again, for example after plugging in the interface.");
    identifyBtn.setTooltip("Asks the synth for its model and software version. If nothing comes back, check both cables and the I/O setting.");
    session.midi.addListener(this);
    refreshDevices();

    addAndMakeVisible(filesSec);
    for (juce::Component* b : std::initializer_list<juce::Component*>{&openFolderBtn, &exportLibBtn, &importLibBtn, &openLogsBtn, &logTraffic})
        addAndMakeVisible(b);
    openFolderBtn.onClick = [] { settingsDir().startAsProcess(); };
    openLogsBtn.onClick = [] {
        auto dir = qslog::Writer::get().folder();
        dir.createDirectory();
        dir.startAsProcess();
    };
    openLogsBtn.setTooltip("Today's log is session-<date>.txt. Attach it to a bug report: it shows what you clicked, what QS Studio sent "
                           "and what the synth answered.");
    logTraffic.setToggleState(qslog::Writer::get().traffic(), juce::dontSendNotification);
    logTraffic.setTooltip("Writes every message to and from the QS (not notes) into the log. Clicks, messages and problems are always logged.");
    logTraffic.onClick = [this] { qslog::Writer::get().setTraffic(logTraffic.getToggleState()); };
    paceBox.addItem("Normal", 1);
    paceBox.addItem("Slow (2x waits)", 2);
    paceBox.addItem("Very slow (4x waits)", 3);
    paceBox.setSelectedId(settings().getIntValue("transferPace", 0) + 1, juce::dontSendNotification);
    paceBox.setTooltip("If bank reads time out or stop part way (some USB MIDI interfaces drop long SysEx), pick Slow: "
                       "QS Studio waits longer for each answer and between messages.");
    paceBox.onChange = [this] {
        settings().setValue("transferPace", paceBox.getSelectedId() - 1);
        settings().saveIfNeeded();
        qslog::action("Transfer pace: " + paceBox.getText());
    };
    paceLabel.setColour(juce::Label::textColourId, theme::dim);
    addAndMakeVisible(paceBox);
    addAndMakeVisible(paceLabel);
    exportLibBtn.setTooltip("Saves a copy of this model's library (names and programs read from your synth) to share or keep.");
    importLibBtn.setTooltip("Replaces this model's library with one you exported, or one someone shared with you.");
    exportLibBtn.onClick = [this] {
        session.library.save();
        const auto src = session.library.file();
        chooser = std::make_unique<juce::FileChooser>("Export the library", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                                                                 .getChildFile(src.getFileName()), "*.library");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [src](const juce::FileChooser& fc) {
            auto f = fc.getResult();
            if (f != juce::File()) src.copyFileTo(f.withFileExtension("library"));
        });
    };
    importLibBtn.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Import a library for your " + session.library.model, juce::File(), "*.library");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            auto f = fc.getResult();
            if (!f.existsAsFile()) return;
            session.library.save();
            session.library.file().copyFileTo(session.library.file().withFileExtension("library.before-import"));
            if (!session.library.loadFrom(f)) {
                alert("Import", f.getFileName() + " isn't a QS Studio library.");
                return;
            }
            session.library.save();
            if (session.libraryChanged) session.libraryChanged();
            alert("Import", "Imported " + f.getFileName() + " as your " + session.library.model + " library. The previous one was kept as " +
                                session.library.file().withFileExtension("library.before-import").getFileName() + ".");
        });
    };
    updateFilesText();
}

void ConnectionPage::updateFilesText() {
    filesSec.body = "QS Studio keeps everything it saves in the \"QS Studio Data\" folder next to the program, and nowhere else (no AppData, "
                    "no registry). Move or copy the QS Studio folder and everything comes along. To update, replace the program and keep "
                    "the data folder.\n"
                    "This copy uses: " + settingsDir().getFullPathName() + "\n"
                    "- QSStudio.<model>.library: names, programs and mixes read from each synth (one file per model, so your QS6 "
                    "and QSR don't overwrite each other). This one is QSStudio." + session.library.model + ".library.\n"
                    "- Backups: the backups you make.  - QSStudio.settings: ports, look and other choices.\n"
                    "- Logs: a log per day of what you did and what went to and from the QS, for bug reports. Kept 30 days.";
    resized();
    repaint();
}

ConnectionPage::~ConnectionPage() { session.midi.removeListener(this); }

void ConnectionPage::log(const juce::String& s) { addLine(logBox, s); }

void ConnectionPage::refreshDevices() {
    inBox.clear(juce::dontSendNotification);
    outBox.clear(juce::dontSendNotification);
    int i = 1;
    for (auto& d : juce::MidiInput::getAvailableDevices()) inBox.addItem(d.name, i++);
    i = 1;
    for (auto& d : juce::MidiOutput::getAvailableDevices()) outBox.addItem(d.name, i++);
    const auto savedIn = settings().getValue("midiIn"), savedOut = settings().getValue("midiOut");
    for (int k = 0; k < inBox.getNumItems(); ++k)
        if (inBox.getItemText(k) == savedIn) inBox.setSelectedItemIndex(k, juce::dontSendNotification);
    for (int k = 0; k < outBox.getNumItems(); ++k)
        if (outBox.getItemText(k) == savedOut) outBox.setSelectedItemIndex(k, juce::dontSendNotification);
    if (inBox.getSelectedId() == 0 && inBox.getNumItems() > 0) inBox.setSelectedItemIndex(0, juce::dontSendNotification);
    if (outBox.getSelectedId() == 0 && outBox.getNumItems() > 0) outBox.setSelectedItemIndex(0, juce::dontSendNotification);
    log("Found " + juce::String(inBox.getNumItems()) + " MIDI inputs and " + juce::String(outBox.getNumItems()) + " MIDI outputs.");
}

void ConnectionPage::connect() {
    juce::String err;
    if (!session.midi.open(inBox.getText(), outBox.getText(), err)) {
        status.setText("Not connected", juce::dontSendNotification);
        alert("MIDI", err);
        return;
    }
    qslog::action("Connected to MIDI in \"" + inBox.getText() + "\", out \"" + outBox.getText() + "\"");
    settings().setValue("midiIn", inBox.getText());
    settings().setValue("midiOut", outBox.getText());
    settings().saveIfNeeded();
    session.programModeSent = false;
    session.deviceName = {};
    if (onPortsChanged) onPortsChanged();
    status.setText("Connected to the ports. Waiting for the QS to answer...", juce::dontSendNotification);
    log("Connected: " + inBox.getText() + " / " + outBox.getText() + ". Asking the QS what it is...");
    session.midi.send(qs::deviceInquiry());
    juce::Timer::callAfterDelay(2500, [this] {
        if (session.midi.isOpen() && session.deviceName.isEmpty())
            log("No answer yet. Check that both MIDI cables are connected (OUT to IN both ways) and that Global > I/O on the QS is MIDI.");
    });
}

void ConnectionPage::connectSaved() {
    const auto savedIn = settings().getValue("midiIn"), savedOut = settings().getValue("midiOut");
    if (savedIn.isNotEmpty() && inBox.getText() == savedIn && outBox.getText() == savedOut) connect();
}

void ConnectionPage::sysexReceived(const qs::Bytes& msg) {
    if (auto d = qs::parseDeviceReply(msg)) {
        log("< " + juce::String(d->model) + ", OS version " + juce::String(d->version));
        status.setText("Connected to your " + juce::String(d->model) + " (OS " + juce::String(d->version) + ")", juce::dontSendNotification);
    }
}

void ConnectionPage::paint(juce::Graphics& g) { g.fillAll(theme::bg); }

void ConnectionPage::resized() {
    auto r = getLocalBounds().reduced(14);
    steps.setBounds(r.removeFromTop(48 + steps.textHeight(r.getWidth() - 32)));
    r.removeFromTop(14);
    auto labels = r.removeFromTop(18);
    inLabel.setBounds(labels.removeFromLeft(320));
    labels.removeFromLeft(16);
    outLabel.setBounds(labels.removeFromLeft(320));
    auto row = r.removeFromTop(32);
    inBox.setBounds(row.removeFromLeft(320));
    row.removeFromLeft(16);
    outBox.setBounds(row.removeFromLeft(320));
    row.removeFromLeft(16);
    refreshBtn.setBounds(row.removeFromLeft(120));
    r.removeFromTop(12);
    row = r.removeFromTop(38);
    connectBtn.setBounds(row.removeFromLeft(140));
    row.removeFromLeft(10);
    identifyBtn.setBounds(row.removeFromLeft(200));
    row.removeFromLeft(16);
    paceBox.setBounds(row.removeFromRight(190));
    paceLabel.setBounds(row.removeFromRight(100));
    status.setBounds(row);
    r.removeFromTop(12);
    const int filesH = filesSec.heightFor(r.getWidth(), 40);
    filesSec.setBounds(r.removeFromBottom(filesH));
    auto fb = filesSec.contentArea().removeFromTop(34);
    openFolderBtn.setBounds(fb.removeFromLeft(160));
    fb.removeFromLeft(10);
    exportLibBtn.setBounds(fb.removeFromLeft(180));
    fb.removeFromLeft(10);
    importLibBtn.setBounds(fb.removeFromLeft(180));
    fb.removeFromLeft(10);
    openLogsBtn.setBounds(fb.removeFromLeft(150));
    fb.removeFromLeft(14);
    logTraffic.setBounds(fb.removeFromLeft(170));
    r.removeFromBottom(12);
    logBox.setBounds(r);
}

// ---------------- Backup & Restore ----------------

BackupPage::BackupPage(Session& s) : session(s) {
    for (juce::Component* c : std::initializer_list<juce::Component*>{&backupSec, &restoreSec, &captureSec, &backupBtn, &openBtn, &restoreBtn,
                                                                    &includeGlobal, &capStart, &capStop, &capSave, &capCount, &previewTitle,
                                                                    &previewBox})
        addAndMakeVisible(c);
    styleLog(previewBox);
    previewTitle.setFont(theme::uiFont(16.0f, true));
    previewTitle.setText("Nothing opened yet", juce::dontSendNotification);
    capCount.setColour(juce::Label::textColourId, theme::dim);
    restoreBtn.setEnabled(false);
    capStop.setEnabled(false);
    capSave.setEnabled(false);
    backupBtn.setTooltip("Asks the QS for all User programs, mixes, effects and Global settings (about 30 seconds) and saves them as one file.");
    openBtn.setTooltip("Opens a .syx file so you can see what's in it before restoring it.");
    restoreBtn.setTooltip("Sends the opened file to the QS, replacing what's in its User memory.");
    includeGlobal.setTooltip("Off by default so a restore doesn't change how your QS is set up for MIDI.");
    backupBtn.onClick = [this] { backupNow(); };
    openBtn.onClick = [this] { restoreFromFile(); };
    restoreBtn.onClick = [this] { restoreLoaded(); };
    capStart.onClick = [this] {
        captured.clear();
        capturing = true;
        capStart.setEnabled(false);
        capStop.setEnabled(true);
        capCount.setText("Listening... send from the QS now.", juce::dontSendNotification);
    };
    capStop.onClick = [this] {
        capturing = false;
        capStart.setEnabled(true);
        capStop.setEnabled(false);
        capSave.setEnabled(!captured.empty());
        preview(captured, "Captured by hand");
    };
    capSave.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Save what was captured", settingsDir().getChildFile("Backups").getChildFile("captured.syx"), "*.syx");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            qs::Bytes all;
            for (auto& m : captured) all.insert(all.end(), m.begin(), m.end());
            f.withFileExtension("syx").replaceWithData(all.data(), all.size());
        });
    };
    session.midi.addListener(this);
}

BackupPage::~BackupPage() { session.midi.removeListener(this); }

void BackupPage::backupNow(std::function<void(juce::File)> then) {
    if (!session.midi.isOpen()) {
        alert("Not connected", "Connect to the QS on the Connection page first.");
        return;
    }
    const auto f = defaultBackupFile(session);
    chooser = std::make_unique<juce::FileChooser>("Save the backup as", f, "*.syx");
    f.getParentDirectory().createDirectory();
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, then](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file == juce::File()) return;
        file = file.withFileExtension("syx");
        (new BackupJob(session, file, [this, file, then](bool ok, juce::String msg, std::vector<qs::Bytes> msgs) {
            if (!msgs.empty()) preview(msgs, "Backup: " + file.getFileName());
            alert(ok ? "Backup saved" : "Backup problem", msg);
            if (session.setStatus) session.setStatus(ok ? "Backed up to " + file.getFileName() : "Backup incomplete: see the Backup & Restore page.");
            if (ok && then) then(file);
        }))->launchThread();
    });
}

void BackupPage::restoreFromFile() {
    chooser = std::make_unique<juce::FileChooser>("Choose a backup to restore", settingsDir().getChildFile("Backups"), "*.syx;*.SYX");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        juce::MemoryBlock mb;
        if (!f.existsAsFile() || !f.loadFileAsData(mb)) return;
        auto p = static_cast<const uint8_t*>(mb.getData());
        loaded = qs::splitSysex(qs::Bytes(p, p + mb.getSize()));
        restoreBtn.setEnabled(!loaded.empty());
        preview(loaded, f.getFileName());
    });
}

void BackupPage::restoreLoaded() {
    if (!session.midi.isOpen()) {
        alert("Not connected", "Connect to the QS on the Connection page first.");
        return;
    }
    std::vector<qs::Bytes> msgs;
    for (auto& m : loaded) {
        const int op = qs::opcodeOf(m);
        if (op < 0 || op >= qs::SectorErase) continue;
        if (op == qs::GlobalDump && !includeGlobal.getToggleState()) continue;
        msgs.push_back(m);
    }
    if (msgs.empty()) {
        alert("Nothing to send", "This file has no QS data to restore.");
        return;
    }
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::WarningIcon)
                                     .withTitle("Replace the QS's User memory?")
                                     .withMessage("This sends " + juce::String(int(msgs.size())) +
                                                  " parts to the QS and replaces its User programs, mixes and effects with the ones in the file.")
                                     .withButton("Restore")
                                     .withButton("Cancel"),
                                 [this, msgs](int r) {
                                     if (r != 1) return;
                                     (new SendJob(session, msgs, "Restoring to the QS", [this](bool ok) {
                                         alert("Restore", ok ? "Done. The QS now has the programs, mixes and effects from the file."
                                                             : "Stopped part way. Run the restore again to finish.");
                                     }))->launchThread();
                                 });
}

void BackupPage::sysexReceived(const qs::Bytes& msg) {
    if (!capturing) return;
    const int op = qs::opcodeOf(msg);
    if (op >= qs::SectorErase && op <= qs::FlashNack) return;
    captured.push_back(msg);
    capCount.setText(juce::String(int(captured.size())) + " messages captured", juce::dontSendNotification);
}

void BackupPage::preview(const std::vector<qs::Bytes>& msgs, const juce::String& title) {
    auto s = qs::summarize(msgs);
    previewTitle.setText(title, juce::dontSendNotification);
    previewBox.clear();
    addLine(previewBox, juce::String(s.programs) + " programs, " + juce::String(s.mixes + s.oldMixes) + " mixes, " + juce::String(s.effects) +
                            " effects, " + juce::String(s.globals) + " Global" + (s.other ? ", " + juce::String(s.other) + " other" : juce::String()));
    if (s.programs + s.mixes + s.oldMixes + s.effects + s.globals > 0)
        addLine(previewBox, qs::isCompleteBackup(s) ? juce::String("A complete backup.")
                                                    : juce::String("Partial: a complete backup has 128 programs, 100 mixes, 128 effects and 1 Global."));
    if (!s.programNames.empty()) {
        addLine(previewBox, "\nPROGRAMS");
        for (auto& [n, name] : s.programNames) addLine(previewBox, juce::String(n).paddedLeft('0', 3) + "  " + juce::String(name));
    }
    if (!s.mixNames.empty()) {
        addLine(previewBox, "\nMIXES");
        for (auto& [n, name] : s.mixNames) addLine(previewBox, juce::String(n).paddedLeft('0', 2) + "  " + juce::String(name));
    }
    previewBox.moveCaretToTop(false);
}

void BackupPage::paint(juce::Graphics& g) { g.fillAll(theme::bg); }

void BackupPage::resized() {
    auto r = getLocalBounds().reduced(14);
    auto left = r.removeFromLeft(juce::jmin(620, r.getWidth() / 2));
    r.removeFromLeft(14);
    auto place = [&left](Section& sec, int controlsH) {
        const int h = 48 + sec.textHeight(left.getWidth() - 32) + controlsH;
        sec.setBounds(left.removeFromTop(h));
        left.removeFromTop(12);
        return sec.getBounds().withTrimmedTop(h - controlsH - 12).reduced(16, 0).withHeight(controlsH);
    };
    auto b = place(backupSec, 40);
    backupBtn.setBounds(b.removeFromLeft(180).withHeight(34));
    auto rs = place(restoreSec, 84);
    auto row = rs.removeFromTop(34);
    openBtn.setBounds(row.removeFromLeft(210));
    row.removeFromLeft(10);
    restoreBtn.setBounds(row.removeFromLeft(190));
    rs.removeFromTop(8);
    includeGlobal.setBounds(rs.removeFromTop(28));
    auto cp = place(captureSec, 40);
    row = cp.removeFromTop(34);
    capStart.setBounds(row.removeFromLeft(90));
    row.removeFromLeft(8);
    capStop.setBounds(row.removeFromLeft(90));
    row.removeFromLeft(8);
    capSave.setBounds(row.removeFromLeft(90));
    row.removeFromLeft(12);
    capCount.setBounds(row);
    previewTitle.setBounds(r.removeFromTop(28));
    previewBox.setBounds(r);
}

// ---------------- Move banks ----------------

MoveBanksPage::MoveBanksPage(Session& s, BackupPage& b, LibraryTab& l) : session(s), backupPage(b), library(l) {
    for (juce::Component* c : std::initializer_list<juce::Component*>{&why, &step1, &step2, &step3, &backupBtn, &convertBtn, &restoreBtn, &cardBtn})
        addAndMakeVisible(c);
    backupBtn.onClick = [this] { backupPage.backupNow(); };
    convertBtn.onClick = [this] { library.convertBankFile(); };
    cardBtn.onClick = [this] {
        auto* w = new juce::AlertWindow("Copy onto a RAM card bank", "Which card bank? With one RAM512 in slot A, banks 1-8 are on that card.",
                                        juce::MessageBoxIconType::QuestionIcon);
        juce::StringArray banks;
        for (int i = 1; i <= 8; ++i) banks.add("Card bank " + juce::String(i));
        w->addComboBox("bank", banks);
        w->addButton("Next", 1, juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
            if (r != 1) return;
            if (goToPrograms) goToPrograms();
            library.showBank(kFirstCardBank + w->getComboBoxComponent("bank")->getSelectedItemIndex());
            library.copyBankToCard();
        }), true);
    };
    restoreBtn.setTooltip("Opens the converted file on the Backup & Restore page, where you can check it and send it.");
    restoreBtn.onClick = [this] {
        if (goToBackup) goToBackup();
        backupPage.restoreFromFile();
    };
}

void MoveBanksPage::paint(juce::Graphics& g) { g.fillAll(theme::bg); }

void MoveBanksPage::resized() {
    auto r = getLocalBounds().reduced(14);
    why.setBounds(r.removeFromTop(48 + why.textHeight(r.getWidth() - 32)));
    r.removeFromTop(14);
    const int w = (r.getWidth() - 28) / 3;
    Section* steps[] = {&step1, &step2, &step3};
    int h = 0;
    for (auto* s : steps) h = juce::jmax(h, 48 + s->textHeight(w - 32));
    h += 100;
    auto row = r.removeFromTop(h);
    for (auto* s : steps) {
        s->setBounds(row.removeFromLeft(w));
        row.removeFromLeft(14);
    }
    auto btnArea = [](Section& s) { return s.getBounds().reduced(16, 14).removeFromBottom(80); };
    auto a1 = btnArea(step1);
    backupBtn.setBounds(a1.removeFromBottom(36).removeFromLeft(juce::jmin(260, a1.getWidth())));
    auto a2 = btnArea(step2);
    convertBtn.setBounds(a2.removeFromBottom(36).removeFromLeft(juce::jmin(220, a2.getWidth())));
    auto a3 = btnArea(step3);
    cardBtn.setBounds(a3.removeFromBottom(36).removeFromLeft(juce::jmin(260, a3.getWidth())));
    a3.removeFromBottom(8);
    restoreBtn.setBounds(a3.removeFromBottom(36).removeFromLeft(juce::jmin(260, a3.getWidth())));
}

// ---------------- Global ----------------

namespace {
struct GlobalField { const char* label; int page, pot; };
// Global data, opcode 10 addressing (mode 0, function 0): from the QS SysEx spec.
const GlobalField kCcFields[] = {{"Controller A (MIDI CC)", 2, 0}, {"Controller B (MIDI CC)", 2, 1}, {"Controller C (MIDI CC)", 2, 2},
                                 {"Controller D (MIDI CC)", 2, 3}, {"Pedal 1 (MIDI CC)", 4, 0},      {"Pedal 2 (MIDI CC)", 4, 2}};
}  // namespace

GlobalPage::GlobalPage(Session& s) : session(s) {
    addAndMakeVisible(settingsSec);
    addAndMakeVisible(tryCtl);
    addAndMakeVisible(readBtn);
    readBtn.onClick = [this] {
        if (!session.midi.isOpen()) {
            alert("Not connected", "Connect to the QS on the Connection page first.");
            return;
        }
        session.midi.send(qs::globalRequest());
    };
    for (int i = 0; i < 6; ++i) {
        auto* l = labels.add(new juce::Label({}, kCcFields[i].label));
        l->setColour(juce::Label::textColourId, theme::dim);
        addAndMakeVisible(l);
        auto* sl = ccBoxes.add(new juce::Slider(juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft));
        sl->setRange(0, 120, 1);
        sl->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 50, 26);
        sl->onValueChange = [this, i, sl] {
            if (loading) return;
            if (i < 4) session.global.controller[i] = int(sl->getValue());
            else session.global.pedal[i - 4] = int(sl->getValue());
            sendGlobal(kCcFields[i].page, kCcFields[i].pot, int(sl->getValue()));
        };
        addAndMakeVisible(sl);
    }
    auto* psl = labels.add(new juce::Label({}, "MIDI Program Select"));
    psl->setColour(juce::Label::textColourId, theme::dim);
    addAndMakeVisible(psl);
    progSelect.addItem("Off (ignores program changes)", 1);
    progSelect.addItem("On", 2);
    for (int ch = 1; ch <= 16; ++ch) progSelect.addItem("On, channel " + juce::String(ch) + " only", ch + 2);
    progSelect.onChange = [this] {
        if (loading) return;
        session.global.programSelect = progSelect.getSelectedId() - 1;
        sendGlobal(5, 0, progSelect.getSelectedId() - 1);
    };
    progSelect.setTooltip("QS Studio switches programs with program changes, so this must not be Off. To pick mixes from QS Studio it has to be a channel (the one QS Studio uses).");
    addAndMakeVisible(progSelect);
    gmToggle.onClick = [this] {
        if (!loading) sendGlobal(0, 1, gmToggle.getToggleState() ? 1 : 0);
    };
    addAndMakeVisible(gmToggle);
    for (int i = 0; i < 4; ++i) {
        auto* t = tryers.add(new juce::Slider(juce::Slider::LinearVertical, juce::Slider::TextBoxBelow));
        t->setRange(0, 127, 1);
        t->setTextBoxStyle(juce::Slider::TextBoxBelow, true, 44, 20);
        t->onValueChange = [this, i, t] {
            session.midi.sendMidi(juce::MidiMessage::controllerEvent(session.channel, session.global.controller[i], int(t->getValue())));
        };
        addAndMakeVisible(t);
        auto* l = tryLabels.add(new juce::Label({}, juce::String::charToString(juce::juce_wchar('A' + i))));
        l->setJustificationType(juce::Justification::centred);
        l->setFont(theme::uiFont(14.0f, true));
        addAndMakeVisible(l);
    }
    session.midi.addListener(this);
    refreshFromSession();
}

GlobalPage::~GlobalPage() { session.midi.removeListener(this); }

void GlobalPage::refreshFromSession() {
    const juce::ScopedValueSetter<bool> svs(loading, true);
    for (int i = 0; i < 4; ++i) ccBoxes[i]->setValue(session.global.controller[i], juce::dontSendNotification);
    for (int i = 0; i < 2; ++i) ccBoxes[4 + i]->setValue(session.global.pedal[i], juce::dontSendNotification);
    progSelect.setSelectedId(session.global.programSelect + 1, juce::dontSendNotification);
    gmToggle.setToggleState(session.global.generalMidi, juce::dontSendNotification);
    for (int i = 0; i < 4; ++i) tryLabels[i]->setText(juce::String::charToString(juce::juce_wchar('A' + i)) + "  CC" + juce::String(session.global.controller[i]),
                                                      juce::dontSendNotification);
}

void GlobalPage::sendGlobal(int page, int pot, int value) {
    if (session.midi.isOpen()) session.midi.send(qs::paramEdit(0, 0, 0, page, 0, pot, value));
    refreshFromSession();
}

void GlobalPage::sysexReceived(const qs::Bytes& msg) {
    if (qs::opcodeOf(msg) == qs::GlobalDump) refreshFromSession();  // MainComponent stores it in the session first
}

void GlobalPage::paint(juce::Graphics& g) { g.fillAll(theme::bg); }

void GlobalPage::resized() {
    auto r = getLocalBounds().reduced(14);
    auto left = r.removeFromLeft(juce::jmin(600, r.getWidth() * 3 / 5));
    r.removeFromLeft(14);
    const int rows = 8;
    settingsSec.setBounds(left.removeFromTop(48 + settingsSec.textHeight(left.getWidth() - 32) + 46 + rows * 40));
    auto c = settingsSec.contentArea();
    readBtn.setBounds(c.removeFromTop(34).removeFromLeft(160));
    c.removeFromTop(12);
    for (int i = 0; i < 6; ++i) {
        auto row = c.removeFromTop(34);
        labels[i]->setBounds(row.removeFromLeft(220));
        ccBoxes[i]->setBounds(row.removeFromLeft(140));
        c.removeFromTop(6);
    }
    auto row = c.removeFromTop(34);
    labels[6]->setBounds(row.removeFromLeft(220));
    progSelect.setBounds(row.removeFromLeft(260));
    c.removeFromTop(6);
    gmToggle.setBounds(c.removeFromTop(30).removeFromLeft(260));
    tryCtl.setBounds(r.removeFromTop(48 + tryCtl.textHeight(r.getWidth() - 32) + 230));
    auto t = tryCtl.contentArea();
    const int w = t.getWidth() / 4;
    for (int i = 0; i < 4; ++i) {
        auto col = t.removeFromLeft(w);
        tryLabels[i]->setBounds(col.removeFromTop(22));
        tryers[i]->setBounds(col.reduced(w / 4, 0));
    }
}
