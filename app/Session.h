// State shared by the tabs: the MIDI link, the channel the QS listens on, the on-screen keyboard,
// and the program library (names + data per bank, cached next to the exe).
#pragma once
#include <JuceHeader.h>

#include <array>
#include <optional>

#include "../core/qs_factory.h"
#include "../core/qs_program.h"
#include "../core/qs_samples.h"
#include "MidiLink.h"

juce::File settingsDir();      // "QS Studio Data" beside the program: every file QS Studio writes goes here
juce::File appFolder();
bool dataFolderWritable();
juce::String versionLabel();  // e.g. "0.4.0 pre-alpha"
juce::PropertiesFile& settings();

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
    // The original QS6 has one assignable slider (Controller A); the QS6.1 and later have four (A-D).
    // Follows the Panel look when it's set to a model, otherwise the connected (or last connected) synth.
    bool hasOneControllerSlider() const {
        const int look = settings().getIntValue("panelLook", 1);
        if (look == 2) return true;
        if (look == 3) return false;
        const auto model = deviceName.isNotEmpty() ? deviceName : settings().getValue("lastModel", "QSR");
        return model.upToFirstOccurrenceOf(" ", false, false) == "QS6";
    }

    // Hooks set by MainComponent so tabs can hand work to each other.
    std::function<void(const qs::Program&, const juce::String& where)> openInEditor;
    std::function<void(const juce::String&)> setStatus;
    std::function<void(int bank, int program)> currentProgramChanged;
    std::function<void()> channelChanged;
    std::function<void()> libraryChanged;  // the library was replaced (import) or switched to another model  // the MIDI channel was changed somewhere other than the top bar
    int currentBank = 0, currentProgram = 0;
    int currentMixBank = 0, currentMix = 0;
    bool mixModeSent = false;  // the QS was put in Mix mode by us
    juce::Value volume{juce::var(127)};  // Volume slider/knob (sent as CC7); shared by the strip and the QSR panel

    void setChannel(int ch) {
        channel = juce::jlimit(1, 16, ch);
        settings().setValue("midiChannel", channel);
        settings().saveIfNeeded();
    }

    bool programModeSent = false;  // Program mode is selected once per connection, not on every click

    // Select a program on the QS: Program mode (first time), bank select, program change.
    void selectProgram(int bank, int prog) {
        currentBank = bank;
        currentProgram = prog;
        if (!programModeSent) {
            midi.send(qs::modeSelect(0));
            programModeSent = true;
            mixModeSent = false;
        }
        midi.sendMidi(juce::MidiMessage::controllerEvent(channel, 0, kBanks[size_t(bank)].cc0));
        midi.sendMidi(juce::MidiMessage::programChange(channel, prog));
        if (currentProgramChanged) currentProgramChanged(bank, prog);
    }

    // Mixes can only be picked over MIDI when Global > MIDI Program Select is set to a channel (Ch 1-16): the QS then
    // changes mixes on program changes received on that channel (QS6 manual, Global page 14). With it "On", program
    // changes go to the programs inside the mix instead.
    bool canSelectMixes() const { return globalKnown && global.programSelect >= 2; }
    int mixSelectChannel() const { return juce::jlimit(1, 16, global.programSelect - 1); }

    void setProgramSelect(int value) {  // 0 Off, 1 On, 2-17 = Channel 1-16
        global.programSelect = value;
        midi.send(qs::paramEdit(0, 0, 0, 5, 0, 0, value));
    }

    void selectMix(int bank, int mix) {
        currentMixBank = bank;
        currentMix = mix;
        if (programModeSent || !mixModeSent) {
            midi.send(qs::modeSelect(1));
            mixModeSent = true;
            programModeSent = false;
        }
        const int ch = mixSelectChannel();
        midi.sendMidi(juce::MidiMessage::controllerEvent(ch, 0, kBanks[size_t(bank)].cc0));
        midi.sendMidi(juce::MidiMessage::programChange(ch, mix));
        if (currentProgramChanged) currentProgramChanged(bank, mix);
    }

    void allNotesOff() {
        keys.allNotesOff(0);
        midi.sendMidi(juce::MidiMessage::allNotesOff(channel));
        midi.sendMidi(juce::MidiMessage::allSoundOff(channel));
    }

private:
    static bool& echoing() {
        thread_local bool e = false;
        return e;
    }
    void handleNoteOn(juce::MidiKeyboardState*, int, int note, float vel) override {
        if (!echoing()) midi.sendMidi(juce::MidiMessage::noteOn(channel, note, juce::jlimit(0.01f, 1.0f, vel)));
    }
    void handleNoteOff(juce::MidiKeyboardState*, int, int note, float) override {
        if (!echoing()) midi.sendMidi(juce::MidiMessage::noteOff(channel, note));
    }

    // From the MIDI controller (MIDI input thread).
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& in) override {
        if (in.isSysEx() || in.isActiveSense() || in.isMidiClock() || in.getChannel() == 0) return;
        auto m = in;
        m.setChannel(channel);
        midi.sendMidi(m);
        // Light up the on-screen keys without sending the note a second time.
        echoing() = true;
        if (m.isNoteOn()) keys.noteOn(1, m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) keys.noteOff(1, m.getNoteNumber(), 0.0f);
        echoing() = false;
    }

    std::unique_ptr<juce::MidiInput> controllerIn;
};
