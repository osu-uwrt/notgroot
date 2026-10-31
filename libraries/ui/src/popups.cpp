// Finder (Ctrl+P), node picker, dialogs and shortcut dispatch.
#include "notgroot/core/search.hpp"
#include "notgroot/ui/panels.hpp"
#include "notgroot/ui/style.hpp"
#include "notgroot/ui/theme.hpp"
#include <algorithm>
#include <functional>
#include <imgui_internal.h>

namespace notgroot::ui {
using namespace core;

namespace {
// A list with a query box: type to filter, ↑↓ to choose, Enter to take, Esc to close. Returns the index taken.
struct Choice {
    std::string label, detail, hint;
    ImVec4 color{0, 0, 0, 0}; // chip color (alpha 0: none)
    int bonus = 0;            // ranks ahead of plain matches
};

// `prefixed`: a leading > @ $ or # in the query picks the kind and is not matched. The field keeps the whole text
// (an active ImGui input owns its buffer, so the caller can't strip it).
int chooser(const char *id, std::string &query, const std::vector<Choice> &choices, int &selected, bool focus,
            bool &close, const char *placeholder, bool prefixed = false) {
    if (focus)
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-FLT_MIN);
    inputStringWithHint(id, placeholder, query);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        close = true;

    // Rank: fuzzy score on the label, then the bonus.
    const bool kind = prefixed && !query.empty() && std::string(">@$#").find(query[0]) != std::string::npos;
    const std::string_view text = std::string_view(query).substr(kind ? 1 : 0);
    struct Ranked {
        int score;
        std::size_t index;
        std::vector<int> positions;
    };
    std::vector<Ranked> ranked;
    for (std::size_t i = 0; i < choices.size(); ++i) {
        auto match = fuzzyMatch(text, choices[i].label);
        if (match.matched)
            ranked.push_back({match.score + choices[i].bonus, i, std::move(match.positions)});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](auto &a, auto &b) { return a.score > b.score; });
    if (ranked.size() > 300)
        ranked.resize(300);
    const int count = int(ranked.size());
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && count)
        selected = (selected + 1) % count;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && count)
        selected = (selected + count - 1) % count;
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown) && count)
        selected = std::min(count - 1, selected + 10);
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp) && count)
        selected = std::max(0, selected - 10);
    selected = std::clamp(selected, 0, std::max(0, count - 1));
    int taken = -1;
    if ((ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) && count)
        taken = int(ranked[std::size_t(selected)].index);

    ImGui::BeginChild("##choices", {0, ui(360)}, ImGuiChildFlags_AutoResizeY);
    for (int i = 0; i < count; ++i) {
        const auto &choice = choices[ranked[std::size_t(i)].index];
        ImGui::PushID(i);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##row", i == selected, 0, {0, ImGui::GetTextLineHeight()}))
            taken = int(ranked[std::size_t(i)].index);
        if (i == selected &&
            ImGui::IsKeyPressed(ImGuiKey_DownArrow, true) | ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
            ImGui::SetScrollHereY();
        auto *draw = ImGui::GetWindowDrawList();
        float x = at.x;
        const float line = ImGui::GetTextLineHeight();
        if (choice.color.w > 0) {
            draw->AddRectFilled({x, at.y + line * .2f}, {x + line * .45f, at.y + line * .8f},
                                ImGui::GetColorU32(choice.color), ui(2));
            x += line * .8f;
        }
        const auto &hits = ranked[std::size_t(i)].positions;
        for (std::size_t c = 0; c < choice.label.size(); ++c) {
            const char piece[2] = {choice.label[c], 0};
            const bool hit = std::find(hits.begin(), hits.end(), int(c)) != hits.end();
            draw->AddText({x, at.y}, ImGui::GetColorU32(hit ? palette().accent : palette().text), piece);
            x += ImGui::CalcTextSize(piece).x;
        }
        if (!choice.detail.empty())
            draw->AddText({x + ui(12), at.y}, ImGui::GetColorU32(palette().muted), choice.detail.c_str());
        if (!choice.hint.empty()) {
            const float w = ImGui::CalcTextSize(choice.hint.c_str()).x;
            draw->AddText({at.x + ImGui::GetContentRegionAvail().x - w, at.y}, ImGui::GetColorU32(palette().muted),
                          choice.hint.c_str());
        }
        ImGui::PopID();
    }
    if (!count)
        ImGui::TextDisabled("Nothing matches");
    ImGui::EndChild();
    return taken;
}

// What the finder can go to, rebuilt when the workspace changes.
struct Target {
    enum Kind { Tree, Node, Key, Command } kind;
    Choice choice;
    std::string tree, key, command;
    NodeRef node;
};

void collectNodes(Session &session, const std::string &tree, const Element &element, const NodeRef &ref,
                  std::vector<Target> &out) {
    for (std::size_t i = 0; i < element.children.size(); ++i) {
        const auto &child = element.children[i];
        if (child->isComment() || (child->tag == "Script" && isGlobalsPart(*child)))
            continue;
        const auto childRef = ref.child(i);
        if (isGlobalsPart(*child)) {
            collectNodes(session, tree, *child, childRef, out);
            continue;
        }
        const bool call = isSubTree(*child);
        std::string label = call ? "SubTree " + child->get("ID") : nodeId(*child);
        if (child->has("name"))
            label += " \"" + child->get("name") + "\"";
        // A few port values help find the node you mean ("Info MISSION: RESCUE").
        std::string values;
        for (const auto &attribute : child->attributes)
            if (attribute.name != "ID" && attribute.name != "name" && values.size() < 60)
                values += " " + attribute.value;
        Target target{
            Target::Node,
            {label + values, "in " + tree, "", categoryColor(nodeCategory(*child, session.workspace.catalog())), 0},
            tree,
            "",
            "",
            childRef};
        out.push_back(std::move(target));
        collectNodes(session, tree, *child, childRef, out);
    }
}
} // namespace

void drawFinder(Session &session) {
    static bool wasOpen = false;
    static std::string query;
    static int selected = 0;
    static std::vector<Target> all;
    static unsigned builtFor = ~0u;
    if (!session.showFinder) {
        wasOpen = false;
        return;
    }
    const bool opening = !wasOpen;
    wasOpen = true;
    if (opening) {
        query = session.finderPrefix;
        selected = 0;
    }
    if (builtFor != session.workspace.version() || opening) {
        builtFor = session.workspace.version();
        all.clear();
        for (const auto &key : session.workspace.missions())
            all.push_back({Target::Tree, {key, "mission", "", rootColor(), 40}, key, "", "", {}});
        for (const auto &key : session.workspace.subtrees())
            all.push_back({Target::Tree, {key, "subtree", "", categoryColor(Category::SubTree), 40}, key, "", "", {}});
        for (const auto &[key, entry] : session.workspace.trees())
            collectNodes(session, key, *entry.tree, {key, {}}, all);
        if (!session.active.empty())
            for (const auto &[name, info] : session.variables(session.active).keys)
                all.push_back({Target::Key,
                               {"{" + name + "}",
                                std::to_string(info.reads) + " reads, " + std::to_string(info.writes) + " writes", "",
                                keyColor(name), 20},
                               session.active,
                               name,
                               "",
                               {}});
        for (const auto &command : session.commands)
            if (command.id != "edit.redo2")
                all.push_back({Target::Command,
                               {command.label, command.group, chordName(command.chord), {0, 0, 0, 0}, 0},
                               "",
                               "",
                               command.id,
                               {}});
    }

    // The prefix picks the kind: > commands, @ nodes here, $ keys here, # trees; none: trees, nodes, keys.
    char prefix = query.empty() ? 0 : query[0];
    if (prefix != '>' && prefix != '@' && prefix != '$' && prefix != '#')
        prefix = 0;
    std::vector<Choice> choices;
    std::vector<const Target *> targets;
    for (const auto &target : all) {
        bool keep = false;
        switch (prefix) {
        case '>':
            keep = target.kind == Target::Command;
            break;
        case '@':
            keep = target.kind == Target::Node && target.tree == session.active;
            break;
        case '$':
            keep = target.kind == Target::Key;
            break;
        case '#':
            keep = target.kind == Target::Tree;
            break;
        default:
            keep = target.kind != Target::Command;
        }
        if (!keep)
            continue;
        if (target.kind == Target::Command)
            if (const auto *c = session.command(target.command); c && (!Session::enabled(*c) || !Session::applies(*c)))
                continue;
        choices.push_back(target.choice);
        targets.push_back(&target);
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = std::min(ui(720), display.x - ui(40));
    ImGui::SetNextWindowPos({(display.x - width) / 2, ui(60)});
    ImGui::SetNextWindowSize({width, 0});
    bool close = false;
    if (ImGui::Begin("##finder", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_AlwaysAutoResize)) {
        if (opening)
            ImGui::SetWindowFocus();
        const std::string before = query;
        const int taken = chooser("##query", query, choices, selected, opening, close,
                                  prefix == '>' ? "Run a command"
                                                : "Trees, nodes, {keys} — > commands  @ nodes here  "
                                                  "$ keys here  # trees",
                                  true);
        if (query != before)
            selected = 0;
        if (taken >= 0) {
            const auto &target = *targets[std::size_t(taken)];
            close = true;
            switch (target.kind) {
            case Target::Tree:
                session.openTree(target.tree);
                break;
            case Target::Node:
                session.reveal(target.node);
                break;
            case Target::Key: {
                session.highlightKey = target.key;
                const auto &info = session.variables(target.tree).keys;
                if (const auto found = info.find(target.key); found != info.end() && !found->second.uses.empty())
                    session.reveal(found->second.uses.front().node);
                break;
            }
            case Target::Command:
                session.showFinder = false;
                session.focusCanvas = true;
                session.run(target.command);
                break;
            }
        }
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !opening)
            close = true;
    }
    ImGui::End();
    if (close) {
        session.showFinder = false;
        if (!session.showSearch && session.dialog.kind == Session::Dialog::None)
            session.focusCanvas = true;
    }
}

void drawPicker(Session &session) {
    static bool wasOpen = false;
    static std::string query;
    static int selected = 0;
    auto &picker = session.picker;
    if (!picker.open) {
        wasOpen = false;
        return;
    }
    const bool opening = !wasOpen;
    wasOpen = true;
    if (opening) {
        query.clear();
        selected = 0;
    }

    // Which models fit: wrapping takes controls and decorators; everything else, any node.
    std::vector<NodeModel> models;
    for (const auto *model : session.workspace.catalog().all()) {
        if (picker.purpose == PickFor::Wrap && model->category != Category::Control &&
            model->category != Category::Decorator)
            continue;
        if (model->category == Category::SubTree && picker.target.tree == model->id)
            continue; // a subtree inside itself
        models.push_back(*model);
    }
    std::vector<Choice> choices;
    for (const auto &model : models) {
        std::string ports;
        for (const auto &port : model.ports)
            if (ports.size() < 50)
                ports += (ports.empty() ? "" : " ") + port.name;
        choices.push_back(
            {model.id, ports, categoryName(model.category), categoryColor(model.category), model.builtin ? 0 : 2});
    }

    const char *titles[] = {"Add child", "Add sibling after", "Wrap in", "Change to"};
    ImGui::SetNextWindowPos(picker.anchor, opening ? ImGuiCond_Always : ImGuiCond_Appearing);
    ImGui::SetNextWindowSize({ui(440), 0});
    bool close = false;
    if (ImGui::Begin("##picker", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
        if (opening)
            ImGui::SetWindowFocus();
        ImGui::TextColored(palette().accent, "%s", titles[int(picker.purpose)]);
        const std::string before = query;
        const int taken = chooser("##pick", query, choices, selected, opening, close, "Node or subtree");
        if (query != before)
            selected = 0;
        if (taken >= 0) {
            const auto &model = models[std::size_t(taken)];
            close = true;
            NodeRef made;
            switch (picker.purpose) {
            case PickFor::Child: {
                auto *element = resolve(session.workspace, picker.target);
                if (element && acceptsChild(session.workspace, *element))
                    made = session.editor.insert(picker.target, nodeChildren(*element).size(), makeNode(model),
                                                 "Add " + model.id);
                else if (element && !picker.target.isTree()) {
                    made = session.editor.insertAfter(picker.target, makeNode(model), "Add " + model.id);
                    if (!made.empty())
                        session.status(nodeId(*element) + " takes no more children: added after it");
                } else
                    session.status("The tree already has its root node: add under it", true);
                break;
            }
            case PickFor::Sibling:
                made = session.editor.insertAfter(picker.target, makeNode(model), "Add " + model.id);
                break;
            case PickFor::Wrap:
                made = session.editor.wrap(session.targets(), model);
                break;
            case PickFor::Retype:
                made = session.editor.retype(picker.target, model);
                break;
            }
            session.edited(made);
            // A new node with inputs: straight to its first one.
            if (!made.empty() && picker.purpose != PickFor::Wrap && picker.purpose != PickFor::Retype &&
                std::any_of(model.ports.begin(), model.ports.end(),
                            [](auto &p) { return p.direction != PortDirection::Output; })) {
                session.focusInspector = true;
                session.focusCanvas = false;
            }
        }
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !opening)
            close = true;
    }
    ImGui::End();
    if (close) {
        picker.open = false;
        if (!session.focusInspector)
            session.focusCanvas = true;
    }
}

namespace {
// One line of text and OK / Cancel; returns true on OK (Enter).
bool nameDialog(Session &session, const char *prompt, bool &close) {
    auto &dialog = session.dialog;
    ImGui::TextUnformatted(prompt);
    if (dialog.fresh)
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(ui(360));
    const bool enter =
        inputString("##name", dialog.text, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    dialog.fresh = false;
    bool ok = enter;
    if (ImGui::Button("OK"))
        ok = true;
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        close = true;
    return ok;
}

struct PortRow {
    PortModel port;
    std::string original; // name when the dialog opened ("" for new rows)
};

void portsDialog(Session &session, bool &close) {
    static std::vector<PortRow> rows;
    static std::string forId;
    auto &dialog = session.dialog;
    if (dialog.fresh || forId != dialog.target) {
        rows.clear();
        for (const auto &port : session.workspace.subtreeModel(dialog.target).ports)
            rows.push_back({port, port.name});
        forId = dialog.target;
        dialog.fresh = false;
    }
    ImGui::TextDisabled("Ports of %s, kept in lib/node_models.xml. Renaming a port renames it at every call and "
                        "inside the subtree.",
                        dialog.target.c_str());
    if (ImGui::BeginTable("##portsEdit", 5, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, ui(170));
        ImGui::TableSetupColumn("Direction", ImGuiTableColumnFlags_WidthFixed, ui(110));
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthFixed, ui(300));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        tableHeaders();
        int remove = -1, up = -1;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            auto &row = rows[i];
            ImGui::PushID(int(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            inputString("##n", row.port.name);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            const char *directions[] = {"input", "output", "in-out"};
            int direction = int(row.port.direction);
            if (ImGui::Combo("##d", &direction, directions, 3))
                row.port.direction = PortDirection(direction);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            inputString("##t", row.port.description);
            ImGui::TableNextColumn();
            if (i > 0 && ImGui::SmallButton("↑"))
                up = int(i);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("×"))
                remove = int(i);
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (remove >= 0)
            rows.erase(rows.begin() + remove);
        if (up > 0)
            std::swap(rows[std::size_t(up)], rows[std::size_t(up - 1)]);
    }
    if (ImGui::Button("+ Port"))
        rows.push_back({{"port" + std::to_string(rows.size() + 1), "", "", "", PortDirection::Input, false}, ""});
    ImGui::Separator();
    if (ImGui::Button("Apply")) {
        std::vector<PortModel> ports;
        std::map<std::string, std::string> renamed;
        for (const auto &row : rows) {
            ports.push_back(row.port);
            if (!row.original.empty() && row.original != row.port.name)
                renamed[row.original] = row.port.name;
        }
        if (session.editor.setSubtreePorts(dialog.target, ports, renamed)) {
            session.status("Ports of " + dialog.target + " updated");
            close = true;
        } else
            session.status(session.editor.error(), true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        close = true;
}
} // namespace

void drawDialogs(Session &session) {
    auto &dialog = session.dialog;
    const char *titles[] = {"",
                            "New subtree",
                            "New mission",
                            "Rename tree",
                            "Rename variable",
                            "Extract to subtree",
                            "Subtree ports",
                            "Unsaved changes",
                            "Can't save: floating nodes"};
    if (dialog.kind != Session::Dialog::None && !ImGui::IsPopupOpen(titles[dialog.kind]))
        ImGui::OpenPopup(titles[dialog.kind]);

    for (int kind = 1; kind <= Session::Dialog::Floating; ++kind) {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {.5f, .5f});
        if (!ImGui::BeginPopupModal(titles[kind], nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            continue;
        bool close = false;
        switch (Session::Dialog::Kind(kind)) {
        case Session::Dialog::NewSubtree:
            if (nameDialog(session, "Subtree ID (its file is lib/<ID>.xml):", close)) {
                const auto made = session.editor.createSubtree(dialog.text);
                if (!made.empty()) {
                    session.openTree(made.tree);
                    session.status("New subtree " + made.tree + ": press Tab to add its root node");
                    close = true;
                } else
                    session.status(session.editor.error(), true);
            }
            break;
        case Session::Dialog::NewMission:
            if (nameDialog(session, "Mission name (its file is <name>.xml):", close)) {
                const auto made = session.editor.createMission(dialog.text);
                if (!made.empty()) {
                    session.openTree(made.tree);
                    close = true;
                } else
                    session.status(session.editor.error(), true);
            }
            break;
        case Session::Dialog::RenameTree:
            if (nameDialog(session, ("New name for " + dialog.target + ":").c_str(), close)) {
                const auto *entry = session.workspace.tree(dialog.target);
                const bool mission = entry && entry->id != dialog.target;
                if (session.editor.renameTree(dialog.target, dialog.text)) {
                    std::string key = dialog.text;
                    if (mission && (key.size() < 4 || key.substr(key.size() - 4) != ".xml"))
                        key += ".xml";
                    std::replace(session.tabs.begin(), session.tabs.end(), dialog.target, key);
                    if (session.active == dialog.target)
                        session.active.clear();
                    session.openTree(key);
                    session.status("Renamed " + dialog.target + " to " + key);
                    close = true;
                } else
                    session.status(session.editor.error(), true);
            }
            break;
        case Session::Dialog::RenameKey: {
            static std::string from;
            if (dialog.fresh)
                from = dialog.text;
            if (nameDialog(session, ("Rename {" + from + "} everywhere in " + dialog.target + " to:").c_str(), close)) {
                const int changed = session.editor.renameKey(dialog.target, from, dialog.text);
                if (changed > 0) {
                    session.highlightKey = dialog.text;
                    session.status("Renamed {" + from + "} in " + std::to_string(changed) + " places");
                    close = true;
                } else
                    session.status(session.editor.error().empty() ? "Nothing to rename" : session.editor.error(), true);
            }
            break;
        }
        case Session::Dialog::ExtractSubtree:
            if (nameDialog(session, "New subtree ID for the selected nodes:", close)) {
                const auto made = session.editor.extractSubtree(session.targets(), dialog.text);
                if (!made.empty()) {
                    session.setCursor(made);
                    session.status("Extracted " + dialog.text + "; its ports come from the keys the nodes use");
                    close = true;
                } else
                    session.status(session.editor.error(), true);
            }
            break;
        case Session::Dialog::EditPorts:
            portsDialog(session, close);
            break;
        case Session::Dialog::Unsaved: {
            ImGui::TextUnformatted("Some trees have unsaved changes.");
            for (const auto &document : session.workspace.documents())
                if (document->dirty())
                    ImGui::BulletText("%s", document->path.filename().string().c_str());
            if (ImGui::Button("Save")) {
                session.save();
                if (!session.dirty() && session.afterUnsaved)
                    session.afterUnsaved();
                close = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard")) {
                if (session.afterUnsaved)
                    session.afterUnsaved();
                close = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
                close = true;
            break;
        }
        case Session::Dialog::Floating: {
            ImGui::TextUnformatted("Nothing was saved. These trees have nodes where their parent can't hold them;");
            ImGui::TextUnformatted("move each on (Alt+arrows) and save again.");
            ImGui::Spacing();
            std::vector<std::string> trees;
            for (const auto &ref : session.floating)
                if (std::find(trees.begin(), trees.end(), ref.tree) == trees.end())
                    trees.push_back(ref.tree);
            for (const auto &tree : trees) {
                int count = 0;
                for (const auto &ref : session.floating)
                    count += ref.tree == tree;
                // A link: opens the tree with the cursor on its first floating node.
                ImGui::PushStyleColor(ImGuiCol_Text, palette().accent);
                const bool go = ImGui::Selectable(tree.c_str(), false, 0, {ImGui::CalcTextSize(tree.c_str()).x, 0});
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SameLine();
                ImGui::TextDisabled("%d floating", count);
                if (go) {
                    for (const auto &ref : session.floating)
                        if (ref.tree == tree) {
                            session.reveal(ref);
                            break;
                        }
                    close = true;
                }
            }
            ImGui::Spacing();
            if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape))
                close = true;
            break;
        }
        case Session::Dialog::None:
            break;
        }
        if (close) {
            dialog.kind = Session::Dialog::None;
            session.focusCanvas = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Files changed on disk while edited here.
    if (!session.conflicts.empty()) {
        if (!ImGui::IsPopupOpen("Changed on disk"))
            ImGui::OpenPopup("Changed on disk");
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {.5f, .5f});
        if (ImGui::BeginPopupModal("Changed on disk", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            auto *document = session.conflicts.front();
            ImGui::Text("%s changed on disk, and it has unsaved edits here.",
                        document->path.filename().string().c_str());
            if (ImGui::Button("Reload from disk (drop my edits)")) {
                session.workspace.reload(*document);
                session.history.clear();
                session.conflicts.erase(session.conflicts.begin());
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Keep mine (save overwrites it)")) {
                document->savedTime = std::filesystem::last_write_time(document->path);
                std::string text;
                readFile(document->path, text);
                document->savedText = text;
                session.conflicts.erase(session.conflicts.begin());
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
}

const std::vector<std::pair<const char *, const char *>> &canvasKeys() {
    static const std::vector<std::pair<const char *, const char *>> keys{
        {"Up", "parent (left-right layout: Left)"},
        {"Down", "first child, or back to the one you came from"},
        {"Left / Right", "previous / next sibling, or the nearest node on that level"},
        {"Home", "the tree's root"},
        {"Shift+arrows", "move and add both nodes to the selection"},
        {"Ctrl+arrows", "move without changing the selection"},
        {"Shift+Home / Shift+End", "select up to the first / last sibling"},
        {"Alt+Left / Alt+Right", "move the node earlier / later among its siblings"},
        {"Alt+Up", "move the node out of its parent"},
        {"Alt+Down", "move the node into the previous sibling"},
        {"Space", "fold / unfold; on a SubTree call, open it in place"},
        {"Esc", "clear the selection and the key highlight"},
        {"Ctrl+= / Ctrl+-", "zoom in / out (Ctrl+wheel: at the mouse)"},
        {"Click", "select; Ctrl+click or Shift+click adds or removes"},
        {"Shift+drag", "select a box (on empty canvas)"},
        {"Drag a node", "move it (and the selection): beside a node, or into it"},
        {"Double-click", "edit the node; on a call, open its tree"},
        {"Drag", "pan (empty canvas; also the middle button)"},
    };
    return keys;
}

void drawShortcuts(Session &session) {
    if (!session.showShortcuts)
        return;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({ui(620), display.y * .75f}, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos({display.x / 2, display.y / 2}, ImGuiCond_Appearing, {.5f, .5f});
    if (!ImGui::Begin("Keyboard shortcuts", &session.showShortcuts, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    dismissible(session.showShortcuts);
    static std::string filter;
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-FLT_MIN);
    inputStringWithHint("##keys", "Filter (a key or what it does)", filter);
    if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape))
        session.showShortcuts = false;
    const auto shown = [&](const std::string &a, const std::string &b) {
        return filter.empty() || fuzzyMatch(filter, a).matched || fuzzyMatch(filter, b).matched;
    };
    const auto table = [&](const char *id, const std::vector<std::pair<std::string, std::string>> &rows) {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
            return;
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, ui(190));
        ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch);
        for (const auto &[keys, what] : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(palette().accent, "%s", keys.c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", what.c_str());
        }
        ImGui::EndTable();
    };
    ImGui::BeginChild("##shortcuts");
    std::vector<std::pair<std::string, std::string>> rows;
    for (const auto &[keys, what] : canvasKeys())
        if (shown(keys, what))
            rows.push_back({keys, what});
    if (!rows.empty()) {
        sectionTitle("Canvas");
        table("##canvas", rows);
    }
    for (const char *group : {"File", "Edit", "Select", "Node", "Tree", "View", "Help"}) {
        rows.clear();
        for (const auto &c : session.commands)
            if (c.group == group && c.chord && c.id != "edit.redo2" && shown(chordName(c.chord), c.label))
                rows.push_back({chordName(c.chord), c.label});
        if (rows.empty())
            continue;
        sectionTitle(group);
        table(group, rows);
    }
    ImGui::EndChild();
    ImGui::End();
}

void handleShortcuts(Session &session) {
    if (session.showFinder || session.picker.open || session.dialog.kind != Session::Dialog::None ||
        !session.conflicts.empty() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        return;
    for (const auto &command : session.commands) {
        if (!command.chord)
            continue;
        bool pressed = false;
        if (command.canvasOnly)
            pressed = session.canvasFocused && ImGui::IsKeyChordPressed(command.chord);
        else
            pressed = ImGui::Shortcut(command.chord, ImGuiInputFlags_RouteGlobal);
        if (pressed) {
            session.run(command.id);
            break;
        }
    }
}
} // namespace notgroot::ui
