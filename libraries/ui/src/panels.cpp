// Explorer, palette, variables and problems panels.
#include "notgroot/ui/panels.hpp"
#include "notgroot/core/search.hpp"
#include "notgroot/ui/style.hpp"
#include "notgroot/ui/theme.hpp"
#include <algorithm>

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
    std::stable_sort(shown.begin(), shown.end(), [](auto &a, auto &b) { return a.first < b.first; });

    const auto add = [&](const NodeModel &model) {
        if (session.cursor.readOnly() || session.cursor.ref.empty()) {
            session.status("Put the cursor on a node in the open tree first", true);
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
    for (const auto &[score, model] : shown) {
        chip(categoryColor(model.category));
        ImGui::PushID(model.id.c_str());
        if (ImGui::Selectable(model.id.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            add(model);
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("NOTGROOT_MODEL", model.id.data(), model.id.size());
            ImGui::Text("%s", model.id.c_str());
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

void drawVariables(Session &session) {
    if (session.active.empty()) {
        emptyState("Open a tree to see its blackboard keys.");
        return;
    }
    static std::string filter;
    static bool constants = false;
    filterBox("##variables", "Filter keys", filter);
    ImGui::Checkbox("Show constants", &constants);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Keys set to their own name (SetBlackboardV3 output_key=\"5\" value=\"5\"): how converted "
                          "trees pass literals");
    ImGui::SameLine();
    ImGui::TextDisabled("(F2 renames the highlighted key)");

    const auto &variables = session.variables(session.active);
    std::vector<const KeyInfo *> shown;
    for (const auto &[name, key] : variables.keys)
        if ((constants || !key.constant) && fuzzyMatch(filter, name).matched)
            shown.push_back(&key);

    const auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp |
                       ImGuiTableFlags_BordersInnerH;
    const float detail = session.highlightKey.empty() ? 0 : ui(170);
    if (ImGui::BeginTable("##keys", 4, flags, {0, -detail})) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Reads", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Writes", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        tableHeaders();
        for (const auto *key : shown) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            chip(keyColor(key->name));
            if (ImGui::Selectable(("{" + key->name + "}").c_str(), session.highlightKey == key->name,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                session.highlightKey = session.highlightKey == key->name ? "" : key->name;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !key->uses.empty()) {
                    session.highlightKey = key->name;
                    session.reveal(key->uses.front().node);
                }
            }
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Rename…", "F2"))
                    session.ask(Session::Dialog::RenameKey, session.active, key->name);
                if (ImGui::MenuItem("Go to first use") && !key->uses.empty())
                    session.reveal(key->uses.front().node);
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            ImGui::Text("%d", key->reads);
            ImGui::TableNextColumn();
            ImGui::Text("%d", key->writes);
            ImGui::TableNextColumn();
            if (key->port) {
                ImGui::TextColored(directionColor(key->portDirection), "port %s", directionBadge(key->portDirection));
                ImGui::SameLine();
            }
            if (key->undefined())
                ImGui::TextColored(palette().warn, "never set");
            else if (key->unused() && !key->constant)
                ImGui::TextDisabled("never read");
            else if (key->constant)
                ImGui::TextDisabled("constant");
        }
        ImGui::EndTable();
    }

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
