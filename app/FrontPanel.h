// A virtual front panel for the Programs page, laid out like the connected synth:
// QS6: LCD, PROGRAM / EDIT SELECT / STORE, BANK and VALUE arrows, the 13 group buttons and the 0-9 buttons.
// QSR (and QS7/QS8): LCD, PROG / EDIT / STORE, bank DOWN/UP and the VALUE knob.
#pragma once
#include <JuceHeader.h>

#include "Theme.h"

// A hardware-style button: rounded rubber key with a printed label (one or two lines) and an LED when lit.
class PanelKey : public juce::Button {
public:
    PanelKey(juce::String top, juce::String bottom = {}) : juce::Button(top + " " + bottom), line1(std::move(top)), line2(std::move(bottom)) {}
    bool lit = false;
    void setLabel(juce::String top) {
        line1 = std::move(top);
        repaint();
    }
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        auto r = getLocalBounds().toFloat().reduced(2.0f);
        g.setColour(down ? theme::panelHi.darker(0.3f) : (over ? theme::panelHi.brighter(0.08f) : theme::panelHi));
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(lit ? theme::accent : theme::line);
        g.drawRoundedRectangle(r, 5.0f, lit ? 1.6f : 1.0f);
        if (lit) {
            g.setColour(theme::accent);
            g.fillEllipse(r.getRight() - 9.0f, r.getY() + 4.0f, 5.0f, 5.0f);
        }
        g.setColour(theme::text);
        if (line2.isEmpty()) {
            g.setFont(theme::uiFont(juce::jmin(13.0f, r.getHeight() * 0.42f), true));
            g.drawFittedText(line1, r.toNearestInt().reduced(3, 0), juce::Justification::centred, 1, 0.7f);
        } else {
            auto t = r.reduced(2.0f, 3.0f);
            g.setFont(theme::uiFont(12.5f, true));
            g.drawFittedText(line1, t.removeFromTop(t.getHeight() * 0.48f).toNearestInt(), juce::Justification::centredBottom, 1, 0.7f);
            g.setColour(theme::dim);
            g.setFont(theme::uiFont(9.5f, true));
            g.drawFittedText(line2, t.toNearestInt(), juce::Justification::centredTop, 1, 0.6f);
        }
    }

private:
    juce::String line1, line2;
};

class FrontPanel : public juce::Component {
public:
    struct Actions {
        std::function<void(int)> selectProgram;  // 0-127 in the current bank
        std::function<void(int)> stepBank;       // -1 / +1
        std::function<void()> edit, store;
        std::function<int()> currentProgram;
    };

    FrontPanel(theme::Lcd& display, Actions a) : lcd(display), act(std::move(a)) {
        addAndMakeVisible(lcd);
        static const char* groups[] = {"PIANO", "CHROMATIC", "ORGAN", "GUITAR", "BASS", "STRINGS", "BRASS",
                                       "WOODWIND", "SYNTH 1", "SYNTH 2", "SYNTH 3", "RHYTHM/FX", "DRUMS/PERC"};
        for (int i = 0; i < 13; ++i) {
            auto* k = groupKeys.add(new PanelKey(juce::String(i * 10).paddedLeft('0', 2), groups[i]));
            k->onClick = [this, i] { select(juce::jmin(127, i * 10 + current() % 10)); };
            k->setTooltip("Group " + juce::String(i * 10).paddedLeft('0', 2) + "-" + juce::String(juce::jmin(127, i * 10 + 9)) +
                          ": picks the tens digit, like the group buttons on the QS6");
            addChildComponent(k);
        }
        for (int d = 0; d < 10; ++d) {
            auto* k = digitKeys.add(new PanelKey(juce::String(d)));
            k->onClick = [this, d] {
                const int p = (current() / 10) * 10 + d;
                if (p <= 127) select(p);
            };
            addChildComponent(k);
        }
        progKey.onClick = [this] { select(current()); };
        progKey.setTooltip("Program mode: selects the shown program on the QS again");
        editKey.onClick = [this] { if (act.edit) act.edit(); };
        editKey.setTooltip("Opens this program in the Program Editor");
        storeKey.onClick = [this] { if (act.store) act.store(); };
        storeKey.setTooltip("Stores this program into a User slot on the QS");
        bankDown.onClick = [this] { if (act.stepBank) act.stepBank(-1); };
        bankUp.onClick = [this] { if (act.stepBank) act.stepBank(1); };
        valueDown.onClick = [this] { select(juce::jmax(0, current() - 1)); };
        valueUp.onClick = [this] { select(juce::jmin(127, current() + 1)); };
        for (auto* k : {&progKey, &editKey, &storeKey, &bankDown, &bankUp, &valueDown, &valueUp}) addAndMakeVisible(k);

        knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        knob.setRange(0, 127, 1);
        knob.setMouseDragSensitivity(400);
        knob.setTooltip("VALUE knob: turn to step through the programs in this bank");
        knob.onValueChange = [this] {
            if (!syncing) select(int(knob.getValue()));
        };
        addChildComponent(knob);
        setStyle(theme::currentStyle);
    }

    void setStyle(theme::Style s) {
        style = s;
        const bool qs6 = s == theme::Style::QS6;
        for (auto* k : groupKeys) k->setVisible(qs6);
        for (auto* k : digitKeys) k->setVisible(qs6);
        knob.setVisible(!qs6);
        progKey.setLabel(qs6 ? "PROGRAM" : "PROG");
        editKey.setLabel(qs6 ? "EDIT SELECT" : "EDIT");
        resized();
        repaint();
    }

    int preferredHeight() const { return style == theme::Style::QS6 ? 178 : 132; }

    // Call when the program, bank or style changes.
    void sync() {
        if (theme::currentStyle != style) setStyle(theme::currentStyle);
        const int p = current();
        for (int i = 0; i < groupKeys.size(); ++i) groupKeys[i]->lit = i == p / 10;
        for (int d = 0; d < digitKeys.size(); ++d) {
            digitKeys[d]->lit = d == p % 10;
            digitKeys[d]->setEnabled((p / 10) * 10 + d <= 127);
        }
        const juce::ScopedValueSetter<bool> svs(syncing, true);
        knob.setValue(p, juce::dontSendNotification);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(theme::bezel);
        g.fillRoundedRectangle(r, 12.0f);
        g.setColour(theme::line);
        g.drawRoundedRectangle(r.reduced(0.5f), 12.0f, 1.0f);
        g.setColour(theme::dim);
        g.setFont(theme::uiFont(11.0f, true));
        for (auto& [area, text] : captions) g.drawText(text, area, juce::Justification::centred);
        g.setColour(theme::accent);
        g.setFont(theme::uiFont(13.0f, true));
        g.drawText(style == theme::Style::QS6 ? "ALESIS  QS6" : "ALESIS  QSR", getLocalBounds().reduced(18, 8).removeFromTop(16),
                   juce::Justification::topRight);
    }

    void resized() override {
        captions.clear();
        auto r = getLocalBounds().reduced(14, 12);
        const bool qs6 = style == theme::Style::QS6;
        lcd.setBounds(r.removeFromLeft(juce::jmin(qs6 ? 400 : 460, r.getWidth() / 2)).withHeight(qs6 ? 120 : r.getHeight()));
        r.removeFromLeft(18);
        r.removeFromTop(16);  // room for the model name
        auto caption = [this](juce::Rectangle<int> a, const juce::String& t) { captions.push_back({a, t}); };
        auto top = r.removeFromTop(48);
        auto modeArea = top.removeFromLeft(3 * 96 + 8);
        caption(modeArea.removeFromTop(14), qs6 ? "MODE" : "");
        for (auto* k : {&progKey, &editKey, &storeKey}) {
            k->setBounds(modeArea.removeFromLeft(96).withTrimmedRight(4));
        }
        top.removeFromLeft(12);
        auto bank = top.removeFromLeft(110);
        caption(bank.removeFromTop(14), qs6 ? "BANK" : "BANK DOWN / UP");
        bankDown.setBounds(bank.removeFromLeft(54).withTrimmedRight(4));
        bankUp.setBounds(bank.removeFromLeft(54));
        top.removeFromLeft(12);
        if (qs6) {
            auto val = top.removeFromLeft(110);
            caption(val.removeFromTop(14), "VALUE");
            valueDown.setBounds(val.removeFromLeft(54).withTrimmedRight(4));
            valueUp.setBounds(val.removeFromLeft(54));
            r.removeFromTop(8);
            auto groupsRow = r.removeFromTop(44);
            const float gw = groupsRow.getWidth() / 13.0f;
            for (int i = 0; i < 13; ++i) groupKeys[i]->setBounds(juce::Rectangle<float>(groupsRow.getX() + i * gw, float(groupsRow.getY()), gw, 44.0f).toNearestInt().withTrimmedRight(3));
            r.removeFromTop(6);
            auto digits = r.removeFromTop(32);
            const int dw = juce::jmin(52, digits.getWidth() / 10);
            for (int d = 0; d < 10; ++d) digitKeys[d]->setBounds(digits.removeFromLeft(dw).withTrimmedRight(3));
        } else {
            valueDown.setVisible(false);
            valueUp.setVisible(false);
            auto k = top.removeFromLeft(130);
            r.removeFromTop(4);
            knob.setBounds(juce::Rectangle<int>(k.getX(), k.getY() - 10, 64, 64 + 10 + juce::jmin(0, r.getHeight() - 64)).withSizeKeepingCentre(66, 66));
            caption({knob.getX() - 20, knob.getBottom(), knob.getWidth() + 40, 14}, "VALUE");
        }
        if (qs6) {
            valueDown.setVisible(true);
            valueUp.setVisible(true);
        }
    }

private:
    int current() const { return act.currentProgram ? act.currentProgram() : 0; }
    void select(int p) {
        if (act.selectProgram) act.selectProgram(juce::jlimit(0, 127, p));
    }

    theme::Lcd& lcd;
    Actions act;
    theme::Style style = theme::Style::QSR;
    juce::OwnedArray<PanelKey> groupKeys, digitKeys;
    PanelKey progKey{"PROGRAM"}, editKey{"EDIT SELECT"}, storeKey{"STORE"}, bankDown{juce::String(juce::CharPointer_UTF8("\xe2\x97\x80"))},
        bankUp{juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6"))}, valueDown{juce::String(juce::CharPointer_UTF8("\xe2\x97\x80"))},
        valueUp{juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6"))};
    juce::Slider knob;
    bool syncing = false;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> captions;
};
