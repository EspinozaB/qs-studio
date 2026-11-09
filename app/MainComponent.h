#pragma once
#include <JuceHeader.h>

#include "../core/qs_card.h"
#include "MidiLink.h"

// Shared helpers
juce::String hex(const qs::Bytes& b, size_t maxBytes = 32);
juce::File settingsDir();

class DeviceTab : public juce::Component, private MidiLink::Listener {
public:
    explicit DeviceTab(MidiLink& m);
    ~DeviceTab() override;
    void resized() override;

private:
    void sysexReceived(const qs::Bytes& msg) override;
    void refreshDevices();
    void connect();

    MidiLink& midi;
    juce::Label inLabel{{}, "MIDI In"}, outLabel{{}, "MIDI Out"}, status;
    juce::ComboBox inBox, outBox;
    juce::TextButton refreshBtn{"Refresh"}, connectBtn{"Connect"}, identifyBtn{"Identify synth"};
    juce::TextEditor log;
};

class SysexTab : public juce::Component, private MidiLink::Listener {
public:
    explicit SysexTab(MidiLink& m);
    ~SysexTab() override;
    void resized() override;

private:
    void sysexReceived(const qs::Bytes& msg) override;
    void updateSummary(const std::vector<qs::Bytes>& msgs, const juce::String& title);
    void save();
    void load();
    void sendLoaded();

    MidiLink& midi;
    bool recording = false;
    std::vector<qs::Bytes> captured, loaded;
    juce::TextButton recBtn{"Record"}, stopBtn{"Stop"}, saveBtn{"Save .syx"}, requestBtn{"Ask QS to Send All"},
        loadBtn{"Open .syx"}, sendBtn{"Send to QS"};
    juce::Label countLabel, help;
    juce::TextEditor summary;
    std::unique_ptr<juce::FileChooser> chooser;
};

class CardTab : public juce::Component {
public:
    explicit CardTab(MidiLink& m);
    void resized() override;

private:
    void openImage();
    void showCard();
    void exportWavs(bool sfz);
    void testReader();
    void readCard();
    void writeCard();
    bool requireMidi();

    MidiLink& midi;
    qs::CardImage card;
    juce::File cardFile;
    juce::TextButton openBtn{"Open card image"}, wavBtn{"Export WAVs"}, sfzBtn{"Export SFZ instruments"},
        testBtn{"Test card reader"}, readBtn{"Read card from QS"}, writeBtn{"Write image to FLASH card"};
    juce::ComboBox sizeBox;
    juce::Label sizeLabel{{}, "Card size"};
    juce::TextEditor info;
    std::unique_ptr<juce::FileChooser> chooser;
};

class MainComponent : public juce::Component {
public:
    MainComponent();
    void resized() override;

private:
    MidiLink midi;
    juce::TabbedComponent tabs{juce::TabbedButtonBar::TabsAtTop};
};
