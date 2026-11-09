// Pitch wheel, mod wheel, controller sliders and volume, like the QS front panel.
// Controller numbers come from the QS Global settings (factory: A=12, B=13, C=91, D=93).
#pragma once
#include <JuceHeader.h>

#include "Session.h"
#include "Theme.h"

class PerformanceControls : public juce::Component {
public:
    explicit PerformanceControls(Session& s) : session(s) {
        setupSlider(pitch, -8192, 8191, 0);
        pitch.onValueChange = [this] {
            session.midi.sendMidi(juce::MidiMessage::pitchWheel(session.channel, int(pitch.getValue()) + 8192));
        };
        pitch.onDragEnd = [this] { pitch.setValue(0.0, juce::sendNotificationSync); };  // springs back like the real wheel
        pitch.setDoubleClickReturnValue(true, 0.0);
        pitch.setTooltip("Pitch wheel (springs back)");

        setupSlider(mod, 0, 127, 0);
        mod.onValueChange = [this] { sendCc(1, mod); };
        mod.setTooltip("Mod wheel (CC1)");

        for (int i = 0; i < 4; ++i) {
            auto* c = ctrl.add(new juce::Slider());
            setupSlider(*c, 0, 127, 0);
            c->onValueChange = [this, i] { sendCc(session.global.controller[i], *ctrl[i]); };
        }
        setupSlider(volume, 0, 127, 127);
        volume.onValueChange = [this] { sendCc(7, volume); };
        volume.setTooltip("MIDI volume (CC7) for this channel");
        updateLayout();
    }

    // Call when the model or the Global settings change.
    void updateLayout() {
        const bool one = session.hasOneControllerSlider();
        for (int i = 0; i < 4; ++i) {
            ctrl[i]->setVisible(i == 0 || !one);
            ctrl[i]->setTooltip("Controller " + juce::String::charToString(juce::juce_wchar('A' + i)) + " (CC" +
                                juce::String(session.global.controller[i]) + ")" + (session.globalKnown ? "" : ", factory default"));
        }
        resized();
        repaint();
    }

    int preferredWidth() const { return (session.hasOneControllerSlider() ? 4 : 7) * kColW + 8; }

    void paint(juce::Graphics& g) override {
        g.setColour(theme::bg.darker(0.2f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
        auto r = getLocalBounds().reduced(4, 0);
        for (auto* s : visibleSliders()) {
            const auto b = s->getBounds();
            theme::drawCaption(g, {b.getX() - 4, getHeight() - 18, b.getWidth() + 8, 16}, captionFor(s));
        }
        juce::ignoreUnused(r);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(4, 4);
        r.removeFromBottom(16);
        for (auto* s : visibleSliders()) s->setBounds(r.removeFromLeft(kColW).reduced(6, 0));
    }

private:
    static constexpr int kColW = 46;

    void setupSlider(juce::Slider& s, double lo, double hi, double initial) {
        s.setSliderStyle(juce::Slider::LinearVertical);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setRange(lo, hi, 1.0);
        s.setValue(initial, juce::dontSendNotification);
        s.setPopupDisplayEnabled(true, false, nullptr);
        addAndMakeVisible(s);
    }

    void sendCc(int cc, juce::Slider& s) {
        session.midi.sendMidi(juce::MidiMessage::controllerEvent(session.channel, juce::jlimit(0, 127, cc), int(s.getValue())));
    }

    juce::Array<juce::Slider*> visibleSliders() {
        juce::Array<juce::Slider*> v{&pitch, &mod};
        for (auto* c : ctrl)
            if (c->isVisible()) v.add(c);
        v.add(&volume);
        return v;
    }

    juce::String captionFor(juce::Slider* s) const {
        if (s == &pitch) return "Pitch";
        if (s == &mod) return "Mod";
        if (s == &volume) return "Vol";
        for (int i = 0; i < 4; ++i)
            if (s == ctrl[i]) return juce::String::charToString(juce::juce_wchar('A' + i));
        return {};
    }

    Session& session;
    juce::Slider pitch, mod, volume;
    juce::OwnedArray<juce::Slider> ctrl;
};
