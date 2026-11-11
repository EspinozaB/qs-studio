// Mix editor: the 16 channels of a mix (program, level, pan, output, effect send, pitch, key range, MIDI switches),
// edited live in the QS's mix edit buffer, then stored to a User mix.
#pragma once
#include <JuceHeader.h>

#include "Session.h"
#include "Theme.h"

class MixTab : public juce::Component, private MidiLink::Listener, private juce::Timer {
public:
    explicit MixTab(Session& s);
    ~MixTab() override;
    void paint(juce::Graphics& g) override;
    void resized() override;

    void openMix(const qs::Mix& m, const juce::String& where, int userNumber);
    void refreshNames();  // program names changed (library read or imported)

private:
    class Row;
    void sysexReceived(const qs::Bytes& msg) override;
    void timerCallback() override;
    void loadAll();
    void updateLcd();
    void changed(int ch, const qs::MixParam& p, int raw);
    void commonChanged(const qs::MixParam& p, int raw);
    void getFromQs();
    void sendToQs();
    void storeToUser();
    void saveSyx();
    void openSyx();

    Session& session;
    qs::Mix mix;
    juce::String where = "No mix loaded";
    int userNumber = -1;
    bool awaitingDump = false;
    int requestsSent = 0;
    juce::uint32 nextRequestAt = 0, lastNameEdit = 0;
    bool nameDirty = false;

    theme::Lcd lcd;
    juce::TextEditor nameEdit;
    juce::ToggleButton liveToggle{"Live edit"}, fxProgChange{"FX follow program changes"};
    juce::ComboBox fxChannel;
    juce::Label fxChannelLabel{{}, "Effects from channel"};
    juce::TextButton getBtn{"Get from QS"}, sendBtn{"Send to QS"}, storeBtn{"Store to User mix..."}, openBtn{"Open .syx"},
        saveBtn{"Save .syx"}, fxBtn{"Edit the mix's effects"};
    std::unique_ptr<juce::Component> columns;  // column titles, scroll with the rows
    juce::Viewport view;
    juce::Component content;
    juce::OwnedArray<Row> rows;
    std::unique_ptr<juce::FileChooser> chooser;
};
