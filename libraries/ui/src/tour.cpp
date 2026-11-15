// Tour: the getting-started pages, shown on first start and from Help. Each page says what is different from Groot
// and shows every move it teaches both ways: with the mouse and with its keys.
#include "notgroot/ui/panels.hpp"
#include "notgroot/ui/style.hpp"
#include "notgroot/ui/theme.hpp"
#include <algorithm>
#include <cfloat>
#include <string>
#include <vector>

namespace notgroot::ui {
namespace {
// One move: what it does, with the mouse, with the keys ("|" between keys that do the same).
struct Move {
    const char *what, *mouse, *keys;
};

// A sketch beside a page's text: two small cards that tell a key from text.
enum class Sketch { None, Keys };

struct Page {
    const char *title;
    std::vector<const char *> text; // paragraphs
    std::vector<Move> moves;
    Sketch sketch = Sketch::None;
};

const std::vector<Page> &pages() {
    static const std::vector<Page> all{
        {"Welcome to notgroot",
         {"notgroot edits riptide_autonomy's BehaviorTree.CPP 4 trees. If you know Groot or Groot2, most of it is "
          "familiar; these pages cover what works differently, in about three minutes.",
          "Every move is shown both ways: with the mouse, and with its keys. Everything can be done from the keyboard. "
          "F1 lists every key, and Help > Getting started tour brings these pages back."},
         {{"Next page / previous page", "Next / Back below", "Right | Left"},
          {"Close the tour", "Skip, or click Done at the end", "Esc"},
          {"Every key, searchable", "Help > Keyboard shortcuts", "F1"}}},
        {"One folder, one copy of every subtree",
         {"Open the whole trees/ folder, not one file. Missions sit at the top; every subtree lives once, in "
          "trees/lib/<ID>.xml, and each mission <include>s what it uses. notgroot keeps the includes right.",
          "So unlike Groot, changing a subtree changes it for every mission that runs it, with no re-saving of each "
          "mission. (The robot sees it after riptide_autonomy2 is rebuilt, as trees/ is installed by copy.)",
          "Layout is automatic: you never place nodes, only put them in the tree. Child order on screen is the order "
          "in the file."},
         {{"Open a trees folder", "File > Open trees folder", "Ctrl+O"},
          {"Open any tree", "Double-click it in Trees", "Ctrl+P, then its name"},
          {"Switch tabs", "Click the tab", "Ctrl+PageDown | Ctrl+PageUp"},
          {"Save (and check every mission)", "File > Save", "Ctrl+S"}}},
        {"A key is {key}; anything else is text",
         {"BehaviorTree.CPP 4 reads a port as a blackboard key only when the whole value is {name}. A v3 call like "
          "x=\"target_x\" now passes the text target_x: write x=\"{target_x}\".",
          "A SubTree has its own blackboard and sees only what its call passes through its ports. Text passed in gets "
          "a type from how it looks: 2 is an int, 2.5 a double, true stays text.",
          "notgroot flags these as you type: keys read but never set, double braces, a literal a subtree would read "
          "with the wrong type."},
         {{"Turn a value into a key and back", "The {} button beside it in the Inspector", "{ }"},
          {"Pick a key while typing", "Type {, click a suggestion", "{ then Up/Down, Tab | Enter"}},
         Sketch::Keys},
        {"Adding nodes",
         {"Nodes only exist inside a tree. Drag one from the Palette onto a node: dropped on the middle of a node it "
          "goes inside it, at either end it goes beside it. Cards that can take it get a faint ring while you drag.",
          "Let go anywhere else (empty canvas, a locked subtree, a node that is full) and nothing is added; a red note "
          "by the pointer and the status bar say why."},
         {{"Add a child", "Drag from the Palette onto the node's middle", "Tab, type to filter, Enter"},
          {"Add a sibling after", "Drag onto the node's left or right end", "Shift+Tab"},
          {"Add under the cursor", "Double-click it in the Palette", "Palette filter, Enter"},
          {"Paste nodes (plain XML, Groot's too)", "Right-click > Paste nodes", "Ctrl+V"}}},
        {"Moving, wrapping, deleting",
         {"Drag a node onto another to move it, with the rest of the selection if it is selected. Alt+arrows move it "
          "one step at a time. Where its new parent can't hold it, it floats, drawn dotted, until you move it on; "
          "nothing saves while a node floats.",
          "Every edit is one undo step, including renames that touch many files."},
         {{"Move a node", "Drag it onto another node", "Alt+arrows"},
          {"Select several", "Ctrl+click, or Shift+drag a box", "Shift+arrows | Ctrl+Space"},
          {"Delete", "Right-click > Delete nodes", "Delete | Backspace"},
          {"Wrap in a control or decorator", "Right-click > Wrap in…", "Ctrl+W"},
          {"Change a node's type", "Right-click > Change node type…", "Ctrl+R"},
          {"Undo / redo", "Edit > Undo / Redo", "Ctrl+Z | Ctrl+Shift+Z"}}},
        {"Editing ports",
         {"Double-click a port's row on a card to edit its value right there: no trip to the Inspector. It works on "
          "SubTree calls too; a double-click on a call's title still opens the subtree. A double-click on any other "
          "node's title gives it a name the same way, and the card grows as you type.",
          "The Inspector shows every port of the node under the cursor, with what each is for, and stays the keyboard "
          "way in."},
         {{"Edit one value (Enter keeps it, Esc cancels, Tab goes to the next row)", "Double-click its row on the card",
           "Enter, then Tab to it"},
          {"Edit all of a node's ports", "Click the node, then the Inspector", "Enter"},
          {"Back to the canvas", "Click the canvas", "Enter | Esc"}}},
        {"Subtrees in place: locked, then unlocked",
         {"A SubTree call opens in place under itself, as in Groot. It starts locked: grayed, in the subtree's color, "
          "shown but not edited.",
          "Unlock it to edit it right there. The band turns amber with a dashed edge, and every edit goes to that "
          "shared file; the count above the call says how many trees call it. Lock it again when you are done: locking or closing it "
          "also locks every subtree under it.",
          "Beside the call sits a box with its globals: a plain list while locked, editable while unlocked."},
         {{"Open a call in place", "The round button under the call", "Space"},
          {"Unlock / lock it", "The padlock on the call's corner", "Ctrl+U"},
          {"Open the subtree in its own tab", "Double-click the call's title", "Ctrl+Enter"},
          {"Edit its globals (unlocked)", "Type in its globals box; + adds, x removes", "Tab | Enter"}}},
        {"Inputs, globals and locals",
         {"The Variables panel splits the open tree's keys three ways. Inputs are a subtree's ports. Globals are set "
          "before anything in the tree runs and have no nodes on the canvas. Locals are everything set while it runs.",
          "Use a global for a value that never changes during the run, in place of a setter node. Each key keeps one "
          "color on every card and panel."},
         {{"Add a global", "Variables > Globals: name, value, +", "Type, then Enter"},
          {"Turn a setter into a global", "Right-click a SetBlackboardV3 > Make global", "Ctrl+Shift+P, Make global"},
          {"Rename a key everywhere", "Right-click it in Variables > Rename", "F2"},
          {"Edit a subtree's ports", "Tree > Edit subtree ports", "Ctrl+Shift+E"}}},
        {"Getting around",
         {"Go to anything by name: a tree, any node in any tree (by type, name or a port value), any key. Prefixes "
          "narrow it: > commands, @ nodes here, $ keys here, # trees."},
         {{"Pan", "Drag empty canvas (or the middle button), wheel", "Arrows (the view follows the cursor)"},
          {"Zoom", "Ctrl+wheel, or pinch on a touchpad", "Ctrl+= | Ctrl+-"},
          {"Fit the tree", "View > Fit tree to view", "Ctrl+0"},
          {"Go to anything", "View > Go to anything", "Ctrl+P"},
          {"Search this tree", "View > Search this tree", "Ctrl+F"},
          {"Left-right layout", "View > Toggle layout", "Ctrl+L"}}},
        {"Checks, then the robot",
         {"Problems updates as you edit. Every save also loads every mission in BehaviorTree.CPP 4.10 itself, so a "
          "tree that won't load shows up here before you ever start doTask.",
          "Habits worth keeping from the migration: SetBlackboardV3, not BT4's SetBlackboard, for keys set while the "
          "tree runs; := creates a key in a script, = only sets one that exists; a script that reads an unset key "
          "stops the tree.",
          "For a live view of a run, set doTask's enable_groot2 parameter and attach Groot2. The full guide is "
          "docs/migrating-from-groot.md."},
         {{"Next problem", "Click it in Problems", "F8"},
          {"Check every mission now", "File > Check with BehaviorTree.CPP", "F7"},
          {"Every command", "The menus", "Ctrl+Shift+P"}}},
    };
    return all;
}

// A key as a small raised chip.
void keyChip(const char *label) {
    const ImVec2 pad{ui(5), ui(1)};
    const ImVec2 size = ImGui::CalcTextSize(label);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    auto *draw = ImGui::GetWindowDrawList();
    const auto &style = ImGui::GetStyle();
    draw->AddRectFilled(at, {at.x + size.x + 2 * pad.x, at.y + size.y + 2 * pad.y},
                        ImGui::GetColorU32(style.Colors[ImGuiCol_FrameBg]), ui(3));
    draw->AddRect(at, {at.x + size.x + 2 * pad.x, at.y + size.y + 2 * pad.y},
                  ImGui::GetColorU32(withAlpha(palette().accent, .6f)), ui(3));
    draw->AddText({at.x + pad.x, at.y + pad.y}, ImGui::GetColorU32(palette().text), label);
    ImGui::Dummy({size.x + 2 * pad.x, size.y + 2 * pad.y});
}

// Keys separated by "|", each a chip.
void keys(const std::string &text) {
    std::size_t from = 0;
    while (from <= text.size()) {
        const auto bar = text.find('|', from);
        auto part = text.substr(from, bar == std::string::npos ? std::string::npos : bar - from);
        while (!part.empty() && part.front() == ' ')
            part.erase(part.begin());
        while (!part.empty() && part.back() == ' ')
            part.pop_back();
        keyChip(part.c_str());
        if (bar == std::string::npos)
            break;
        ImGui::SameLine(0, ui(4));
        ImGui::TextDisabled("or");
        ImGui::SameLine(0, ui(4));
        from = bar + 1;
    }
}

// A SubTree call card as the canvas draws one, with a check or a cross.
void sketchCard(const char *value, const char *note, bool right) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float width = ui(250), line = ImGui::GetTextLineHeight();
    auto *draw = ImGui::GetWindowDrawList();
    const auto &style = ImGui::GetStyle();
    const ImVec2 b{at.x + width, at.y + line * 2 + ui(18)};
    draw->AddRectFilled(at, b, ImGui::GetColorU32(style.Colors[ImGuiCol_FrameBg]), ui(5));
    draw->AddRect(at, b, ImGui::GetColorU32(withAlpha(categoryColor(core::Category::SubTree), .9f)), ui(5));
    draw->AddText({at.x + ui(8), at.y + ui(5)}, ImGui::GetColorU32(palette().text), "SubTree  Move");
    const float y = at.y + line + ui(12);
    draw->AddText({at.x + ui(8), y}, ImGui::GetColorU32(directionColor(core::PortDirection::Input)), "IN");
    draw->AddText({at.x + ui(36), y}, ImGui::GetColorU32(palette().muted), "x");
    const ImVec4 ink = right ? palette().accent : palette().error;
    draw->AddText({b.x - ui(8) - ImGui::CalcTextSize(value).x, y}, ImGui::GetColorU32(ink), value);
    ImGui::Dummy({width, b.y - at.y});
    // A check or a cross, then what it does.
    const float r = line * .35f, cx = b.x + ui(12) + r, cy = at.y + (b.y - at.y) / 2;
    const ImU32 mark = ImGui::GetColorU32(ink);
    if (right) {
        const ImVec2 tick[] = {{cx - r, cy}, {cx - r * .3f, cy + r * .7f}, {cx + r, cy - r * .8f}};
        draw->AddPolyline(tick, 3, mark, 0, ui(2));
    } else {
        draw->AddLine({cx - r, cy - r}, {cx + r, cy + r}, mark, ui(2));
        draw->AddLine({cx - r, cy + r}, {cx + r, cy - r}, mark, ui(2));
    }
    ImGui::SameLine(0, ui(12) + 2 * r + ui(8));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (b.y - at.y - line) / 2);
    ImGui::TextColored(ink, "%s", note);
}
} // namespace

void drawTour(Session &session) {
    const auto &all = pages();
    // Opened once per request. A dialog that needs the screen (unsaved edits, a file changed on disk) replaces it;
    // then it stays closed instead of taking the screen back.
    static bool asked = false;
    if (session.showTour && !asked) {
        ImGui::OpenPopup("Getting started###tour");
        asked = true;
    } else if (asked && !ImGui::IsPopupOpen("Getting started###tour")) {
        asked = false;
        session.showTour = false;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(ui(760), display.x * .9f), std::min(ui(560), display.y * .9f)},
                             ImGuiCond_Appearing);
    ImGui::SetNextWindowPos({display.x / 2, display.y / 2}, ImGuiCond_Appearing, {.5f, .5f});
    if (!ImGui::BeginPopupModal("Getting started###tour", nullptr, ImGuiWindowFlags_NoSavedSettings))
        return;
    const auto close = [&] {
        session.showTour = false;
        session.tourPage = 0;
        if (session.tourDone)
            session.tourDone();
        session.focusCanvas = true;
        ImGui::CloseCurrentPopup();
    };
    session.tourPage = std::clamp(session.tourPage, 0, int(all.size()) - 1);
    const auto &page = all[std::size_t(session.tourPage)];
    const bool last = session.tourPage + 1 == int(all.size());

    // The page: title, text, the sketch, then its moves as mouse / keys.
    const float footer = ImGui::GetFrameHeightWithSpacing() + ui(14);
    ImGui::BeginChild("##page", {0, -footer});
    ImGui::PushFont(typeRamp().number);
    ImGui::TextColored(palette().accent, "%s", page.title);
    ImGui::PopFont();
    ImGui::Spacing();
    for (const char *paragraph : page.text) {
        ImGui::TextWrapped("%s", paragraph);
        ImGui::Spacing();
    }
    if (page.sketch == Sketch::Keys) {
        sketchCard("{target_x}", "passes the key target_x", true);
        sketchCard("target_x", "passes the text \"target_x\"", false);
        ImGui::Spacing();
    }
    if (!page.moves.empty() &&
        ImGui::BeginTable("##moves", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("To", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("With the mouse", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("With the keys", ImGuiTableColumnFlags_WidthStretch, 1);
        tableHeaders();
        for (const auto &move : page.moves) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", move.what);
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", move.mouse);
            ImGui::TableNextColumn();
            keys(move.keys);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    // Page dots, then Skip / Back / Next (Done on the last page). The arrows and Esc work too.
    ImGui::Separator();
    ImGui::Spacing();
    const float dot = ui(4);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    for (int i = 0; i < int(all.size()); ++i)
        ImGui::GetWindowDrawList()->AddCircleFilled(
            {at.x + dot + float(i) * dot * 3.5f, at.y + ImGui::GetFrameHeight() / 2}, dot,
            ImGui::GetColorU32(i == session.tourPage ? palette().accent : withAlpha(palette().muted, .5f)));
    ImGui::Dummy({float(all.size()) * dot * 3.5f, ImGui::GetFrameHeight()});
    ImGui::SameLine();
    ImGui::TextDisabled("%d / %zu", session.tourPage + 1, all.size());
    const float buttons = ui(80) * 3 + ImGui::GetStyle().ItemSpacing.x * 2;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - buttons);
    if (ImGui::Button(last ? "Close" : "Skip", {ui(80), 0}))
        close();
    ImGui::SameLine();
    ImGui::BeginDisabled(session.tourPage == 0);
    if (ImGui::Button("Back", {ui(80), 0}))
        --session.tourPage;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(last ? "Done" : "Next", {ui(80), 0})) {
        if (last)
            close();
        else
            ++session.tourPage;
    }
    if (!ImGui::IsWindowAppearing()) {
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && !last)
            ++session.tourPage;
        else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && session.tourPage > 0)
            --session.tourPage;
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            close();
    }
    ImGui::EndPopup();
}
} // namespace notgroot::ui
