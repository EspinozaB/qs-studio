// The synth's front panel on the Programs page, drawn to match the hardware (from photos of a QS6 and a QSR).
//
// QS6, left to right as on the keyboard: EDIT MODE keys (VALUE up/down, EDIT SELECT, STORE, PAGE = PROGRAM CHANNEL SELECT),
// the 2 x 16 character LCD in its labelled bezel, PLAY MODE keys (MIX, PROGRAM, BANK with COMPARE / GLOBAL under them),
// then the 13 group keys and 0-9 with the Direct Select labels above and below (from the QS6 manual's table).
// QSR (1U rack): PHONES and VOLUME, the LCD (big number, two text lines, MIX / EDIT / channel 1-16 indicators),
// the VALUE knob, EDIT MODE keys (CURSOR, EDIT, MIDI CH, STORE), PLAY MODE keys (MIX, PROG, DOWN/COMPARE, UP/GLOBAL),
// the card slots and POWER.
//
// The model name is plain text: no maker's logo or stylised model logo (QS Studio isn't an Alesis product).
// The layout is in fixed panel units and scaled to fit, so it keeps the hardware's proportions.
#pragma once
#include <JuceHeader.h>

#include "Hardware.h"
#include "Theme.h"

class FrontPanel : public juce::Component, private juce::Value::Listener, private juce::Timer {
public:
    struct Actions {
        std::function<void(int)> selectProgram;  // 0-127 in the current bank (0-99 in Mix mode)
        std::function<void(int)> stepBank;       // -1 / +1
        std::function<void(int)> stepChannel;    // -1 / +1
        std::function<void(bool mix)> setMode;   // MIX or PROGRAM pressed
        std::function<void()> edit, store, showCards;
        std::function<int()> currentProgram;     // current program, or mix in Mix mode
        std::function<int()> highestNumber;      // 127 for programs, 99 for mixes
        std::function<void()> refreshDisplay;    // the QSR cursor moved
    };
    enum class Target { Program, Bank };  // which field the QSR VALUE knob changes (moved with CURSOR)

    // What the LCD shows.
    struct Display {
        bool mix = false;
        juce::String bank = "USER";      // as the QS6 prints it: USER, PRESET1-3, GenMIDI, CARD1...
        juce::String bankQsr = "User";   // as the QSR prints it: User, Preset1-3, GenMIDI, Card1...
        int number = 0;
        juce::String name;
        int channel = 1;
    };

    FrontPanel(juce::Value& volumeValue, Actions a) : volume(volumeValue), act(std::move(a)) {
        static const char* groups[] = {"PIANO", "CHROMATIC", "ORGAN", "GUITAR", "BASS", "STRINGS", "BRASS",
                                       "WOODWIND", "SYNTH 1", "SYNTH 2", "SYNTH 3", "RHYTHM/FX", "DRUMS/PERC"};
        for (int i = 0; i < 13; ++i) {
            auto* k = groupKeys.add(new hw::Key(juce::String(i * 10).paddedLeft('0', 2), groups[i]));
            k->onClick = [this, i] { select(juce::jmin(highest(), i * 10 + current() % 10)); };
            k->setTooltip("Programs " + juce::String(i * 10).paddedLeft('0', 2) + "-" + juce::String(juce::jmin(127, i * 10 + 9)).paddedLeft('0', 2) +
                          ". In the QS's edit modes this key selects " + juce::String(kProgFn[i]) + " (Program), " + kFxFn[i] + " (Effects) or " +
                          kMixFn[i] + " (Mix).");
            addChildComponent(k);
        }
        for (int d = 0; d < 10; ++d) {
            auto* k = digitKeys.add(new hw::Key(juce::String(d)));
            k->onClick = [this, d] {
                const int p = (current() / 10) * 10 + d;
                if (p <= highest()) select(p);
            };
            k->setTooltip("Program number ones digit. In the QS's edit modes: " + juce::String(kSoundFn[d]) + ", Drum " + juce::String(d + 1) +
                          " or Mix Channel " + juce::String(d + 1) + ".");
            addChildComponent(k);
        }
        mixKey.onClick = [this] { if (act.setMode) act.setMode(true); };
        mixKey.setTooltip("MIX: lists the mixes of this bank and switches the QS to Mix mode");
        progKey.onClick = [this] { if (act.setMode) act.setMode(false); };
        progKey.setTooltip("PROGRAM: lists the programs of this bank and switches the QS to Program mode");
        editKey.onClick = [this] { if (act.edit) act.edit(); };
        editKey.setTooltip("EDIT: opens this program in the Program Editor");
        storeKey.onClick = [this] { if (act.store) act.store(); };
        storeKey.setTooltip("STORE: writes this program into a User slot on the QS");
        bankDown.onClick = [this] { if (act.stepBank) act.stepBank(-1); };
        bankUp.onClick = [this] { if (act.stepBank) act.stepBank(1); };
        bankDown.setTooltip("Previous bank (COMPARE in edit mode)");
        bankUp.setTooltip("Next bank (GLOBAL: EDIT then this key opens Global edit on the QS)");
        valueUp.onClick = [this] { select(current() + 1); };
        valueDown.onClick = [this] { select(current() - 1); };
        valueUp.setTooltip("VALUE up: next program");
        valueDown.setTooltip("VALUE down: previous program");
        pageLeft.onClick = [this] { if (act.stepChannel) act.stepChannel(-1); };
        pageRight.onClick = [this] { if (act.stepChannel) act.stepChannel(1); };
        pageLeft.setTooltip("PAGE left. In Program Play mode these are PROGRAM CHANNEL SELECT: MIDI channel down");
        pageRight.setTooltip("PAGE right. In Program Play mode these are PROGRAM CHANNEL SELECT: MIDI channel up");
        cursorKey.split = midiChKey.split = true;
        cursorKey.onClick = [this] {
            target = cursorKey.side() < 0 ? Target::Bank : Target::Program;  // the bank is left of the number on the display
            sync();
            if (act.refreshDisplay) act.refreshDisplay();
        };
        cursorKey.setTooltip("CURSOR: left moves the cursor to the bank, right to the program number. The VALUE knob changes that field.");
        midiChKey.onClick = [this] { if (act.stepChannel) act.stepChannel(midiChKey.side()); };
        midiChKey.setTooltip("MIDI CH: left / right changes the MIDI channel");
        seqKey.setEnabled(false);
        transposeKey.setEnabled(false);
        seqKey.setTooltip("SEQ SELECT (QS6.2 / QS8.2): not available in QS Studio yet");
        transposeKey.setTooltip("TRANSPOSE (QS6.2 / QS8.2): not available in QS Studio yet");
        addChildComponent(seqKey);
        addChildComponent(transposeKey);
        for (juce::Component* k : std::initializer_list<juce::Component*>{&mixKey, &progKey, &editKey, &storeKey, &bankDown, &bankUp, &valueUp,
                                                                         &valueDown, &pageLeft, &pageRight, &cursorKey, &midiChKey})
            addChildComponent(k);

        valueKnob.setTooltip("VALUE knob: drag up/down or use the mouse wheel. Changes the program, or the bank after CURSOR left.");
        valueKnob.onStep = [this](int s) {
            if (target == Target::Bank) {
                if (act.stepBank) act.stepBank(s);
            } else {
                select(current() + s);
            }
        };
        addChildComponent(valueKnob);
        volumeKnob.setTooltip("VOLUME (sent as MIDI CC7). Double-click for full volume.");
        volumeKnob.setValue(int(volume.getValue()), false);
        volumeKnob.onChange = [this](int v) { volume = v; };
        volume.addListener(this);
        addChildComponent(volumeKnob);
        setModel("QSR");
        startTimer(530);  // cursor blink on the QSR display
    }
    ~FrontPanel() override { volume.removeListener(this); }

    Target knobTarget() const { return target; }
    enum class Layout { Classic, Silver, Rack };  // QS6/6.1/7/8 keyboards, QS6.2/8.2, QSR
    bool isRack() const { return layout == Layout::Rack; }

    void setDisplay(const Display& d) {
        display = d;
        repaint();
    }

    void setModel(const juce::String& m) {
        if (m == model && built) return;
        built = true;
        model = m;
        layout = m == "QSR" ? Layout::Rack : m.endsWith(".2") ? Layout::Silver : Layout::Classic;
        const bool rack = layout == Layout::Rack, silver = layout == Layout::Silver, classic = layout == Layout::Classic;
        for (auto* k : groupKeys) k->setVisible(!rack);
        for (auto* k : digitKeys) k->setVisible(!rack);
        for (juce::Component* k : std::initializer_list<juce::Component*>{&valueUp, &valueDown, &pageLeft, &pageRight}) k->setVisible(!rack);
        for (juce::Component* k : std::initializer_list<juce::Component*>{&cursorKey, &midiChKey, &valueKnob, &volumeKnob}) k->setVisible(rack);
        for (juce::Component* k : std::initializer_list<juce::Component*>{&seqKey, &transposeKey}) k->setVisible(silver);
        for (juce::Component* k : std::initializer_list<juce::Component*>{&mixKey, &progKey, &editKey, &storeKey, &bankDown, &bankUp}) k->setVisible(true);
        for (auto* k : allKeys()) {
            k->small = rack;
            k->pill = silver;
            k->legend = silver ? juce::Colour(0xfff2f4f8) : juce::Colour();  // black keys with white print on the silver panel
        }
        // The black keyboards print the EDIT MODE legends in blue.
        if (classic)
            for (auto* k : {&valueUp, &valueDown, &pageLeft, &pageRight, &editKey, &storeKey}) k->legend = hw::qs6Blue();
        progKey.setLabel(rack ? "PROG" : "PROGRAM");
        editKey.setLabel(classic ? "EDIT SELECT" : "EDIT");
        bankDown.setLabel(rack ? juce::String("DOWN") : juce::String(juce::CharPointer_UTF8("\xe2\x97\x80 BANK")), rack ? "COMPARE" : "");
        bankUp.setLabel(rack ? juce::String("UP") : juce::String(juce::CharPointer_UTF8("BANK \xe2\x96\xb6")), rack ? "GLOBAL" : "");
        valueUp.setLabel(juce::String(juce::CharPointer_UTF8("\xe2\x96\xb2 VALUE")));
        valueDown.setLabel(juce::String(juce::CharPointer_UTF8("VALUE \xe2\x96\xbc")));
        pageLeft.setLabel(juce::String(juce::CharPointer_UTF8("\xe2\x97\x80 PAGE")));
        pageRight.setLabel(juce::String(juce::CharPointer_UTF8("PAGE \xe2\x96\xb6")));
        cursorKey.setLabel(juce::String(juce::CharPointer_UTF8("\xe2\x97\x80 CURSOR \xe2\x96\xb6")));
        midiChKey.setLabel(juce::String(juce::CharPointer_UTF8("\xe2\x97\x80 MIDI CH \xe2\x96\xb6")));
        if (!rack) target = Target::Program;
        resized();
        repaint();
    }
    const juce::String& getModel() const { return model; }

    // Height for a given width, keeping the panel's proportions.
    int heightForWidth(int w) const { return juce::roundToInt(logicalH() * juce::jmin(1.0f, float(w) / kW)); }

    // Call when the program, bank, channel or style changes.
    void sync() {
        const int p = current();
        for (int i = 0; i < groupKeys.size(); ++i) {
            groupKeys[i]->setLit(i == p / 10);
            groupKeys[i]->setEnabled(i * 10 <= highest());
        }
        for (int d = 0; d < digitKeys.size(); ++d) {
            digitKeys[d]->setLit(d == p % 10);
            digitKeys[d]->setEnabled((p / 10) * 10 + d <= highest());
        }
        mixKey.setLit(mixMode);
        progKey.setLit(!mixMode);
        cursorKey.setLit(target == Target::Bank);
        repaint();
    }
    void setMixMode(bool m) {
        mixMode = m;
        sync();
    }
    bool inMixMode() const { return mixMode; }

    void paint(juce::Graphics& g) override {
        juce::Graphics::ScopedSaveState save(g);
        g.addTransform(transform());
        if (layout == Layout::Classic) paintQs6(g);
        else if (layout == Layout::Silver) paintSilver(g);
        else paintQsr(g);
    }

    void mouseUp(const juce::MouseEvent& e) override {
        if (!isRack()) return;
        const auto p = e.position.transformedBy(transform().inverted());
        if (slots.contains(p))
            if (act.showCards) act.showCards();
    }

    void resized() override {
        if (layout == Layout::Classic) layoutQs6();
        else if (layout == Layout::Silver) layoutSilver();
        else layoutQsr();
    }

private:
    static constexpr float kW = 1200.0f;
    float logicalH() const { return layout == Layout::Classic ? 214.0f : layout == Layout::Silver ? 226.0f : 128.0f; }
    juce::AffineTransform transform() const {
        const float s = juce::jmin(1.0f, float(getWidth()) / kW);
        return juce::AffineTransform::scale(s).translated((float(getWidth()) - kW * s) / 2.0f, 0.0f);
    }
    void place(juce::Component& c, float x, float y, float w, float h) {
        c.setBounds(juce::Rectangle<float>(x, y, w, h).transformedBy(transform()).toNearestInt());
    }
    void timerCallback() override {
        if (layout != Layout::Classic) {
            blink = !blink;
            repaint(lcdArea.transformedBy(transform()).getSmallestIntegerContainer());
        }
    }

    static void label(juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float size, juce::Justification j = juce::Justification::centred,
                      float alpha = 1.0f) {
        g.setColour(hw::silk().withAlpha(alpha));
        g.setFont(theme::uiFont(size, true));
        g.drawFittedText(t, r.toNearestInt(), j, 1, 0.55f);
    }
    // Tiny printed function labels: squeezed to fit their key's width, never into the next one.
    static void small(juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r) {
        g.setColour(hw::silk());
        g.setFont(theme::uiFont(6.8f, true));
        g.drawFittedText(t, r.toNearestInt(), juce::Justification::centred, 1, 0.3f);
    }
    // A caption with a rule beside it, like the "EDIT MODE" and "PLAY MODE" brackets on the panels.
    static void bracket(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, bool below = false, float size = 8.5f) {
        g.setFont(theme::uiFont(size, true));
        const float tw = float(juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), t)) + 10.0f;
        g.setColour(hw::silk());
        g.drawText(t, r, juce::Justification::centred);
        g.setColour(hw::silk().withAlpha(0.75f));
        const float y = r.getCentreY(), tick = below ? -4.0f : 4.0f;
        const float l1 = r.getX(), l2 = r.getCentreX() - tw / 2, r1 = r.getCentreX() + tw / 2, r2 = r.getRight();
        g.drawLine(l1, y, l2, y, 1.0f);
        g.drawLine(r1, y, r2, y, 1.0f);
        g.drawLine(l1, y, l1, y + tick, 1.0f);
        g.drawLine(r2, y, r2, y + tick, 1.0f);
    }
    // The model name, set big and bold in a metallic finish so it reads like a nameplate. It's our own lettering
    // (a plain typeface), not the maker's logo.
    static void modelName(juce::Graphics& g, const juce::String& name, juce::Rectangle<float> r, juce::Colour top, juce::Colour bottom,
                          juce::Justification j = juce::Justification::centredLeft) {
        juce::GlyphArrangement ga;
        const auto font = theme::uiFont(r.getHeight(), true).withHorizontalScale(1.25f).withExtraKerningFactor(0.06f);
        ga.addFittedText(font, name, r.getX(), r.getY(), r.getWidth(), r.getHeight(), j, 1, 0.8f);
        juce::Path p;
        ga.createPath(p);
        // Fit the letters themselves (not the font's line box) inside r, so the nameplate is the same size on every
        // platform's fonts and never spills onto the printed labels around it.
        if (!p.isEmpty()) p.applyTransform(p.getTransformToScaleToFit(r, true, j));
        const auto b = p.getBounds();
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.fillPath(p, juce::AffineTransform::translation(1.5f, 2.0f));
        g.setGradientFill(juce::ColourGradient(top, 0, b.getY(), bottom, 0, b.getBottom(), false));
        g.fillPath(p);
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.strokePath(p, juce::PathStrokeType(0.7f));
    }

    // ---------------- LCDs ----------------
    // QS6: a 2 x 16 character display, yellow-green backlight.
    void drawQs6Lcd(juce::Graphics& g, juce::Rectangle<float> glass) const {
        g.setColour(juce::Colour(0xffa7c43c));
        g.fillRoundedRectangle(glass, 2.0f);
        g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.15f), glass.getX(), glass.getY(), juce::Colours::transparentBlack,
                                               glass.getX(), glass.getBottom(), false));
        g.fillRoundedRectangle(glass, 2.0f);
        juce::String mode = display.mix ? "MIX " : "PROG";
        const juce::String num = display.mix || display.number < 100 ? juce::String(display.number).paddedLeft('0', 2) : juce::String(display.number);
        juce::String line1 = mode + " " + display.bank.substring(0, 7).paddedRight(' ', 7) + " " + num;
        juce::String name = display.name.isNotEmpty() ? display.name.substring(0, 10) : juce::String("----------");
        juce::String line2 = name.paddedRight(' ', 10) + (display.mix ? juce::String() : " Ch" + juce::String(display.channel).paddedLeft('0', 2));
        drawCharRows(g, glass.reduced(8.0f, 9.0f), {line1, line2}, 16, juce::Colour(0xff192408), juce::Colour(0xff9bb935));
    }

    // Character cells: every cell shows faintly (like an unlit dot-matrix LCD), text on top in a mono font.
    static void drawCharRows(juce::Graphics& g, juce::Rectangle<float> area, std::initializer_list<juce::String> rows, int cols, juce::Colour ink,
                             juce::Colour cell, int cursorRow = -1, int cursorCol = -1, int cursorLen = 0) {
        const int nRows = int(rows.size());
        const float cw = area.getWidth() / float(cols), rh = area.getHeight() / float(nRows);
        g.setFont(theme::monoFont(juce::jmin(rh * 0.78f, cw * 1.55f)));  // fit the cell whichever way is tighter
        int row = 0;
        for (const auto& text : rows) {
            for (int c = 0; c < cols; ++c) {
                juce::Rectangle<float> cr(area.getX() + c * cw, area.getY() + row * rh, cw, rh);
                g.setColour(cell);
                g.fillRect(cr.reduced(cw * 0.08f, rh * 0.1f));
                g.setColour(ink);
                if (c < text.length()) g.drawText(juce::String::charToString(text[c]), cr, juce::Justification::centred);
                if (row == cursorRow && c >= cursorCol && c < cursorCol + cursorLen)
                    g.fillRect(cr.getX() + cw * 0.08f, cr.getBottom() - rh * 0.1f, cw * 0.84f, juce::jmax(1.5f, rh * 0.07f));
            }
            ++row;
        }
    }

    // A 7-segment digit (the QSR shows the program/mix number this way).
    static void drawSevenSeg(juce::Graphics& g, juce::Rectangle<float> r, int digit, juce::Colour on, juce::Colour off) {
        static const uint8_t segs[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
        const float w = r.getWidth(), h = r.getHeight(), t = w * 0.17f;
        const juce::Rectangle<float> rects[7] = {
            {r.getX() + t, r.getY(), w - 2 * t, t},                         // a
            {r.getRight() - t, r.getY() + t * 0.6f, t, h / 2 - t},         // b
            {r.getRight() - t, r.getCentreY() + t * 0.4f, t, h / 2 - t},   // c
            {r.getX() + t, r.getBottom() - t, w - 2 * t, t},                // d
            {r.getX(), r.getCentreY() + t * 0.4f, t, h / 2 - t},            // e
            {r.getX(), r.getY() + t * 0.6f, t, h / 2 - t},                  // f
            {r.getX() + t, r.getCentreY() - t / 2, w - 2 * t, t}};          // g
        const uint8_t mask = digit >= 0 && digit <= 9 ? segs[digit] : 0;
        for (int i = 0; i < 7; ++i) {
            g.setColour((mask >> i) & 1 ? on : off);
            g.fillRoundedRectangle(rects[i], t * 0.3f);
        }
    }

    // QSR: big number on the left, two 16-character text lines, and MIX / EDIT / channel 1-16 indicators along the bottom
    // (as drawn in the QSR supplement). The cursor (CURSOR key) underlines the field the VALUE knob changes.
    void drawQsrLcd(juce::Graphics& g, juce::Rectangle<float> glass, bool silver = false) const {
        const auto bg = silver ? juce::Colour(0xffa6d846) : juce::Colour(0xffc2cf72), ink = juce::Colour(0xff1d2610),
                   faint = silver ? juce::Colour(0xff9bcc3f) : juce::Colour(0xffb5c467);
        g.setColour(bg);
        g.fillRoundedRectangle(glass, 2.0f);
        g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.15f), glass.getX(), glass.getY(), juce::Colours::transparentBlack,
                                               glass.getX(), glass.getBottom(), false));
        g.fillRoundedRectangle(glass, 2.0f);
        auto r = glass.reduced(7.0f, 5.0f);
        auto bottom = r.removeFromBottom(10.0f);
        // Big number.
        auto digitsArea = r.removeFromLeft(r.getHeight() * (silver ? 0.95f : 1.25f));
        // QSR: zero-padded (000 / 00). QS6.2 / QS8.2: no leading zeros (the photo shows "8").
        const juce::String num = silver ? juce::String(display.number) : juce::String(display.number).paddedLeft('0', display.mix ? 2 : 3);
        const int digits = num.length();
        const float dw = digitsArea.getWidth() / 3.0f;
        for (int i = 0; i < 3; ++i) {
            const int idx = i - (3 - digits);
            const int dgt = idx >= 0 ? num[idx] - '0' : -1;
            drawSevenSeg(g, juce::Rectangle<float>(digitsArea.getX() + i * dw, digitsArea.getY() + 2, dw * 0.8f, digitsArea.getHeight() - 6), dgt, ink, faint);
        }
        if (target == Target::Program && blink) {
            g.setColour(ink);
            g.fillRect(digitsArea.getX(), digitsArea.getBottom() - 2.0f, digitsArea.getWidth() * 0.92f, 1.6f);
        }
        r.removeFromLeft(6.0f);
        const juce::String nm = display.name.isNotEmpty() ? display.name.substring(0, 10) : juce::String("----------");
        const juce::String name = silver ? nm : "\"" + nm + "\"";
        const juce::String bankText = (silver ? display.bank : display.bankQsr).substring(0, 11);
        const juce::String line2 = silver ? "BANK: " + bankText : "Bank:" + bankText.paddedLeft(' ', 11);
        drawCharRows(g, r, {name, line2}, 16, ink, faint, target == Target::Bank && blink ? 1 : -1, silver ? 6 : 16 - bankText.length(),
                     bankText.length());
        // Indicators: MIX, EDIT, then channels 1-16 (the current one shown).
        g.setFont(theme::uiFont(7.0f, true));
        auto ind = bottom;
        auto annun = [&](const juce::String& t, float w, bool on) {
            g.setColour(on ? ink : faint.darker(0.06f));
            g.drawText(t, ind.removeFromLeft(w), juce::Justification::centred);
        };
        annun("MIX", 22, display.mix);
        annun("EDIT", 24, false);
        const float cw = ind.getWidth() / 16.0f;
        for (int ch = 1; ch <= 16; ++ch) annun(juce::String(ch), cw, ch == display.channel);
    }

    // ---------------- QS6 ----------------
    // Panel units: 1200 x 214.
    static constexpr float kEditX = 16, kKeyW = 58, kKeyH = 34, kRow1 = 52, kRow2 = 104;
    static constexpr float kLcdX = 218, kLcdW = 236;
    static constexpr float kPlayX = 472;
    static constexpr float kGridX = 618, kCellW = 40.0f, kGroupY = 82, kGroupH = 40, kDigitY = 132, kDigitH = 30;

    void layoutQs6() {
        place(valueUp, kEditX, kRow1, kKeyW, kKeyH);
        place(editKey, kEditX + 64, kRow1, kKeyW, kKeyH);
        place(storeKey, kEditX + 128, kRow1, kKeyW, kKeyH);
        place(valueDown, kEditX, kRow2, kKeyW, kKeyH);
        place(pageLeft, kEditX + 64, kRow2, kKeyW, kKeyH);
        place(pageRight, kEditX + 128, kRow2, kKeyW, kKeyH);
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
        lcdArea = {kLcdX, 30, kLcdW, 150};
    }

    void paintQs6(juce::Graphics& g) const {
        const juce::Rectangle<float> panel(0, 0, kW, logicalH());
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1b1c1f), 0, 0, juce::Colour(0xff0d0e0f), 0, panel.getBottom(), false));
        g.fillRoundedRectangle(panel, 10.0f);
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawRoundedRectangle(panel.reduced(0.5f), 10.0f, 1.0f);

        bracket(g, {kEditX, 34, kKeyW * 3 + 12, 12}, "EDIT MODE");
        bracket(g, {kEditX + 62, kRow2 + kKeyH + 8, kKeyW * 2 + 10, 12}, "PROGRAM CHANNEL SELECT", true, 7.5f);
        bracket(g, {kPlayX, 34, kKeyW * 2 + 18, 12}, "PLAY MODE");
        label(g, "COMPARE", {kPlayX, kRow2 + kKeyH + 4, kKeyW + 6, 10}, 7.0f, juce::Justification::centred, 0.8f);
        label(g, "GLOBAL", {kPlayX + 70, kRow2 + kKeyH + 4, kKeyW + 6, 10}, 7.0f, juce::Justification::centred, 0.8f);

        // LCD bezel with its printed labels (QS6 manual, "About the Display").
        auto bez = lcdArea;
        g.setColour(juce::Colour(0xff08090a));
        g.fillRoundedRectangle(bez, 6.0f);
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawRoundedRectangle(bez.reduced(0.5f), 6.0f, 1.0f);
        auto inner = bez.reduced(12.0f, 0.0f);
        auto lab = [&](juce::Rectangle<float> r, const juce::String& t) { label(g, t, r, 6.6f, juce::Justification::centred, 0.75f); };
        auto rule = [&](float x1, float x2, float y) {
            g.setColour(hw::silk().withAlpha(0.4f));
            g.drawHorizontalLine(int(y), x1, x2);
        };
        lab({inner.getX() + 20, bez.getY() + 6, 120, 9}, "EDIT MODE");
        rule(inner.getX() + 10, inner.getX() + 150, bez.getY() + 16);
        lab({inner.getRight() - 46, bez.getY() + 6, 40, 9}, "PAGE");
        rule(inner.getRight() - 46, inner.getRight() - 6, bez.getY() + 16);
        lab({inner.getX(), bez.getY() + 20, 40, 9}, "MODE");
        lab({inner.getX() + 62, bez.getY() + 20, 70, 9}, "BANK");
        lab({inner.getRight() - 58, bez.getY() + 20, 52, 9}, "NUMBER");
        rule(inner.getX(), inner.getRight(), bez.getY() + 30);
        auto glass = juce::Rectangle<float>(inner.getX(), bez.getY() + 34, inner.getWidth(), 60);
        drawQs6Lcd(g, glass);
        rule(inner.getX(), inner.getRight(), glass.getBottom() + 4);
        lab({inner.getX() + 10, glass.getBottom() + 8, 70, 9}, "NAME");
        // Channel grid 1-16 (the Mix mode channel indicators sit above these numbers).
        g.setFont(theme::uiFont(6.0f, true));
        const float gx = inner.getRight() - 112, cw = 13.5f;
        for (int ch = 1; ch <= 16; ++ch) {
            const bool on = display.mix ? false : ch == display.channel;
            g.setColour(on ? hw::silk() : hw::silk().withAlpha(0.55f));
            g.drawText(juce::String(ch), juce::Rectangle<float>(gx + ((ch - 1) % 8) * cw, glass.getBottom() + 8 + ((ch - 1) / 8) * 9, cw, 9),
                       juce::Justification::centred);
        }
        rule(inner.getX(), inner.getRight(), glass.getBottom() + 28);
        lab({inner.getX(), glass.getBottom() + 31, inner.getWidth(), 9}, "PARAMETER");

        // Direct Select labels: three rows above the group keys (MIX, EFFECTS, PROGRAM) and three below 0-9 (SOUND, DRUM, MIX).
        const char* const* above[] = {kMixFn, kFxFn, kProgFn};
        const char* aboveNames[] = {"MIX", "EFFECTS", "PROGRAM"};
        for (int row = 0; row < 3; ++row) {
            const float y = 32.0f + row * 15.0f;
            for (int i = 0; i < 13; ++i) small(g, above[row][i], {kGridX + i * kCellW + 1, y, kCellW - 2, 11});
            g.setColour(hw::silk().withAlpha(0.35f));
            g.drawHorizontalLine(int(y + 12.5f), kGridX, kGridX + 13 * kCellW - 3);
            label(g, juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6 ")) + aboveNames[row], {kGridX + 13 * kCellW + 2, y, kW - 6 - (kGridX + 13 * kCellW + 2), 11},
                  7.4f, juce::Justification::centredLeft);
        }
        const char* belowNames[] = {"KEYBOARD SOUND", "DRUM SOUND", "MIX"};
        for (int row = 0; row < 3; ++row) {
            const float y = kDigitY + kDigitH + 6 + row * 14.0f;
            for (int d = 0; d < 10; ++d) {
                const juce::String t = row == 0 ? juce::String(kSoundFn[d]) : (row == 1 ? juce::String("DRUM ") : juce::String("CHANNEL ")) + juce::String(d + 1);
                small(g, t, {kGridX + d * kCellW + 1, y, kCellW - 2, 11});
            }
            g.setColour(hw::silk().withAlpha(0.35f));
            g.drawHorizontalLine(int(y - 1.5f), kGridX, kGridX + 10 * kCellW - 3);
            label(g, juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6 ")) + belowNames[row], {kGridX + 10 * kCellW + 2, y, 92, 11}, 7.4f,
                  juce::Justification::centredLeft);
        }
        // Model name in plain lettering (not the logo): right of 0-9, under the last group keys, clear of the label rows.
        modelName(g, model, {kGridX + 10 * kCellW + 16, kDigitY + 1, kW - 10 - (kGridX + 10 * kCellW + 16), 26}, juce::Colour(0xffd9ecf8),
                  juce::Colour(0xff6f93ae), juce::Justification::centredRight);

    }

    // ---------------- QS6.2 / QS8.2 ----------------
    // Panel units: 1200 x 206, from a photo of a QS6.2: silver panel, blue key blocks either side of the LCD,
    // rounded black keys.
    static constexpr float sGridX = 612, sCell = 40.0f, sGroupY = 70, sDigitY = 110, sKeyH = 26;

    void layoutSilver() {
        place(valueUp, 72, 40, 52, 24);
        place(pageLeft, 26, 66, 52, 24);
        place(pageRight, 118, 66, 52, 24);
        place(valueDown, 72, 92, 52, 24);
        place(editKey, 176, 42, 44, 26);
        place(storeKey, 176, 92, 44, 26);
        place(mixKey, 508, 40, 52, 24);
        place(bankDown, 470, 66, 56, 24);
        place(bankUp, 540, 66, 56, 24);
        place(progKey, 500, 100, 68, 26);
        for (int i = 0; i < 13; ++i) place(*groupKeys[i], sGridX + i * sCell, sGroupY, sCell - 4, sKeyH + 4);
        for (int d = 0; d < 10; ++d) place(*digitKeys[d], sGridX + d * sCell, sDigitY, sCell - 4, sKeyH);
        place(seqKey, sGridX + 11 * sCell, sDigitY, sCell - 4, sKeyH);
        place(transposeKey, sGridX + 12 * sCell, sDigitY, sCell - 4, sKeyH);
        lcdArea = {232, 28, 222, 104};  // the QS6.2's window is small for its text; drawn as wide as the panel allows
    }

    void paintSilver(juce::Graphics& g) const {
        const juce::Rectangle<float> panel(0, 0, kW, logicalH());
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffd3d6db), 0, 0, juce::Colour(0xffaeb2b9), 0, panel.getBottom(), false));
        g.fillRoundedRectangle(panel, 10.0f);
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.drawHorizontalLine(1, 10, kW - 10);
        g.setColour(juce::Colour(0xff8d9199));
        g.drawRoundedRectangle(panel.reduced(0.5f), 10.0f, 1.0f);
        // Blue key blocks.
        auto blueBlock = [&](juce::Rectangle<float> r) {
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3d72d8), r.getX(), r.getY(), juce::Colour(0xff1f4fae), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle(r, 8.0f);
            g.setColour(juce::Colours::black.withAlpha(0.25f));
            g.drawRoundedRectangle(r, 8.0f, 1.0f);
        };
        blueBlock({16, 32, 214, 100});
        blueBlock({460, 32, 146, 100});
        auto white = [&](const juce::String& t, juce::Rectangle<float> r, float size) {
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.setFont(theme::uiFont(size, true));
            g.drawFittedText(t, r.toNearestInt(), juce::Justification::centred, 1, 0.6f);
        };
        white("MIX/PROG/EFFECTS", {166, 70, 64, 9}, 6.0f);
        white("COMPARE", {470, 91, 56, 8}, 6.0f);
        white("GLOBAL", {540, 91, 56, 8}, 6.0f);
        label(g, juce::String(juce::CharPointer_UTF8("\xe2\x97\x80  MIDI/MIX CHANNEL  \xe2\x96\xb6")), {30, 136, 180, 10}, 7.0f);
        // LCD in a black window.
        auto win = lcdArea;
        g.setColour(juce::Colour(0xff0a0b0c));
        g.fillRoundedRectangle(win, 4.0f);
        drawQsrLcd(g, win.reduced(6.0f, 12.0f), true);
        label(g, "MODE", {win.getX() + 10, win.getBottom() + 2, 40, 8}, 6.5f);
        label(g, "MIDI CHANNEL", {win.getX() + 70, win.getBottom() + 2, 130, 8}, 6.5f);
        // Direct Select labels: three rows above the group keys, three below 0-9.
        const char* const* above[] = {kMixFn, kFxFn, kProgFn};
        const char* aboveNames[] = {"MIX", "EFFECTS", "PROGRAM"};
        for (int row = 0; row < 3; ++row) {
            const float y = 28.0f + row * 12.5f;
            for (int i = 0; i < 13; ++i) small(g, above[row][i], {sGridX + i * sCell, y, sCell - 4, 10});
            g.setColour(hw::silk().withAlpha(0.35f));
            g.drawHorizontalLine(int(y + 10.5f), sGridX, sGridX + 13 * sCell - 4);
            label(g, juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6 ")) + aboveNames[row], {sGridX + 13 * sCell + 2, y, kW - 6 - (sGridX + 13 * sCell + 2), 10},
                  7.0f, juce::Justification::centredLeft);
        }
        const char* belowNames[] = {"KEYBOARD SOUND", "DRUM SOUND", "MIX"};
        for (int row = 0; row < 3; ++row) {
            const float y = sDigitY + sKeyH + 6 + row * 11.5f;
            for (int d = 0; d < 10; ++d) {
                const juce::String t = row == 0 ? juce::String(kSoundFn[d]) : (row == 1 ? juce::String("DRUM ") : juce::String("CHANNEL ")) + juce::String(d + 1);
                small(g, t, {sGridX + d * sCell, y, sCell - 4, 10});
            }
            g.setColour(hw::silk().withAlpha(0.35f));
            g.drawHorizontalLine(int(y - 1.0f), sGridX, sGridX + 10 * sCell - 4);
            label(g, juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6 ")) + belowNames[row], {sGridX + 10 * sCell + 2, y, 92, 10}, 7.0f,
                  juce::Justification::centredLeft);
        }
        // Model name in plain text where the logo is, with the descriptive line under it.
        // Its own band under the label rows (which end at y 175), so nothing overlaps.
        modelName(g, model, {900, 184, 286, 24}, juce::Colour(0xff4f86e6), juce::Colour(0xff163e94), juce::Justification::centredRight);
        label(g, "64 VOICE SYNTHESIZER WITH FLASH ROM EXPANSION", {880, 213, 306, 8}, 6.0f, juce::Justification::centredRight, 0.8f);
    }

    // ---------------- QSR ----------------
    // Panel units: 1200 x 128, a 1U rack face with ears; positions measured from a photo of a QSR.
    void layoutQsr() {
        place(volumeKnob, 168, 34, 36, 36);
        place(valueKnob, 549, 24, 78, 78);
        place(cursorKey, 664, 32, 70, 26);
        place(editKey, 740, 32, 46, 26);
        place(midiChKey, 664, 72, 70, 26);
        place(storeKey, 740, 72, 46, 26);
        place(mixKey, 804, 32, 48, 26);
        place(progKey, 858, 32, 48, 26);
        place(bankDown, 804, 72, 48, 30);
        place(bankUp, 858, 72, 48, 30);
        lcdArea = {226, 12, 300, 76};
        slots = {924, 64, 180, 40};
    }

    void paintQsr(juce::Graphics& g) const {
        const float h = logicalH();
        // Rack ears with screw slots.
        for (float x : {0.0f, kW - 34.0f}) {
            juce::Rectangle<float> ear(x, 0, 34, h);
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2c2d30), ear.getX(), 0, juce::Colour(0xff1a1b1d), ear.getRight(), 0, false));
            g.fillRoundedRectangle(ear, 4.0f);
            for (float y : {h * 0.2f, h * 0.8f}) {
                g.setColour(juce::Colours::black);
                g.fillRoundedRectangle(juce::Rectangle<float>(ear.getCentreX() - 6, y - 4, 12, 8), 4.0f);
            }
        }
        juce::Rectangle<float> face(36, 0, kW - 72, h);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1d1e21), 0, 0, juce::Colour(0xff101113), 0, h, false));
        g.fillRect(face);
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        g.drawHorizontalLine(1, face.getX(), face.getRight());
        g.setColour(juce::Colours::black);
        g.drawHorizontalLine(int(h) - 1, face.getX(), face.getRight());

        // Phones and volume.
        juce::Rectangle<float> jack(97, 39, 26, 26);
        g.setColour(juce::Colour(0xff3a3c40));
        g.fillEllipse(jack);
        g.setColour(juce::Colour(0xff5a5c60));
        g.drawEllipse(jack.reduced(2), 1.5f);
        g.setColour(juce::Colours::black);
        g.fillEllipse(jack.reduced(7));
        label(g, "PHONES", {82, 90, 56, 10}, 7.5f);
        label(g, "VOLUME", {158, 90, 56, 10}, 7.5f);

        // Display window, with the text printed above the glass and the MIDI CHANNEL bracket below.
        auto win = lcdArea;
        g.setColour(juce::Colour(0xff08090a));
        g.fillRoundedRectangle(win, 5.0f);
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawRoundedRectangle(win.reduced(0.5f), 5.0f, 1.0f);
        label(g, "64 VOICE EXPANDABLE SYNTHESIZER MODULE", {win.getX() + 12, win.getY() + 3, 200, 8}, 6.2f, juce::Justification::centredLeft, 0.85f);
        drawQsrLcd(g, win.reduced(8.0f, 0.0f).withTrimmedTop(13.0f).withTrimmedBottom(6.0f));
        bracket(g, {win.getX() + 60, win.getBottom() + 3, win.getWidth() - 70, 10}, "MIDI CHANNEL", true, 7.0f);

        label(g, "VALUE", {557, 6, 62, 10}, 7.5f);
        bracket(g, {664, 14, 122, 12}, "EDIT MODE", false, 7.5f);
        bracket(g, {804, 14, 102, 12}, "PLAY MODE", false, 7.5f);
        bracket(g, {804, 106, 102, 12}, "BANK SELECT", true, 7.0f);

        // Model name (plain text, where the QSR has its logo), card slots and power.
        modelName(g, "QSR", {slots.getX() + 20, 20, slots.getWidth() - 40, 30}, juce::Colour(0xffe6eaee), juce::Colour(0xff7d8791),
                  juce::Justification::centred);  // centred over the card slots, where the unit's nameplate is
        for (int i = 0; i < 2; ++i) {
            juce::Rectangle<float> slot(slots.getX(), slots.getY() + i * 20.0f, slots.getWidth(), 17);
            g.setColour(juce::Colour(0xff2a2b2e));
            g.fillRoundedRectangle(slot, 2.0f);
            hw::drawWell(g, slot.reduced(6, 5), 1.5f);
            label(g, i == 0 ? "A" : "B", {slot.getRight() + 3, slot.getY(), 10, 17}, 8.0f);
        }
        label(g, "PCMCIA EXPANSION CARDS", {slots.getX(), slots.getBottom() + 2, slots.getWidth(), 9}, 6.5f, juce::Justification::centred, 0.85f);
        label(g, "POWER", {1112, 14, 52, 10}, 7.0f);
        juce::Rectangle<float> pw(1125, 30, 26, 26);
        g.setColour(juce::Colours::black);
        g.fillEllipse(pw.expanded(2));
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3e3f43), pw.getX(), pw.getY(), juce::Colour(0xff111214), pw.getRight(), pw.getBottom(), false));
        g.fillEllipse(pw);
    }

    void valueChanged(juce::Value& v) override { volumeKnob.setValue(int(v.getValue()), false); }
    int current() const { return act.currentProgram ? act.currentProgram() : 0; }
    int highest() const { return act.highestNumber ? act.highestNumber() : 127; }
    void select(int p) {
        if (act.selectProgram) act.selectProgram(juce::jlimit(0, highest(), p));
    }

    // Direct Select functions from the QS6 manual (Chapter 4, "Selecting Functions and Parameters").
    static constexpr const char* kMixFn[13] = {"CHANNEL 11", "CHANNEL 12", "CHANNEL 13", "CHANNEL 14", "CHANNEL 15", "CHANNEL 16", "LEVEL",
                                               "PITCH", "EFFECT", "KEYBD/MIDI", "CONTROLLERS", "RANGE", "NAME"};
    static constexpr const char* kFxFn[13] = {"SEND 1", "SEND 2", "SEND 3", "SEND 4", "CONFIG", "EQ", "MOD", "LEZLIE", "PITCH", "DELAY",
                                              "REVERB", "OVERDRIVE", "MIX"};
    static constexpr const char* kProgFn[13] = {"SOUND 1", "SOUND 2", "SOUND 3", "SOUND 4", "VOICE", "LEVEL", "PITCH", "FILTER", "AMP/RANGE",
                                                "PITCH ENV", "FILTER ENV", "AMP ENV", "NAME"};
    static constexpr const char* kSoundFn[10] = {"MOD 1", "MOD 2", "MOD 3", "MOD 4", "MOD 5", "MOD 6", "PITCH LFO", "FILTER LFO", "AMP LFO", "TRACK GEN"};

    juce::Value& volume;
    Actions act;
    Display display;
    juce::String model;
    Layout layout = Layout::Rack;
    bool built = false;
    Target target = Target::Program;
    bool mixMode = false, blink = true;
    juce::Rectangle<float> lcdArea, slots;
    juce::OwnedArray<hw::Key> groupKeys, digitKeys;
    hw::Key mixKey{"MIX"}, progKey{"PROGRAM"}, editKey{"EDIT SELECT"}, storeKey{"STORE"}, bankDown{"BANK"}, bankUp{"BANK"}, valueUp{"VALUE"},
        valueDown{"VALUE"}, pageLeft{"PAGE"}, pageRight{"PAGE"}, cursorKey{"CURSOR"}, midiChKey{"MIDI CH"}, seqKey{"SEQ", "SELECT"},
        transposeKey{"TRANS-", "POSE"};
    std::vector<hw::Key*> allKeys() {
        std::vector<hw::Key*> v{&mixKey, &progKey, &editKey, &storeKey, &bankDown, &bankUp, &valueUp, &valueDown, &pageLeft, &pageRight,
                                &cursorKey, &midiChKey, &seqKey, &transposeKey};
        for (auto* k : groupKeys) v.push_back(k);
        for (auto* k : digitKeys) v.push_back(k);
        return v;
    }
    hw::Encoder valueKnob;
    hw::Knob volumeKnob{0, 127, 127};
};
