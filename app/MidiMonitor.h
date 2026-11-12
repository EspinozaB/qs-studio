// MIDI monitor for setting up a controller or e-drum pads with the QS (QS SoundEditor's Drum Kit Calibration):
// shows each note and velocity that comes in, and the softest and hardest hit per note, so pad sensitivity can be set
// until every pad reaches the velocities the QS drum sounds respond to.
#pragma once
#include <JuceHeader.h>

#include <array>

#include "Session.h"
#include "Theme.h"

class MidiMonitor : public juce::Component, private juce::Timer {
public:
    explicit MidiMonitor(Session& s) : session(s) {
        session.monitorInput = &fifoIn;
        addAndMakeVisible(resetBtn);
        resetBtn.onClick = [this] {
            for (auto& n : stats) n = {};
            recent.clear();
            repaint();
        };
        startTimerHz(30);
        setSize(720, 520);
    }
    ~MidiMonitor() override { session.monitorInput = nullptr; }

    // Lock-free hand-over from the MIDI thread.
    struct Fifo : Session::MonitorSink {
        juce::AbstractFifo fifo{512};
        std::array<juce::uint32, 512> data{};
        void push(const juce::MidiMessage& m) override {
            if (!m.isNoteOn() && !m.isChannelPressure() && !m.isAftertouch()) return;
            const auto packed = juce::uint32(m.getRawData()[0]) << 16 | juce::uint32(m.getRawData()[1]) << 8 |
                                juce::uint32(m.getRawDataSize() > 2 ? m.getRawData()[2] : 0);
            const auto scope = fifo.write(1);
            if (scope.blockSize1 > 0) data[size_t(scope.startIndex1)] = packed;
        }
    } fifoIn;

    void resized() override { resetBtn.setBounds(getWidth() - 132, 10, 120, 28); }

    void paint(juce::Graphics& g) override {
        g.fillAll(theme::bg);
        auto r = getLocalBounds().reduced(12);
        g.setColour(theme::text);
        g.setFont(theme::uiFont(15.0f, true));
        g.drawText("Hit each pad or key a few times, soft and hard.", r.removeFromTop(28), juce::Justification::centredLeft);
        g.setFont(theme::uiFont(13.0f));
        g.setColour(theme::dim);
        g.drawText("Last hit, then the softest and hardest velocity seen per note. Aim for pads that reach 120+ when hit hard.",
                   r.removeFromTop(20), juce::Justification::centredLeft);
        r.removeFromTop(8);
        // Last hit, big.
        auto big = r.removeFromTop(70);
        g.setColour(theme::panel);
        g.fillRoundedRectangle(big.toFloat(), 8.0f);
        if (!recent.empty()) {
            const auto& e = recent.front();
            g.setColour(theme::accent);
            g.setFont(theme::monoFont(30.0f, true));
            g.drawText(noteName(e.note) + "  vel " + juce::String(e.velocity), big.reduced(16, 0), juce::Justification::centredLeft);
            g.setFont(theme::uiFont(13.0f));
            g.setColour(theme::dim);
            g.drawText("note " + juce::String(e.note) + ", MIDI channel " + juce::String(e.channel), big.reduced(16, 0), juce::Justification::centredRight);
        } else {
            g.setColour(theme::dim);
            g.setFont(theme::uiFont(15.0f));
            g.drawText("Waiting for your controller (pick it under \"Controller in\", top right)...", big.reduced(16, 0), juce::Justification::centredLeft);
        }
        r.removeFromTop(10);
        // Per-note table.
        g.setFont(theme::uiFont(13.0f));
        int y = r.getY();
        for (int n = 0; n < 128 && y < r.getBottom() - 22; ++n) {
            const auto& st = stats[size_t(n)];
            if (st.hits == 0) continue;
            g.setColour(theme::text);
            g.drawText(noteName(n) + " (" + juce::String(n) + ")", r.getX(), y, 110, 20, juce::Justification::centredLeft);
            const int bx = r.getX() + 120, bw = r.getWidth() - 260;
            g.setColour(theme::panelHi);
            g.fillRect(bx, y + 4, bw, 12);
            g.setColour(theme::accent.withAlpha(0.8f));
            g.fillRect(bx + bw * st.lo / 127, y + 4, juce::jmax(2, bw * (st.hi - st.lo) / 127), 12);
            g.setColour(theme::dim);
            g.drawText(juce::String(st.lo) + " - " + juce::String(st.hi) + "   (" + juce::String(st.hits) + " hits)", bx + bw + 10, y, 130, 20,
                       juce::Justification::centredLeft);
            y += 22;
        }
    }

private:
    void timerCallback() override {
        bool any = false;
        const auto scope = fifoIn.fifo.read(fifoIn.fifo.getNumReady());
        auto take = [&](int start, int size) {
            for (int i = 0; i < size; ++i) {
                const auto v = fifoIn.data[size_t(start + i)];
                const int status = int(v >> 16), d1 = int((v >> 8) & 0x7F), d2 = int(v & 0x7F);
                if ((status & 0xF0) != 0x90 || d2 == 0) continue;
                recent.insert(recent.begin(), {d1, d2, (status & 0x0F) + 1});
                if (recent.size() > 32) recent.pop_back();
                auto& st = stats[size_t(d1)];
                st.lo = st.hits == 0 ? d2 : juce::jmin(st.lo, d2);
                st.hi = juce::jmax(st.hi, d2);
                ++st.hits;
                any = true;
            }
        };
        take(scope.startIndex1, scope.blockSize1);
        take(scope.startIndex2, scope.blockSize2);
        if (any) repaint();
    }

    static juce::String noteName(int n) { return juce::MidiMessage::getMidiNoteName(n, true, true, 3); }

    struct Event {
        int note, velocity, channel;
    };
    struct Stat {
        int lo = 0, hi = 0, hits = 0;
    };
    Session& session;
    juce::TextButton resetBtn{"Start over"};
    std::vector<Event> recent;
    std::array<Stat, 128> stats{};
};
