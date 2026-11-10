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
    std::function<void()> onPortsChanged;

private:
    void sysexReceived(const qs::Bytes& msg) override;
    void refreshDevices();
    void connect();
    void log(const juce::String& s);

    Session& session;
    Section steps{"Connect your QS", "1. Connect the QS's MIDI OUT to your interface's MIDI IN, and the interface's MIDI OUT to the QS's MIDI IN "
                                     "(both cables are needed: QS Studio talks to the synth and listens to its answers).\n"
                                     "2. On the QS, set Global > I/O (the serial port setting) to MIDI.\n"
                                     "3. Pick the interface below and click Connect. QS Studio then asks the synth what it is."};
    juce::Label inLabel{{}, "MIDI In (from the QS)"}, outLabel{{}, "MIDI Out (to the QS)"}, status;
    juce::ComboBox inBox, outBox;
    juce::TextButton refreshBtn{"Refresh list"}, connectBtn{"Connect"}, identifyBtn{"Ask the QS what it is"};
    juce::TextEditor logBox;
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
    Section backupSec{"Back up the synth", "Saves everything in the QS's memory that you can change: the 128 User programs, 100 User mixes, "
                                           "their 128 effects and the Global settings, into one .syx file on this computer. "
                                           "Nothing on the QS changes. Do this before anything that writes to the synth."};
    Section restoreSec{"Restore a backup", "Sends a backup file to the QS. This REPLACES the User programs, mixes and effects on the QS with "
                                           "the ones in the file. Make a fresh backup first if you might want the current ones back."};
    Section captureSec{"Capture by hand (advanced)", "Records whatever SysEx the QS sends, for dumps you start from the QS's own STORE "
                                                     "menu (for example a single program). Click Start, send from the QS, then Stop and Save."};
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
    Section why{"Why a bank needs converting", "The QS6 has its own sample ROM. The QS7, QS8 and QSR share a different one. A program doesn't "
                                               "store its samples, only \"group, sample number\", and the two ROMs number many samples "
                                               "differently. So a QS6 program can play a different sample on a QSR (and the other way round). "
                                               "Converting rewrites each program to use the sample with the same name on the other synth, and "
                                               "reports any sample the other synth doesn't have."};
    Section step1{"1. Back up the synth the bank is on", "Connect that synth and save its whole User bank. Programs, mixes and effects "
                                                         "travel together, so patches keep their reverb and chorus."};
    Section step2{"2. Convert the backup", "Pick the backup file and the two synths. You get a new .syx for the other synth and a report "
                                           "listing every change and every sample without a match."};
    Section step3{"3. Put it on the other synth", "Connect the other synth, then either restore the converted file to its User bank, or "
                                                  "copy it onto a RAM card bank (a RAM512 holds 8 banks) so it doesn't replace anything."};
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
    Section settingsSec{"Global settings on the QS", "These live in the synth and apply to every program. Changes are sent to the QS straight away "
                                                     "(they're also part of every backup). Click Read from QS first to see the current values."};
    Section tryCtl{"Try the controllers", "Move these to send each controller to the QS on the current MIDI channel. The QS6 has only the "
                                          "Controller A slider on its panel; B, C and D still work over MIDI, so you can use them here."};
    juce::TextButton readBtn{"Read from QS"};
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<juce::Slider> ccBoxes;  // A-D, Pedal 1, Pedal 2
    juce::ComboBox progSelect;
    juce::ToggleButton gmToggle{"General MIDI mode"};
    juce::OwnedArray<juce::Slider> tryers;
    juce::OwnedArray<juce::Label> tryLabels;
    bool loading = false;
};
