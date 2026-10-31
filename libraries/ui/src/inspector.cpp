// Inspector: the cursor's node (or tree) and its ports, with key completion.
#include "notgroot/core/search.hpp"
#include "notgroot/ui/panels.hpp"
#include "notgroot/ui/style.hpp"
#include "notgroot/ui/theme.hpp"
#include <algorithm>
#include <imgui_internal.h>

namespace notgroot::ui {
using namespace core;

namespace {
// Key completion inside a value field: after an unclosed "{", the tree's keys that match what follows it.
struct Completion {
    ImGuiID field = 0;
    std::vector<std::string> options;
    int selected = 0;
    std::string accept; // set when a key is chosen; the callback writes it in
    bool visible = false;
};
Completion completion;

std::string fragment(const char *text, int cursor) {
    const std::string before(text, std::size_t(cursor));
    const auto open = before.rfind('{');
    if (open == std::string::npos || before.find('}', open) != std::string::npos)
        return "\x01"; // no open brace
    return before.substr(open + 1);
}

int completeCallback(ImGuiInputTextCallbackData *data) {
    auto *keys = static_cast<const std::vector<std::string> *>(data->UserData);
    const auto typed = fragment(data->Buf, data->CursorPos);
    if (typed == "\x01") {
        completion.options.clear();
        return 0;
    }
    if (!completion.accept.empty()) {
        // Replace the fragment with the key and close the brace.
        data->DeleteChars(data->CursorPos - int(typed.size()), int(typed.size()));
        std::string insert = completion.accept;
        if (data->CursorPos >= data->BufTextLen || data->Buf[data->CursorPos] != '}')
            insert += "}";
        data->InsertChars(data->CursorPos, insert.c_str());
        completion.accept.clear();
        completion.options.clear();
        return 0;
    }
    if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory && !completion.options.empty()) {
        const int n = int(completion.options.size());
        completion.selected = (completion.selected + (data->EventKey == ImGuiKey_UpArrow ? n - 1 : 1)) % n;
        return 0;
    }
    std::vector<std::pair<int, std::string>> ranked;
    for (const auto &key : *keys)
        if (const auto match = fuzzyMatch(typed, key); match.matched)
            ranked.push_back({-match.score, key});
    std::stable_sort(ranked.begin(), ranked.end());
    completion.options.clear();
    for (std::size_t i = 0; i < ranked.size() && i < 12; ++i)
        completion.options.push_back(ranked[i].second);
    completion.selected = std::min(completion.selected, std::max(0, int(completion.options.size()) - 1));
    return 0;
}

// The editable value of one attribute. Commits on Enter or when the field loses focus; Esc returns to the canvas.
void valueField(Session &session, const NodeRef &ref, const std::string &name, const std::string &value,
                const std::vector<std::string> &keys, bool focus, bool toggle = true) {
    static std::map<std::string, std::string> buffers;
    const std::string id = "##value_" + name;
    const ImGuiID field = ImGui::GetID(id.c_str());
    auto &buffer = buffers[name];
    if (ImGui::GetActiveID() != field)
        buffer = value;

    // {} toggles between text and a key.
    const bool key = toggle && isKeyReference(value);
    if (toggle) {
        if (key)
            ImGui::PushStyleColor(ImGuiCol_Button, withAlpha(session.keyColor(ref.tree, keyOf(value)), .35f));
        if (ImGui::SmallButton(("{}##toggle_" + name).c_str())) {
            if (key)
                session.edited(session.editor.setAttribute(ref, name, keyOf(value)));
            else
                session.edited(session.editor.setAttribute(ref, name, "{" + (value.empty() ? name : value) + "}"));
        }
        if (key)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(key ? "A blackboard key: click for text" : "Text: click to make it a blackboard key");
        ImGui::SameLine();
    }

    if (focus)
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (key)
        ImGui::PushStyleColor(ImGuiCol_Text, session.keyColor(ref.tree, keyOf(value)));
    ImGuiInputTextFlags flags =
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_CallbackHistory;
    const bool showing = completion.field == field && !completion.options.empty();
    if (showing)
        flags |= ImGuiInputTextFlags_CallbackCompletion; // Tab takes the suggestion
    bool enter =
        inputString(id.c_str(), buffer, flags, completeCallback, const_cast<std::vector<std::string> *>(&keys));
    if (key)
        ImGui::PopStyleColor();
    const bool active = ImGui::IsItemActive();
    if (active)
        completion.field = field;

    // Enter or Tab on a suggestion takes it (next frame's callback writes it in) instead of committing.
    if (showing && active && (ImGui::IsKeyPressed(ImGuiKey_Tab) || enter)) {
        completion.accept = completion.options[std::size_t(completion.selected)];
        if (enter) {
            enter = false;
            ImGui::ActivateItemByID(field);
        }
    }
    if (enter || ImGui::IsItemDeactivatedAfterEdit()) {
        if (buffer != value || (!value.empty() && buffer.empty()))
            session.edited(session.editor.setAttribute(ref, name, buffer));
        if (enter)
            session.focusCanvas = true;
    }
    if (ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Escape))
        session.focusCanvas = true;

    // The suggestions under the field.
    if (active && completion.field == field && !completion.options.empty()) {
        const ImVec2 under{ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + ui(2)};
        ImGui::SetNextWindowPos(under);
        ImGui::SetNextWindowSizeConstraints({ImGui::GetItemRectSize().x, 0}, {ui(420), ui(300)});
        ImGui::Begin("##completion", nullptr,
                     ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoTitleBar |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings);
        for (std::size_t i = 0; i < completion.options.size(); ++i) {
            const auto &option = completion.options[i];
            const bool on = int(i) == completion.selected;
            ImGui::TextColored(on ? palette().accent : session.keyColor(session.active, option), "%s{%s}",
                               on ? "› " : "  ", option.c_str());
        }
        ImGui::TextDisabled("Tab or Enter takes it, ↑↓ choose");
        ImGui::End();
    } else if (completion.field == field && !active)
        completion.options.clear();
}

// Keys a value can name in this tree: its keys and its ports.
std::vector<std::string> keysIn(Session &session, const std::string &tree) {
    std::vector<std::string> keys;
    for (const auto &[name, info] : session.variables(tree).keys)
        keys.push_back(name);
    return keys;
}

void treeInspector(Session &session, const std::string &key) {
    const auto *entry = session.workspace.tree(key);
    if (!entry)
        return;
    // Its ID, kept: an edit below (its color) reindexes the workspace, and `entry` goes with it.
    const std::string id = entry->id;
    const bool subtree = key == id;
    ImGui::PushFont(typeRamp().number);
    ImGui::TextColored(subtree ? categoryColor(Category::SubTree) : rootColor(), "%s", key.c_str());
    ImGui::PopFont();
    ImGui::TextDisabled("%s · %s", subtree ? "subtree" : "mission", entry->document->path.string().c_str());
    if (ImGui::Button("Rename…"))
        session.ask(Session::Dialog::RenameTree, key, key);
    ImGui::SameLine();
    if (subtree && ImGui::Button("Edit ports…"))
        session.ask(Session::Dialog::EditPorts, key);
    if (subtree)
        ImGui::SameLine();
    if (ImGui::Button("Delete"))
        session.run("tree.delete");

    if (subtree) {
        // Its color on every call (btstudio's custom subtree colors), kept as a notgroot:color comment.
        const auto hex = treeColor(session.workspace, id);
        ImVec4 color = subtreeColor(hex);
        float rgb[3] = {color.x, color.y, color.z};
        ImGui::ColorEdit3("Color", rgb, ImGuiColorEditFlags_NoInputs);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            char text[8];
            std::snprintf(text, sizeof(text), "#%02x%02x%02x", int(rgb[0] * 255 + .5f), int(rgb[1] * 255 + .5f),
                          int(rgb[2] * 255 + .5f));
            session.edited(session.editor.setTreeColor(id, text));
        }
        if (!hex.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Default color"))
                session.edited(session.editor.setTreeColor(id, ""));
        }
        sectionTitle("Ports");
        const auto model = session.workspace.subtreeModel(id);
        if (model.ports.empty())
            emptyState("No ports: callers pass nothing in or out. Edit ports… to add some.");
        for (const auto &port : model.ports) {
            ImGui::TextColored(directionColor(port.direction), "%-3s", directionBadge(port.direction));
            ImGui::SameLine();
            ImGui::TextColored(session.keyColor(key, port.name), "%s", port.name.c_str());
            if (!port.description.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", port.description.c_str());
            }
        }
        sectionTitle("Called from");
        const auto sites = session.editor.callSites(id);
        if (sites.empty())
            emptyState("Nothing calls this subtree.");
        for (std::size_t i = 0; i < sites.size(); ++i)
            if (ImGui::Selectable((sites[i].tree + "##site" + std::to_string(i)).c_str()))
                session.reveal(sites[i]);
    }
}
} // namespace

void drawInspector(Session &session) {
    if (session.focusInspector)
        ImGui::SetWindowFocus();
    const auto &cursor = session.cursor;
    const auto *element = resolve(session.workspace, cursor.ref);
    if (!element) {
        emptyState("Select a node on the canvas.");
        session.focusInspector = false;
        return;
    }
    if (cursor.ref.isTree()) {
        treeInspector(session, cursor.ref.tree);
        session.focusInspector = false;
        return;
    }
    const bool readOnly = cursor.readOnly();
    const bool call = isSubTree(*element);
    const auto id = call ? element->get("ID") : nodeId(*element);
    const auto category = nodeCategory(*element, session.workspace.catalog());
    NodeModel model;
    const bool known = session.model(id, model);

    // Type, category, description.
    ImGui::PushFont(typeRamp().number);
    ImGui::TextColored(categoryColor(category), "%s", id.c_str());
    ImGui::PopFont();
    ImGui::TextDisabled("%s%s%s", call ? "SubTree call" : categoryName(category),
                        known && model.builtin ? " · builtin" : "",
                        readOnly ? (" · in " + cursor.ref.tree + ", read-only here").c_str() : "");
    if (known && !model.description.empty())
        ImGui::TextWrapped("%s", model.description.c_str());
    if (!readOnly) {
        if (ImGui::SmallButton("Change type… (Ctrl+R)"))
            session.pick(PickFor::Retype);
        if (call) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Open definition (Ctrl+Enter)"))
                session.run("node.definition");
            ImGui::SameLine();
            if (ImGui::SmallButton("Edit ports…"))
                session.ask(Session::Dialog::EditPorts, id);
        }
    } else if (ImGui::SmallButton("Open its tree to edit (Ctrl+Enter)"))
        session.run("node.definition");

    ImGui::BeginDisabled(readOnly);
    const auto keys = keysIn(session, cursor.ref.tree);
    bool focusFirst = session.focusInspector;
    session.focusInspector = false;
    const std::string focusPort = session.focusPort;
    session.focusPort.clear();
    // The first port takes the focus (Name only when asked for by name).
    const auto focusHere = [&](const std::string &name) {
        const bool now = focusPort.empty() ? focusFirst && name != "name" : focusPort == name;
        if (now)
            focusFirst = false;
        return now;
    };

    sectionTitle("Name");
    valueField(session, cursor.ref, "name", element->get("name"), {}, focusHere("name"), false);

    // Ports: the model's, then attributes it does not know, then the special ones.
    const auto row = [&](const std::string &name, const char *badge, ImVec4 badgeInk, const std::string &tip,
                         bool removable) {
        ImGui::PushID(name.c_str());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(badgeInk, "%s", badge);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(name.c_str());
        if (!tip.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tip.c_str());
        ImGui::TableNextColumn();
        valueField(session, cursor.ref, name, element->get(name), keys, focusHere(name));
        ImGui::TableNextColumn();
        if (removable && element->has(name)) {
            if (ImGui::SmallButton("×"))
                session.edited(session.editor.removeAttribute(cursor.ref, name));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Remove the attribute (a port falls back to its default)");
        }
        ImGui::PopID();
    };

    sectionTitle(call ? "Passed to the subtree" : "Ports");
    if (ImGui::BeginTable("##ports", 4, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        if (known)
            for (const auto &port : model.ports) {
                std::string tip = port.description;
                if (!port.type.empty())
                    tip += (tip.empty() ? "" : "\n") + std::string("type ") + port.type;
                if (!port.defaultValue.empty())
                    tip += (tip.empty() ? "" : "\n") + std::string("default ") + port.defaultValue;
                if (port.keyName)
                    tip += (tip.empty() ? "" : "\n") + std::string("a key's name, written bare (no braces)");
                row(port.name, directionBadge(port.direction), directionColor(port.direction), tip, true);
            }
        for (const auto &attribute : element->attributes) {
            if (attribute.name == "ID" || attribute.name == "name" || (known && model.port(attribute.name)) ||
                isReservedAttribute(attribute.name))
                continue;
            row(attribute.name, known ? "??" : "·", known ? palette().error : palette().muted,
                known ? "Not a port of " + id + (call ? " (node_models.xml)" : ": BehaviorTree.CPP rejects it") : "",
                true);
        }
        for (const auto &attribute : element->attributes)
            if (isReservedAttribute(attribute.name) && attribute.name != "ID" && attribute.name != "name")
                row(attribute.name, "IF", palette().muted, "BehaviorTree.CPP script attribute", true);
        ImGui::EndTable();
    }

    // Add a script attribute.
    static const char *scripted[] = {"_skipIf",    "_successIf", "_failureIf", "_while",
                                     "_onSuccess", "_onFailure", "_onHalted",  "_post"};
    ImGui::SetNextItemWidth(ui(200));
    if (ImGui::BeginCombo("##addSpecial", "+ precondition / script…")) {
        for (const char *name : scripted)
            if (!element->has(name) && ImGui::Selectable(name)) {
                session.edited(session.editor.setAttribute(cursor.ref, name, ""));
                session.focusInspector = true;
                session.focusPort = name;
            }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    // Problems on this node.
    bool header = false;
    for (const auto &problem : session.problems())
        if (problem.node == cursor.ref) {
            if (!header)
                sectionTitle("Problems");
            header = true;
            ImGui::TextColored(severityColor(int(problem.severity)), "%s", severityName(problem.severity));
            ImGui::SameLine();
            ImGui::TextWrapped("%s", problem.message.c_str());
        }
}
} // namespace notgroot::ui
