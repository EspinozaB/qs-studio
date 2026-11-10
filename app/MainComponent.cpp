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

// ---------- Card tools ----------
CardTab::CardTab(MidiLink& m) : midi(m) {
    for (juce::Component* c : std::initializer_list<juce::Component*>{&imageSec, &slotSec, &romSec, &openBtn, &wavBtn, &sfzBtn, &testBtn, &readBtn, &writeBtn, &romBtn, &sizeBox, &sizeLabel, &info})
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
    romBtn.onClick = [this] { dumpRom(); };
    openBtn.setTooltip("Opens a card image file (.img/.bin) on your computer to look at its samples, programs and mixes.");
    testBtn.setTooltip("Asks the QS for the first 1 KB of its card slot. Some units answer with their built-in sample ROM instead.");
    readBtn.setTooltip("Reads a FLASH card in the slot into an .img file. RAM program cards (RAM512) can't be read this way.");
    writeBtn.setTooltip("Writes the image you opened with Open card image onto a FLASH card in the slot. It doesn't create an image: "
                        "use Read card or Dump internal sample ROM for that.");
    romBtn.setTooltip("Saves the QS's built-in sample ROM to a .bin file over MIDI, on units where Test card reader says "
                      "\"built-in sound ROM\". Takes about 1 hour per 8 MB and can be paused and resumed.");
    appendLine(info, "Results appear here.");
    sizeLabel.setColour(juce::Label::textColourId, theme::dim);
}

bool CardTab::requireMidi() {
    if (midi.isOpen()) return true;
    ::info("Not connected", "Connect to the QS on the Connection page first.");
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
                msg = "< No reply in 4 seconds. Check both MIDI cables, the Connection page, and that the QS I/O setting is MIDI.";
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

void CardTab::dumpRom() {
    if (!requireMidi()) return;
    auto* w = new juce::AlertWindow("Dump internal sample ROM",
                                    "Saves the built-in sample ROM to a .bin file over MIDI. Leave the QS alone while it runs.\n"
                                    "About 1 hour for 8 MB, 2 hours for 16 MB. You can cancel and click Dump again later to resume.\n\n"
                                    "Keep the file for yourself: the samples are Alesis's copyright.",
                                    juce::MessageBoxIconType::QuestionIcon);
    w->addComboBox("size", {"16 MB (QS7, QS8, QSR and .1/.2 models)", "8 MB (original QS6)"}, "ROM size");
    w->addButton("Choose file and start", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
        if (r != 1) return;
        const int sectors = w->getComboBoxComponent("size")->getSelectedItemIndex() == 0 ? 128 : 64;
        auto dir = settingsDir().getChildFile("ROM dumps");
        dir.createDirectory();
        chooser = std::make_unique<juce::FileChooser>("Save the ROM dump as (pick an existing partial dump to resume it)",
                                                      dir.getChildFile(sectors == 128 ? "QS78R_internal_sample_ROM.bin" : "QS6_internal_sample_ROM.bin"),
                                                      "*.bin");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode, [this, sectors](const juce::FileChooser& fc) {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            f = f.withFileExtension("bin");
            if (f.existsAsFile() && f.getSize() > 0)
                appendLine(info, "\nResuming " + f.getFileName() + " from " + juce::String(f.getSize() / 1024) + " KB.");
            auto* job = new cardjobs::DumpRomJob(midi, sectors, f, [this](juce::String res) {
                appendLine(info, "\n" + res);
                ::info("ROM dump", res);
            });
            job->launchThread();
        });
    }), true);
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
    auto r = getLocalBounds().reduced(14);
    auto left = r.removeFromLeft(juce::jmin(640, r.getWidth() / 2 + 60));
    r.removeFromLeft(14);
    info.setBounds(r);
    auto place = [&left](Section& sec, int controlsH) {
        const int h = 48 + sec.textHeight(left.getWidth() - 32) + controlsH;
        sec.setBounds(left.removeFromTop(h));
        left.removeFromTop(12);
        return sec.getBounds().withTrimmedTop(h - controlsH - 12).reduced(16, 0).withHeight(controlsH);
    };
    auto a = place(imageSec, 40);
    auto row = a.removeFromTop(34);
    for (auto* b : {&openBtn, &wavBtn, &sfzBtn}) {
        b->setBounds(row.removeFromLeft(b == &sfzBtn ? 190 : 150));
        row.removeFromLeft(8);
    }
    auto s = place(slotSec, 80);
    row = s.removeFromTop(32);
    sizeLabel.setBounds(row.removeFromLeft(72));
    sizeBox.setBounds(row.removeFromLeft(170));
    s.removeFromTop(8);
    row = s.removeFromTop(34);
    for (auto* b : {&testBtn, &readBtn, &writeBtn}) {
        b->setBounds(row.removeFromLeft(b == &writeBtn ? 200 : 150));
        row.removeFromLeft(8);
    }
    auto m = place(romSec, 40);
    romBtn.setBounds(m.removeFromTop(34).removeFromLeft(240));
}

// ---------- Main window ----------
namespace {
bool looksLikeQs6(const juce::String& model) { return model.startsWith("QS6") || model.startsWithIgnoreCase("QuadraSynth"); }
}  // namespace

MainComponent::MainComponent() {
    auto own = [this](juce::Component* c) {
        owned.emplace_back(c);
        return c;
    };
    library = new LibraryTab(session);
    editor = new EditorTab(session);
    connection = new ConnectionPage(session);
    globalPage = new GlobalPage(session);
    auto* backup = new BackupPage(session);
    auto* move = new MoveBanksPage(session, *backup, *library);
    move->goToBackup = [this] { showPage(2); };
    move->goToPrograms = [this] { showPage(0); };
    own(move);
    own(library);
    own(editor);
    own(connection);
    own(globalPage);
    own(backup);

    pages = {
        {"Play", "Programs",
         "Browse every bank, click a program to hear it on the QS, and send, store or copy programs.",
         "1. Pick a bank on the left. User is the bank you can change; Preset 1-3 and GenMIDI are fixed; Card banks are on a "
         "PCMCIA card.\n"
         "2. Click a program: the QS switches to it, so you can play it from the keyboard below or your own controller. "
         "Up/Down arrow keys step through programs.\n"
         "3. Names come from the QS (Read this bank) or from .syx files you import. The line under the list says where they came from.\n"
         "4. Audition sends a program into the QS's edit buffer to try it without storing. Store to User writes it to a User slot.",
         "Browse banks and play programs", library},
        {"Edit", "Program Editor",
         "Edit the program that's in the QS's edit buffer. Every change is sent as you make it, so you hear it right away.",
         "1. Open a program from the Programs page, or click Get from QS to load whatever the QS is playing.\n"
         "2. Each of the 4 sounds has a card with its main settings. Click a section to see all its settings.\n"
         "3. Changes are only in the edit buffer. Click Store to keep them in a User slot on the QS, or Save .syx to keep them on this computer.",
         "Edit programs live", editor},
        {"Transfer", "Backup & Restore",
         "Save the synth's User programs, mixes, effects and Global settings to one file, or put a saved file back.",
         "A backup asks the QS to send everything you can change (what the manual calls Send All Data To MIDI) and saves it as a "
         ".syx file. It doesn't change anything on the QS.\n"
         "Restoring sends that file back and replaces the QS's User programs, mixes and effects. Global settings are left alone "
         "unless you tick the box.\n"
         "Backups are saved in the Backups folder next to QS Studio unless you choose another place.",
         "Save or restore everything in User memory", backup},
        {"Transfer", juce::String(juce::CharPointer_UTF8("Move banks QS6 \xe2\x86\x94 QSR")),
         "Move programs between a QS6 and a QS7, QS8 or QSR without them playing the wrong samples.",
         "The two families of synth have different sample ROMs, numbered differently. Follow the three steps: back up, convert, then "
         "restore or copy to a card. The report lists every sample that changed and any the other synth doesn't have.",
         "Convert banks between the QS6 and QS7/QS8/QSR sample sets", move},
        {"Transfer", "Cards & ROM",
         "Look inside card images, read and write FLASH sample cards, and save the built-in sample ROM.",
         "Card images (.img) hold samples, programs and mixes. You can export the samples as WAVs.\n"
         "Program cards (RAM512) are handled on the Programs page (Card banks) and Move Banks page instead.",
         "PCMCIA sound cards and the internal sample ROM", own(new CardTab(session.midi))},
        {"Capture", "Record & Sample",
         "Record the QS's audio output through your audio interface, or sample every note of a program automatically.",
         "1. Pick the audio input the QS's outputs are connected to (1 or 2 channels).\n"
         "2. Turn on monitoring to hear it through your computer.\n"
         "3. Record by hand, or use Auto-sample to play and record each note into its own WAV file.",
         "Record audio and auto-sample programs", own(new RecorderTab(session))},
        {"Setup", "Connection",
         "Connect QS Studio to your synth over MIDI. Both directions are needed.",
         "",
         "MIDI ports and synth detection", connection},
        {"Setup", "Global Settings",
         "MIDI settings stored in the synth: controller numbers, Program Select and General MIDI mode.",
         "QS Studio needs MIDI Program Select set to On (or to your channel) to switch programs.\n"
         "Controller A-D send the MIDI CC numbers set here. The QS6 has only the Controller A slider; use the sliders on this page "
         "for B, C and D.",
         "Controller numbers, Program Select, General MIDI", globalPage},
    };
    std::vector<Sidebar::Item> items;
    for (auto& p : pages) {
        items.push_back({p.group, p.title, p.tip});
        addChildComponent(p.comp);
    }
    sidebar.setItems(items);
    sidebar.onSelect = [this](int i) { showPage(i); };
    addAndMakeVisible(sidebar);
    addAndMakeVisible(header);
    header.onHeightChanged = [this] { resized(); };

    session.openInEditor = [this](const qs::Program& p, const juce::String& where) {
        editor->openProgram(p, where);
        showPage(1);
    };
    session.setStatus = [this](const juce::String& s) { statusLabel.setText(s, juce::dontSendNotification); };
    session.midi.addListener(this);

    // ---- top bar
    appTitle.setFont(theme::uiFont(20.0f, true));
    addAndMakeVisible(appTitle);
    pill.onClick = [this] { showPage(6); };
    pill.setTooltip("Click to open the Connection page");
    addAndMakeVisible(pill);
    lookBox.addItem("Match the synth", 1);
    lookBox.addItem("QS6", 2);
    lookBox.addItem("QS7 / QS8 / QSR", 3);
    lookBox.setSelectedId(settings().getIntValue("panelLook", 1), juce::dontSendNotification);
    lookBox.setTooltip("Colours and front panel of QS Studio. \"Match the synth\" follows the model that's connected (or was last connected).");
    lookBox.onChange = [this] {
        settings().setValue("panelLook", lookBox.getSelectedId());
        settings().saveIfNeeded();
        applyLook();
    };
    for (int ch = 1; ch <= 16; ++ch) channelBox.addItem(juce::String(ch), ch);
    channelBox.setSelectedId(session.channel, juce::dontSendNotification);
    channelBox.setTooltip("The MIDI channel the QS receives on in Program mode (QS Global > MIDI channel).");
    channelBox.onChange = [this] {
        session.setChannel(channelBox.getSelectedId());
        library->refresh();
    };
    controllerBox.setTooltip("A MIDI keyboard or controller connected to this computer. Everything you play on it goes to the QS on the "
                             "MIDI channel shown. Don't pick the interface port the QS itself is connected to.");
    controllerBox.onChange = [this] {
        const int id = controllerBox.getSelectedId();
        if (id == 999) { fillControllerBox(); return; }
        const auto ident = id >= 2 ? controllerDevices[id - 2].identifier : juce::String();
        juce::String err;
        if (!session.openController(ident, err)) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "MIDI controller", err);
            controllerBox.setSelectedId(1, juce::dontSendNotification);
            return;
        }
        settings().setValue("controllerIn", ident);
        settings().saveIfNeeded();
        statusLabel.setText(ident.isEmpty() ? "No MIDI controller." : "Playing the QS from " + controllerBox.getText() + ".", juce::dontSendNotification);
    };
    fillControllerBox();
    {
        const auto saved = settings().getValue("controllerIn");
        for (int i = 0; i < controllerDevices.size(); ++i)
            if (controllerDevices[i].identifier == saved) {
                juce::String err;
                if (session.openController(saved, err)) controllerBox.setSelectedId(i + 2, juce::dontSendNotification);
            }
    }
    for (auto* l : {&lookLabel, &chLabel, &ctrlLabel, &velLabel}) {
        l->setColour(juce::Label::textColourId, theme::dim);
        l->setFont(theme::uiFont(13.0f, true));
        l->setJustificationType(juce::Justification::centredRight);
    }
    for (juce::Component* c : std::initializer_list<juce::Component*>{&lookLabel, &lookBox, &chLabel, &channelBox, &ctrlLabel, &controllerBox})
        addAndMakeVisible(c);

    // ---- performance strip
    keyboard.setAvailableRange(21, 108);
    keyboard.setLowestVisibleKey(36);
    keyboard.setKeyPressBaseOctave(4);
    keyboard.setScrollButtonWidth(22);
    velSlider.setRange(1, 127, 1);
    velSlider.setValue(settings().getIntValue("keyVelocity", 100), juce::dontSendNotification);
    velSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 24);
    velSlider.setTooltip("How hard the on-screen keyboard plays");
    auto applyVel = [this] {
        keyboard.setVelocity(float(velSlider.getValue()) / 127.0f, false);
        settings().setValue("keyVelocity", int(velSlider.getValue()));
    };
    velSlider.onValueChange = applyVel;
    applyVel();
    notesOffBtn.onClick = [this] { session.allNotesOff(); };
    notesOffBtn.setTooltip("Stops any stuck notes on the QS");
    keysBtn.setClickingTogglesState(true);
    keysBtn.setToggleState(settings().getBoolValue("hideKeys", false), juce::dontSendNotification);
    keysBtn.setButtonText(keysBtn.getToggleState() ? "Show keyboard" : "Hide keyboard");
    keysBtn.onClick = [this] {
        settings().setValue("hideKeys", keysBtn.getToggleState());
        keysBtn.setButtonText(keysBtn.getToggleState() ? "Show keyboard" : "Hide keyboard");
        resized();
    };
    statusLabel.setColour(juce::Label::textColourId, theme::dim);
    statusLabel.setFont(theme::uiFont(13.0f));
    for (juce::Component* c : std::initializer_list<juce::Component*>{&keyboard, &perf, &velLabel, &velSlider, &notesOffBtn, &keysBtn, &statusLabel})
        addAndMakeVisible(c);

    applyLook();
    updatePill();
    setSize(1400, 940);
    connection->onPortsChanged = [this] { updatePill(); };
    connection->connectSaved();
    showPage(session.midi.isOpen() ? 0 : 6);  // not connected yet: start on Connection
}

void MainComponent::fillControllerBox() {
    const auto current = controllerBox.getSelectedId() >= 2 && controllerBox.getSelectedId() - 2 < controllerDevices.size()
                             ? controllerDevices[controllerBox.getSelectedId() - 2].identifier
                             : juce::String();
    controllerDevices = juce::MidiInput::getAvailableDevices();
    controllerBox.clear(juce::dontSendNotification);
    controllerBox.addItem("None", 1);
    for (int i = 0; i < controllerDevices.size(); ++i) controllerBox.addItem(controllerDevices[i].name, i + 2);
    controllerBox.addSeparator();
    controllerBox.addItem("Rescan devices", 999);
    controllerBox.setSelectedId(1, juce::dontSendNotification);
    for (int i = 0; i < controllerDevices.size(); ++i)
        if (controllerDevices[i].identifier == current) controllerBox.setSelectedId(i + 2, juce::dontSendNotification);
}

MainComponent::~MainComponent() {
    session.midi.removeListener(this);
    session.openInEditor = nullptr;
    session.setStatus = nullptr;
    for (auto& p : pages) removeChildComponent(p.comp);
}

void MainComponent::showPage(int index) {
    if (index < 0 || index >= int(pages.size())) return;
    if (current >= 0) pages[size_t(current)].comp->setVisible(false);
    current = index;
    auto& p = pages[size_t(index)];
    p.comp->setVisible(true);
    sidebar.setSelected(index);
    header.set(p.title, p.summary, p.help);
    resized();
}

void MainComponent::applyLook() {
    const int look = lookBox.getSelectedId();
    const auto model = session.deviceName.isNotEmpty() ? session.deviceName : settings().getValue("lastModel", "QSR");
    const bool qs6 = look == 2 || (look == 1 && looksLikeQs6(model));
    theme::applyStyle(qs6 ? theme::Style::QS6 : theme::Style::QSR);
    if (auto* lf = dynamic_cast<theme::LookAndFeel*>(&juce::LookAndFeel::getDefaultLookAndFeel())) lf->refresh();
    sendLookAndFeelChange();
    if (library) {
        library->refresh();
        library->resized();
    }
    repaint();
    for (auto& p : pages) p.comp->repaint();
}

void MainComponent::updatePill() {
    if (!session.midi.isOpen())
        pill.set(false, "Not connected: click to set up");
    else if (session.deviceName.isEmpty())
        pill.set(false, "MIDI open, no answer from the QS yet");
    else
        pill.set(true, "Connected to " + session.deviceName.replace(" v", ", OS "));
}

void MainComponent::sysexReceived(const qs::Bytes& msg) {
    if (auto d = qs::parseDeviceReply(msg)) {
        session.deviceName = juce::String(d->model) + " v" + juce::String(d->version);
        settings().setValue("lastModel", juce::String(d->model));
        statusLabel.setText("Connected to " + session.deviceName, juce::dontSendNotification);
        session.programModeSent = false;
        library->applySavedImports(true);  // fill empty banks from the .syx folder the user imported before
        perf.updateLayout();
        applyLook();
        updatePill();
        resized();
        session.midi.send(qs::globalRequest());  // controller numbers and Program Select
        if (current == 6) juce::Timer::callAfterDelay(1200, [this] { showPage(0); });  // connected: go play
        return;
    }
    if (auto g = qs::parseGlobalDump(msg)) {
        session.global = *g;
        session.globalKnown = true;
        perf.updateLayout();
        globalPage->refreshFromSession();
        if (g->programSelect == 0)
            statusLabel.setText("MIDI Program Select is OFF on the QS, so it ignores program changes. Turn it on in Global Settings.",
                                juce::dontSendNotification);
    }
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(theme::bg);
    g.setColour(theme::bezel);
    g.fillRect(getLocalBounds().removeFromTop(56));
    g.setColour(theme::accent);
    g.fillRect(0, 54, getWidth(), 2);
    const int stripH = keysBtn.getToggleState() ? 44 : 158;
    g.setColour(theme::panel);
    g.fillRect(getLocalBounds().removeFromBottom(stripH));
    g.setColour(theme::line);
    g.drawHorizontalLine(getHeight() - stripH, 0.0f, float(getWidth()));
}

void MainComponent::resized() {
    auto r = getLocalBounds();
    auto top = r.removeFromTop(56).reduced(16, 11);
    appTitle.setBounds(top.removeFromLeft(130));
    top.removeFromLeft(10);
    pill.setBounds(top.removeFromLeft(340));
    controllerBox.setBounds(top.removeFromRight(210));
    ctrlLabel.setBounds(top.removeFromRight(100));
    top.removeFromRight(12);
    channelBox.setBounds(top.removeFromRight(64));
    chLabel.setBounds(top.removeFromRight(70));
    top.removeFromRight(12);
    lookBox.setBounds(top.removeFromRight(170));
    lookLabel.setBounds(top.removeFromRight(90));

    const bool hidden = keysBtn.getToggleState();
    auto strip = r.removeFromBottom(hidden ? 44 : 158).reduced(12, 7);
    auto bar = strip.removeFromTop(30);
    keysBtn.setBounds(bar.removeFromLeft(130));
    bar.removeFromLeft(12);
    notesOffBtn.setBounds(bar.removeFromLeft(120));
    bar.removeFromLeft(4);
    velLabel.setBounds(bar.removeFromLeft(70));
    velSlider.setBounds(bar.removeFromLeft(200));
    bar.removeFromLeft(16);
    statusLabel.setBounds(bar);
    perf.setVisible(!hidden);
    keyboard.setVisible(!hidden);
    if (!hidden) {
        strip.removeFromTop(6);
        perf.setBounds(strip.removeFromLeft(perf.preferredWidth()));
        strip.removeFromLeft(8);
        keyboard.setBounds(strip);
        keyboard.setKeyWidth(juce::jmax(14.0f, float(strip.getWidth() - 44) / 52.0f));
    }

    sidebar.setBounds(r.removeFromLeft(230));
    header.setBounds(r.removeFromTop(header.heightFor(r.getWidth())));
    if (current >= 0) pages[size_t(current)].comp->setBounds(r);
}
