#include "EditorTab.h"

namespace {

void alert(const juce::String& title, const juce::String& msg) {
    qslog::action("Message: " + title + ": " + msg.replace("\n", " / "));
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, title, msg);
}

juce::String titleCase(juce::String s) {
    return s.isEmpty() ? s : s.substring(0, 1).toUpperCase() + s.substring(1);
}

bool nameHas(const char* name, const char* what) { return juce::String(name).containsIgnoreCase(what); }

bool enumLike(const qs::ParamDef& d) {
    for (auto* w : {"waveform", "source", "destination", "type", "mode", "curve", "output", "bus", "trigger", "input", "group"})
        if (nameHas(d.name, w)) return d.limit <= 48;
    return false;
}

const qs::ParamDef* kbd(const char* name) {
    const int i = qs::findKeyboardParam(name);
    jassert(i >= 0);
    return i >= 0 ? &qs::keyboardParam(i) : nullptr;
}

const qs::ParamDef* drumDef(const char* name) {
    for (int i = 0; i < qs::drumParamCount(); ++i)
        if (juce::String(qs::drumParam(i).name) == name) return &qs::drumParam(i);
    jassertfalse;
    return nullptr;
}

// Detail pages: which spec functions each page shows.
struct Section { const char* name; std::vector<int> funcs; };
const std::vector<Section>& sections() {
    static const std::vector<Section> s{{"Voice", {0, 1, 2}}, {"Pitch", {3}},   {"Filter", {4}},      {"Amp", {5}},
                                        {"Range", {6}},       {"Mod", {7}},     {"LFOs", {9, 10, 11}}, {"Envelopes", {12, 13, 14}},
                                        {"Tracking", {15}}};
    return s;
}

}  // namespace

// ---------------- ParamControl ----------------

// MIDI note as the QS shows it: 0 = C-2, 60 = C3, 127 = G8.
static juce::String noteName(int n) {
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(names[((n % 12) + 12) % 12]) + juce::String(n / 12 - 2);
}

juce::String ParamControl::valueLabel(const qs::ParamDef& d, int raw) {
    const int v = raw + d.offset;
    if (nameHas(d.name, "waveform")) {
        juce::String n = qs::lfoWaveName(raw);
        return n.isNotEmpty() ? n : juce::String(raw);
    }
    if (nameHas(d.name, "sample group")) {  // the name alone fits the menu; QS Edit Pro shows it the same way
        juce::String n = qs::sampleGroupName(raw);
        return n.isNotEmpty() ? n : "Group " + juce::String(raw);
    }
    if (nameHas(d.name, "effect bus")) return "Bus " + juce::String(raw + 1);
    // Names from the QS6 manual (Program Edit chapter).
    if (nameHas(d.name, "velocity curve")) {
        static const char* curves[] = {"Linear", "Inverted", "Maximum", "Minimum", "1 of 2", "2 of 2", "1 of 3",
                                       "2 of 3", "3 of 3",   "1 of 4",  "2 of 4",  "3 of 4", "4 of 4"};
        return raw >= 0 && raw < 13 ? juce::String(curves[raw]) : "Curve " + juce::String(raw + 1);
    }
    if (nameHas(d.name, "key mode")) {
        static const char* modes[] = {"Mono", "Poly", "1-Pitch", "1-PMono"};
        return raw >= 0 && raw < 4 ? juce::String(modes[raw]) : juce::String(raw);
    }
    if (nameHas(d.name, "portamento mode")) {
        static const char* modes[] = {"Exponential", "Linear", "1 Speed"};
        return raw >= 0 && raw < 3 ? juce::String(modes[raw]) : juce::String(raw);
    }
    if (juce::String(d.name) == "Output") {
        static const char* outs[] = {"Main", "Aux", "Off"};
        return raw >= 0 && raw < 3 ? juce::String(outs[raw]) : juce::String(raw);
    }
    if (nameHas(d.name, "note limit") || juce::String(d.name) == "Note number") return noteName(raw);
    if (nameHas(d.name, "pan")) return raw == 3 ? juce::String("C") : (raw < 3 ? "L" + juce::String(3 - raw) : "R" + juce::String(raw - 3));
    if (d.bits == 1) return raw ? "On" : "Off";
    return juce::String(v);
}

ParamControl::ParamControl(const qs::ParamDef& d, int s, int dr, Change cb, juce::String cap)
    : def(d), sound(s), drum(dr), caption(cap.isNotEmpty() ? cap : titleCase(d.name)), onChange(std::move(cb)) {
    kind = d.bits == 1 ? Kind::Toggle : (enumLike(d) || nameHas(d.name, "sample number") ? Kind::Combo : Kind::Knob);
    if (kind == Kind::Knob) {
        addAndMakeVisible(knob);
        knob.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        knob.textFromValueFunction = [this](double v) { return label(int(v) - def.offset); };
        knob.setDoubleClickReturnValue(true, double(d.offset < 0 ? 0 : d.offset));
        knob.setPopupDisplayEnabled(true, false, nullptr);
        knob.onValueChange = [this] { if (!loading && onChange) onChange(sound, def, drum, int(knob.getValue())); };
    } else if (kind == Kind::Combo) {
        addAndMakeVisible(combo);
        combo.onChange = [this] { if (!loading && onChange && combo.getSelectedId() > 0) onChange(sound, def, drum, combo.getSelectedId() - 1 + def.offset); };
    } else {
        addAndMakeVisible(toggle);
        toggle.onClick = [this] { if (!loading && onChange) onChange(sound, def, drum, toggle.getToggleState() ? 1 : 0); };
    }
}

void ParamControl::load(const qs::Program& p) {
    const juce::ScopedValueSetter<bool> svs(loading, true);
    const int raw = p.raw(sound, def, drum), lim = p.editLimit(sound, def, drum);
    if (kind == Kind::Knob) {
        knob.setRange(double(def.offset), double(def.offset + lim), 1.0);
        knob.setValue(double(raw + def.offset), juce::dontSendNotification);
    } else if (kind == Kind::Combo) {
        // Rebuild the list when its size or its names change (sample names follow the selected group).
        juce::StringArray items;
        for (int r = 0; r <= lim; ++r) items.add(label(r));
        bool same = combo.getNumItems() == items.size();
        for (int i = 0; same && i < items.size(); ++i) same = combo.getItemText(i) == items[i];
        if (!same) {
            combo.clear(juce::dontSendNotification);
            for (int r = 0; r <= lim; ++r) combo.addItem(items[r], r + 1);
        }
        combo.setSelectedId(raw + 1, juce::dontSendNotification);
    } else {
        toggle.setToggleState(raw != 0, juce::dontSendNotification);
    }
}

void ParamControl::paint(juce::Graphics& g) {
    theme::drawCaption(g, getLocalBounds().removeFromTop(28), caption);
}

void ParamControl::resized() {
    auto r = getLocalBounds();
    r.removeFromTop(28);
    if (kind == Kind::Knob) knob.setBounds(r.withSizeKeepingCentre(juce::jmin(r.getWidth(), r.getHeight()), juce::jmin(r.getWidth(), r.getHeight())));
    else if (kind == Kind::Combo) combo.setBounds(r.withSizeKeepingCentre(r.getWidth() - 4, 28));
    else toggle.setBounds(r.withSizeKeepingCentre(juce::jmin(r.getWidth(), 60), 28));
}

// ---------------- SoundPanel ----------------

SoundPanel::SoundPanel(EditorTab& e, int s) : editor(e), sound(s) {
    power.setButtonText("On");
    power.onClick = [this] { editor.setEnabled(sound, power.getToggleState()); };
    addAndMakeVisible(power);
    for (int d = 0; d < qs::kDrumsPerSound; ++d) drumPick.addItem("Drum " + juce::String(d + 1), d + 1);
    drumPick.setSelectedId(1, juce::dontSendNotification);
    drumPick.onChange = [this] {
        drumIndex = drumPick.getSelectedId() - 1;
        rebuild(true);
        load(editor.program());
    };
    addChildComponent(drumPick);
    rebuild(false);
}

ParamControl* SoundPanel::add(const qs::ParamDef& d, juce::String caption, int drumIdx) {
    auto* c = controls.add(new ParamControl(d, sound, drumIdx,
                                            [this](int s, const qs::ParamDef& def, int dr, int v) { editor.paramChanged(s, def, dr, v); }, caption));
    editor.attachLabels(*c);
    addAndMakeVisible(c);
    return c;
}

void SoundPanel::rebuild(bool drum) {
    isDrum = drum;
    controls.clear();
    details.clear();
    drumPick.setVisible(drum);
    if (!drum) {
        add(*kbd("Sample group"), "Group");
        add(*kbd("Sample number"), "Sample");
        add(*kbd("Volume"), "Volume");
        add(*kbd("Pan"), "Pan");
        add(*kbd("Effect level"), "FX send");
        add(*kbd("Effect bus"), "FX bus");
        add(*kbd("Filter frequency"), "Cutoff");
        add(*kbd("Amp env attack"), "Attack");
        add(*kbd("Amp env release"), "Release");
        add(*kbd("Key mode"), "Key mode");
        add(*kbd("Amp velocity curve"), "Vel curve");
        add(*kbd("Pitch semitone"), "Semitone");
        for (auto& sec : sections()) {
            auto* b = details.add(new juce::TextButton(sec.name));
            const juce::String name = sec.name;
            b->onClick = [this, name] { editor.showDetail(sound, name); };
            addAndMakeVisible(b);
        }
    } else {
        for (auto* n : {"Sample group", "Sample number", "Volume", "Pan", "Pitch", "Amp envelope decay", "Note number", "Effect level"})
            add(*drumDef(n), {}, drumIndex);
        auto* b = details.add(new juce::TextButton("All drum settings"));
        b->onClick = [this] { editor.showDetail(sound, "Drum", drumIndex); };
        addAndMakeVisible(b);
    }
    resized();
}

void SoundPanel::load(const qs::Program& p) {
    const bool drum = p.isDrum(sound);
    if (drum != isDrum) rebuild(drum);
    enabled = p.isEnabled(sound);
    power.setToggleState(enabled, juce::dontSendNotification);
    for (auto* c : controls) {
        c->load(p);
        c->setAlpha(enabled ? 1.0f : 0.45f);
    }
    repaint();
}

void SoundPanel::paint(juce::Graphics& g) {
    theme::drawPanel(g, getLocalBounds().toFloat(), enabled, "SOUND " + juce::String(sound + 1) + (isDrum ? "  (drum kit)" : ""));
    // Like QS Edit Pro's strip header: what this sound plays, where on the keyboard, and how.
    const auto summary = editor.soundSummary(sound);
    if (summary.isNotEmpty()) {
        auto r = getLocalBounds().reduced(12, 10).removeFromTop(26).withTrimmedLeft(isDrum ? 170 : 86).withTrimmedRight(isDrum ? 180 : 66);
        g.setColour(enabled ? theme::accent : theme::dim);
        g.setFont(theme::uiFont(13.5f, true));
        g.drawFittedText(summary, r, juce::Justification::centredLeft, 1, 0.8f);
    }
}

void SoundPanel::resized() {
    auto r = getLocalBounds().reduced(12, 10);
    auto head = r.removeFromTop(26);
    power.setBounds(head.removeFromRight(56));
    if (isDrum) drumPick.setBounds(head.removeFromRight(110).reduced(0, 1));
    r.removeFromTop(4);
    auto btnRow = r.removeFromBottom(28);
    r.removeFromBottom(6);
    const int perRow = isDrum ? 4 : 6, nRows = (controls.size() + perRow - 1) / perRow;
    const int rowH = nRows > 0 ? r.getHeight() / nRows : 0, colW = r.getWidth() / perRow;
    for (int i = 0; i < controls.size(); ++i)
        controls[i]->setBounds(r.getX() + (i % perRow) * colW, r.getY() + (i / perRow) * rowH, colW, rowH);
    if (!details.isEmpty()) {
        const int bw = btnRow.getWidth() / details.size();
        for (auto* b : details) b->setBounds(btnRow.removeFromLeft(bw).reduced(2, 0));
    }
}

// ---------------- EditorTab ----------------

EditorTab::EditorTab(Session& s) : session(s) {
    addAndMakeVisible(lcd);
    nameEdit.setInputRestrictions(10);
    nameEdit.setFont(theme::monoFont(18.0f, true));
    nameEdit.setTextToShowWhenEmpty("Name", theme::dim);
    nameEdit.onTextChange = [this] {
        prog.setName(nameEdit.getText().toStdString());
        dumpDirty = true;
        lastDumpEdit = juce::Time::getMillisecondCounter();
        updateLcd();
        edited("Name");
    };
    addAndMakeVisible(nameEdit);
    liveToggle.setToggleState(settings().getBoolValue("liveEdit", true), juce::dontSendNotification);
    liveToggle.setTooltip("Send each change to the QS as you make it");
    liveToggle.onClick = [this] { settings().setValue("liveEdit", liveToggle.getToggleState()); };
    for (juce::Component* c : std::initializer_list<juce::Component*>{&liveToggle, &getBtn, &sendBtn, &storeBtn, &openBtn, &saveBtn, &fxBtn,
                                                                    &compareBtn, &undoBtns.undo, &undoBtns.redo, &toolsBtn})
        addAndMakeVisible(c);
    undoBtns.undo.onClick = [this] { if (auto st = history.undo()) applyState(*st); };
    undoBtns.redo.onClick = [this] { if (auto st = history.redo()) applyState(*st); };
    toolsBtn.setTooltip("Copy, paste, swap or reset sounds; start a new program; randomize or blend programs.");
    toolsBtn.onClick = [this] { showTools(); };
    setWantsKeyboardFocus(false);
    history.reset(prog.bytes());
    undoBtns.refresh(history);
    fxBtn.setTooltip("Opens the effect this program is playing through, in the Effects editor.");
    fxBtn.onClick = [this] {
        if (editNumber == 0) session.effectsProgram = prog;
        if (session.openEffectsEditor) session.openEffectsEditor(qs::Effects(), juce::String(prog.name()).trimEnd() + " effect", editNumber > 0 ? 1 : 0);
    };
    compareBtn.setClickingTogglesState(true);
    compareBtn.setTooltip("Hear the program as it was before your edits (like the QS's COMPARE key). Click again to go back to your edits.");
    compareBtn.onClick = [this] { setComparing(compareBtn.getToggleState()); };
    addChildComponent(backBtn);
    getBtn.setTooltip("Loads whatever program the QS is playing right now (its edit buffer).");
    sendBtn.setTooltip("Sends this program to the QS edit buffer so you hear it. Not stored: switching programs on the QS discards it.");
    storeBtn.setTooltip("Saves this program permanently into one of the QS's 128 User program slots, replacing what was there.");
    openBtn.setTooltip("Opens a program from a .syx file on your computer.");
    saveBtn.setTooltip("Saves this program as a .syx file on your computer.");
    getBtn.onClick = [this] { getFromQs(); };
    sendBtn.onClick = [this] { sendToQs(); };
    storeBtn.onClick = [this] { storeToUser(); };
    openBtn.onClick = [this] { openSyx(); };
    saveBtn.onClick = [this] { saveSyx(); };
    backBtn.onClick = [this] { showOverview(); };

    for (int i = 0; i < qs::kSoundCount; ++i) addAndMakeVisible(panels.add(new SoundPanel(*this, i)));
    for (int i = 0; i < qs::kSoundCount; ++i) {
        auto* b = soundTabs.add(new juce::TextButton("Sound " + juce::String(i + 1)));
        b->onClick = [this, i] { showDetail(i, prog.isDrum(i) ? juce::String("Drum") : (detailSection == "Drum" ? juce::String("Voice") : detailSection), detailDrum); };
        addChildComponent(b);
    }
    for (auto& sec : sections()) {
        auto* b = sectionTabs.add(new juce::TextButton(sec.name));
        const juce::String name = sec.name;
        b->onClick = [this, name] { showDetail(detailSound, name); };
        addChildComponent(b);
    }
    detailView.setViewedComponent(&detailContent, false);
    detailView.setScrollBarsShown(true, false);
    addChildComponent(detailView);

    session.midi.addListener(this);
    startTimer(30);
    loadAll();
}

EditorTab::~EditorTab() { session.midi.removeListener(this); }

void EditorTab::openMixChannel(int ch, const juce::String& mixName) {
    editNumber = ch + 1;
    where = "Mix " + mixName + " ch " + juce::String(ch + 1);
    pending.clear();
    dumpDirty = false;
    qslog::action("Program editor: editing the program on mix channel " + juce::String(ch + 1));
    if (session.midi.isOpen()) requestEditBuffer(200);
    loadAll();
}

void EditorTab::openProgram(const qs::Program& p, const juce::String& w) {
    editNumber = 0;
    prog = p;
    original = p;
    setComparing(false);
    history.reset(prog.bytes());
    undoBtns.refresh(history);
    where = w;
    pending.clear();
    dumpDirty = false;
    awaitingDump = false;
    if (session.midi.isOpen() && !w.endsWith("(offline copy)") && !w.endsWithIgnoreCase(".syx")) requestEditBuffer(300);
    loadAll();
}

// Ask the QS for its edit buffer, repeating a few times: right after a program change the QS can miss the request.
void EditorTab::requestEditBuffer(int firstDelayMs) {
    awaitingDump = true;
    requestsSent = 0;
    nextRequestAt = juce::Time::getMillisecondCounter() + juce::uint32(juce::jmax(0, firstDelayMs));
    updateLcd();
}

void EditorTab::loadAll() {
    {
        const juce::ScopedValueSetter<std::function<void()>> noNotify(nameEdit.onTextChange, nullptr);
        nameEdit.setText(juce::String(prog.name()).trimEnd(), false);
    }
    for (auto* p : panels) p->load(prog);
    for (auto* c : detailControls) c->load(prog);
    updateLcd();
}

void EditorTab::updateLcd() {
    int on = 0;
    for (int i = 0; i < qs::kSoundCount; ++i) on += prog.isEnabled(i);
    lcd.setLines(juce::String(prog.name()).trimEnd(),
                 {where.toUpperCase(), "SOUNDS ON: " + juce::String(on) + " OF 4   " + juce::String(qs::romSetName(session.romSet())) + " SAMPLES" +
                                           (awaitingDump ? juce::String("   WAITING FOR QS...") : comparing ? juce::String("   COMPARE: ORIGINAL") : juce::String())});
}

juce::String EditorTab::soundSummary(int sound) const {
    if (prog.isDrum(sound)) return {};
    auto val = [&](const char* name) {
        const int i = qs::findKeyboardParam(name);
        return i >= 0 ? prog.raw(sound, qs::keyboardParam(i), 0) : 0;
    };
    const int group = val("Sample group"), number = val("Sample number");
    const auto rs = session.romSet();
    juce::String sample = number == 0 ? juce::String("No sample") : juce::String(qs::sampleName(rs, false, group, number));
    if (sample.isEmpty()) sample = "Sample " + juce::String(number);
    const juce::String groupName = qs::sampleGroupName(group);
    static const char* modes[] = {"Mono", "Poly", "1-Pitch", "1-PMono"};
    const int km = val("Key mode");
    return sample + (groupName.isNotEmpty() ? "  (" + groupName + ")" : juce::String()) + "   " + noteName(val("Low note limit")) + " to " +
           noteName(val("High note limit")) + "   " + (km >= 0 && km < 4 ? modes[km] : "");
}

void EditorTab::attachLabels(ParamControl& c) {
    const bool isGroup = nameHas(c.def.name, "sample group"), isNumber = nameHas(c.def.name, "sample number");
    if (!isGroup && !isNumber) return;
    const int sound = c.sound, drum = c.drum;
    const auto* def = &c.def;
    c.labelFn = [this, sound, drum, def, isGroup](int raw) -> juce::String {
        const bool drumSound = prog.isDrum(sound);
        const auto rs = session.romSet();
        if (isGroup) {
            if (!drumSound) return ParamControl::valueLabel(*def, raw);
            const juce::String n = qs::drumGroupName(rs, raw);
            return juce::String(raw) + (n.isNotEmpty() ? " " + n : juce::String());
        }
        // sample number: look up the name in the current group
        const qs::ParamDef* g = nullptr;
        if (drumSound) {
            for (int i = 0; i < qs::drumParamCount(); ++i)
                if (juce::String(qs::drumParam(i).name) == "Sample group") g = &qs::drumParam(i);
        } else {
            g = &qs::keyboardParam(qs::findKeyboardParam("Sample group"));
        }
        const int group = g ? prog.raw(sound, *g, drumSound ? drum : 0) : 0;
        if (raw == 0) return "00  (no sample)";
        const juce::String name = qs::sampleName(rs, drumSound, group, raw);
        return juce::String(raw).paddedLeft('0', 2) + (name.isNotEmpty() ? "  " + name : juce::String());
    };
}

void EditorTab::paramChanged(int sound, const qs::ParamDef& d, int drum, int value) {
    prog.setValue(sound, d, value, drum);
    if (sound >= 0 && sound < panels.size()) panels[sound]->repaint();  // its header names the sample
    const bool drumParam = prog.isDrum(sound);
    if (drumParam) {
        dumpDirty = true;
        lastDumpEdit = juce::Time::getMillisecondCounter();
    } else {
        pending[{sound, &d}] = prog.value(sound, d, drum);
    }
    // Keep the overview and the detail page in sync.
    for (auto* p : panels) p->load(prog);
    for (auto* c : detailControls)
        if (&c->def != &d || c->sound != sound) c->load(prog);
    updateLcd();
    edited(juce::String(d.name) + " (sound " + juce::String(sound + 1) + ")");
}

void EditorTab::setEnabled(int sound, bool on) {
    if (prog.isDrum(sound)) {
        static const qs::ParamDef drumEnable{"Sound enable", 81 * 8, 1, 0, 1, 9, 0, 3};  // spec: drum sound 81:0
        prog.setRaw(sound, drumEnable, on ? 1 : 0);
        dumpDirty = true;
        lastDumpEdit = juce::Time::getMillisecondCounter();
    } else {
        const auto* d = kbd("Enable");
        prog.setRaw(sound, *d, on ? 1 : 0);
        pending[{sound, d}] = on ? 1 : 0;
    }
    panels[sound]->load(prog);
    updateLcd();
    edited("Sound " + juce::String(sound + 1) + (on ? " on" : " off"));
}

void EditorTab::timerCallback() {
    const auto now = juce::Time::getMillisecondCounter();
    if (awaitingDump && now >= nextRequestAt) {
        if (requestsSent < 4 && session.midi.isOpen()) {
            session.midi.send(qs::editProgramRequest(editNumber));
            ++requestsSent;
            nextRequestAt = now + 900;
        } else {
            awaitingDump = false;
            updateLcd();
            if (session.setStatus)
                session.setStatus("The QS didn't send its edit buffer. Check that it's in Program mode and that both MIDI cables are connected.");
        }
    }
    if (!liveToggle.getToggleState() || !session.midi.isOpen()) return;
    if (dumpDirty && juce::Time::getMillisecondCounter() - lastDumpEdit > 250) {
        dumpDirty = false;
        pending.clear();  // the full dump carries every change
        session.midi.send(prog.editDump(editNumber));
        return;
    }
    for (auto& [key, value] : pending)
        session.midi.send(qs::paramEdit(2, key.second->func, key.first, key.second->page, editNumber > 0 ? editNumber - 1 : 0, key.second->pot, value));
    pending.clear();
}

void EditorTab::flushNow() {
    pending.clear();
    dumpDirty = false;
}

void EditorTab::sysexReceived(const qs::Bytes& msg) {
    if (!awaitingDump || qs::opcodeOf(msg) != qs::EditProgramDump || msg.size() < 7 || msg[6] != editNumber) return;
    if (auto p = qs::Program::fromDump(msg)) {
        awaitingDump = false;
        prog = *p;
        original = *p;
        setComparing(false);
        flushNow();
        loadAll();
        history.reset(prog.bytes());
        undoBtns.refresh(history);
        if (session.setStatus) session.setStatus("Loaded " + juce::String(prog.name()).trimEnd() + " from the QS edit buffer.");
    }
}

void EditorTab::setComparing(bool on) {
    if (on == comparing) return;
    comparing = on;
    compareBtn.setToggleState(on, juce::dontSendNotification);
    if (session.midi.isOpen()) {
        flushNow();
        session.midi.send((on ? original : prog).editDump(editNumber));
    }
    for (auto* p : panels) p->setEnabled(!on);
    detailView.setEnabled(!on);
    updateLcd();
}

void EditorTab::getFromQs() {
    if (!session.midi.isOpen()) { alert("Not connected", "Connect to the QS on the Connection page first."); return; }
    if (editNumber == 0) where = "QS edit buffer";
    requestEditBuffer(0);
}

void EditorTab::sendToQs() {
    if (!session.midi.isOpen()) { alert("Not connected", "Connect to the QS on the Connection page first."); return; }
    flushNow();
    session.midi.send(qs::modeSelect(editNumber > 0 ? 1 : 0));
    session.midi.send(prog.editDump(editNumber));
    if (session.setStatus) session.setStatus("Sent " + juce::String(prog.name()).trimEnd() + " to the QS edit buffer (not stored).");
}

void EditorTab::storeToUser() {
    if (!session.midi.isOpen()) { alert("Not connected", "Connect to the QS on the Connection page first."); return; }
    auto* w = new juce::AlertWindow("Store to User bank",
                                    "Store \"" + juce::String(prog.name()).trimEnd() + "\" into which User program?\n"
                                    "This overwrites that User program on the QS. Back up first with SysEx > Ask QS to Send All.",
                                    juce::MessageBoxIconType::WarningIcon);
    w->addTextEditor("num", juce::String(session.currentBank == 0 ? session.currentProgram : 0), "User program (0-127)");
    w->addButton("Store", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
        if (r != 1) return;
        const int n = w->getTextEditorContents("num").getIntValue();
        if (n < 0 || n > 127) { alert("Store", "Pick a number from 0 to 127."); return; }
        flushNow();
        session.midi.send(prog.userDump(n));
        session.library.programs[0][size_t(n)] = prog;
        session.library.save();
        where = "User " + juce::String(n).paddedLeft('0', 3);
        updateLcd();
        if (session.setStatus) session.setStatus("Stored " + juce::String(prog.name()).trimEnd() + " to " + where + ".");
    }), true);
}

void EditorTab::saveSyx() {
    const auto fname = juce::File::createLegalFileName(juce::String(prog.name()).trimEnd().isEmpty() ? juce::String("program") : juce::String(prog.name()).trimEnd());
    chooser = std::make_unique<juce::FileChooser>("Save program", settingsDir().getChildFile(fname + ".syx"), "*.syx");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        const auto d = prog.editDump(0);
        f.replaceWithData(d.data(), d.size());
    });
}

void EditorTab::openSyx() {
    chooser = std::make_unique<juce::FileChooser>("Open a program", settingsDir(), "*.syx;*.SYX");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (!f.existsAsFile()) return;
        juce::MemoryBlock mb;
        f.loadFileAsData(mb);
        auto ptr = static_cast<const uint8_t*>(mb.getData());
        for (auto& m : qs::splitSysex(qs::Bytes(ptr, ptr + mb.getSize())))
            if (auto p = qs::Program::fromDump(m)) {
                openProgram(*p, f.getFileName());
                return;
            }
        alert("No program", f.getFileName() + " has no QS program in it.");
    });
}

void EditorTab::showOverview() {
    detailMode = false;
    detailControls.clear();
    detailHeaders.clear();
    detailGroupStart.clear();
    resized();
}

void EditorTab::showDetail(int sound, const juce::String& section, int drum) {
    detailMode = true;
    detailSound = sound;
    detailDrum = drum;
    detailSection = prog.isDrum(sound) ? juce::String("Drum") : (section == "Drum" ? juce::String("Voice") : section);
    detailControls.clear();
    detailHeaders.clear();
    detailGroupStart.clear();
    auto change = [this](int s, const qs::ParamDef& d, int dr, int v) { paramChanged(s, d, dr, v); };
    auto startGroup = [this](const juce::String& title) {
        detailGroupStart.push_back(detailControls.size());
        auto* l = detailHeaders.add(new juce::Label({}, title.toUpperCase()));
        l->setFont(theme::uiFont(13.0f, true));
        l->setColour(juce::Label::textColourId, theme::accent);
        detailContent.addAndMakeVisible(l);
    };
    // Captions drop the prefix the row title already says ("Pitch env attack" under PITCH ENV reads "Attack").
    auto shortCaption = [](juce::String name, const juce::String& prefix) {
        if (prefix.isNotEmpty() && name.startsWithIgnoreCase(prefix + " ")) name = name.substring(prefix.length() + 1);
        return titleCase(name);
    };
    if (detailSection == "Drum") {
        for (int dr = 0; dr < qs::kDrumsPerSound; ++dr) {
            startGroup("Drum " + juce::String(dr + 1));
            for (int i = 0; i < qs::drumParamCount(); ++i)
            {
                auto* c = detailControls.add(new ParamControl(qs::drumParam(i), sound, dr, change));
                attachLabels(*c);
                detailContent.addAndMakeVisible(c);
            }
        }
    } else {
        static const char* prefixes[] = {"", "", "", "", "", "", "", "", "", "Pitch lfo", "Filter lfo", "Amp lfo",
                                         "Pitch env", "Filter env", "Amp env", "Tracking", ""};
        for (auto& sec : sections())
            if (detailSection == sec.name)
                for (int f : sec.funcs) {
                    startGroup(qs::functionName(f));
                    for (int i = 0; i < qs::keyboardParamCount(); ++i) {
                        const auto& d = qs::keyboardParam(i);
                        if (d.func == f && !nameHas(d.name, "drum mode"))
                        {
                            auto* c = detailControls.add(new ParamControl(d, sound, 0, change, shortCaption(d.name, prefixes[f])));
                            attachLabels(*c);
                            detailContent.addAndMakeVisible(c);
                        }
                    }
                }
    }
    for (auto* c : detailControls) c->load(prog);
    for (int i = 0; i < soundTabs.size(); ++i) soundTabs[i]->setToggleState(i == sound, juce::dontSendNotification);
    for (auto* b : sectionTabs) {
        b->setToggleState(b->getButtonText() == detailSection, juce::dontSendNotification);
        b->setEnabled(!prog.isDrum(sound));
    }
    resized();
}

void EditorTab::paint(juce::Graphics& g) {
    g.fillAll(theme::bg);
    if (detailMode) {
        auto r = getLocalBounds().reduced(14);
        r.removeFromTop(118 + 10 + 36 + 6 + 36 + 8);
        theme::drawPanel(g, r.toFloat(), true);
    }
}

void EditorTab::resized() {
    auto r = getLocalBounds().reduced(14);
    auto top = r.removeFromTop(124);
    lcd.setBounds(top.removeFromLeft(juce::jmin(560, top.getWidth() / 2)).withHeight(118));
    top.removeFromLeft(16);
    auto row1 = top.removeFromTop(34);
    nameEdit.setBounds(row1.removeFromLeft(180));
    row1.removeFromLeft(10);
    liveToggle.setBounds(row1.removeFromLeft(110));
    top.removeFromTop(6);
    auto row2 = top.removeFromTop(34);
    for (auto* b : {&getBtn, &sendBtn, &storeBtn, &openBtn, &saveBtn}) {
        b->setBounds(row2.removeFromLeft(juce::jmin(130, row2.getWidth())));
        row2.removeFromLeft(8);
    }
    row1.removeFromLeft(10);
    compareBtn.setBounds(row1.removeFromLeft(100));
    row1.removeFromLeft(8);
    fxBtn.setBounds(row1.removeFromLeft(100));
    top.removeFromTop(6);
    auto row3 = top.removeFromTop(30);
    undoBtns.undo.setBounds(row3.removeFromLeft(80));
    row3.removeFromLeft(6);
    undoBtns.redo.setBounds(row3.removeFromLeft(80));
    row3.removeFromLeft(14);
    toolsBtn.setBounds(row3.removeFromLeft(120));
    r.removeFromTop(10);

    for (auto* p : panels) p->setVisible(!detailMode);
    backBtn.setVisible(detailMode);
    detailView.setVisible(detailMode);
    for (auto* b : soundTabs) b->setVisible(detailMode);
    for (auto* b : sectionTabs) b->setVisible(detailMode);

    if (!detailMode) {
        const int gap = 12, w = (r.getWidth() - gap) / 2, h = (r.getHeight() - gap) / 2;
        for (int i = 0; i < panels.size(); ++i)
            panels[i]->setBounds(r.getX() + (i % 2) * (w + gap), r.getY() + (i / 2) * (h + gap), w, h);
        return;
    }
    auto tabs1 = r.removeFromTop(36);
    backBtn.setBounds(tabs1.removeFromLeft(130));
    tabs1.removeFromLeft(16);
    for (auto* b : soundTabs) {
        b->setBounds(tabs1.removeFromLeft(100));
        tabs1.removeFromLeft(6);
    }
    r.removeFromTop(6);
    auto tabs2 = r.removeFromTop(36);
    for (auto* b : sectionTabs) {
        b->setBounds(tabs2.removeFromLeft(110));
        tabs2.removeFromLeft(6);
    }
    r.removeFromTop(8);
    detailView.setBounds(r.reduced(10));
    const int cellW = 116, cellH = 112, headerH = 24, cols = juce::jmax(1, (detailView.getWidth() - 16) / cellW);
    int y = 0;
    for (size_t g = 0; g < detailGroupStart.size(); ++g) {
        const int from = detailGroupStart[g], to = g + 1 < detailGroupStart.size() ? detailGroupStart[g + 1] : detailControls.size();
        detailHeaders[int(g)]->setBounds(4, y, cols * cellW, headerH);
        y += headerH;
        for (int i = from; i < to; ++i) detailControls[i]->setBounds(((i - from) % cols) * cellW, y + ((i - from) / cols) * cellH, cellW, cellH);
        y += ((to - from + cols - 1) / cols) * cellH + 8;
    }
    detailContent.setSize(cols * cellW, y);
}

// ---------------- undo, tools ----------------

void EditorTab::edited(const juce::String& label) {
    history.changed(prog.bytes(), label);
    undoBtns.refresh(history);
}

void EditorTab::applyState(const qs::Bytes& state) {
    if (auto p = qs::Program::fromData(state)) prog = *p;
    pending.clear();
    loadAll();
    if (detailMode) showDetail(detailSound, detailSection, detailDrum);
    if (liveToggle.getToggleState() && session.midi.isOpen()) session.midi.send(prog.editDump(editNumber));
    dumpDirty = false;
    undoBtns.refresh(history);
}

void EditorTab::replaceProgram(const qs::Program& p, const juce::String& label) {
    prog = p;
    history.changed(prog.bytes(), label);
    applyState(prog.bytes());
    qslog::action("Program editor: " + label);
}

bool EditorTab::keyPressed(const juce::KeyPress& k) {
    if (isUndoKey(k)) { if (auto st = history.undo()) applyState(*st); return true; }
    if (isRedoKey(k)) { if (auto st = history.redo()) applyState(*st); return true; }
    return false;
}

void EditorTab::showTools() {
    juce::PopupMenu copy, paste, swap, init;
    for (int s = 0; s < qs::kSoundCount; ++s) {
        const auto n = "Sound " + juce::String(s + 1) + (prog.isDrum(s) ? " (drums)" : "") + (prog.isEnabled(s) ? "" : " (off)");
        copy.addItem(100 + s, n);
        paste.addItem(200 + s, n, soundClipboard.has_value());
        init.addItem(400 + s, n);
        for (int t = s + 1; t < qs::kSoundCount; ++t) swap.addItem(300 + s * 4 + t, "Sounds " + juce::String(s + 1) + " and " + juce::String(t + 1));
    }
    juce::PopupMenu m;
    m.addSectionHeader("Sounds");
    m.addSubMenu("Copy a sound", copy);
    m.addSubMenu("Paste the copied sound into", paste, soundClipboard.has_value());
    m.addSubMenu("Swap two sounds", swap);
    m.addSubMenu("Reset a sound to a plain default", init);
    m.addSectionHeader("Programs");
    m.addItem(1, "New program from scratch");
    m.addItem(2, "Randomize this program...");
    m.addItem(3, "Blend with another program...");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&toolsBtn), [this](int r) {
        if (r >= 100 && r < 104) {
            soundClipboard = prog.sound(r - 100);
            if (session.setStatus) session.setStatus("Copied sound " + juce::String(r - 99) + ". Tools > Paste puts it into another sound.");
        } else if (r >= 200 && r < 204 && soundClipboard) {
            auto p = prog;
            p.setSound(r - 200, *soundClipboard);
            replaceProgram(p, "paste into sound " + juce::String(r - 199));
        } else if (r >= 300 && r < 316) {
            const int a = (r - 300) / 4, b = (r - 300) % 4;
            auto p = prog;
            p.swapSounds(a, b);
            replaceProgram(p, "swap sounds " + juce::String(a + 1) + " and " + juce::String(b + 1));
        } else if (r >= 400 && r < 404) {
            auto p = prog;
            p.initSound(r - 400, true);
            replaceProgram(p, "reset sound " + juce::String(r - 399));
        } else if (r == 1) {
            replaceProgram(qs::Program::initProgram(), "new program");
            where = "New program";
            updateLcd();
        } else if (r == 2) {
            randomizeDialog();
        } else if (r == 3) {
            blendDialog();
        }
    });
}

void EditorTab::randomizeDialog() {
    auto* w = new juce::AlertWindow("Randomize", "Nudges every amount (envelopes, filter, LFOs, levels, modulation) by a random step. "
                                                 "Samples, volume and key ranges stay as they are. Undo takes it back.",
                                    juce::MessageBoxIconType::QuestionIcon);
    w->addComboBox("amount", {"A little (10%)", "Some (25%)", "A lot (50%)", "Wild (100%)"}, "How much");
    w->getComboBoxComponent("amount")->setSelectedItemIndex(1);
    w->addComboBox("choices", {"Keep waveforms, modes and routings", "Change those too"}, "Choices");
    w->addButton("Randomize", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
        if (r != 1) return;
        static const double amounts[] = {0.1, 0.25, 0.5, 1.0};
        const double amount = amounts[juce::jlimit(0, 3, w->getComboBoxComponent("amount")->getSelectedItemIndex())];
        const bool choices = w->getComboBoxComponent("choices")->getSelectedItemIndex() == 1;
        replaceProgram(qs::randomizeProgram(prog, amount, choices, juce::Random::getSystemRandom().nextInt() | 1), "randomize");
    }), true);
}

void EditorTab::blendDialog() {
    // Programs QS Studio has the data for: every bank in the library.
    struct Choice {
        int bank, number;
    };
    auto choices = std::make_shared<std::vector<Choice>>();
    juce::StringArray names;
    for (int b = 0; b < int(kBanks.size()); ++b)
        for (int n = 0; n < 128; ++n)
            if (session.library.programs[size_t(b)][size_t(n)]) {
                choices->push_back({b, n});
                names.add(juce::String(kBanks[size_t(b)].title) + " " + juce::String(n).paddedLeft('0', 3) + "  " + session.library.nameOf(b, n));
            }
    if (names.isEmpty()) {
        alert("Nothing to blend with", "Read a bank from the QS (Programs page) or load a .syx file first.");
        return;
    }
    auto* w = new juce::AlertWindow("Blend", "Makes a program between this one and another. Amounts are mixed; samples, waveforms and "
                                             "modes come from whichever program is nearer. Undo takes it back.",
                                    juce::MessageBoxIconType::QuestionIcon);
    w->addComboBox("other", names, "Blend with");
    w->addComboBox("amount", {"10% of the other", "25%", "50% (half way)", "75%", "90%"}, "How far");
    w->getComboBoxComponent("amount")->setSelectedItemIndex(2);
    w->addButton("Blend", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w, choices](int r) {
        if (r != 1) return;
        const int i = w->getComboBoxComponent("other")->getSelectedItemIndex();
        if (i < 0 || i >= int(choices->size())) return;
        static const double ts[] = {0.1, 0.25, 0.5, 0.75, 0.9};
        const double t = ts[juce::jlimit(0, 4, w->getComboBoxComponent("amount")->getSelectedItemIndex())];
        const auto& other = *session.library.programs[size_t((*choices)[size_t(i)].bank)][size_t((*choices)[size_t(i)].number)];
        auto out = qs::blendPrograms(prog, other, t);
        if (t >= 0.5) out.setName(prog.name());  // keep this program's name either way
        replaceProgram(out, "blend");
    }), true);
}
