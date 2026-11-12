#include "PlayerTab.h"

namespace {
void alert(const juce::String& title, const juce::String& msg) {
    qslog::action("Message: " + title + ": " + msg.replace("\n", " / "));
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, title, msg);
}
}  // namespace

PlayerTab::PlayerTab(Session& s) : session(s) {
    collector.reset(48000.0);  // until the audio device starts
    for (juce::Component* c : std::initializer_list<juce::Component*>{&lcd, &sourceSec, &soundSec, &openBtn, &audioBtn, &fileLabel, &countLabel,
                                                                    &search, &list, &attack, &release, &volume, &attackLabel, &releaseLabel,
                                                                    &volumeLabel, &alsoQs, &decay, &sustain, &decayLabel, &sustainLabel})
        addAndMakeVisible(c);
    openBtn.onClick = [this] { openFile(); };
    audioBtn.onClick = [this] { showAudioSettings(); };
    audioBtn.setTooltip("Pick the audio interface the sound plays through (shared with Record & Sample).");
    search.setTextToShowWhenEmpty("Search voices...", theme::dim);
    search.onTextChange = [this] { filter(); };
    list.setRowHeight(26);
    list.setColour(juce::ListBox::backgroundColourId, theme::panel);
    for (auto* l : {&fileLabel, &countLabel, &attackLabel, &decayLabel, &sustainLabel, &releaseLabel, &volumeLabel}) l->setColour(juce::Label::textColourId, theme::dim);
    auto setup = [](juce::Slider& sl, double lo, double hi, double v, const juce::String& suffix) {
        sl.setSliderStyle(juce::Slider::LinearBar);
        sl.setRange(lo, hi, 0.001);
        sl.setValue(v, juce::dontSendNotification);
        sl.setTextValueSuffix(suffix);
        sl.setNumDecimalPlacesToDisplay(suffix == " s" ? 3 : 2);
    };
    setup(attack, 0.0, 2.0, settings().getDoubleValue("playerAttack", 0.002), " s");
    setup(decay, 0.01, 10.0, settings().getDoubleValue("playerDecay", 1.0), " s");
    setup(sustain, 0.0, 1.0, settings().getDoubleValue("playerSustain", 1.0), "");
    setup(release, 0.0, 5.0, settings().getDoubleValue("playerRelease", 0.35), " s");
    setup(volume, 0.0, 1.0, settings().getDoubleValue("playerVolume", 0.5), "");
    attack.setSkewFactorFromMidPoint(0.2);
    release.setSkewFactorFromMidPoint(0.8);
    decay.setSkewFactorFromMidPoint(1.0);
    sustain.setTooltip("Level while the key is held. 1 holds the sample's loop at full level; lower it (with Decay) for "
                       "pianos, mallets and plucks, which the QS fades with the program's envelope.");
    auto apply = [this] {
        engine.settings.attack = float(attack.getValue());
        engine.settings.release = float(release.getValue());
        engine.settings.decay = float(decay.getValue());
        engine.settings.sustain = float(sustain.getValue());
        settings().setValue("playerDecay", decay.getValue());
        settings().setValue("playerSustain", sustain.getValue());
        engine.settings.gain = float(volume.getValue());
        settings().setValue("playerAttack", attack.getValue());
        settings().setValue("playerRelease", release.getValue());
        settings().setValue("playerVolume", volume.getValue());
    };
    attack.onValueChange = release.onValueChange = volume.onValueChange = decay.onValueChange = sustain.onValueChange = apply;
    apply();
    alsoQs.setToggleState(settings().getBoolValue("playerAlsoQs", false), juce::dontSendNotification);
    alsoQs.setTooltip("Off: the keyboard and your controller play only these samples. On: they play the QS as well.");
    alsoQs.onClick = [this] {
        settings().setValue("playerAlsoQs", alsoQs.getToggleState());
        if (listening) session.notesToQs = alsoQs.getToggleState();
    };
    updateInfo();
}

PlayerTab::~PlayerTab() {
    if (listening) {
        session.keys.removeListener(this);
        session.playerInput = nullptr;
        session.notesToQs = true;
        session.audio().removeAudioCallback(this);
    }
}

// Listen (keyboard, controller, audio) only while the page is showing, so other pages play the QS as before.
void PlayerTab::visibilityChanged() {
    const bool show = isShowing();
    if (show == listening) return;
    listening = show;
    if (show) {
        if (image == nullptr) {
            const juce::File last(settings().getValue("playerFile"));
            if (last.existsAsFile()) load(last);
        }
        session.audio().addAudioCallback(this);
        session.keys.addListener(this);
        session.playerInput = &collector;
        session.notesToQs = alsoQs.getToggleState();
    } else {
        session.keys.removeListener(this);
        session.playerInput = nullptr;
        session.notesToQs = true;
        session.audio().removeAudioCallback(this);
        engine.allNotesOff();
    }
    updateInfo();
}

void PlayerTab::audioDeviceAboutToStart(juce::AudioIODevice* d) {
    engine.prepare(d->getCurrentSampleRate());
    collector.reset(d->getCurrentSampleRate());
    scratch.setSize(2, d->getCurrentBufferSizeSamples() * 2);
}

void PlayerTab::audioDeviceIOCallbackWithContext(const float* const*, int, float* const* out, int numOut, int n,
                                                 const juce::AudioIODeviceCallbackContext&) {
    // Write the outputs, never add to them: the device manager hands a second callback a scratch buffer that still
    // holds the previous block, and sums it into the device itself. (Adding here fed each block back into the next:
    // a whine that grew with every note and didn't stop.)
    for (int ch = 0; ch < numOut; ++ch)
        if (out[ch] != nullptr) juce::FloatVectorOperations::clear(out[ch], n);
    if (scratch.getNumSamples() < n) scratch.setSize(2, n, false, false, true);  // rare: a bigger block than announced
    scratch.clear(0, n);
    juce::MidiBuffer midi;
    collector.removeNextBlockOfMessages(midi, n);
    engine.render(scratch, midi, 0, n);
    for (int ch = 0; ch < numOut; ++ch)
        if (out[ch] != nullptr) juce::FloatVectorOperations::copy(out[ch], scratch.getReadPointer(juce::jmin(ch, 1)), n);
}

void PlayerTab::handleNoteOn(juce::MidiKeyboardState*, int, int note, float vel) {
    if (Session::isEcho()) return;  // the controller's notes already reached the player directly
    auto m = juce::MidiMessage::noteOn(1, note, vel);
    m.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    collector.addMessageToQueue(m);
}

void PlayerTab::handleNoteOff(juce::MidiKeyboardState*, int, int note, float) {
    if (Session::isEcho()) return;
    auto m = juce::MidiMessage::noteOff(1, note);
    m.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    collector.addMessageToQueue(m);
}

void PlayerTab::openFile() {
    chooser = std::make_unique<juce::FileChooser>("Open a QCard image or QS ROM dump", imageFile.existsAsFile() ? imageFile : settingsDir(),
                                                  "*.img;*.IMG;*.bin;*.BIN;*.qcd;*.QCD");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (f.existsAsFile()) load(f);
    });
}

void PlayerTab::load(const juce::File& f) {
    juce::String err;
    auto img = qsplayer::loadFile(f, err);
    if (img == nullptr) {
        qslog::action("Sound Player: can't play " + f.getFullPathName() + ": " + err);
        alert("Can't play this file", err);
        return;
    }
    image = img;
    imageFile = f;
    settings().setValue("playerFile", f.getFullPathName());
    qslog::action("Sound Player: opened " + f.getFileName() + " (" + juce::String(int(image->voices.size())) + " voices, " +
                  juce::String(image->sampleCheck) + ")");
    engine.select(nullptr, -1);
    filter();
    if (!rows.empty()) list.selectRow(0);
    updateInfo();
}

void PlayerTab::filter() {
    rows.clear();
    if (image != nullptr) {
        const auto q = search.getText().trim();
        for (int i = 0; i < int(image->voices.size()); ++i)
            if (q.isEmpty() || juce::String(image->voices[size_t(i)].name).containsIgnoreCase(q) ||
                juce::String(qs::voiceGroupName(*image, image->voices[size_t(i)].group)).containsIgnoreCase(q))
                rows.push_back(i);
    }
    list.updateContent();
    list.repaint();
}

void PlayerTab::selectedRowsChanged(int row) {
    if (image == nullptr || row < 0 || row >= int(rows.size())) return;
    engine.select(image, rows[size_t(row)]);
    updateInfo();
}

void PlayerTab::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) {
    if (image == nullptr || row < 0 || row >= int(rows.size())) return;
    const auto& v = image->voices[size_t(rows[size_t(row)])];
    if (selected) g.fillAll(theme::accent.withAlpha(0.25f));
    const int pct = qsplayer::availablePercent(*image, v);
    g.setFont(theme::uiFont(13.0f));
    g.setColour(theme::dim);
    g.drawText(qs::voiceGroupName(*image, v.group), 8, 0, 120, h, juce::Justification::centredLeft);
    g.setColour(pct == 0 ? theme::dim : theme::text);
    g.setFont(theme::uiFont(14.0f));
    g.drawText(v.name, 140, 0, w - 320, h, juce::Justification::centredLeft);
    g.setColour(theme::dim);
    g.setFont(theme::uiFont(12.0f));
    const juce::String detail = juce::String(int(v.zones.size())) + (v.zones.size() == 1 ? " sample" : " samples") +
                                (v.layers > 1 ? ", " + juce::String(v.layers) + " layers" : juce::String()) +
                                (pct < 100 ? "   " + juce::String(pct) + "% in this file" : juce::String());
    g.drawText(detail, w - 170, 0, 162, h, juce::Justification::centredRight);
}

void PlayerTab::updateInfo() {
    if (image == nullptr) {
        fileLabel.setText("Nothing open yet.", juce::dontSendNotification);
        countLabel.setText({}, juce::dontSendNotification);
        lcd.setLines("SOUND PLAYER", {"OPEN A CARD IMAGE", "OR A ROM DUMP"});
        return;
    }
    int full = 0;
    for (auto& v : image->voices) full += qsplayer::availablePercent(*image, v) == 100;
    fileLabel.setText(imageFile.getFileName() + "  (" + juce::String(image->cardName).trim() + ")", juce::dontSendNotification);
    countLabel.setText(juce::String(int(image->voices.size())) + " voices, " + juce::String(full) + " fully playable from this file" +
                           (image->isRom && full < int(image->voices.size())
                                ? ". The others use the ROM's second chip, which isn't in this dump."
                                : juce::String()),
                       juce::dontSendNotification);
    const int row = list.getSelectedRow();
    const auto name = row >= 0 && row < int(rows.size()) ? juce::String(image->voices[size_t(rows[size_t(row)])].name) : juce::String("-");
    auto* dev = session.audio().getCurrentAudioDevice();
    lcd.setLines(name, {juce::String(image->isRom ? "INTERNAL ROM" : "CARD").toUpperCase() + "  " + imageFile.getFileNameWithoutExtension().toUpperCase(),
                        dev != nullptr ? "OUT: " + dev->getName().toUpperCase() : juce::String("NO AUDIO OUTPUT: CLICK AUDIO OUTPUT")});
}

void PlayerTab::showAudioSettings() {
    auto* sel = new juce::AudioDeviceSelectorComponent(session.audio(), 0, 2, 1, 2, false, false, true, false);
    sel->setSize(520, 380);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned(sel);
    o.dialogTitle = "Audio output";
    o.dialogBackgroundColour = theme::bg;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void PlayerTab::paint(juce::Graphics& g) { g.fillAll(theme::bg); }

void PlayerTab::resized() {
    auto r = getLocalBounds().reduced(14);
    auto top = r.removeFromTop(118);
    lcd.setBounds(top.removeFromLeft(juce::jmin(520, top.getWidth() / 2)));
    top.removeFromLeft(16);
    auto b = top.removeFromTop(34);
    openBtn.setBounds(b.removeFromLeft(270));
    b.removeFromLeft(8);
    audioBtn.setBounds(b.removeFromLeft(150));
    top.removeFromTop(6);
    fileLabel.setBounds(top.removeFromTop(22));
    countLabel.setBounds(top.removeFromTop(40));
    r.removeFromTop(10);
    auto right = r.removeFromRight(juce::jmin(360, r.getWidth() / 3));
    r.removeFromRight(14);
    search.setBounds(r.removeFromTop(30));
    r.removeFromTop(6);
    list.setBounds(r);
    const int h = soundSec.heightFor(right.getWidth(), 5 * 34 + 40);
    soundSec.setBounds(right.removeFromTop(h));
    auto c = soundSec.contentArea();
    for (auto [l, s] : {std::pair<juce::Label*, juce::Slider*>{&attackLabel, &attack}, {&decayLabel, &decay}, {&sustainLabel, &sustain},
                        {&releaseLabel, &release}, {&volumeLabel, &volume}}) {
        auto row = c.removeFromTop(30);
        l->setBounds(row.removeFromLeft(70));
        s->setBounds(row);
        c.removeFromTop(4);
    }
    alsoQs.setBounds(c.removeFromTop(30));
    right.removeFromTop(10);
    sourceSec.setBounds(right.removeFromTop(sourceSec.heightFor(right.getWidth(), 0)));
}
