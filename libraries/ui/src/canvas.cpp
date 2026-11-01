// Canvas: the active tree as cards in an automatic tidy layout, with keyboard navigation and editing.
#include "notgroot/core/globals.hpp"
#include "notgroot/core/layout.hpp"
#include "notgroot/ui/panels.hpp"
#include "notgroot/ui/style.hpp"
#include "notgroot/ui/theme.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <functional>
#include <imgui_internal.h>
#include <map>

namespace notgroot::ui {
using namespace core;

namespace {
struct Row {
    std::string port, value;
    PortDirection direction = PortDirection::Input;
    bool modeled = true;  // a port of the node's model (else an attribute BT.CPP would reject)
    bool special = false; // _skipIf and friends
    bool missing = false; // a model port the node does not set
};

struct Card {
    CardKey key;
    const Element *element = nullptr;
    Category category = Category::Unknown;
    bool root = false, call = false, opened = false, folded = false;
    bool floating = false; // where its parent can't hold it (a keyboard move on its way)
    std::string title, subtitle, comment;
    std::string color; // a subtree's own color (#rrggbb), for its calls and its root card
    Accent accent;     // Groot's accent and icon for its type
    std::vector<Row> rows;
    int hidden = 0; // nodes folded away under it
    int parent = -1, depth = 0;
    std::vector<int> children;
    ImVec2 size{0, 0}, position{0, 0};
};

struct Scene {
    std::string tree;
    std::size_t signature = 0;
    std::vector<Card> cards;
    ImVec2 extent{0, 0};
    int find(const CardKey &key) const {
        for (std::size_t i = 0; i < cards.size(); ++i)
            if (cards[i].key == key)
                return int(i);
        return -1;
    }
};

// Card metrics at 100 % zoom (scaled by the interface scale).
struct Metrics {
    ImFont *title, *body;
    float pad, rowGap, maxValue, minWidth, maxWidth, header;
};

Metrics metrics() {
    Metrics m;
    m.title = typeRamp().strong ? typeRamp().strong : ImGui::GetFont();
    m.body = typeRamp().small ? typeRamp().small : ImGui::GetFont();
    m.pad = ui(8);
    m.rowGap = ui(3);
    m.maxValue = ui(230);
    m.minWidth = ui(120);
    m.maxWidth = ui(380);
    m.header = m.title->FontSize + 2 * ui(5);
    return m;
}

// The gap between a parent's row and its children's. Edges turn in the half of it next to the children, so they
// never cross a tall card on the parent's row.
float levelGap(bool horizontal) {
    return ui(horizontal ? 64 : 52);
}

float textWidth(ImFont *font, const std::string &text) {
    return font->CalcTextSizeA(font->FontSize, FLT_MAX, 0, text.c_str()).x;
}

// Text cut to fit `width`, with an ellipsis.
std::string fitText(ImFont *font, const std::string &text, float width) {
    if (textWidth(font, text) <= width)
        return text;
    std::string cut = text;
    while (!cut.empty() && textWidth(font, cut + "…") > width)
        cut.pop_back();
    return cut + "…";
}

std::string firstLine(std::string text) {
    const auto start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return {};
    text = text.substr(start);
    const auto end = text.find('\n');
    if (end != std::string::npos)
        text = text.substr(0, end) + " …";
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.pop_back();
    return text;
}

int countNodes(const Element &element) {
    int count = 0;
    for (const auto *child : element.elements())
        count += 1 + countNodes(*child);
    return count;
}

void describe(Session &session, Card &card, const Element &element) {
    const auto &catalog = session.workspace.catalog();
    card.element = &element;
    card.call = isSubTree(element);
    const auto id = card.call ? element.get("ID") : nodeId(element);
    card.title = id;
    card.subtitle = element.get("name");
    card.category = nodeCategory(element, catalog);
    card.floating = isFloating(session.workspace, element);
    card.accent = nodeAccent(card.call ? "SubTree" : id, card.category);
    if (card.call)
        card.color = treeColor(session.workspace, id);
    const NodeModel *model = catalog.find(id);
    if (model && !card.call && model->category == Category::SubTree)
        model = nullptr;

    // Ports in the model's order, then attributes the model does not know, then the special ones.
    if (model)
        for (const auto &port : model->ports) {
            Row row{port.name, element.get(port.name), port.direction, true, false, !element.has(port.name)};
            if (row.missing && !card.call)
                row.value = port.defaultValue;
            card.rows.push_back(row);
        }
    for (const auto &attribute : element.attributes) {
        if (attribute.name == "ID" || attribute.name == "name" || (model && model->port(attribute.name)))
            continue;
        const bool special = isReservedAttribute(attribute.name);
        card.rows.push_back(
            {attribute.name, attribute.value, PortDirection::Input, special || !model || card.call, special, false});
    }
    // Comments right above the node.
    if (element.parent)
        for (auto at = element.index(); at > 0 && element.parent->children[at - 1]->isComment(); --at)
            card.comment = firstLine(element.parent->children[at - 1]->text);
}

// The icon before a title is a square as tall as the title's line.
float iconRoom(const Card &card, const Metrics &m) {
    return nodeIcon(card.accent.icon) ? m.title->FontSize + ui(5) : 0;
}

void measure(Card &card, const Metrics &m) {
    float width = iconRoom(card, m) + textWidth(m.title, card.title);
    if (!card.comment.empty())
        width += m.pad + textWidth(m.body, "//");
    if (!card.subtitle.empty())
        width = std::max(width, textWidth(m.body, card.subtitle));
    const float badge = textWidth(m.body, "OUT") + m.pad;
    for (auto &row : card.rows) {
        row.value = fitText(m.body, row.value, m.maxValue);
        width = std::max(width, badge + textWidth(m.body, row.port) + m.pad + textWidth(m.body, row.value));
    }
    card.size.x = std::clamp(width + 2 * m.pad, m.minWidth, m.maxWidth);
    float height = m.header;
    if (!card.subtitle.empty())
        height += m.body->FontSize + m.rowGap;
    if (!card.rows.empty())
        height += m.pad + float(card.rows.size()) * (m.body->FontSize + m.rowGap) + m.pad - m.rowGap;
    card.size.y = height;
}

// The element holding a tree's root node (the globals wrapper or the tree), and its ref.
std::pair<const Element *, NodeRef> treeBodyRef(const Element &tree, const NodeRef &treeRef) {
    const auto *body = treeBody(tree);
    return {body, body == &tree ? treeRef : treeRef.child(body->index())};
}

// A child to draw as a card: not a comment, not the globals' Script.
bool isNode(const Element &element) {
    return !element.isComment() && !(element.tag == "Script" && isGlobalsPart(element));
}

// A call inside an opened subtree, named by the calls that lead to it (the one in this tree first).
std::vector<NodeRef> callChain(const CardKey &key) {
    std::vector<NodeRef> chain{key.origin};
    chain.insert(chain.end(), key.via.begin(), key.via.end());
    chain.push_back(key.ref);
    return chain;
}

bool isOpened(Session &session, const CardKey &key) {
    auto &view = session.view(session.active);
    return key.origin.empty() ? view.opened.count(key.ref.path) > 0 : view.openedInside.count(callChain(key)) > 0;
}

void toggleOpened(Session &session, const CardKey &key) {
    auto &view = session.view(session.active);
    if (key.origin.empty()) {
        if (!view.opened.erase(key.ref.path))
            view.opened.insert(key.ref.path);
    } else if (!view.openedInside.erase(callChain(key)))
        view.openedInside.insert(callChain(key));
}

void build(Session &session, Scene &scene, const Element &element, const CardKey &key, int parent, int depth,
           std::vector<std::string> &stack, const Metrics &m) {
    Card card;
    card.key = key;
    card.parent = parent;
    card.depth = depth;
    const bool own = key.origin.empty();
    if (element.tag == "BehaviorTree") {
        card.root = true;
        card.element = &element;
        const auto *entry = session.workspace.tree(key.ref.tree);
        card.title = key.ref.tree;
        card.subtitle = entry && key.ref.tree != entry->id ? "mission" : "subtree";
        // Its globals live in the Variables panel; the card says how many.
        if (const auto count = readGlobals(element).globals.size())
            card.subtitle += " · " + std::to_string(count) + (count == 1 ? " global" : " globals");
        if (entry && key.ref.tree == entry->id)
            card.color = treeColor(session.workspace, entry->id);
        if (entry && key.ref.tree == entry->id)
            for (const auto &port : session.workspace.subtreeModel(entry->id).ports)
                card.rows.push_back({port.name, "", port.direction, true, false, false});
    } else
        describe(session, card, element);
    auto &view = session.view(session.active);
    card.folded = own && view.folded.count(key.ref.path) && !element.elements().empty();
    card.opened = card.call && isOpened(session, key);
    if (card.folded)
        card.hidden = countNodes(element);
    measure(card, m);
    const int index = int(scene.cards.size());
    scene.cards.push_back(card);
    if (parent >= 0)
        scene.cards[std::size_t(parent)].children.push_back(index);
    if (card.folded)
        return;

    // A subtree opened in place: its root's node under the call (read-only; calls in it open further).
    if (card.opened) {
        const auto id = element.get("ID");
        const auto *entry = session.workspace.tree(id);
        if (entry && std::find(stack.begin(), stack.end(), id) == stack.end()) {
            stack.push_back(id);
            auto via = key.via;
            if (!own)
                via.push_back(key.ref);
            const auto [body, bodyRef] = treeBodyRef(*entry->tree, {id, {}});
            for (std::size_t i = 0; i < body->children.size(); ++i)
                if (isNode(*body->children[i]))
                    build(session, scene, *body->children[i], {bodyRef.child(i), own ? key.ref : key.origin, via},
                          index, depth + 1, stack, m);
            stack.pop_back();
        }
        return;
    }
    // A tree with globals: its root sits in their wrapper, after their Script (neither is shown).
    const auto [body, bodyRef] = card.root ? treeBodyRef(element, key.ref) : std::pair{&element, key.ref};
    for (std::size_t i = 0; i < body->children.size(); ++i)
        if (isNode(*body->children[i]))
            build(session, scene, *body->children[i], {bodyRef.child(i), key.origin, key.via}, index, depth + 1, stack,
                  m);
}

std::size_t signature(Session &session) {
    std::size_t h = std::hash<std::string>{}(session.active);
    const auto mix = [&](std::size_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
    mix(session.workspace.version());
    mix(session.horizontal);
    mix(std::hash<float>{}(interfaceScale()));
    mix(std::hash<const void *>{}(typeRamp().strong));
    mix(std::hash<std::string>{}(currentTheme()));
    const auto &view = session.view(session.active);
    for (const auto &path : view.folded)
        for (const auto i : path)
            mix(i + 1), mix(7);
    mix(13);
    for (const auto &path : view.opened)
        for (const auto i : path)
            mix(i + 1), mix(11);
    for (const auto &chain : view.openedInside) {
        for (const auto &ref : chain) {
            mix(std::hash<std::string>{}(ref.tree));
            for (const auto i : ref.path)
                mix(i + 1);
        }
        mix(17);
    }
    return h;
}

Scene &scene(Session &session) {
    static Scene cached;
    const auto sig = signature(session);
    if (cached.tree == session.active && cached.signature == sig)
        return cached;
    cached = Scene{session.active, sig, {}, {0, 0}};
    const auto *entry = session.workspace.tree(session.active);
    if (!entry)
        return cached;
    const auto m = metrics();
    std::vector<std::string> stack{entry->id};
    build(session, cached, *entry->tree, {{session.active, {}}, {}}, -1, 0, stack, m);

    std::vector<LayoutBox> boxes;
    for (const auto &card : cached.cards)
        boxes.push_back({card.size.x, card.size.y, card.children});
    LayoutOptions options;
    options.horizontal = session.horizontal;
    options.siblingGap = ui(18);
    options.subtreeGap = ui(30);
    options.levelGap = levelGap(session.horizontal);
    const auto rects = layoutTree(boxes, 0, options);
    float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
    for (const auto &rect : rects) {
        minX = std::min(minX, rect.x);
        minY = std::min(minY, rect.y);
        maxX = std::max(maxX, rect.x + rect.width);
        maxY = std::max(maxY, rect.y + rect.height);
    }
    for (std::size_t i = 0; i < rects.size(); ++i)
        cached.cards[i].position = {rects[i].x - minX, rects[i].y - minY};
    cached.extent = {maxX - minX, maxY - minY};
    return cached;
}

// --- drawing ---

void dashedLine(ImDrawList *draw, ImVec2 p, ImVec2 q, ImU32 color, float thickness, float dash) {
    const float length = std::hypot(q.x - p.x, q.y - p.y);
    for (float t = 0; t < length; t += 2 * dash) {
        const float e = std::min(t + dash, length);
        draw->AddLine({p.x + (q.x - p.x) * t / length, p.y + (q.y - p.y) * t / length},
                      {p.x + (q.x - p.x) * e / length, p.y + (q.y - p.y) * e / length}, color, thickness);
    }
}

void dashedRect(ImDrawList *draw, ImVec2 a, ImVec2 b, ImU32 color, float thickness, float dash) {
    const auto line = [&](ImVec2 p, ImVec2 q) { dashedLine(draw, p, q, color, thickness, dash); };
    line(a, {b.x, a.y});
    line({b.x, a.y}, b);
    line(b, {a.x, b.y});
    line({a.x, b.y}, a);
}

bool mentions(const std::string &value, const std::string &key) {
    return !key.empty() && value.find("{" + key + "}") != std::string::npos;
}

bool rowWritesKey(const Row &row, const std::string &key) {
    return !key.empty() && row.value == key && row.port == "output_key";
}

struct View {
    ImVec2 origin; // screen position of canvas (0, 0)
    float zoom;
    ImFont *title = nullptr, *body = nullptr; // the card fonts baked for this zoom
    float titleSize = 0, bodySize = 0;
    ImVec2 at(ImVec2 p) const {
        return {origin.x + p.x * zoom, origin.y + p.y * zoom};
    }
};

View makeView(ImVec2 origin, float zoom, const Metrics &m) {
    View v{origin, zoom};
    v.title = canvasTitleFont(zoom);
    v.body = canvasBodyFont(zoom);
    // No baked font (ImGui's default): scale the 100 % one.
    v.titleSize = v.title ? v.title->FontSize : m.title->FontSize * zoom;
    v.bodySize = v.body ? v.body->FontSize : m.body->FontSize * zoom;
    if (!v.title)
        v.title = m.title;
    if (!v.body)
        v.body = m.body;
    return v;
}

// Text on whole pixels, so the glyphs land on the texels they were rasterized to.
void text(ImDrawList *draw, ImFont *font, float size, ImVec2 at, ImU32 color, const char *value) {
    draw->AddText(font, size, {std::round(at.x), std::round(at.y)}, color, value);
}

float drawnWidth(const View &view, const std::string &value) {
    return view.body->CalcTextSizeA(view.bodySize, FLT_MAX, 0, value.c_str()).x;
}

// Behind a subtree opened in place: a band in its color over its cards on each row (a tidy layout keeps them
// together on a row, so other cards stay outside), named, so it reads as shown, not edited.
void drawOpened(const Scene &scene, const View &view, ImDrawList *draw, bool horizontal) {
    for (const auto &card : scene.cards) {
        if (!card.opened || card.children.empty())
            continue;
        // Its cards' extent per row: along the row (lo, hi) and across it (top, bottom).
        std::map<int, std::array<float, 4>> rows;
        std::vector<int> open(card.children.begin(), card.children.end());
        while (!open.empty()) {
            const auto &inside = scene.cards[std::size_t(open.back())];
            open.pop_back();
            const float along = horizontal ? inside.position.y : inside.position.x;
            const float across = horizontal ? inside.position.x : inside.position.y;
            const float length = horizontal ? inside.size.y : inside.size.x;
            const float depth = horizontal ? inside.size.x : inside.size.y;
            auto [row, fresh] =
                rows.try_emplace(inside.depth, std::array<float, 4>{along, along + length, across, across + depth});
            if (!fresh)
                row->second = {std::min(row->second[0], along), std::max(row->second[1], along + length),
                               std::min(row->second[2], across), std::max(row->second[3], across + depth)};
            open.insert(open.end(), inside.children.begin(), inside.children.end());
        }
        const float pad = ui(10), gap = levelGap(horizontal) / 2;
        const ImVec4 color = !card.color.empty() ? subtreeColor(card.color) : categoryColor(Category::SubTree);
        ImVec2 label{0, 0};
        for (auto row = rows.begin(); row != rows.end(); ++row) {
            const auto next = std::next(row);
            // From half the gap above the row to half the gap above the next one, so the bands join up.
            const float top = row->second[2] - gap,
                        bottom = next != rows.end() ? next->second[2] - gap : row->second[3] + pad;
            const ImVec2 a = horizontal ? view.at({top, row->second[0] - pad}) : view.at({row->second[0] - pad, top});
            const ImVec2 b =
                horizontal ? view.at({bottom, row->second[1] + pad}) : view.at({row->second[1] + pad, bottom});
            draw->AddRectFilled(a, b, ImGui::GetColorU32(withAlpha(color, darkTheme() ? .07f : .1f)));
            if (row == rows.begin())
                label = a;
        }
        const std::string text = card.title + " · read-only · Ctrl+Enter to edit";
        notgroot::ui::text(draw, view.body, view.bodySize, {label.x + pad * view.zoom, label.y + ui(3) * view.zoom},
                           ImGui::GetColorU32(withAlpha(palette().muted, .9f)), text.c_str());
    }
}

void drawEdges(const Scene &scene, const View &view, ImDrawList *draw, bool horizontal) {
    const ImU32 color = ImGui::GetColorU32(withAlpha(palette().muted, .7f));
    const float thickness = std::max(1.f, ui(1.4f) * view.zoom);
    for (const auto &card : scene.cards) {
        if (card.children.empty())
            continue;
        const ImVec2 p0 = view.at(card.position),
                     p1 = view.at({card.position.x + card.size.x, card.position.y + card.size.y});
        for (const int index : card.children) {
            const auto &child = scene.cards[std::size_t(index)];
            const ImVec2 c0 = view.at(child.position);
            const ImVec2 c1 = view.at({child.position.x + child.size.x, child.position.y + child.size.y});
            const ImU32 ink = child.key.readOnly() && !card.key.readOnly()
                                  ? ImGui::GetColorU32(withAlpha(categoryColor(Category::SubTree), .8f))
                                  : color;
            if (horizontal) {
                const ImVec2 from{p1.x, (p0.y + p1.y) / 2}, to{c0.x, (c0.y + c1.y) / 2};
                const float mid = std::max(from.x, to.x - levelGap(true) * view.zoom / 2);
                const ImVec2 points[] = {from, {mid, from.y}, {mid, to.y}, to};
                if (child.floating)
                    for (int k = 0; k < 3; ++k)
                        dashedLine(draw, points[k], points[k + 1], ImGui::GetColorU32(palette().warn), thickness,
                                   ui(3) * view.zoom);
                else
                    draw->AddPolyline(points, 4, ink, 0, thickness);
            } else {
                const ImVec2 from{(p0.x + p1.x) / 2, p1.y}, to{(c0.x + c1.x) / 2, c0.y};
                const float mid = std::max(from.y, to.y - levelGap(false) * view.zoom / 2);
                const ImVec2 points[] = {from, {from.x, mid}, {to.x, mid}, to};
                if (child.floating)
                    for (int k = 0; k < 3; ++k)
                        dashedLine(draw, points[k], points[k + 1], ImGui::GetColorU32(palette().warn), thickness,
                                   ui(3) * view.zoom);
                else
                    draw->AddPolyline(points, 4, ink, 0, thickness);
            }
        }
    }
}

void drawCard(Session &session, const Card &card, const View &view, ImDrawList *draw, const Metrics &m, bool cursor,
              bool picked, bool matched, int severity) {
    const float z = view.zoom;
    const ImVec2 a = view.at(card.position),
                 b = view.at({card.position.x + card.size.x, card.position.y + card.size.y});
    const float rounding = ui(5) * z;
    const auto &style = ImGui::GetStyle();
    // A subtree opened in place is grayed: it is shown, not edited here.
    const bool readOnly = card.key.readOnly();
    const float alpha = readOnly ? .55f : 1.f;
    const ImVec4 body = style.Colors[ImGuiCol_FrameBg];
    const ImVec4 band = !card.color.empty() ? subtreeColor(card.color)
                        : card.root         ? rootColor()
                                            : categoryColor(card.category);
    // Plain cards: the type is a bar down the side, and the nodes that steer the flow have Groot's color and icon
    // on their title.
    ImVec4 bandInk = card.accent.colored ? readableOn(card.accent.color, body) : palette().text;
    if (readOnly)
        bandInk = mix(bandInk, palette().muted, .6f);
    const bool keyed = std::any_of(card.rows.begin(), card.rows.end(), [&](const Row &row) {
        return row.value == "{" + session.highlightKey + "}" || mentions(row.value, session.highlightKey) ||
               rowWritesKey(row, session.highlightKey);
    });

    // Shadow, body, the rule under the title, the type bar (the tree's own card has none).
    if (!readOnly)
        draw->AddRectFilled({a.x + 2 * z, a.y + 3 * z}, {b.x + 2 * z, b.y + 3 * z},
                            ImGui::GetColorU32(ImVec4(0, 0, 0, darkTheme() ? .35f : .12f)), rounding);
    draw->AddRectFilled(a, b, ImGui::GetColorU32(readOnly ? mix(body, style.Colors[ImGuiCol_WindowBg], .5f) : body),
                        rounding);
    const float headerHeight = (m.header + (card.subtitle.empty() ? 0 : m.body->FontSize + m.rowGap)) * z;
    if (!card.rows.empty())
        draw->AddLine({a.x, std::round(a.y + headerHeight)}, {b.x, std::round(a.y + headerHeight)},
                      ImGui::GetColorU32(withAlpha(style.Colors[ImGuiCol_Border], alpha)), std::max(1.f, z * .8f));
    if (!card.root || !card.color.empty())
        draw->AddRectFilled(a, {a.x + std::max(2.f, ui(3) * z), b.y},
                            ImGui::GetColorU32(readOnly ? withAlpha(palette().muted, .5f) : band), rounding,
                            ImDrawFlags_RoundCornersLeft);

    // Icon, title, name, comment marker.
    float y = a.y + ui(5) * z;
    const float x = a.x + m.pad * z;
    const float icon = iconRoom(card, m) * z;
    if (icon > 0) {
        const float side = m.title->FontSize * z;
        draw->AddImage(
            nodeIcon(card.accent.icon), {std::round(x), std::round(y)}, {std::round(x + side), std::round(y + side)},
            {0, 0}, {1, 1},
            ImGui::GetColorU32(withAlpha(card.accent.colored ? bandInk : withAlpha(palette().text, .8f), alpha)));
    }
    text(draw, view.title, view.titleSize, {x + icon, y}, ImGui::GetColorU32(bandInk), card.title.c_str());
    if (!card.comment.empty())
        text(draw, view.body, view.bodySize,
             {b.x - m.pad * z - drawnWidth(view, "//"), y + (m.title->FontSize - m.body->FontSize) * z},
             ImGui::GetColorU32(withAlpha(palette().muted, alpha)), "//");
    y += m.title->FontSize * z;
    if (!card.subtitle.empty()) {
        y += m.rowGap * z;
        text(draw, view.body, view.bodySize, {x, y}, ImGui::GetColorU32(withAlpha(palette().muted, alpha)),
             card.subtitle.c_str());
    }

    // Port rows: badge, name, value (a key in its own color).
    y = a.y + headerHeight + m.pad * z;
    const float badgeWidth = (textWidth(m.body, "OUT") + m.pad) * z;
    for (const auto &row : card.rows) {
        const float line = m.body->FontSize * z;
        if (row.value == "{" + session.highlightKey + "}" || mentions(row.value, session.highlightKey) ||
            rowWritesKey(row, session.highlightKey))
            draw->AddRectFilled(
                {a.x + 2 * z, y - m.rowGap * z / 2}, {b.x - 2 * z, y + line + m.rowGap * z / 2},
                ImGui::GetColorU32(withAlpha(session.keyColor(card.key.ref.tree, session.highlightKey), .18f)),
                ui(2) * z);
        const ImVec4 badgeInk = row.special   ? palette().muted
                                : row.modeled ? directionColor(row.direction)
                                              : palette().error;
        text(draw, view.body, view.bodySize, {x, y}, ImGui::GetColorU32(withAlpha(badgeInk, alpha)),
             row.special ? "IF" : (!row.modeled ? "??" : directionBadge(row.direction)));
        text(draw, view.body, view.bodySize, {x + badgeWidth, y},
             ImGui::GetColorU32(withAlpha(row.missing ? withAlpha(palette().muted, .6f) : palette().muted, alpha)),
             row.port.c_str());
        const std::string &value = row.value;
        const float valueX = b.x - m.pad * z - drawnWidth(view, value);
        ImVec4 ink = palette().text;
        if (isKeyReference(value))
            ink = session.keyColor(card.key.ref.tree, keyOf(value));
        else if (row.missing)
            ink = withAlpha(palette().muted, .6f);
        text(draw, view.body, view.bodySize, {valueX, y}, ImGui::GetColorU32(withAlpha(ink, alpha)), value.c_str());
        y += line + m.rowGap * z;
    }

    // Outline: subtree calls dashed; cursor, selection, search hits, problems and the highlighted key.
    const ImU32 edge = ImGui::GetColorU32(withAlpha(band, .9f * alpha));
    if (card.floating) {
        // Dotted in the warning color, tagged: it can't stay here.
        dashedRect(draw, a, b, ImGui::GetColorU32(palette().warn), std::max(1.5f, ui(2) * z), ui(3) * z);
        text(draw, view.body, view.bodySize, {a.x, a.y - view.bodySize - ui(3) * z}, ImGui::GetColorU32(palette().warn),
             "floating");
    } else if (card.call)
        dashedRect(draw, a, b, edge, std::max(1.f, ui(1.5f) * z), ui(5) * z);
    else
        draw->AddRect(a, b, ImGui::GetColorU32(withAlpha(style.Colors[ImGuiCol_Border], alpha)), rounding, 0,
                      std::max(1.f, z));
    const auto ring = [&](ImVec4 color, float width, float grow) {
        draw->AddRect({a.x - grow, a.y - grow}, {b.x + grow, b.y + grow}, ImGui::GetColorU32(color), rounding + grow, 0,
                      width);
    };
    if ((severity == 0 || severity == 1) && !card.floating)
        ring(severityColor(severity), std::max(1.5f, ui(2) * z), ui(1) * z);
    if (keyed)
        ring(session.keyColor(card.key.ref.tree, session.highlightKey), std::max(1.5f, ui(2) * z), ui(3) * z);
    if (matched)
        ring(palette().change, std::max(1.5f, ui(2) * z), ui(3) * z);
    if (picked)
        ring(withAlpha(palette().accent, .7f), std::max(1.5f, ui(2) * z), ui(4) * z);
    if (cursor)
        ring(palette().accent, std::max(2.f, ui(3) * z), ui(5) * z);

    // Folded: how much is hidden. Opened call: a marker.
    if (card.folded) {
        char label[32];
        std::snprintf(label, sizeof(label), "+%d", card.hidden);
        const float w = (textWidth(m.body, label) + m.pad) * z, h = (m.body->FontSize + ui(4)) * z;
        const ImVec2 at = session.horizontal ? ImVec2(b.x + ui(6) * z, (a.y + b.y) / 2 - h / 2)
                                             : ImVec2((a.x + b.x) / 2 - w / 2, b.y + ui(6) * z);
        draw->AddRectFilled(at, {at.x + w, at.y + h}, ImGui::GetColorU32(style.Colors[ImGuiCol_Button]), h / 2);
        text(draw, view.body, view.bodySize, {at.x + m.pad * z / 2, at.y + ui(2) * z},
             ImGui::GetColorU32(palette().muted), label);
    }
}

// The round button under a SubTree call that opens it in place (beside it in the left-right layout).
bool caret(const Card &card, const View &view, bool horizontal, ImVec2 &center, float &radius) {
    if (!card.call)
        return false;
    const ImVec2 a = view.at(card.position),
                 b = view.at({card.position.x + card.size.x, card.position.y + card.size.y});
    radius = std::max(5.f, ui(7) * view.zoom);
    const float gap = ui(3) * view.zoom + radius;
    center = horizontal ? ImVec2(b.x + gap, (a.y + b.y) / 2) : ImVec2((a.x + b.x) / 2, b.y + gap);
    return true;
}

void drawCaret(const Card &card, const View &view, ImDrawList *draw, bool horizontal, bool hot) {
    ImVec2 c;
    float r = 0;
    if (!caret(card, view, horizontal, c, r))
        return;
    const auto &style = ImGui::GetStyle();
    const ImVec4 band = !card.color.empty() ? subtreeColor(card.color) : categoryColor(Category::SubTree);
    draw->AddCircleFilled(c, r, ImGui::GetColorU32(style.Colors[hot ? ImGuiCol_ButtonHovered : ImGuiCol_Button]));
    draw->AddCircle(c, r, ImGui::GetColorU32(withAlpha(band, .9f)), 0, std::max(1.f, ui(1.2f) * view.zoom));
    // A chevron pointing the way it unfolds (back when open).
    const float s = r * .45f;
    const float d = card.opened ? -1.f : 1.f;
    const ImVec2 p0 = horizontal ? ImVec2(c.x - s * .5f * d, c.y - s) : ImVec2(c.x - s, c.y - s * .5f * d);
    const ImVec2 p1 = horizontal ? ImVec2(c.x + s * .5f * d, c.y) : ImVec2(c.x, c.y + s * .5f * d);
    const ImVec2 p2 = horizontal ? ImVec2(c.x - s * .5f * d, c.y + s) : ImVec2(c.x + s, c.y - s * .5f * d);
    const ImVec2 chevron[] = {p0, p1, p2};
    draw->AddPolyline(chevron, 3, ImGui::GetColorU32(palette().text), 0, std::max(1.2f, ui(1.6f) * view.zoom));
}

// --- keyboard ---

int neighbor(const Scene &scene, int from, bool next, bool horizontal) {
    const auto &card = scene.cards[std::size_t(from)];
    if (card.parent >= 0) {
        const auto &siblings = scene.cards[std::size_t(card.parent)].children;
        const auto at = std::find(siblings.begin(), siblings.end(), from) - siblings.begin();
        const auto to = at + (next ? 1 : -1);
        if (to >= 0 && to < long(siblings.size()))
            return siblings[std::size_t(to)];
    }
    // No sibling that way: the nearest card on the same level.
    const auto breadth = [&](const Card &c) {
        return horizontal ? c.position.y + c.size.y / 2 : c.position.x + c.size.x / 2;
    };
    int best = -1;
    float distance = FLT_MAX;
    for (std::size_t i = 0; i < scene.cards.size(); ++i) {
        const auto &other = scene.cards[i];
        if (other.depth != card.depth || int(i) == from)
            continue;
        const float d = (breadth(other) - breadth(card)) * (next ? 1 : -1);
        if (d > 0 && d < distance) {
            distance = d;
            best = int(i);
        }
    }
    return best;
}

void navigate(Session &session, const Scene &scene, int current) {
    if (current < 0)
        return;
    const bool h = session.horizontal;
    const auto &card = scene.cards[std::size_t(current)];
    const ImGuiKey toParent = h ? ImGuiKey_LeftArrow : ImGuiKey_UpArrow;
    const ImGuiKey toChild = h ? ImGuiKey_RightArrow : ImGuiKey_DownArrow;
    const ImGuiKey toPrevious = h ? ImGuiKey_UpArrow : ImGuiKey_LeftArrow;
    const ImGuiKey toNext = h ? ImGuiKey_DownArrow : ImGuiKey_RightArrow;
    const auto &io = ImGui::GetIO();
    const bool plain = !io.KeyCtrl && !io.KeyAlt;
    // Ctrl moves the cursor and leaves the selection alone (Ctrl+Space then adds the node).
    const bool steer = plain || (io.KeyCtrl && !io.KeyAlt && !io.KeyShift);

    // Shift+Home / End: the selection runs to the first / last sibling.
    if (io.KeyShift && !io.KeyCtrl && !io.KeyAlt && card.parent >= 0 && !card.key.readOnly() &&
        (ImGui::IsKeyPressed(ImGuiKey_Home) || ImGui::IsKeyPressed(ImGuiKey_End))) {
        const auto &siblings = scene.cards[std::size_t(card.parent)].children;
        const auto at = std::find(siblings.begin(), siblings.end(), current) - siblings.begin();
        const bool home = ImGui::IsKeyPressed(ImGuiKey_Home);
        const long from = home ? 0 : at, to = home ? at : long(siblings.size()) - 1;
        for (long i = from; i <= to; ++i)
            if (const auto &ref = scene.cards[std::size_t(siblings[std::size_t(i)])].key.ref; !session.selected(ref))
                session.toggleSelected(ref);
        session.setCursor(scene.cards[std::size_t(siblings[std::size_t(home ? from : to)])].key, true);
        return;
    }

    int target = -1;
    static CardKey lastChild; // the card we left going up
    if (steer && ImGui::IsKeyPressed(toParent)) {
        target = card.parent;
        lastChild = card.key;
    } else if (steer && ImGui::IsKeyPressed(toChild) && !card.children.empty()) {
        // Back down to the child we came up from, if any.
        target = card.children.front();
        for (const int c : card.children)
            if (scene.cards[std::size_t(c)].key == lastChild)
                target = c;
        lastChild = {};
    } else if (steer && ImGui::IsKeyPressed(toPrevious))
        target = neighbor(scene, current, false, h);
    else if (steer && ImGui::IsKeyPressed(toNext))
        target = neighbor(scene, current, true, h);
    else if (plain && ImGui::IsKeyPressed(ImGuiKey_Home))
        target = 0;
    if (target >= 0) {
        const auto &to = scene.cards[std::size_t(target)];
        // Shift extends the selection along the way (editable nodes only).
        if (io.KeyShift && !to.key.readOnly() && !card.key.readOnly() && !to.root) {
            if (!card.root && !session.selected(card.key.ref))
                session.toggleSelected(card.key.ref);
            if (!session.selected(to.key.ref))
                session.toggleSelected(to.key.ref);
            session.setCursor(to.key, true);
        } else
            session.setCursor(to.key, io.KeyCtrl);
        return;
    }

    // Alt+arrows move the node: along its siblings, out of its parent, into the previous sibling.
    if (io.KeyAlt && !io.KeyCtrl && !card.key.readOnly() && !card.root) {
        if (ImGui::IsKeyPressed(toPrevious))
            session.run("node.earlier");
        else if (ImGui::IsKeyPressed(toNext))
            session.run("node.later");
        else if (ImGui::IsKeyPressed(toParent))
            session.run("node.outdent");
        else if (ImGui::IsKeyPressed(toChild))
            session.run("node.indent");
    }

    // Space folds a node, or opens a subtree call in place.
    if (plain && ImGui::IsKeyPressed(ImGuiKey_Space) && (card.call || !card.key.readOnly())) {
        auto &view = session.view(session.active);
        if (card.call)
            toggleOpened(session, card.key);
        else if (view.folded.count(card.key.ref.path))
            view.folded.erase(card.key.ref.path);
        else if (!card.children.empty())
            view.folded.insert(card.key.ref.path);
        session.revealPending = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        session.selection.clear();
        session.highlightKey.clear();
        session.showSearch = false;
    }
}

// --- search (Ctrl+F) ---

bool contains(const std::string &text, const std::string &query) {
    if (query.empty())
        return false;
    return std::search(text.begin(), text.end(), query.begin(), query.end(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           }) != text.end();
}

bool cardMatches(const Card &card, const std::string &query) {
    if (contains(card.title, query) || contains(card.subtitle, query))
        return true;
    for (const auto *attribute = card.element ? &card.element->attributes : nullptr; attribute;) {
        for (const auto &a : *attribute)
            if (contains(a.value, query) || contains(a.name, query))
                return true;
        break;
    }
    return false;
}

void searchBar(Session &session, const Scene &scene, int current) {
    ImGui::SetNextItemWidth(ui(320));
    if (ImGui::IsWindowAppearing() || session.showSearch) {
        static bool wasShown = false;
        if (!wasShown)
            ImGui::SetKeyboardFocusHere();
        wasShown = session.showSearch;
    }
    const bool enter = inputStringWithHint("##search", "Search this tree (Enter next, Shift+Enter previous, Esc close)",
                                           session.search, ImGuiInputTextFlags_EnterReturnsTrue);
    std::vector<int> hits;
    for (std::size_t i = 0; i < scene.cards.size(); ++i)
        if (cardMatches(scene.cards[i], session.search))
            hits.push_back(int(i));
    ImGui::SameLine();
    ImGui::TextDisabled("%zu found", hits.size());
    if (enter && !hits.empty()) {
        const bool back = ImGui::GetIO().KeyShift;
        int pick = back ? hits.back() : hits.front();
        for (std::size_t i = 0; i < hits.size(); ++i) {
            if (!back && hits[i] > current) {
                pick = hits[i];
                break;
            }
            if (back && hits[hits.size() - 1 - i] < current) {
                pick = hits[hits.size() - 1 - i];
                break;
            }
        }
        session.setCursor(scene.cards[std::size_t(pick)].key);
        ImGui::SetKeyboardFocusHere(-1);
    }
    if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        session.showSearch = false;
        session.focusCanvas = true;
    }
}

// The node menu: only what applies to the node; what can't run right now is grayed, with the reason.
void contextMenu(Session &session) {
    if (!ImGui::BeginPopup("##card"))
        return;
    bool any = false, separate = false;
    const auto group = [&](std::initializer_list<const char *> ids) {
        separate = any;
        for (const char *id : ids) {
            const auto *c = session.command(id);
            if (!c || !Session::applies(*c))
                continue;
            if (separate)
                ImGui::Separator();
            separate = false;
            any = true;
            const bool on = Session::enabled(*c);
            if (ImGui::MenuItem(c->label.c_str(), chordName(c->chord).c_str(), false, on))
                c->run();
            if (!on && c->why && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", c->why().c_str());
        }
    };
    if (session.cursor.ref.isTree() && !session.cursor.readOnly()) {
        group({"node.child", "edit.paste"});
        group({"tree.rename", "tree.ports"});
    } else {
        group({"node.child", "node.sibling", "node.wrap", "node.retype", "node.unwrap"});
        group({"node.definition", "tree.extract", "tree.ports", "node.makeGlobal"});
        group({"edit.cut", "edit.copy", "edit.paste", "edit.duplicate", "edit.delete"});
        group({"select.below", "select.sameType", "select.siblings"});
    }
    ImGui::EndPopup();
}

// --- mouse: dragging nodes ---

// Where dragged nodes would land: under `parent`, before its nodeIndex-th child.
struct Drop {
    int card = -1; // the card the mouse is on
    int edge = 0;  // -1 before it, 1 after it, 0 into it
    bool ok = false;
    NodeRef parent;
    std::size_t index = 0;
};

std::size_t nodeIndexIn(const Element &element) {
    const auto siblings = nodeChildren(*element.parent);
    return std::size_t(std::find(siblings.begin(), siblings.end(), &element) - siblings.begin());
}

Drop dropAt(Session &session, const Scene &scene, int under, ImVec2 mouse, ImVec2 a, ImVec2 b,
            const std::vector<NodeRef> &moving) {
    Drop drop;
    drop.card = under;
    if (under < 0)
        return drop;
    const auto &card = scene.cards[std::size_t(under)];
    if (card.key.readOnly())
        return drop;
    // The ends of a card along the siblings' axis put the nodes beside it; the middle, inside it.
    const float along = session.horizontal ? (mouse.y - a.y) / (b.y - a.y) : (mouse.x - a.x) / (b.x - a.x);
    const auto *element = resolve(session.workspace, card.key.ref);
    if (!element)
        return drop;
    const bool container = card.root || maxChildren(card.category) != 0;
    drop.edge = card.root ? 0 : along < .25f ? -1 : along > .75f ? 1 : container ? 0 : along < .5f ? -1 : 1;
    if (drop.edge == 0) {
        drop.parent = card.key.ref;
        drop.index = nodeChildren(*element).size();
    } else {
        drop.parent = card.key.ref.parent();
        drop.index = nodeIndexIn(*element) + (drop.edge > 0 ? 1 : 0);
    }
    // Not into themselves, and not past what the parent takes.
    const auto *parent = resolve(session.workspace, drop.parent);
    if (parent && parent->tag == "BehaviorTree")
        parent = treeBody(*parent); // a tree with globals holds its root in their wrapper
    int arriving = 0;
    for (const auto &ref : moving) {
        if (ref.tree != drop.parent.tree || ref == drop.parent || isAncestor(ref, drop.parent))
            return drop;
        const auto *node = resolve(session.workspace, ref);
        arriving += node && node->parent != parent;
    }
    const int most = capacity(session.workspace.catalog(), *parent);
    drop.ok = most < 0 || int(nodeChildren(*parent).size()) + arriving <= most;
    return drop;
}
} // namespace

void drawCanvas(Session &session) {
    // Tree tabs.
    if (ImGui::BeginTabBar("##trees", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll |
                                          ImGuiTabBarFlags_AutoSelectNewTabs)) {
        std::string closing;
        static std::string shown;
        for (const auto &key : session.tabs) {
            const auto *entry = session.workspace.tree(key);
            if (!entry) { // removed by an undo or a rename
                closing = key;
                continue;
            }
            bool open = true;
            const bool dirty = entry && entry->document->dirty();
            ImGuiTabItemFlags flags = dirty ? ImGuiTabItemFlags_UnsavedDocument : 0;
            if (key == session.active && shown != session.active)
                flags |= ImGuiTabItemFlags_SetSelected;
            if (ImGui::BeginTabItem(key.c_str(), &open, flags)) {
                if (session.active != key && shown == session.active)
                    session.openTree(key);
                ImGui::EndTabItem();
            }
            if (ImGui::IsItemHovered() && entry)
                ImGui::SetTooltip("%s", entry->document->path.string().c_str());
            if (!open)
                closing = key;
        }
        shown = session.active;
        ImGui::EndTabBar();
        if (!closing.empty())
            session.closeTree(closing);
    }

    if (!session.workspace.isOpen() || session.active.empty()) {
        emptyState(
            session.workspace.isOpen()
                ? "Open a tree from the explorer, or press Ctrl+P and type its name."
                : "Open a trees folder (Ctrl+O): riptide_autonomy/trees, or any folder with lib/node_models.xml. "
                  "notgroot edits the files in place.");
        session.canvasFocused = false;
        return;
    }

    auto &current = scene(session);
    auto &view = session.view(session.active);
    int cursorIndex = current.find(session.cursor);
    if (cursorIndex < 0 && !current.cards.empty()) {
        // The cursor's node is folded away or gone: the nearest visible ancestor.
        CardKey up = session.cursor;
        while (cursorIndex < 0 && !up.ref.path.empty()) {
            up.ref.path.pop_back();
            if (up.ref.path.empty() && !up.via.empty()) {
                auto via = up.via;
                const auto call = via.back();
                via.pop_back();
                up = {call, up.origin, via};
            } else if (up.ref.path.empty() && !up.origin.empty())
                up = {up.origin, {}};
            cursorIndex = current.find(up);
        }
        if (cursorIndex < 0)
            cursorIndex = 0;
        session.cursor = current.cards[std::size_t(cursorIndex)].key;
    }

    if (session.showSearch)
        searchBar(session, current, cursorIndex);

    // The canvas region: one invisible button for mouse input; the window's focus is the canvas's.
    const ImVec2 start = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
    if (size.x < 10 || size.y < 10)
        return;
    // Asked for until it holds over a whole frame: a window that appears later in a frame takes focus first.
    if (session.focusCanvas && !session.showSearch) {
        if (ImGui::IsWindowFocused() && session.canvasHeld)
            session.focusCanvas = false;
        else
            ImGui::SetWindowFocus();
    }
    ImGui::InvisibleButton("##canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle |
                               ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    session.canvasFocused = ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput;
    session.canvasHeld = session.canvasFocused;
    auto &io = ImGui::GetIO();

    // Fit or zoom. A fitted view refits as the canvas resizes (docking settles over the first frames).
    if (view.fitted && (std::abs(view.size.x - size.x) > 1 || std::abs(view.size.y - size.y) > 1))
        view.fit = true;
    view.size = size;
    view.zoom = nearestZoom(view.zoom);
    if (view.fit && current.extent.x > 0) {
        // The largest step that shows it whole, but never below a readable size: a wide tree then starts at its root.
        const float margin = ui(40);
        const float whole =
            std::min((size.x - 2 * margin) / current.extent.x, (size.y - 2 * margin) / current.extent.y);
        view.zoom = .7f;
        for (const float level : zoomLevels())
            if (level > view.zoom && level <= std::min(whole, 1.f))
                view.zoom = level;
        const auto &root = current.cards.front();
        view.pan = {(size.x - current.extent.x * view.zoom) / 2, margin};
        if (current.extent.x * view.zoom > size.x - 2 * margin)
            view.pan.x = size.x / 2 - (root.position.x + root.size.x / 2) * view.zoom;
        if (session.horizontal && current.extent.y * view.zoom > size.y - 2 * margin)
            view.pan.y = size.y / 2 - (root.position.y + root.size.y / 2) * view.zoom;
        view.fit = false;
        view.fitted = true;
    }
    if (hovered && (io.MouseWheel != 0 || io.MouseWheelH != 0))
        view.fitted = false;
    // Zoom moves a step per wheel notch (touchpads send fractions: they add up to one).
    static float wheel = 0;
    if (hovered && io.MouseWheel != 0 && io.KeyCtrl)
        wheel += io.MouseWheel;
    else if (!hovered || !io.KeyCtrl)
        wheel = 0;
    if (std::abs(wheel) >= 1) {
        const float before = view.zoom;
        view.zoom = stepZoom(view.zoom, wheel > 0 ? 1 : -1);
        wheel -= wheel > 0 ? 1 : -1;
        const ImVec2 mouse{io.MousePos.x - start.x, io.MousePos.y - start.y};
        view.pan.x = mouse.x - (mouse.x - view.pan.x) * view.zoom / before;
        view.pan.y = mouse.y - (mouse.y - view.pan.y) * view.zoom / before;
    } else if (hovered && !io.KeyCtrl && (io.MouseWheel != 0 || io.MouseWheelH != 0)) {
        const float step = ui(60);
        if (io.KeyShift)
            view.pan.x += io.MouseWheel * step;
        else {
            view.pan.y += io.MouseWheel * step;
            view.pan.x += io.MouseWheelH * step;
        }
    }
    if (session.canvasFocused && io.KeyCtrl && !io.KeyShift) {
        const auto zoomBy = [&](int steps) {
            view.fitted = false;
            const float before = view.zoom;
            view.zoom = stepZoom(view.zoom, steps);
            view.pan.x = size.x / 2 - (size.x / 2 - view.pan.x) * view.zoom / before;
            view.pan.y = size.y / 2 - (size.y / 2 - view.pan.y) * view.zoom / before;
        };
        if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))
            zoomBy(1);
        if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))
            zoomBy(-1);
    }

    const auto m = metrics();
    View v = makeView({start.x + view.pan.x, start.y + view.pan.y}, view.zoom, m);

    // Hit test.
    int under = -1;
    if (hovered)
        for (std::size_t i = current.cards.size(); i-- > 0;) {
            const auto &card = current.cards[i];
            const ImVec2 a = v.at(card.position),
                         b = v.at({card.position.x + card.size.x, card.position.y + card.size.y});
            if (io.MousePos.x >= a.x && io.MousePos.x < b.x && io.MousePos.y >= a.y && io.MousePos.y < b.y) {
                under = int(i);
                break;
            }
        }

    // The open-in-place button under a call is hit before the cards.
    int caretUnder = -1;
    if (hovered)
        for (std::size_t i = 0; i < current.cards.size(); ++i) {
            ImVec2 c;
            float r = 0;
            if (caret(current.cards[i], v, session.horizontal, c, r) &&
                std::hypot(io.MousePos.x - c.x, io.MousePos.y - c.y) <= r + 1) {
                caretUnder = int(i);
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SetTooltip(current.cards[i].opened ? "Close in place (Space)"
                                                          : "Open in place (Space); double-click the call for its tab");
            }
        }

    // Mouse: click selects (Ctrl or Shift adds), double-click edits or opens a subtree, dragging a node moves it,
    // Shift+drag on empty space selects a box, other drags there pan.
    static bool panning = false, boxing = false, dragging = false;
    static CardKey pressed;
    static ImVec2 pressAt;
    const bool left = ImGui::IsItemClicked(ImGuiMouseButton_Left), right = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    if (left && caretUnder >= 0) {
        const auto &card = current.cards[std::size_t(caretUnder)];
        toggleOpened(session, card.key);
        session.setCursor(card.key);
    } else if (left || right) {
        if (under >= 0) {
            const auto &card = current.cards[std::size_t(under)];
            if (left && (io.KeyCtrl || io.KeyShift) && !card.key.readOnly() && !card.root) {
                session.toggleSelected(card.key.ref);
                session.setCursor(card.key, true);
            } else
                session.setCursor(card.key, session.selected(card.key.ref)); // a selection stays, to drag or act on
            session.revealPending = false;
            if (left) {
                pressed = card.key;
                pressAt = io.MousePos;
            } else
                ImGui::OpenPopup("##card");
        } else if (left && io.KeyShift) {
            boxing = true;
            pressAt = io.MousePos;
        } else if (left)
            panning = true;
    }
    if (!pressed.ref.empty() && !dragging && !pressed.readOnly() && !pressed.ref.isTree() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left, ui(6)))
        dragging = true;
    const auto moving =
        session.selected(pressed.ref) ? outermost(session.selection) : std::vector<NodeRef>{pressed.ref};
    Drop drop;
    if (dragging && under >= 0) {
        const auto &card = current.cards[std::size_t(under)];
        drop = dropAt(session, current, under, io.MousePos, v.at(card.position),
                      v.at({card.position.x + card.size.x, card.position.y + card.size.y}), moving);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (dragging && drop.ok)
            session.edited(session.editor.relocate(moving, drop.parent, drop.index));
        else if (!dragging && !pressed.ref.empty() && !io.KeyCtrl && !io.KeyShift && session.selection.size() > 1 &&
                 session.selected(pressed.ref))
            session.setCursor(pressed); // a plain click in a selection picks that node alone
        if (boxing) {
            const ImVec2 lo{std::min(pressAt.x, io.MousePos.x), std::min(pressAt.y, io.MousePos.y)};
            const ImVec2 hi{std::max(pressAt.x, io.MousePos.x), std::max(pressAt.y, io.MousePos.y)};
            if (!io.KeyCtrl)
                session.selection.clear();
            for (const auto &card : current.cards) {
                const ImVec2 a = v.at(card.position),
                             b = v.at({card.position.x + card.size.x, card.position.y + card.size.y});
                if (card.root || card.key.readOnly() || b.x < lo.x || a.x > hi.x || b.y < lo.y || a.y > hi.y ||
                    session.selected(card.key.ref))
                    continue;
                session.selection.push_back(card.key.ref);
            }
            if (!session.selection.empty()) {
                session.setCursor(CardKey{session.selection.front(), {}}, true);
                session.revealPending = false;
            }
        }
        boxing = dragging = false;
        pressed = {};
    }
    if (hovered && under >= 0 && caretUnder < 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        const auto &card = current.cards[std::size_t(under)];
        if (card.call || card.key.readOnly())
            session.run("node.definition");
        else if (card.root)
            session.run("tree.rename");
        else
            session.run("node.edit");
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        panning = false;
    if ((panning && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)) ||
        (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0))) {
        view.pan.x += io.MouseDelta.x;
        view.pan.y += io.MouseDelta.y;
        view.fitted = false;
    }

    // Drop a model from the palette on a card: as its child, or after it.
    if (ImGui::BeginDragDropTarget()) {
        if (const auto *payload = ImGui::AcceptDragDropPayload("NOTGROOT_MODEL")) {
            const std::string id(static_cast<const char *>(payload->Data), std::size_t(payload->DataSize));
            NodeModel model;
            if (under >= 0 && session.model(id, model) && !current.cards[std::size_t(under)].key.readOnly()) {
                const auto &card = current.cards[std::size_t(under)];
                auto *element = resolve(session.workspace, card.key.ref);
                if (element && acceptsChild(session.workspace, *element))
                    session.edited(session.editor.insert(card.key.ref, nodeChildren(*element).size(), makeNode(model),
                                                         "Add " + id));
                else
                    session.edited(session.editor.insertAfter(card.key.ref, makeNode(model), "Add " + id));
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Keyboard.
    if (session.canvasFocused)
        navigate(session, current, cursorIndex);
    cursorIndex = current.find(session.cursor);

    // Keep the cursor in view after it moves (once the layout has caught up with a fold or an open).
    if (session.revealPending && cursorIndex >= 0 && signature(session) == current.signature) {
        const auto &card = current.cards[std::size_t(cursorIndex)];
        const float margin = ui(40);
        const ImVec2 a = v.at(card.position), b = v.at({card.position.x + card.size.x, card.position.y + card.size.y});
        if (a.x < start.x + margin)
            view.pan.x += start.x + margin - a.x;
        else if (b.x > start.x + size.x - margin)
            view.pan.x -= std::min(b.x - (start.x + size.x - margin), a.x - (start.x + margin));
        if (a.y < start.y + margin)
            view.pan.y += start.y + margin - a.y;
        else if (b.y > start.y + size.y - margin)
            view.pan.y -= std::min(b.y - (start.y + size.y - margin), a.y - (start.y + margin));
        v.origin = {start.x + view.pan.x, start.y + view.pan.y};
        session.revealPending = false;
        view.fitted = false;
    }

    // Draw: dotted grid, edges, cards (off-screen ones skipped).
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(start, {start.x + size.x, start.y + size.y}, true);
    const float grid = ui(24) * view.zoom;
    if (grid > 6) {
        const ImU32 dot = ImGui::GetColorU32(withAlpha(palette().muted, .18f));
        for (float x = std::fmod(view.pan.x, grid); x < size.x; x += grid)
            for (float y = std::fmod(view.pan.y, grid); y < size.y; y += grid)
                draw->AddRectFilled({start.x + x, start.y + y}, {start.x + x + 1.5f, start.y + y + 1.5f}, dot);
    }
    drawOpened(current, v, draw, session.horizontal);
    drawEdges(current, v, draw, session.horizontal);
    for (std::size_t i = 0; i < current.cards.size(); ++i) {
        const auto &card = current.cards[i];
        const ImVec2 a = v.at(card.position), b = v.at({card.position.x + card.size.x, card.position.y + card.size.y});
        if (b.x < start.x || a.x > start.x + size.x || b.y < start.y || a.y > start.y + size.y)
            continue;
        const int severity = card.key.readOnly() ? -1 : session.severityAt(card.key.ref);
        drawCard(session, card, v, draw, m, int(i) == cursorIndex,
                 !card.key.readOnly() && session.selected(card.key.ref),
                 session.showSearch && cardMatches(card, session.search), severity);
        drawCaret(card, v, draw, session.horizontal, int(i) == caretUnder);
    }

    // Box selection, and where dragged nodes would land.
    if (boxing) {
        draw->AddRectFilled(pressAt, io.MousePos, ImGui::GetColorU32(withAlpha(palette().accent, .12f)));
        draw->AddRect(pressAt, io.MousePos, ImGui::GetColorU32(withAlpha(palette().accent, .8f)), 0, 0, ui(1));
    }
    if (dragging) {
        if (drop.card >= 0) {
            const auto &card = current.cards[std::size_t(drop.card)];
            const ImVec2 a = v.at(card.position),
                         b = v.at({card.position.x + card.size.x, card.position.y + card.size.y});
            const ImU32 ink = ImGui::GetColorU32(drop.ok ? palette().accent : palette().error);
            const float width = std::max(2.f, ui(3) * v.zoom), gap = ui(9) * v.zoom;
            if (!drop.ok) {
                draw->AddRect({a.x - gap / 2, a.y - gap / 2}, {b.x + gap / 2, b.y + gap / 2}, ink, ui(5) * v.zoom, 0,
                              width);
                // A cross beside it: can't go here.
                const float r = ui(6) * v.zoom;
                const ImVec2 c{b.x + gap + r, a.y + r};
                draw->AddLine({c.x - r, c.y - r}, {c.x + r, c.y + r}, ink, width);
                draw->AddLine({c.x - r, c.y + r}, {c.x + r, c.y - r}, ink, width);
            } else if (drop.edge == 0)
                draw->AddRect({a.x - gap / 2, a.y - gap / 2}, {b.x + gap / 2, b.y + gap / 2}, ink, ui(5) * v.zoom, 0,
                              width);
            else if (session.horizontal) {
                const float y = drop.edge < 0 ? a.y - gap : b.y + gap;
                draw->AddLine({a.x, y}, {b.x, y}, ink, width);
            } else {
                const float x = drop.edge < 0 ? a.x - gap : b.x + gap;
                draw->AddLine({x, a.y}, {x, b.y}, ink, width);
            }
        }
        // The dragged node beside the pointer, with how many go along.
        const auto &pressedCard = current.cards[std::size_t(std::max(0, current.find(pressed)))];
        std::string label = pressedCard.title;
        if (moving.size() > 1)
            label += "  +" + std::to_string(moving.size() - 1);
        const ImVec2 at{io.MousePos.x + ui(14), io.MousePos.y + ui(10)};
        const ImVec2 extent = ImGui::CalcTextSize(label.c_str());
        draw->AddRectFilled(at, {at.x + extent.x + ui(16), at.y + extent.y + ui(8)},
                            ImGui::GetColorU32(withAlpha(categoryColor(pressedCard.category), .85f)), ui(4));
        draw->AddText({at.x + ui(8), at.y + ui(4)}, ImGui::GetColorU32(inkOnCategory(pressedCard.category)),
                      label.c_str());
        ImGui::SetMouseCursor(drop.ok ? ImGuiMouseCursor_ResizeAll : ImGuiMouseCursor_NotAllowed);
    }
    draw->PopClipRect();

    // Where the node picker opens: beside the cursor's card.
    if (cursorIndex >= 0) {
        const auto &card = current.cards[std::size_t(cursorIndex)];
        session.picker.anchor = v.at({card.position.x + card.size.x, card.position.y});
        session.picker.anchor.x += ui(12);
        session.picker.anchor.x = std::min(session.picker.anchor.x, start.x + size.x - ui(380));
        session.picker.anchor.y = std::clamp(session.picker.anchor.y, start.y, start.y + size.y - ui(300));
    } else
        session.picker.anchor = {start.x + ui(20), start.y + ui(20)};

    // Tooltip: the card's problems and comment.
    if (under >= 0 && !dragging && !ImGui::IsPopupOpen("##card")) {
        const auto &card = current.cards[std::size_t(under)];
        std::vector<const Problem *> here;
        if (!card.key.readOnly())
            for (const auto &problem : session.problems())
                if (problem.node == card.key.ref)
                    here.push_back(&problem);
        const auto *model = session.workspace.catalog().find(card.title);
        const bool described = model && !model->description.empty();
        // Only when there is something to say (an empty tooltip is just a box).
        if ((card.key.readOnly() || !card.comment.empty() || !here.empty() || described) && ImGui::BeginItemTooltip()) {
            if (card.key.readOnly())
                ImGui::TextDisabled("In %s (read-only here: Ctrl+Enter opens it)", card.key.ref.tree.c_str());
            if (!card.comment.empty() && card.element && card.element->parent)
                for (auto at = card.element->index(); at > 0 && card.element->parent->children[at - 1]->isComment();
                     --at)
                    ImGui::TextDisabled("<!--%s-->", card.element->parent->children[at - 1]->text.c_str());
            for (const auto *problem : here)
                ImGui::TextColored(severityColor(int(problem->severity)), "%s: %s", severityName(problem->severity),
                                   problem->message.c_str());
            if (described)
                ImGui::TextWrapped("%s", model->description.c_str());
            ImGui::EndTooltip();
        }
    }
    contextMenu(session);

    // Hint along the bottom edge for an empty tree.
    if (current.cards.size() == 1)
        draw->AddText({start.x + ui(16), start.y + size.y - ImGui::GetFontSize() - ui(12)},
                      ImGui::GetColorU32(palette().muted), "Empty tree: press Tab to add its root node");
}
} // namespace notgroot::ui
