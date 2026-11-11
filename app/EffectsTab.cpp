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
int rawFrom(const qs::EffectParam& p, int shown) {
    if (isHorn(p)) return shown < 0 ? shown + 128 : shown;
    return shown - p.offset;
}

}  // namespace

// One parameter: a drop-down for named choices, otherwise a slider. Caption above.
class EffectsTab::Control : public juce::Component {
public:
    Control(EffectsTab& t, const qs::EffectParam& def, bool type) : tab(t), p(def), isType(type) {
        caption.setText(qs::effectShortName(p), juce::dontSendNotification);
        caption.setFont(theme::uiFont(12.0f, true));
        caption.setColour(juce::Label::textColourId, theme::dim);
        addAndMakeVisible(caption);
        const bool named = !qs::effectValueText(p, 0).empty() && p.limit <= 12 && !isHorn(p) &&
                           !juce::String(p.name).contains("ms") && !(p.offset == -99 && p.limit == 198);
        if (named) {
            for (int i = 0; i <= p.limit; ++i) {
                auto text = juce::String(qs::effectValueText(p, i));
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
        if (useSlider) slider.setValue(displayed(p, raw), juce::dontSendNotification);
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

private:
    juce::Label caption;
    juce::ComboBox combo;
    juce::Slider slider;
    bool useSlider = false, loading = false;
};

EffectsTab::EffectsTab(Session& s) : session(s) {
    addAndMakeVisible(lcd);
    for (int c = 0; c < qs::kEffectConfigs; ++c) configBox.addItem(juce::String(c + 1) + ": " + qs::effectConfigName(c), c + 1);
    configBox.setTooltip("Which effects the patch has and how the four sends are wired (QS manual, Chapter 7). Pick this first: "
                         "the other parameters depend on it.");
    configBox.onChange = [this] {
        const int c = configBox.getSelectedId() - 1;
        if (c < 0 || c == fx.config()) return;
        fx.setConfig(c);
        if (liveToggle.getToggleState() && session.midi.isOpen()) session.midi.send(qs::effectParamEdit(qs::effectConfigParam(), c));
        qslog::action("Effects editor: configuration " + juce::String(c + 1));
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
    note.setText("Reverb and Lezlie input sources show as numbers: the manual names them (Input 1: Pitch or Delay output; "
                 "Input 2: Pitch, Delay or the dry send) but not their order. Modulation sources follow the manual's list order.",
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
    view.setViewedComponent(&content, false);
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
    headers.clear();
    controls.clear();
    groups.clear();
    const int c = fx.config();
    configBox.setSelectedId(c + 1, juce::dontSendNotification);
    const auto& cfg = qs::effectConfig(c);
    juce::String lastSection;
    for (int i = 0; i < cfg.count; ++i) {
        if (!fx.visible(c, i)) continue;
        const auto& p = cfg.params[i];
        // Several types share one "mix" row each: show only the first visible one with a given name per section.
        bool dup = false;
        if (!groups.empty())
            for (auto* existing : groups.back().second)
                if (juce::String(existing->p.name) == p.name) dup = true;
        if (dup) continue;
        if (lastSection != p.section || groups.empty()) {
            lastSection = p.section;
            auto* h = headers.add(new juce::Label({}, juce::String(p.section).toUpperCase()));
            h->setFont(theme::uiFont(13.0f, true));
            h->setColour(juce::Label::textColourId, theme::accent);
            content.addAndMakeVisible(h);
            groups.push_back({h, {}});
        }
        bool isType = false;
        for (int j = 0; j < cfg.count; ++j)
            if (cfg.params[j].condIndex == i) isType = true;
        auto* ctl = controls.add(new Control(*this, p, isType));
        content.addAndMakeVisible(ctl);
        groups.back().second.push_back(ctl);
    }
    loadValues();
    layoutContent();
    updateLcd();
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

void EffectsTab::layoutContent() {
    const int w = juce::jmax(400, view.getWidth() - 16), cellW = 190, cellH = 48, gap = 12;
    const int perRow = juce::jmax(1, (w + gap) / (cellW + gap));
    int y = 0;
    for (auto& [h, ctls] : groups) {
        h->setBounds(0, y, w, 22);
        y += 24;
        for (size_t i = 0; i < ctls.size(); ++i) {
            const int col = int(i) % perRow, row = int(i) / perRow;
            ctls[i]->setBounds(col * (cellW + gap), y + row * (cellH + 6), cellW, cellH);
        }
        y += int((ctls.size() + size_t(perRow) - 1) / size_t(perRow)) * (cellH + 6) + 10;
    }
    content.setSize(w, juce::jmax(y, 10));
}
