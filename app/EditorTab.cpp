#include "EditorTab.h"

namespace {

void alert(const juce::String& title, const juce::String& msg) {
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

juce::String ParamControl::valueLabel(const qs::ParamDef& d, int raw) {
    const int v = raw + d.offset;
    if (nameHas(d.name, "waveform")) {
        juce::String n = qs::lfoWaveName(raw);
        return n.isNotEmpty() ? n : juce::String(raw);
    }
    if (nameHas(d.name, "sample group")) {
        juce::String n = qs::sampleGroupName(raw);
        return n.isNotEmpty() ? juce::String(raw) + " " + n : "Group " + juce::String(raw);
    }
    if (nameHas(d.name, "effect bus")) return "Bus " + juce::String(raw + 1);
    if (nameHas(d.name, "velocity curve")) return "Curve " + juce::String(raw + 1);
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
    };
    addAndMakeVisible(nameEdit);
    liveToggle.setToggleState(settings().getBoolValue("liveEdit", true), juce::dontSendNotification);
    liveToggle.setTooltip("Send each change to the QS as you make it");
    liveToggle.onClick = [this] { settings().setValue("liveEdit", liveToggle.getToggleState()); };
    for (juce::Component* c : std::initializer_list<juce::Component*>{&liveToggle, &getBtn, &sendBtn, &storeBtn, &openBtn, &saveBtn})
        addAndMakeVisible(c);
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

void EditorTab::openProgram(const qs::Program& p, const juce::String& w) {
    prog = p;
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
                                           (awaitingDump ? juce::String("   WAITING FOR QS...") : juce::String())});
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
        const juce::String name = qs::sampleName(rs, drumSound, group, raw);
        return juce::String(raw).paddedLeft('0', 2) + (name.isNotEmpty() ? "  " + name : juce::String());
    };
}

void EditorTab::paramChanged(int sound, const qs::ParamDef& d, int drum, int value) {
    prog.setValue(sound, d, value, drum);
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
}

void EditorTab::timerCallback() {
    const auto now = juce::Time::getMillisecondCounter();
    if (awaitingDump && now >= nextRequestAt) {
        if (requestsSent < 4 && session.midi.isOpen()) {
            session.midi.send(qs::editProgramRequest(0));
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
        session.midi.send(prog.editDump());
        return;
    }
    for (auto& [key, value] : pending) session.midi.send(qs::paramEdit(*key.second, key.first, value));
    pending.clear();
}

void EditorTab::flushNow() {
    pending.clear();
    dumpDirty = false;
}

void EditorTab::sysexReceived(const qs::Bytes& msg) {
    if (!awaitingDump || qs::opcodeOf(msg) != qs::EditProgramDump || msg.size() < 7 || msg[6] != 0) return;
    if (auto p = qs::Program::fromDump(msg)) {
        awaitingDump = false;
        prog = *p;
        flushNow();
        loadAll();
        if (session.setStatus) session.setStatus("Loaded " + juce::String(prog.name()).trimEnd() + " from the QS edit buffer.");
    }
}

void EditorTab::getFromQs() {
    if (!session.midi.isOpen()) { alert("Not connected", "Connect to the QS on the Device tab first."); return; }
    where = "QS edit buffer";
    requestEditBuffer(0);
}

void EditorTab::sendToQs() {
    if (!session.midi.isOpen()) { alert("Not connected", "Connect to the QS on the Device tab first."); return; }
    flushNow();
    session.midi.send(qs::modeSelect(0));
    session.midi.send(prog.editDump());
    if (session.setStatus) session.setStatus("Sent " + juce::String(prog.name()).trimEnd() + " to the QS edit buffer (not stored).");
}

void EditorTab::storeToUser() {
    if (!session.midi.isOpen()) { alert("Not connected", "Connect to the QS on the Device tab first."); return; }
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
        const auto d = prog.editDump();
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
    auto top = r.removeFromTop(118);
    lcd.setBounds(top.removeFromLeft(juce::jmin(560, top.getWidth() / 2)));
    top.removeFromLeft(16);
    auto row1 = top.removeFromTop(36);
    nameEdit.setBounds(row1.removeFromLeft(180));
    row1.removeFromLeft(10);
    liveToggle.setBounds(row1.removeFromLeft(110));
    top.removeFromTop(10);
    auto row2 = top.removeFromTop(36);
    for (auto* b : {&getBtn, &sendBtn, &storeBtn, &openBtn, &saveBtn}) {
        b->setBounds(row2.removeFromLeft(juce::jmin(150, row2.getWidth())));
        row2.removeFromLeft(8);
    }
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
