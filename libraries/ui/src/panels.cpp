// Explorer, palette, variables and problems panels.
#include "notgroot/ui/panels.hpp"
#include "notgroot/core/search.hpp"
#include "notgroot/ui/style.hpp"
#include "notgroot/ui/theme.hpp"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <map>

namespace notgroot::ui {
using namespace core;

namespace {
// A color chip the height of a text line.
void chip(ImVec4 color, float width = 0) {
    const float size = ImGui::GetTextLineHeight();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled({at.x, at.y + size * .2f},
                                              {at.x + (width > 0 ? width : size * .45f), at.y + size * .8f},
                                              ImGui::GetColorU32(color), ui(2));
    ImGui::Dummy({width > 0 ? width : size * .45f, size});
    ImGui::SameLine();
}

bool filterBox(const char *id, const char *hint, std::string &text) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    return inputStringWithHint(id, hint, text);
}

} // namespace

void drawExplorer(Session &session) {
    if (!session.workspace.isOpen()) {
        emptyState("No trees folder open (Ctrl+O).");
        return;
    }
    static std::string filter;
    filterBox("##explorer", "Filter trees", filter);
    if (ImGui::SmallButton("+ Subtree"))
        session.run("tree.newSubtree");
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Mission"))
        session.run("tree.newMission");
    ImGui::Separator();

    ImGui::BeginChild("##explorer_list");
    const auto section = [&](const char *title, const std::vector<std::string> &keys) {
        std::vector<std::pair<int, std::string>> shown;
        for (const auto &key : keys) {
            const auto match = fuzzyMatch(filter, key);
            if (match.matched)
                shown.push_back({filter.empty() ? 0 : -match.score, key});
        }
        std::stable_sort(shown.begin(), shown.end());
        char header[96];
        std::snprintf(header, sizeof(header), "%s (%zu)###%s", title, shown.size(), title);
        if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen))
            return;
        for (const auto &[score, key] : shown) {
            const auto *entry = session.workspace.tree(key);
            const bool dirty = entry->document->dirty();
            int worst = 3;
            for (const auto &problem : session.problems())
                if (problem.node.tree == key && problem.node.path.empty())
                    worst = std::min(worst, int(problem.severity));
            chip(key != entry->id ? rootColor() : categoryColor(Category::SubTree));
            const std::string label = key + (dirty ? " •" : "");
            if (ImGui::Selectable((label + "##" + key).c_str(), key == session.active,
                                  ImGuiSelectableFlags_AllowDoubleClick))
                session.openTree(key);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", entry->document->path.string().c_str());
            if (worst <= 1) {
                ImGui::SameLine();
                ImGui::TextColored(severityColor(worst), worst == 0 ? "error" : "warning");
            }
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Open"))
                    session.openTree(key);
                if (ImGui::MenuItem("Rename…")) {
                    session.openTree(key);
                    session.ask(Session::Dialog::RenameTree, key, key);
                }
                if (key == entry->id && ImGui::MenuItem("Edit ports…"))
                    session.ask(Session::Dialog::EditPorts, key);
                if (ImGui::MenuItem("Delete")) {
                    if (session.editor.deleteTree(key)) {
                        session.closeTree(key);
                        session.status("Deleted " + key + " (removed from disk on save)");
                    } else
                        session.status(session.editor.error(), true);
                }
                ImGui::EndPopup();
            }
        }
    };
    section("Missions", session.workspace.missions());
    section("Subtrees", session.workspace.subtrees());
    ImGui::EndChild();
}

void drawPalette(Session &session) {
    static std::string filter;
    static int category = -1;
    filterBox("##palette", "Filter nodes (Enter adds the first under the cursor)", filter);
    const bool enter = ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Enter);
    const char *names[] = {"All", "Action", "Condition", "Control", "Decorator", "SubTree"};
    for (int i = -1; i < 5; ++i) {
        if (i > -1)
            ImGui::SameLine();
        const bool on = category == i;
        if (on)
            ImGui::PushStyleColor(ImGuiCol_Button, palette().active);
        if (ImGui::SmallButton(names[i + 1]))
            category = i;
        if (on)
            ImGui::PopStyleColor();
    }
    ImGui::Separator();

    std::vector<std::pair<int, NodeModel>> shown;
    for (const auto *model : session.workspace.catalog().all()) {
        if (category >= 0 && int(model->category) != category)
            continue;
        const auto match = fuzzyMatch(filter, model->id);
        if (match.matched)
            shown.push_back({filter.empty() ? 0 : -match.score, *model});
    }
    // Best match first while filtering; else by type, then by name.
    const auto lower = [](std::string text) {
        for (auto &c : text)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };
    std::stable_sort(shown.begin(), shown.end(), [&](auto &a, auto &b) {
        if (!filter.empty())
            return a.first < b.first;
        if (a.second.category != b.second.category)
            return a.second.category < b.second.category;
        return lower(a.second.id) < lower(b.second.id);
    });

    const auto add = [&](const NodeModel &model) {
        if (session.readOnly(session.cursor) || session.cursor.ref.empty()) {
            session.status(session.cursor.ref.empty() ? "Put the cursor on a node in the open tree first"
                                                      : "That node is in a locked subtree: unlock it first (Ctrl+U)",
                           true);
            return;
        }
        auto *element = resolve(session.workspace, session.cursor.ref);
        if (element && acceptsChild(session.workspace, *element))
            session.edited(session.editor.insert(session.cursor.ref, nodeChildren(*element).size(), makeNode(model),
                                                 "Add " + model.id));
        else
            session.edited(session.editor.insertAfter(session.cursor.ref, makeNode(model), "Add " + model.id));
    };
    if (enter && !shown.empty())
        add(shown.front().second);

    ImGui::BeginChild("##palette_list");
    for (std::size_t i = 0; i < shown.size(); ++i) {
        const auto &model = shown[i].second;
        if (filter.empty() && (i == 0 || shown[i - 1].second.category != model.category))
            sectionTitle(categoryName(model.category));
        chip(categoryColor(model.category));
        ImGui::PushID(model.id.c_str());
        if (ImGui::Selectable(model.id.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            add(model);
        if (ImGui::BeginDragDropSource()) {
            // A small card of the node, and where it can go.
            ImGui::SetDragDropPayload("NOTGROOT_MODEL", model.id.data(), model.id.size());
            chip(categoryColor(model.category), ui(4));
            ImGui::PushFont(typeRamp().strong);
            ImGui::TextUnformatted(model.id.c_str());
            ImGui::PopFont();
            ImGui::TextDisabled("Drop on a node in the tree: its middle adds a child, its ends a sibling");
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginItemTooltip()) {
            ImGui::TextColored(categoryColor(model.category), "%s%s", categoryName(model.category),
                               model.builtin ? " (builtin)" : "");
            if (!model.description.empty())
                ImGui::TextWrapped("%s", model.description.c_str());
            for (const auto &port : model.ports) {
                ImGui::TextColored(directionColor(port.direction), "%-3s", directionBadge(port.direction));
                ImGui::SameLine();
                ImGui::Text("%s", port.name.c_str());
                if (!port.description.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", port.description.c_str());
                }
            }
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

namespace {
// Typing a key after "{": the rest of the first key that starts that way, shown in gray; Tab takes it and closes the
// brace.
struct Suggestion {
    std::string *text;
    const std::vector<std::string> *keys;
    std::string rest; // what Tab would add (the key's rest and "}")
    int cursor = 0;
};

int suggest(ImGuiInputTextCallbackData *data) {
    auto *s = static_cast<Suggestion *>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        s->text->resize(std::size_t(data->BufTextLen));
        data->Buf = s->text->data();
        return 0;
    }
    const std::string_view text(data->Buf, std::size_t(data->BufTextLen));
    s->rest = keyCompletion(text, std::size_t(data->CursorPos), *s->keys);
    s->cursor = data->CursorPos;
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        const bool closed = data->CursorPos < data->BufTextLen && data->Buf[data->CursorPos] == '}';
        if (!s->rest.empty())
            data->InsertChars(data->CursorPos, s->rest.c_str());
        else if (closed)
            data->CursorPos += 1; // a whole key typed already: Tab steps past its brace
        if (closed && !s->rest.empty())
            data->CursorPos += 1;
        s->rest.clear();
    }
    return 0;
}

bool keyField(const char *id, const char *hint, std::string &text, const std::vector<std::string> &keys,
              ImGuiInputTextFlags flags = 0) {
    Suggestion s{&text, &keys, "", 0};
    const bool done =
        ImGui::InputTextWithHint(id, hint, text.data(), text.capacity() + 1,
                                 flags | ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CallbackAlways |
                                     ImGuiInputTextFlags_CallbackCompletion,
                                 suggest, &s);
    if (ImGui::IsItemActive() && !s.rest.empty()) {
        const auto &style = ImGui::GetStyle();
        const float x =
            ImGui::GetItemRectMin().x + style.FramePadding.x +
            ImGui::CalcTextSize(text.c_str(), text.c_str() + std::min<std::size_t>(std::size_t(s.cursor), text.size()))
                .x;
        ImGui::GetWindowDrawList()->AddText({x, ImGui::GetItemRectMin().y + style.FramePadding.y},
                                            ImGui::GetColorU32(ImGuiCol_TextDisabled), s.rest.c_str());
    }
    return done;
}

std::vector<Global> globalsOf(Session &session, const std::string &tree) {
    const auto *entry = session.workspace.tree(tree);
    return entry ? readGlobals(*entry->tree).globals : std::vector<Global>{};
}
} // namespace

bool applyGlobals(Session &session, const std::string &tree, const std::vector<Global> &list, const std::string &what) {
    session.globalCycle.clear();
    if (session.editor.setGlobals(tree, list)) {
        session.status(what);
        return true;
    }
    for (const auto &cycle : session.editor.cycles())
        session.globalCycle.insert(session.globalCycle.end(), cycle.begin(), cycle.end());
    session.status(session.editor.error(), true);
    return false;
}

std::vector<std::string> globalNames(Session &session, const std::string &tree) {
    std::vector<std::string> usable;
    for (const auto &[name, key] : session.variables(tree).keys)
        if (key.scope != Scope::Local)
            usable.push_back(name);
    return usable;
}

void globalValueField(Session &session, const std::string &tree, const std::string &name, const std::string &value,
                      const std::vector<std::string> &usable) {
    // The value: text, with {key} for another global or an input (they are joined in). A buffer per field: the
    // panel and the boxes on the canvas can show the same global.
    static std::map<ImGuiID, std::string> editing;
    auto &text = editing.try_emplace(ImGui::GetID("##value"), value).first->second;
    keyField("##value", "text, {key}…", text, usable);
    if (ImGui::IsItemDeactivatedAfterEdit() && text != value) {
        auto list = globalsOf(session, tree);
        for (auto &global : list)
            if (global.name == name)
                global.value = text;
        applyGlobals(session, tree, list, "Set " + name);
    }
    if (!ImGui::IsItemActive())
        text = value;
}

void removeGlobalButton(Session &session, const std::string &tree, const std::string &name) {
    if (ImGui::SmallButton("x")) {
        auto list = globalsOf(session, tree);
        list.erase(std::remove_if(list.begin(), list.end(), [&](auto &g) { return g.name == name; }), list.end());
        applyGlobals(session, tree, list, "Removed global " + name);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Remove the global");
}

void newGlobalFields(Session &session, const std::string &tree, const std::vector<std::string> &usable,
                     float nameWidth) {
    static std::map<std::string, std::pair<std::string, std::string>> drafts; // name and value, per tree
    auto &[name, value] = drafts[tree];
    ImGui::SetNextItemWidth(nameWidth);
    bool add = inputStringWithHint("##newname", "new global", name, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
    add |= keyField("##newvalue", "value, {key}…", value, usable, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    add |= ImGui::Button("+", {ImGui::GetFrameHeight(), 0});
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Add the global");
    if (add && !name.empty()) {
        auto list = globalsOf(session, tree);
        list.push_back({name, value});
        if (applyGlobals(session, tree, list, "New global " + name)) {
            session.highlightKey = name;
            name.clear();
            value.clear();
        }
    }
}

void drawVariables(Session &session) {
    if (session.active.empty()) {
        emptyState("Open a tree to see its blackboard keys.");
        return;
    }
    const auto *entry = session.workspace.tree(session.active);
    if (!entry)
        return;
    static std::string filter;
    filterBox("##variables", "Filter keys", filter);
    // A copy: an edit made from this panel (a global added, a value set) recomputes the session's, while this frame
    // still draws the rows it found.
    const auto variables = session.variables(session.active);
    std::vector<const KeyInfo *> inputs, globals, locals;
    for (const auto &[name, key] : variables.keys)
        if (fuzzyMatch(filter, name).matched)
            (key.scope == Scope::Input ? inputs : key.scope == Scope::Global ? globals : locals).push_back(&key);
    const float detail = session.highlightKey.empty() ? 0 : ui(170);
    ImGui::BeginChild("##sections", {0, -detail});

    // A key's name: its color, a click highlights it, a double-click goes to its first use, F2 renames it.
    const auto keyCell = [&](const KeyInfo &key) {
        chip(session.keyColor(session.active, key.name));
        const bool circular =
            std::find(session.globalCycle.begin(), session.globalCycle.end(), key.name) != session.globalCycle.end();
        if (circular)
            ImGui::PushStyleColor(ImGuiCol_Text, palette().error);
        if (ImGui::Selectable(("{" + key.name + "}##" + key.name).c_str(), session.highlightKey == key.name,
                              ImGuiSelectableFlags_AllowDoubleClick)) {
            session.highlightKey = session.highlightKey == key.name ? "" : key.name;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !key.uses.empty()) {
                session.highlightKey = key.name;
                session.reveal(key.uses.front().node);
            }
        }
        if (circular) {
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Part of a circle of globals that use each other");
        }
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Rename…", "F2"))
                session.ask(Session::Dialog::RenameKey, session.active, key.name);
            if (ImGui::MenuItem("Go to first use", nullptr, false, !key.uses.empty()))
                session.reveal(key.uses.front().node);
            ImGui::EndPopup();
        }
    };
    const auto counts = [&](const KeyInfo &key) {
        ImGui::TableNextColumn();
        ImGui::Text("%d", key.reads);
        ImGui::TableNextColumn();
        ImGui::Text("%d", key.writes);
        ImGui::TableNextColumn();
        if (key.undefined())
            ImGui::TextColored(palette().warn, "never set");
        else if (key.unused())
            ImGui::TextDisabled("never read");
    };
    const auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH;
    const auto header = [&](const char *title, std::size_t count) {
        char label[64];
        std::snprintf(label, sizeof(label), "%s (%zu)###%s", title, count, title);
        return ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
    };

    // Inputs: the subtree's ports (Edit ports… changes them).
    if (entry->id == session.active && header("Inputs", inputs.size())) {
        if (inputs.empty())
            ImGui::TextDisabled("No ports: Tree > Edit subtree ports adds them");
        else if (ImGui::BeginTable("##inputs", 4, flags)) {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Reads", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Writes", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
            tableHeaders();
            for (const auto *key : inputs) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                keyCell(*key);
                ImGui::SameLine();
                ImGui::TextColored(directionColor(key->portDirection), "%s", directionBadge(key->portDirection));
                counts(*key);
            }
            ImGui::EndTable();
        }
    }

    // Globals: set before the tree runs, in the order their values need. No nodes: one Script the editor keeps.
    if (header("Globals", globals.size())) {
        const auto usable = globalNames(session, session.active);
        if (!globals.empty() && ImGui::BeginTable("##globals", 3, flags)) {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 1);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.6f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
            tableHeaders();
            for (const auto *key : globals) {
                ImGui::PushID(key->name.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                keyCell(*key);
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                globalValueField(session, session.active, key->name, key->value, usable);
                if (ImGui::IsItemHovered() && key->uses.empty())
                    ImGui::SetTooltip("Not read anywhere yet");
                ImGui::TableNextColumn();
                removeGlobalButton(session, session.active, key->name);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        newGlobalFields(session, session.active, usable, ui(130));
    }

    // Locals: keys bound while the tree runs (a node's {key}, a setter, a script).
    if (header("Locals", locals.size()) && !locals.empty() && ImGui::BeginTable("##locals", 4, flags)) {
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Reads", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Writes", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        tableHeaders();
        for (const auto *key : locals) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            keyCell(*key);
            counts(*key);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    // The highlighted key's uses.
    if (!session.highlightKey.empty()) {
        const auto found = variables.keys.find(session.highlightKey);
        sectionTitle(("Uses of {" + session.highlightKey + "}").c_str());
        ImGui::BeginChild("##uses");
        if (found != variables.keys.end())
            for (std::size_t i = 0; i < found->second.uses.size(); ++i) {
                const auto &use = found->second.uses[i];
                const auto *element = resolve(session.workspace, use.node);
                if (!element)
                    continue;
                const std::string what =
                    (use.access & Write ? (use.access & Read ? "reads+writes " : "writes ") : "reads ");
                const std::string label = what + nodeId(*element) + "." + use.attribute + "##use" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), session.cursor.ref == use.node))
                    session.reveal(use.node);
            }
        ImGui::EndChild();
    }
}

void drawProblems(Session &session) {
    const auto &problems = session.problems();
    static bool notes = false;
    int counts[3] = {0, 0, 0};
    for (const auto &problem : problems)
        ++counts[int(problem.severity)];
    ImGui::TextColored(counts[0] ? palette().error : palette().muted, "%d errors", counts[0]);
    ImGui::SameLine();
    ImGui::TextColored(counts[1] ? palette().warn : palette().muted, "%d warnings", counts[1]);
    ImGui::SameLine();
    ImGui::Checkbox(("notes (" + std::to_string(counts[2]) + ")").c_str(), &notes);
    ImGui::SameLine();
    ImGui::TextDisabled("F8: next problem");

    const auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("##problems", 3, flags))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Problem", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Where", ImGuiTableColumnFlags_WidthFixed);
    tableHeaders();
    for (std::size_t i = 0; i < problems.size(); ++i) {
        const auto &problem = problems[i];
        if (problem.severity == Severity::Note && !notes)
            continue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(severityColor(int(problem.severity)), "%s", severityName(problem.severity));
        ImGui::TableNextColumn();
        if (ImGui::Selectable((problem.message + "##p" + std::to_string(i)).c_str(), false,
                              ImGuiSelectableFlags_SpanAllColumns) &&
            !problem.node.empty())
            session.reveal(problem.node);
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s:%d", problem.file.filename().string().c_str(), problem.line);
    }
    ImGui::EndTable();
}
} // namespace notgroot::ui
