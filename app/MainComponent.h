#pragma once
#include <JuceHeader.h>

#include <map>

#include "../core/qs_card.h"
#include "MidiLink.h"
#include "Session.h"
#include "Pages.h"
#include "PerformanceControls.h"
#include "Shell.h"
#include "Theme.h"

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
    void testReader();
    void readCard();
    void dumpRom();
    void writeCard();
    bool requireMidi();

    MidiLink& midi;
    qs::CardImage card;
    juce::File cardFile;
    juce::TextButton openBtn{"Open card image"}, wavBtn{"Export WAVs"}, sfzBtn{"Export SFZ instruments"},
        testBtn{"Test card reader"}, readBtn{"Read card from QS"}, writeBtn{"Write image to FLASH card"}, romBtn{"Dump internal sample ROM..."};
    juce::ComboBox sizeBox;
    juce::Label sizeLabel{{}, "Card size"};
    Section imageSec{"Look inside a card image", "Open a card image file from your computer (.img or .bin, for example a Sound Bridge image or a "
                                                 "card you read with the button below) to list its samples, programs and mixes, and export the "
                                                 "samples as WAV files or SFZ instruments for a software sampler."};
    Section slotSec{"The card in the QS", "Power the QS off before inserting or removing a card. Test checks that the QS answers card "
                                          "requests. Read saves a FLASH sample card to an image; Write puts an image onto a FLASH card. "
                                          "RAM512 program cards can't be read or written this way: use Move Banks for those."};
    Section romSec{"Built-in sample ROM", "On units where Test says \"built-in sound ROM\" (the QSR does), card reads return the synth's own "
                                          "sample ROM. This saves it to a file, about 1 hour per 8 MB, and resumes if you stop. "
                                          "Only the first 8 MB can be reached over MIDI. Keep the file private: the samples are Alesis's."};
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
    juce::TooltipWindow tooltips{this, 600};

    // Computer keyboard as a piano (QS SoundEditor had this): A W S E D F T G Y H U J K O L P ; ' play notes,
    // Z / X shift the octave, - / = change the velocity, Space stops all notes.
    bool keyPressed(const juce::KeyPress& k) override;
    bool keyStateChanged(bool isKeyDown) override;
    int keyOctave = 4;
    std::map<int, int> heldComputerKeys;  // key code -> note
};
