// Colors the editor draws itself: btstudio's node categories, and a steady hue per blackboard key so every use of
// one key reads alike. Both adapt to the theme (light or dark paper) through the theme's own contrast helpers.
#pragma once
#include "notgroot/core/models.hpp"
#include <imgui.h>
#include <string>
#include <vector>

namespace notgroot::ui {
bool darkTheme();
// A node category's color (btstudio: action green, condition blue, control orange, decorator purple, subtree
// cyan, unknown gray); `root` is the tree card's red.
ImVec4 categoryColor(core::Category);
ImVec4 rootColor();
// A subtree's color: its own (notgroot:color metadata) if set, else the subtree category's.
ImVec4 subtreeColor(const std::string &hex);
// What sets a node type apart on the canvas, after Groot (bt_editor/resources/NodesStyle.json): an accent for its
// title, icon and outline, and the name of its icon (content/icons/nodes). Plain nodes have neither.
struct Accent {
    ImVec4 color{0, 0, 0, 0};
    bool colored = false;
    const char *icon = nullptr;
};
Accent nodeAccent(const std::string &id, core::Category);
// `color` nudged lighter or darker until it reads on `paper` (4.5:1).
ImVec4 readableOn(ImVec4 color, ImVec4 paper);
// Node icon textures (white shapes, drawn tinted), registered by the window by name.
void setNodeIcon(const std::string &name, ImTextureID);
ImTextureID nodeIcon(const char *name);
// Ink that reads on a category's fill.
ImVec4 inkOnCategory(core::Category);
// A blackboard key's color: hue from its name, lightness for the theme's paper.
ImVec4 keyColor(const std::string &key);
// Port direction badge colors: in green, out orange, in-out the accent.
ImVec4 directionColor(core::PortDirection);
const char *directionBadge(core::PortDirection); // IN, OUT, IO
// Problem severity colors from the theme (error, warn, muted).
ImVec4 severityColor(int severity);
ImVec4 withAlpha(ImVec4, float);

// The canvas zooms in steps, and its card fonts are baked at every step so text is drawn at the size it was
// rasterized at (scaling a bitmap font blurs it).
const std::vector<float> &zoomLevels();
// The step nearest a zoom; the next step in or out from one.
float nearestZoom(float zoom);
float stepZoom(float zoom, int steps);
// Card fonts per step (title, body), set by the window after loading fonts; null when a step has none.
void setCanvasFonts(std::vector<ImFont *> titles, std::vector<ImFont *> bodies);
ImFont *canvasTitleFont(float zoom);
ImFont *canvasBodyFont(float zoom);
ImVec4 mix(ImVec4 a, ImVec4 b, float t);
} // namespace notgroot::ui
