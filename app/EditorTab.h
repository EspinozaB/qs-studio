// Program editor: 4 sound panels plus full detail pages, edits sent live to the QS edit buffer.
#pragma once
#include <JuceHeader.h>

#include <map>

#include "Session.h"
#include "Theme.h"

// One parameter as a knob, a drop-down or an on/off switch, with a caption.
class ParamControl : public juce::Component {
public:
    using Change = std::function<void(int sound, const qs::ParamDef&, int drum, int value)>;
    ParamControl(const qs::ParamDef& d, int sound, int drum, Change cb, juce::String caption = {});
    void load(const qs::Program& p);
    void paint(juce::Graphics& g) override;
    void resized() override;

    const qs::ParamDef& def;
    const int sound, drum;

    static juce::String valueLabel(const qs::ParamDef& d, int raw);
    // Optional: names that depend on the program or the model (sample names, drum group names).
    std::function<juce::String(int raw)> labelFn;
    juce::String label(int raw) const { return labelFn ? labelFn(raw) : valueLabel(def, raw); }

private:
    enum class Kind { Knob, Combo, Toggle } kind;
    juce::Slider knob{juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox};
    juce::ComboBox combo;
    juce::ToggleButton toggle;
    juce::String caption;
    Change onChange;
    bool loading = false;
};

class EditorTab;

class SoundPanel : public juce::Component {
public:
    SoundPanel(EditorTab& e, int sound);
    void load(const qs::Program& p);
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void rebuild(bool drum);
    ParamControl* add(const qs::ParamDef& d, juce::String caption = {}, int drumIdx = 0);

    EditorTab& editor;
    const int sound;
    bool isDrum = false, enabled = true;
    int drumIndex = 0;
    juce::ToggleButton power;
    juce::ComboBox drumPick;
    juce::OwnedArray<ParamControl> controls;
    juce::OwnedArray<juce::TextButton> details;
};

class EditorTab : public juce::Component, private MidiLink::Listener, private juce::Timer {
public:
    explicit EditorTab(Session& s);
    ~EditorTab() override;
    void paint(juce::Graphics& g) override;
    void resized() override;

    void openProgram(const qs::Program& p, const juce::String& where);
    // Edit the program a mix channel plays, in the QS's mix channel edit buffer (opcode 02/03 edit# 1-16).
    void openMixChannel(int channel, const juce::String& mixName);
    void showDetail(int sound, const juce::String& section, int drum = 0);
    void showOverview();
    juce::String soundSummary(int sound) const;  // "Tympani (Percussion)   C-2 to G8   Poly"

    // Called by controls.
    void paramChanged(int sound, const qs::ParamDef& d, int drum, int value);
    void setEnabled(int sound, bool on);
    const qs::Program& program() const { return prog; }
    void attachLabels(ParamControl& c);  // sample and drum-group names for the connected model

private:
    void sysexReceived(const qs::Bytes& msg) override;
    void timerCallback() override;
    void loadAll();
    void updateLcd();
    void getFromQs();
    void sendToQs();
    void storeToUser();
    void saveSyx();
    void openSyx();
    void flushNow();

    Session& session;
    qs::Program prog;
    int editNumber = 0;  // 0 = Program mode edit buffer, 1-16 = Mix channel 1-16
    juce::String where = "No program loaded";
    bool awaitingDump = false;
    int requestsSent = 0;
    juce::uint32 nextRequestAt = 0;
    void requestEditBuffer(int firstDelayMs);

    // Live edit queue: latest value per (sound, param) sent every timer tick; drum/name edits send a full dump.
    std::map<std::pair<int, const qs::ParamDef*>, int> pending;
    bool dumpDirty = false;
    juce::uint32 lastDumpEdit = 0;

    theme::Lcd lcd;
    juce::TextEditor nameEdit;
    juce::ToggleButton liveToggle{"Live edit"};
    juce::TextButton getBtn{"Get from QS"}, sendBtn{"Send to QS"}, storeBtn{"Store to User..."}, openBtn{"Open .syx"},
        saveBtn{"Save .syx"}, backBtn{"< All sounds"}, fxBtn{"Effect..."}, compareBtn{"Compare"};
    qs::Program original;  // as opened or received, for Compare
    bool comparing = false;
    void setComparing(bool on);
    juce::OwnedArray<SoundPanel> panels;

    // Detail page
    bool detailMode = false;
    int detailSound = 0, detailDrum = 0;
    juce::String detailSection;
    juce::OwnedArray<juce::TextButton> soundTabs, sectionTabs;
    juce::OwnedArray<ParamControl> detailControls;
    juce::OwnedArray<juce::Label> detailHeaders;
    std::vector<int> detailGroupStart;  // index into detailControls where each titled row group starts
    juce::Viewport detailView;
    juce::Component detailContent;
    std::unique_ptr<juce::FileChooser> chooser;
};
