// Background jobs that talk to the card slot over MIDI (QS SysEx opcodes 11-15).
#pragma once
#include <JuceHeader.h>

#include <algorithm>

#include "../core/qs_sysex.h"
#include "MidiLink.h"

namespace cardjobs {

inline bool isBlockReply(const qs::Bytes& m) {
    const int op = qs::opcodeOf(m);
    return op == qs::SectorWrite || op == qs::FlashNack;
}
inline bool isAckOrNack(const qs::Bytes& m) {
    const int op = qs::opcodeOf(m);
    return op == qs::FlashAck || op == qs::FlashNack;
}

struct BlockResult {
    enum Kind { Ok, BadChecksum, Nack, Timeout, Cancelled } kind = Timeout;
    qs::Bytes data;
    int nackCode = -1;
};

// Request one 1 KB block and wait for the reply.
inline BlockResult readBlock(MidiLink& midi, int sector, int block, juce::Thread* t) {
    BlockResult r;
    midi.send(qs::sectorRequest(sector, block));
    auto reply = midi.waitFor(isBlockReply, 4000, t);
    if (!reply) { r.kind = t && t->threadShouldExit() ? BlockResult::Cancelled : BlockResult::Timeout; return r; }
    if (qs::opcodeOf(*reply) == qs::FlashNack) {
        r.kind = BlockResult::Nack;
        r.nackCode = reply->size() > 6 ? (*reply)[6] : -1;
        return r;
    }
    auto sb = qs::parseSectorWrite(*reply);
    if (!sb || sb->sector != sector || sb->block != block) { r.kind = BlockResult::Timeout; return r; }
    r.data = sb->data;
    r.kind = sb->checksumOk ? BlockResult::Ok : BlockResult::BadChecksum;
    return r;
}

// Reads `sectors` x 128 KB from the card into an image file.
class ReadCardJob : public juce::ThreadWithProgressWindow {
public:
    ReadCardJob(MidiLink& m, int numSectors, juce::File dest, std::function<void(juce::String)> done)
        : ThreadWithProgressWindow("Reading card over MIDI", true, true), midi(m), sectors(numSectors), file(dest),
          onDone(std::move(done)) {}

    void run() override {
        midi.clearQueue();
        qs::Bytes image;
        image.reserve(size_t(sectors) * 128 * 1024);
        int badSums = 0;
        const auto t0 = juce::Time::getMillisecondCounter();
        for (int s = 0; s < sectors && !threadShouldExit(); ++s)
            for (int b = 0; b < 128 && !threadShouldExit(); ++b) {
                BlockResult r;
                for (int attempt = 0; attempt < 3; ++attempt) {
                    r = readBlock(midi, s, b, this);
                    if (r.kind == BlockResult::Ok || r.kind == BlockResult::Nack || r.kind == BlockResult::Cancelled) break;
                }
                if (r.kind == BlockResult::Nack) { result = "The QS reported: " + juce::String(qs::nackText(r.nackCode)); return; }
                if (r.kind == BlockResult::Timeout) {
                    result = "No reply from the QS for sector " + juce::String(s) + " block " + juce::String(b) +
                             ". Check both MIDI cables and that the QS's I/O setting is MIDI.";
                    return;
                }
                if (r.kind == BlockResult::Cancelled) break;
                if (r.kind == BlockResult::BadChecksum) ++badSums;
                image.insert(image.end(), r.data.begin(), r.data.end());
                const int done = s * 128 + b + 1, total = sectors * 128;
                setProgress(double(done) / total);
                const double el = (juce::Time::getMillisecondCounter() - t0) / 1000.0;
                setStatusMessage("Sector " + juce::String(s + 1) + "/" + juce::String(sectors) + ", block " + juce::String(b + 1) +
                                 "/128   (" + juce::String(int(el / done * (total - done) / 60.0) + 1) + " min left)");
            }
        if (image.empty()) { result = "Cancelled."; return; }
        file.replaceWithData(image.data(), image.size());
        result = "Saved " + juce::String(int(image.size() / 1024)) + " KB to " + file.getFullPathName() +
                 (threadShouldExit() ? " (partial: cancelled)" : "") +
                 (badSums ? "\n\nWarning: " + juce::String(badSums) + " blocks had checksum mismatches after 3 tries." : "");
    }

    void threadComplete(bool) override {
        if (onDone) onDone(result);
        delete this;
    }

private:
    MidiLink& midi;
    int sectors;
    juce::File file;
    std::function<void(juce::String)> onDone;
    juce::String result;
};

// Erases and programs a 5 V AMD-compatible FLASH card from an image (the Sound Bridge job).
class WriteCardJob : public juce::ThreadWithProgressWindow {
public:
    WriteCardJob(MidiLink& m, qs::Bytes img, std::function<void(juce::String)> done)
        : ThreadWithProgressWindow("Writing FLASH card over MIDI", true, true), midi(m), image(std::move(img)),
          onDone(std::move(done)) {}

    void run() override {
        const size_t sectorBytes = 128 * 1024;
        image.resize(((image.size() + sectorBytes - 1) / sectorBytes) * sectorBytes, 0xFF);
        const int sectors = int(image.size() / sectorBytes);
        midi.clearQueue();
        for (int s = 0; s < sectors && !threadShouldExit(); ++s) {
            const uint8_t* sec = image.data() + size_t(s) * sectorBytes;
            setStatusMessage("Erasing sector " + juce::String(s + 1) + "/" + juce::String(sectors));
            midi.send(qs::sectorErase(s));
            auto r = midi.waitFor(isAckOrNack, 12000, this);
            if (!check(r, "erasing sector " + juce::String(s))) return;
            for (int b = 0; b < 128 && !threadShouldExit(); ++b) {
                const uint8_t* blk = sec + size_t(b) * 1024;
                if (std::all_of(blk, blk + 1024, [](uint8_t x) { return x == 0xFF; })) continue;  // erased already
                bool ok = false;
                for (int attempt = 0; attempt < 2 && !ok; ++attempt) {
                    midi.send(qs::sectorWrite(s, b, blk));
                    auto w = midi.waitFor(isAckOrNack, 5000, this);
                    if (w && qs::opcodeOf(*w) == qs::FlashNack && w->size() > 6 && (*w)[6] == 3) continue;  // checksum: resend
                    if (!check(w, "writing sector " + juce::String(s) + " block " + juce::String(b))) return;
                    ok = true;
                }
                if (!ok) { result = "Checksum rejected twice at sector " + juce::String(s) + " block " + juce::String(b) + "."; return; }
                setProgress((s * 128 + b + 1) / double(sectors * 128));
                setStatusMessage("Writing sector " + juce::String(s + 1) + "/" + juce::String(sectors) + ", block " + juce::String(b + 1));
            }
        }
        result = threadShouldExit() ? "Cancelled. The card is only partly written." : "Card written. Power-cycle the QS to use it.";
    }

    void threadComplete(bool) override {
        if (onDone) onDone(result);
        delete this;
    }

private:
    bool check(const std::optional<qs::Bytes>& r, const juce::String& what) {
        if (!r) { result = threadShouldExit() ? "Cancelled." : "No reply from the QS while " + what + "."; return false; }
        if (qs::opcodeOf(*r) == qs::FlashNack) {
            result = "The QS refused while " + what + ": " + juce::String(qs::nackText(r->size() > 6 ? (*r)[6] : -1));
            return false;
        }
        return true;
    }

    MidiLink& midi;
    qs::Bytes image;
    std::function<void(juce::String)> onDone;
    juce::String result;
};

}  // namespace cardjobs
