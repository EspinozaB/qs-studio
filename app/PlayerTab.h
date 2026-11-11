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
    Section sourceSec{"Sound source", "Open a QCard image (.img, .bin) or the internal sample ROM dump from Cards & ROM. QS Studio "
                                      "lists its voices; pick one and play it from the keyboard, your controller or the computer keys."};
    Section soundSec{"Sound", "This plays the raw samples the way the card or ROM maps them across the keys (splits, velocity "
                              "layers, loops). It's not the QS synth engine yet: no QS filters, envelopes or effects."};
    juce::TextButton openBtn{"Open a card image or ROM dump..."}, audioBtn{"Audio output..."};
    juce::Label fileLabel, countLabel;
    juce::TextEditor search;
    juce::ListBox list{"voices", this};
    juce::Slider attack, release, volume;
    juce::Label attackLabel{{}, "Attack"}, releaseLabel{{}, "Release"}, volumeLabel{{}, "Volume"};
    juce::ToggleButton alsoQs{"Also play the QS"};
    std::unique_ptr<juce::FileChooser> chooser;
};
