// Undo/redo for the editors: snapshots of the edited data (a program, mix or effect patch is a few hundred bytes).
// Quick runs of changes (dragging a slider) collapse into one step.
#pragma once
#include <JuceHeader.h>

#include <vector>

#include "../core/qs_sysex.h"

class UndoHistory {
public:
    // Start over from this state (a newly opened program, mix or effect).
    void reset(const qs::Bytes& state) {
        current = state;
        undoStack.clear();
        redoStack.clear();
        lastChange = 0;
    }

    // Call after every edit with the new state. label names the step ("Volume", "Swap sounds 1 and 3").
    void changed(const qs::Bytes& state, const juce::String& label) {
        if (state == current) return;
        const auto now = juce::Time::getMillisecondCounter();
        // The same control moved again within a moment: one step, not dozens.
        if (!(label == lastLabel && now - lastChange < 700 && !undoStack.empty())) {
            undoStack.push_back({current, label});
            if (undoStack.size() > 200) undoStack.erase(undoStack.begin());
        }
        current = state;
        redoStack.clear();
        lastChange = now;
        lastLabel = label;
    }

    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }
    juce::String undoLabel() const { return canUndo() ? undoStack.back().label : juce::String(); }
    juce::String redoLabel() const { return canRedo() ? redoStack.back().label : juce::String(); }

    // Returns the state to load, or nothing.
    std::optional<qs::Bytes> undo() {
        if (undoStack.empty()) return std::nullopt;
        auto step = undoStack.back();
        undoStack.pop_back();
        redoStack.push_back({current, step.label});
        current = step.state;
        lastLabel = {};
        return current;
    }
    std::optional<qs::Bytes> redo() {
        if (redoStack.empty()) return std::nullopt;
        auto step = redoStack.back();
        redoStack.pop_back();
        undoStack.push_back({current, step.label});
        current = step.state;
        lastLabel = {};
        return current;
    }

private:
    struct Step {
        qs::Bytes state;
        juce::String label;
    };
    qs::Bytes current;
    std::vector<Step> undoStack, redoStack;
    juce::uint32 lastChange = 0;
    juce::String lastLabel;
};

// Undo / Redo buttons that follow a history and show what they'd undo.
struct UndoButtons {
    juce::TextButton undo{"Undo"}, redo{"Redo"};
    void refresh(const UndoHistory& h) {
        undo.setEnabled(h.canUndo());
        redo.setEnabled(h.canRedo());
        undo.setTooltip(h.canUndo() ? "Undo " + h.undoLabel() + " (Ctrl+Z)" : juce::String("Nothing to undo"));
        redo.setTooltip(h.canRedo() ? "Redo " + h.redoLabel() + " (Ctrl+Y)" : juce::String("Nothing to redo"));
    }
};

// True for Ctrl+Z / Cmd+Z, and Ctrl+Y / Ctrl+Shift+Z / Cmd+Shift+Z.
inline bool isUndoKey(const juce::KeyPress& k) {
    return k.getModifiers().isCommandDown() && !k.getModifiers().isShiftDown() && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z');
}
inline bool isRedoKey(const juce::KeyPress& k) {
    return k.getModifiers().isCommandDown() &&
           (k.getKeyCode() == 'Y' || k.getKeyCode() == 'y' || (k.getModifiers().isShiftDown() && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z')));
}
