// QS Studio - sound player: plays the samples of a QCard image or a QS internal sample ROM dump, mapped across the
// keyboard the way the card or ROM's voice directory maps them (key splits, velocity layers, loops).
// A basic sample player, not the QS synth engine: no QS filters, LFOs, envelopes or effects yet.
// Shared by the app (Play > Sound Player) and the plugin.
#pragma once
#include <JuceHeader.h>

#include <memory>

#include "../core/qs_card.h"

namespace qsplayer {

using Image = std::shared_ptr<const qs::CardImage>;

// Load a card image or an internal ROM dump. Empty with error text on failure.
inline Image loadFile(const juce::File& f, juce::String& error) {
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb)) {
        error = "Couldn't read " + f.getFileName();
        return {};
    }
    auto ptr = static_cast<const uint8_t*>(mb.getData());
    qs::Bytes raw(ptr, ptr + mb.getSize());
    auto img = std::make_shared<qs::CardImage>();
    std::string err;
    if (qs::looksLikeInternalRom(raw) ? qs::loadInternalRom(raw, *img, err) : qs::loadCardImage(raw, *img, err)) {
        if (img->voices.empty()) {
            error = f.getFileName() + " has programs but no samples (a program card).";
            return {};
        }
        if (img->samplesLookWrong) {
            error = f.getFileName() + "'s voice list doesn't point at audio in this file (" + juce::String(img->sampleCheck) +
                    "). QS Studio won't play it, to spare your ears. Please send the file so the format can be worked out.";
            return {};
        }
        return img;
    }
    error = juce::String(err);
    return {};
}

// How much of a voice can play from this image (a ROM dump of one chip can't play the other chip's zones).
inline int availablePercent(const qs::CardImage& img, const qs::Voice& v) {
    if (v.zones.empty()) return 0;
    int ok = 0;
    for (auto& z : v.zones) ok += img.zoneAvailable(z);
    return int(100 * ok / int(v.zones.size()));
}

class Sound : public juce::SynthesiserSound {
public:
    Sound(Image i, int voiceIndex) : img(std::move(i)), voice(&img->voices[size_t(voiceIndex)]), regions(qs::voiceRegions(*voice)) {}
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }

    const qs::Region* find(int note, int velocity) const {
        for (auto& r : regions)
            if (note >= r.lo && note <= r.hi && velocity >= r.lovel && velocity <= r.hivel && img->zoneAvailable(*r.zone)) return &r;
        return nullptr;
    }

    Image img;
    const qs::Voice* voice;
    std::vector<qs::Region> regions;
};

struct Settings {
    std::atomic<float> attack{0.002f}, decay{1.0f}, sustain{1.0f}, release{0.35f}, gain{0.5f};
};

class Voice : public juce::SynthesiserVoice {
public:
    explicit Voice(const Settings& s) : settings(s) {}
    bool canPlaySound(juce::SynthesiserSound* s) override { return dynamic_cast<Sound*>(s) != nullptr; }

    void startNote(int note, float velocity, juce::SynthesiserSound* s, int) override {
        auto* snd = static_cast<Sound*>(s);
        const auto* r = snd->find(note, juce::jlimit(1, 127, int(velocity * 127.0f + 0.5f)));
        if (r == nullptr) {
            clearCurrentNote();
            return;
        }
        img = snd->img;
        const auto& z = *r->zone;
        base = img->zoneWordBase(z);
        start = z.start;
        end = z.end;
        // Every QS sample loops: from start+offset to the end (single-cycle waves have offset 0 and loop whole; drums
        // loop a few silent words at the end). The last word repeats the loop-start word, so the loop runs to end-1.
        looping = z.start + z.loopOffset + 2 < z.end;
        loopStart = double(z.start + z.loopOffset);
        pos = double(z.start);
        // QS samples are 48 kHz.
        step = std::pow(2.0, (note - z.root) / 12.0) * 48000.0 / getSampleRate();
        level = velocity * velocity;
        adsr.setSampleRate(getSampleRate());
        adsr.setParameters({settings.attack.load(), settings.decay.load(), settings.sustain.load(), settings.release.load()});
        adsr.noteOn();
    }

    void stopNote(float, bool allowTailOff) override {
        if (allowTailOff) adsr.noteOff();
        else {
            adsr.reset();
            clearCurrentNote();
            img.reset();
        }
    }

    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}

    using juce::SynthesiserVoice::renderNextBlock;
    void renderNextBlock(juce::AudioBuffer<float>& out, int startSample, int n) override {
        if (img == nullptr) return;
        const float g = settings.gain.load() * level;
        const double last = looping ? double(end - 1) : double(end);
        for (int i = 0; i < n; ++i) {
            if (pos >= last) {
                if (!looping) {
                    clearCurrentNote();
                    img.reset();
                    return;
                }
                pos -= last - loopStart;
            }
            const auto w = uint32_t(pos);
            const float frac = float(pos - double(w));
            const float a = img->sampleAt(base + w) / 32768.0f, b = img->sampleAt(base + w + 1) / 32768.0f;
            const float v = (a + (b - a) * frac) * g * adsr.getNextSample();
            for (int ch = 0; ch < out.getNumChannels(); ++ch) out.addSample(ch, startSample + i, v);
            pos += step;
            if (!adsr.isActive()) {
                clearCurrentNote();
                img.reset();
                return;
            }
        }
    }

private:
    const Settings& settings;
    Image img;
    juce::ADSR adsr;
    uint32_t base = 0, start = 0, end = 0;
    double pos = 0, step = 1, loopStart = 0;
    bool looping = false;
    float level = 1.0f;
};

// The engine: a 32-voice synth whose single sound is the chosen voice. Safe to switch voices while playing.
class Engine {
public:
    Engine() {
        for (int i = 0; i < 32; ++i) synth.addVoice(new Voice(settings));
    }

    void select(Image img, int voiceIndex) {
        synth.allNotesOff(0, false);
        synth.clearSounds();
        if (img != nullptr && voiceIndex >= 0 && voiceIndex < int(img->voices.size())) synth.addSound(new Sound(img, voiceIndex));
    }

    void prepare(double sampleRate) { synth.setCurrentPlaybackSampleRate(sampleRate); }
    void render(juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, int startSample, int n) {
        synth.renderNextBlock(buffer, midi, startSample, n);
        // Soft limit: many full-scale samples at once (or a bad file) can't jump past about -1 dBFS.
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
            auto* d = buffer.getWritePointer(ch, startSample);
            for (int i = 0; i < n; ++i) d[i] = 0.9f * std::tanh(d[i] / 0.9f);
        }
    }
    void allNotesOff() { synth.allNotesOff(0, true); }

    Settings settings;

private:
    juce::Synthesiser synth;
};

}  // namespace qsplayer
