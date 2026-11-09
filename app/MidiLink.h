// MIDI connection to the QS: one input + one output, SysEx queue for request/response jobs.
#pragma once
#include <JuceHeader.h>

#include <deque>
#include <optional>
#include <functional>

#include "../core/qs_sysex.h"

class MidiLink : private juce::MidiInputCallback {
public:
    struct Listener {
        virtual ~Listener() = default;
        virtual void sysexReceived(const qs::Bytes& msg) = 0;  // called on the message thread
    };

    ~MidiLink() override { close(); }

    juce::StringArray inputNames() const { return names(juce::MidiInput::getAvailableDevices()); }
    juce::StringArray outputNames() const { return names(juce::MidiOutput::getAvailableDevices()); }

    bool open(const juce::String& inName, const juce::String& outName, juce::String& error) {
        close();
        for (auto& d : juce::MidiInput::getAvailableDevices())
            if (d.name == inName) input = juce::MidiInput::openDevice(d.identifier, this);
        for (auto& d : juce::MidiOutput::getAvailableDevices())
            if (d.name == outName) output = juce::MidiOutput::openDevice(d.identifier);
        if (input == nullptr || output == nullptr) {
            error = input == nullptr ? "Couldn't open MIDI input \"" + inName + "\"" : "Couldn't open MIDI output \"" + outName + "\"";
            close();
            return false;
        }
        input->start();
        return true;
    }

    void close() {
        if (input) input->stop();
        input.reset();
        const juce::ScopedLock sl(outLock);
        output.reset();
    }

    bool isOpen() const { return input != nullptr && output != nullptr; }

    void send(const qs::Bytes& m) {
        if (m.size() < 2 || m.front() != 0xF0 || m.back() != 0xF7) return;
        sendMidi(juce::MidiMessage::createSysExMessage(m.data() + 1, int(m.size()) - 2));
    }

    // Notes, controllers, program changes. Safe from any thread.
    void sendMidi(const juce::MidiMessage& m) {
        const juce::ScopedLock sl(outLock);
        if (output != nullptr) output->sendMessageNow(m);
    }

    // Seconds needed on a 31.25 kbaud MIDI wire for n bytes (10 bits per byte).
    static double wireSeconds(size_t n) { return double(n) * 10.0 / 31250.0; }

    void clearQueue() { const juce::ScopedLock sl(lock); queue.clear(); }

    // Blocking wait (worker threads only) for the first queued message accepted by pred.
    std::optional<qs::Bytes> waitFor(std::function<bool(const qs::Bytes&)> pred, int timeoutMs, juce::Thread* t = nullptr) {
        const auto deadline = juce::Time::getMillisecondCounter() + juce::uint32(timeoutMs);
        while (juce::Time::getMillisecondCounter() < deadline) {
            {
                const juce::ScopedLock sl(lock);
                for (auto it = queue.begin(); it != queue.end(); ++it)
                    if (pred(*it)) { auto m = *it; queue.erase(it); return m; }
            }
            if (t != nullptr && t->threadShouldExit()) return std::nullopt;
            arrived.wait(50);
        }
        return std::nullopt;
    }

    void addListener(Listener* l) { listeners.add(l); }
    void removeListener(Listener* l) { listeners.remove(l); }

private:
    static juce::StringArray names(const juce::Array<juce::MidiDeviceInfo>& devs) {
        juce::StringArray s;
        for (auto& d : devs) s.add(d.name);
        return s;
    }

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override {
        if (!m.isSysEx()) return;
        qs::Bytes b;
        b.reserve(size_t(m.getSysExDataSize()) + 2);
        b.push_back(0xF0);
        b.insert(b.end(), m.getSysExData(), m.getSysExData() + m.getSysExDataSize());
        b.push_back(0xF7);
        {
            const juce::ScopedLock sl(lock);
            queue.push_back(b);
            while (queue.size() > 20000) queue.pop_front();
        }
        arrived.signal();
        juce::MessageManager::callAsync([this, b] { listeners.call([&](Listener& l) { l.sysexReceived(b); }); });
    }

    std::unique_ptr<juce::MidiInput> input;
    std::unique_ptr<juce::MidiOutput> output;
    juce::CriticalSection lock, outLock;
    std::deque<qs::Bytes> queue;
    juce::WaitableEvent arrived;
    juce::ListenerList<Listener> listeners;
};
