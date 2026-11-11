// QS Studio - session log for bug reports: what the user did, what QS Studio sent and what the synth answered.
// One file per day in "QS Studio Data/Logs" (session-YYYY-MM-DD.txt); files older than 30 days are deleted at start.
#pragma once
#include <JuceHeader.h>

#include <atomic>

#include "../core/qs_sysex.h"

juce::File settingsDir();
juce::PropertiesFile& settings();

namespace qslog {

class Writer {
public:
    static Writer& get() {
        static Writer w;
        return w;
    }

    juce::File folder() const { return settingsDir().getChildFile("Logs"); }
    juce::File todaysFile() const { return folder().getChildFile("session-" + juce::Time::getCurrentTime().formatted("%Y-%m-%d") + ".txt"); }

    // MIDI traffic lines are on unless turned off on the Connection page; actions and problems are always written.
    bool traffic() const { return trafficOn.load(); }
    void setTraffic(bool on) {
        trafficOn = on;
        settings().setValue("logMidiTraffic", on);
        settings().saveIfNeeded();
    }

    void start(const juce::String& version) {
        trafficOn = settings().getBoolValue("logMidiTraffic", true);
        folder().createDirectory();
        for (auto& f : folder().findChildFiles(juce::File::findFiles, false, "session-*.txt"))
            if (f.getLastModificationTime() < juce::Time::getCurrentTime() - juce::RelativeTime::days(30)) f.deleteFile();
        write("---- QS Studio " + version + " started (" + juce::SystemStats::getOperatingSystemName() + ")");
    }

    void write(const juce::String& line) {
        const auto now = juce::Time::getCurrentTime();
        const auto stamp = now.formatted("%H:%M:%S") + "." + juce::String(now.getMilliseconds()).paddedLeft('0', 3);
        const juce::ScopedLock sl(lock);
        const auto f = todaysFile();
        if (f != currentFile || out == nullptr) {
            out.reset();
            currentFile = f;
            folder().createDirectory();
            out = std::make_unique<juce::FileOutputStream>(f);
            if (out->failedToOpen()) { out.reset(); return; }
        }
        *out << stamp << "  " << line << juce::newLine;
        out->flush();
    }

private:
    Writer() = default;
    juce::CriticalSection lock;
    juce::File currentFile;
    std::unique_ptr<juce::FileOutputStream> out;
    std::atomic<bool> trafficOn{true};
};

inline juce::String hex(const uint8_t* d, size_t n, size_t max = 16) {
    juce::String s;
    for (size_t i = 0; i < n && i < max; ++i) s << juce::String::toHexString(int(d[i])).paddedLeft('0', 2).toUpperCase() << " ";
    if (n > max) s << "...";
    return s.trimEnd();
}

inline const char* opcodeName(int op) {
    switch (op) {
        case 0x00: return "User program dump";
        case 0x01: return "User program request";
        case 0x02: return "Edit program dump";
        case 0x03: return "Edit program request";
        case 0x04: return "Old mix dump";
        case 0x05: return "Old mix request";
        case 0x06: return "User effects dump";
        case 0x07: return "User effects request";
        case 0x08: return "Edit effects dump";
        case 0x09: return "Edit effects request";
        case 0x0A: return "Global dump";
        case 0x0B: return "Global request";
        case 0x0C: return "Send All request";
        case 0x0D: return "Mode select";
        case 0x0E: return "Mix dump";
        case 0x0F: return "Mix request";
        case 0x10: return "Parameter edit";
        case 0x11: return "Card sector erase";
        case 0x12: return "Card sector write";
        case 0x13: return "Card sector request";
        case 0x14: return "Card ACK";
        case 0x15: return "Card NACK";
        default: return "QS opcode";
    }
}

// One line describing a MIDI message, e.g. "Mode select: Program" or "Program change 5, ch 1".
inline juce::String describe(const juce::MidiMessage& m) {
    if (!m.isSysEx()) {
        if (m.isProgramChange()) return "Program change " + juce::String(m.getProgramChangeNumber()) + ", ch " + juce::String(m.getChannel());
        if (m.isController())
            return "CC" + juce::String(m.getControllerNumber()) + " = " + juce::String(m.getControllerValue()) + ", ch " + juce::String(m.getChannel());
        return m.getDescription();
    }
    const auto* d = m.getSysExData();  // without F0/F7
    const int n = m.getSysExDataSize();
    if (n >= 5 && d[0] == 0 && d[1] == 0 && d[2] == 0x0E && d[3] == 0x0E) {
        const int op = d[4];
        juce::String s = opcodeName(op);
        if (op == 0x0D && n >= 6) return s + ": " + (d[5] & 1 ? "Mix" : "Program");
        if (op == 0x10 && n >= 9) {
            const int mode = (d[5] >> 5) & 3, func = d[5] & 0x1F, sound = (d[6] >> 5) & 3, page = d[6] & 0x1F;
            const int ch = (d[7] >> 3) & 0xF, pot = (d[7] >> 1) & 3, value = int(int8_t(uint8_t(((d[7] & 1) << 7) | d[8])));
            static const char* modes[] = {"Global", "Mix", "Program", "Effects"};
            return s + ": " + modes[mode] + " func " + juce::String(func) + " page " + juce::String(page) + " pot " + juce::String(pot) +
                   (mode == 2 || mode == 3 ? " sound/bus " + juce::String(sound + 1) : juce::String()) +
                   (mode == 1 ? " ch " + juce::String(ch + 1) : juce::String()) + " = " + juce::String(value);
        }
        if (n >= 6 && op != 0x0B && op != 0x0C) s << " #" << int(d[5]);
        if (op == 0x13 && n >= 7) s << " block " << int(d[6]);
        return s + " (" + juce::String(n + 2) + " bytes)";
    }
    if (n >= 3 && d[0] == 0x7E) return "Universal SysEx " + hex(d, size_t(n), 8) + " (" + juce::String(n + 2) + " bytes)";
    return "SysEx " + hex(d, size_t(n)) + " (" + juce::String(n + 2) + " bytes)";
}

inline void action(const juce::String& what) { Writer::get().write(what); }
inline void midiOut(const juce::MidiMessage& m) {
    if (Writer::get().traffic()) Writer::get().write("-> " + describe(m));
}
inline void midiIn(const juce::MidiMessage& m) {
    if (Writer::get().traffic()) Writer::get().write("<- " + describe(m));
}

}  // namespace qslog
