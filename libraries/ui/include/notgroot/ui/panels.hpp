// The editor's panels. Each draws inside a window the host has begun (so the host decides docking), except the
// floating ones (finder, picker, dialogs) which open their own.
#pragma once
#include "notgroot/core/globals.hpp"
#include "notgroot/ui/session.hpp"

namespace notgroot::ui {
// Tree tabs and the canvas: cards laid out automatically, keyboard navigation and editing.
void drawCanvas(Session &);
// Missions and subtrees with a filter; opening, creating, renaming.
void drawExplorer(Session &);
// Every node model by category with a filter; Enter or double-click adds it under the cursor.
void drawPalette(Session &);
// The cursor's node: name, type, ports (literal or {key}, with completion), special attributes; or the tree itself.
void drawInspector(Session &);
// The active tree's blackboard keys: reads, writes, ports, problems; highlight, rename, go to uses.
void drawVariables(Session &);
// Every problem in the workspace; Enter or click goes to it.
void drawProblems(Session &);

// A port's value with {key} completion, set on Enter or when the field loses focus: the inspector's rows, and the
// edit on a card (double-click a row). Takes the width set before it.
enum class FieldEnd { Editing, Entered, Left, Canceled }; // Left: focus moved away; Canceled: Esc
FieldEnd portValueField(Session &, const core::NodeRef &, const std::string &name, const std::string &value,
                        const std::vector<std::string> &keys, bool focus, std::string *live = nullptr);
// Keys a value can name in this tree: its keys and its ports.
std::vector<std::string> keysIn(Session &, const std::string &tree);

// Editing a tree's globals, shared by the Variables panel and the box beside a subtree opened in place.
// Sets the whole list as one undo step; a refusal goes to the status line, and globalCycle names a circle.
bool applyGlobals(Session &, const std::string &tree, const std::vector<core::Global> &, const std::string &what);
// What a global's value can name: the tree's inputs and globals (its locals don't exist yet).
std::vector<std::string> globalNames(Session &, const std::string &tree);
// A global's value with {key} completion, set when the field loses focus. Takes the width set before it.
void globalValueField(Session &, const std::string &tree, const std::string &name, const std::string &value,
                      const std::vector<std::string> &usable);
void removeGlobalButton(Session &, const std::string &tree, const std::string &name);
// Name, value and + for a new global.
void newGlobalFields(Session &, const std::string &tree, const std::vector<std::string> &usable, float nameWidth);

// Floating: Ctrl+P finder / command palette, the node picker, and the dialogs (names, ports, unsaved changes).
void drawFinder(Session &);
void drawPicker(Session &);
void drawDialogs(Session &);
// The keyboard guide (F1): every command with its key, and the canvas's own keys; searchable.
void drawShortcuts(Session &);
// The getting-started tour (first start, Help > Getting started tour): what differs from Groot, each move shown with
// the mouse and with its keys.
void drawTour(Session &);
// The canvas's keys that are not commands (moving, folding, selecting), as {keys, what they do}.
const std::vector<std::pair<const char *, const char *>> &canvasKeys();
// Runs the commands whose shortcut was pressed this frame.
void handleShortcuts(Session &);
} // namespace notgroot::ui
