// Layered tidy-tree layout with outline packing.
#include "notgroot/core/layout.hpp"
#include <algorithm>
#include <limits>

namespace notgroot::core {
namespace {
// Breadth (across siblings) and depth (down the levels) sizes of a box for the chosen direction.
float breadth(const LayoutBox &box, bool horizontal) {
    return horizontal ? box.height : box.width;
}
float depth(const LayoutBox &box, bool horizontal) {
    return horizontal ? box.width : box.height;
}

// A subtree's outline: for each level below its root, the leftmost and rightmost breadth it occupies.
struct Outline {
    std::vector<float> left, right;
};

struct Placer {
    const std::vector<LayoutBox> &boxes;
    const LayoutOptions &options;
    std::vector<float> offset; // breadth position of each box's centre, relative to its parent's centre

    // Lays out a subtree around its root's centre at breadth 0 and returns its outline.
    Outline place(int index) {
        const auto &box = boxes[std::size_t(index)];
        const float half = breadth(box, options.horizontal) / 2;
        Outline outline{{-half}, {half}};
        if (box.children.empty())
            return outline;

        // Children left to right: each one shifted just clear of everything before it, level by level.
        Outline merged;
        std::vector<float> centres;
        for (std::size_t i = 0; i < box.children.size(); ++i) {
            const int child = box.children[i];
            Outline next = place(child);
            float shift = 0;
            if (i > 0) {
                shift = -std::numeric_limits<float>::infinity();
                const auto shared = std::min(merged.right.size(), next.left.size());
                for (std::size_t level = 0; level < shared; ++level) {
                    const float gap = level == 0 ? options.siblingGap : options.subtreeGap;
                    shift = std::max(shift, merged.right[level] - next.left[level] + gap);
                }
            }
            centres.push_back(shift);
            for (std::size_t level = 0; level < next.left.size(); ++level) {
                const float l = next.left[level] + shift, r = next.right[level] + shift;
                if (level < merged.left.size()) {
                    merged.left[level] = std::min(merged.left[level], l);
                    merged.right[level] = std::max(merged.right[level], r);
                } else {
                    merged.left.push_back(l);
                    merged.right.push_back(r);
                }
            }
        }

        // Centre the parent over its first and last child.
        const float middle = (centres.front() + centres.back()) / 2;
        for (std::size_t i = 0; i < box.children.size(); ++i)
            offset[std::size_t(box.children[i])] = centres[i] - middle;
        for (std::size_t level = 0; level < merged.left.size(); ++level) {
            outline.left.push_back(merged.left[level] - middle);
            outline.right.push_back(merged.right[level] - middle);
        }
        return outline;
    }
};

void levels(const std::vector<LayoutBox> &boxes, int index, std::size_t level, bool horizontal,
            std::vector<float> &extent) {
    if (extent.size() <= level)
        extent.resize(level + 1, 0);
    extent[level] = std::max(extent[level], depth(boxes[std::size_t(index)], horizontal));
    for (const int child : boxes[std::size_t(index)].children)
        levels(boxes, child, level + 1, horizontal, extent);
}

void assign(const std::vector<LayoutBox> &boxes, const std::vector<float> &offset, const std::vector<float> &levelStart,
            int index, std::size_t level, float centre, bool horizontal, std::vector<LayoutRect> &rects) {
    const auto &box = boxes[std::size_t(index)];
    auto &rect = rects[std::size_t(index)];
    rect.width = box.width;
    rect.height = box.height;
    if (horizontal) {
        rect.x = levelStart[level];
        rect.y = centre - box.height / 2;
    } else {
        rect.x = centre - box.width / 2;
        rect.y = levelStart[level];
    }
    for (const int child : box.children)
        assign(boxes, offset, levelStart, child, level + 1, centre + offset[std::size_t(child)], horizontal, rects);
}
} // namespace

std::vector<LayoutRect> layoutTree(const std::vector<LayoutBox> &boxes, int root, const LayoutOptions &options) {
    std::vector<LayoutRect> rects(boxes.size());
    if (root < 0 || std::size_t(root) >= boxes.size())
        return rects;
    Placer placer{boxes, options, std::vector<float>(boxes.size(), 0)};
    placer.place(root);

    // Each level starts after the deepest box of the level above.
    std::vector<float> extent, start;
    levels(boxes, root, 0, options.horizontal, extent);
    float at = 0;
    for (const float size : extent) {
        start.push_back(at);
        at += size + options.levelGap;
    }
    const float centre = breadth(boxes[std::size_t(root)], options.horizontal) / 2;
    assign(boxes, placer.offset, start, root, 0, centre, options.horizontal, rects);
    return rects;
}
} // namespace notgroot::core
