#include <JuceHeader.h>

#include "MainComponent.h"
#include "Theme.h"

class QSStudioApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "QS Studio"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override {
        juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
        window = std::make_unique<MainWindow>(getApplicationName());
    }
    void shutdown() override {
        window.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }
    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow : public juce::DocumentWindow {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name + " " + versionLabel() + "  -  for Alesis QS synths (unofficial)",
                             theme::bg,
                             DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    theme::LookAndFeel lnf;  // declared before the window so it outlives it
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION(QSStudioApplication)
