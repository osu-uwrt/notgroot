// What the panels share: the open workspace with its undo history, the open trees and their views, the cursor and
// selection, cached problems and variables, the commands (menus, shortcuts and the command palette run the same ones)
// and the requests one panel makes of another (focus the inspector, open the node picker, reveal a node).
#pragma once
#include "notgroot/core/edit.hpp"
#include "notgroot/core/history.hpp"
#include "notgroot/core/validate.hpp"
#include "notgroot/core/variables.hpp"
#include "notgroot/core/workspace.hpp"
#include <functional>
#include <imgui.h>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace notgroot::ui {
// A card on the canvas: a node, or (for a subtree opened in place) a node of that subtree reached through a call.
struct CardKey {
    core::NodeRef ref;    // the node itself (in its own tree)
    core::NodeRef origin; // the SubTree call it is shown under; empty for the tree's own nodes
    bool operator==(const CardKey &o) const {
        return ref == o.ref && origin == o.origin;
    }
    bool operator!=(const CardKey &o) const {
        return !(*this == o);
    }
    bool readOnly() const {
        return !origin.empty();
    }
};

// How a tree is shown: scroll, zoom, folded nodes and subtrees opened in place (by path in the tree).
struct ViewState {
    ImVec2 pan{0, 0};
    float zoom = 1;
    std::set<std::vector<std::size_t>> folded;
    std::set<std::vector<std::size_t>> opened; // SubTree calls shown expanded
    bool fit = true;                           // fit the tree to the view on the next frame
    bool fitted = false;                       // still as fitted (no pan or zoom since): refit when the view resizes
    ImVec2 size{0, 0};                         // the canvas size last frame
};

struct Command {
    std::string id, label, group;
    ImGuiKeyChord chord = 0; // 0: none
    bool canvasOnly = false; // only while the canvas has focus (keys that mean text elsewhere)
    std::function<void()> run;
    std::function<bool()> enabled; // null: always
};

// The node picker (Tab, Shift+Tab, Ctrl+W, Ctrl+R, the palette): what the picked model is for.
enum class PickFor { Child, Sibling, Wrap, Retype };

class Session {
  public:
    Session();
    core::Workspace workspace;
    core::History history{workspace};
    core::Editor editor{workspace, history};

    // --- workspace ---
    bool open(const std::filesystem::path &, std::string &error);
    bool loadBuiltinModels(const std::filesystem::path &, std::string &error);
    void save();
    bool dirty() const;
    // Checks the files on disk (call every second or so): clean documents reload, edited ones are listed.
    void pollDisk();
    std::vector<core::Document *> conflicts; // changed on disk while edited here

    // --- trees and views ---
    std::vector<std::string> tabs;
    std::string active; // the tree on the canvas
    void openTree(const std::string &key);
    void closeTree(const std::string &key);
    ViewState &view(const std::string &key) {
        return views_[key];
    }
    bool horizontal = false; // left-right layout instead of top-down

    // --- cursor and selection (in the active tree) ---
    CardKey cursor;
    std::vector<core::NodeRef> selection; // editable nodes only
    void setCursor(const CardKey &, bool keepSelection = false);
    void setCursor(const core::NodeRef &ref) {
        setCursor(CardKey{ref, {}});
    }
    void toggleSelected(const core::NodeRef &);
    bool selected(const core::NodeRef &) const;
    // The nodes an edit applies to: the selection, else the cursor (when it is editable).
    std::vector<core::NodeRef> targets() const;
    // Opens the node's tree and puts the cursor on it.
    void reveal(const core::NodeRef &);
    bool revealPending = false; // the canvas scrolls the cursor into view

    // A key highlighted on the canvas (from the variables panel or the finder).
    std::string highlightKey;

    // --- after an edit ---
    // Moves the cursor to what an edit made, or shows why it refused.
    void edited(const core::NodeRef &made);
    void edited(bool ok);

    // --- cached analysis (recomputed when the workspace changes) ---
    const std::vector<core::Problem> &problems();
    const core::TreeVariables &variables(const std::string &key);
    // The worst problem on a node (none: -1).
    int severityAt(const core::NodeRef &);

    // --- requests between panels ---
    bool focusCanvas = false, focusInspector = false;
    std::string focusPort; // the inspector focuses this port's value
    struct Picker {
        bool open = false;
        PickFor purpose = PickFor::Child;
        core::NodeRef target;
        ImVec2 anchor{0, 0}; // screen position to open at
    } picker;
    void pick(PickFor);
    bool showFinder = false;
    std::string finderPrefix; // "" anything, ">" commands, "@" nodes here, "$" keys here
    bool showSearch = false;  // Ctrl+F in the canvas
    std::string search;
    struct Dialog {
        enum Kind { None, NewSubtree, NewMission, RenameTree, RenameKey, ExtractSubtree, EditPorts, Unsaved };
        Kind kind = None;
        std::string target, text;
        bool fresh = true;
    } dialog;
    void ask(Dialog::Kind, std::string target = {}, std::string text = {});
    std::function<void()> afterUnsaved; // what Save / Discard in the unsaved-changes dialog goes on to

    // --- status line ---
    void status(std::string text, bool error = false);
    std::string statusText;
    bool statusError = false;
    double statusTime = -100;

    // --- commands ---
    std::vector<Command> commands;
    const Command *command(const std::string &id) const;
    void run(const std::string &id);
    bool canvasFocused = false;   // the canvas window has keyboard focus this frame
    bool canvasHeld = false;      // it had it at the end of the last frame
    std::function<void()> quit;   // set by the host
    std::function<void()> browse; // File > Open: set by the host (a desktop dialog)

    // Copy of a model (pointers into the catalog die with the next edit).
    bool model(const std::string &id, core::NodeModel &out) const;

  private:
    void registerCommands();
    void validateCursor();
    std::map<std::string, ViewState> views_;
    unsigned problemsVersion_ = ~0u;
    std::vector<core::Problem> problems_;
    std::map<core::NodeRef, int> severity_;
    std::map<std::string, std::pair<unsigned, core::TreeVariables>> variables_;
};

// ImGui text input on a std::string.
bool inputString(const char *label, std::string &text, ImGuiInputTextFlags flags = 0,
                 ImGuiInputTextCallback callback = nullptr, void *user = nullptr);
bool inputStringWithHint(const char *label, const char *hint, std::string &text, ImGuiInputTextFlags flags = 0);
// The chord as text ("Ctrl+Shift+P").
std::string chordName(ImGuiKeyChord);
} // namespace notgroot::ui
