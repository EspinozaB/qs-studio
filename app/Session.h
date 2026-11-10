// State shared by the tabs: the MIDI link, the channel the QS listens on, the on-screen keyboard,
// and the program library (names + data per bank, cached next to the exe).
#pragma once
#include <JuceHeader.h>

#include <array>
#include <optional>

#include "../core/qs_program.h"
#include "MidiLink.h"

juce::File settingsDir();
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

struct Library {
    std::array<std::array<std::optional<qs::Program>, 128>, kBanks.size()> programs;
    std::array<juce::String, kBanks.size()> source;  // where each bank's data came from
    bool isFactorySource(int bank) const { return source[size_t(bank)].startsWith("Factory file"); }

    // Import the program dumps in a .syx file into one bank of the library (QS Studio only; nothing is sent to the QS).
    int importFile(const juce::File& f, int bank, const juce::String& sourceText) {
        juce::MemoryBlock mb;
        if (!f.loadFileAsData(mb)) return 0;
        auto ptr = static_cast<const uint8_t*>(mb.getData());
        int n = 0;
        for (auto& m : qs::splitSysex(qs::Bytes(ptr, ptr + mb.getSize())))
            if (qs::opcodeOf(m) == qs::UserProgramDump && m.size() > 6)
                if (auto p = qs::Program::fromDump(m)) {
                    programs[size_t(bank)][m[6] & 0x7F] = p;
                    ++n;
                }
        if (n > 0) source[size_t(bank)] = sourceText.isNotEmpty() ? sourceText : f.getFileName();
        return n;
    }

    // Which bank a factory file belongs to, from its name: *PST1* -> Preset 1 ... *PST4* -> GenMIDI. -1 if unknown.
    static int factoryBankForName(const juce::String& fileName) {
        const auto up = fileName.toUpperCase();
        for (int i = 1; i <= 4; ++i)
            if (up.contains("PST" + juce::String(i))) return i;
        return -1;
    }
    juce::String nameOf(int bank, int prog) const {
        const auto& p = programs[size_t(bank)][size_t(prog)];
        return p ? juce::String(p->name()).trimEnd() : juce::String();
    }
    int countRead(int bank) const {
        int n = 0;
        for (auto& p : programs[size_t(bank)]) n += p.has_value();
        return n;
    }

    juce::File file() const { return settingsDir().getChildFile("QSStudio.library"); }

    void save() const {
        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        for (size_t b = 0; b < kBanks.size(); ++b) {
            juce::Array<juce::var> arr;
            for (auto& p : programs[b])
                arr.add(p ? juce::var(juce::String::toHexString(p->bytes().data(), int(p->bytes().size()), 0)) : juce::var());
            root->setProperty(kBanks[b].title, arr);
        }
        juce::DynamicObject::Ptr src = new juce::DynamicObject();
        for (size_t b = 0; b < kBanks.size(); ++b) src->setProperty(kBanks[b].title, source[b]);
        root->setProperty("_sources", juce::var(src.get()));
        file().replaceWithText(juce::JSON::toString(juce::var(root.get())));
    }

    void load() {
        auto v = juce::JSON::parse(file());
        for (size_t b = 0; b < kBanks.size(); ++b) source[b] = v["_sources"][kBanks[b].title].toString();
        for (size_t b = 0; b < kBanks.size(); ++b) {
            auto* arr = v[kBanks[b].title].getArray();
            if (arr == nullptr) continue;
            for (int i = 0; i < juce::jmin(128, arr->size()); ++i) {
                juce::MemoryBlock mb;
                const auto hex = (*arr)[i].toString();
                if (hex.isEmpty()) continue;
                mb.loadFromHexString(hex);
                auto ptr = static_cast<const uint8_t*>(mb.getData());
                programs[b][size_t(i)] = qs::Program::fromData(qs::Bytes(ptr, ptr + mb.getSize()));
            }
        }
    }
};

struct Session : private juce::MidiKeyboardState::Listener, private juce::MidiInputCallback {
    Session() {
        keys.addListener(this);
        channel = juce::jlimit(1, 16, settings().getIntValue("midiChannel", 1));
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
    // The original QS6 has one assignable slider (Controller A); the other models have four (A-D).
    bool hasOneControllerSlider() const { return deviceName.startsWith("QS6 "); }

    // Hooks set by MainComponent so tabs can hand work to each other.
    std::function<void(const qs::Program&, const juce::String& where)> openInEditor;
    std::function<void(const juce::String&)> setStatus;
    std::function<void(int bank, int program)> currentProgramChanged;
    int currentBank = 0, currentProgram = 0;

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
        }
        midi.sendMidi(juce::MidiMessage::controllerEvent(channel, 0, kBanks[size_t(bank)].cc0));
        midi.sendMidi(juce::MidiMessage::programChange(channel, prog));
        if (currentProgramChanged) currentProgramChanged(bank, prog);
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
