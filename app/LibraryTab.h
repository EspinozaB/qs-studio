// Library: browse every bank and program, select them on the QS, read names from the QS, open in the editor.
#pragma once
#include <JuceHeader.h>

#include "FrontPanel.h"
#include "Session.h"
#include "Theme.h"

class LibraryTab : public juce::Component, private juce::ListBoxModel, private juce::Timer {
public:
    explicit LibraryTab(Session& s);
    ~LibraryTab() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& k) override;

    void showBank(int bank);
    void refresh();
    void applySavedImports(bool onlyEmptyBanks);  // re-import the remembered .syx folder
    void copyBankToCard();   // pick a .syx and copy it onto the card bank being shown
    void convertBankFile();  // QS6 <-> QSR sample renumbering
    void selectProgram(int prog);  // select it in the list, which sends it to the QS
    void setMixView(bool mixes, bool switchQs);  // list mixes instead of programs (MIX / PROGRAM)

private:
    // ListBoxModel
    int getNumRows() override { return int(rows.size()); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
    void selectedRowsChanged(int row) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    void timerCallback() override;

    void rebuildRows();
    void updateLcd();
    int selectedProgram() const;
    void readBanks(std::vector<int> banks);
    void readMixes(std::vector<int> banks);
    void selectMixOnQs(int mix);
    int count() const;
    juce::String nameAt(int n) const;
    int currentBankShown() const;
    int currentNumber() const;
    void importOneSyx();
    void importFolder();
    void importFolderWithMap(const juce::File& dir, const juce::StringPairArray& map);
    void openInEditor(int prog);
    void sendCachedToEditBuffer();
    void storeToUser();
    void startCardCopy(std::vector<qs::Bytes> msgs, std::array<std::optional<qs::Program>, 128> expected, juce::String label, int cardBank);
    void finishCardCopy(std::array<std::optional<qs::Program>, 128> expected, juce::String label, int cardBank, juce::File backup);
    void step(int delta);

    Session& session;
    int bank = 0;
    std::vector<int> rows;  // program numbers shown (after search)
    int pendingSelect = -1;
    bool suppressSend = false;
    bool mixView = false, askedProgramSelect = false;

    std::unique_ptr<FrontPanel> panel;
    juce::OwnedArray<juce::TextButton> bankButtons;  // User, Preset 1-3, GenMIDI, Card
    juce::ComboBox cardBankBox;
    juce::TextEditor search;
    juce::ListBox list{"programs", this};
    juce::TextButton prevBtn{"<"}, nextBtn{">"}, readBtn{"Read this bank from QS"}, readAllBtn{"Read all banks from QS"},
        importFolderBtn{"Import a folder of .syx banks..."}, importOneBtn{"Import a .syx into this bank..."}, editBtn{"Open in editor"},
        sendBtn{"Audition (send to QS, don't store)"}, storeBtn{"Store to User..."}, copyToCardBtn{"Copy a bank onto this card bank..."},
        convertBtn{"Convert a bank file QS6 <-> QSR..."}, progViewBtn{"Programs"}, mixViewBtn{"Mixes"};
    juce::HyperlinkButton factoryLink{"Get the Alesis factory banks (.syx)",
                                      juce::URL("https://github.com/jzemeocala/Alesis-Archive-Project/tree/HEAD/BANKS/FACTORY")};
    juce::Label sourceLabel;
    std::unique_ptr<juce::FileChooser> chooser;
};
