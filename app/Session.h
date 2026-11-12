// State shared by the tabs: the MIDI link, the channel the QS listens on, the on-screen keyboard,
// and the program library (names + data per bank, cached next to the exe).
#pragma once
#include <JuceHeader.h>

#include <array>
#include <optional>

#include "../core/qs_effects.h"
#include "../core/qs_factory.h"
#include "../core/qs_mix.h"
#include "../core/qs_program.h"
#include "../core/qs_samples.h"
#include "MidiLink.h"
#include "Theme.h"

// settingsDir() and settings() are declared in Log.h (via MidiLink.h). settingsDir(): "QS Studio Data" beside the program: every file QS Studio writes goes here
juce::File appFolder();
bool dataFolderWritable();
juce::String versionLabel();  // e.g. "0.4.0 pre-alpha"

struct BankInfo {
    const char* title;
    int cc0;  // MIDI bank select (controller 0) value
};
// QS manual: controller 0 = 0 User, 1-3 Preset 1-3, 4 GenMIDI, 5-15 card banks 1-11.
// Card banks are numbered across both slots (a RAM512 alone holds 8), so "Card 1" is not "slot A".
inline const std::array<BankInfo, 16> kBanks{{{"User", 0}, {"Preset 1", 1}, {"Preset 2", 2}, {"Preset 3", 3}, {"GenMIDI", 4},
                                              {"Card 1", 5}, {"Card 2", 6}, {"Card 3", 7}, {"Card 4", 8}, {"Card 5", 9},
                                              {"Card 6", 10}, {"Card 7", 11}, {"Card 8", 12}, {"Card 9", 13}, {"Card 10", 14},
                                              {"Card 11", 15}}};
constexpr int kFirstCardBank = 5;

// What QS Studio knows about the banks of one synth model: program data and mix data, from the QS itself or from .syx files.
// Each model gets its own file (QSStudio.<model>.library) so a QS6's banks and a QSR's banks never overwrite each other.
struct Library {
    struct MixEntry {
        juce::String name;
        qs::Bytes dump;  // the New/Old Mix Dump message it came from
    };
    std::array<std::array<std::optional<qs::Program>, 128>, kBanks.size()> programs;
    std::array<std::array<std::optional<MixEntry>, 100>, kBanks.size()> mixes;
    // Each program's effect patch (65 bytes), when known: on the QS a program is stored together with its effect.
    std::array<std::array<std::optional<qs::Bytes>, 128>, kBanks.size()> effects;
    std::array<juce::String, kBanks.size()> source, mixSource;  // where each bank's data came from
    juce::String model = "QSR";  // file key: QS6, QSR, QS7, QS8, QS6.1 ...

    bool isFactorySource(int bank) const { return source[size_t(bank)].startsWith("Factory file"); }

    // "QS6 v2.00" or "QS6" -> "QS6". Used for the library file name.
    static juce::String modelKey(const juce::String& modelOrDeviceName) {
        auto k = modelOrDeviceName.upToFirstOccurrenceOf(" ", false, false).retainCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.");
        return k.isNotEmpty() ? k : juce::String("QS");
    }

    void clear() {
        for (auto& b : programs) b.fill(std::nullopt);
        for (auto& b : mixes) b.fill(std::nullopt);
        for (auto& b : effects) b.fill(std::nullopt);
        source.fill({});
        mixSource.fill({});
    }

    bool setMix(int bank, int number, const qs::Bytes& msg) {
        auto mi = qs::parseMixDump(msg);
        if (!mi || number < 0 || number >= 100) return false;
        mixes[size_t(bank)][size_t(number)] = MixEntry{juce::String(mi->name).trimEnd(), msg};
        return true;
    }

    // Import the program and mix dumps in a .syx file into one bank of the library (QS Studio only; nothing is sent to the QS).
    // Returns the number of programs; mixesOut gets the number of mixes.
    int importFile(const juce::File& f, int bank, const juce::String& sourceText, int* mixesOut = nullptr) {
        juce::MemoryBlock mb;
        if (!f.loadFileAsData(mb)) return 0;
        auto ptr = static_cast<const uint8_t*>(mb.getData());
        int n = 0, nm = 0;
        for (auto& m : qs::splitSysex(qs::Bytes(ptr, ptr + mb.getSize()))) {
            const int op = qs::opcodeOf(m);
            if (op == qs::UserProgramDump && m.size() > 6) {
                if (auto p = qs::Program::fromDump(m)) {
                    programs[size_t(bank)][m[6] & 0x7F] = p;
                    ++n;
                }
            } else if ((op == qs::NewMixDump || op == qs::OldMixDump) && m.size() > 6 && m[6] < 100) {
                nm += setMix(bank, m[6], m);
            } else if (op == qs::UserEffectsDump && m.size() > 6) {
                if (auto e = qs::Effects::fromDump(m)) effects[size_t(bank)][m[6] & 0x7F] = e->bytes();
            }
        }
        const auto label = sourceText.isNotEmpty() ? sourceText : f.getFileName();
        if (n > 0) source[size_t(bank)] = label;
        if (nm > 0) mixSource[size_t(bank)] = label;
        if (mixesOut) *mixesOut = nm;
        return n;
    }

    // Which bank a factory file belongs to, from its name: *PST1* -> Preset 1 ... *PST4* -> GenMIDI. -1 if unknown.
    static int factoryBankForName(const juce::String& fileName) {
        const auto up = fileName.toUpperCase();
        for (int i = 1; i <= 4; ++i)
            if (up.contains("PST" + juce::String(i))) return i;
        return -1;
    }

    // Name to show: what was read or imported, else the factory name built into QS Studio (Preset 1-3, GenMIDI).
    juce::String nameOf(int bank, int prog) const {
        const auto& p = programs[size_t(bank)][size_t(prog)];
        if (p) return juce::String(p->name()).trimEnd();
        if (bank >= 1 && bank <= 4) return juce::String(qs::factoryProgramName(bank, prog)).trimEnd();
        return {};
    }
    juce::String mixNameOf(int bank, int mix) const {
        const auto& m = mixes[size_t(bank)][size_t(mix)];
        if (m) return m->name;
        if (bank >= 1 && bank <= 4) return juce::String(qs::factoryMixName(bank, mix)).trimEnd();
        return {};
    }
    bool hasBuiltInNames(int bank) const { return bank >= 1 && bank <= 4; }
    int countRead(int bank) const {
        int n = 0;
        for (auto& p : programs[size_t(bank)]) n += p.has_value();
        return n;
    }
    int countMixesRead(int bank) const {
        int n = 0;
        for (auto& m : mixes[size_t(bank)]) n += m.has_value();
        return n;
    }

    juce::File file() const { return settingsDir().getChildFile("QSStudio." + model + ".library"); }

    void save() const {
        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        root->setProperty("_model", model);
        for (size_t b = 0; b < kBanks.size(); ++b) {
            juce::Array<juce::var> arr;
            for (auto& p : programs[b])
                arr.add(p ? juce::var(juce::String::toHexString(p->bytes().data(), int(p->bytes().size()), 0)) : juce::var());
            root->setProperty(kBanks[b].title, arr);
        }
        juce::DynamicObject::Ptr mixRoot = new juce::DynamicObject();
        for (size_t b = 0; b < kBanks.size(); ++b) {
            juce::Array<juce::var> arr;
            bool any = false;
            for (auto& m : mixes[b]) {
                arr.add(m ? juce::var(juce::String::toHexString(m->dump.data(), int(m->dump.size()), 0)) : juce::var());
                any |= m.has_value();
            }
            if (any) mixRoot->setProperty(kBanks[b].title, arr);
        }
        root->setProperty("_mixes", juce::var(mixRoot.get()));
        juce::DynamicObject::Ptr fxRoot = new juce::DynamicObject();
        for (size_t b = 0; b < kBanks.size(); ++b) {
            juce::Array<juce::var> arr;
            bool any = false;
            for (auto& e : effects[b]) {
                arr.add(e ? juce::var(juce::String::toHexString(e->data(), int(e->size()), 0)) : juce::var());
                any |= e.has_value();
            }
            if (any) fxRoot->setProperty(kBanks[b].title, arr);
        }
        root->setProperty("_effects", juce::var(fxRoot.get()));
        juce::DynamicObject::Ptr src = new juce::DynamicObject(), msrc = new juce::DynamicObject();
        for (size_t b = 0; b < kBanks.size(); ++b) {
            src->setProperty(kBanks[b].title, source[b]);
            msrc->setProperty(kBanks[b].title, mixSource[b]);
        }
        root->setProperty("_sources", juce::var(src.get()));
        root->setProperty("_mixSources", juce::var(msrc.get()));
        file().replaceWithText(juce::JSON::toString(juce::var(root.get())));
    }

    // Load this model's library. The first time, an old single-file library (before 0.9) is taken over by the model last used.
    void load() { loadFrom(file()); }
    bool loadFrom(const juce::File& f) {
        auto fromFile = f;
        if (!fromFile.existsAsFile()) {
            auto legacy = settingsDir().getChildFile("QSStudio.library");
            if (!legacy.existsAsFile() || modelKey(settings().getValue("lastModel", "QSR")) != model) return false;
            fromFile = legacy;
        }
        auto v = juce::JSON::parse(fromFile);
        if (!v.isObject()) return false;
        clear();
        auto hexBytes = [](const juce::var& x) {
            juce::MemoryBlock mb;
            mb.loadFromHexString(x.toString());
            auto ptr = static_cast<const uint8_t*>(mb.getData());
            return qs::Bytes(ptr, ptr + mb.getSize());
        };
        for (size_t b = 0; b < kBanks.size(); ++b) {
            source[b] = v["_sources"][kBanks[b].title].toString();
            mixSource[b] = v["_mixSources"][kBanks[b].title].toString();
            if (auto* arr = v[kBanks[b].title].getArray())
                for (int i = 0; i < juce::jmin(128, arr->size()); ++i)
                    if ((*arr)[i].toString().isNotEmpty()) programs[b][size_t(i)] = qs::Program::fromData(hexBytes((*arr)[i]));
            if (auto* arr = v["_mixes"][kBanks[b].title].getArray())
                for (int i = 0; i < juce::jmin(100, arr->size()); ++i)
                    if ((*arr)[i].toString().isNotEmpty()) setMix(int(b), i, hexBytes((*arr)[i]));
            if (auto* arr = v["_effects"][kBanks[b].title].getArray())
                for (int i = 0; i < juce::jmin(128, arr->size()); ++i)
                    if ((*arr)[i].toString().isNotEmpty()) effects[b][size_t(i)] = hexBytes((*arr)[i]);
        }
        if (fromFile != file()) {  // migrated: keep the old file as a backup so it isn't taken over twice
            save();
            fromFile.moveFileTo(fromFile.withFileExtension("library.old"));
        }
        return true;
    }

    // Switch to another model's library (saving this one first).
    void switchModel(const juce::String& newModel) {
        const auto key = modelKey(newModel);
        if (key == model) return;
        save();
        model = key;
        clear();
        load();
    }
};

struct Session : private juce::MidiKeyboardState::Listener, private juce::MidiInputCallback {
    Session() {
        keys.addListener(this);
        channel = juce::jlimit(1, 16, settings().getIntValue("midiChannel", 1));
        library.model = Library::modelKey(settings().getValue("lastModel", "QSR"));
        library.load();
    }
    ~Session() override {
        *aliveFlag = false;
        closeController();
        keys.removeListener(this);
    }

    // A MIDI keyboard/controller plugged into the computer: everything it plays is sent to the QS on our channel.
    bool openController(const juce::String& identifier, juce::String& error) {
        closeController();
        if (identifier.isEmpty()) return true;
        controllerIn = juce::MidiInput::openDevice(identifier, this);
        if (controllerIn == nullptr) {
            error = "Couldn't open that MIDI input. Another program (or QS Studio's own QS connection) may be using it.";
            return false;
        }
        controllerIn->start();
        return true;
    }
    void closeController() {
        if (controllerIn) controllerIn->stop();
        controllerIn.reset();
    }

    MidiLink midi;
    juce::MidiKeyboardState keys;
    Library library;
    int channel = 1;        // 1-16, the QS Program mode channel
    juce::String deviceName;  // from Identify, e.g. "QS6 v2.00"
    qs::GlobalSettings global;  // read from the QS after Identify; factory defaults until then
    bool globalKnown = false;
    // Which sample ROM the connected (or last connected) synth has: the QS6's own, or the QS7/QS8/QSR one.
    qs::RomSet romSet() const {
        const auto model = (deviceName.isNotEmpty() ? deviceName : settings().getValue("lastModel", "QSR")).upToFirstOccurrenceOf(" ", false, false);
        return qs::romSetForModel(model.toStdString());
    }
    // The models QS Studio draws a panel for, in the Panel look menu's order.
    static const juce::StringArray& panelModels() {
        static const juce::StringArray m{"QS6", "QS6.1", "QS7", "QS7.1", "QS8", "QS8.1", "QS6.2", "QS8.2", "QSR"};
        return m;
    }
    // The model whose panel is shown: the Panel look setting, or the connected (or last connected) synth.
    juce::String panelModel() const {
        auto chosen = settings().getValue("panelModel");
        if (chosen.isEmpty()) {  // settings from before 0.4.1 stored a number
            const int old = settings().getIntValue("panelLook", 1);
            chosen = old == 2 ? "QS6" : old == 3 ? "QSR" : "";
        }
        if (panelModels().contains(chosen)) return chosen;
        const auto key = Library::modelKey(deviceName.isNotEmpty() ? deviceName : settings().getValue("lastModel", "QSR"));
        if (panelModels().contains(key)) return key;
        return key.startsWith("QS") && !key.startsWith("QSR") ? juce::String("QS6") : juce::String("QSR");  // QuadraSynth and unknown keyboards
    }
    theme::Style panelStyle() const {
        const auto m = panelModel();
        return m == "QSR" ? theme::Style::QSR : m.endsWith(".2") ? theme::Style::Silver : theme::Style::QS6;
    }
    // Only the original QS6 has a single assignable slider (Controller A); the QS6.1 and every later model has four (A-D).
    bool hasOneControllerSlider() const { return panelModel() == "QS6"; }
    // The keyboard's range: QS6 family 61 keys, QS7 76 keys, QS8 and the QSR (any controller) 88.
    juce::Range<int> keyRange() const {
        const auto m = panelModel();
        if (m.startsWith("QS6")) return {36, 96};
        if (m.startsWith("QS7")) return {28, 103};
        return {21, 108};
    }

    // Hooks set by MainComponent so tabs can hand work to each other.
    std::function<void(const qs::Program&, const juce::String& where)> openInEditor;
    std::function<void(const qs::Mix&, const juce::String& where, int userNumber)> openMixEditor;   // userNumber -1 if not a User mix
    std::function<void(const qs::Effects&, const juce::String& where, int edit)> openEffectsEditor; // edit: 0 Program, 1 Mix
    std::function<void(int channel, const juce::String& mixName)> editMixChannelProgram;
    std::optional<qs::Program> effectsProgram;  // set before openEffectsEditor when the effect's program is known
    std::function<void(const juce::String&)> setStatus;
    std::function<void(int bank, int program)> currentProgramChanged;
    std::function<void()> channelChanged;
    std::function<void()> libraryChanged;  // the library was replaced (import) or switched to another model  // the MIDI channel was changed somewhere other than the top bar
    int currentBank = 0, currentProgram = 0;
    int currentMixBank = 0, currentMix = 0;
    bool mixModeSent = false;  // the QS was put in Mix mode by us
    enum class CardState { Unknown, Present, Absent };
    CardState cardState = CardState::Unknown;  // checked the first time card banks are opened after connecting
    juce::Value volume{juce::var(127)};  // Volume slider/knob (sent as CC7); shared by the strip and the QSR panel

    void setChannel(int ch) {
        channel = juce::jlimit(1, 16, ch);
        settings().setValue("midiChannel", channel);
        settings().saveIfNeeded();
    }

    // One audio device for the whole app (recorder and sound player both use it), opened on first use with the saved setup.
    juce::AudioDeviceManager& audio() {
        if (!audioOpened) {
            audioOpened = true;
            std::unique_ptr<juce::XmlElement> state(juce::parseXML(settings().getValue("audioState")));
            audioDevices.initialise(2, 2, state.get(), true);
        }
        return audioDevices;
    }
    // Notes from the on-screen keyboard and the controller go to the QS; the sound player can turn that off.
    std::atomic<bool> notesToQs{true};

    // Transfer pace (Connection page): stretches the waits in long reads for slow or flaky MIDI interfaces.
    static int paced(int ms) {
        const int pace = settings().getIntValue("transferPace", 0);
        return ms * (pace == 2 ? 4 : pace == 1 ? 2 : 1);
    }

    juce::AudioDeviceManager audioDevices;
    bool audioOpened = false;
    bool programModeSent = false;  // Program mode is selected once per connection, not on every click
    int checkToken = 0;
    bool checkMix = false, checkIsRetry = false;
    int checkBank = 0, checkNumber = 0;
    juce::String checkExpected, checkWhat;
    juce::uint32 checkSentAt = 0;

    // Select a program on the QS: Program mode, bank select, program change, then a check that the QS really switched.
    void selectProgram(int bank, int prog, bool slow = false) {
        currentBank = bank;
        currentProgram = prog;
        qslog::action("Select program: " + juce::String(kBanks[size_t(bank)].title) + " " + juce::String(prog) + " \"" +
                      library.nameOf(bank, prog) + "\" on channel " + juce::String(channel));
        if (globalKnown && global.programSelect >= 2) {
            // On a channel, a QS6 in Program mode ignores program changes (tested). On is the setting that works for
            // programs, and selectMix sets a channel only for the moment it needs one.
            qslog::action("Global > Program Select was on channel " + juce::String(global.programSelect - 1) + "; setting it to On");
            setProgramSelect(1);
            if (setStatus) setStatus("Program Select on the QS was set to one channel, which stops program changes. QS Studio set it back to On.");
        }
        // Program mode every time: an edit page may be showing, or someone pressed MIX on the QS (QS Studio can't see that).
        playingMix = false;
        midi.send(qs::modeSelect(0));
        programModeSent = true;
        mixModeSent = false;
        if (slow) juce::Thread::sleep(60);
        midi.sendMidi(juce::MidiMessage::controllerEvent(channel, 0, kBanks[size_t(bank)].cc0));
        midi.sendMidi(juce::MidiMessage::programChange(channel, prog));
        if (currentProgramChanged) currentProgramChanged(bank, prog);
        scheduleCheck(false, bank, prog, slow);
    }

    // Mixes can only be picked over MIDI when Global > MIDI Program Select is set to a channel (Ch 1-16): the QS then
    // changes mixes on program changes received on that channel (QS6 manual, Global page 14). With it "On", program
    // changes go to the programs inside the mix instead. Tested on a QS6: left on a channel, Program mode stops
    // changing programs. So QS Studio sets it to its channel only for the mix change and puts the user's setting back.
    bool canSelectMixes() const { return globalKnown; }
    int mixSelectChannel() const { return global.programSelect >= 2 ? global.programSelect - 1 : channel; }

    void setProgramSelect(int value) {  // 0 Off, 1 On, 2-17 = Channel 1-16
        global.programSelect = value;
        midi.send(qs::paramEdit(0, 0, 0, 5, 0, 0, value));
    }

    void selectMix(int bank, int mix, bool slow = false) {
        currentMixBank = bank;
        currentMix = mix;
        playingMix = true;
        if (auto& cached = library.mixes[size_t(bank)][size_t(mix)]) setActiveMix(qs::Mix::fromDump(cached->dump));
        else setActiveMix(std::nullopt);
        // Mix mode every time: someone may have pressed PROGRAM on the QS since, and QS Studio can't see that.
        midi.send(qs::modeSelect(1));
        mixModeSent = true;
        programModeSent = false;
        const int gap = slow ? 60 : 15;  // ms between steps, longer on the automatic retry
        const int ch = mixSelectChannel();
        const int original = global.programSelect;
        const bool temporary = original < 2;
        if (temporary) {
            midi.send(qs::paramEdit(0, 0, 0, 5, 0, 0, ch + 1));  // Program Select = this channel, for this change only
            juce::Thread::sleep(gap);
        }
        midi.sendMidi(juce::MidiMessage::controllerEvent(ch, 0, kBanks[size_t(bank)].cc0));
        midi.sendMidi(juce::MidiMessage::programChange(ch, mix));
        if (temporary) {
            juce::Thread::sleep(gap * 2);
            midi.send(qs::paramEdit(0, 0, 0, 5, 0, 0, original));  // and back to the user's setting
            midi.send(qs::modeSelect(1));  // the edit above leaves the Global page on the QS's display; back to Mix play
        }
        qslog::action(juce::String(slow ? "Retry: select mix: " : "Select mix: ") + kBanks[size_t(bank)].title + " " + juce::String(mix) + " \"" +
                      library.mixNameOf(bank, mix) + "\" via channel " + juce::String(ch) +
                      (temporary ? " (Program Select set for the change, then restored to " + juce::String(original) + ")" : juce::String()));
        if (currentProgramChanged) currentProgramChanged(bank, mix);
        scheduleCheck(true, bank, mix, slow);
    }

    // ---- Did the QS really switch? A moment after a selection, read its edit buffer and compare the name.
    // Mismatches go to the status bar and the log, so "QS Studio changed but the synth didn't" shows up in bug reports.
    void scheduleCheck(bool mix, int bank, int number, bool isRetry = false) {
        const int token = ++checkToken;
        checkMix = mix;
        checkBank = bank;
        checkNumber = number;
        checkIsRetry = isRetry;
        checkExpected = mix ? library.mixNameOf(bank, number) : library.nameOf(bank, number);
        checkWhat = juce::String(mix ? "mix " : "program ") + kBanks[size_t(bank)].title + " " + juce::String(number);
        juce::Timer::callAfterDelay(450, [this, alive = aliveFlag, token] {
            if (!*alive || token != checkToken || !midi.isOpen() || midi.jobsRunning > 0) return;
            checkSentAt = juce::Time::getMillisecondCounter();
            midi.send(checkMix ? qs::mixRequest(100) : qs::editProgramRequest(0));
        });
    }

    // Called with every SysEx from the QS (message thread). Returns true if it was the answer to a check.
    // Mix mode was entered without QS Studio knowing which mix (the panel's MIX key): ask the QS for its mix edit buffer.
    void learnActiveMix() {
        playingMix = true;
        setActiveMix(std::nullopt);
        if (!midi.isOpen()) return;
        wantMixData = true;
        juce::Timer::callAfterDelay(300, [this, alive = aliveFlag] {
            if (*alive && midi.jobsRunning == 0) midi.send(qs::mixRequest(100));
        });
    }

    bool checkReply(const qs::Bytes& msg) {
        if (wantMixData && qs::opcodeOf(msg) == qs::NewMixDump && msg.size() > 6 && msg[6] == 100 && checkSentAt == 0) {
            wantMixData = false;
            setActiveMix(qs::Mix::fromDump(msg));
            return true;
        }
        if (checkSentAt == 0 || juce::Time::getMillisecondCounter() - checkSentAt > 2500) return false;
        juce::String got;
        const int op = qs::opcodeOf(msg);
        if (!checkMix && op == qs::EditProgramDump) {
            if (auto p = qs::Program::fromDump(msg)) got = juce::String(p->name()).trimEnd();
        } else if (checkMix && (op == qs::NewMixDump || op == qs::OldMixDump)) {
            auto mi = qs::parseMixDump(msg);
            if (!mi || mi->number != 100) return false;
            got = juce::String(mi->name).trimEnd();
            setActiveMix(qs::Mix::fromDump(msg));  // what the QS is really playing: which channels the keyboard should play
        } else {
            return false;
        }
        checkSentAt = 0;
        const auto expected = checkExpected.trimEnd();
        if (expected.isEmpty()) {
            qslog::action("Check: QS is on \"" + got + "\" after selecting " + checkWhat + " (name not in the library to compare)");
        } else if (got == expected) {
            qslog::action("Check OK: QS is on \"" + got + "\"");
        } else if (!checkIsRetry) {
            // Try once more, slower, before bothering anyone.
            qslog::action("Check: selected " + checkWhat + " \"" + expected + "\" but the QS is on \"" + got + "\"; trying again");
            const bool mixAgain = checkMix;
            const int b = checkBank, n = checkNumber;
            juce::Timer::callAfterDelay(100, [this, alive = aliveFlag, mixAgain, b, n] {
                if (!*alive) return;
                if (mixAgain) selectMix(b, n, true);
                else selectProgram(b, n, true);
            });
        } else {
            qslog::action("Check FAILED: selected " + checkWhat + " \"" + expected + "\" but the QS is on \"" + got + "\"");
            if (setStatus)
                setStatus("The QS didn't switch: it's on \"" + got + "\", not \"" + expected + "\". " +
                          (checkMix ? "Check that it's in Mix mode." : "Press PROGRAM on the QS, and check Global > MIDI Program Select is On."));
        }
        return true;
    }

    void allNotesOff() {
        keys.allNotesOff(0);
        for (int ch = 1; ch <= 16; ++ch) {  // a mix plays on several channels
            midi.sendMidi(juce::MidiMessage::allNotesOff(ch));
            midi.sendMidi(juce::MidiMessage::allSoundOff(ch));
        }
        for (auto& m : noteChannels) m = 0;
    }

    // ---- Playing a mix from QS Studio's keyboard, wheels and sliders, and the controller.
    // In Mix mode the QS's own keys play every mix channel whose Keyboard switch is on, each on its own MIDI channel.
    // QS Studio does the same, so a layered or split mix sounds whole instead of just channel 1.
    std::optional<qs::Mix> activeMix;  // the mix the QS is playing, when known (library copy, or read back by the check)
    std::atomic<uint16_t> mixKeyboardChannels{0};  // from activeMix: channels with Enable and Keyboard on (0 = not known)
    std::atomic<bool> playingMix{false};
    bool wantMixData = false;

    void setActiveMix(std::optional<qs::Mix> m) {
        activeMix = std::move(m);
        uint16_t mask = 0;
        if (activeMix) {
            static const auto& enableP = qs::mixChannelParam(qs::findMixChannelParam("Enable"));
            static const auto& kbdP = qs::mixChannelParam(qs::findMixChannelParam("Keyboard"));
            for (int ch = 0; ch < qs::kMixChannels; ++ch)
                if (activeMix->channel(ch, enableP) && activeMix->channel(ch, kbdP)) mask = uint16_t(mask | (1u << ch));
        }
        mixKeyboardChannels = mask;
    }

    uint16_t playChannels() const {
        const uint16_t mix = mixKeyboardChannels.load();
        if (playingMix && mix != 0) return mix;
        return uint16_t(1u << (juce::jlimit(1, 16, channel) - 1));
    }

    // Any thread (the controller comes in on the MIDI thread).
    void sendPerformance(const juce::MidiMessage& m) {
        uint16_t mask = playChannels();
        if (m.isNoteOn()) noteChannels[size_t(m.getNoteNumber())] = mask;
        else if (m.isNoteOff()) {
            // Off on the channels the note started on, even if the mix changed while it was held.
            if (noteChannels[size_t(m.getNoteNumber())] != 0) mask = noteChannels[size_t(m.getNoteNumber())];
            noteChannels[size_t(m.getNoteNumber())] = 0;
        }
        for (int ch = 0; ch < 16; ++ch)
            if (mask & (1u << ch)) {
                auto copy = m;
                copy.setChannel(ch + 1);
                midi.sendMidi(copy);
            }
    }
    std::array<std::atomic<uint16_t>, 128> noteChannels{};

private:
    static bool& echoing() {
        thread_local bool e = false;
        return e;
    }
    void handleNoteOn(juce::MidiKeyboardState*, int, int note, float vel) override {
        if (!echoing() && notesToQs) sendPerformance(juce::MidiMessage::noteOn(channel, note, juce::jlimit(0.01f, 1.0f, vel)));
    }
    void handleNoteOff(juce::MidiKeyboardState*, int, int note, float) override {
        if (!echoing() && notesToQs) sendPerformance(juce::MidiMessage::noteOff(channel, note));
    }

    // From the MIDI controller (MIDI input thread).
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& in) override {
        if (in.isSysEx() || in.isActiveSense() || in.isMidiClock() || in.getChannel() == 0) return;
        auto m = in;
        m.setChannel(channel);
        if (m.isNoteOnOrOff() || m.isPitchWheel() || m.isController() || m.isAftertouch() || m.isChannelPressure()) {
            if (notesToQs) sendPerformance(m);
        } else {
            midi.sendMidi(m);
        }
        if (auto* p = playerInput.load()) p->addMessageToQueue(m);  // straight to the sound player, without the UI's delay
        // Show it on screen (keys light up, wheels and sliders move) on the message thread, without sending it twice.
        juce::MessageManager::callAsync([this, m, alive = aliveFlag] {
            if (!*alive) return;
            echoing() = true;
            if (m.isNoteOn()) keys.noteOn(1, m.getNoteNumber(), m.getFloatVelocity());
            else if (m.isNoteOff()) keys.noteOff(1, m.getNoteNumber(), 0.0f);
            echoing() = false;
            if (controllerMoved) controllerMoved(m);
        });
    }
    std::shared_ptr<bool> aliveFlag = std::make_shared<bool>(true);

public:
    std::function<void(const juce::MidiMessage&)> controllerMoved;  // a message from the MIDI controller, on the message thread
    std::atomic<juce::MidiMessageCollector*> playerInput{nullptr};   // the sound player, while it's listening
    // True while the controller's notes are being shown on the on-screen keyboard (they're already sent).
    static bool isEcho() { return echoing(); }

private:

    std::unique_ptr<juce::MidiInput> controllerIn;
};
