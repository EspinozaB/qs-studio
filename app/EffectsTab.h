// Effects editor: one effect patch (configuration plus every pitch, delay, reverb, lezlie, overdrive, EQ and
// modulation parameter it has), for the program being played or for the current mix, edited live.
#pragma once
#include <JuceHeader.h>

#include <array>
#include <optional>

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
    // The program this effect belongs to, when known: shows which effect sends its sounds actually use.
    void setProgram(const std::optional<qs::Program>& p);

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
    // One effect block (Pitch, Delay, Reverb, Lezlie, Overdrive on a send, or the EQ / modulation), drawn as a card.
    struct Block {
        juce::String kind;  // "Pitch", "Delay", "Reverb", "Lezlie", "Overdrive", "Equalizer", "Modulation"
        int send = 0;       // 1-4, 0 for the patch-wide blocks
        std::vector<Control*> controls;
        juce::Rectangle<int> card;
        int feedsReverb = 0;  // a send's way into another send's reverb (no reverb of its own): that reverb's number
        juce::String title() const;
        bool isOff(const qs::Effects& fx) const;  // its mix (or level into the reverb) is 0, so it adds nothing
    };
    juce::String usedBuses;  // "1, 3" from the program's sounds, when known
    std::array<bool, 4> busUsed{{true, true, true, true}};
    bool busesKnown = false;
    void refreshCards();
    class Content;
    juce::Viewport view;
    std::unique_ptr<Content> content;
    juce::OwnedArray<Control> controls;
    std::vector<Block> blocks;
    std::unique_ptr<juce::FileChooser> chooser;
};
