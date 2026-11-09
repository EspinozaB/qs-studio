#include "MainComponent.h"

#include "CardJobs.h"
#include "EditorTab.h"
#include "LibraryTab.h"
#include "RecorderTab.h"

// ---------- helpers ----------
juce::String hex(const qs::Bytes& b, size_t maxBytes) {
    juce::String s;
    for (size_t i = 0; i < b.size() && i < maxBytes; ++i) s << juce::String::toHexString(int(b[i])).paddedLeft('0', 2) << " ";
    if (b.size() > maxBytes) s << "... (" << int(b.size()) << " bytes)";
    return s.trim();
}

// Portable: settings live next to the .exe when that folder is writable.
juce::File settingsDir() {
    auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    if (exeDir.hasWriteAccess()) return exeDir;
    auto d = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("QS Studio");
    d.createDirectory();
    return d;
}

juce::PropertiesFile& settings() {
    static juce::PropertiesFile p(settingsDir().getChildFile("QSStudio.settings"), juce::PropertiesFile::Options());
    return p;
}

static void setupLog(juce::TextEditor& t) {
    t.setMultiLine(true);
    t.setReadOnly(true);
    t.setScrollbarsShown(true);
    t.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
}

static void appendLine(juce::TextEditor& t, const juce::String& s) {
    t.moveCaretToEnd();
    t.insertTextAtCaret(s + "\n");
}

static void info(const juce::String& title, const juce::String& msg) {
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, title, msg);
}

static qs::Bytes toBytes(const juce::MemoryBlock& mb) {
    auto p = static_cast<const uint8_t*>(mb.getData());
    return qs::Bytes(p, p + mb.getSize());
}

// ---------- Device ----------
DeviceTab::DeviceTab(MidiLink& m) : midi(m) {
    for (juce::Component* c : std::initializer_list<juce::Component*>{&inLabel, &outLabel, &status, &inBox, &outBox, &refreshBtn, &connectBtn, &identifyBtn, &log})
        addAndMakeVisible(c);
    setupLog(log);
    status.setText("Not connected", juce::dontSendNotification);
    refreshBtn.onClick = [this] { refreshDevices(); };
    connectBtn.onClick = [this] { connect(); };
    identifyBtn.onClick = [this] {
        if (!midi.isOpen()) { info("Not connected", "Pick your MIDI In and Out and click Connect first."); return; }
        appendLine(log, "> Device inquiry");
        midi.send(qs::deviceInquiry());
    };
    midi.addListener(this);
    refreshDevices();
    appendLine(log, "Connect the QS MIDI OUT to your interface's MIDI IN, and the interface's MIDI OUT to the QS MIDI IN.");
    appendLine(log, "On the QS, the Global I/O setting must be MIDI (not PC or Mac serial).");
}

DeviceTab::~DeviceTab() { midi.removeListener(this); }

void DeviceTab::refreshDevices() {
    inBox.clear();
    outBox.clear();
    inBox.addItemList(midi.inputNames(), 1);
    outBox.addItemList(midi.outputNames(), 1);
    auto lastIn = settings().getValue("midiIn"), lastOut = settings().getValue("midiOut");
    for (int i = 0; i < inBox.getNumItems(); ++i) if (inBox.getItemText(i) == lastIn) inBox.setSelectedItemIndex(i);
    for (int i = 0; i < outBox.getNumItems(); ++i) if (outBox.getItemText(i) == lastOut) outBox.setSelectedItemIndex(i);
    if (inBox.getSelectedId() == 0 && inBox.getNumItems() > 0) inBox.setSelectedItemIndex(0);
    if (outBox.getSelectedId() == 0 && outBox.getNumItems() > 0) outBox.setSelectedItemIndex(0);
}

void DeviceTab::connect() {
    juce::String err;
    if (!midi.open(inBox.getText(), outBox.getText(), err)) {
        status.setText("Not connected", juce::dontSendNotification);
        info("MIDI", err);
        return;
    }
    settings().setValue("midiIn", inBox.getText());
    settings().setValue("midiOut", outBox.getText());
    settings().saveIfNeeded();
    status.setText("Connected: " + inBox.getText() + " / " + outBox.getText(), juce::dontSendNotification);
    appendLine(log, "Connected. Asking the QS to identify itself...");
    midi.send(qs::deviceInquiry());
}

void DeviceTab::sysexReceived(const qs::Bytes& msg) {
    if (auto d = qs::parseDeviceReply(msg)) {
        appendLine(log, "< " + juce::String(d->model) + ", OS version " + juce::String(d->version));
        status.setText("Connected to " + juce::String(d->model) + " v" + juce::String(d->version), juce::dontSendNotification);
    }
}

void DeviceTab::resized() {
    auto r = getLocalBounds().reduced(12);
    auto row = r.removeFromTop(28);
    inLabel.setBounds(row.removeFromLeft(70));
    inBox.setBounds(row.removeFromLeft(260));
    row.removeFromLeft(12);
    outLabel.setBounds(row.removeFromLeft(70));
    outBox.setBounds(row.removeFromLeft(260));
    r.removeFromTop(8);
    row = r.removeFromTop(28);
    refreshBtn.setBounds(row.removeFromLeft(90));
    row.removeFromLeft(8);
    connectBtn.setBounds(row.removeFromLeft(100));
    row.removeFromLeft(8);
    identifyBtn.setBounds(row.removeFromLeft(130));
    row.removeFromLeft(12);
    status.setBounds(row);
    r.removeFromTop(8);
    log.setBounds(r);
}

// ---------- SysEx librarian ----------
SysexTab::SysexTab(MidiLink& m) : midi(m) {
    for (juce::Component* c : std::initializer_list<juce::Component*>{&recBtn, &stopBtn, &saveBtn, &requestBtn, &loadBtn, &sendBtn, &countLabel, &help, &summary})
        addAndMakeVisible(c);
    setupLog(summary);
    help.setText("Record, then on the QS press STORE, PAGE up 4 times, STORE (\"SEND ALL DATA TO MIDI\"). "
                 "Or click Ask QS to Send All.",
                 juce::dontSendNotification);
    stopBtn.setEnabled(false);
    saveBtn.setEnabled(false);
    sendBtn.setEnabled(false);
    recBtn.onClick = [this] {
        captured.clear();
        recording = true;
        recBtn.setEnabled(false);
        stopBtn.setEnabled(true);
        countLabel.setText("Recording...", juce::dontSendNotification);
    };
    stopBtn.onClick = [this] {
        recording = false;
        recBtn.setEnabled(true);
        stopBtn.setEnabled(false);
        saveBtn.setEnabled(!captured.empty());
        updateSummary(captured, "Captured");
    };
    requestBtn.onClick = [this] {
        if (!midi.isOpen()) { info("Not connected", "Connect on the Device tab first."); return; }
        if (!recording) recBtn.onClick();
        midi.send(qs::allDumpRequest());
    };
    saveBtn.onClick = [this] { save(); };
    loadBtn.onClick = [this] { load(); };
    sendBtn.onClick = [this] { sendLoaded(); };
    midi.addListener(this);
}

SysexTab::~SysexTab() { midi.removeListener(this); }

void SysexTab::sysexReceived(const qs::Bytes& msg) {
    if (!recording) return;
    const int op = qs::opcodeOf(msg);
    if (op >= qs::SectorErase && op <= qs::FlashNack) return;  // card traffic is not a patch dump
    captured.push_back(msg);
    size_t bytes = 0;
    for (auto& c : captured) bytes += c.size();
    countLabel.setText(juce::String(int(captured.size())) + " messages, " + juce::String(int(bytes)) + " bytes",
                       juce::dontSendNotification);
}

void SysexTab::updateSummary(const std::vector<qs::Bytes>& msgs, const juce::String& title) {
    auto s = qs::summarize(msgs);
    summary.clear();
    appendLine(summary, title + ": " + juce::String(int(msgs.size())) + " messages");
    appendLine(summary, "Programs " + juce::String(s.programs) + ", Mixes " + juce::String(s.mixes + s.oldMixes) + ", Effects " +
                            juce::String(s.effects) + ", Global " + juce::String(s.globals) + ", Other " + juce::String(s.other));
    if (s.programs + s.mixes + s.oldMixes + s.effects + s.globals > 0)
        appendLine(summary, qs::isCompleteBackup(s)
                                ? juce::String("Complete backup (all 357 parts).")
                                : "INCOMPLETE: a full Send All is 128 programs, 100 mixes, 128 effects and 1 global (357 parts). "
                                  "Record again before relying on this file.");
    if (!s.programNames.empty()) {
        appendLine(summary, "\nPROGRAMS");
        for (auto& [n, name] : s.programNames) appendLine(summary, juce::String(n).paddedLeft('0', 3) + "  " + juce::String(name));
    }
    if (!s.mixNames.empty()) {
        appendLine(summary, "\nMIXES");
        for (auto& [n, name] : s.mixNames) appendLine(summary, juce::String(n).paddedLeft('0', 2) + "  " + juce::String(name));
    }
}

void SysexTab::save() {
    chooser = std::make_unique<juce::FileChooser>("Save SysEx", settingsDir().getChildFile("qs_dump.syx"), "*.syx");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this](const juce::FileChooser& fc) {
                             auto f = fc.getResult();
                             if (f == juce::File()) return;
                             juce::MemoryBlock mb;
                             for (auto& m : captured) mb.append(m.data(), m.size());
                             f.withFileExtension("syx").replaceWithData(mb.getData(), mb.getSize());
                         });
}

void SysexTab::load() {
    chooser = std::make_unique<juce::FileChooser>("Open SysEx", settingsDir(), "*.syx");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc) {
                             auto f = fc.getResult();
                             juce::MemoryBlock mb;
                             if (f == juce::File() || !f.loadFileAsData(mb)) return;
                             loaded = qs::splitSysex(toBytes(mb));
                             sendBtn.setEnabled(!loaded.empty());
                             updateSummary(loaded, f.getFileName());
                         });
}

void SysexTab::sendLoaded() {
    if (!midi.isOpen()) { info("Not connected", "Connect on the Device tab first."); return; }
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Send to QS?")
            .withMessage("This overwrites whatever the file contains on the QS (usually the whole User bank). Continue?")
            .withButton("Send")
            .withButton("Cancel"),
        [this](int result) {
            if (result != 1) return;
            auto msgs = loaded;
            juce::Thread::launch([this, msgs] {
                for (auto& m : msgs) {
                    midi.send(m);
                    juce::Thread::sleep(int(MidiLink::wireSeconds(m.size()) * 1000) + 20);  // let the QS keep up
                }
                juce::MessageManager::callAsync([] { info("Done", "Sent."); });
            });
        });
}

void SysexTab::resized() {
    auto r = getLocalBounds().reduced(12);
    help.setBounds(r.removeFromTop(40));
    auto row = r.removeFromTop(28);
    for (auto* b : {&recBtn, &stopBtn, &saveBtn, &requestBtn}) {
        b->setBounds(row.removeFromLeft(b == &requestBtn ? 150 : 90));
        row.removeFromLeft(8);
    }
    countLabel.setBounds(row);
    r.removeFromTop(8);
    row = r.removeFromTop(28);
    loadBtn.setBounds(row.removeFromLeft(110));
    row.removeFromLeft(8);
    sendBtn.setBounds(row.removeFromLeft(110));
    r.removeFromTop(8);
    summary.setBounds(r);
}

// ---------- Card tools ----------
CardTab::CardTab(MidiLink& m) : midi(m) {
    for (juce::Component* c : std::initializer_list<juce::Component*>{&openBtn, &wavBtn, &sfzBtn, &testBtn, &readBtn, &writeBtn, &sizeBox, &sizeLabel, &info})
        addAndMakeVisible(c);
    setupLog(info);
    sizeBox.addItem("512 KB (RAM512)", 4);
    sizeBox.addItem("2 MB", 16);
    sizeBox.addItem("4 MB", 32);
    sizeBox.addItem("8 MB", 64);
    sizeBox.setSelectedId(64);
    wavBtn.setEnabled(false);
    sfzBtn.setEnabled(false);
    openBtn.onClick = [this] { openImage(); };
    wavBtn.onClick = [this] { exportWavs(false); };
    sfzBtn.onClick = [this] { exportWavs(true); };
    testBtn.onClick = [this] { testReader(); };
    readBtn.onClick = [this] { readCard(); };
    writeBtn.onClick = [this] { writeCard(); };
    appendLine(info, "Open a QCard / QuadraCard / Sound Bridge .img to see its voices, programs and mixes and export samples.");
    appendLine(info, "Test card reader asks the QS for the first 1 KB of the card in its slot. Power the QS off before inserting a card.");
}

bool CardTab::requireMidi() {
    if (midi.isOpen()) return true;
    ::info("Not connected", "Connect to the QS on the Device tab first.");
    return false;
}

void CardTab::openImage() {
    chooser = std::make_unique<juce::FileChooser>("Open card image", settingsDir(), "*.img;*.bin");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc) {
                             auto f = fc.getResult();
                             juce::MemoryBlock mb;
                             if (f == juce::File() || !f.loadFileAsData(mb)) return;
                             std::string err;
                             if (!qs::loadCardImage(toBytes(mb), card, err)) { ::info("Can't read image", err); return; }
                             cardFile = f;
                             showCard();
                         });
}

void CardTab::showCard() {
    info.clear();
    size_t zones = 0;
    for (auto& v : card.voices) zones += v.zones.size();
    appendLine(info, cardFile.getFileName() + "   card: " + juce::String(card.cardName) + "   " +
                         juce::String(int(card.data.size() / 1024)) + " KB");
    for (auto& f : card.fixes) appendLine(info, "fixed: " + juce::String(f));
    juce::StringArray g;
    for (auto& x : card.groups) g.add(juce::String(x).trim());
    appendLine(info, "Groups: " + g.joinIntoString(", "));
    appendLine(info, "Voices " + juce::String(int(card.voices.size())) + ", zones " + juce::String(int(zones)) + ", unique samples " +
                         juce::String(int(card.uniqueSampleCount())));
    appendLine(info, "\nPROGRAMS");
    for (int i = 0; i < int(card.programNames.size()); ++i)
        appendLine(info, juce::String(i).paddedLeft('0', 3) + "  " + juce::String(card.programNames[size_t(i)]));
    appendLine(info, "\nMIXES");
    for (int i = 0; i < int(card.mixNames.size()); ++i)
        appendLine(info, juce::String(i).paddedLeft('0', 2) + "  " + juce::String(card.mixNames[size_t(i)]));
    appendLine(info, "\nVOICES");
    for (auto& v : card.voices)
        appendLine(info, "g" + juce::String::toHexString(v.group) + " " + juce::String(v.index).paddedLeft(' ', 3) + "  " +
                             juce::String(v.name) + "  zones " + juce::String(int(v.zones.size())) +
                             (v.layers > 1 ? "  vel layers " + juce::String(v.layers) : ""));
    wavBtn.setEnabled(!card.voices.empty());
    sfzBtn.setEnabled(!card.voices.empty());
    info.moveCaretToTop(false);
}

void CardTab::exportWavs(bool sfz) {
    chooser = std::make_unique<juce::FileChooser>("Choose a folder", cardFile.getParentDirectory());
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                         [this, sfz](const juce::FileChooser& fc) {
                             auto dir = fc.getResult();
                             if (dir == juce::File()) return;
                             auto out = dir.getChildFile(qs::safeName(cardFile.getFileNameWithoutExtension().toStdString()));
                             auto wavDir = sfz ? out.getChildFile("samples") : out;
                             wavDir.createDirectory();
                             auto samples = qs::exportSamples(card);
                             for (auto& s : samples) wavDir.getChildFile(s.fileName).replaceWithData(s.wav.data(), s.wav.size());
                             int n = 0;
                             if (sfz)
                                 for (auto& [name, text] : qs::exportSfz(card)) { out.getChildFile(name).replaceWithText(text); ++n; }
                             ::info("Exported", juce::String(int(samples.size())) + " samples" +
                                                    (sfz ? " and " + juce::String(n) + " SFZ instruments" : juce::String()) + " in\n" +
                                                    out.getFullPathName());
                         });
}

void CardTab::testReader() {
    if (!requireMidi()) return;
    appendLine(info, "\n> Requesting card sector 0, block 0...");
    juce::Thread::launch([this] {
        midi.clearQueue();
        auto r = cardjobs::readBlock(midi, 0, 0, nullptr);
        juce::String msg;
        switch (r.kind) {
            case cardjobs::BlockResult::Ok:
            case cardjobs::BlockResult::BadChecksum:
                msg = "< Got 1 KB. Checksum " + juce::String(r.kind == cardjobs::BlockResult::Ok ? "OK" : "MISMATCH") +
                      ".\n  First bytes: " + hex(r.data, 16) + "\n  " +
                      (qs::looksLikeInternalRom(r.data)
                           ? "This is NOT the card. Your QS answered with its built-in sound ROM. Card reads on this unit "
                             "return internal memory, so Read card will save the internal ROM, not the card. "
                             "Please send this result to the QS Studio issue tracker." :
                       qs::looksLikeCard(r.data) ? "That's a QS sound card header. The reader works." :
                       "Not a sample-card header (RAM program cards and blank FLASH look different). The reader works.");
                break;
            case cardjobs::BlockResult::Nack:
                msg = "< The QS answered: " + juce::String(qs::nackText(r.nackCode)) + ". The protocol works; check the card.";
                break;
            default:
                msg = "< No reply in 4 seconds. Check both MIDI cables, the Device tab connection, and that the QS I/O setting is MIDI.";
        }
        juce::MessageManager::callAsync([this, msg] { appendLine(info, msg); });
    });
}

void CardTab::readCard() {
    if (!requireMidi()) return;
    const int sectors = sizeBox.getSelectedId();
    chooser = std::make_unique<juce::FileChooser>("Save card image as", settingsDir().getChildFile("card_dump.img"), "*.img");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this, sectors](const juce::FileChooser& fc) {
                             auto f = fc.getResult();
                             if (f == juce::File()) return;
                             auto* job = new cardjobs::ReadCardJob(midi, sectors, f.withFileExtension("img"), [this](juce::String r) {
                                 appendLine(info, "\n" + r);
                                 ::info("Card read", r);
                             });
                             job->launchThread();
                         });
}

void CardTab::writeCard() {
    if (!requireMidi()) return;
    if (card.data.empty()) { ::info("No image", "Open the card image to write first."); return; }
    const size_t cardBytes = size_t(sizeBox.getSelectedId()) * 128 * 1024;
    if (card.data.size() > cardBytes) {
        ::info("Too big", "This image is larger than the card size selected.");
        return;
    }
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Erase and write the card?")
            .withMessage("This ERASES the FLASH card in the QS and writes " + cardFile.getFileName() + ".\n\n"
                         "Needs a 5 V AMD-type FLASH card with its write-protect switch off. "
                         "It takes about 8 minutes per MB. Don't power off the QS while it runs.")
            .withButton("Erase and write")
            .withButton("Cancel"),
        [this](int result) {
            if (result != 1) return;
            auto* job = new cardjobs::WriteCardJob(midi, card.data, [this](juce::String r) {
                appendLine(info, "\n" + r);
                ::info("Card write", r);
            });
            job->launchThread();
        });
}

void CardTab::resized() {
    auto r = getLocalBounds().reduced(12);
    auto row = r.removeFromTop(28);
    for (auto* b : {&openBtn, &wavBtn, &sfzBtn}) {
        b->setBounds(row.removeFromLeft(b == &sfzBtn ? 170 : 130));
        row.removeFromLeft(8);
    }
    r.removeFromTop(8);
    row = r.removeFromTop(28);
    sizeLabel.setBounds(row.removeFromLeft(70));
    sizeBox.setBounds(row.removeFromLeft(150));
    row.removeFromLeft(12);
    for (auto* b : {&testBtn, &readBtn, &writeBtn}) {
        b->setBounds(row.removeFromLeft(b == &writeBtn ? 190 : 140));
        row.removeFromLeft(8);
    }
    r.removeFromTop(8);
    info.setBounds(r);
}

// ---------- Main window ----------
MainComponent::MainComponent() {
    library = new LibraryTab(session);
    editor = new EditorTab(session);
    tabs.addTab("Library", theme::bg, library, true);
    tabs.addTab("Editor", theme::bg, editor, true);
    tabs.addTab("Record", theme::bg, new RecorderTab(session), true);
    tabs.addTab("Device", theme::bg, new DeviceTab(session.midi), true);
    tabs.addTab("SysEx Librarian", theme::bg, new SysexTab(session.midi), true);
    tabs.addTab("Card Tools", theme::bg, new CardTab(session.midi), true);
    tabs.setTabBarDepth(44);
    tabs.setOutline(0);
    // Not connected yet: start on the Device tab.
    tabs.setCurrentTabIndex(3);
    addAndMakeVisible(tabs);

    session.openInEditor = [this](const qs::Program& p, const juce::String& where) {
        editor->openProgram(p, where);
        tabs.setCurrentTabIndex(1);
    };
    session.setStatus = [this](const juce::String& s) { statusLabel.setText(s, juce::dontSendNotification); };
    session.midi.addListener(this);

    keyboard.setAvailableRange(21, 108);
    keyboard.setLowestVisibleKey(36);
    keyboard.setKeyPressBaseOctave(4);
    keyboard.setScrollButtonWidth(22);
    addAndMakeVisible(keyboard);
    for (int ch = 1; ch <= 16; ++ch) channelBox.addItem(juce::String(ch), ch);
    channelBox.setSelectedId(session.channel, juce::dontSendNotification);
    channelBox.setTooltip("The MIDI channel the QS listens on in Program mode");
    channelBox.onChange = [this] {
        session.setChannel(channelBox.getSelectedId());
        library->refresh();
    };
    velSlider.setRange(1, 127, 1);
    velSlider.setValue(settings().getIntValue("keyVelocity", 100), juce::dontSendNotification);
    velSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 24);
    auto applyVel = [this] {
        keyboard.setVelocity(float(velSlider.getValue()) / 127.0f, false);
        settings().setValue("keyVelocity", int(velSlider.getValue()));
    };
    velSlider.onValueChange = applyVel;
    applyVel();
    notesOffBtn.onClick = [this] { session.allNotesOff(); };
    statusLabel.setColour(juce::Label::textColourId, theme::dim);
    statusLabel.setFont(theme::uiFont(13.0f));
    for (juce::Component* c : std::initializer_list<juce::Component*>{&chLabel, &velLabel, &channelBox, &velSlider, &notesOffBtn, &statusLabel})
        addAndMakeVisible(c);
    for (auto* l : {&chLabel, &velLabel}) {
        l->setColour(juce::Label::textColourId, theme::dim);
        l->setFont(theme::uiFont(13.0f, true));
    }
    setSize(1360, 920);
}

MainComponent::~MainComponent() {
    session.midi.removeListener(this);
    session.openInEditor = nullptr;
    session.setStatus = nullptr;
}

void MainComponent::sysexReceived(const qs::Bytes& msg) {
    if (auto d = qs::parseDeviceReply(msg)) {
        session.deviceName = juce::String(d->model) + " v" + juce::String(d->version);
        statusLabel.setText("Connected to " + session.deviceName, juce::dontSendNotification);
        library->refresh();
    }
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(theme::bg);
    g.setColour(theme::panel);
    g.fillRect(getLocalBounds().removeFromBottom(150));
}

void MainComponent::resized() {
    auto r = getLocalBounds();
    auto bottom = r.removeFromBottom(150).reduced(12, 8);
    auto bar = bottom.removeFromTop(30);
    chLabel.setBounds(bar.removeFromLeft(60));
    channelBox.setBounds(bar.removeFromLeft(64).reduced(0, 2));
    bar.removeFromLeft(16);
    velLabel.setBounds(bar.removeFromLeft(64));
    velSlider.setBounds(bar.removeFromLeft(220));
    bar.removeFromLeft(16);
    notesOffBtn.setBounds(bar.removeFromLeft(120).reduced(0, 1));
    bar.removeFromLeft(16);
    statusLabel.setBounds(bar);
    bottom.removeFromTop(6);
    keyboard.setBounds(bottom);
    keyboard.setKeyWidth(juce::jmax(14.0f, float(bottom.getWidth() - 44) / 52.0f));  // about 52 white keys in view
    tabs.setBounds(r);
}
