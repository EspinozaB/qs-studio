// Search every saved library (each model's), every backup and the built-in factory names for a program or mix name.
// Double-click a result to open it in the Program or Mix editor. (MIDI Quest's database search, for QS files.)
#pragma once
#include <JuceHeader.h>

#include "Session.h"
#include "Theme.h"

class SearchAll : public juce::Component, private juce::ListBoxModel {
public:
    explicit SearchAll(Session& s) : session(s) {
        addAndMakeVisible(query);
        addAndMakeVisible(list);
        addAndMakeVisible(status);
        query.setTextToShowWhenEmpty("Type part of a name (\"piano\", \"pad\", \"stik\")...", theme::dim);
        query.onTextChange = [this] { filter(); };
        list.setModel(this);
        list.setRowHeight(24);
        list.setColour(juce::ListBox::backgroundColourId, theme::panel);
        status.setColour(juce::Label::textColourId, theme::dim);
        scan();
        filter();
        setSize(820, 560);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(12);
        query.setBounds(r.removeFromTop(30));
        r.removeFromTop(6);
        status.setBounds(r.removeFromBottom(24));
        list.setBounds(r);
    }
    void paint(juce::Graphics& g) override { g.fillAll(theme::bg); }

private:
    struct Hit {
        juce::String where, bank, name;
        int number = 0;
        bool mix = false;
        qs::Bytes data;  // program (350) or mix dump; empty for factory names without data
    };

    void scan() {
        hits.clear();
        auto hexBytes = [](const juce::var& x) {
            juce::MemoryBlock mb;
            mb.loadFromHexString(x.toString());
            auto ptr = static_cast<const uint8_t*>(mb.getData());
            return qs::Bytes(ptr, ptr + mb.getSize());
        };
        // Every model's library file (read only: never loaded into the session).
        for (auto& f : settingsDir().findChildFiles(juce::File::findFiles, false, "QSStudio.*.library")) {
            auto v = juce::JSON::parse(f);
            if (!v.isObject()) continue;
            const auto model = f.getFileName().fromFirstOccurrenceOf("QSStudio.", false, false).upToLastOccurrenceOf(".library", false, false);
            for (size_t b = 0; b < kBanks.size(); ++b) {
                if (auto* arr = v[kBanks[b].title].getArray())
                    for (int i = 0; i < juce::jmin(128, arr->size()); ++i)
                        if ((*arr)[i].toString().isNotEmpty())
                            if (auto p = qs::Program::fromData(hexBytes((*arr)[i])))
                                hits.push_back({model + " library", kBanks[b].title, juce::String(p->name()).trimEnd(), i, false, p->bytes()});
                if (auto* arr = v["_mixes"][kBanks[b].title].getArray())
                    for (int i = 0; i < juce::jmin(100, arr->size()); ++i)
                        if ((*arr)[i].toString().isNotEmpty()) {
                            const auto d = hexBytes((*arr)[i]);
                            if (auto mi = qs::parseMixDump(d)) hits.push_back({model + " library", kBanks[b].title, juce::String(mi->name).trimEnd(), i, true, d});
                        }
            }
        }
        // Backups and any .syx kept in the data folder.
        for (auto& f : settingsDir().findChildFiles(juce::File::findFiles, true, "*.syx;*.SYX")) {
            juce::MemoryBlock mb;
            if (!f.loadFileAsData(mb)) continue;
            auto ptr = static_cast<const uint8_t*>(mb.getData());
            const auto where = f.getParentDirectory().getFileName() == "Backups" ? "Backup " + f.getFileNameWithoutExtension() : f.getFileName();
            for (auto& m : qs::splitSysex(qs::Bytes(ptr, ptr + mb.getSize()))) {
                const int op = qs::opcodeOf(m);
                if (op == qs::UserProgramDump || op == qs::EditProgramDump) {
                    if (auto p = qs::Program::fromDump(m))
                        hits.push_back({where, op == qs::UserProgramDump ? "User" : "Edit", juce::String(p->name()).trimEnd(), m[6], false, p->bytes()});
                } else if (op == qs::NewMixDump) {
                    if (auto mi = qs::parseMixDump(m)) hits.push_back({where, "User", juce::String(mi->name).trimEnd(), mi->number, true, m});
                }
            }
        }
        // Factory names (data comes from the synth or a factory file).
        for (int b = 1; b <= 4; ++b) {
            for (int i = 0; i < 128; ++i)
                hits.push_back({"Factory names", kBanks[size_t(b)].title, juce::String(qs::factoryProgramName(b, i)).trimEnd(), i, false, {}});
            for (int i = 0; i < 100; ++i) hits.push_back({"Factory names", kBanks[size_t(b)].title, juce::String(qs::factoryMixName(b, i)).trimEnd(), i, true, {}});
        }
    }

    void filter() {
        shown.clear();
        const auto q = query.getText().trim();
        for (size_t i = 0; i < hits.size(); ++i)
            if (q.isNotEmpty() && hits[i].name.containsIgnoreCase(q)) shown.push_back(i);
        list.updateContent();
        list.repaint();
        status.setText(q.isEmpty() ? juce::String(int(hits.size())) + " programs and mixes indexed. Double-click a result to open it."
                                   : juce::String(int(shown.size())) + " found.",
                       juce::dontSendNotification);
    }

    int getNumRows() override { return int(shown.size()); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override {
        if (row >= int(shown.size())) return;
        const auto& hit = hits[shown[size_t(row)]];
        if (selected) g.fillAll(theme::accent.withAlpha(0.25f));
        g.setFont(theme::uiFont(14.0f));
        g.setColour(theme::text);
        g.drawText(hit.name, 8, 0, 180, h, juce::Justification::centredLeft);
        g.setColour(theme::dim);
        g.setFont(theme::uiFont(13.0f));
        g.drawText(juce::String(hit.mix ? "Mix " : "Program ") + hit.bank + " " + juce::String(hit.number).paddedLeft('0', hit.mix ? 2 : 3), 196, 0,
                   200, h, juce::Justification::centredLeft);
        g.drawText(hit.where, 400, 0, w - 408, h, juce::Justification::centredLeft);
    }
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override {
        if (row >= int(shown.size())) return;
        const auto& hit = hits[shown[size_t(row)]];
        const auto where = hit.where + ", " + hit.bank + " " + juce::String(hit.number);
        if (hit.data.empty()) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Name only",
                                                   hit.name + " is a factory name. Pick it on the Programs page to play it, or read that bank "
                                                              "from the QS to edit it.");
            return;
        }
        if (hit.mix) {
            if (auto m = qs::Mix::fromDump(hit.data); m && session.openMixEditor) session.openMixEditor(*m, where, -1);
        } else if (auto p = qs::Program::fromData(hit.data); p && session.openInEditor) {
            session.openInEditor(*p, where);
        }
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(0);
    }

    Session& session;
    juce::TextEditor query;
    juce::ListBox list;
    juce::Label status;
    std::vector<Hit> hits;
    std::vector<size_t> shown;
};
