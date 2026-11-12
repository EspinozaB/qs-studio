// First-run setup guide: connect the QS, back it up, then start playing. Three short steps over the page area,
// skippable, and reopened from the sidebar's "Setup guide" button.
#pragma once
#include <JuceHeader.h>

#include "Theme.h"

class WelcomeGuide : public juce::Component {
public:
    std::function<juce::StringArray()> inputs, outputs;
    std::function<void(juce::String, juce::String)> connect;  // MIDI in, out
    std::function<void()> backup;                             // closes the guide and starts Back up now
    std::function<void(bool playNow)> finished;               // closes the guide

    WelcomeGuide() {
        for (juce::Component* c : std::initializer_list<juce::Component*>{&inBox, &outBox, &refreshBtn, &connectBtn, &backupBtn,
                                                                          &nextBtn, &backBtn, &skipBtn, &status})
            addChildComponent(c);
        skipBtn.setVisible(true);
        refreshBtn.onClick = [this] { refreshPorts(); };
        connectBtn.onClick = [this] {
            if (inBox.getText().isEmpty() || outBox.getText().isEmpty()) {
                setStatus("Pick both ports first.", false);
                return;
            }
            setStatus("Asking the synth what it is...", false);
            if (connect) connect(inBox.getText(), outBox.getText());
            juce::Component::SafePointer<WelcomeGuide> safe(this);
            juce::Timer::callAfterDelay(3000, [safe] {
                if (safe && !safe->found)
                    safe->setStatus("No answer yet. Check both cables, and that Global > I/O on the QS is MIDI, then Connect again.", false);
            });
        };
        backupBtn.onClick = [this] { if (backup) backup(); };
        nextBtn.onClick = [this] {
            if (step == 2) { if (finished) finished(true); return; }
            show(step + 1);
        };
        backBtn.onClick = [this] { show(step - 1); };
        skipBtn.onClick = [this] { if (finished) finished(false); };
        status.setJustificationType(juce::Justification::centredLeft);
        status.setFont(theme::uiFont(15.0f, true));
        show(0);
    }

    void start() {
        found = false;
        refreshPorts();
        show(0);
    }

    // From MainComponent when the synth answers the device inquiry.
    void synthFound(const juce::String& name) {
        found = true;
        setStatus("Found your " + name + ".", true);
        nextBtn.setButtonText("Next");
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(theme::bg.withAlpha(0.96f));
        auto card = cardArea();
        theme::drawPanel(g, card.toFloat(), false);
        auto r = card.reduced(32, 26);
        g.setColour(theme::dim);
        g.setFont(theme::uiFont(13.0f, true));
        g.drawText("SETUP GUIDE  " + juce::String(step + 1) + " OF 3", r.removeFromTop(20), juce::Justification::centredLeft);
        g.setColour(theme::text);
        g.setFont(theme::uiFont(26.0f, true));
        g.drawText(titles[step], r.removeFromTop(40), juce::Justification::centredLeft);
        r.removeFromTop(6);
        juce::AttributedString a;
        a.append(bodies[step], theme::uiFont(16.0f), theme::text.withAlpha(0.9f));
        a.setLineSpacing(4.0f);
        juce::TextLayout tl;
        tl.createLayout(a, float(r.getWidth()));
        tl.draw(g, r.removeFromTop(int(tl.getHeight()) + 4).toFloat());
    }

    void resized() override {
        auto card = cardArea();
        auto r = card.reduced(32, 26);
        auto bottom = r.removeFromBottom(40);
        skipBtn.setBounds(bottom.removeFromLeft(150));
        nextBtn.setBounds(bottom.removeFromRight(160));
        bottom.removeFromRight(10);
        backBtn.setBounds(bottom.removeFromRight(110));
        r.removeFromBottom(16);
        status.setBounds(r.removeFromBottom(30));
        r.removeFromBottom(8);
        auto controls = r.removeFromBottom(step == 0 ? 82 : 40);
        if (step == 0) {
            auto row = controls.removeFromTop(36);
            inBox.setBounds(row.removeFromLeft((row.getWidth() - 20) / 2));
            row.removeFromLeft(20);
            outBox.setBounds(row);
            controls.removeFromTop(8);
            row = controls.removeFromTop(36);
            connectBtn.setBounds(row.removeFromLeft(160));
            row.removeFromLeft(10);
            refreshBtn.setBounds(row.removeFromLeft(140));
        } else if (step == 1) {
            backupBtn.setBounds(controls.removeFromLeft(220).withHeight(38));
        }
    }

private:
    juce::Rectangle<int> cardArea() const {
        const int w = juce::jmin(760, getWidth() - 40), h = juce::jmin(400, getHeight() - 40);
        return getLocalBounds().withSizeKeepingCentre(w, h);
    }

    void show(int s) {
        step = juce::jlimit(0, 2, s);
        inBox.setVisible(step == 0);
        outBox.setVisible(step == 0);
        refreshBtn.setVisible(step == 0);
        connectBtn.setVisible(step == 0);
        backupBtn.setVisible(step == 1);
        backBtn.setVisible(step > 0);
        nextBtn.setVisible(true);
        skipBtn.setVisible(step < 2);
        status.setVisible(step == 0);
        nextBtn.setButtonText(step == 2 ? "Start playing" : step == 0 && !found ? "Skip this step" : "Next");
        resized();
        repaint();
    }

    void refreshPorts() {
        const auto keepIn = inBox.getText(), keepOut = outBox.getText();
        inBox.clear(juce::dontSendNotification);
        outBox.clear(juce::dontSendNotification);
        const auto ins = inputs ? inputs() : juce::StringArray(), outs = outputs ? outputs() : juce::StringArray();
        inBox.addItemList(ins, 1);
        outBox.addItemList(outs, 1);
        inBox.setTextWhenNothingSelected("MIDI In (from the QS)");
        outBox.setTextWhenNothingSelected("MIDI Out (to the QS)");
        if (ins.contains(keepIn)) inBox.setText(keepIn, juce::dontSendNotification);
        else if (ins.size() == 1) inBox.setSelectedItemIndex(0, juce::dontSendNotification);
        if (outs.contains(keepOut)) outBox.setText(keepOut, juce::dontSendNotification);
        else if (outs.size() == 1) outBox.setSelectedItemIndex(0, juce::dontSendNotification);
        if (ins.isEmpty() || outs.isEmpty())
            setStatus("No MIDI ports found. Plug in the MIDI interface, then Refresh.", false);
        else if (!found)
            setStatus({}, false);
    }

    void setStatus(const juce::String& s, bool good) {
        status.setText(s, juce::dontSendNotification);
        status.setColour(juce::Label::textColourId, good ? theme::accent : theme::warn);
    }

    const juce::String titles[3] = {"Connect your QS", "Back up your synth", "You're set"};
    const juce::String bodies[3] = {
        "Connect two MIDI cables: the QS's MIDI OUT to your interface's MIDI IN, and the interface's MIDI OUT to the QS's MIDI IN. "
        "On the QS, set Global > I/O to MIDI. Then pick the interface below and click Connect.",
        "Save everything you can change on the QS (User programs, mixes, effects and Global settings) to one file on this "
        "computer before you edit anything. It changes nothing on the synth and takes about a minute.",
        "- Programs: click a program and the QS plays it.\n"
        "- Program, Mix and Effects Editor: change sounds live, with Undo.\n"
        "- Sound Player: play QCard images on this computer.\n"
        "- Check-up (under Synth): run it if something doesn't work.\n"
        "Every page has a How this works button at the top right."};
    juce::ComboBox inBox, outBox;
    juce::TextButton refreshBtn{"Refresh"}, connectBtn{"Connect"}, backupBtn{"Back up now..."}, nextBtn{"Next"}, backBtn{"Back"},
        skipBtn{"Skip the guide"};
    juce::Label status;
    int step = 0;
    bool found = false;
};
