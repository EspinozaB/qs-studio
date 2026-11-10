// QS Studio look: dark charcoal panels, mint-green accents, a backlit green LCD.
#pragma once
#include <JuceHeader.h>

namespace theme {

// Colours are switched per synth model (see applyStyle). Components read them when they paint.
inline juce::Colour bg{0xff1c1d1f}, panel{0xff26272a}, panelHi{0xff2f3034}, line{0xff3b3d42}, text{0xfff0f1f2},
    dim{0xffa3a6ab}, accent{0xff74dc9c}, accentDim{0xff3f7a57}, warn{0xffe2a94b}, bad{0xffe0675a},
    lcdBg{0xff5e9a6c}, lcdInk{0xff17301f}, bezel{0xff0d0e0f};

enum class Style { QS6, QSR };
inline Style currentStyle = Style::QSR;

// QS6: black keyboard panel with a yellow-green backlit LCD. QS7/QS8/QSR: black rack panel with a grey-blue LCD.
inline void applyStyle(Style s) {
    currentStyle = s;
    if (s == Style::QS6) {
        accent = juce::Colour(0xffa6d65c);
        accentDim = juce::Colour(0xff55702f);
        lcdBg = juce::Colour(0xff9fb85a);
        lcdInk = juce::Colour(0xff1b2410);
    } else {
        accent = juce::Colour(0xff71b7e0);
        accentDim = juce::Colour(0xff335c76);
        lcdBg = juce::Colour(0xff8ea3ad);
        lcdInk = juce::Colour(0xff111c24);
    }
}

inline juce::Font uiFont(float h, bool bold = false) {
    return juce::Font(juce::FontOptions(h, bold ? juce::Font::bold : juce::Font::plain));
}
inline juce::Font monoFont(float h, bool bold = false) {
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain));
}

class LookAndFeel : public juce::LookAndFeel_V4 {
public:
    LookAndFeel() { refresh(); }

    // Re-read the theme colours (after applyStyle).
    void refresh() {
        setColourScheme({bg, panel, panel, line, text, panelHi, text, accent, text});
        setColour(juce::ResizableWindow::backgroundColourId, bg);
        setColour(juce::TextButton::buttonColourId, panelHi);
        setColour(juce::TextButton::buttonOnColourId, accent);
        setColour(juce::TextButton::textColourOffId, text);
        setColour(juce::TextButton::textColourOnId, bg);
        setColour(juce::ComboBox::backgroundColourId, bg.darker(0.25f));
        setColour(juce::ComboBox::outlineColourId, line);
        setColour(juce::ComboBox::arrowColourId, dim);
        setColour(juce::PopupMenu::backgroundColourId, panel);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, accentDim);
        setColour(juce::TextEditor::backgroundColourId, bg.darker(0.3f));
        setColour(juce::TextEditor::outlineColourId, line);
        setColour(juce::TextEditor::focusedOutlineColourId, accent);
        setColour(juce::TextEditor::highlightColourId, accentDim);
        setColour(juce::Label::textColourId, text);
        setColour(juce::ListBox::backgroundColourId, panel);
        setColour(juce::Slider::thumbColourId, accent);
        setColour(juce::Slider::trackColourId, accent);
        setColour(juce::Slider::backgroundColourId, bg.darker(0.3f));
        setColour(juce::Slider::rotarySliderFillColourId, accent);
        setColour(juce::Slider::rotarySliderOutlineColourId, line);
        setColour(juce::Slider::textBoxTextColourId, text);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::ToggleButton::tickColourId, accent);
        setColour(juce::ToggleButton::textColourId, text);
        setColour(juce::TabbedButtonBar::tabOutlineColourId, line);
        setColour(juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
        setColour(juce::ScrollBar::thumbColourId, line);
        setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, accent.withAlpha(0.75f));
        setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, accent.withAlpha(0.3f));
        setColour(juce::TooltipWindow::backgroundColourId, panelHi);
        setColour(juce::TooltipWindow::textColourId, text);
        setColour(juce::TooltipWindow::outlineColourId, accentDim);
        setColour(juce::AlertWindow::backgroundColourId, panel);
        setColour(juce::AlertWindow::textColourId, text);
        setColour(juce::AlertWindow::outlineColourId, line);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return uiFont(juce::jmin(15.0f, h * 0.55f), true); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return uiFont(14.0f); }
    juce::Font getPopupMenuFont() override { return uiFont(14.0f); }
    juce::Font getLabelFont(juce::Label& l) override { return l.getFont(); }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool over, bool down) override {
        auto r = b.getLocalBounds().toFloat().reduced(0.5f);
        auto c = b.getToggleState() ? accent : base;
        if (!b.isEnabled()) c = c.withMultipliedAlpha(0.4f);
        else if (down) c = c.brighter(0.15f);
        else if (over) c = c.brighter(0.07f);
        g.setColour(c);
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(b.getToggleState() ? accent.brighter(0.2f) : line);
        g.drawRoundedRectangle(r, 6.0f, 1.0f);
    }

    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override {
        auto r = juce::Rectangle<float>(0, 0, float(w), float(h)).reduced(0.5f);
        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(box.hasKeyboardFocus(false) ? accent : line);
        g.drawRoundedRectangle(r, 5.0f, 1.0f);
        juce::Path p;
        const float cx = float(w) - 14.0f, cy = float(h) * 0.5f;
        p.startNewSubPath(cx - 4, cy - 2);
        p.lineTo(cx, cy + 2);
        p.lineTo(cx + 4, cy - 2);
        g.setColour(dim);
        g.strokePath(p, juce::PathStrokeType(1.6f));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override {
        auto r = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(4.0f);
        const float rad = juce::jmin(r.getWidth(), r.getHeight()) * 0.5f, cx = r.getCentreX(), cy = r.getCentreY();
        const float ang = a0 + pos * (a1 - a0);
        // tick ring
        g.setColour(line);
        for (int i = 0; i <= 10; ++i) {
            const float a = a0 + (a1 - a0) * float(i) / 10.0f;
            const juce::Point<float> p1(cx + std::sin(a) * (rad - 1), cy - std::cos(a) * (rad - 1)),
                p2(cx + std::sin(a) * (rad - 5), cy - std::cos(a) * (rad - 5));
            g.drawLine({p1, p2}, 1.4f);
        }
        // knob body, lit when not at the bottom of its range
        const float kr = rad - 8;
        const bool lit = s.isEnabled() && pos > 0.001f;
        g.setColour(lit ? accent : bezel);
        g.fillEllipse(cx - kr, cy - kr, kr * 2, kr * 2);
        g.setColour(lit ? accent.darker(0.4f) : line);
        g.drawEllipse(cx - kr, cy - kr, kr * 2, kr * 2, 1.0f);
        // pointer
        g.setColour(lit ? bezel : accent);
        const juce::Point<float> tip(cx + std::sin(ang) * (kr - 2), cy - std::cos(ang) * (kr - 2)),
            base(cx + std::sin(ang) * (kr * 0.55f), cy - std::cos(ang) * (kr * 0.55f));
        g.drawLine({base, tip}, 2.4f);
        // value in the middle
        g.setColour(lit ? bezel : text);
        g.setFont(uiFont(juce::jmax(10.0f, kr * 0.62f), true));
        g.drawText(s.getTextFromValue(s.getValue()), juce::Rectangle<float>(cx - kr, cy - kr, kr * 2, kr * 2),
                   juce::Justification::centred, false);
    }

    void drawTabButton(juce::TabBarButton& b, juce::Graphics& g, bool over, bool) override {
        auto r = b.getActiveArea().toFloat().reduced(3.0f, 4.0f);
        const bool front = b.isFrontTab();
        g.setColour(front ? accent : (over ? panelHi : panel));
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(front ? bg : text);
        g.setFont(uiFont(14.0f, true));
        g.drawText(b.getButtonText(), r, juce::Justification::centred, false);
    }
    int getTabButtonBestWidth(juce::TabBarButton& b, int h) override {
        return juce::GlyphArrangement::getStringWidthInt(uiFont(14.0f, true), b.getButtonText()) + 36 + h / 4;
    }
    void drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics& g) override { g.fillAll(bg); }
    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int, int) override {}
};

// Green backlit LCD, like the QS front panel: big line + small detail lines.
class Lcd : public juce::Component {
public:
    void setLines(const juce::String& big, const juce::StringArray& small = {}) {
        if (big == bigLine && small == smallLines) return;
        bigLine = big;
        smallLines = small;
        repaint();
    }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(bezel);
        g.fillRoundedRectangle(r, 10.0f);
        auto glass = r.reduced(12.0f, 10.0f);
        g.setGradientFill(juce::ColourGradient(lcdBg.brighter(0.08f), glass.getTopLeft(), lcdBg.darker(0.12f), glass.getBottomLeft(), false));
        g.fillRect(glass);
        auto t = glass.reduced(12.0f, 6.0f);
        g.setColour(lcdInk);
        const float bigH = smallLines.isEmpty() ? t.getHeight() * 0.7f : t.getHeight() * 0.52f;
        g.setFont(monoFont(bigH * 0.8f, true));
        g.drawText(bigLine, t.removeFromTop(bigH), juce::Justification::centredLeft, true);
        if (!smallLines.isEmpty()) {
            const float lh = t.getHeight() / float(smallLines.size());
            g.setFont(monoFont(juce::jmin(15.0f, lh * 0.85f)));
            for (auto& s : smallLines) g.drawText(s, t.removeFromTop(lh), juce::Justification::centredLeft, true);
        }
    }

private:
    juce::String bigLine;
    juce::StringArray smallLines;
};

// Rounded panel with an optional accent outline and caption.
inline void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, bool highlight, const juce::String& caption = {}) {
    g.setColour(panel);
    g.fillRoundedRectangle(r, 10.0f);
    g.setColour(highlight ? accent : line);
    g.drawRoundedRectangle(r.reduced(1.0f), 10.0f, highlight ? 2.0f : 1.0f);
    if (caption.isNotEmpty()) {
        g.setColour(text);
        g.setFont(uiFont(16.0f, true));
        g.drawText(caption, r.reduced(14.0f, 10.0f).removeFromTop(22.0f), juce::Justification::centredLeft, false);
    }
}

// Small caps caption above a control.
inline void drawCaption(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& s, juce::Justification j = juce::Justification::centred) {
    g.setColour(dim);
    g.setFont(uiFont(11.0f, true));
    g.drawFittedText(s.toUpperCase(), r, j, 2, 0.8f);
}

}  // namespace theme
