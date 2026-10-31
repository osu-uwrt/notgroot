// Canvas: the active tree as cards in an automatic tidy layout, with keyboard navigation and editing.
#include "notgroot/core/layout.hpp"
#include "notgroot/ui/panels.hpp"
#include "notgroot/ui/style.hpp"
#include "notgroot/ui/theme.hpp"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <functional>
#include <imgui_internal.h>

namespace notgroot::ui {
using namespace core;

namespace {
struct Row {
    std::string port, value;
    PortDirection direction = PortDirection::Input;
    bool modelled = true; // a port of the node's model (else an attribute BT.CPP would reject)
    bool special = false; // _skipIf and friends
    bool missing = false; // a model port the node does not set
};

struct Card {
    CardKey key;
    const Element *element = nullptr;
    Category category = Category::Unknown;
    bool root = false, call = false, opened = false, folded = false;
    std::string title, subtitle, comment;
    std::string color; // a subtree's own color (#rrggbb), for its calls and its root card
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

void measure(Card &card, const Metrics &m) {
    float width = textWidth(m.title, card.title);
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
        if (entry && key.ref.tree == entry->id)
            card.color = treeColor(session.workspace, entry->id);
        if (entry && key.ref.tree == entry->id)
            for (const auto &port : session.workspace.subtreeModel(entry->id).ports)
                card.rows.push_back({port.name, "", port.direction, true, false, false});
    } else
        describe(session, card, element);
    auto &view = session.view(session.active);
    card.folded = own && view.folded.count(key.ref.path) && !element.elements().empty();
    card.opened = own && card.call && view.opened.count(key.ref.path);
    if (card.folded)
        card.hidden = countNodes(element);
    measure(card, m);
    const int index = int(scene.cards.size());
    scene.cards.push_back(card);
    if (parent >= 0)
        scene.cards[std::size_t(parent)].children.push_back(index);
    if (card.folded)
        return;

    // A subtree opened in place: its root's node under the call (read-only, one level).
    if (card.opened) {
        const auto id = element.get("ID");
        const auto *entry = session.workspace.tree(id);
        if (entry && std::find(stack.begin(), stack.end(), id) == stack.end()) {
            stack.push_back(id);
            for (std::size_t i = 0; i < entry->tree->children.size(); ++i)
                if (!entry->tree->children[i]->isComment())
                    build(session, scene, *entry->tree->children[i], {{id, {i}}, key.ref}, index, depth + 1, stack, m);
            stack.pop_back();
        }
        return;
    }
    for (std::size_t i = 0; i < element.children.size(); ++i)
        if (!element.children[i]->isComment())
            build(session, scene, *element.children[i], {key.ref.child(i), key.origin}, index, depth + 1, stack, m);
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
    options.levelGap = ui(session.horizontal ? 56 : 44);
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

void dashedRect(ImDrawList *draw, ImVec2 a, ImVec2 b, ImU32 color, float thickness, float dash) {
    const auto line = [&](ImVec2 p, ImVec2 q) {
        const float length = std::hypot(q.x - p.x, q.y - p.y);
        for (float t = 0; t < length; t += 2 * dash) {
            const float e = std::min(t + dash, length);
            draw->AddLine({p.x + (q.x - p.x) * t / length, p.y + (q.y - p.y) * t / length},
                          {p.x + (q.x - p.x) * e / length, p.y + (q.y - p.y) * e / length}, color, thickness);
        }
    };
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
                const float mid = (from.x + to.x) / 2;
                const ImVec2 points[] = {from, {mid, from.y}, {mid, to.y}, to};
                draw->AddPolyline(points, 4, ink, 0, thickness);
            } else {
                const ImVec2 from{(p0.x + p1.x) / 2, p1.y}, to{(c0.x + c1.x) / 2, c0.y};
                const float mid = (from.y + to.y) / 2;
                const ImVec2 points[] = {from, {from.x, mid}, {to.x, mid}, to};
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
    const float alpha = card.key.readOnly() ? .78f : 1.f;
    const ImVec4 body = style.Colors[ImGuiCol_FrameBg];
    const ImVec4 band = !card.color.empty() ? subtreeColor(card.color)
                        : card.root         ? rootColor()
                                            : categoryColor(card.category);
    const ImVec4 bandInk = inkOn(band, {1, 1, 1, 1}, 3.f);
    const bool keyed = std::any_of(card.rows.begin(), card.rows.end(), [&](const Row &row) {
        return row.value == "{" + session.highlightKey + "}" || mentions(row.value, session.highlightKey) ||
               rowWritesKey(row, session.highlightKey);
    });

    // Shadow, body, header band.
    draw->AddRectFilled({a.x + 2 * z, a.y + 3 * z}, {b.x + 2 * z, b.y + 3 * z},
                        ImGui::GetColorU32(ImVec4(0, 0, 0, darkTheme() ? .35f : .12f)), rounding);
    draw->AddRectFilled(a, b, ImGui::GetColorU32(withAlpha(body, alpha)), rounding);
    const float header = (m.header + (card.subtitle.empty() ? 0 : m.body->FontSize + m.rowGap)) * z;
    draw->AddRectFilled(a, {b.x, a.y + header}, ImGui::GetColorU32(withAlpha(band, alpha)), rounding,
                        card.rows.empty() ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersTop);

    // Title, name, comment marker.
    float y = a.y + ui(5) * z;
    const float x = a.x + m.pad * z;
    text(draw, view.title, view.titleSize, {x, y}, ImGui::GetColorU32(bandInk), card.title.c_str());
    if (!card.comment.empty())
        text(draw, view.body, view.bodySize,
             {b.x - m.pad * z - drawnWidth(view, "//"), y + (m.title->FontSize - m.body->FontSize) * z},
             ImGui::GetColorU32(withAlpha(bandInk, .75f)), "//");
    y += m.title->FontSize * z;
    if (!card.subtitle.empty()) {
        y += m.rowGap * z;
        text(draw, view.body, view.bodySize, {x, y}, ImGui::GetColorU32(withAlpha(bandInk, .85f)),
             card.subtitle.c_str());
    }

    // Port rows: badge, name, value (a key in its own color).
    y = a.y + header + m.pad * z;
    const float badgeWidth = (textWidth(m.body, "OUT") + m.pad) * z;
    for (const auto &row : card.rows) {
        const float line = m.body->FontSize * z;
        if (row.value == "{" + session.highlightKey + "}" || mentions(row.value, session.highlightKey) ||
            rowWritesKey(row, session.highlightKey))
            draw->AddRectFilled({a.x + 2 * z, y - m.rowGap * z / 2}, {b.x - 2 * z, y + line + m.rowGap * z / 2},
                                ImGui::GetColorU32(withAlpha(keyColor(session.highlightKey), .18f)), ui(2) * z);
        const ImVec4 badgeInk = row.special    ? palette().muted
                                : row.modelled ? directionColor(row.direction)
                                               : palette().error;
        text(draw, view.body, view.bodySize, {x, y}, ImGui::GetColorU32(withAlpha(badgeInk, alpha)),
             row.special ? "IF" : (!row.modelled ? "??" : directionBadge(row.direction)));
        text(draw, view.body, view.bodySize, {x + badgeWidth, y},
             ImGui::GetColorU32(withAlpha(row.missing ? withAlpha(palette().muted, .6f) : palette().muted, alpha)),
             row.port.c_str());
        const std::string &value = row.value;
        const float valueX = b.x - m.pad * z - drawnWidth(view, value);
        ImVec4 ink = palette().text;
        if (isKeyReference(value))
            ink = keyColor(keyOf(value));
        else if (row.missing)
            ink = withAlpha(palette().muted, .6f);
        text(draw, view.body, view.bodySize, {valueX, y}, ImGui::GetColorU32(withAlpha(ink, alpha)), value.c_str());
        y += line + m.rowGap * z;
    }

    // Outline: subtree calls dashed; cursor, selection, search hits, problems and the highlighted key.
    const ImU32 edge = ImGui::GetColorU32(withAlpha(band, .9f * alpha));
    if (card.call)
        dashedRect(draw, a, b, edge, std::max(1.f, ui(1.5f) * z), ui(5) * z);
    else
        draw->AddRect(a, b, ImGui::GetColorU32(withAlpha(style.Colors[ImGuiCol_Border], alpha)), rounding, 0,
                      std::max(1.f, z));
    const auto ring = [&](ImVec4 color, float width, float grow) {
        draw->AddRect({a.x - grow, a.y - grow}, {b.x + grow, b.y + grow}, ImGui::GetColorU32(color), rounding + grow, 0,
                      width);
    };
    if (severity == 0 || severity == 1)
        ring(severityColor(severity), std::max(1.5f, ui(2) * z), ui(1) * z);
    if (keyed)
        ring(keyColor(session.highlightKey), std::max(1.5f, ui(2) * z), ui(3) * z);
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
        text(draw, view.body, view.bodySize, {at.x + m.pad * z / 2, at.y + ui(2) * z}, ImGui::GetColorU32(palette().muted),
             label);
    }
}

// --- keyboard ---

int neighbour(const Scene &scene, int from, bool next, bool horizontal) {
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

    int target = -1;
    if (plain && ImGui::IsKeyPressed(toParent))
        target = card.parent;
    else if (plain && ImGui::IsKeyPressed(toChild) && !card.children.empty()) {
        // Back down to the child we came up from, if any.
        static CardKey lastChild;
        target = card.children.front();
        for (const int c : card.children)
            if (scene.cards[std::size_t(c)].key == lastChild)
                target = c;
        lastChild = {};
    } else if (plain && ImGui::IsKeyPressed(toPrevious))
        target = neighbour(scene, current, false, h);
    else if (plain && ImGui::IsKeyPressed(toNext))
        target = neighbour(scene, current, true, h);
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
            session.setCursor(to.key);
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
    if (plain && ImGui::IsKeyPressed(ImGuiKey_Space) && !card.key.readOnly()) {
        auto &view = session.view(session.active);
        auto &set = card.call ? view.opened : view.folded;
        if (set.count(card.key.ref.path))
            set.erase(card.key.ref.path);
        else if (card.call || !card.children.empty())
            set.insert(card.key.ref.path);
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

void contextMenu(Session &session) {
    if (!ImGui::BeginPopup("##card"))
        return;
    const auto item = [&](const char *id) {
        const auto *c = session.command(id);
        if (c && ImGui::MenuItem(c->label.c_str(), chordName(c->chord).c_str(), false, !c->enabled || c->enabled()))
            c->run();
    };
    for (const char *id : {"node.child", "node.sibling", "node.wrap", "node.retype", "node.unwrap"})
        item(id);
    ImGui::Separator();
    for (const char *id : {"node.definition", "tree.extract", "tree.ports"})
        item(id);
    ImGui::Separator();
    for (const char *id : {"edit.cut", "edit.copy", "edit.paste", "edit.duplicate", "edit.delete"})
        item(id);
    ImGui::EndPopup();
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
            if (up.ref.path.empty() && !up.origin.empty())
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

    // Mouse: click selects (Ctrl adds), double-click edits or opens a subtree, drag on empty space pans.
    static bool panning = false;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        if (under >= 0) {
            const auto &card = current.cards[std::size_t(under)];
            if (io.KeyCtrl && !card.key.readOnly() && !card.root) {
                session.toggleSelected(card.key.ref);
                session.setCursor(card.key, true);
            } else if (!(ImGui::IsItemClicked(ImGuiMouseButton_Right) && session.selected(card.key.ref)))
                session.setCursor(card.key);
            session.revealPending = false;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("##card");
        } else if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            panning = true;
    }
    if (hovered && under >= 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
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

    // Keep the cursor in view after it moves.
    if (session.revealPending && cursorIndex >= 0) {
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
    if (under >= 0 && !ImGui::IsPopupOpen("##card")) {
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
