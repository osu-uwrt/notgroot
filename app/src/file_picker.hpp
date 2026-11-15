// The desktop's "open file" dialog (osascript on macOS, else zenity or kdialog), run as its own process so the editor keeps drawing
// while it is open: the UI thread opens it, then polls for the file picked.
#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace notgroot::app {
class FilePicker {
  public:
    // Finds the picker program on PATH (zenity, else kdialog; osascript on macOS).
    FilePicker();
    ~FilePicker(); // closes a dialog that is still open
    FilePicker(const FilePicker &) = delete;
    FilePicker &operator=(const FilePicker &) = delete;

    // The picker program found, or empty when the desktop has none.
    const std::string &program() const {
        return program_;
    }

    // The X11 window the dialog stays above (0: none).
    void setParent(unsigned long x11Window) {
        parent_ = x11Window;
    }

    // Opens the dialog at `start` (a file to preselect or a folder; empty: home) listing `patterns` (e.g. *.yaml)
    // under the filter name `kind`, or everything; `folder` picks a folder instead, `save` a file to write (`start`
    // names it). False when there is no picker or a dialog is already open.
    bool open(const std::string &title, const std::filesystem::path &start, const std::string &kind,
              const std::vector<std::string> &patterns, bool folder = false, bool save = false);
    bool busy() const;
    // The file picked, once, after the dialog closed on one (nothing on cancel or failure).
    std::optional<std::filesystem::path> take();
    // A Browse button's tooltip: what it does, or why it can't.
    const char *hint() const;

    // The command line open() runs for `program` (zenity, kdialog or osascript).
    static std::vector<std::string> command(const std::string &program, const std::string &title,
                                            const std::string &start, const std::string &kind,
                                            const std::vector<std::string> &patterns, unsigned long parent,
                                            bool folder = false, bool save = false);

  private:
    struct State; // shared with the thread that waits for the dialog
    std::shared_ptr<State> state_;
    std::string program_;
    unsigned long parent_ = 0;
};
} // namespace notgroot::app
