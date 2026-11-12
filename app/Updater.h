// Check for updates, and update only when the user chooses to.
//
// The CI release ("latest" on GitHub) carries version.json: the version, the changelog and a SHA-256 for each
// platform's download. QS Studio compares the version with its own, shows what changed since, and if the user clicks
// Update now: downloads the file, checks its SHA-256, writes a small script that waits for QS Studio to close, puts the
// new program in place of the old one (the "QS Studio Data" folder is never touched) and starts it again.
#pragma once
#include <JuceHeader.h>

#include "Log.h"
#include "Theme.h"

#if JUCE_WINDOWS
#include <process.h>
#else
#include <unistd.h>
#endif

namespace updates {

inline const juce::String kBase = "https://github.com/EspinozaB/qs-studio/releases/download/latest/";
// QS_STUDIO_UPDATE_URL points the check at another server (for testing the updater).
inline juce::String base() {
    auto u = juce::SystemStats::getEnvironmentVariable("QS_STUDIO_UPDATE_URL", {});
    return u.isNotEmpty() ? u : kBase;
}
inline const juce::String kReleasePage = "https://github.com/EspinozaB/qs-studio/releases/tag/latest";

struct Info {
    juce::String version, stage, sha, changelog, file, sha256;
};

inline juce::String platformKey() {
#if JUCE_WINDOWS
    return "windows";
#elif JUCE_MAC
    return "macos";
#else
    return "linux";
#endif
}

// "0.10.2" > "0.9.9"
inline int compareVersions(const juce::String& a, const juce::String& b) {
    auto pa = juce::StringArray::fromTokens(a.trim(), ".", ""), pb = juce::StringArray::fromTokens(b.trim(), ".", "");
    for (int i = 0; i < juce::jmax(pa.size(), pb.size()); ++i) {
        const int x = i < pa.size() ? pa[i].getIntValue() : 0, y = i < pb.size() ? pb[i].getIntValue() : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

// Downloads over HTTPS. JUCE's own Linux networking has no HTTPS without libcurl, so Linux uses the curl command,
// which every desktop distro has.
inline bool download(const juce::String& url, const juce::File& dest, juce::String& error, juce::Thread* t = nullptr) {
    dest.deleteFile();
#if JUCE_LINUX
    juce::ChildProcess p;
    if (!p.start(juce::StringArray{"curl", "-fsSL", "--max-time", "600", "-o", dest.getFullPathName(), url})) {
        error = "curl isn't installed";
        return false;
    }
    while (p.isRunning()) {
        if (t && t->threadShouldExit()) { p.kill(); error = "cancelled"; return false; }
        juce::Thread::sleep(100);
    }
    if (p.getExitCode() != 0 || !dest.existsAsFile()) { error = "download failed (curl exit " + juce::String(p.getExitCode()) + ")"; return false; }
    return true;
#else
    int status = 0;
    auto in = juce::URL(url).createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                                   .withConnectionTimeoutMs(15000)
                                                   .withNumRedirectsToFollow(10)
                                                   .withStatusCode(&status));
    if (in == nullptr || status >= 400) { error = "couldn't reach GitHub" + (status ? " (HTTP " + juce::String(status) + ")" : juce::String()); return false; }
    juce::FileOutputStream out(dest);
    if (!out.openedOk()) { error = "can't write " + dest.getFullPathName(); return false; }
    juce::HeapBlock<char> buf(65536);
    for (;;) {
        if (t && t->threadShouldExit()) { error = "cancelled"; return false; }
        const auto n = in->read(buf, 65536);
        if (n <= 0) break;
        out.write(buf, size_t(n));
    }
    out.flush();
    return true;
#endif
}

inline bool fetchInfo(Info& info, juce::String& error) {
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("qsstudio-version", ".json");
    if (!download(base() + "version.json", tmp, error)) return false;
    const auto json = juce::JSON::parse(tmp.loadFileAsString());
    tmp.deleteFile();
    if (!json.isObject()) { error = "the release has no version information yet"; return false; }
    info.version = json["version"].toString();
    info.stage = json["stage"].toString();
    info.sha = json["sha"].toString();
    info.changelog = json["changelog"].toString();
    const auto asset = json["assets"][juce::Identifier(platformKey())];
    info.file = asset["file"].toString();
    info.sha256 = asset["sha256"].toString();
    if (info.version.isEmpty()) { error = "the release has no version number"; return false; }
    return true;
}

// The changelog sections ("## 0.8.0") newer than the running version.
inline juce::String changesSince(const juce::String& changelog, const juce::String& current) {
    juce::String out;
    bool keep = false;
    for (auto& line : juce::StringArray::fromLines(changelog)) {
        if (line.startsWith("## ")) keep = compareVersions(line.substring(3).upToFirstOccurrenceOf(" ", false, false), current) > 0;
        if (keep) out << (line.startsWith("## ") ? "Version " + line.substring(3) : line) << "\n";
    }
    return out.trim();
}

inline juce::String shellQuote(const juce::String& s) { return "'" + s.replace("'", "'\\''") + "'"; }

// Writes and starts the script that swaps in the new program once this one has quit. Returns false with error text.
inline bool startInstaller(const juce::File& downloaded, juce::String& error) {
#if JUCE_WINDOWS
    const auto pid = juce::String(int(_getpid()));
#else
    const auto pid = juce::String(int(getpid()));
#endif
    auto dir = downloaded.getParentDirectory();
#if JUCE_WINDOWS
    const auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    if (!exe.getParentDirectory().hasWriteAccess()) { error = "QS Studio's folder is read-only (" + exe.getParentDirectory().getFullPathName() + ")"; return false; }
    auto script = dir.getChildFile("install-update.cmd");
    juce::String s;
    s << "@echo off\r\nchcp 65001 >nul\r\n"
      << ":wait\r\ntasklist /FI \"PID eq " << pid << "\" 2>nul | find \" " << pid << " \" >nul\r\n"
      << "if not errorlevel 1 (timeout /t 1 /nobreak >nul & goto wait)\r\n"
      << "set n=0\r\n:copy\r\n"
      << "copy /Y \"" << downloaded.getFullPathName() << "\" \"" << exe.getFullPathName() << "\" >nul && goto run\r\n"
      << "set /a n+=1\r\nif %n% GEQ 15 goto run\r\ntimeout /t 1 /nobreak >nul\r\ngoto copy\r\n"
      << ":run\r\nstart \"\" \"" << exe.getFullPathName() << "\"\r\n";
    if (!script.replaceWithText(s, false, false, "\r\n")) { error = "can't write the update script"; return false; }
    if (!script.startAsProcess()) { error = "can't start the update script"; return false; }
    return true;
#else
#if JUCE_MAC
    const auto app = juce::File::getSpecialLocation(juce::File::currentApplicationFile);  // the .app
    if (!app.getParentDirectory().hasWriteAccess()) { error = "QS Studio's folder is read-only (" + app.getParentDirectory().getFullPathName() + ")"; return false; }
    juce::String s;
    s << "#!/bin/sh\nexec >/dev/null 2>&1\n"
      << "while kill -0 " << pid << " 2>/dev/null; do sleep 1; done\n"
      << "T=$(mktemp -d)\n"
      << "ditto -x -k " << shellQuote(downloaded.getFullPathName()) << " \"$T\" || exit 1\n"
      << "NEW=$(ls -d \"$T\"/*.app | head -1)\n"
      << "[ -d \"$NEW\" ] || exit 1\n"
      << "rm -rf " << shellQuote(app.getFullPathName()) << "\n"
      << "ditto \"$NEW\" " << shellQuote(app.getFullPathName()) << "\n"
      << "xattr -dr com.apple.quarantine " << shellQuote(app.getFullPathName()) << "\n"
      << "rm -rf \"$T\"\nopen " << shellQuote(app.getFullPathName()) << "\n";
#else
    const auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    if (!exe.getParentDirectory().hasWriteAccess()) { error = "QS Studio's folder is read-only (" + exe.getParentDirectory().getFullPathName() + ")"; return false; }
    juce::String s;
    s << "#!/bin/sh\nexec >/dev/null 2>&1\n"
      << "while kill -0 " << pid << " 2>/dev/null; do sleep 1; done\n"
      << "T=$(mktemp -d)\n"
      << "tar -xzf " << shellQuote(downloaded.getFullPathName()) << " -C \"$T\" || exit 1\n"
      << "NEW=$(find \"$T\" -type f -name 'QS Studio' | head -1)\n"
      << "[ -f \"$NEW\" ] || exit 1\n"
      << "cp \"$NEW\" " << shellQuote(exe.getFullPathName() + ".new") << " && chmod +x " << shellQuote(exe.getFullPathName() + ".new")
      << " && mv -f " << shellQuote(exe.getFullPathName() + ".new") << " " << shellQuote(exe.getFullPathName()) << "\n"
      << "rm -rf \"$T\"\n"
      << "cd " << shellQuote(exe.getParentDirectory().getFullPathName()) << " && nohup " << shellQuote(exe.getFullPathName()) << " &\n";
#endif
    auto script = dir.getChildFile("install-update.sh");
    if (!script.replaceWithText(s, false, false, "\n")) { error = "can't write the update script"; return false; }
    script.setExecutePermission(true);
    juce::ChildProcess p;
    if (!p.start(juce::StringArray{"/bin/sh", "-c", "nohup /bin/sh " + shellQuote(script.getFullPathName()) + " >/dev/null 2>&1 &"})) {
        error = "can't start the update script";
        return false;
    }
    p.waitForProcessToFinish(5000);
    return true;
#endif
}

class DownloadJob : public juce::ThreadWithProgressWindow {
public:
    DownloadJob(Info i, juce::File d) : juce::ThreadWithProgressWindow("Downloading QS Studio " + i.version, false, true), info(std::move(i)), dir(std::move(d)) {}
    void run() override {
        setStatusMessage("Downloading " + info.file + "...");
        dir.createDirectory();
        file = dir.getChildFile(info.file);
        if (!download(base() + info.file, file, error, this)) return;
        setStatusMessage("Checking the download...");
        juce::FileInputStream in(file);
        const auto hash = juce::SHA256(in).toHexString();
        if (info.sha256.isNotEmpty() && !hash.equalsIgnoreCase(info.sha256)) {
            error = "the download doesn't match the release (checksum differs), so nothing was changed";
            file.deleteFile();
            return;
        }
        ok = true;
    }
    Info info;
    juce::File dir, file;
    juce::String error;
    bool ok = false;
};

// "What's new" with Update now / Later / Skip this version.
class UpdateDialog : public juce::Component {
public:
    std::function<void()> onUpdate, onSkip, onClose;

    UpdateDialog(const Info& info, const juce::String& current) {
        title = "QS Studio " + info.version + " is available";
        sub = "You have " + current + ". Your settings, libraries and backups in \"QS Studio Data\" stay as they are.";
        auto changes = changesSince(info.changelog, current);
        notes.setMultiLine(true);
        notes.setReadOnly(true);
        notes.setScrollbarsShown(true);
        notes.setFont(theme::uiFont(14.5f));
        notes.setText(changes.isNotEmpty() ? changes : juce::String("No list of changes came with this release."));
        for (juce::Component* c : std::initializer_list<juce::Component*>{&notes, &updateBtn, &laterBtn, &skipBtn}) addAndMakeVisible(c);
        updateBtn.onClick = [this] { if (onUpdate) onUpdate(); };
        laterBtn.onClick = [this] { if (onClose) onClose(); };
        skipBtn.onClick = [this] { if (onSkip) onSkip(); };
        updateBtn.setTooltip("Downloads the new version, closes QS Studio, puts the new program in place and starts it again.");
        setSize(640, 520);
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(theme::bg);
        auto r = getLocalBounds().reduced(20, 16);
        g.setColour(theme::text);
        g.setFont(theme::uiFont(20.0f, true));
        g.drawText(title, r.removeFromTop(30), juce::Justification::centredLeft);
        g.setColour(theme::dim);
        g.setFont(theme::uiFont(14.0f));
        g.drawFittedText(sub, r.removeFromTop(40), juce::Justification::topLeft, 2);
    }
    void resized() override {
        auto r = getLocalBounds().reduced(20, 16);
        r.removeFromTop(76);
        auto row = r.removeFromBottom(36);
        updateBtn.setBounds(row.removeFromRight(150));
        row.removeFromRight(10);
        laterBtn.setBounds(row.removeFromRight(100));
        skipBtn.setBounds(row.removeFromLeft(160));
        r.removeFromBottom(12);
        notes.setBounds(r);
    }

private:
    juce::String title, sub;
    juce::TextEditor notes;
    juce::TextButton updateBtn{"Update now"}, laterBtn{"Later"}, skipBtn{"Skip this version"};
};

}  // namespace updates
