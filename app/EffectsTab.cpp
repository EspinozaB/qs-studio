#include "EffectsTab.h"

namespace {

void alert(const juce::String& title, const juce::String& msg) {
    qslog::action("Message: " + title + ": " + msg.replace("\n", " / "));
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, title, msg);
}

bool isHorn(const qs::EffectParam& p) { return juce::String(p.name).contains("lezlie horn"); }

// Displayed value for opcode 10 and the slider: raw + offset, except the Lezlie horn (7-bit two's complement).
int displayed(const qs::EffectParam& p, int raw) {
    if (isHorn(p)) return raw >= 64 ? raw - 128 : raw;
    return raw + p.offset;
}
// "Send 1 reverb decay" in the Reverb card is just "Decay".
juce::String shortCaption(const qs::EffectParam& p) {
    auto n = juce::String(qs::effectShortName(p));
    for (auto kind : {"Pitch ", "Delay ", "Reverb ", "Lezlie ", "Overdrive "})
        if (n.startsWithIgnoreCase(kind) && n.length() > juce::String(kind).length()) {
            n = n.substring(juce::String(kind).length());
            break;
        }
    n = n.replace("prdly", "predelay").replace("10ms", "(10 ms steps)").replace("1ms", "(1 ms steps)").replace("MIDImod", "Mod");
    return n.substring(0, 1).toUpperCase() + n.substring(1);
}

int rawFrom(const qs::EffectParam& p, int shown) {
    if (isHorn(p)) return shown < 0 ? shown + 128 : shown;
    return shown - p.offset;
}

}  // namespace

// One parameter: a drop-down for named choices, otherwise a slider. Caption above.
class EffectsTab::Control : public juce::Component {
public:
    // fineParam: for a time split over two parameters (10 ms steps + 1 ms steps), one control in milliseconds.
    Control(EffectsTab& t, const qs::EffectParam& def, bool type, const qs::EffectParam* fineParam = nullptr)
        : tab(t), p(def), isType(type), fine(fineParam) {
        auto cap = shortCaption(p);
        if (fine != nullptr) {
            cap = cap.replace("(10 ms steps)", "").trim();
            cap = (cap.isEmpty() ? juce::String("Time") : cap) + " (ms)";
        }
        caption.setText(cap, juce::dontSendNotification);
        caption.setFont(theme::uiFont(12.0f, true));
        caption.setTooltip(p.name);
        caption.setColour(juce::Label::textColourId, theme::dim);
        addAndMakeVisible(caption);
        const bool named = !qs::effectValueText(p, 0, t.fx.config()).empty() && p.limit <= 40 && !isHorn(p) &&
                           !juce::String(p.name).contains("ms") && !(p.offset == -99 && p.limit == 198);
        if (fine != nullptr) {
            useSlider = true;
            slider.setSliderStyle(juce::Slider::LinearBar);
            slider.setRange(0, p.limit * 10 + fine->limit, 1);
            slider.textFromValueFunction = [](double v) { return juce::String(int(v)) + " ms"; };
            slider.onValueChange = [this] {
                if (loading) return;
                const int ms = int(slider.getValue());
                tab.changed(p, ms / 10, false);
                tab.changed(*fine, ms % 10, false);
            };
            addAndMakeVisible(slider);
        } else if (named) {
            for (int i = 0; i <= p.limit; ++i) {
                auto text = juce::String(qs::effectValueText(p, i, t.fx.config()));
                combo.addItem(text.isNotEmpty() ? text : juce::String(i), i + 1);
            }
            combo.onChange = [this] {
                if (!loading && combo.getSelectedId() > 0) tab.changed(p, combo.getSelectedId() - 1, isType);
            };
            addAndMakeVisible(combo);
        } else {
            useSlider = true;
            slider.setSliderStyle(juce::Slider::LinearBar);
            slider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 70, 20);
            if (isHorn(p)) slider.setRange(-6, 6, 1);
            else slider.setRange(p.offset, p.limit + p.offset, 1);
            slider.textFromValueFunction = [this](double v) {
                const auto text = juce::String(qs::effectValueText(p, rawFrom(p, int(v))));
                return text.isNotEmpty() ? text : juce::String(int(v));
            };
            slider.onValueChange = [this] {
                if (!loading) tab.changed(p, rawFrom(p, int(slider.getValue())), isType);
            };
            addAndMakeVisible(slider);
        }
    }

    void load() {
        const juce::ScopedValueSetter<bool> l(loading, true);
        const int raw = tab.fx.raw(p);
        if (fine != nullptr) slider.setValue(raw * 10 + tab.fx.raw(*fine), juce::dontSendNotification);
        else if (useSlider) slider.setValue(displayed(p, raw), juce::dontSendNotification);
        else combo.setSelectedId(raw + 1, juce::dontSendNotification);
    }

    void resized() override {
        auto r = getLocalBounds();
        caption.setBounds(r.removeFromTop(16));
        r.removeFromTop(2);
        (useSlider ? static_cast<juce::Component&>(slider) : static_cast<juce::Component&>(combo)).setBounds(r.removeFromTop(26));
    }

    EffectsTab& tab;
    const qs::EffectParam& p;
    const bool isType;
    const qs::EffectParam* fine;

private:
    juce::Label caption;
    juce::ComboBox combo;
    juce::Slider slider;
    bool useSlider = false, loading = false;
};

// Draws the cards and the send titles behind the controls.
class EffectsTab::Content : public juce::Component {
public:
    struct Card {
        juce::Rectangle<int> r;
        juce::String title, badge;
        bool off;
    };
    std::vector<Card> cards;
    std::vector<std::tuple<juce::Rectangle<int>, juce::String, bool>> columns;  // area, title, used by the program
    juce::String intro;
    juce::Rectangle<int> introArea;
    void paint(juce::Graphics& g) override {
        g.setColour(theme::panelHi);
        g.fillRoundedRectangle(introArea.toFloat(), 8.0f);
        g.setColour(theme::text);
        g.setFont(theme::uiFont(13.5f));
        g.drawFittedText(intro, introArea.reduced(12, 6), juce::Justification::centredLeft, 4, 1.0f);
        for (auto& [r, title, used] : columns) {
            g.setColour(used ? theme::accent : theme::dim);
            g.setFont(theme::uiFont(13.0f, true));
            g.drawText(title, r, juce::Justification::centredLeft);
        }
        for (auto& c : cards) {
            g.setColour(c.off ? theme::bg : theme::panel);
            g.fillRoundedRectangle(c.r.toFloat(), 8.0f);
            g.setColour(theme::line);
            g.drawRoundedRectangle(c.r.toFloat().reduced(0.5f), 8.0f, 1.0f);
            g.setColour(c.off ? theme::dim : theme::text);
            g.setFont(theme::uiFont(14.0f, true));
            g.drawText(c.title.toUpperCase(), c.r.withHeight(26).reduced(10, 0), juce::Justification::centredLeft);
            if (c.badge.isNotEmpty()) {
                g.setColour(theme::dim);
                g.setFont(theme::uiFont(12.0f));
                g.drawText(c.badge, c.r.withHeight(26).reduced(10, 0), juce::Justification::centredRight);
            }
        }
    }
};

namespace {
// What each configuration is, in a sentence or two (from the QS6 manual's configuration diagrams).
juce::String configStory(int c) {
    switch (c) {
        case 0: return "1 Reverb: sends 1-3 each go through a Pitch effect (chorus, flange, detune or resonator) and a Delay; send 4 has a "
                       "Delay. All four can feed one shared reverb (Reverb 1).";
        case 1: return "2 Reverbs: send 1 goes through a Delay, a chorus and Reverb 1, and send 2 can feed Reverb 1 too. Send 3 goes "
                       "through a chorus and Reverb 3, and send 4 can feed Reverb 3.";
        case 2: return "Lezlie and Reverb: send 1 has a rotary speaker (Lezlie) and a Delay; sends 2 and 3 have Pitch and Delay; send 4 "
                       "a Delay. All of them can feed one shared reverb (Reverb 1).";
        case 3: return "Reverb and EQ: sends 1 and 2 each have Pitch and Delay and share one reverb; a low and high EQ shapes the main "
                       "outputs. Sends 3 and 4 have no effects in this configuration.";
        case 4: return "Overdrive and Lezlie: one guitar or organ chain on send 1 (overdrive, pitch, delay, reverb and a rotary speaker), "
                       "plus EQ. Sends 2-4 have no effects in this configuration.";
        default: return {};
    }
}
}  // namespace

juce::String EffectsTab::Block::title() const {
    if (feedsReverb > 0) return "Into Reverb " + juce::String(feedsReverb);
    return send > 0 ? kind + " " + juce::String(send) : kind;
}

bool EffectsTab::Block::isOff(const qs::Effects& e) const {
    for (auto* c : controls) {
        const auto n = juce::String(c->p.name);
        if (feedsReverb > 0 ? n.endsWith("input level") : n.endsWith(" mix") || n == "Pitch mix") return e.raw(c->p) == 0;
    }
    return false;
}

EffectsTab::EffectsTab(Session& s) : session(s) {
    content = std::make_unique<Content>();
    addAndMakeVisible(lcd);
    for (int c = 0; c < qs::kEffectConfigs; ++c) configBox.addItem(juce::String(c + 1) + ": " + qs::effectConfigName(c), c + 1);
    configBox.setTooltip("Which effects the patch has and how the four sends are wired (QS manual, Chapter 7). Pick this first: "
                         "on the QS, changing it resets every effect setting to that configuration's defaults.");
    configBox.onChange = [this] {
        const int c = configBox.getSelectedId() - 1;
        if (c < 0 || c == fx.config()) return;
        fx.setConfig(c);
        qslog::action("Effects editor: configuration " + juce::String(c + 1));
        if (liveToggle.getToggleState() && session.midi.isOpen()) {
            // The QS resets every effect setting to that configuration's defaults: read them back.
            session.midi.send(qs::effectParamEdit(qs::effectConfigParam(), c));
            awaitingDump = true;
            requestsSent = 0;
            nextRequestAt = juce::Time::getMillisecondCounter() + 400;
        } else if (session.setStatus) {
            session.setStatus("Configuration changed here only. On the QS a new configuration starts from its defaults; Get from QS after "
                              "changing it there.");
        }
        rebuild();
    };
    targetBox.addItem("The program (Program mode)", 1);
    targetBox.addItem("The mix (Mix mode)", 2);
    targetBox.setSelectedId(1, juce::dontSendNotification);
    targetBox.setTooltip("Which of the QS's two effect edit buffers this works on.");
    targetBox.onChange = [this] { edit = targetBox.getSelectedId() - 1; updateLcd(); };
    for (auto* l : {&configLabel, &targetLabel}) l->setColour(juce::Label::textColourId, theme::dim);
    liveToggle.setToggleState(settings().getBoolValue("liveEdit", true), juce::dontSendNotification);
    liveToggle.onClick = [this] { settings().setValue("liveEdit", liveToggle.getToggleState()); };
    note.setText("Columns are the four effect sends, top to bottom in signal order. An effect's type (chorus, flange, delay type...) "
                 "decides which settings it shows. Mix sets how much of an effect you hear: 0 is off.",
                 juce::dontSendNotification);
    note.setColour(juce::Label::textColourId, theme::dim);
    note.setFont(theme::uiFont(12.0f));
    getBtn.setTooltip("Loads the effect the QS is using now (its effect edit buffer).");
    sendBtn.setTooltip("Puts this effect in the QS's effect edit buffer so you hear it. Not stored.");
    storeBtn.setTooltip("Saves this effect into a User effect slot. On the QS, User effect n belongs to User program n.");
    getBtn.onClick = [this] { getFromQs(); };
    sendBtn.onClick = [this] { sendToQs(); };
    storeBtn.onClick = [this] { storeToUser(); };
    openBtn.onClick = [this] { openSyx(); };
    saveBtn.onClick = [this] { saveSyx(); };
    for (juce::Component* c : std::initializer_list<juce::Component*>{&configBox, &configLabel, &targetBox, &targetLabel, &liveToggle, &getBtn,
                                                                    &sendBtn, &storeBtn, &openBtn, &saveBtn, &note})
        addAndMakeVisible(c);
    view.setViewedComponent(content.get(), false);
    view.setScrollBarsShown(true, false);
    addAndMakeVisible(view);
    session.midi.addListener(this);
    startTimer(40);
    rebuild();
}

EffectsTab::~EffectsTab() { session.midi.removeListener(this); }

void EffectsTab::openEffects(const qs::Effects& e, const juce::String& w, int ed, bool fetch) {
    edit = ed;
    targetBox.setSelectedId(edit + 1, juce::dontSendNotification);
    fx = e;
    where = w;
    qslog::action("Effects editor: opened " + w + (fetch ? " (asking the QS)" : ""));
    rebuild();
    if (fetch) getFromQs();
}

void EffectsTab::rebuild() {
    controls.clear();
    blocks.clear();
    const int c = fx.config();
    configBox.setSelectedId(c + 1, juce::dontSendNotification);
    const auto& cfg = qs::effectConfig(c);
    for (int i = 0; i < cfg.count; ++i) {
        if (!fx.visible(c, i)) continue;
        const auto& p = cfg.params[i];
        // "Reverb Send 2" -> kind Reverb, send 2; "Equalizer" / "Modulation" -> send 0.
        const auto section = juce::String(p.section);
        const auto kind = section.upToFirstOccurrenceOf(" ", false, false);
        const int send = section.contains("Send") ? section.getTrailingIntValue() : 0;
        auto it = std::find_if(blocks.begin(), blocks.end(), [&](const Block& b) { return b.kind == kind && b.send == send; });
        if (it == blocks.end()) {
            blocks.push_back({kind, send, {}, {}});
            it = blocks.end() - 1;
        }
        // Several effect types share a "mix" row: one per block is enough (only the current type's is visible anyway).
        if (std::any_of(it->controls.begin(), it->controls.end(), [&](Control* x) { return juce::String(x->p.name) == p.name; })) continue;
        // The 1 ms half of a delay or predelay time is part of its 10 ms control.
        const auto name = juce::String(p.name);
        if (name.endsWith("1ms") && !name.endsWith("10ms")) continue;
        const qs::EffectParam* fine = nullptr;
        if (name.endsWith("10ms"))
            for (int j = 0; j < cfg.count; ++j)
                if (juce::String(cfg.params[j].name) == name.dropLastCharacters(4) + "1ms" && fx.visible(c, j)) fine = &cfg.params[j];
        bool isType = false;
        for (int j = 0; j < cfg.count; ++j)
            if (cfg.params[j].condIndex == i) isType = true;
        auto* ctl = controls.add(new Control(*this, p, isType, fine));
        content->addAndMakeVisible(ctl);
        it->controls.push_back(ctl);
    }
    // A send's reverb card with no reverb type of its own is its way into another send's reverb.
    for (auto& b : blocks) {
        if (b.kind != "Reverb") continue;
        const bool hasType = std::any_of(b.controls.begin(), b.controls.end(), [](Control* x) { return juce::String(x->p.name) == "Reverb type"; });
        if (hasType) continue;
        int target = 1;
        for (auto& o : blocks)
            if (o.kind == "Reverb" && o.send < b.send &&
                std::any_of(o.controls.begin(), o.controls.end(), [](Control* x) { return juce::String(x->p.name) == "Reverb type"; }))
                target = o.send;
        b.feedsReverb = target;
    }
    loadValues();
    layoutContent();
    updateLcd();
}

void EffectsTab::setProgram(const std::optional<qs::Program>& p) {
    busesKnown = p.has_value();
    busUsed = {{!busesKnown, !busesKnown, !busesKnown, !busesKnown}};
    if (p) {
        static const auto& busP = qs::keyboardParam(qs::findKeyboardParam("Effect bus"));
        static const auto& levelP = qs::keyboardParam(qs::findKeyboardParam("Effect level"));
        for (int snd = 0; snd < qs::kSoundCount; ++snd) {
            if (!p->isEnabled(snd)) continue;
            if (p->isDrum(snd)) {
                for (int i = 0; i < qs::drumParamCount(); ++i) {
                    const auto& d = qs::drumParam(i);
                    if (juce::String(d.name) != "Effect bus") continue;
                    for (int drum = 0; drum < qs::kDrumsPerSound; ++drum) busUsed[size_t(juce::jlimit(0, 3, p->raw(snd, d, drum)))] = true;
                }
            } else if (p->raw(snd, levelP) > 0) {
                busUsed[size_t(juce::jlimit(0, 3, p->raw(snd, busP)))] = true;
            }
        }
    }
    layoutContent();
}

void EffectsTab::refreshCards() {
    for (size_t i = 0; i < blocks.size() && i < content->cards.size(); ++i) {
        content->cards[i].off = blocks[i].isOff(fx);
        for (auto* c : blocks[i].controls) c->setAlpha(content->cards[i].off ? 0.55f : 1.0f);
        content->cards[i].badge = content->cards[i].off ? (blocks[i].feedsReverb > 0 ? "off (level 0)" : "off (mix 0)") : juce::String();
    }
    content->repaint();
}

void EffectsTab::loadValues() {
    for (auto* c : controls) c->load();
}

void EffectsTab::updateLcd() {
    lcd.setLines(where, {juce::String("CONFIG ") + juce::String(fx.config() + 1) + ": " + juce::String(qs::effectConfigName(fx.config())).toUpperCase(),
                         juce::String(edit == 0 ? "PROGRAM MODE EFFECT" : "MIX MODE EFFECT") +
                             (awaitingDump ? juce::String("   WAITING FOR QS...") : juce::String())});
}

void EffectsTab::changed(const qs::EffectParam& p, int raw, bool isType) {
    fx.setRaw(p, raw);
    if (liveToggle.getToggleState() && session.midi.isOpen()) session.midi.send(qs::effectParamEdit(p, displayed(p, raw)));
    refreshCards();
    if (isType) rebuildPending = true;  // other parameters appear or go; rebuild outside the control's own callback
}

void EffectsTab::timerCallback() {
    if (rebuildPending) {
        rebuildPending = false;
        rebuild();
    }
    const auto now = juce::Time::getMillisecondCounter();
    if (awaitingDump && now >= nextRequestAt) {
        if (requestsSent < 4 && session.midi.isOpen()) {
            session.midi.send(qs::editEffectsRequest(edit));
            ++requestsSent;
            nextRequestAt = now + 900;
        } else {
            awaitingDump = false;
            updateLcd();
            if (session.setStatus) session.setStatus("The QS didn't send its effect edit buffer. Check both MIDI cables.");
        }
    }
}

void EffectsTab::sysexReceived(const qs::Bytes& msg) {
    if (!awaitingDump || qs::opcodeOf(msg) != qs::EditEffectsDump || msg.size() < 7 || msg[6] != edit) return;
    if (auto e = qs::Effects::fromDump(msg)) {
        awaitingDump = false;
        fx = *e;
        if (where.isEmpty() || where == "No effect loaded") where = "QS effect edit buffer";
        rebuild();
        if (session.setStatus) session.setStatus("Loaded the " + juce::String(edit == 0 ? "program" : "mix") + " effect from the QS.");
    }
}

void EffectsTab::getFromQs() {
    if (!session.midi.isOpen()) { alert("Not connected", "Connect to the QS on the Connection page first."); return; }
    awaitingDump = true;
    requestsSent = 0;
    nextRequestAt = juce::Time::getMillisecondCounter();
    updateLcd();
}

void EffectsTab::sendToQs() {
    if (!session.midi.isOpen()) { alert("Not connected", "Connect to the QS on the Connection page first."); return; }
    session.midi.send(fx.editDump(edit));
    qslog::action("Effects editor: sent the effect to edit buffer " + juce::String(edit));
    if (session.setStatus) session.setStatus("Sent the effect to the QS (not stored).");
}

void EffectsTab::storeToUser() {
    if (!session.midi.isOpen()) { alert("Not connected", "Connect to the QS on the Connection page first."); return; }
    auto* w = new juce::AlertWindow("Store to a User effect",
                                    "Store this effect into which User effect? User effect n is the effect of User program n, so this "
                                    "changes how that program sounds.",
                                    juce::MessageBoxIconType::WarningIcon);
    juce::StringArray items;
    for (int i = 0; i < 128; ++i) {
        const auto n = session.library.nameOf(0, i);
        items.add(juce::String(i).paddedLeft('0', 3) + "  " + (n.isNotEmpty() ? n : juce::String("(not read)")));
    }
    w->addComboBox("slot", items, "User program's effect");
    w->getComboBoxComponent("slot")->setSelectedItemIndex(session.currentBank == 0 ? session.currentProgram : 0);
    w->addButton("Store", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
        if (r != 1) return;
        const int n = w->getComboBoxComponent("slot")->getSelectedItemIndex();
        session.midi.send(fx.userDump(n));
        session.library.effects[0][size_t(n)] = fx.bytes();
        session.library.save();
        qslog::action("Effects editor: stored to User effect " + juce::String(n));
        if (session.setStatus) session.setStatus("Stored the effect as User effect " + juce::String(n) + ".");
    }), true);
}

void EffectsTab::saveSyx() {
    chooser = std::make_unique<juce::FileChooser>("Save effect", settingsDir().getChildFile("effect.syx"), "*.syx");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        const auto d = fx.editDump(0);
        f.replaceWithData(d.data(), d.size());
    });
}

void EffectsTab::openSyx() {
    chooser = std::make_unique<juce::FileChooser>("Open an effect", settingsDir(), "*.syx;*.SYX");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (!f.existsAsFile()) return;
        juce::MemoryBlock mb;
        f.loadFileAsData(mb);
        auto ptr = static_cast<const uint8_t*>(mb.getData());
        for (auto& m : qs::splitSysex(qs::Bytes(ptr, ptr + mb.getSize())))
            if (auto e = qs::Effects::fromDump(m)) {
                openEffects(*e, f.getFileName(), edit, false);
                return;
            }
        alert("No effect", f.getFileName() + " has no QS effect in it.");
    });
}

void EffectsTab::paint(juce::Graphics& g) { g.fillAll(theme::bg); }

void EffectsTab::resized() {
    auto r = getLocalBounds().reduced(14);
    auto top = r.removeFromTop(118);
    lcd.setBounds(top.removeFromLeft(juce::jmin(520, top.getWidth() / 2)));
    top.removeFromLeft(16);
    auto row1 = top.removeFromTop(32);
    targetLabel.setBounds(row1.removeFromLeft(70));
    targetBox.setBounds(row1.removeFromLeft(220));
    row1.removeFromLeft(10);
    liveToggle.setBounds(row1.removeFromLeft(100));
    top.removeFromTop(6);
    auto row2 = top.removeFromTop(32);
    configLabel.setBounds(row2.removeFromLeft(100));
    configBox.setBounds(row2.removeFromLeft(260));
    top.removeFromTop(8);
    auto row3 = top.removeFromTop(34);
    for (auto* b : {&getBtn, &sendBtn, &storeBtn, &openBtn, &saveBtn}) {
        b->setBounds(row3.removeFromLeft(juce::jmin(b == &storeBtn ? 140 : 110, row3.getWidth())));
        row3.removeFromLeft(8);
    }
    r.removeFromTop(6);
    note.setBounds(r.removeFromBottom(34));
    view.setBounds(r);
    layoutContent();
}

// Sends side by side as columns (each a chain of cards, top to bottom in signal order), then the patch-wide cards.
void EffectsTab::layoutContent() {
    const int w = juce::jmax(400, view.getWidth() - 14), gap = 12, cellH = 46, pad = 10, titleH = 26;
    std::vector<int> sends;
    for (auto& b : blocks)
        if (b.send > 0 && std::find(sends.begin(), sends.end(), b.send) == sends.end()) sends.push_back(b.send);
    std::sort(sends.begin(), sends.end());
    const int ncol = juce::jmax(1, int(sends.size()));
    const int colW = (w - gap * (ncol - 1)) / ncol;
    auto placeCard = [&](Block& b, int x, int y, int cw) {
        const int perRow = juce::jmax(1, (cw - 2 * pad + 8) / (160 + 8));
        const int cellW = (cw - 2 * pad - 8 * (perRow - 1)) / perRow;
        const int rows = int((b.controls.size() + size_t(perRow) - 1) / size_t(perRow));
        for (size_t i = 0; i < b.controls.size(); ++i)
            b.controls[i]->setBounds(x + pad + int(i % size_t(perRow)) * (cellW + 8), y + titleH + int(i / size_t(perRow)) * (cellH + 6), cellW, cellH);
        b.card = {x, y, cw, titleH + rows * (cellH + 6) + 6};
        return b.card.getBottom();
    };
    content->intro = configStory(fx.config()) +
                     "\nEach program sound sends to one of the four effect sends (its Effect bus). Every effect below is always there; "
                     "one turned down to mix 0 adds nothing, so it shows as off." +
                     (busesKnown ? juce::String("\nThis program uses send ") + [&] {
                         juce::StringArray used;
                         for (int i = 0; i < 4; ++i)
                             if (busUsed[size_t(i)]) used.add(juce::String(i + 1));
                         return used.isEmpty() ? juce::String("none") : used.joinIntoString(", ");
                     }() + ": changes on the other sends won't be heard with it."
                                 : juce::String());
    content->introArea = {0, 0, w, busesKnown ? 78 : 62};
    const int top = content->introArea.getBottom() + 10;
    int bottom = top;
    for (int ci = 0; ci < int(sends.size()); ++ci) {
        int y = top + 30;  // room for the column title
        const int x = ci * (colW + gap);
        for (auto& b : blocks)
            if (b.send == sends[size_t(ci)]) y = placeCard(b, x, y, colW) + gap;
        bottom = juce::jmax(bottom, y);
    }
    // Patch-wide cards share a row.
    std::vector<Block*> shared;
    for (auto& b : blocks)
        if (b.send == 0) shared.push_back(&b);
    if (!shared.empty()) {
        const int sw = (w - gap * (int(shared.size()) - 1)) / int(shared.size());
        bottom = juce::jmax(bottom, top);
        int y2 = bottom;
        for (size_t i = 0; i < shared.size(); ++i) y2 = juce::jmax(y2, placeCard(*shared[i], int(i) * (sw + gap), bottom, sw) + gap);
        bottom = y2;
    }
    content->columns.clear();
    for (int ci = 0; ci < int(sends.size()); ++ci) {
        const int sn = sends[size_t(ci)];
        juce::String chain;
        for (auto& b : blocks)
            if (b.send == sn) chain << (chain.isEmpty() ? "" : "  >  ") << (b.feedsReverb > 0 ? "Reverb " + juce::String(b.feedsReverb) : b.kind);
        const bool used = busUsed[size_t(juce::jlimit(1, 4, sn) - 1)];
        content->columns.push_back({{ci * (colW + gap), top, colW, 24},
                                    "SEND " + juce::String(sn) + ":  " + chain + (used ? juce::String() : "   (not used by this program)"), used});
    }
    content->cards.clear();
    for (auto& b : blocks) content->cards.push_back({b.card, b.title(), {}, false});
    content->setSize(w, juce::jmax(bottom, 10));
    refreshCards();
}
