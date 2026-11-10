// The window frame: a grouped sidebar like a hardware editor, and a page header with a plain-language summary and help.
#pragma once
#include <JuceHeader.h>

#include "Theme.h"

// A titled block with a short explanation, used to group controls on a page.
class Section : public juce::Component {
public:
    Section(juce::String title, juce::String text) : heading(std::move(title)), body(std::move(text)) {}

    int textHeight(int width) const {
        if (body.isEmpty()) return 0;
        return int(std::ceil(layout(width).getHeight())) + 6;
    }
    // The area below the title and text, in the parent's coordinates, for laying out controls on top of the section.
    juce::Rectangle<int> contentArea() const {
        auto r = getLocalBounds().reduced(16, 12);
        r.removeFromTop(26);
        r.removeFromTop(textHeight(r.getWidth()));
        r.removeFromTop(6);
        return r + getPosition();
    }
    // Height needed for the title, text and controlsHeight of controls at this width.
    int heightFor(int width, int controlsHeight) const { return 24 + 26 + textHeight(width - 32) + 6 + controlsHeight; }

    void paint(juce::Graphics& g) override {
        theme::drawPanel(g, getLocalBounds().toFloat(), false);
        auto r = getLocalBounds().reduced(16, 12);
        g.setColour(theme::text);
        g.setFont(theme::uiFont(17.0f, true));
        g.drawText(heading, r.removeFromTop(26), juce::Justification::centredLeft);
        layout(r.getWidth()).draw(g, r.toFloat());
    }
    juce::String heading, body;

private:
    juce::TextLayout layout(int width) const {
        juce::AttributedString a;
        a.append(body, theme::uiFont(14.0f), theme::dim);
        juce::TextLayout tl;
        tl.createLayout(a, float(juce::jmax(50, width)));
        return tl;
    }
};

class Sidebar : public juce::Component, public juce::SettableTooltipClient {
public:
    struct Item { juce::String group, title, tip; };

    void setItems(std::vector<Item> i) {
        items = std::move(i);
        repaint();
    }
    void setSelected(int i) {
        selected = i;
        repaint();
    }
    int getSelected() const { return selected; }
    std::function<void(int)> onSelect;
    juce::String footer;  // small print at the bottom

    void paint(juce::Graphics& g) override {
        g.fillAll(theme::panel);
        g.setColour(theme::line);
        g.drawVerticalLine(getWidth() - 1, 0.0f, float(getHeight()));
        if (footer.isNotEmpty()) {
            g.setColour(theme::dim.withAlpha(0.8f));
            g.setFont(theme::uiFont(11.0f));
            g.drawFittedText(footer, getLocalBounds().removeFromBottom(70).reduced(18, 8), juce::Justification::bottomLeft, 5, 1.0f);
        }
        juce::String lastGroup;
        for (int i = 0; i < int(items.size()); ++i) {
            if (items[size_t(i)].group != lastGroup) {
                lastGroup = items[size_t(i)].group;
                g.setColour(theme::dim);
                g.setFont(theme::uiFont(11.5f, true));
                g.drawText(lastGroup.toUpperCase(), groupRow(i).reduced(18, 0), juce::Justification::bottomLeft);
            }
            auto r = itemRow(i);
            const bool sel = i == selected, hov = i == hovered;
            if (sel || hov) {
                g.setColour(sel ? theme::accentDim.withAlpha(0.55f) : theme::panelHi);
                g.fillRoundedRectangle(r.reduced(8, 2).toFloat(), 6.0f);
            }
            if (sel) {
                g.setColour(theme::accent);
                g.fillRoundedRectangle(r.reduced(8, 8).withWidth(4).toFloat(), 2.0f);
            }
            g.setColour(sel ? theme::text : theme::text.withAlpha(0.85f));
            g.setFont(theme::uiFont(15.0f, sel));
            g.drawText(items[size_t(i)].title, r.reduced(22, 0), juce::Justification::centredLeft);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override {
        const int h = hit(e.getPosition());
        if (h != hovered) {
            hovered = h;
            setTooltip(h >= 0 ? items[size_t(h)].tip : juce::String());
            repaint();
        }
    }
    void mouseExit(const juce::MouseEvent&) override {
        hovered = -1;
        repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override {
        const int h = hit(e.getPosition());
        if (h >= 0 && onSelect) onSelect(h);
    }

private:
    static constexpr int kRow = 36, kGroup = 34;
    // y of item i: every new group adds a heading above it
    int topOf(int i) const {
        int y = 8;
        juce::String last;
        for (int k = 0; k <= i && k < int(items.size()); ++k) {
            if (items[size_t(k)].group != last) {
                last = items[size_t(k)].group;
                y += kGroup;
            }
            if (k < i) y += kRow;
        }
        return y;
    }
    juce::Rectangle<int> itemRow(int i) const { return {0, topOf(i), getWidth(), kRow}; }
    juce::Rectangle<int> groupRow(int i) const { return {0, topOf(i) - kGroup, getWidth(), kGroup - 6}; }
    int hit(juce::Point<int> p) const {
        for (int i = 0; i < int(items.size()); ++i)
            if (itemRow(i).contains(p)) return i;
        return -1;
    }

    std::vector<Item> items;
    int selected = 0, hovered = -1;
};

class PageHeader : public juce::Component {
public:
    PageHeader() {
        addAndMakeVisible(helpBtn);
        helpBtn.setClickingTogglesState(true);
        helpBtn.setTooltip("Shows or hides step-by-step help for this page");
        helpBtn.onClick = [this] {
            helpBtn.setButtonText(helpBtn.getToggleState() ? "Hide help" : "How this works");
            if (onHeightChanged) onHeightChanged();
        };
    }
    void set(juce::String t, juce::String summary, juce::String helpText) {
        title = std::move(t);
        desc = std::move(summary);
        help = std::move(helpText);
        helpBtn.setVisible(help.isNotEmpty());
        if (onHeightChanged) onHeightChanged();
        repaint();
    }
    std::function<void()> onHeightChanged;

    int heightFor(int width) const {
        int h = 16 + 30 + layout(desc, 15.0f, width - 220).getHeight() + 14;
        if (helpBtn.getToggleState() && help.isNotEmpty()) h += int(layout(help, 14.5f, width - 64).getHeight()) + 30;
        return h;
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(theme::bg);
        auto r = getLocalBounds().reduced(20, 0);
        r.removeFromTop(14);
        g.setColour(theme::text);
        g.setFont(theme::uiFont(24.0f, true));
        g.drawText(title, r.removeFromTop(32), juce::Justification::centredLeft);
        auto d = layout(desc, 15.0f, r.getWidth() - 200);
        d.draw(g, r.removeFromTop(int(d.getHeight()) + 2).withWidth(r.getWidth() - 200).toFloat());
        if (helpBtn.getToggleState() && help.isNotEmpty()) {
            r.removeFromTop(12);
            auto box = r.removeFromTop(int(layout(help, 14.5f, r.getWidth() - 44).getHeight()) + 20);
            g.setColour(theme::panel);
            g.fillRoundedRectangle(box.toFloat(), 8.0f);
            g.setColour(theme::accent);
            g.fillRoundedRectangle(box.withWidth(4).toFloat(), 2.0f);
            layout(help, 14.5f, box.getWidth() - 44, theme::text).draw(g, box.reduced(22, 10).toFloat());
        }
        g.setColour(theme::line);
        g.drawHorizontalLine(getHeight() - 1, 20.0f, float(getWidth() - 20));
    }
    void resized() override { helpBtn.setBounds(getWidth() - 20 - 160, 18, 160, 30); }

private:
    static juce::TextLayout layout(const juce::String& s, float size, int width, juce::Colour c = theme::dim) {
        juce::AttributedString a;
        a.append(s, theme::uiFont(size), c);
        juce::TextLayout tl;
        tl.createLayout(a, float(juce::jmax(100, width)));
        return tl;
    }
    juce::String title, desc, help;
    juce::TextButton helpBtn{"How this works"};
};

// Connection state in the top bar. Click it to open the Connection page.
class StatusPill : public juce::Component, public juce::SettableTooltipClient {
public:
    void set(bool ok, juce::String t) {
        connected = ok;
        text = std::move(t);
        repaint();
    }
    std::function<void()> onClick;
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(theme::bg);
        g.fillRoundedRectangle(r, r.getHeight() / 2);
        g.setColour(isMouseOver() ? theme::accent : theme::line);
        g.drawRoundedRectangle(r, r.getHeight() / 2, 1.0f);
        g.setColour(connected ? theme::accent : theme::warn);
        g.fillEllipse(r.getX() + 12, r.getCentreY() - 5, 10, 10);
        g.setColour(theme::text);
        g.setFont(theme::uiFont(14.0f, true));
        g.drawText(text, r.withTrimmedLeft(30).withTrimmedRight(12), juce::Justification::centredLeft);
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseUp(const juce::MouseEvent&) override {
        if (onClick) onClick();
    }

private:
    bool connected = false;
    juce::String text = "Not connected";
};
