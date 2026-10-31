// The editor's panels. Each draws inside a window the host has begun (so the host decides docking), except the
// floating ones (finder, picker, dialogs) which open their own.
#pragma once
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

// Floating: Ctrl+P finder / command palette, the node picker, and the dialogs (names, ports, unsaved changes).
void drawFinder(Session &);
void drawPicker(Session &);
void drawDialogs(Session &);
// The keyboard guide (F1): every command with its key, and the canvas's own keys; searchable.
void drawShortcuts(Session &);
// The canvas's keys that are not commands (moving, folding, selecting), as {keys, what they do}.
const std::vector<std::pair<const char *, const char *>> &canvasKeys();
// Runs the commands whose shortcut was pressed this frame.
void handleShortcuts(Session &);
} // namespace notgroot::ui
