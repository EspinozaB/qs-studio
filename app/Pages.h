// Pages for setup and transfer: Connection, Backup & Restore, Move Banks (QS6 <-> QSR), Global Settings.
#pragma once
#include <JuceHeader.h>

#include "Session.h"
#include "Shell.h"
#include "Theme.h"

class LibraryTab;

class ConnectionPage : public juce::Component, private MidiLink::Listener {
public:
    explicit ConnectionPage(Session& s);
    ~ConnectionPage() override;
    void resized() override;
    void paint(juce::Graphics& g) override;
    void connectSaved();  // reconnect to the last ports at startup, if they exist
    void connectTo(const juce::String& in, const juce::String& out);  // from the setup guide
    std::function<void()> onPortsChanged;

private:
    void sysexReceived(const qs::Bytes& msg) override;
    void refreshDevices();
    void connect();
    void log(const juce::String& s);

    Session& session;
    Section steps{"Connect your QS", "Two MIDI cables (QS OUT to interface IN, interface OUT to QS IN), Global > I/O on the QS set to MIDI, then pick the ports and Connect.",
        "QS Studio talks to the synth and listens to its answers, so both cables are needed. I/O is the QS's serial port setting; on anything but MIDI the QS ignores its MIDI IN. After Connect, QS Studio asks the synth what it is."};
    juce::Label inLabel{{}, "MIDI In (from the QS)"}, outLabel{{}, "MIDI Out (to the QS)"}, status;
    juce::ComboBox inBox, outBox;
    juce::TextButton refreshBtn{"Refresh list"}, connectBtn{"Connect"}, identifyBtn{"Ask the QS what it is"};
    juce::TextEditor logBox;
    Section filesSec{"Your files", ""};
    juce::TextButton openFolderBtn{"Open the folder"}, exportLibBtn{"Export a library..."}, importLibBtn{"Import a library..."},
        openLogsBtn{"Open the logs"};
    juce::ToggleButton logTraffic{"Log MIDI traffic"};
    juce::ComboBox paceBox;
    juce::Label paceLabel{{}, "Transfer pace"};
    std::unique_ptr<juce::FileChooser> chooser;
    void updateFilesText();
public:
    void refreshFiles() { updateFilesText(); }
};

class BackupPage : public juce::Component, private MidiLink::Listener {
public:
    explicit BackupPage(Session& s);
    ~BackupPage() override;
    void resized() override;
    void paint(juce::Graphics& g) override;

    void backupNow(std::function<void(juce::File)> then = nullptr);  // ask the QS for everything and save it
    void restoreFromFile();

private:
    void sysexReceived(const qs::Bytes& msg) override;
    void preview(const std::vector<qs::Bytes>& msgs, const juce::String& title);
    void restoreLoaded();

    Session& session;
    Section backupSec{"Back up the synth", "Saves your User programs, mixes, effects and Global settings to one .syx file. Changes nothing on the QS.",
        "128 User programs, 100 User mixes, their 128 effects and the Global settings. Do this before anything that writes to the synth."};
    Section restoreSec{"Restore a backup", "Sends a backup to the QS. It REPLACES the User programs, mixes and effects.",
        "Make a fresh backup first if you might want the current ones back. Global settings are only sent if you tick them."};
    Section captureSec{"Capture by hand (advanced)", "Records SysEx you send from the QS's own STORE menu.",
        "For example a single program. Click Start, send from the QS, then Stop and Save."};
    juce::TextButton backupBtn{"Back up now..."}, openBtn{"Choose a backup file..."}, restoreBtn{"Restore it to the QS"},
        capStart{"Start"}, capStop{"Stop"}, capSave{"Save..."};
    juce::ToggleButton includeGlobal{"Also restore Global settings (MIDI channel, controller numbers, Program Select)"};
    juce::Label capCount, previewTitle;
    juce::TextEditor previewBox;
    std::vector<qs::Bytes> loaded, captured;
    bool capturing = false;
    std::unique_ptr<juce::FileChooser> chooser;
};

class MoveBanksPage : public juce::Component {
public:
    MoveBanksPage(Session& s, BackupPage& backup, LibraryTab& library);
    std::function<void()> goToBackup, goToPrograms;
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    Session& session;
    BackupPage& backupPage;
    LibraryTab& library;
    Section why{"Why a bank needs converting", "The QS6 and the QS7/QS8/QSR number their samples differently, so a program can play the wrong sample on the other synth.",
        "A program stores only \"group, sample number\". Converting rewrites each program to use the sample with the same name on the other synth, and reports any sample the other synth doesn't have."};
    Section step1{"1. Back up the synth the bank is on", "Connect that synth and save its whole User bank.",
        "Programs, mixes and effects travel together, so patches keep their reverb and chorus."};
    Section step2{"2. Convert the backup", "Pick the backup file and the two synths.",
        "You get a new .syx for the other synth and a report listing every change and every sample without a match."};
    Section step3{"3. Put it on the other synth", "Connect the other synth, then restore the converted file or copy it onto a RAM card bank.",
        "A RAM512 holds 8 banks, so a card bank doesn't replace anything in the synth's User memory."};
    juce::TextButton backupBtn{"Back up the connected synth..."}, convertBtn{"Convert a backup..."}, restoreBtn{"Restore to the User bank..."},
        cardBtn{"Copy onto a RAM card bank..."};
};

class GlobalPage : public juce::Component, private MidiLink::Listener {
public:
    explicit GlobalPage(Session& s);
    ~GlobalPage() override;
    void resized() override;
    void paint(juce::Graphics& g) override;
    void refreshFromSession();

private:
    void sysexReceived(const qs::Bytes& msg) override;
    void sendGlobal(int page, int pot, int value);

    Session& session;
    Section settingsSec{"Global settings on the QS", "Stored in the synth for every program. Click Read from QS first; changes go to the QS straight away.",
        "Global settings are also part of every backup."};
    Section tryCtl{"Try the controllers", "Move these to send Controllers A-D to the QS on the current MIDI channel.",
        "The QS6 has only the Controller A slider on its panel; B, C and D still work over MIDI."};
    juce::TextButton readBtn{"Read from QS"};
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<juce::Slider> ccBoxes;  // A-D, Pedal 1, Pedal 2
    juce::ComboBox progSelect;
    juce::ToggleButton gmToggle{"General MIDI mode"};
    juce::OwnedArray<juce::Slider> tryers;
    juce::OwnedArray<juce::Label> tryLabels;
    bool loading = false;
};
