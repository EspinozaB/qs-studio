// Drawn hardware controls: wheels, faders, knobs, encoders and panel keys that look and move like the real ones.
// Everything is vector-drawn so it scales; no images.
#pragma once
#include <JuceHeader.h>

#include "Theme.h"

namespace hw {

// Printed panel lettering. The QS6 prints its labels in light blue; the QSR in light grey.
inline juce::Colour silk() { return theme::currentStyle == theme::Style::QS6 ? juce::Colour(0xff8db6dc) : juce::Colour(0xffc8cdd2); }
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

// A pitch or mod wheel: a ribbed rubber drum turning in a slot, with the molded thumb grip that rolls with it.
class Wheel : public DragControl {
public:
    Wheel(int lo, int hi, int initial, bool centreDetent) : DragControl(lo, hi, initial), detent(centreDetent) {}

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        drawWell(g, r, 6.0f);
        auto drum = r.reduced(5.0f, 6.0f);
        const float cy = drum.getCentreY(), radius = drum.getHeight() * 0.62f;
        // Drum body: dark at the top and bottom where it curves away, lighter where it faces you.
        juce::ColourGradient body(juce::Colour(0xff0d0d0e), drum.getX(), drum.getY(), juce::Colour(0xff0d0d0e), drum.getX(), drum.getBottom(), false);
        body.addColour(0.5, juce::Colour(0xff4a4c50));
        body.addColour(0.3, juce::Colour(0xff2b2c2f));
        body.addColour(0.7, juce::Colour(0xff2b2c2f));
        g.setGradientFill(body);
        g.fillRoundedRectangle(drum, 3.0f);
        // Angle of the drum: about +-50 degrees over the full travel.
        const float theta = (detent ? (norm() - 0.5f) * 2.0f : (norm() * 2.0f - 1.0f)) * juce::degreesToRadians(-50.0f);
        // Ribs every 7 degrees around the drum.
        for (int k = -40; k <= 40; ++k) {
            const float phi = juce::degreesToRadians(7.0f * k) + theta;
            if (std::abs(phi) >= juce::MathConstants<float>::halfPi * 0.97f) continue;
            const float y = cy + radius * std::sin(phi);
            if (y < drum.getY() + 1 || y > drum.getBottom() - 1) continue;
            const float facing = std::cos(phi);
            g.setColour(juce::Colours::black.withAlpha(0.55f * facing));
            g.drawHorizontalLine(int(y), drum.getX() + 2, drum.getRight() - 2);
            g.setColour(juce::Colours::white.withAlpha(0.06f * facing));
            g.drawHorizontalLine(int(y) + 1, drum.getX() + 2, drum.getRight() - 2);
        }
        // Thumb grip: an oval dish that foreshortens as it rolls toward the edges.
        const float facing = std::cos(theta);
        const float gy = cy + radius * std::sin(theta);
        const float gh = drum.getWidth() * 1.05f * facing, gw = drum.getWidth() * 0.68f;
        if (facing > 0.15f) {
            juce::Rectangle<float> dish(drum.getCentreX() - gw / 2, gy - gh / 2, gw, gh);
            dish = dish.getIntersection(drum.reduced(1.0f));
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1a1b1d), dish.getX(), dish.getY(), juce::Colour(0xff5a5c61), dish.getX(), dish.getBottom(), false));
            g.fillEllipse(dish);
            g.setColour(juce::Colours::black.withAlpha(0.5f));
            g.drawEllipse(dish, 1.0f);
        }
        // Light falling off toward the slot edges.
        g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.6f), drum.getX(), cy, juce::Colours::transparentBlack, drum.getX() + 7, cy, false));
        g.fillRect(drum.withWidth(7.0f));
        g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.6f), drum.getRight(), cy, juce::Colours::transparentBlack, drum.getRight() - 7, cy, false));
        g.fillRect(drum.withLeft(drum.getRight() - 7.0f));
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

// A slide fader: a groove with a scale, and a black cap with grip ridges and a white index line.
class Fader : public DragControl {
public:
    Fader(int lo, int hi, int initial) : DragControl(lo, hi, initial) {}

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        const float capH = juce::jmin(28.0f, r.getHeight() * 0.24f);
        auto track = r.reduced(0.0f, capH / 2);
        const float cx = r.getCentreX();
        // Scale ticks either side of the groove.
        g.setColour(hw::silk().withAlpha(0.45f));
        for (int i = 0; i <= 10; ++i) {
            const float y = track.getBottom() - track.getHeight() * i / 10.0f;
            const float w = (i % 5 == 0) ? 6.0f : 3.5f;
            g.drawHorizontalLine(int(y), cx - 9.0f - w, cx - 9.0f);
            g.drawHorizontalLine(int(y), cx + 9.0f, cx + 9.0f + w);
        }
        drawWell(g, juce::Rectangle<float>(cx - 3.0f, track.getY() - 4.0f, 6.0f, track.getHeight() + 8.0f), 3.0f);
        // Cap.
        const float y = track.getBottom() - track.getHeight() * norm();
        const float capW = juce::jmin(r.getWidth() - 4.0f, 30.0f);
        juce::Rectangle<float> cap(cx - capW / 2, y - capH / 2, capW, capH);
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRoundedRectangle(cap.translated(0, 2.5f), 3.0f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a4b4f), cap.getX(), cap.getY(), juce::Colour(0xff121214), cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle(cap, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        for (int i = 1; i <= 3; ++i) {
            g.drawHorizontalLine(int(cap.getY() + cap.getHeight() * i / 8.0f), cap.getX() + 3, cap.getRight() - 3);
            g.drawHorizontalLine(int(cap.getBottom() - cap.getHeight() * i / 8.0f), cap.getX() + 3, cap.getRight() - 3);
        }
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.fillRect(cap.getX() + 2, cap.getCentreY() - 1, cap.getWidth() - 4, 2.0f);
        if (dragging) {
            juce::Rectangle<float> bubble(cap.getRight() + 3, cap.getCentreY() - 10, 30, 20);
            if (bubble.getRight() > r.getRight()) bubble.setX(cap.getX() - 33);
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
    void paint(juce::Graphics& g) override { Knob::drawKnob(g, getLocalBounds().toFloat(), angle, true); }
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
    bool small = false;  // QSR-style compact button

    void paintButton(juce::Graphics& g, bool over, bool down) override {
        auto r = getLocalBounds().toFloat().reduced(1.5f);
        const float corner = small ? 3.0f : 4.0f;
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
        const auto ink = isEnabled() ? (lit ? theme::accent.brighter(0.3f) : silk()) : silk().withAlpha(0.3f);
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
};

}  // namespace hw
