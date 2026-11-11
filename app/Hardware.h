// Drawn hardware controls: wheels, faders, knobs, encoders and panel keys that look and move like the real ones.
// Everything is vector-drawn so it scales; no images.
#pragma once
#include <JuceHeader.h>

#include "Theme.h"

namespace hw {

// Printed panel lettering: light grey on the black panels, dark blue-grey on the silver QS6.2 / QS8.2.
inline juce::Colour silk() { return theme::currentStyle == theme::Style::Silver ? juce::Colour(0xff28304a) : juce::Colour(0xffd2d5d9); }
// The QS6 prints the EDIT MODE key legends (VALUE, EDIT SELECT, STORE, PAGE) in light blue.
inline juce::Colour qs6Blue() { return juce::Colour(0xff6fb6e4); }
inline juce::Colour panelBlack() { return juce::Colour(0xff151618); }

// A dark recess cut into the panel.
inline void drawWell(juce::Graphics& g, juce::Rectangle<float> r, float corner = 4.0f) {
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(r, corner);
    g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.9f), r.getX(), r.getY(), juce::Colours::transparentBlack, r.getX(),
                                           r.getY() + juce::jmin(10.0f, r.getHeight() * 0.3f), false));
    g.fillRoundedRectangle(r, corner);
    g.setColour(juce::Colours::white.withAlpha(0.07f));
    g.drawRoundedRectangle(r.expanded(0.5f), corner, 1.0f);
}

// Base for controls you drag up and down.
class DragControl : public juce::Component, public juce::SettableTooltipClient {
public:
    DragControl(int lo, int hi, int initial) : minV(lo), maxV(hi), value(initial), resetV(initial) {}
    std::function<void(int)> onChange;  // only for changes the user makes

    int getValue() const { return value; }
    void setValue(int v, bool notify) {
        v = juce::jlimit(minV, maxV, v);
        if (v == value) return;
        value = v;
        repaint();
        if (notify && onChange) onChange(value);
    }
    void setResetValue(int v) { resetV = v; }
    void setSpringBack(bool s) { springBack = s; }

    void mouseDown(const juce::MouseEvent& e) override {
        dragStart = value;
        dragging = true;
        dragY = float(e.position.y);
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        const float travel = juce::jmax(20.0f, travelPixels());
        const float delta = (dragY - e.position.y) / travel * float(maxV - minV);
        setValue(dragStart + juce::roundToInt(e.mods.isShiftDown() ? delta * 0.2f : delta), true);
    }
    void mouseUp(const juce::MouseEvent&) override {
        dragging = false;
        if (springBack) setValue(resetV, true);
        repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent&) override { setValue(resetV, true); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override {
        if (springBack) return;
        setValue(value + (w.deltaY > 0 ? 1 : -1) * juce::jmax(1, (maxV - minV) / 64), true);
    }

protected:
    virtual float travelPixels() const { return float(getHeight()) * 0.8f; }
    float norm() const { return float(value - minV) / float(juce::jmax(1, maxV - minV)); }
    int minV, maxV, value, resetV, dragStart = 0;
    bool springBack = false, dragging = false;
    float dragY = 0.0f;
};

// A pitch or mod wheel: a smooth grey rubber drum turning in a slot, with the oval thumb dish that rolls with it.
class Wheel : public DragControl {
public:
    Wheel(int lo, int hi, int initial, bool centreDetent) : DragControl(lo, hi, initial), detent(centreDetent) {}

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        drawWell(g, r, 5.0f);
        auto drum = r.reduced(4.0f, 5.0f);
        const float cy = drum.getCentreY(), radius = drum.getHeight() * 0.62f;
        // Drum: lighter where it faces you, falling off to dark where it curves away into the slot.
        juce::ColourGradient body(juce::Colour(0xff1c1d1f), drum.getX(), drum.getY(), juce::Colour(0xff1c1d1f), drum.getX(), drum.getBottom(), false);
        body.addColour(0.15, juce::Colour(0xff55585c));
        body.addColour(0.5, juce::Colour(0xff8e9196));
        body.addColour(0.85, juce::Colour(0xff55585c));
        g.setGradientFill(body);
        g.fillRoundedRectangle(drum, 4.0f);
        // Angle of the drum: about +-50 degrees over the full travel.
        const float theta = (detent ? (norm() - 0.5f) * 2.0f : (norm() * 2.0f - 1.0f)) * juce::degreesToRadians(-50.0f);
        // Thumb dish: an oval hollow that foreshortens as it rolls toward the edges.
        const float facing = std::cos(theta);
        if (facing > 0.12f) {
            const float gy = cy + radius * std::sin(theta);
            const float gh = drum.getHeight() * 0.62f * facing, gw = drum.getWidth() * 0.72f;
            auto dish = juce::Rectangle<float>(drum.getCentreX() - gw / 2, gy - gh / 2, gw, gh).getIntersection(drum.reduced(1.0f));
            // Hollow: shaded dark at the top (in shadow) and light at the bottom (catching light).
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a4d51), dish.getX(), dish.getY(), juce::Colour(0xff9da0a5), dish.getX(),
                                                   dish.getBottom(), false));
            g.fillEllipse(dish);
            g.setColour(juce::Colour(0xff2a2b2e).withAlpha(0.8f));
            g.drawEllipse(dish, 1.0f);
            g.setColour(juce::Colours::white.withAlpha(0.18f * facing));
            g.drawEllipse(dish.reduced(1.5f).translated(0, 0.8f), 0.8f);
        }
        // Soft shadow where the drum meets the slot sides.
        g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.45f), drum.getX(), cy, juce::Colours::transparentBlack, drum.getX() + 6, cy, false));
        g.fillRect(drum.withWidth(6.0f));
        g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.45f), drum.getRight(), cy, juce::Colours::transparentBlack, drum.getRight() - 6, cy, false));
        g.fillRect(drum.withLeft(drum.getRight() - 6.0f));
        if (detent && value == resetV) {
            g.setColour(theme::accent.withAlpha(0.8f));
            g.fillRect(r.getX() + 1.0f, cy - 1.0f, 3.0f, 2.0f);
            g.fillRect(r.getRight() - 4.0f, cy - 1.0f, 3.0f, 2.0f);
        }
    }

protected:
    float travelPixels() const override { return float(getHeight()) * 1.1f; }

private:
    bool detent;
};

// A slide fader as on the QS6: a recessed box with a metal slot and a black cap.
class Fader : public DragControl {
public:
    Fader(int lo, int hi, int initial) : DragControl(lo, hi, initial) {}

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        const float boxW = juce::jmin(r.getWidth() - 4.0f, 30.0f);
        auto box = r.withSizeKeepingCentre(boxW, r.getHeight());
        // Recessed box.
        g.setColour(juce::Colour(0xff2b2c2f));
        g.fillRoundedRectangle(box, 3.0f);
        drawWell(g, box.reduced(2.5f), 2.0f);
        const float capH = juce::jmin(26.0f, box.getHeight() * 0.22f);
        auto track = box.reduced(0.0f, capH / 2 + 3.0f);
        const float cx = box.getCentreX();
        // Metal slot.
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff8c8f94), cx - 1.5f, 0, juce::Colour(0xff3d3f43), cx + 1.5f, 0, false));
        g.fillRect(juce::Rectangle<float>(cx - 1.5f, track.getY() - 2.0f, 3.0f, track.getHeight() + 4.0f));
        // Cap.
        const float y = track.getBottom() - track.getHeight() * norm();
        juce::Rectangle<float> cap(box.getX() + 4.0f, y - capH / 2, box.getWidth() - 8.0f, capH);
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillRoundedRectangle(cap.translated(0, 2.0f), 2.5f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3c3d41), cap.getX(), cap.getY(), juce::Colour(0xff0f0f11), cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle(cap, 2.5f);
        g.setColour(juce::Colours::white.withAlpha(0.16f));
        g.drawHorizontalLine(int(cap.getY() + 1), cap.getX() + 2, cap.getRight() - 2);
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.fillRect(cap.getX() + 3, cap.getCentreY() - 0.5f, cap.getWidth() - 6, 1.0f);
        if (dragging) {
            // Over the cap, inside this component (a bubble beside the fader would be cut off by its neighbours).
            auto bubble = juce::Rectangle<float>(r.getX() + 1.0f, cap.getCentreY() - 10.0f, r.getWidth() - 2.0f, 20.0f)
                              .constrainedWithin(r);
            g.setColour(theme::bezel.withAlpha(0.95f));
            g.fillRoundedRectangle(bubble, 4.0f);
            g.setColour(theme::accent);
            g.setFont(theme::uiFont(12.0f, true));
            g.drawText(juce::String(value), bubble, juce::Justification::centred);
        }
    }

protected:
    float travelPixels() const override { return float(getHeight()) * 0.76f; }
};

// A rotary pot with a knurled skirt and an index line (QSR volume).
class Knob : public DragControl {
public:
    Knob(int lo, int hi, int initial) : DragControl(lo, hi, initial) {}
    void paint(juce::Graphics& g) override { drawKnob(g, getLocalBounds().toFloat(), juce::jmap(norm(), -2.4f, 2.4f), false); }

    static void drawKnob(juce::Graphics& g, juce::Rectangle<float> area, float angle, bool big) {
        auto r = area.withSizeKeepingCentre(juce::jmin(area.getWidth(), area.getHeight()), juce::jmin(area.getWidth(), area.getHeight())).reduced(2.0f);
        const auto c = r.getCentre();
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillEllipse(r.translated(0, 2.5f));
        // Knurled skirt.
        g.setColour(juce::Colour(0xff0e0e10));
        g.fillEllipse(r);
        const int teeth = big ? 36 : 24;
        g.setColour(juce::Colours::white.withAlpha(0.10f));
        for (int i = 0; i < teeth; ++i) {
            const float a = angle + juce::MathConstants<float>::twoPi * i / teeth;
            g.drawLine(juce::Line<float>(c.getPointOnCircumference(r.getWidth() * 0.5f - 1, a), c.getPointOnCircumference(r.getWidth() * 0.40f, a)), 1.2f);
        }
        // Top.
        auto top = r.reduced(r.getWidth() * 0.13f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4c4e53), top.getX(), top.getY(), juce::Colour(0xff17181a), top.getRight(), top.getBottom(), false));
        g.fillEllipse(top);
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawEllipse(top, 1.0f);
        // Index line.
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.drawLine(juce::Line<float>(c.getPointOnCircumference(top.getWidth() * 0.12f, angle), c.getPointOnCircumference(top.getWidth() * 0.46f, angle)),
                   big ? 3.0f : 2.2f);
    }

protected:
    float travelPixels() const override { return 160.0f; }
};

// An endless encoder (QSR VALUE knob): turning it steps up or down.
class Encoder : public juce::Component, public juce::SettableTooltipClient {
public:
    std::function<void(int)> onStep;
    void paint(juce::Graphics& g) override { drawDimpleKnob(g, getLocalBounds().toFloat(), angle); }

    // The QSR's VALUE knob: a large black knob with a finger dimple near the edge.
    static void drawDimpleKnob(juce::Graphics& g, juce::Rectangle<float> area, float angle) {
        const float d = juce::jmin(area.getWidth(), area.getHeight()) - 4.0f;
        auto r = area.withSizeKeepingCentre(d, d);
        const auto c = r.getCentre();
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillEllipse(r.translated(0, 3.0f));
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3a3b3f), r.getX(), r.getY(), juce::Colour(0xff0b0b0c), r.getRight(), r.getBottom(), false));
        g.fillEllipse(r);
        g.setColour(juce::Colours::white.withAlpha(0.10f));
        g.drawEllipse(r.reduced(1.0f), 1.2f);
        auto top = r.reduced(d * 0.08f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2c2d30), top.getX(), top.getY(), juce::Colour(0xff121214), top.getRight(), top.getBottom(), false));
        g.fillEllipse(top);
        // Dimple.
        const auto p = c.getPointOnCircumference(d * 0.30f, angle);
        juce::Rectangle<float> dimple(p.x - d * 0.11f, p.y - d * 0.11f, d * 0.22f, d * 0.22f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff050506), dimple.getX(), dimple.getY(), juce::Colour(0xff3a3b3f), dimple.getRight(),
                                               dimple.getBottom(), false));
        g.fillEllipse(dimple);
    }
    void mouseDown(const juce::MouseEvent& e) override { lastY = e.position.y; }
    void mouseDrag(const juce::MouseEvent& e) override {
        const int n = int((lastY - e.position.y) / 12.0f);  // one step per 12 pixels, however fast the drag
        for (int i = 0; i < std::abs(n); ++i) step(n > 0 ? 1 : -1);
        lastY -= float(n) * 12.0f;
    }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override {
        if (w.deltaY != 0) step(w.deltaY > 0 ? 1 : -1);
    }

private:
    void step(int s) {
        angle += 0.26f * float(s);
        repaint();
        if (onStep) onStep(s);
    }
    float angle = 0.0f, lastY = 0.0f;
};

// A rubber panel button with printed text. Lit buttons glow in the accent colour.
class Key : public juce::Button {
public:
    explicit Key(juce::String top, juce::String bottom = {}) : juce::Button(top), line1(std::move(top)), line2(std::move(bottom)) {}
    void setLabel(juce::String top, juce::String bottom = {}) {
        line1 = std::move(top);
        line2 = std::move(bottom);
        repaint();
    }
    void setLit(bool l) {
        if (l != lit) {
            lit = l;
            repaint();
        }
    }
    bool small = false;      // QSR-style compact button
    bool pill = false;       // QS6.2 / QS8.2 rounded keys
    juce::Colour legend;     // printed lettering; transparent = the panel's silk colour
    bool split = false;      // one wide key with arrows at both ends (QSR CURSOR, MIDI CH): see side()
    int side() const { return lastSide; }  // -1 if the left half was pressed, +1 for the right half

    void mouseDown(const juce::MouseEvent& e) override {
        lastSide = e.position.x < float(getWidth()) / 2.0f ? -1 : 1;
        juce::Button::mouseDown(e);
    }

    void paintButton(juce::Graphics& g, bool over, bool down) override {
        auto r = getLocalBounds().toFloat().reduced(1.5f);
        const float corner = pill ? getHeight() * 0.45f : small ? 3.0f : 4.0f;
        if (lit) {
            g.setColour(theme::accent.withAlpha(0.35f));
            g.fillRoundedRectangle(r.expanded(1.5f), corner + 1.5f);
        }
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillRoundedRectangle(r.translated(0, down ? 0.5f : 2.0f), corner);
        auto face = down ? r.translated(0, 1.0f) : r;
        const auto base = juce::Colour(isEnabled() ? 0xff36383c : 0xff222326);
        g.setGradientFill(juce::ColourGradient(base.brighter(over ? 0.25f : 0.15f), face.getX(), face.getY(), base.darker(0.45f), face.getX(),
                                               face.getBottom(), false));
        g.fillRoundedRectangle(face, corner);
        g.setColour(lit ? theme::accent : juce::Colours::white.withAlpha(0.10f));
        g.drawRoundedRectangle(face, corner, lit ? 1.5f : 1.0f);
        const auto base2 = legend.isTransparent() ? silk() : legend;
        const auto ink = isEnabled() ? (lit ? theme::accent.brighter(0.3f) : base2) : base2.withAlpha(0.3f);
        g.setColour(ink);
        const float h = face.getHeight();
        if (line2.isEmpty()) {
            g.setFont(theme::uiFont(juce::jlimit(7.0f, 13.0f, h * 0.36f), true));
            g.drawFittedText(line1, face.toNearestInt().reduced(2, 0), juce::Justification::centred, 2, 0.6f);
        } else {
            auto t = face.reduced(1.0f, 2.0f);
            g.setFont(theme::uiFont(juce::jlimit(7.0f, 13.0f, h * 0.33f), true));
            g.drawFittedText(line1, t.removeFromTop(t.getHeight() * 0.52f).toNearestInt(), juce::Justification::centredBottom, 1, 0.6f);
            g.setColour(ink.withAlpha(0.75f));
            g.setFont(theme::uiFont(juce::jlimit(6.0f, 9.5f, h * 0.22f), true));
            g.drawFittedText(line2, t.toNearestInt(), juce::Justification::centredTop, 1, 0.5f);
        }
    }

private:
    juce::String line1, line2;
    bool lit = false;
    int lastSide = 1;
};

}  // namespace hw
