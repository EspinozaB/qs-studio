// Pitch wheel, mod wheel, Volume and controller sliders, drawn like the left end of the QS keyboards.
// QS6: Pitch, Mod, Volume, Controller A (which doubles as EDIT VALUE). Other models: Pitch, Mod, Volume, A-D.
// Controller numbers come from the QS Global settings (factory: A=12, B=13, C=91, D=93).
#pragma once
#include <JuceHeader.h>

#include "Hardware.h"
#include "Session.h"
#include "Theme.h"

class PerformanceControls : public juce::Component, private juce::Value::Listener {
public:
    explicit PerformanceControls(Session& s) : session(s) {
        pitch.setSpringBack(true);
        pitch.onChange = [this](int v) { session.sendPerformance(juce::MidiMessage::pitchWheel(session.channel, v + 8192)); };
        pitch.setTooltip("Pitch wheel: drag up or down; it springs back when you let go");
        mod.onChange = [this](int v) { cc(1, v); };
        mod.setTooltip("Mod wheel (CC1): drag, or use the mouse wheel");
        volume.onChange = [this](int v) { session.volume = v; };
        volume.setTooltip("Volume (MIDI CC7 on this channel). Double-click for full volume.");
        for (int i = 0; i < 4; ++i) {
            auto* c = ctrl.add(new hw::Fader(0, 127, 0));
            c->onChange = [this, i](int v) { cc(session.global.controller[i], v); };
            addAndMakeVisible(c);
        }
        for (juce::Component* c : std::initializer_list<juce::Component*>{&pitch, &mod, &volume}) addAndMakeVisible(c);
        session.volume.addListener(this);
        // Follow the MIDI controller: its wheels and sliders move the ones on screen.
        session.controllerMoved = [this](const juce::MidiMessage& m) {
            if (m.isPitchWheel()) pitch.setValue(m.getPitchWheelValue() - 8192, false);
            else if (m.isController()) {
                const int cc = m.getControllerNumber(), v = m.getControllerValue();
                if (cc == 1) mod.setValue(v, false);
                else if (cc == 7) {
                    const juce::ScopedValueSetter<bool> svs(fromController, true);
                    session.volume = v;
                }
                for (int i = 0; i < 4; ++i)
                    if (cc == session.global.controller[i]) ctrl[i]->setValue(v, false);
            }
        };
        updateLayout();
    }
    ~PerformanceControls() override {
        session.controllerMoved = nullptr;
        session.volume.removeListener(this);
    }

    // Call when the model or the Global settings change.
    void updateLayout() {
        const bool one = session.hasOneControllerSlider();
        for (int i = 0; i < 4; ++i) {
            ctrl[i]->setVisible(i == 0 || !one);
            ctrl[i]->setTooltip("Controller " + juce::String::charToString(juce::juce_wchar('A' + i)) + " sends CC" +
                                juce::String(session.global.controller[i]) + (session.globalKnown ? "" : " (factory default)") +
                                (one ? ". On the QS6 this slider is also EDIT VALUE; Controllers B-D are on the Global Settings page." : ""));
        }
        resized();
        repaint();
    }

    int preferredWidth() const { return kWheelBlock + 10 + (session.hasOneControllerSlider() ? 2 * (kFaderW + 22) : 5 * kFaderW) + 8; }

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(hw::panelBlack());
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);
        // The two wheels sit in a raised housing.
        auto housing = wheelHousing().toFloat();
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2a2b2e), housing.getX(), housing.getY(), juce::Colour(0xff111113), housing.getX(),
                                               housing.getBottom(), false));
        g.fillRoundedRectangle(housing, 6.0f);
        g.setColour(juce::Colours::black);
        g.drawRoundedRectangle(housing, 6.0f, 1.0f);
        g.setColour(juce::Colour(0xffd2d5d9));  // the strip is always black, whatever the panel colour
        g.setFont(theme::uiFont(9.5f, true));
        const bool one = session.hasOneControllerSlider();
        auto label = [&](juce::Component& c, const juce::String& t) {
            g.drawFittedText(t, juce::Rectangle<int>(c.getX() - 2, getHeight() - (one ? 24 : 18), c.getWidth() + 4, 14), juce::Justification::centred, 1, 0.6f);
        };
        label(pitch, "PITCH");
        label(mod, "MODULATION");
        label(volume, "VOLUME");
        // The QS6.2 / QS8.2 print what the sliders do by default: Filter, Delay, Envelope, Reverb.
        const bool silver = session.panelModel().endsWith(".2");
        static const char* silverNames[] = {"FILTER", "DELAY", "ENVELOPE", "REVERB"};
        for (int i = 0; i < 4; ++i)
            if (ctrl[i]->isVisible())
                label(*ctrl[i], one ? juce::String("CONTROLLER A") : silver ? juce::String(silverNames[i]) : "CTRL " + juce::String::charToString(juce::juce_wchar('A' + i)));
        if (one) {
            g.setFont(theme::uiFont(8.5f, true));
            g.setColour(juce::Colour(0xffd2d5d9).withAlpha(0.7f));
            g.drawText("EDIT VALUE", juce::Rectangle<int>(ctrl[0]->getX() - 2, getHeight() - 12, ctrl[0]->getWidth() + 4, 10), juce::Justification::centred);
        }
    }

    void resized() override {
        auto housing = wheelHousing();
        auto w = housing.reduced(8, 7);
        const int ww = (w.getWidth() - 8) / 2;
        pitch.setBounds(w.removeFromLeft(ww));
        w.removeFromLeft(8);
        mod.setBounds(w.removeFromLeft(ww));
        auto r = getLocalBounds().reduced(6, 6);
        r.removeFromLeft(housing.getWidth() + 10);
        r.removeFromBottom(session.hasOneControllerSlider() ? 22 : 16);
        const int fw = session.hasOneControllerSlider() ? kFaderW + 22 : kFaderW;  // the QS6's two sliders get room for their labels
        volume.setBounds(r.removeFromLeft(fw));
        for (auto* c : ctrl)
            if (c->isVisible()) c->setBounds(r.removeFromLeft(fw));
    }

private:
    static constexpr int kWheelBlock = 100, kFaderW = 50;
    juce::Rectangle<int> wheelHousing() const {
        return getLocalBounds().reduced(6, 6).withTrimmedBottom(session.hasOneControllerSlider() ? 20 : 14).withWidth(kWheelBlock);
    }

    void valueChanged(juce::Value& v) override {
        const int vol = int(v.getValue());
        volume.setValue(vol, false);
        if (!fromController) cc(7, vol);  // the controller's own CC7 already went to the QS
    }
    bool fromController = false;
    void cc(int number, int v) { session.sendPerformance(juce::MidiMessage::controllerEvent(session.channel, juce::jlimit(0, 127, number), v)); }

    Session& session;
    hw::Wheel pitch{-8192, 8191, 0, true}, mod{0, 127, 0, false};
    hw::Fader volume{0, 127, 127};
    juce::OwnedArray<hw::Fader> ctrl;
};
