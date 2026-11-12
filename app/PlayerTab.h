// Play > Sound Player: open a QCard image or a QS internal sample ROM dump and play its voices from the keyboard,
// a MIDI controller or the computer keys, through this computer's audio output.
#pragma once
#include <JuceHeader.h>

#include "QsPlayer.h"
#include "Session.h"
#include "Shell.h"
#include "Theme.h"

class PlayerTab : public juce::Component,
                  private juce::ListBoxModel,
                  private juce::AudioIODeviceCallback,
                  private juce::MidiKeyboardState::Listener {
public:
    explicit PlayerTab(Session& s);
    ~PlayerTab() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

private:
    // ListBoxModel
    int getNumRows() override { return int(rows.size()); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
    void selectedRowsChanged(int row) override;

    // Audio
    void audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut, int n,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* d) override;
    void audioDeviceStopped() override {}

    // On-screen keyboard and computer keys
    void handleNoteOn(juce::MidiKeyboardState*, int, int note, float vel) override;
    void handleNoteOff(juce::MidiKeyboardState*, int, int note, float) override;

    void openFile();
    void load(const juce::File& f);
    void filter();
    void updateInfo();
    void showAudioSettings();

    Session& session;
    qsplayer::Engine engine;
    qsplayer::Image image;
    juce::File imageFile;
    std::vector<int> rows;  // voice indices shown
    juce::MidiMessageCollector collector;
    juce::AudioBuffer<float> scratch;
    bool listening = false;

    theme::Lcd lcd;
    Section sourceSec{"Sound source", "Open a card image (.img, .bin) or a ROM dump, pick a voice and play.",
        "Play from the keyboard below, your controller or the computer keys. ROM dumps come from Cards & ROM."};
    Section soundSec{"Sound", "The raw samples as the card maps them (splits, velocity layers, loops), not the full QS engine yet.",
        "No QS filters, envelopes or effects yet: these sliders are a simple envelope and volume for the player."};
    juce::TextButton openBtn{"Open a card image or ROM dump..."}, audioBtn{"Audio output..."};
    juce::Label fileLabel, countLabel;
    juce::TextEditor search;
    juce::ListBox list{"voices", this};
    juce::Slider attack, decay, sustain, release, volume;
    juce::Label attackLabel{{}, "Attack"}, decayLabel{{}, "Decay"}, sustainLabel{{}, "Sustain"}, releaseLabel{{}, "Release"},
        volumeLabel{{}, "Volume"};
    juce::ToggleButton alsoQs{"Also play the QS"};
    std::unique_ptr<juce::FileChooser> chooser;
};
