#include "RecorderTab.h"

// ---------------- AudioRecorder ----------------

bool AudioRecorder::start(const juce::File& f, int bitDepth, juce::String& error) {
    stop();
    if (sampleRate <= 0) { error = "No audio input is running. Pick your audio interface and its inputs first."; return false; }
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    auto stream = std::make_unique<juce::FileOutputStream>(f);
    if (stream->failedToOpen()) { error = "Couldn't write " + f.getFullPathName(); return false; }
    juce::WavAudioFormat wav;
    auto* writer = wav.createWriterFor(stream.get(), sampleRate, 2, bitDepth, {}, 0);
    if (writer == nullptr) { error = "The WAV writer refused this format."; return false; }
    stream.release();  // the writer owns it now
    threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(writer, writerThread, 65536);
    written = 0;
    const juce::ScopedLock sl(writerLock);
    active = threaded.get();
    return true;
}

void AudioRecorder::stop() {
    {
        const juce::ScopedLock sl(writerLock);
        active = nullptr;
    }
    threaded.reset();  // flushes and closes the file
}

void AudioRecorder::audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut, int n,
                                                     const juce::AudioIODeviceCallbackContext&) {
    // Stereo from the first two enabled inputs (mono input is copied to both sides).
    const float* l = nullptr;
    const float* r = nullptr;
    for (int c = 0; c < numIn; ++c)
        if (in[c] != nullptr) {
            if (l == nullptr) l = in[c];
            else if (r == nullptr) r = in[c];
        }
    if (r == nullptr) r = l;
    if (l != nullptr) {
        float pl = 0, pr = 0;
        for (int i = 0; i < n; ++i) {
            pl = juce::jmax(pl, std::abs(l[i]));
            pr = juce::jmax(pr, std::abs(r[i]));
        }
        if (pl > peakL.load()) peakL = pl;
        if (pr > peakR.load()) peakR = pr;
        const juce::ScopedLock sl(writerLock);
        if (auto* w = active.load()) {
            const float* chans[2] = {l, r};
            w->write(chans, n);
            written += n;
        }
    }
    // Live monitoring: input straight to the first two outputs.
    const bool mon = monitor.load() && l != nullptr;
    const float gain = monitorGain.load();
    int outIdx = 0;
    for (int c = 0; c < numOut; ++c) {
        if (out[c] == nullptr) continue;
        if (mon && outIdx < 2) juce::FloatVectorOperations::multiply(out[c], outIdx == 0 ? l : r, gain, n);
        else juce::FloatVectorOperations::clear(out[c], n);
        ++outIdx;
    }
}

// ---------------- Auto-sampler ----------------

class RecorderTab::AutoSampler : public juce::Thread {
public:
    struct Job {
        int low, high, step, holdMs, tailMs, bits;
        juce::Array<int> velocities;
        juce::String prefix;
        juce::File folder;
    };

    AutoSampler(Session& s, AudioRecorder& r, Job j) : Thread("QS auto-sampler"), session(s), recorder(r), job(std::move(j)) {}
    ~AutoSampler() override { stopThread(4000); }

    std::atomic<int> done{0}, total{0};
    juce::String error, current;
    juce::CriticalSection textLock;

    void run() override {
        const int notes = (job.high - job.low) / job.step + 1;
        total = notes * job.velocities.size();
        for (int vel : job.velocities)
            for (int note = job.low; note <= job.high && !threadShouldExit(); note += job.step) {
                const auto name = juce::MidiMessage::getMidiNoteName(note, true, true, 4);
                const auto file = job.folder.getChildFile(juce::File::createLegalFileName(
                    job.prefix + "_" + juce::String(note).paddedLeft('0', 3) + "_" + name + "_v" + juce::String(vel) + ".wav"));
                {
                    const juce::ScopedLock sl(textLock);
                    current = file.getFileName();
                }
                juce::String err;
                if (!recorder.start(file, job.bits, err)) {
                    const juce::ScopedLock sl(textLock);
                    error = err;
                    return;
                }
                wait(60);  // pre-roll so the attack is never cut
                session.midi.sendMidi(juce::MidiMessage::noteOn(session.channel, note, juce::uint8(juce::jlimit(1, 127, vel))));
                wait(job.holdMs);
                session.midi.sendMidi(juce::MidiMessage::noteOff(session.channel, note));
                wait(job.tailMs);
                recorder.stop();
                ++done;
                wait(80);  // let voices die before the next note
            }
        session.midi.sendMidi(juce::MidiMessage::allNotesOff(session.channel));
    }

private:
    Session& session;
    AudioRecorder& recorder;
    Job job;
};

// ---------------- RecorderTab ----------------

RecorderTab::RecorderTab(Session& s) : session(s) {
    std::unique_ptr<juce::XmlElement> state(juce::parseXML(settings().getValue("audioState")));
    devices.initialise(2, 2, state.get(), true);
    devices.addAudioCallback(&recorder);
    selector = std::make_unique<juce::AudioDeviceSelectorComponent>(devices, 1, 2, 0, 2, false, false, true, false);
    addAndMakeVisible(*selector);
    addAndMakeVisible(lcd);

    for (juce::Label* l : {&prefixLabel, &lowLabel, &highLabel, &stepLabel, &velLabel, &holdLabel, &tailLabel, &bitsLabel}) {
        l->setColour(juce::Label::textColourId, theme::dim);
        l->setFont(theme::uiFont(13.0f, true));
        addAndMakeVisible(l);
    }
    prefix.setText(settings().getValue("recPrefix", "QS6"), false);
    velocities.setText(settings().getValue("recVels", "127"), false);
    velocities.setTooltip("Comma separated, e.g. 40,90,127");
    auto setupNum = [this](juce::Slider& sl, double lo, double hi, double step, const char* key, double def) {
        sl.setRange(lo, hi, step);
        sl.setValue(settings().getDoubleValue(key, def), juce::dontSendNotification);
        sl.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 70, 26);
        addAndMakeVisible(sl);
    };
    setupNum(low, 0, 127, 1, "recLow", 36);
    setupNum(high, 0, 127, 1, "recHigh", 96);
    setupNum(stepSemis, 1, 24, 1, "recStep", 3);
    setupNum(hold, 50, 30000, 50, "recHold", 3000);
    setupNum(tail, 0, 30000, 50, "recTail", 1500);
    low.textFromValueFunction = [](double v) { return juce::MidiMessage::getMidiNoteName(int(v), true, true, 4) + " (" + juce::String(int(v)) + ")"; };
    high.textFromValueFunction = low.textFromValueFunction;
    low.updateText();
    high.updateText();
    stepSemis.textFromValueFunction = [](double v) { return juce::String(int(v)) + (int(v) == 1 ? " semi" : " semis"); };
    stepSemis.updateText();
    bits.addItem("16-bit", 16);
    bits.addItem("24-bit", 24);
    bits.setSelectedId(settings().getIntValue("recBits", 24), juce::dontSendNotification);
    for (juce::Component* c : std::initializer_list<juce::Component*>{&prefix, &velocities, &bits, &recBtn, &autoBtn, &folderBtn, &nameFromProgBtn, &status})
        addAndMakeVisible(c);
    status.setFont(theme::uiFont(14.0f));
    status.setColour(juce::Label::textColourId, theme::dim);
    recBtn.onClick = [this] { toggleRecord(); };
    autoBtn.onClick = [this] { if (sampler) stopAuto(); else startAuto(); };
    folderBtn.onClick = [this] { outputFolder().createDirectory(); outputFolder().startAsProcess(); };
    nameFromProgBtn.onClick = [this] {
        auto n = session.library.nameOf(session.currentBank, session.currentProgram);
        if (n.isNotEmpty()) prefix.setText(n.removeCharacters(" "), true);
    };
    monitorToggle.setToggleState(settings().getBoolValue("recMonitor", false), juce::dontSendNotification);
    recorder.monitor = monitorToggle.getToggleState();
    monitorToggle.setTooltip("Plays the inputs through the output device so you can hear the QS without a DAW. Use headphones if a mic is involved.");
    monitorToggle.onClick = [this] {
        recorder.monitor = monitorToggle.getToggleState();
        settings().setValue("recMonitor", monitorToggle.getToggleState());
    };
    monitorLevel.setRange(0.0, 1.0, 0.01);
    monitorLevel.setValue(settings().getDoubleValue("recMonitorLevel", 0.7), juce::dontSendNotification);
    recorder.monitorGain = float(monitorLevel.getValue());
    monitorLevel.onValueChange = [this] {
        recorder.monitorGain = float(monitorLevel.getValue());
        settings().setValue("recMonitorLevel", monitorLevel.getValue());
    };
    monitorLevel.setTooltip("Monitor level");
    addAndMakeVisible(monitorToggle);
    addAndMakeVisible(monitorLevel);
    startTimerHz(20);
}

RecorderTab::~RecorderTab() {
    sampler.reset();
    recorder.stop();
    devices.removeAudioCallback(&recorder);
    saveSettings();
}

void RecorderTab::saveSettings() {
    if (auto xml = devices.createStateXml()) settings().setValue("audioState", xml->toString());
    settings().setValue("recPrefix", prefix.getText());
    settings().setValue("recVels", velocities.getText());
    settings().setValue("recLow", low.getValue());
    settings().setValue("recHigh", high.getValue());
    settings().setValue("recStep", stepSemis.getValue());
    settings().setValue("recHold", hold.getValue());
    settings().setValue("recTail", tail.getValue());
    settings().setValue("recBits", bits.getSelectedId());
    settings().saveIfNeeded();
}

juce::File RecorderTab::outputFolder() const {
    return settingsDir().getChildFile("Recordings").getChildFile(juce::File::createLegalFileName(prefix.getText().trim().isEmpty() ? "QS" : prefix.getText().trim()));
}

void RecorderTab::toggleRecord() {
    if (sampler) return;
    if (recorder.isRecording()) {
        recorder.stop();
        status.setText("Saved to " + outputFolder().getFullPathName(), juce::dontSendNotification);
        return;
    }
    saveSettings();
    const auto f = outputFolder().getNonexistentChildFile(prefix.getText().trim() + "_" + juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S"), ".wav");
    juce::String err;
    if (!recorder.start(f, bits.getSelectedId(), err)) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Record", err);
        return;
    }
    status.setText("Recording to " + f.getFileName(), juce::dontSendNotification);
}

void RecorderTab::startAuto() {
    if (!session.midi.isOpen()) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Not connected", "Connect to the QS on the Device tab first.");
        return;
    }
    if (recorder.isRecording()) recorder.stop();
    AutoSampler::Job job;
    job.low = int(juce::jmin(low.getValue(), high.getValue()));
    job.high = int(juce::jmax(low.getValue(), high.getValue()));
    job.step = int(stepSemis.getValue());
    job.holdMs = int(hold.getValue());
    job.tailMs = int(tail.getValue());
    job.bits = bits.getSelectedId();
    for (auto& t : juce::StringArray::fromTokens(velocities.getText(), ", ", ""))
        if (t.getIntValue() > 0) job.velocities.add(juce::jlimit(1, 127, t.getIntValue()));
    if (job.velocities.isEmpty()) job.velocities.add(127);
    job.prefix = prefix.getText().trim().isEmpty() ? "QS" : prefix.getText().trim();
    job.folder = outputFolder();
    saveSettings();
    sampler = std::make_unique<AutoSampler>(session, recorder, job);
    sampler->startThread();
    autoBtn.setButtonText("Stop auto-sampling");
}

void RecorderTab::stopAuto() {
    if (!sampler) return;
    sampler->signalThreadShouldExit();
    sampler->notify();
    sampler.reset();
    recorder.stop();
    session.allNotesOff();
    autoBtn.setButtonText("Auto-sample notes");
}

void RecorderTab::timerCallback() {
    meterL = juce::jmax(recorder.takePeak(0), meterL * 0.85f);
    meterR = juce::jmax(recorder.takePeak(1), meterR * 0.85f);
    repaint(meterArea);
    juce::StringArray lines;
    if (sampler) {
        juce::String cur, err;
        {
            const juce::ScopedLock sl(sampler->textLock);
            cur = sampler->current;
            err = sampler->error;
        }
        lines.add("AUTO-SAMPLING " + juce::String(sampler->done.load()) + " / " + juce::String(sampler->total.load()));
        lines.add(cur);
        if (err.isNotEmpty() || !sampler->isThreadRunning()) {
            const bool ok = err.isEmpty();
            const int n = sampler->done.load();
            stopAuto();
            status.setText(ok ? "Done: " + juce::String(n) + " files in " + outputFolder().getFullPathName() : err, juce::dontSendNotification);
        }
    } else if (recorder.isRecording()) {
        lines.add("RECORDING  " + juce::String(recorder.seconds(), 1) + " s");
    } else {
        lines.add(recorder.rate() > 0 ? "READY  " + juce::String(int(recorder.rate())) + " Hz" + (recorder.monitor.load() ? "   MONITORING" : "")
                                      : "NO AUDIO DEVICE: pick one on the left");
    }
    lcd.setLines(sampler || recorder.isRecording() ? juce::String("REC") : juce::String("Record"), lines);
    recBtn.setButtonText(recorder.isRecording() && !sampler ? "Stop" : "Record");
    recBtn.setEnabled(!sampler);
}

void RecorderTab::paint(juce::Graphics& g) {
    g.fillAll(theme::bg);
    if (meterArea.isEmpty()) return;
    auto m = meterArea.toFloat();
    const float h = (m.getHeight() - 6) / 2;
    for (int ch = 0; ch < 2; ++ch) {
        auto bar = m.removeFromTop(h);
        m.removeFromTop(6);
        g.setColour(theme::bezel);
        g.fillRoundedRectangle(bar, 4.0f);
        const float level = juce::jlimit(0.0f, 1.0f, (ch == 0 ? meterL : meterR));
        const float db = juce::Decibels::gainToDecibels(level, -60.0f);
        const float frac = (db + 60.0f) / 60.0f;
        g.setColour(level > 0.98f ? theme::bad : (db > -6.0f ? theme::warn : theme::accent));
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * frac), 4.0f);
    }
}

void RecorderTab::resized() {
    auto r = getLocalBounds().reduced(14);
    auto left = r.removeFromLeft(juce::jmin(560, r.getWidth() / 2));
    selector->setBounds(left);
    r.removeFromLeft(20);
    lcd.setBounds(r.removeFromTop(110));
    r.removeFromTop(8);
    meterArea = r.removeFromTop(30);
    r.removeFromTop(10);
    auto mon = r.removeFromTop(30);
    monitorToggle.setBounds(mon.removeFromLeft(230));
    monitorLevel.setBounds(mon.removeFromLeft(220));
    r.removeFromTop(10);
    auto row = [&r](int h = 30) { auto x = r.removeFromTop(h); r.removeFromTop(8); return x; };
    auto labelled = [](juce::Rectangle<int> rr, juce::Label& l, juce::Component& c) {
        l.setBounds(rr.removeFromLeft(130));
        c.setBounds(rr);
    };
    auto pr = row();
    nameFromProgBtn.setBounds(pr.removeFromRight(150));
    pr.removeFromRight(8);
    labelled(pr, prefixLabel, prefix);
    labelled(row().removeFromLeft(330), bitsLabel, bits);
    auto rb = row(40);
    recBtn.setBounds(rb.removeFromLeft(140));
    rb.removeFromLeft(10);
    folderBtn.setBounds(rb.removeFromLeft(190));
    r.removeFromTop(14);
    labelled(row().removeFromLeft(380), lowLabel, low);
    labelled(row().removeFromLeft(380), highLabel, high);
    labelled(row().removeFromLeft(330), stepLabel, stepSemis);
    labelled(row().removeFromLeft(380), velLabel, velocities);
    labelled(row().removeFromLeft(330), holdLabel, hold);
    labelled(row().removeFromLeft(330), tailLabel, tail);
    autoBtn.setBounds(row(40).removeFromLeft(200));
    status.setBounds(row(40));
}
