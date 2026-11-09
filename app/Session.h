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
// QS manual: controller 0 = 0 User, 1-3 Preset 1-3, 4 GenMIDI, 5-15 card banks.
inline const std::array<BankInfo, 7> kBanks{{{"User", 0}, {"Preset 1", 1}, {"Preset 2", 2}, {"Preset 3", 3},
                                             {"GenMIDI", 4}, {"Card A", 5}, {"Card B", 6}}};

struct Library {
    std::array<std::array<std::optional<qs::Program>, 128>, kBanks.size()> programs;

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
        file().replaceWithText(juce::JSON::toString(juce::var(root.get())));
    }

    void load() {
        auto v = juce::JSON::parse(file());
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

struct Session : private juce::MidiKeyboardState::Listener {
    Session() {
        keys.addListener(this);
        channel = juce::jlimit(1, 16, settings().getIntValue("midiChannel", 1));
        library.load();
    }
    ~Session() override { keys.removeListener(this); }

    MidiLink midi;
    juce::MidiKeyboardState keys;
    Library library;
    int channel = 1;        // 1-16, the QS Program mode channel
    juce::String deviceName;  // from Identify, e.g. "QS6 v2.00"

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

    // Select a program on the QS: Program mode, bank select, program change.
    void selectProgram(int bank, int prog) {
        currentBank = bank;
        currentProgram = prog;
        midi.send(qs::modeSelect(0));
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
    void handleNoteOn(juce::MidiKeyboardState*, int, int note, float vel) override {
        midi.sendMidi(juce::MidiMessage::noteOn(channel, note, juce::jlimit(0.01f, 1.0f, vel)));
    }
    void handleNoteOff(juce::MidiKeyboardState*, int, int note, float) override {
        midi.sendMidi(juce::MidiMessage::noteOff(channel, note));
    }
};
