#pragma once
#include <JuceHeader.h>

#include <map>

#include "../core/qs_card.h"
#include "../core/qs_cardbuild.h"
#include "MidiLink.h"
#include "Session.h"
#include "Pages.h"
#include "PerformanceControls.h"
#include "Shell.h"
#include "Theme.h"
#include "Updater.h"
#include "Welcome.h"

class LibraryTab;
class EditorTab;
class MixTab;
class PlayerTab;
class EffectsTab;

// Shared helpers
juce::String hex(const qs::Bytes& b, size_t maxBytes = 32);

class CardTab : public juce::Component {
public:
    explicit CardTab(MidiLink& m);
    void resized() override;

private:
    void openImage();
    void showCard();
    void exportWavs(bool sfz);
    void buildFromWavs();
    void saveBuiltCard(const qs::CardBuild& b);
    void testReader();
    void readCard();
    void dumpRom();
    void writeCard();
    bool requireMidi();

    MidiLink& midi;
    qs::CardImage card;
    juce::File cardFile;
    juce::TextButton openBtn{"Open card image"}, wavBtn{"Export WAVs"}, sfzBtn{"Export SFZ instruments"},
        testBtn{"Test card reader"}, readBtn{"Read card from QS"}, writeBtn{"Write image to FLASH card"}, romBtn{"Dump internal sample ROM..."},
        buildBtn{"Build a card from WAV files..."};
    juce::ComboBox sizeBox;
    juce::Label sizeLabel{{}, "Card size"};
    Section imageSec{"Look inside a card image", "Open a card image (.img or .bin) to list its samples, programs and mixes, and export the samples as WAV or SFZ.",
        "For example a Sound Bridge image or a card you read with Read card from QS. SFZ instruments open in most software samplers."};
    Section buildSec{"Make your own card", "Turn WAV files into a QS card image. New and untested on hardware: use a FLASH card you can rewrite.",
        "Files named like Piano_C3.wav, Piano_C4.wav become one voice split across the keys (C3 = 60); others become a voice each. Loops in the WAVs are kept and each voice gets a program. The image opens here, ready for the Sound Player or Write."};
    Section slotSec{"The card in the QS", "Test, read or write the FLASH card in the QS. Power the QS off before inserting or removing a card.",
        "Read saves a FLASH sample card to an image; Write erases the card and puts an image on it. RAM512 program cards are on the Programs page (Card banks) and Move banks."};
    Section romSec{"Built-in sample ROM", "Saves the synth's built-in sample ROM, on units whose card reads return it (the QSR does).",
        "About 1 hour per 8 MB, and it resumes if you stop. Only the first 8 MB can be reached over MIDI. Keep the file private: the samples are Alesis's."};
    juce::TextEditor info;
    std::unique_ptr<juce::FileChooser> chooser;
};

class MainComponent : public juce::Component, private MidiLink::Listener {
public:
    MainComponent();
    ~MainComponent() override;
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void sysexReceived(const qs::Bytes& msg) override;
    void showPage(int index);
    void showPageNamed(const juce::String& title);
    int pageIndex(const juce::String& title) const;
    void applyLook();  // panel colours from the look setting or the connected model
    void updatePill();
    void fillControllerBox();

    struct PageInfo { juce::String group, title, summary, help, tip; juce::Component* comp; };

    Session session;  // first: everything below uses it
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<PageInfo> pages;
    int current = -1;
    LibraryTab* library = nullptr;
    EditorTab* editor = nullptr;
    MixTab* mixEditor = nullptr;
    PlayerTab* player = nullptr;
    EffectsTab* fxEditor = nullptr;
    ConnectionPage* connection = nullptr;
    GlobalPage* globalPage = nullptr;
    BackupPage* backupPage = nullptr;

    // Setup guide and updates.
    WelcomeGuide guide;
    juce::TextButton guideBtn{"Setup guide"}, updatesBtn{"Updates..."}, updateAvailableBtn;
    std::optional<updates::Info> availableUpdate;
    std::unique_ptr<juce::Thread> updateCheck;
    void showGuide(bool show);
    void checkForUpdates(bool userAsked);
    void showUpdate(const updates::Info& info);
    void installUpdate(const updates::Info& info);

    Sidebar sidebar;
    PageHeader header;
    StatusPill pill;
    juce::Label appTitle{{}, "QS Studio"}, lookLabel{{}, "Panel look"}, chLabel{{}, "MIDI ch"}, ctrlLabel{{}, "Controller in"};
    juce::ComboBox lookBox, channelBox, controllerBox;
    juce::Array<juce::MidiDeviceInfo> controllerDevices;

    juce::MidiKeyboardComponent keyboard{session.keys, juce::MidiKeyboardComponent::horizontalKeyboard};
    PerformanceControls perf{session};
    juce::Label velLabel{{}, "Velocity"}, statusLabel;
    juce::Slider velSlider{juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight};
    juce::TextButton notesOffBtn{"All notes off"}, keysBtn{"Hide keyboard"};
    juce::ToggleButton computerKeys{"Play from computer keys"};
    juce::TextButton aftertouchBtn{"Aftertouch..."};
    juce::TooltipWindow tooltips{this, 600};

    // Computer keyboard as a piano (QS SoundEditor had this): A W S E D F T G Y H U J K O L P ; ' play notes,
    // Z / X shift the octave, - / = change the velocity, Space stops all notes.
    bool keyPressed(const juce::KeyPress& k) override;
    bool keyStateChanged(bool isKeyDown) override;
    int keyOctave = 4;
    std::map<int, int> heldComputerKeys;  // key code -> note
};
