#include "PluginProcessor.h"

namespace {
juce::AudioProcessorValueTreeState::ParameterLayout layout() {
    using P = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add(std::make_unique<P>(juce::ParameterID{"attack", 1}, "Attack", juce::NormalisableRange<float>(0.0f, 2.0f, 0.001f, 0.4f), 0.002f));
    l.add(std::make_unique<P>(juce::ParameterID{"release", 1}, "Release", juce::NormalisableRange<float>(0.0f, 5.0f, 0.001f, 0.4f), 0.35f));
    l.add(std::make_unique<P>(juce::ParameterID{"volume", 1}, "Volume", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    return l;
}

const juce::Colour kBg{0xff1c1d1f}, kPanel{0xff26272a}, kText{0xfff0f1f2}, kDim{0xff9a9ca1}, kAccent{0xffa7d65a};
}  // namespace

QsPlayerProcessor::QsPlayerProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "QSSoundPlayer", layout()) {}

void QsPlayerProcessor::syncParams() {
    engine.settings.attack = params.getRawParameterValue("attack")->load();
    engine.settings.release = params.getRawParameterValue("release")->load();
    engine.settings.gain = params.getRawParameterValue("volume")->load();
}

void QsPlayerProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    syncParams();
    buffer.clear();
    engine.render(buffer, midi, 0, buffer.getNumSamples());
}

bool QsPlayerProcessor::loadFile(const juce::File& f, juce::String& error) {
    auto img = qsplayer::loadFile(f, error);
    if (img == nullptr) return false;
    image = img;
    file = f;
    selectVoice(0);
    return true;
}

void QsPlayerProcessor::selectVoice(int index) {
    if (image == nullptr) return;
    voice = juce::jlimit(0, int(image->voices.size()) - 1, index);
    engine.select(image, voice);
    if (onChange) onChange();
}

void QsPlayerProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = params.copyState();
    state.setProperty("file", file.getFullPathName(), nullptr);
    state.setProperty("voice", voice, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void QsPlayerProcessor::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr) return;
    auto state = juce::ValueTree::fromXml(*xml);
    params.replaceState(state);
    const juce::File f(state.getProperty("file").toString());
    juce::String err;
    if (f.existsAsFile() && loadFile(f, err)) selectVoice(int(state.getProperty("voice", 0)));
}

// ---------------- editor ----------------

class QsPlayerEditor : public juce::AudioProcessorEditor, private juce::ListBoxModel {
public:
    explicit QsPlayerEditor(QsPlayerProcessor& p)
        : AudioProcessorEditor(p), proc(p),
          attachA(p.params, "attack", attack), attachR(p.params, "release", release), attachV(p.params, "volume", volume) {
        for (juce::Component* c : std::initializer_list<juce::Component*>{&title, &openBtn, &fileLabel, &search, &list, &attack, &release, &volume,
                                                                        &attackL, &releaseL, &volumeL, &note})
            addAndMakeVisible(c);
        title.setText("QS Sound Player", juce::dontSendNotification);
        title.setFont(juce::FontOptions(20.0f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, kText);
        for (auto* l : {&fileLabel, &attackL, &releaseL, &volumeL, &note}) l->setColour(juce::Label::textColourId, kDim);
        note.setText("Plays the raw samples of your own card image or ROM dump (splits, layers, loops), not the full QS engine. "
                     "QS Studio " + juce::String(JucePlugin_VersionString) + " " + QS_STAGE + ". Unofficial; not made or endorsed by Alesis.",
                     juce::dontSendNotification);
        note.setFont(juce::FontOptions(12.0f));
        openBtn.onClick = [this] {
            chooser = std::make_unique<juce::FileChooser>("Open a QCard image or QS ROM dump", proc.file, "*.img;*.IMG;*.bin;*.BIN;*.qcd;*.QCD");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
                auto f = fc.getResult();
                juce::String err;
                if (f.existsAsFile() && !proc.loadFile(f, err))
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Can't play this file", err);
                refresh();
            });
        };
        search.setTextToShowWhenEmpty("Search voices...", kDim);
        search.onTextChange = [this] { refresh(); };
        list.setModel(this);
        list.setRowHeight(24);
        list.setColour(juce::ListBox::backgroundColourId, kPanel);
        for (auto* s : {&attack, &release, &volume}) {
            s->setSliderStyle(juce::Slider::LinearBar);
            s->setColour(juce::Slider::trackColourId, kAccent.withAlpha(0.6f));
        }
        attackL.setText("Attack", juce::dontSendNotification);
        releaseL.setText("Release", juce::dontSendNotification);
        volumeL.setText("Volume", juce::dontSendNotification);
        juce::Component::SafePointer<QsPlayerEditor> safe(this);
        proc.onChange = [safe] {
            juce::MessageManager::callAsync([safe] {
                if (safe != nullptr) safe->refresh();
            });
        };
        setResizable(true, true);
        setResizeLimits(520, 400, 1600, 1200);
        setSize(720, 520);
        refresh();
    }
    ~QsPlayerEditor() override { proc.onChange = nullptr; }

    void paint(juce::Graphics& g) override { g.fillAll(kBg); }
    void resized() override {
        auto r = getLocalBounds().reduced(12);
        auto top = r.removeFromTop(32);
        title.setBounds(top.removeFromLeft(220));
        openBtn.setBounds(top.removeFromRight(260));
        fileLabel.setBounds(r.removeFromTop(24));
        note.setBounds(r.removeFromBottom(36));
        auto knobs = r.removeFromBottom(30);
        for (auto [l, s] : {std::pair<juce::Label*, juce::Slider*>{&attackL, &attack}, {&releaseL, &release}, {&volumeL, &volume}}) {
            auto c = knobs.removeFromLeft(knobs.getWidth() / 3 + 1);
            l->setBounds(c.removeFromLeft(60));
            s->setBounds(c.reduced(4, 0));
        }
        r.removeFromBottom(8);
        search.setBounds(r.removeFromTop(28));
        r.removeFromTop(6);
        list.setBounds(r);
    }

private:
    int getNumRows() override { return int(rows.size()); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override {
        if (proc.image == nullptr || row >= int(rows.size())) return;
        const auto& v = proc.image->voices[size_t(rows[size_t(row)])];
        if (selected) g.fillAll(kAccent.withAlpha(0.25f));
        const int pct = qsplayer::availablePercent(*proc.image, v);
        g.setColour(pct == 0 ? kDim : kText);
        g.setFont(juce::FontOptions(14.0f));
        g.drawText(v.name, 8, 0, w - 140, h, juce::Justification::centredLeft);
        g.setColour(kDim);
        g.setFont(juce::FontOptions(12.0f));
        g.drawText(pct < 100 ? juce::String(pct) + "% in this file" : juce::String(int(v.zones.size())) + " samples", w - 130, 0, 122, h,
                   juce::Justification::centredRight);
    }
    void selectedRowsChanged(int row) override {
        if (row >= 0 && row < int(rows.size()) && rows[size_t(row)] != proc.voice) proc.selectVoice(rows[size_t(row)]);
    }

    void refresh() {
        rows.clear();
        if (proc.image != nullptr) {
            const auto q = search.getText().trim();
            for (int i = 0; i < int(proc.image->voices.size()); ++i)
                if (q.isEmpty() || juce::String(proc.image->voices[size_t(i)].name).containsIgnoreCase(q)) rows.push_back(i);
            fileLabel.setText(proc.file.getFileName() + " - " + juce::String(int(proc.image->voices.size())) + " voices",
                              juce::dontSendNotification);
        } else {
            fileLabel.setText("Open a QCard image (.img/.bin) or a QS internal sample ROM dump.", juce::dontSendNotification);
        }
        list.updateContent();
        const auto it = std::find(rows.begin(), rows.end(), proc.voice);
        if (it != rows.end()) list.selectRow(int(it - rows.begin()), false, true);
        list.repaint();
    }

    QsPlayerProcessor& proc;
    juce::Label title, fileLabel, attackL, releaseL, volumeL, note;
    juce::TextButton openBtn{"Open a card image or ROM dump..."};
    juce::TextEditor search;
    juce::ListBox list;
    juce::Slider attack, release, volume;
    juce::AudioProcessorValueTreeState::SliderAttachment attachA, attachR, attachV;
    std::vector<int> rows;
    std::unique_ptr<juce::FileChooser> chooser;
};

juce::AudioProcessorEditor* QsPlayerProcessor::createEditor() { return new QsPlayerEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new QsPlayerProcessor(); }
