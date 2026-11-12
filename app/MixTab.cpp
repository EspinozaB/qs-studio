#include "MixTab.h"

namespace {

void alert(const juce::String& title, const juce::String& msg) {
    qslog::action("Message: " + title + ": " + msg.replace("\n", " / "));
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, title, msg);
}

// Columns of the channel table: which parameter, title and width.
struct Column {
    const char* param;  // qs::MixParam name, or "" for the channel number
    const char* title;
    int width;
    const char* tip;
};
const Column kColumns[] = {
    {"", "Ch", 34, nullptr},
    {"Enable", "On", 38, "Channel on or off. An off channel keeps its settings but makes no sound."},
    {"Bank", "Bank", 96, "Which bank the channel's program comes from."},
    {"Program", "Program", 170, "The program this channel plays."},
    {"", "", 44, "Edit this channel's program inside the mix (the QS keeps a separate edit buffer per channel)."},
    {"Volume", "Volume", 110, "Channel volume, 0-99."},
    {"Pan", "Pan", 66, "Pan <3 (left) to 3> (right), or Prog to use the program's own pan."},
    {"Output", "Output", 70, "Main, Aux (QS7/QS8/QSR), Off (effects only), or Prog to use the program's setting."},
    {"Effect level", "FX level", 110, "How much goes to the effects, 0-99, or Prog (the far right) for the program's own level."},
    {"Effect bus", "FX bus", 70, "Effect bus 1-4, or Prog for the program's own bus per sound."},
    {"Octave", "Oct", 56, "Transpose in octaves, -2 to +2."},
    {"Semitone", "Semi", 58, "Transpose in semitones, -12 to +12."},
    {"Low note", "Low", 66, "Lowest note this channel plays (for splits)."},
    {"High note", "High", 66, "Highest note this channel plays."},
    {"MIDI in", "In", 34, "Responds to MIDI coming in on this channel."},
    {"MIDI out", "Out", 34, "Sends what you play on this channel out of MIDI OUT."},
    {"Keyboard", "Kbd", 34, "Plays from the QS's own keys, wheels and pedals (QSR: MIDI Group)."},
    {"Wheels", "Whl", 34, "Pitch and mod wheels."},
    {"Aftertouch", "AT", 34, "Aftertouch."},
    {"Sustain", "Sus", 34, "Sustain pedal."},
    {"Controllers A-D, pedals", "Ctl", 34, "Controllers A-D and pedals 1-2."},
};

int totalWidth() {
    int w = 0;
    for (auto& c : kColumns) w += c.width + 4;
    return w;
}

}  // namespace

// One channel: a control per column.
class MixTab::Row : public juce::Component {
public:
    Row(MixTab& t, int channel) : tab(t), ch(channel) {
        for (auto& col : kColumns) {
            const juce::String pname = col.param;
            juce::Component* c = nullptr;
            if (pname.isEmpty() && juce::String(col.title) == "Ch") {
                auto* l = new juce::Label({}, juce::String(ch + 1));
                l->setFont(theme::monoFont(14.0f, true));
                l->setColour(juce::Label::textColourId, theme::accent);
                c = l;
            } else if (pname.isEmpty()) {
                auto* b = new juce::TextButton("Edit");
                b->onClick = [this] {
                    if (tab.session.editMixChannelProgram) tab.session.editMixChannelProgram(ch, juce::String(tab.mix.name()).trimEnd());
                };
                c = b;
            } else {
                const int idx = qs::findMixChannelParam(pname.toStdString());
                const auto& p = qs::mixChannelParam(idx);
                if (p.bits == 1) {
                    auto* t = new juce::ToggleButton();
                    t->onClick = [this, t, &p] { if (!loading) tab.changed(ch, p, t->getToggleState() ? 1 : 0); };
                    c = t;
                } else if (pname == "Volume" || pname == "Effect level") {
                    auto* s = new juce::Slider(juce::Slider::LinearBar, juce::Slider::TextBoxLeft);
                    s->setRange(0, p.limit, 1);
                    if (pname == "Effect level") s->textFromValueFunction = [](double v) { return v >= 100 ? juce::String("Prog") : juce::String(int(v)); };
                    s->onValueChange = [this, s, &p] { if (!loading) tab.changed(ch, p, int(s->getValue())); };
                    c = s;
                } else {
                    auto* b = new juce::ComboBox();
                    if (pname == "Program") programBox = b;
                    else if (pname == "Bank")
                        for (int i = 0; i <= p.limit; ++i) b->addItem(kBanks[size_t(i)].title, i + 1);
                    else
                        for (int i = 0; i <= p.limit; ++i) {
                            auto text = juce::String(qs::mixValueText(p, i));
                            b->addItem(text.isNotEmpty() ? text : juce::String(i + p.offset), i + 1);
                        }
                    b->onChange = [this, b, &p] {
                        if (loading || b->getSelectedId() == 0) return;
                        tab.changed(ch, p, b->getSelectedId() - 1);
                        if (juce::String(p.name) == "Bank") fillPrograms();
                    };
                    c = b;
                }
            }
            if (col.tip != nullptr)
                if (auto* tc = dynamic_cast<juce::SettableTooltipClient*>(c)) tc->setTooltip(col.tip);
            controls.add(c);
            addAndMakeVisible(c);
        }
    }

    void fillPrograms() {
        if (programBox == nullptr) return;
        const juce::ScopedValueSetter<bool> l(loading, true);
        const int bank = tab.mix.bank(ch), sel = tab.mix.program(ch);
        programBox->clear(juce::dontSendNotification);
        for (int i = 0; i < 128; ++i) {
            const auto name = bank < int(kBanks.size()) ? tab.session.library.nameOf(bank, i) : juce::String();
            programBox->addItem(juce::String(i).paddedLeft('0', 3) + "  " + (name.isNotEmpty() ? name : juce::String("(not read)")), i + 1);
        }
        programBox->setSelectedId(sel + 1, juce::dontSendNotification);
    }

    void load() {
        const juce::ScopedValueSetter<bool> l(loading, true);
        int i = 0;
        for (auto& col : kColumns) {
            auto* c = controls[i++];
            const juce::String pname = col.param;
            if (pname.isEmpty()) continue;
            const auto& p = qs::mixChannelParam(qs::findMixChannelParam(pname.toStdString()));
            const int raw = tab.mix.channel(ch, p);
            if (auto* t = dynamic_cast<juce::ToggleButton*>(c)) t->setToggleState(raw != 0, juce::dontSendNotification);
            else if (auto* s = dynamic_cast<juce::Slider*>(c)) s->setValue(raw, juce::dontSendNotification);
            else if (auto* b = dynamic_cast<juce::ComboBox*>(c)) {
                if (b == programBox) fillPrograms();
                else b->setSelectedId(raw + 1, juce::dontSendNotification);
            }
        }
        dimIfOff();
    }

    // An off channel keeps its settings; show them faded, with the On switch at full strength.
    void dimIfOff() {
        const bool on = tab.mix.enabled(ch);
        for (int i = 0; i < controls.size(); ++i)
            if (i != 0 && i != 1) controls[i]->setAlpha(on ? 1.0f : 0.45f);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        if (ch % 2) g.fillAll(theme::panelHi.withAlpha(0.35f));
    }

    void resized() override {
        int x = 0, i = 0;
        for (auto& col : kColumns) {
            controls[i++]->setBounds(x, 3, col.width, getHeight() - 6);
            x += col.width + 4;
        }
    }

private:
    MixTab& tab;
    const int ch;
    bool loading = false;
    juce::ComboBox* programBox = nullptr;
    juce::OwnedArray<juce::Component> controls;
};

namespace {
class ColumnHeader : public juce::Component {
public:
    void paint(juce::Graphics& g) override {
        g.setFont(theme::uiFont(12.0f, true));
        g.setColour(theme::dim);
        int x = 0;
        for (auto& col : kColumns) {
            g.drawText(col.title, x, 0, col.width, getHeight(), juce::Justification::centredLeft);
            x += col.width + 4;
        }
    }
};
}  // namespace

MixTab::MixTab(Session& s) : session(s) {
    addAndMakeVisible(lcd);
    nameEdit.setInputRestrictions(10);
    nameEdit.setFont(theme::monoFont(18.0f, true));
    nameEdit.setTextToShowWhenEmpty("Mix name", theme::dim);
    nameEdit.onTextChange = [this] {
        mix.setName(nameEdit.getText().toStdString());
        nameDirty = true;
        lastNameEdit = juce::Time::getMillisecondCounter();
        updateLcd();
    };
    addAndMakeVisible(nameEdit);
    liveToggle.setToggleState(settings().getBoolValue("liveEdit", true), juce::dontSendNotification);
    liveToggle.setTooltip("Send each change to the QS as you make it (the QS must be in Mix mode with this mix's edit buffer).");
    liveToggle.onClick = [this] { settings().setValue("liveEdit", liveToggle.getToggleState()); };

    for (int ch = 1; ch <= 16; ++ch) fxChannel.addItem("Ch " + juce::String(ch), ch);
    fxChannel.setTooltip("The mix uses the effect of the program on this channel (one effect per mix).");
    fxChannel.onChange = [this] { commonChanged(qs::mixCommonParam(1), fxChannel.getSelectedId() - 1); };
    fxProgChange.setTooltip("On: a program change on the effects channel also changes the mix's effect.");
    fxProgChange.onClick = [this] { commonChanged(qs::mixCommonParam(0), fxProgChange.getToggleState() ? 1 : 0); };
    fxChannelLabel.setColour(juce::Label::textColourId, theme::dim);

    for (juce::Component* c : std::initializer_list<juce::Component*>{&liveToggle, &getBtn, &sendBtn, &storeBtn, &openBtn, &saveBtn, &fxBtn,
                                                                    &fxChannel, &fxChannelLabel, &fxProgChange})
        addAndMakeVisible(c);
    getBtn.setTooltip("Loads the mix the QS is playing (its mix edit buffer).");
    sendBtn.setTooltip("Puts this mix in the QS's mix edit buffer so you hear it. Not stored until you Store it.");
    storeBtn.setTooltip("Saves this mix into one of the QS's 100 User mixes, replacing what was there.");
    fxBtn.setTooltip("Opens the effect this mix uses (the effect of the program on its effects channel) in the Effects editor.");
    getBtn.onClick = [this] { getFromQs(); };
    sendBtn.onClick = [this] { sendToQs(); };
    storeBtn.onClick = [this] { storeToUser(); };
    openBtn.onClick = [this] { openSyx(); };
    saveBtn.onClick = [this] { saveSyx(); };
    fxBtn.onClick = [this] {
        if (!session.midi.isOpen()) { alert("Not connected", "Connect to the QS on the Connection page first."); return; }
        if (session.openEffectsEditor) session.openEffectsEditor(qs::Effects(), "Mix " + juce::String(mix.name()).trimEnd(), 1);
    };

    columns = std::make_unique<ColumnHeader>();
    content.addAndMakeVisible(*columns);
    for (int ch = 0; ch < qs::kMixChannels; ++ch) content.addAndMakeVisible(rows.add(new Row(*this, ch)));
    view.setViewedComponent(&content, false);
    view.setScrollBarsShown(true, true);
    addAndMakeVisible(view);
    session.midi.addListener(this);
    startTimer(40);
    loadAll();
}

MixTab::~MixTab() { session.midi.removeListener(this); }

void MixTab::openMix(const qs::Mix& m, const juce::String& w, int number) {
    mix = m;
    where = w;
    userNumber = number;
    nameDirty = false;
    qslog::action("Mix editor: opened \"" + juce::String(mix.name()).trimEnd() + "\" from " + w);
    loadAll();
    // Not known here: once the QS has switched to the mix, ask for its mix edit buffer.
    const bool empty = std::all_of(mix.bytes().begin(), mix.bytes().end(), [](uint8_t b) { return b == 0; });
    if (empty && session.midi.isOpen()) {
        awaitingDump = true;
        requestsSent = 0;
        nextRequestAt = juce::Time::getMillisecondCounter() + 500;
        updateLcd();
    }
}

void MixTab::refreshNames() {
    for (auto* r : rows) r->fillPrograms();
}

void MixTab::loadAll() {
    {
        const juce::ScopedValueSetter<std::function<void()>> noNotify(nameEdit.onTextChange, nullptr);
        nameEdit.setText(juce::String(mix.name()).trimEnd(), false);
    }
    fxChannel.setSelectedId(mix.common(qs::mixCommonParam(1)) + 1, juce::dontSendNotification);
    fxProgChange.setToggleState(mix.common(qs::mixCommonParam(0)) != 0, juce::dontSendNotification);
    for (auto* r : rows) r->load();
    updateLcd();
}

void MixTab::updateLcd() {
    int on = 0;
    for (int ch = 0; ch < qs::kMixChannels; ++ch) on += mix.enabled(ch);
    lcd.setLines(juce::String(mix.name()).trimEnd(),
                 {where.toUpperCase(), "CHANNELS ON: " + juce::String(on) + " OF 16   EFFECTS FROM CH " +
                                           juce::String(mix.common(qs::mixCommonParam(1)) + 1) +
                                           (awaitingDump ? juce::String("   WAITING FOR QS...") : juce::String())});
}

void MixTab::changed(int ch, const qs::MixParam& p, int raw) {
    mix.setChannel(ch, p, raw);
    if (rows[ch] != nullptr) rows[ch]->dimIfOff();
    if (liveToggle.getToggleState() && session.playingMix) session.setActiveMix(mix);  // keyboard follows Enable/Keyboard switches
    updateLcd();
    if (liveToggle.getToggleState() && session.midi.isOpen()) session.midi.send(qs::mixParamEdit(p, ch, raw + p.offset));
}

void MixTab::commonChanged(const qs::MixParam& p, int raw) {
    mix.setCommon(p, raw);
    updateLcd();
    if (liveToggle.getToggleState() && session.midi.isOpen()) session.midi.send(qs::mixParamEdit(p, 0, raw + p.offset));
}

void MixTab::timerCallback() {
    const auto now = juce::Time::getMillisecondCounter();
    if (awaitingDump && now >= nextRequestAt) {
        if (requestsSent < 4 && session.midi.isOpen()) {
            session.midi.send(qs::mixRequest(100));
            ++requestsSent;
            nextRequestAt = now + 900;
        } else {
            awaitingDump = false;
            updateLcd();
            if (session.setStatus) session.setStatus("The QS didn't send its mix edit buffer. Check both MIDI cables.");
        }
    }
    // A renamed mix goes over as a whole dump once typing stops.
    if (nameDirty && now - lastNameEdit > 400 && liveToggle.getToggleState() && session.midi.isOpen()) {
        nameDirty = false;
        session.midi.send(mix.dump(100));
    }
}

void MixTab::sysexReceived(const qs::Bytes& msg) {
    if (!awaitingDump || qs::opcodeOf(msg) != qs::NewMixDump || msg.size() < 7 || msg[6] != 100) return;
    if (auto m = qs::Mix::fromDump(msg)) {
        awaitingDump = false;
        mix = *m;
        where = "QS mix edit buffer";
        userNumber = -1;
        loadAll();
        if (session.setStatus) session.setStatus("Loaded mix " + juce::String(mix.name()).trimEnd() + " from the QS.");
    }
}

void MixTab::getFromQs() {
    if (!session.midi.isOpen()) { alert("Not connected", "Connect to the QS on the Connection page first."); return; }
    awaitingDump = true;
    requestsSent = 0;
    nextRequestAt = juce::Time::getMillisecondCounter();
    updateLcd();
}

void MixTab::sendToQs() {
    if (!session.midi.isOpen()) { alert("Not connected", "Connect to the QS on the Connection page first."); return; }
    session.midi.send(qs::modeSelect(1));
    session.midi.send(mix.dump(100));
    session.playingMix = true;
    session.setActiveMix(mix);
    nameDirty = false;
    qslog::action("Mix editor: sent \"" + juce::String(mix.name()).trimEnd() + "\" to the mix edit buffer");
    if (session.setStatus) session.setStatus("Sent mix " + juce::String(mix.name()).trimEnd() + " to the QS (not stored).");
}

void MixTab::storeToUser() {
    if (!session.midi.isOpen()) { alert("Not connected", "Connect to the QS on the Connection page first."); return; }
    auto* w = new juce::AlertWindow("Store to a User mix",
                                    "Store \"" + juce::String(mix.name()).trimEnd() + "\" into which User mix? This replaces that mix on the QS.",
                                    juce::MessageBoxIconType::WarningIcon);
    juce::StringArray items;
    for (int i = 0; i < 100; ++i) {
        const auto n = session.library.mixNameOf(0, i);
        items.add(juce::String(i).paddedLeft('0', 2) + "  " + (n.isNotEmpty() ? n : juce::String("(not read)")));
    }
    w->addComboBox("slot", items, "User mix");
    w->getComboBoxComponent("slot")->setSelectedItemIndex(juce::jmax(0, userNumber));
    w->addButton("Store", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
        if (r != 1) return;
        const int n = w->getComboBoxComponent("slot")->getSelectedItemIndex();
        const auto d = mix.dump(n);
        session.midi.send(d);
        session.library.setMix(0, n, d);
        session.library.save();
        userNumber = n;
        where = "User mix " + juce::String(n).paddedLeft('0', 2);
        updateLcd();
        qslog::action("Mix editor: stored \"" + juce::String(mix.name()).trimEnd() + "\" to User mix " + juce::String(n));
        if (session.libraryChanged) session.libraryChanged();
        if (session.setStatus) session.setStatus("Stored " + juce::String(mix.name()).trimEnd() + " to " + where + ".");
    }), true);
}

void MixTab::saveSyx() {
    const auto n = juce::String(mix.name()).trimEnd();
    chooser = std::make_unique<juce::FileChooser>("Save mix", settingsDir().getChildFile(juce::File::createLegalFileName(n.isEmpty() ? "mix" : n) + ".syx"),
                                                  "*.syx");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        const auto d = mix.dump(100);
        f.replaceWithData(d.data(), d.size());
    });
}

void MixTab::openSyx() {
    chooser = std::make_unique<juce::FileChooser>("Open a mix", settingsDir(), "*.syx;*.SYX");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (!f.existsAsFile()) return;
        juce::MemoryBlock mb;
        f.loadFileAsData(mb);
        auto ptr = static_cast<const uint8_t*>(mb.getData());
        for (auto& m : qs::splitSysex(qs::Bytes(ptr, ptr + mb.getSize())))
            if (auto x = qs::Mix::fromDump(m)) {
                openMix(*x, f.getFileName(), -1);
                return;
            }
        alert("No mix", f.getFileName() + " has no QS mix (new format) in it.");
    });
}

void MixTab::paint(juce::Graphics& g) { g.fillAll(theme::bg); }

void MixTab::resized() {
    auto r = getLocalBounds().reduced(14);
    auto top = r.removeFromTop(118);
    lcd.setBounds(top.removeFromLeft(juce::jmin(520, top.getWidth() / 2)));
    top.removeFromLeft(16);
    auto row1 = top.removeFromTop(36);
    nameEdit.setBounds(row1.removeFromLeft(170));
    row1.removeFromLeft(10);
    liveToggle.setBounds(row1.removeFromLeft(100));
    row1.removeFromLeft(10);
    fxChannelLabel.setBounds(row1.removeFromLeft(140));
    fxChannel.setBounds(row1.removeFromLeft(80));
    top.removeFromTop(4);
    auto rowFx = top.removeFromTop(30);
    fxProgChange.setBounds(rowFx.removeFromLeft(230));
    rowFx.removeFromLeft(8);
    fxBtn.setBounds(rowFx.removeFromLeft(200));
    top.removeFromTop(6);
    auto row2 = top.removeFromTop(34);
    for (auto* b : {&getBtn, &sendBtn, &storeBtn, &openBtn, &saveBtn}) {
        const int w = b == &storeBtn ? 160 : 104;
        b->setBounds(row2.removeFromLeft(juce::jmin(w, row2.getWidth())));
        row2.removeFromLeft(8);
    }
    r.removeFromTop(10);
    const int width = totalWidth(), rowH = 32, headH = 22;
    view.setBounds(r);
    content.setSize(width, headH + rowH * qs::kMixChannels);
    columns->setBounds(0, 0, width, headH);
    for (int i = 0; i < rows.size(); ++i) rows[i]->setBounds(0, headH + i * rowH, width, rowH);
}
