// Effects editor: one effect patch (configuration plus every pitch, delay, reverb, lezlie, overdrive, EQ and
// modulation parameter it has), for the program being played or for the current mix, edited live.
#pragma once
#include <JuceHeader.h>

#include "Session.h"
#include "Theme.h"

class EffectsTab : public juce::Component, private MidiLink::Listener, private juce::Timer {
public:
    explicit EffectsTab(Session& s);
    ~EffectsTab() override;
    void paint(juce::Graphics& g) override;
    void resized() override;

    // edit: 0 = the Program mode effect, 1 = the Mix mode effect. An empty patch means "get it from the QS".
    void openEffects(const qs::Effects& e, const juce::String& where, int edit, bool fetch);

private:
    class Control;
    void sysexReceived(const qs::Bytes& msg) override;
    void timerCallback() override;
    void rebuild();
    void loadValues();
    void updateLcd();
    void changed(const qs::EffectParam& p, int raw, bool isType);
    void getFromQs();
    void sendToQs();
    void storeToUser();
    void saveSyx();
    void openSyx();
    void layoutContent();

    Session& session;
    qs::Effects fx;
    juce::String where = "No effect loaded";
    int edit = 0;
    bool awaitingDump = false, rebuildPending = false;
    int requestsSent = 0;
    juce::uint32 nextRequestAt = 0;

    theme::Lcd lcd;
    juce::ComboBox configBox, targetBox;
    juce::Label configLabel{{}, "Configuration"}, targetLabel{{}, "Effect of"};
    juce::ToggleButton liveToggle{"Live edit"};
    juce::TextButton getBtn{"Get from QS"}, sendBtn{"Send to QS"}, storeBtn{"Store to User..."}, openBtn{"Open .syx"}, saveBtn{"Save .syx"};
    juce::Label note;
    juce::Viewport view;
    juce::Component content;
    juce::OwnedArray<juce::Label> headers;
    juce::OwnedArray<Control> controls;
    std::vector<std::pair<juce::Label*, std::vector<Control*>>> groups;
    std::unique_ptr<juce::FileChooser> chooser;
};
