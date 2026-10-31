// Colors the editor draws itself: btstudio's node categories, and a steady hue per blackboard key so every use of
// one key reads alike. Both adapt to the theme (light or dark paper) through the theme's own contrast helpers.
#pragma once
#include "notgroot/core/models.hpp"
#include <imgui.h>
#include <string>

namespace notgroot::ui {
bool darkTheme();
// A node category's color (btstudio: action green, condition blue, control orange, decorator purple, subtree
// cyan, unknown gray); `root` is the tree card's red.
ImVec4 categoryColor(core::Category);
ImVec4 rootColor();
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
ImVec4 mix(ImVec4 a, ImVec4 b, float t);
} // namespace notgroot::ui
