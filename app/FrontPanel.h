// The synth's front panel on the Programs page, drawn to match the hardware.
//
// QS6 (left to right, as on the keyboard): EDIT MODE keys (VALUE up/down, EDIT SELECT, STORE, PAGE left/right),
// the LCD, PLAY MODE keys (MIX, PROGRAM, BANK left/right), then the 13 group keys (00-120) and 0-9 with the
// Direct Select function labels printed above and below them (from the QS6 manual's table).
// QSR (1U rack): PHONES and VOLUME, the LCD, the VALUE knob, CURSOR / EDIT / MIDI CH / STORE,
// MIX / PROG / bank DOWN / UP, the two card slots and POWER.
//
// The layout is in fixed panel units and scaled to fit, so it keeps the hardware's proportions.
#pragma once
#include <JuceHeader.h>

#include "Hardware.h"
#include "Theme.h"

class FrontPanel : public juce::Component, private juce::Value::Listener {
public:
    struct Actions {
        std::function<void(int)> selectProgram;  // 0-127 in the current bank
        std::function<void(int)> stepBank;       // -1 / +1
        std::function<void(int)> stepChannel;    // -1 / +1
        std::function<void(bool mix)> setMode;   // MIX or PROGRAM pressed
        std::function<void()> edit, store, showCards;
        std::function<int()> currentProgram;
        std::function<void()> refreshDisplay;    // the QSR cursor moved
    };
    enum class Target { Program, Bank, Channel };  // what the QSR VALUE knob changes

    FrontPanel(theme::Lcd& display, juce::Value& volumeValue, Actions a) : lcd(display), volume(volumeValue), act(std::move(a)) {
        addAndMakeVisible(lcd);
        static const char* groups[] = {"PIANO", "CHROMATIC", "ORGAN", "GUITAR", "BASS", "STRINGS", "BRASS",
                                       "WOODWIND", "SYNTH 1", "SYNTH 2", "SYNTH 3", "RHYTHM/FX", "DRUMS/PERC"};
        for (int i = 0; i < 13; ++i) {
            auto* k = groupKeys.add(new hw::Key(juce::String(i * 10).paddedLeft('0', 2), groups[i]));
            k->onClick = [this, i] { select(juce::jmin(127, i * 10 + current() % 10)); };
            k->setTooltip("Programs " + juce::String(i * 10).paddedLeft('0', 2) + "-" + juce::String(juce::jmin(127, i * 10 + 9)).paddedLeft('0', 2) +
                          ". In the QS's edit modes this key selects " + juce::String(kProgFn[i]) + " (Program), " + kFxFn[i] + " (Effects) or " +
                          kMixFn[i] + " (Mix).");
            addChildComponent(k);
        }
        for (int d = 0; d < 10; ++d) {
            auto* k = digitKeys.add(new hw::Key(juce::String(d)));
            k->onClick = [this, d] {
                const int p = (current() / 10) * 10 + d;
                if (p <= 127) select(p);
            };
            k->setTooltip("Program number ones digit. In the QS's edit modes: " + juce::String(kSoundFn[d]) + ", Drum " + juce::String(d + 1) +
                          " or Mix Channel " + juce::String(d + 1) + ".");
            addChildComponent(k);
        }
        mixKey.onClick = [this] {
            mixMode = true;
            if (act.setMode) act.setMode(true);
            sync();
        };
        mixKey.setTooltip("Switches the QS to Mix mode. QS Studio doesn't list mixes yet; press PROGRAM (or pick a program) to go back.");
        progKey.onClick = [this] {
            mixMode = false;
            if (act.setMode) act.setMode(false);
            sync();
        };
        progKey.setTooltip("Program mode: plays the program shown");
        editKey.onClick = [this] { if (act.edit) act.edit(); };
        editKey.setTooltip("EDIT: opens this program in the Program Editor");
        storeKey.onClick = [this] { if (act.store) act.store(); };
        storeKey.setTooltip("STORE: writes this program into a User slot on the QS");
        bankDown.onClick = [this] { if (act.stepBank) act.stepBank(-1); };
        bankUp.onClick = [this] { if (act.stepBank) act.stepBank(1); };
        bankDown.setTooltip("Previous bank");
        bankUp.setTooltip("Next bank");
        valueUp.onClick = [this] { select(juce::jmin(127, current() + 1)); };
        valueDown.onClick = [this] { select(juce::jmax(0, current() - 1)); };
        valueUp.setTooltip("VALUE up: next program");
        valueDown.setTooltip("VALUE down: previous program");
        pageLeft.onClick = [this] { if (act.stepChannel) act.stepChannel(-1); };
        pageRight.onClick = [this] { if (act.stepChannel) act.stepChannel(1); };
        pageLeft.setTooltip("PAGE left. In Program Play mode these are PROGRAM CHANNEL SELECT: MIDI channel down");
        pageRight.setTooltip("PAGE right. In Program Play mode these are PROGRAM CHANNEL SELECT: MIDI channel up");
        cursorKey.onClick = [this] { setTarget(target == Target::Program ? Target::Bank : Target::Program); };
        cursorKey.setTooltip("CURSOR: moves between the program number and the bank, which the VALUE knob then changes");
        midiChKey.onClick = [this] { setTarget(target == Target::Channel ? Target::Program : Target::Channel); };
        midiChKey.setTooltip("MIDI CH: the VALUE knob changes the MIDI channel; press again to go back to programs");
        for (juce::Component* k : std::initializer_list<juce::Component*>{&mixKey, &progKey, &editKey, &storeKey, &bankDown, &bankUp, &valueUp,
                                                                         &valueDown, &pageLeft, &pageRight, &cursorKey, &midiChKey})
            addChildComponent(k);

        valueKnob.setTooltip("VALUE knob: drag up/down or use the mouse wheel. Changes the program, or the bank / MIDI channel after CURSOR or MIDI CH.");
        valueKnob.onStep = [this](int s) {
            switch (target) {
                case Target::Program: select(juce::jlimit(0, 127, current() + s)); break;
                case Target::Bank: if (act.stepBank) act.stepBank(s); break;
                case Target::Channel: if (act.stepChannel) act.stepChannel(s); break;
            }
        };
        addChildComponent(valueKnob);
        volumeKnob.setTooltip("VOLUME (sent as MIDI CC7). Double-click for full volume.");
        volumeKnob.setValue(int(volume.getValue()), false);
        volumeKnob.onChange = [this](int v) { volume = v; };
        volume.addListener(this);
        addChildComponent(volumeKnob);
        setStyle(theme::currentStyle);
    }
    ~FrontPanel() override { volume.removeListener(this); }

    Target knobTarget() const { return target; }
    bool isQs6() const { return style == theme::Style::QS6; }

    void setStyle(theme::Style s) {
        style = s;
        const bool qs6 = isQs6();
        for (auto* k : groupKeys) k->setVisible(qs6);
        for (auto* k : digitKeys) k->setVisible(qs6);
        for (juce::Component* k : std::initializer_list<juce::Component*>{&valueUp, &valueDown, &pageLeft, &pageRight}) k->setVisible(qs6);
        for (juce::Component* k : std::initializer_list<juce::Component*>{&cursorKey, &midiChKey, &valueKnob, &volumeKnob}) k->setVisible(!qs6);
        for (juce::Component* k : std::initializer_list<juce::Component*>{&mixKey, &progKey, &editKey, &storeKey, &bankDown, &bankUp}) k->setVisible(true);
        for (auto* k : {&mixKey, &progKey, &editKey, &storeKey, &bankDown, &bankUp, &cursorKey, &midiChKey}) k->small = !qs6;
        progKey.setLabel(qs6 ? "PROGRAM" : "PROG");
        editKey.setLabel(qs6 ? "EDIT SELECT" : "EDIT");
        bankDown.setLabel(qs6 ? juce::String(juce::CharPointer_UTF8("\xe2\x97\x80 BANK")) : "DOWN");
        bankUp.setLabel(qs6 ? juce::String(juce::CharPointer_UTF8("BANK \xe2\x96\xb6")) : "UP");
        if (qs6) target = Target::Program;
        resized();
        repaint();
    }

    // Height for a given width, keeping the panel's proportions.
    int heightForWidth(int w) const { return juce::roundToInt(logicalH() * juce::jmin(1.0f, float(w) / kW)); }

    // Call when the program, bank, channel or style changes.
    void sync() {
        if (theme::currentStyle != style) setStyle(theme::currentStyle);
        const int p = current();
        for (int i = 0; i < groupKeys.size(); ++i) groupKeys[i]->setLit(!mixMode && i == p / 10);
        for (int d = 0; d < digitKeys.size(); ++d) {
            digitKeys[d]->setLit(!mixMode && d == p % 10);
            digitKeys[d]->setEnabled((p / 10) * 10 + d <= 127);
        }
        mixKey.setLit(mixMode);
        progKey.setLit(!mixMode);
        cursorKey.setLit(target == Target::Bank);
        midiChKey.setLit(target == Target::Channel);
    }
    void leaveMixMode() {
        mixMode = false;
        sync();
    }
    bool inMixMode() const { return mixMode; }

    void paint(juce::Graphics& g) override {
        juce::Graphics::ScopedSaveState save(g);
        g.addTransform(transform());
        if (isQs6()) paintQs6(g);
        else paintQsr(g);
    }

    void mouseUp(const juce::MouseEvent& e) override {
        if (isQs6()) return;
        const auto p = e.position.transformedBy(transform().inverted());
        if (slotA.contains(p) || slotB.contains(p))
            if (act.showCards) act.showCards();
    }

    void resized() override {
        if (isQs6()) layoutQs6();
        else layoutQsr();
    }

private:
    static constexpr float kW = 1200.0f;
    float logicalH() const { return isQs6() ? 214.0f : 132.0f; }
    juce::AffineTransform transform() const {
        const float s = juce::jmin(1.0f, float(getWidth()) / kW);
        return juce::AffineTransform::scale(s).translated((float(getWidth()) - kW * s) / 2.0f, 0.0f);
    }
    // Place a child at panel coordinates.
    void place(juce::Component& c, float x, float y, float w, float h) {
        c.setBounds(juce::Rectangle<float>(x, y, w, h).transformedBy(transform()).toNearestInt());
    }

    static void label(juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float size, juce::Justification j = juce::Justification::centred,
                      float alpha = 1.0f) {
        g.setColour(hw::silk().withAlpha(alpha));
        g.setFont(theme::uiFont(size, true));
        g.drawFittedText(t, r.toNearestInt(), j, 1, 0.55f);
    }
    static void bracket(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t) {
        // A caption with a rule under it, spanning a group of keys (like "EDIT MODE" / "PLAY MODE").
        label(g, t, r, 9.5f);
        g.setColour(hw::silk().withAlpha(0.6f));
        g.drawHorizontalLine(int(r.getBottom() + 1), r.getX() + 4, r.getRight() - 4);
    }

    // ---------------- QS6 ----------------
    // Panel units: 1200 x 214.
    static constexpr float kEditX = 16, kKeyW = 58, kKeyH = 34, kRow1 = 52, kRow2 = 104;
    static constexpr float kLcdX = 220, kLcdW = 232;
    static constexpr float kPlayX = 470;
    static constexpr float kGridX = 618, kCellW = 40.0f, kGroupY = 82, kGroupH = 40, kDigitY = 132, kDigitH = 30;

    void layoutQs6() {
        place(valueUp, kEditX, kRow1, kKeyW, kKeyH);
        place(editKey, kEditX + 64, kRow1, kKeyW, kKeyH);
        place(storeKey, kEditX + 128, kRow1, kKeyW, kKeyH);
        place(valueDown, kEditX, kRow2, kKeyW, kKeyH);
        place(pageLeft, kEditX + 64, kRow2, kKeyW, kKeyH);
        place(pageRight, kEditX + 128, kRow2, kKeyW, kKeyH);
        place(lcd, kLcdX, 40, kLcdW, 112);
        place(mixKey, kPlayX, kRow1, kKeyW + 6, kKeyH);
        place(progKey, kPlayX + 70, kRow1, kKeyW + 6, kKeyH);
        place(bankDown, kPlayX, kRow2, kKeyW + 6, kKeyH);
        place(bankUp, kPlayX + 70, kRow2, kKeyW + 6, kKeyH);
        for (int i = 0; i < 13; ++i) place(*groupKeys[i], kGridX + i * kCellW, kGroupY, kCellW - 3, kGroupH);
        for (int d = 0; d < 10; ++d) place(*digitKeys[d], kGridX + d * kCellW, kDigitY, kCellW - 3, kDigitH);
        valueUp.setLabel(juce::String(juce::CharPointer_UTF8("\xe2\x96\xb2 VALUE")));
        valueDown.setLabel(juce::String(juce::CharPointer_UTF8("VALUE \xe2\x96\xbc")));
        pageLeft.setLabel(juce::String(juce::CharPointer_UTF8("\xe2\x97\x80 PAGE")));
        pageRight.setLabel(juce::String(juce::CharPointer_UTF8("PAGE \xe2\x96\xb6")));
    }

    void paintQs6(juce::Graphics& g) const {
        const juce::Rectangle<float> panel(0, 0, kW, logicalH());
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1d1e21), 0, 0, juce::Colour(0xff0e0f10), 0, panel.getBottom(), false));
        g.fillRoundedRectangle(panel, 10.0f);
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawRoundedRectangle(panel.reduced(0.5f), 10.0f, 1.0f);
        // ALESIS wordmark (top-left, as on the keyboard).
        g.setColour(juce::Colour(0xffd9dadc));
        g.setFont(theme::uiFont(19.0f, true).withHorizontalScale(1.15f));
        g.drawText("ALESIS", juce::Rectangle<float>(kEditX, 8, 150, 24), juce::Justification::centredLeft);

        bracket(g, {kEditX, 34, kKeyW * 3 + 12, 14}, "EDIT MODE");
        label(g, juce::String(juce::CharPointer_UTF8("\xe2\x97\x80 PROGRAM CHANNEL SELECT \xe2\x96\xb6")), {kEditX + 62, kRow2 + kKeyH + 6, kKeyW * 2 + 10, 12}, 8.5f);
        bracket(g, {kPlayX, 34, kKeyW * 2 + 18, 14}, "PLAY MODE");

        // Direct Select labels: three rows above the group keys (MIX, EFFECTS, PROGRAM) and three below 0-9 (SOUND, DRUM, MIX).
        const char* const* above[] = {kMixFn, kFxFn, kProgFn};
        const char* aboveNames[] = {"MIX", "EFFECTS", "PROGRAM"};
        for (int row = 0; row < 3; ++row) {
            const float y = 32.0f + row * 15.0f;
            for (int i = 0; i < 13; ++i) label(g, above[row][i], {kGridX + i * kCellW - 1, y, kCellW + 1, 11}, 7.6f);
            g.setColour(hw::silk().withAlpha(0.35f));
            g.drawHorizontalLine(int(y + 12.5f), kGridX, kGridX + 13 * kCellW - 3);
            label(g, juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6 ")) + aboveNames[row], {kGridX + 13 * kCellW + 2, y, kW - 6 - (kGridX + 13 * kCellW + 2), 11}, 7.6f,
                  juce::Justification::centredLeft);
        }
        const char* belowNames[] = {"KEYBOARD SOUND", "DRUM SOUND", "MIX"};
        for (int row = 0; row < 3; ++row) {
            const float y = kDigitY + kDigitH + 6 + row * 14.0f;
            for (int d = 0; d < 10; ++d) {
                const juce::String t = row == 0 ? juce::String(kSoundFn[d]) : (row == 1 ? juce::String("DRUM ") : juce::String("CHANNEL ")) + juce::String(d + 1);
                label(g, t, {kGridX + d * kCellW - 1, y, kCellW + 1, 11}, 7.6f);
            }
            g.setColour(hw::silk().withAlpha(0.35f));
            g.drawHorizontalLine(int(y - 1.5f), kGridX, kGridX + 10 * kCellW - 3);
            label(g, juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6 ")) + belowNames[row], {kGridX + 10 * kCellW + 2, y, 92, 11}, 7.6f,
                  juce::Justification::centredLeft);
        }
        // QS6 logo, outlined, bottom right.
        juce::Path logo;
        juce::GlyphArrangement ga;
        ga.addLineOfText(theme::uiFont(46.0f, true).withHorizontalScale(1.35f), "QS6", 0, 0);
        ga.createPath(logo);
        const auto lb = logo.getBounds();
        // Sits to the right of 0-9, level with them, like on the keyboard.
        const float logoW = kW - 14 - (kGridX + 10 * kCellW + 96);
        logo.applyTransform(juce::AffineTransform::scale(juce::jmin(1.0f, logoW / lb.getWidth())));
        const auto lb2 = logo.getBounds();
        logo.applyTransform(juce::AffineTransform::translation(kW - 14 - lb2.getRight(), kDigitY + kDigitH + 4 - lb2.getBottom()));
        g.setColour(juce::Colour(0xff9aa3ad));
        g.strokePath(logo, juce::PathStrokeType(1.4f));
        label(g, "64 VOICE EXPANDABLE SYNTHESIZER", {kW - 14 - logoW, kDigitY + kDigitH + 8, logoW, 10}, 6.0f, juce::Justification::centredRight, 0.7f);
    }

    // ---------------- QSR ----------------
    // Panel units: 1200 x 132, a 1U rack face with ears.
    void layoutQsr() {
        place(volumeKnob, 210, 44, 48, 48);
        place(lcd, 276, 22, 300, 88);
        place(valueKnob, 594, 32, 72, 72);
        const float bw = 72, bh = 32;
        place(cursorKey, 684, 26, bw, bh);
        place(editKey, 762, 26, bw, bh);
        place(midiChKey, 684, 72, bw, bh);
        place(storeKey, 762, 72, bw, bh);
        place(mixKey, 852, 26, bw, bh);
        place(progKey, 930, 26, bw, bh);
        place(bankDown, 852, 72, bw, bh);
        place(bankUp, 930, 72, bw, bh);
        slotA = {1018, 26, 104, 26};
        slotB = {1018, 72, 104, 26};
    }

    void paintQsr(juce::Graphics& g) const {
        const float h = logicalH();
        // Rack ears.
        for (float x : {0.0f, kW - 34.0f}) {
            juce::Rectangle<float> ear(x, 0, 34, h);
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2c2d30), ear.getX(), 0, juce::Colour(0xff1a1b1d), ear.getRight(), 0, false));
            g.fillRoundedRectangle(ear, 4.0f);
            for (float y : {h * 0.22f, h * 0.78f}) {
                g.setColour(juce::Colours::black);
                g.fillRoundedRectangle(juce::Rectangle<float>(ear.getCentreX() - 6, y - 4, 12, 8), 4.0f);
            }
        }
        juce::Rectangle<float> face(36, 0, kW - 72, h);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1e1f22), 0, 0, juce::Colour(0xff111214), 0, h, false));
        g.fillRect(face);
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        g.drawHorizontalLine(1, face.getX(), face.getRight());
        g.setColour(juce::Colours::black);
        g.drawHorizontalLine(int(h) - 1, face.getX(), face.getRight());
        // Logo block.
        g.setColour(juce::Colour(0xffd9dadc));
        g.setFont(theme::uiFont(15.0f, true).withHorizontalScale(1.15f));
        g.drawText("ALESIS", juce::Rectangle<float>(50, 18, 110, 20), juce::Justification::centredLeft);
        juce::Path logo;
        juce::GlyphArrangement ga;
        ga.addLineOfText(theme::uiFont(38.0f, true).withHorizontalScale(1.3f), "QSR", 0, 0);
        ga.createPath(logo);
        const auto lb = logo.getBounds();
        logo.applyTransform(juce::AffineTransform::translation(50 - lb.getX(), 82 - lb.getBottom()));
        g.setColour(juce::Colour(0xff9aa3ad));
        g.strokePath(logo, juce::PathStrokeType(1.3f));
        label(g, "64 VOICE EXPANDABLE SYNTH MODULE", {50, 90, 120, 10}, 6.0f, juce::Justification::centredLeft, 0.7f);
        // Phones jack.
        juce::Rectangle<float> jack(172, 57, 22, 22);
        g.setColour(juce::Colour(0xff3a3c40));
        g.fillEllipse(jack);
        g.setColour(juce::Colours::black);
        g.fillEllipse(jack.reduced(5));
        label(g, "PHONES", {160, 98, 46, 10}, 7.5f);
        label(g, "VOLUME", {207, 98, 54, 10}, 7.5f);
        label(g, "VALUE", {594, 110, 72, 10}, 8.0f);
        label(g, "BANK", {852, 110, 150, 10}, 7.5f, juce::Justification::centred, 0.8f);
        // Card slots.
        for (auto [slot, name] : {std::pair<juce::Rectangle<float>, const char*>{slotA, "CARD A"}, std::pair<juce::Rectangle<float>, const char*>{slotB, "CARD B"}}) {
            g.setColour(juce::Colour(0xff2b2c2f));
            g.fillRoundedRectangle(slot, 3.0f);
            hw::drawWell(g, slot.reduced(8, 9), 2.0f);
            label(g, name, {slot.getX(), slot.getBottom() + 1, slot.getWidth(), 10}, 7.0f, juce::Justification::centred, 0.8f);
        }
        // Power rocker.
        juce::Rectangle<float> pw(1136, 40, 22, 40);
        g.setColour(juce::Colours::black);
        g.fillRoundedRectangle(pw.expanded(3), 3.0f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a4b4f), pw.getX(), pw.getY(), juce::Colour(0xff151618), pw.getX(), pw.getBottom(), false));
        g.fillRoundedRectangle(pw, 2.0f);
        label(g, "POWER", {1122, 98, 50, 10}, 7.5f);
    }

    void valueChanged(juce::Value& v) override { volumeKnob.setValue(int(v.getValue()), false); }
    int current() const { return act.currentProgram ? act.currentProgram() : 0; }
    void select(int p) {
        mixMode = false;
        if (act.selectProgram) act.selectProgram(juce::jlimit(0, 127, p));
    }
    void setTarget(Target t) {
        target = t;
        sync();
        if (act.refreshDisplay) act.refreshDisplay();
    }

    // Direct Select functions from the QS6 manual (Chapter 4, "Selecting Functions and Parameters").
    static constexpr const char* kMixFn[13] = {"CHANNEL 11", "CHANNEL 12", "CHANNEL 13", "CHANNEL 14", "CHANNEL 15", "CHANNEL 16", "LEVEL",
                                               "PITCH", "EFFECT", "KEYBD/MIDI", "CONTROLLERS", "RANGE", "NAME"};
    static constexpr const char* kFxFn[13] = {"SEND 1", "SEND 2", "SEND 3", "SEND 4", "CONFIG", "EQ", "MOD", "LEZLIE", "PITCH", "DELAY",
                                              "REVERB", "OVERDRIVE", "MIX"};
    static constexpr const char* kProgFn[13] = {"SOUND 1", "SOUND 2", "SOUND 3", "SOUND 4", "VOICE", "LEVEL", "PITCH", "FILTER", "AMP/RANGE",
                                                "PITCH ENV", "FILTER ENV", "AMP ENV", "NAME"};
    static constexpr const char* kSoundFn[10] = {"MOD 1", "MOD 2", "MOD 3", "MOD 4", "MOD 5", "MOD 6", "PITCH LFO", "FILTER LFO", "AMP LFO", "TRACK GEN"};

    theme::Lcd& lcd;
    juce::Value& volume;
    Actions act;
    theme::Style style = theme::Style::QSR;
    Target target = Target::Program;
    bool mixMode = false;
    juce::OwnedArray<hw::Key> groupKeys, digitKeys;
    hw::Key mixKey{"MIX"}, progKey{"PROGRAM"}, editKey{"EDIT SELECT"}, storeKey{"STORE"}, bankDown{"BANK"}, bankUp{"BANK"}, valueUp{"VALUE"},
        valueDown{"VALUE"}, pageLeft{"PAGE"}, pageRight{"PAGE"}, cursorKey{"CURSOR"}, midiChKey{"MIDI CH"};
    hw::Encoder valueKnob;
    hw::Knob volumeKnob{0, 127, 127};
    juce::Rectangle<float> slotA, slotB;
};
