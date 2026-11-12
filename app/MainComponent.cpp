#include "MainComponent.h"

#include "CardJobs.h"

#include "EditorTab.h"
#include "EffectsTab.h"
#include "LibraryTab.h"
#include "MixTab.h"
#include "Undo.h"
#include "MidiMonitor.h"
#include "Diagnostics.h"
#include "PlayerTab.h"
#include "RecorderTab.h"

// ---------- helpers ----------
juce::String hex(const qs::Bytes& b, size_t maxBytes) {
    juce::String s;
    for (size_t i = 0; i < b.size() && i < maxBytes; ++i) s << juce::String::toHexString(int(b[i])).paddedLeft('0', 2) << " ";
    if (b.size() > maxBytes) s << "... (" << int(b.size()) << " bytes)";
    return s.trim();
}

// Portable: settings live next to the .exe when that folder is writable.
// The folder QS Studio lives in. Everything it writes goes in a "QS Studio Data" folder beside it, never in AppData,
// the registry or the user's home folder, so the whole thing can be moved, backed up or put on a USB stick, and updating
// means replacing the program and leaving the data folder alone.
//  - Windows / Linux: the folder holding the executable.
//  - macOS: the folder holding QS Studio.app (writing inside the .app would break its signature).
//  - Linux AppImage: the folder holding the .AppImage file (the image itself is mounted read-only).
juce::File appFolder() {
    const auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const auto appImage = juce::SystemStats::getEnvironmentVariable("APPIMAGE", {});
    if (appImage.isNotEmpty()) return juce::File(appImage).getParentDirectory();
    for (auto d = exe.getParentDirectory(); d != d.getParentDirectory(); d = d.getParentDirectory())
        if (d.hasFileExtension("app")) return d.getParentDirectory();
    return exe.getParentDirectory();
}

namespace {
// Files from older versions (beside the exe, or in AppData) are moved into the data folder once.
void moveOldFiles(const juce::File& from, const juce::File& to) {
    if (!from.isDirectory() || from == to) return;
    for (const auto& f : from.findChildFiles(juce::File::findFilesAndDirectories, false)) {
        const auto name = f.getFileName();
        const bool ours = name.startsWith("QSStudio.") || name == "Backups" || name == "Recordings";
        if (ours && !to.getChildFile(name).exists()) f.moveFileTo(to.getChildFile(name));
    }
}
}  // namespace

juce::File settingsDir() {
    static const juce::File dir = [] {
        auto d = appFolder().getChildFile("QS Studio Data");
        d.createDirectory();
        moveOldFiles(appFolder(), d);
        const auto oldAppData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("QS Studio");
        moveOldFiles(oldAppData, d);
        if (oldAppData.isDirectory() && oldAppData.findChildFiles(juce::File::findFilesAndDirectories, false).isEmpty()) oldAppData.deleteRecursively();
        return d;
    }();
    return dir;
}

bool dataFolderWritable() { return settingsDir().isDirectory() && settingsDir().hasWriteAccess(); }

juce::String versionLabel() { return juce::String(ProjectInfo::versionString) + " " + QS_STAGE; }

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
    qslog::action("Message: " + title + ": " + msg.replace("\n", " / "));
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, title, msg);
}

static qs::Bytes toBytes(const juce::MemoryBlock& mb) {
    auto p = static_cast<const uint8_t*>(mb.getData());
    return qs::Bytes(p, p + mb.getSize());
}

// ---------- Card tools ----------
CardTab::CardTab(MidiLink& m) : midi(m) {
    for (juce::Component* c : std::initializer_list<juce::Component*>{&imageSec, &buildSec, &buildBtn, &slotSec, &romSec, &openBtn, &wavBtn, &sfzBtn, &testBtn, &readBtn, &writeBtn, &romBtn, &sizeBox, &sizeLabel, &info})
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
    buildBtn.onClick = [this] { buildFromWavs(); };
    buildBtn.setTooltip("Makes a QS card image (.img) from your own WAV files, like Alesis Sound Bridge did.");
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

void CardTab::buildFromWavs() {
    chooser = std::make_unique<juce::FileChooser>("Pick the WAV files for the card", settingsDir(), "*.wav;*.WAV");
    chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
        [this](const juce::FileChooser& fc) {
            auto files = fc.getResults();
            if (files.isEmpty()) return;
            juce::MouseCursor::showWaitCursor();
            std::vector<qs::NamedWav> wavs;
            juce::StringArray problems;
            for (auto& f : files) {
                juce::MemoryBlock mb;
                qs::NamedWav w;
                w.fileName = f.getFileName().toStdString();
                std::string err;
                if (!f.loadFileAsData(mb) || !qs::readWav(toBytes(mb), w.wav, err)) {
                    problems.add(f.getFileName() + ": " + juce::String(err.empty() ? "can't read it" : err));
                    continue;
                }
                wavs.push_back(std::move(w));
            }
            juce::MouseCursor::hideWaitCursor();
            if (wavs.empty()) { ::info("No samples", "None of the files could be read.\n" + problems.joinIntoString("\n")); return; }
            std::vector<std::string> notes;
            qs::CardBuild b;
            b.voices = qs::voicesFromWavs(wavs, notes);
            double seconds = 0;
            for (auto& w : wavs) seconds += double(w.wav.pcm.size()) / qs::kCardRate;
            for (auto& n : notes) problems.add(juce::String(n));
            juce::String summary = juce::String(int(wavs.size())) + " files make " + juce::String(int(b.voices.size())) + " voices, " +
                                   juce::String(seconds, 1) + " s of audio (an 8 MB card holds about 87 s at 48 kHz).\n";
            for (size_t i = 0; i < b.voices.size() && i < 8; ++i)
                summary << "\n  " << juce::String(b.voices[i].name).substring(0, 10) << ": " << int(b.voices[i].zones.size()) << " sample"
                        << (b.voices[i].zones.size() == 1 ? "" : "s");
            if (b.voices.size() > 8) summary << "\n  ...and " << int(b.voices.size() - 8) << " more";
            if (b.voices.size() > 127) problems.add("More than 127 voices: only the first 127 fit in one group, the rest go in the next groups.");
            for (size_t i = 0; i < b.voices.size(); ++i) b.voices[i].group = 16 + int(i / 127);
            if (!problems.isEmpty()) summary << "\n\nNotes:\n" << problems.joinIntoString("\n");
            auto* w = new juce::AlertWindow("Build a card", summary, juce::MessageBoxIconType::NoIcon);
            w->addTextEditor("name", files[0].getParentDirectory().getFileName().substring(0, 14), "Card name (14 letters)");
            w->addTextEditor("group", "User", "Sample group name on the QS (6 letters)");
            w->addButton("Build and save", 1, juce::KeyPress(juce::KeyPress::returnKey));
            w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            w->enterModalState(true, juce::ModalCallbackFunction::create([this, w, b](int r) mutable {
                if (r != 1) return;
                b.cardName = w->getTextEditorContents("name").substring(0, 14).toStdString();
                const auto g = w->getTextEditorContents("group").substring(0, 6).toStdString();
                b.groupNames.assign(8, g);
                saveBuiltCard(b);
            }), true);
        });
}

void CardTab::saveBuiltCard(const qs::CardBuild& b) {
    qs::BuildResult res;
    std::string err;
    if (!qs::buildCard(b, res, err)) { ::info("Can't build the card", err); return; }
    auto dir = settingsDir().getChildFile("Cards");
    dir.createDirectory();
    const auto name = juce::String(qs::safeName(b.cardName.empty() ? "My card" : b.cardName));
    chooser = std::make_unique<juce::FileChooser>("Save the card image as", dir.getChildFile(name + ".img"), "*.img");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this, res](const juce::FileChooser& fc) {
                             auto f = fc.getResult();
                             if (f == juce::File()) return;
                             f = f.withFileExtension("img");
                             if (!f.replaceWithData(res.image.data(), res.image.size())) { ::info("Can't save", f.getFullPathName()); return; }
                             std::string err;
                             if (!qs::loadCardImage(res.image, card, err)) { ::info("Built, but can't read it back", err); return; }
                             cardFile = f;
                             qslog::action("Built card " + f.getFullPathName() + ", " + juce::String(res.secondsUsed(), 1) + " s");
                             showCard();
                             appendLine(info, "\nBUILT " + f.getFullPathName() + "\n" + juce::String(res.secondsUsed(), 1) + " s of audio, " +
                                                  juce::String(int(res.image.size() / 1024)) + " KB image (" +
                                                  juce::String(juce::roundToInt(100.0 * double(res.image.size()) / (8.0 * 1024 * 1024))) + "% of an 8 MB card)");
                             for (auto& n : res.notes) appendLine(info, "  " + juce::String(n));
                             info.moveCaretToTop(false);
                             ::info("Card built", "Saved " + f.getFileName() + ".\n\nPlay it on the Sound Player page, or put a FLASH card in "
                                                  "the QS and click Write image to FLASH card.");
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
    auto bsec = place(buildSec, 40);
    buildBtn.setBounds(bsec.removeFromTop(34).removeFromLeft(240));
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
constexpr int kStripH = 176;  // bottom strip: wheels, faders and keyboard
}  // namespace

MainComponent::MainComponent() {
    qslog::Writer::get().start(versionLabel());
    auto own = [this](juce::Component* c) {
        owned.emplace_back(c);
        return c;
    };
    library = new LibraryTab(session);
    editor = new EditorTab(session);
    mixEditor = new MixTab(session);
    player = new PlayerTab(session);
    own(player);
    fxEditor = new EffectsTab(session);
    own(mixEditor);
    own(fxEditor);
    connection = new ConnectionPage(session);
    globalPage = new GlobalPage(session);
    auto* checkup = own(new DiagnosticsPage(session));
    auto* backup = new BackupPage(session);
    auto* move = new MoveBanksPage(session, *backup, *library);
    move->goToBackup = [this] { showPageNamed("Backup & Restore"); };
    move->goToPrograms = [this] { showPageNamed("Programs"); };
    own(move);
    own(library);
    own(editor);
    own(connection);
    own(globalPage);
    own(backup);

    // Help is kept to a few short lines per page: what to click, in order. Details live in the tooltips.
    pages = {
        {"Play", "Programs",
         "Pick a bank, click a program and the synth plays it.",
         "- Click a program, or use the panel keys like on the synth (hover a key to see what it does).\n"
         "- Programs / Mixes switches the list.\n"
         "- Read from the synth (right) gets your own names; the factory ones are already there.",
         "Browse banks and play programs", library},
        {"Play", "Sound Player",
         "Play the samples of a QCard image or a QS sample ROM dump from this computer, like a simple sampler.",
         "- Open a card image (.img/.bin) or the ROM dump from Cards & ROM.\n"
         "- Pick a voice and play it from the keyboard below, your controller or the computer keys.\n"
         "- Audio output... picks where it plays. It's the raw samples, not the full QS synth engine yet.",
         "Play card and ROM samples", player},
        {"Edit", "Program Editor",
         "Change the program the synth is playing. You hear every change straight away.",
         "- Open a program from the Programs page, or click Get from QS.\n"
         "- Changes aren't kept until you click Store to User (or Save .syx).",
         "Edit programs live", editor},
        {"Edit", "Mix Editor",
         "Change the 16 channels of a mix: program, level, pan, effect send, transpose, key range and MIDI switches.",
         "- Open a mix from the Programs page (Mixes, then Edit this mix), or click Get from QS.\n"
         "- Edit on a channel opens that channel's program inside the mix.\n"
         "- Changes aren't kept until you click Store to User mix.",
         "Edit mixes live", mixEditor},
        {"Edit", "Effects Editor",
         "Change an effect patch: its configuration, then the pitch, delay, reverb, lezlie, overdrive, EQ and modulation settings.",
         "- Pick Program or Mix, then Get from QS.\n"
         "- Pick the configuration first: the other settings depend on it.\n"
         "- User effect n belongs to User program n: Store to User... saves it there.",
         "Edit effects live", fxEditor},
        {"Transfer", "Backup & Restore",
         "Save everything you can change on the synth to one file, or put a saved file back.",
         "- Back up now: saves User programs, mixes, effects and Global settings. Changes nothing on the synth.\n"
         "- Restore: replaces the synth's User programs, mixes and effects with the file's.",
         "Save or restore everything in User memory", backup},
        {"Transfer", juce::String(juce::CharPointer_UTF8("Move banks QS6 \xe2\x86\x94 QSR")),
         "Move programs between a QS6 and a QS7, QS8 or QSR without them playing the wrong samples.",
         "- Back up, convert, then restore or put on a card: the three steps below, in order.\n"
         "- The report lists every sample that changed.",
         "Convert banks between the QS6 and QS7/QS8/QSR sample sets", move},
        {"Transfer", "Cards & ROM",
         "Look inside card images, read and write FLASH sample cards, and save the built-in sample ROM.",
         "- Program cards (RAM512) are on the Programs page under Card.",
         "PCMCIA sound cards and the internal sample ROM", own(new CardTab(session.midi))},
        {"Capture", "Record & Sample",
         "Record the synth through your audio interface, or sample every note of a program.",
         "- Pick the input in step 1, then record by hand or auto-sample.",
         "Record audio and auto-sample programs", own(new RecorderTab(session))},
        {"Setup", "Connection",
         "Connect QS Studio to your synth over MIDI. Both directions are needed.",
         "",
         "MIDI ports and synth detection", connection},
        {"Setup", "Global Settings",
         "MIDI settings stored in the synth: controller numbers, Program Select and General MIDI mode.",
         "- Program Select must not be Off, or QS Studio can't change programs.",
         "Controller numbers, Program Select, General MIDI", globalPage},
        {"Setup", "Check-up",
         "Ask the QS everything QS Studio can over MIDI and explain the answers. Changes nothing.",
         "- Run it when something doesn't work, and save the report for a bug report.",
         "Model, OS version, MIDI link test, Globals, User bank, card slot", checkup},
    };
    std::vector<Sidebar::Item> items;
    for (auto& p : pages) {
        items.push_back({p.group, p.title, p.tip});
        addChildComponent(p.comp);
    }
    sidebar.setItems(items);
    sidebar.footer = "QS Studio " + versionLabel() +
                     ". Unofficial, not made or endorsed by Alesis or inMusic. Alesis, QuadraSynth and the QS model names are "
                     "trademarks of their owners and are used only to say which synths this works with.";
    sidebar.onSelect = [this](int i) { showPage(i); };
    addAndMakeVisible(sidebar);
    addAndMakeVisible(header);
    header.onHeightChanged = [this] { resized(); };

    session.openInEditor = [this](const qs::Program& p, const juce::String& where) {
        editor->openProgram(p, where);
        showPageNamed("Program Editor");
    };
    session.openMixEditor = [this](const qs::Mix& m, const juce::String& where, int number) {
        mixEditor->openMix(m, where, number);
        showPageNamed("Mix Editor");
    };
    session.openEffectsEditor = [this](const qs::Effects& e, const juce::String& where, int edit) {
        // An all-zero patch means "not known here": ask the QS for its effect edit buffer.
        const bool fetch = std::all_of(e.bytes().begin(), e.bytes().end(), [](uint8_t b) { return b == 0; });
        fxEditor->openEffects(e, where, edit, fetch);
        fxEditor->setProgram(session.effectsProgram);
        session.effectsProgram.reset();
        showPageNamed("Effects Editor");
    };
    // Screenshots without a synth: QS_STUDIO_UI_SYX=<file> fills the mix and effects editors from a .syx.
    if (auto f = juce::File(juce::SystemStats::getEnvironmentVariable("QS_STUDIO_UI_SYX", {})); f.existsAsFile()) {
        juce::MemoryBlock mb;
        f.loadFileAsData(mb);
        auto ptr = static_cast<const uint8_t*>(mb.getData());
        bool gotMix = false, gotFx = false;
        for (auto& m : qs::splitSysex(qs::Bytes(ptr, ptr + mb.getSize()))) {
            if (auto x = qs::Mix::fromDump(m); x && !gotMix && m[6] == 1) { mixEditor->openMix(*x, f.getFileName(), -1); gotMix = true; }
            if (auto e = qs::Effects::fromDump(m); e && !gotFx && m[6] == 0) { fxEditor->openEffects(*e, f.getFileName(), 0, false); gotFx = true; }
            if (auto pr = qs::Program::fromDump(m); pr && m[6] == 0 && qs::opcodeOf(m) == qs::UserProgramDump) fxEditor->setProgram(pr);
        }
    }
    session.editMixChannelProgram = [this](int ch, const juce::String& mixName) {
        editor->openMixChannel(ch, mixName);
        showPageNamed("Program Editor");
    };
    session.setStatus = [this](const juce::String& s) {
        qslog::action("Status: " + s);
        statusLabel.setText(s, juce::dontSendNotification);
    };
    session.channelChanged = [this] { channelBox.setSelectedId(session.channel, juce::dontSendNotification); };
    session.libraryChanged = [this] {
        library->refresh();
        mixEditor->refreshNames();
        connection->refreshFiles();
    };
    session.midi.addListener(this);

    // ---- top bar
    appTitle.setFont(theme::uiFont(20.0f, true));
    addAndMakeVisible(appTitle);
    pill.onClick = [this] { showPageNamed("Connection"); };
    pill.setTooltip("Click to open the Connection page");
    addAndMakeVisible(pill);
    lookBox.addItem("Match the synth", 1);
    for (int i = 0; i < Session::panelModels().size(); ++i) lookBox.addItem(Session::panelModels()[i], i + 2);
    {
        const auto saved = settings().getValue("panelModel");
        const int idx = Session::panelModels().indexOf(saved.isNotEmpty() ? saved
                                                                          : (settings().getIntValue("panelLook", 1) == 2 ? juce::String("QS6")
                                                                             : settings().getIntValue("panelLook", 1) == 3 ? juce::String("QSR")
                                                                                                                             : juce::String()));
        lookBox.setSelectedId(idx >= 0 ? idx + 2 : 1, juce::dontSendNotification);
    }
    lookBox.setTooltip("Which synth's front panel, sliders and keyboard QS Studio shows. \"Match the synth\" follows the model that's "
                       "connected (or was last connected).");
    lookBox.onChange = [this] {
        const int id = lookBox.getSelectedId();
        settings().setValue("panelModel", id >= 2 ? Session::panelModels()[id - 2] : juce::String("match"));
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
    keyboard.setWantsKeyboardFocus(false);  // MainComponent plays the computer keys, so the keyboard doesn't do it twice
    computerKeys.setToggleState(settings().getBoolValue("computerKeys", true), juce::dontSendNotification);
    computerKeys.setTooltip("Letters A to ' play notes (A = C, W = C#...). Z / X: octave down / up. - / =: velocity. Space: all notes off. "
                            "Typing in a text box never plays.");
    computerKeys.onClick = [this] { settings().setValue("computerKeys", computerKeys.getToggleState()); };
    addAndMakeVisible(computerKeys);
    session.passAftertouch = settings().getBoolValue("passAftertouch", true);
    aftertouchBtn.setTooltip("Aftertouch (key pressure): show an on-screen Pressure slider for keyboards that don't send it, "
                             "or stop your controller's aftertouch reaching the QS.");
    aftertouchBtn.onClick = [this] {
        juce::PopupMenu m;
        m.addItem(1, "Show the Pressure slider (aftertouch from the screen)", true, settings().getBoolValue("showAftertouch", false));
        m.addItem(2, "Pass my controller's aftertouch to the QS", true, session.passAftertouch.load());
        m.addSeparator();
        m.addItem(3, "MIDI monitor: check the velocity of each key or drum pad...");
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&aftertouchBtn), [this](int r) {
            if (r == 1) {
                settings().setValue("showAftertouch", !settings().getBoolValue("showAftertouch", false));
                perf.updateLayout();
                resized();
            } else if (r == 2) {
                session.passAftertouch = !session.passAftertouch.load();
                settings().setValue("passAftertouch", session.passAftertouch.load());
            } else if (r == 3) {
                juce::DialogWindow::LaunchOptions o;
                o.content.setOwned(new MidiMonitor(session));
                o.dialogTitle = "MIDI monitor";
                o.dialogBackgroundColour = theme::bg;
                o.useNativeTitleBar = true;
                o.resizable = true;
                o.launchAsync();
            }
        });
    };
    addAndMakeVisible(aftertouchBtn);
    setWantsKeyboardFocus(true);
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
    if (!dataFolderWritable())
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon, "QS Studio can't save its files",
            "QS Studio keeps everything in a \"QS Studio Data\" folder next to the program, but it can't write to:\n" +
                settingsDir().getFullPathName() +
                "\n\nMove the whole QS Studio folder somewhere you own, such as Documents, the Desktop or a USB stick, and run it from "
                "there. (Program Files is read-only for normal programs.) Until then nothing you read or change will be kept.");
    connection->connectSaved();
    showPageNamed(session.midi.isOpen() ? "Programs" : "Connection");  // not connected yet: start on Connection
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
    session.channelChanged = nullptr;
    session.libraryChanged = nullptr;
    for (auto& p : pages) removeChildComponent(p.comp);
}

int MainComponent::pageIndex(const juce::String& title) const {
    for (size_t i = 0; i < pages.size(); ++i)
        if (pages[i].title == title) return int(i);
    return 0;
}

void MainComponent::showPageNamed(const juce::String& title) { showPage(pageIndex(title)); }

void MainComponent::showPage(int index) {
    if (index < 0 || index >= int(pages.size())) return;
    if (current >= 0) pages[size_t(current)].comp->setVisible(false);
    current = index;
    auto& p = pages[size_t(index)];
    qslog::action("Page: " + p.title);
    p.comp->setVisible(true);
    sidebar.setSelected(index);
    header.set(p.title, p.summary, p.help);
    resized();
}

void MainComponent::applyLook() {
    theme::applyStyle(session.panelStyle());
    if (auto* lf = dynamic_cast<theme::LookAndFeel*>(&juce::LookAndFeel::getDefaultLookAndFeel())) lf->refresh();
    sendLookAndFeelChange();
    // The model's own key range: QS6 family 61 keys, QS7 76, QS8 and the QSR 88.
    const auto keys = session.keyRange();
    keyboard.setAvailableRange(keys.getStart(), keys.getEnd());
    perf.updateLayout();
    resized();
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
    if (session.checkReply(msg)) return;
    if (auto d = qs::parseDeviceReply(msg)) {
        if (session.midi.jobsRunning > 0 && session.deviceName.isNotEmpty()) return;  // a check-up asking again: nothing changed
        qslog::action("The synth says it is a " + juce::String(d->model) + ", OS " + juce::String(d->version));
        session.deviceName = juce::String(d->model) + " v" + juce::String(d->version);
        settings().setValue("lastModel", juce::String(d->model));
        session.library.switchModel(juce::String(d->model));  // each model keeps its own banks
        library->refresh();
        connection->refreshFiles();
        statusLabel.setText("Connected to " + session.deviceName, juce::dontSendNotification);
        session.programModeSent = false;
        session.cardState = Session::CardState::Unknown;  // a different synth may have a different card
        library->applySavedImports(true);  // fill empty banks from the .syx folder the user imported before
        perf.updateLayout();
        applyLook();
        updatePill();
        resized();
        session.midi.send(qs::globalRequest());  // controller numbers and Program Select
        if (current == pageIndex("Connection")) juce::Timer::callAfterDelay(1200, [this] { showPageNamed("Programs"); });  // connected: go play
        return;
    }
    if (auto g = qs::parseGlobalDump(msg)) {
        session.global = *g;
        session.globalKnown = true;
        qslog::action("Global settings: Program Select = " + juce::String(g->programSelect) + " (0 Off, 1 On, 2-17 channel 1-16), GM " +
                      (g->generalMidi ? "on" : "off"));
        perf.updateLayout();
        globalPage->refreshFromSession();
        if (g->programSelect == 0)
            statusLabel.setText("MIDI Program Select is OFF on the QS, so it ignores program changes. Turn it on in Global Settings.",
                                juce::dontSendNotification);
        else if (g->programSelect >= 2)
            statusLabel.setText("MIDI Program Select is set to channel " + juce::String(g->programSelect - 1) +
                                    " on the QS. If programs don't change, set it to On in Global Settings.",
                                juce::dontSendNotification);
    }
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(theme::bg);
    g.setColour(theme::bezel);
    g.fillRect(getLocalBounds().removeFromTop(56));
    g.setColour(theme::accent);
    g.fillRect(0, 54, getWidth(), 2);
    const int stripH = keysBtn.getToggleState() ? 44 : kStripH;
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
    auto strip = r.removeFromBottom(hidden ? 44 : kStripH).reduced(12, 7);
    auto bar = strip.removeFromTop(30);
    keysBtn.setBounds(bar.removeFromLeft(130));
    bar.removeFromLeft(12);
    notesOffBtn.setBounds(bar.removeFromLeft(120));
    bar.removeFromLeft(4);
    velLabel.setBounds(bar.removeFromLeft(70));
    velSlider.setBounds(bar.removeFromLeft(200));
    bar.removeFromLeft(12);
    computerKeys.setBounds(bar.removeFromLeft(190));
    bar.removeFromLeft(8);
    aftertouchBtn.setBounds(bar.removeFromLeft(110).reduced(0, 2));
    bar.removeFromLeft(12);
    statusLabel.setBounds(bar);
    perf.setVisible(!hidden);
    keyboard.setVisible(!hidden);
    if (!hidden) {
        strip.removeFromTop(6);
        perf.setBounds(strip.removeFromLeft(perf.preferredWidth()));
        strip.removeFromLeft(8);
        keyboard.setBounds(strip);
        // Fill the width with the model's key range.
        int whites = 0;
        for (int n = keyboard.getRangeStart(); n <= keyboard.getRangeEnd(); ++n)
            if (!juce::MidiMessage::isMidiNoteBlack(n)) ++whites;
        keyboard.setScrollButtonsVisible(false);
        keyboard.setKeyWidth(juce::jmax(12.0f, float(strip.getWidth()) / float(juce::jmax(1, whites))));
        keyboard.setLowestVisibleKey(keyboard.getRangeStart());
    }

    sidebar.setBounds(r.removeFromLeft(230));
    header.setBounds(r.removeFromTop(header.heightFor(r.getWidth())));
    if (current >= 0) pages[size_t(current)].comp->setBounds(r);
}

// ---------------- computer keyboard ----------------

namespace {
const char* const kNoteKeys = "awsedftgyhujkolp;'";
}

bool MainComponent::keyPressed(const juce::KeyPress& k) {
    // Ctrl+Z / Ctrl+Y with nothing focused: hand them to the page showing (the editors keep undo histories).
    if (isUndoKey(k) || isRedoKey(k)) {
        if (current >= 0 && current < int(pages.size()) && pages[size_t(current)].comp != nullptr)
            return pages[size_t(current)].comp->keyPressed(k);
        return false;
    }
    if (!computerKeys.getToggleState()) return false;
    const auto c = juce::CharacterFunctions::toLowerCase(k.getTextCharacter());
    if (k.getModifiers().isCommandDown() || k.getModifiers().isAltDown()) return false;
    if (c == 'z' || c == 'x') {
        keyOctave = juce::jlimit(0, 8, keyOctave + (c == 'x' ? 1 : -1));
        statusLabel.setText("Computer keys: octave starts at C" + juce::String(keyOctave - 2) + " (note " + juce::String(keyOctave * 12) + ")",
                            juce::dontSendNotification);
        return true;
    }
    if (c == '-' || c == '=') {
        velSlider.setValue(juce::jlimit(1.0, 127.0, velSlider.getValue() + (c == '=' ? 5 : -5)));
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::spaceKey) {
        session.allNotesOff();
        heldComputerKeys.clear();
        return true;
    }
    return juce::CharPointer_ASCII(kNoteKeys).indexOf(c) >= 0;  // the note itself is handled in keyStateChanged
}

bool MainComponent::keyStateChanged(bool) {
    if (!computerKeys.getToggleState()) return false;
    bool used = false;
    const float vel = float(velSlider.getValue()) / 127.0f;
    for (int i = 0; kNoteKeys[i] != 0; ++i) {
        const int code = int(kNoteKeys[i]);
        const bool down = juce::KeyPress::isKeyCurrentlyDown(code);
        const auto it = heldComputerKeys.find(code);
        if (down && it == heldComputerKeys.end()) {
            const int note = juce::jlimit(0, 127, keyOctave * 12 + i);
            heldComputerKeys[code] = note;
            session.keys.noteOn(1, note, vel);
            used = true;
        } else if (!down && it != heldComputerKeys.end()) {
            session.keys.noteOff(1, it->second, 0.0f);
            heldComputerKeys.erase(it);
            used = true;
        }
    }
    return used;
}
