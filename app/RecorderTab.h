// Record: capture the QS outputs through an audio interface, by hand or note by note (auto-sampler).
#pragma once
#include <JuceHeader.h>

#include <atomic>

#include "Session.h"
#include "Shell.h"
#include "Theme.h"

class AudioRecorder : public juce::AudioIODeviceCallback {
public:
    AudioRecorder() { writerThread.startThread(); }
    ~AudioRecorder() override { stop(); }

    bool start(const juce::File& f, int bits, juce::String& error);
    void stop();
    bool isRecording() const { return active.load() != nullptr; }
    double seconds() const { return sampleRate > 0 ? double(written.load()) / sampleRate : 0.0; }
    float takePeak(int ch) { return (ch == 0 ? peakL : peakR).exchange(0.0f); }
    double rate() const { return sampleRate; }
    std::atomic<bool> monitor{false};
    std::atomic<float> monitorGain{0.7f};

    void audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut, int n,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* d) override { sampleRate = d->getCurrentSampleRate(); }
    void audioDeviceStopped() override { sampleRate = 0; }

private:
    juce::TimeSliceThread writerThread{"QS Studio audio writer"};
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threaded;
    juce::CriticalSection writerLock;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> active{nullptr};
    std::atomic<juce::int64> written{0};
    std::atomic<float> peakL{0}, peakR{0};
    double sampleRate = 0;
};

class RecorderTab : public juce::Component, private juce::Timer {
public:
    explicit RecorderTab(Session& s);
    ~RecorderTab() override;
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class AutoSampler;
    void timerCallback() override;
    juce::File outputFolder() const;
    void toggleRecord();
    void startAuto();
    void stopAuto();
    void saveSettings();

    Session& session;
    juce::AudioDeviceManager& devices;  // shared with the Sound Player (Session::audio)
    AudioRecorder recorder;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
    std::unique_ptr<AutoSampler> sampler;

    theme::Lcd lcd;
    Section audioSec{"1. Audio in and out", "Pick the audio interface and the input(s) the QS's main outputs are plugged into.",
        "One input for mono, two for stereo. Pick an output if you want to hear the QS through this computer."},
        recSec{"2. Record by hand", "Click Record, play the QS from the keyboard below or your controller, then click Stop. Each take is "
                                     "saved as a WAV file in the recordings folder."},
        autoSec{"3. Auto-sample a program", "Plays each note in the range on the QS and saves it as its own WAV, named with the note and "
                                             "velocity, ready for a sampler. Select the program on the Programs page first."};
    juce::Label prefixLabel{{}, "File name"}, lowLabel{{}, "Lowest note"}, highLabel{{}, "Highest note"}, stepLabel{{}, "Every"},
        velLabel{{}, "Velocities"}, holdLabel{{}, "Hold (ms)"}, tailLabel{{}, "Release tail (ms)"}, bitsLabel{{}, "Bit depth"};
    juce::TextEditor prefix, velocities;
    juce::Slider low{juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft}, high{juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft},
        stepSemis{juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft}, hold{juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft},
        tail{juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft};
    juce::ComboBox bits;
    juce::TextButton recBtn{"Record"}, autoBtn{"Auto-sample notes"}, folderBtn{"Open recordings folder"}, nameFromProgBtn{"Use program name"};
    juce::Label status;
    juce::ToggleButton monitorToggle{"Monitor (hear the inputs)"};
    juce::Slider monitorLevel{juce::Slider::LinearHorizontal, juce::Slider::NoTextBox};
    float meterL = 0, meterR = 0;
    juce::Rectangle<int> meterArea;
};
