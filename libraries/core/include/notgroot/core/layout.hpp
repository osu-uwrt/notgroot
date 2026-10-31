// Automatic tree layout: the editor never stores positions. Children sit in order beside each other one level below
// their parent (top-down) or one column right of it (left-right); each subtree is packed against its neighbours by
// their outlines per level, and a parent is centred over its children.
#pragma once
#include <vector>

namespace notgroot::core {
struct LayoutBox {
    float width = 0, height = 0;
    std::vector<int> children; // indices into the box list
};

struct LayoutRect {
    float x = 0, y = 0, width = 0, height = 0;
};

struct LayoutOptions {
    bool horizontal = false; // left-right instead of top-down
    float siblingGap = 24;   // between neighbours
    float levelGap = 48;     // between levels
    float subtreeGap = 36;   // extra between neighbouring subtrees' outlines
};

// Lays out the tree rooted at `root` (boxes not reachable from it keep a zero rect). The root's box starts at (0, 0).
std::vector<LayoutRect> layoutTree(const std::vector<LayoutBox> &boxes, int root, const LayoutOptions & = {});
} // namespace notgroot::core
