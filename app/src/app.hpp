// The editor application: one window with the menu bar, a dockspace holding the panels, and the preferences
// (theme, scale, layout, recent workspaces) kept in the config directory.
#pragma once
#include "file_picker.hpp"
#include "notgroot/ui/session.hpp"
#include "window.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace notgroot::app {
struct Options {
    std::filesystem::path open;       // a trees directory or a tree file to open at start
    std::string theme;                // --theme: overrides the preferences for this run
    std::filesystem::path screenshot; // --screenshot: save the frame after `frames` frames, then quit
    int frames = 0;                   // --frames: quit after this many frames (0: run until closed)
    bool persist = true;              // --no-persist: read and write no preferences (tests, screenshots)
    std::string keys;                 // --keys: key presses replayed first, e.g. "Down,Down,Ctrl+E" (screenshots)
    std::string text;                 // --type: text typed after them
};

class App {
  public:
    explicit App(Options);
    ~App();
    int run();

  private:
    void frame();
    void menuBar();
    void statusBar();
    void aboutWindow();
    void dockspace();
    void defaultLayout(unsigned dock);
    void openWorkspace(const std::filesystem::path &);
    void loadWorkspace(const std::filesystem::path &);
    void updateTitle();
    // Quits now, or asks first when trees have unsaved edits.
    void quit();
    void replay();
    std::pair<ImGuiKey, ImGuiKeyChord> pendingRelease_{ImGuiKey_None, 0};

    // Theme: --theme, else the theme picked here (notgroot.yaml), else Nereus's (nereus/viewer.yaml), else
    // timing-board. Changes apply between frames.
    std::string preferredTheme() const;
    void setTheme(const std::string &id, bool follow);
    void applyPending();
    void savePreference(const char *key, const std::string &value);
    std::string preference(const char *key) const;

    unsigned logoTexture();
    void loadNodeIcons(); // content/icons/nodes as textures for the canvas

    Options opt_;
    std::unique_ptr<Window> window_;
    ui::Session session_;
    FilePicker picker_;
    std::vector<std::string> recent_; // recently opened workspaces
    bool closing_ = false;            // the user agreed to quit
    double lastPoll_ = 0;
    std::string title_;
    std::filesystem::path content_, config_;
    std::string pendingTheme_;
    bool pendingFollow_ = false, followNereus_ = true;
    float uiScale_ = 1, pendingScale_ = 0;
    bool showAbout_ = false, resetLayout_ = false;
    unsigned logo_ = 0;
    std::vector<unsigned> icons_;
    int logoWidth_ = 0, logoHeight_ = 0;
    int frameCount_ = 0;
};

// The content directory (themes, fonts, icons): the source tree's in a build, share/notgroot once installed.
std::filesystem::path contentDirectory();
// $XDG_CONFIG_HOME, else ~/.config (empty when neither is known).
std::filesystem::path configHome();
} // namespace notgroot::app
