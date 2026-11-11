// QS Sound Player plugin: plays the samples of a QCard image or a QS internal sample ROM dump in a DAW.
// The same engine as QS Studio's Play > Sound Player page (app/QsPlayer.h).
#pragma once
#include <JuceHeader.h>

#include "../app/QsPlayer.h"

class QsPlayerProcessor : public juce::AudioProcessor {
public:
    QsPlayerProcessor();

    void prepareToPlay(double sampleRate, int) override { engine.prepare(sampleRate); }
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        const auto out = l.getMainOutputChannelSet();
        return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
    }
    using juce::AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "QS Sound Player"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // The project remembers the file's path and the voice, not the samples (they stay in your own file).
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    bool loadFile(const juce::File& f, juce::String& error);
    void selectVoice(int index);

    qsplayer::Engine engine;
    qsplayer::Image image;
    juce::File file;
    int voice = 0;
    juce::AudioProcessorValueTreeState params;
    std::function<void()> onChange;  // the editor refreshes

private:
    void syncParams();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QsPlayerProcessor)
};
