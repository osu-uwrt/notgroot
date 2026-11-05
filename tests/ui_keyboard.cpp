// The editor driven by the keyboard alone, headless: a real ImGui context and the real panels, with key presses and
// typed text injected frame by frame, checked against the files that come out.
#include "notgroot/ui/panels.hpp"
#include "notgroot/ui/style.hpp"
#include "notgroot/ui/theme.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <gtest/gtest.h>
#include <imgui_internal.h>
#include <thread>

using namespace notgroot;
using notgroot::test::ScratchCopy;

namespace {
class Harness : public ::testing::Test {
  protected:
    void SetUp() override {
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1600, 1000};
        io.DeltaTime = 1.f / 60;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.Fonts->AddFontDefault();
        unsigned char *pixels = nullptr;
        int w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        ui::applyTheme(ui::themes().front().id);
        std::string error;
        ASSERT_TRUE(session.loadBuiltinModels(NOTGROOT_BUILTIN_MODELS, error)) << error;
        ASSERT_TRUE(session.open(copy.path(), error)) << error;
        session.openTree("Toy.xml");
        frames(3);
    }
    void TearDown() override {
        ImGui::DestroyContext();
    }

    // One frame of the editor: the canvas and inspector windows side by side, then the floating parts.
    void frame() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({1100, 1000});
        if (ImGui::Begin("Canvas", nullptr, ImGuiWindowFlags_NoScrollbar))
            ui::drawCanvas(session);
        ImGui::End();
        ImGui::SetNextWindowPos({1100, 0});
        ImGui::SetNextWindowSize({500, variablesPanel ? 450.f : 1000.f});
        if (palettePanel) {
            if (ImGui::Begin("Palette"))
                ui::drawPalette(session);
        } else if (ImGui::Begin("Inspector"))
            ui::drawInspector(session);
        ImGui::End();
        if (variablesPanel) {
            ImGui::SetNextWindowPos({1100, 450});
            ImGui::SetNextWindowSize({500, 550});
            if (ImGui::Begin("Variables"))
                ui::drawVariables(session);
            ImGui::End();
        }
        ui::drawFinder(session);
        ui::drawPicker(session);
        ui::drawDialogs(session);
        ui::drawShortcuts(session);
        ui::drawTour(session);
        ui::handleShortcuts(session);
        ImGui::Render();
        session.problems();
    }
    void frames(int n) {
        for (int i = 0; i < n; ++i)
            frame();
    }
    void press(ImGuiKey key, ImGuiKeyChord mods = 0) {
        auto &io = ImGui::GetIO();
        const auto modifiers = [&](bool down) {
            if (mods & ImGuiMod_Ctrl)
                io.AddKeyEvent(ImGuiMod_Ctrl, down);
            if (mods & ImGuiMod_Shift)
                io.AddKeyEvent(ImGuiMod_Shift, down);
            if (mods & ImGuiMod_Alt)
                io.AddKeyEvent(ImGuiMod_Alt, down);
        };
        modifiers(true);
        io.AddKeyEvent(key, true);
        frame();
        io.AddKeyEvent(key, false);
        modifiers(false);
        frames(2);
    }
    void type(const std::string &text) {
        ImGui::GetIO().AddInputCharactersUTF8(text.c_str());
        frames(2);
    }
    core::Element *at(const core::NodeRef &ref) {
        return core::resolve(session.workspace, ref);
    }
    bool variablesPanel = false;
    bool palettePanel = false; // the palette where the inspector is

    // Like hover, for an item whose id needs the window's (a pushed id, a child): `id` makes it from the window's id.
    bool hoverId(const char *window, const std::function<ImGuiID(ImGuiID)> &id, float x) {
        ImGuiWindow *found = nullptr;
        for (auto *w : GImGui->Windows)
            if (std::string(w->Name).find(window) != std::string::npos)
                found = w;
        if (!found)
            return false;
        const ImGuiID target = id(found->ID);
        for (float y = found->Pos.y; y < found->Pos.y + found->Size.y; y += 3) {
            ImGui::GetIO().AddMousePosEvent(found->Pos.x + x, y);
            frame();
            if (GImGui->HoveredId == target)
                return true;
        }
        return false;
    }
    // Whether a window with that in its name was drawn last frame.
    bool shown(const char *window) {
        for (auto *w : GImGui->Windows)
            if (w->WasActive && std::string(w->Name).find(window) != std::string::npos)
                return true;
        return false;
    }
    void clickAt(ImVec2 at) {
        ImGui::GetIO().AddMousePosEvent(at.x, at.y);
        frame();
        click();
    }
    void doubleClickAt(ImVec2 at) {
        clickAt(at);
        click();
    }

    // A card's box on the canvas (screen coordinates), found by clicking across the canvas until the cursor lands on
    // it. The view is put back after each click (a click can open a call or unlock a band), and no two clicks make a
    // double-click.
    bool findCard(const ui::CardKey &key, ImVec2 &lo, ImVec2 &hi) {
        auto &io = ImGui::GetIO();
        const float doubleClick = io.MouseDoubleClickTime;
        io.MouseDoubleClickTime = 0;
        const auto saved = session.view(session.active);
        const auto lands = [&](float x, float y) {
            session.cursor = {{session.active, {}}, {}}; // a click on empty canvas leaves it there
            io.AddMousePosEvent(x, y);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            frame();
            auto &view = session.view(session.active);
            view.opened = saved.opened;
            view.openedInside = saved.openedInside;
            view.unlocked = saved.unlocked;
            view.pan = saved.pan;
            view.zoom = saved.zoom;
            return session.cursor == key;
        };
        bool found = false;
        for (float y = 60; y < 990 && !found; y += 12)
            for (float x = 10; x < 1090 && !found; x += 30)
                if (lands(x, y)) {
                    lo = hi = {x, y};
                    found = true;
                }
        if (found) {
            while (lo.x > 0 && lands(lo.x - 2, lo.y))
                lo.x -= 2;
            while (hi.x < 1100 && lands(hi.x + 2, hi.y))
                hi.x += 2;
            const float mid = (lo.x + hi.x) / 2;
            lo.x = hi.x = mid;
            while (lo.y > 0 && lands(mid, lo.y - 2))
                lo.y -= 2;
            while (hi.y < 1000 && lands(mid, hi.y + 2))
                hi.y += 2;
            float left = mid, right = mid;
            while (left > 0 && lands(left - 2, (lo.y + hi.y) / 2))
                left -= 2;
            while (right < 1100 && lands(right + 2, (lo.y + hi.y) / 2))
                right += 2;
            lo.x = left;
            hi.x = right;
        }
        io.MouseDoubleClickTime = doubleClick;
        frames(int(doubleClick / io.DeltaTime) + 2); // the last probe can't pair with the next click
        return found;
    }
    // Drags from where the mouse is to `to` with the left button, in steps.
    void dragTo(ImVec2 to) {
        auto &io = ImGui::GetIO();
        const ImVec2 from = io.MousePos;
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame();
        for (int i = 1; i <= 12; ++i) {
            io.AddMousePosEvent(from.x + (to.x - from.x) * float(i) / 12, from.y + (to.y - from.y) * float(i) / 12);
            frame();
        }
        frames(2);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frames(3);
    }

    // Moves the mouse down a column of the window that holds `label` until that item is under it; false if none is.
    bool hover(const char *window, const char *label, float x) {
        ImGuiWindow *found = nullptr;
        for (auto *w : GImGui->Windows)
            if (std::string(w->Name).find(window) != std::string::npos && (!found || std::string(found->Name) != window))
                found = w;
        if (!found)
            return false;
        const ImGuiID id = ImHashStr(label, 0, found->ID);
        for (float y = found->Pos.y; y < found->Pos.y + found->Size.y; y += 3) {
            ImGui::GetIO().AddMousePosEvent(found->Pos.x + x, y);
            frame();
            if (GImGui->HoveredId == id)
                return true;
        }
        return false;
    }
    void click() {
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame();
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frames(2);
    }

    std::string cursorId() {
        auto *element = at(session.cursor.ref);
        return element ? (session.cursor.ref.isTree() ? "<tree>" : core::nodeId(*element)) : "<none>";
    }

    ScratchCopy copy{"toy"};
    ui::Session session;
};
} // namespace

TEST_F(Harness, NavigateAddEditUndoWithKeysOnly) {
    EXPECT_EQ(session.active, "Toy.xml");
    EXPECT_EQ(cursorId(), "<tree>");
    press(ImGuiKey_DownArrow);
    EXPECT_EQ(cursorId(), "Sequence");
    press(ImGuiKey_DownArrow);
    EXPECT_EQ(cursorId(), "SetBlackboardV3");
    press(ImGuiKey_RightArrow);
    EXPECT_EQ(cursorId(), "Info");
    press(ImGuiKey_UpArrow);
    EXPECT_EQ(cursorId(), "Sequence");

    // Tab: the picker; type and Enter adds a Wait as the Sequence's last child, then the inspector takes its port.
    press(ImGuiKey_Tab);
    ASSERT_TRUE(session.picker.open);
    type("Wait");
    press(ImGuiKey_Enter);
    EXPECT_FALSE(session.picker.open);
    EXPECT_EQ(cursorId(), "Wait");
    const auto wait = session.cursor.ref;
    frames(2);
    type("2.5");
    press(ImGuiKey_Enter);
    EXPECT_EQ(at(wait)->get("seconds"), "2.5");

    // Back on the canvas: undo twice takes the value, then the node.
    frames(2);
    press(ImGuiKey_Z, ImGuiMod_Ctrl);
    EXPECT_EQ(at(wait)->get("seconds"), "");
    press(ImGuiKey_Z, ImGuiMod_Ctrl);
    EXPECT_FALSE(at(wait) && core::nodeId(*at(wait)) == "Wait");
    press(ImGuiKey_Z, ImGuiMod_Ctrl | ImGuiMod_Shift);
    EXPECT_TRUE(at(wait) && core::nodeId(*at(wait)) == "Wait");
}

TEST_F(Harness, FinderOpensATreeAndWrapReplacesInPlace) {
    press(ImGuiKey_P, ImGuiMod_Ctrl);
    ASSERT_TRUE(session.showFinder);
    type("Settle");
    press(ImGuiKey_Enter);
    EXPECT_FALSE(session.showFinder);
    EXPECT_EQ(session.active, "Settle");

    frames(2);
    press(ImGuiKey_DownArrow);
    EXPECT_EQ(cursorId(), "RetryUntilSuccessfulV3");
    press(ImGuiKey_W, ImGuiMod_Ctrl);
    ASSERT_TRUE(session.picker.open);
    type("Inverter");
    press(ImGuiKey_Enter);
    EXPECT_EQ(cursorId(), "Inverter");
    auto *inverter = at(session.cursor.ref);
    ASSERT_EQ(core::nodeChildren(*inverter).size(), 1u);
    EXPECT_EQ(core::nodeId(*core::nodeChildren(*inverter)[0]), "RetryUntilSuccessfulV3");
}

TEST_F(Harness, MoveDeleteAndSave) {
    press(ImGuiKey_DownArrow);
    press(ImGuiKey_DownArrow);
    press(ImGuiKey_RightArrow); // Info
    EXPECT_EQ(cursorId(), "Info");
    press(ImGuiKey_RightArrow, ImGuiMod_Alt); // later among siblings
    auto *sequence = at(session.cursor.ref.parent());
    EXPECT_EQ(core::nodeId(*core::nodeChildren(*sequence)[2]), "Info");
    press(ImGuiKey_Delete);
    for (auto *child : core::nodeChildren(*sequence))
        EXPECT_NE(core::nodeId(*child), "Info");

    press(ImGuiKey_S, ImGuiMod_Ctrl);
    std::string text;
    ASSERT_TRUE(core::readFile(copy.path() / "Toy.xml", text));
    EXPECT_EQ(text.find("<Info"), std::string::npos);
    EXPECT_FALSE(session.dirty());
}

TEST_F(Harness, ExtractSelectionAndRenameKey) {
    session.openTree("Move");
    frames(2);
    press(ImGuiKey_DownArrow);                  // Sequence
    press(ImGuiKey_DownArrow);                  // GetOdometry
    press(ImGuiKey_RightArrow, ImGuiMod_Shift); // + Math
    ASSERT_EQ(session.selection.size(), 2u);
    press(ImGuiKey_E, ImGuiMod_Ctrl);
    ASSERT_EQ(session.dialog.kind, ui::Session::Dialog::ExtractSubtree);
    frames(2);
    type("Offset");
    press(ImGuiKey_Enter);
    EXPECT_EQ(session.dialog.kind, ui::Session::Dialog::None);
    ASSERT_TRUE(session.workspace.tree("Offset"));
    EXPECT_EQ(cursorId(), "Offset"); // the new call, by its ID

    // F2 with a highlighted key renames it across the tree.
    session.highlightKey = "x";
    press(ImGuiKey_F2);
    ASSERT_EQ(session.dialog.kind, ui::Session::Dialog::RenameKey);
    frames(2);
    press(ImGuiKey_A, ImGuiMod_Ctrl);
    type("goal_x");
    press(ImGuiKey_Enter);
    EXPECT_TRUE(session.workspace.catalog().find("Move")->port("goal_x"));
}

TEST_F(Harness, NodeMenuOffersOnlyWhatApplies) {
    const auto applies = [&](const char *id) { return ui::Session::applies(*session.command(id)); };
    const core::NodeRef sequence{"Toy.xml", {0}};
    session.setCursor(sequence.child(2)); // Info: a leaf, not a call
    EXPECT_FALSE(applies("node.definition"));
    EXPECT_FALSE(applies("tree.ports"));
    EXPECT_FALSE(applies("node.child"));
    EXPECT_FALSE(applies("node.unwrap"));
    EXPECT_TRUE(applies("edit.delete"));
    session.setCursor(sequence.child(3)); // SubTree Move
    EXPECT_TRUE(applies("node.definition"));
    EXPECT_TRUE(applies("tree.ports"));
    session.setCursor(sequence);
    EXPECT_TRUE(applies("node.unwrap"));
    EXPECT_TRUE(applies("node.child"));
    session.setCursor(core::NodeRef{"Toy.xml", {}});
    EXPECT_FALSE(applies("edit.delete"));
    EXPECT_TRUE(applies("node.child"));

    // Wrap needs one parent: grayed, with the reason, for nodes from two levels.
    session.selection = {sequence.child(2), sequence.child(4).child(0)};
    EXPECT_FALSE(ui::Session::enabled(*session.command("node.wrap")));
    EXPECT_FALSE(session.command("node.wrap")->why().empty());
}

TEST_F(Harness, SelectSiblingsTreeAndType) {
    const core::NodeRef sequence{"Toy.xml", {0}};
    session.setCursor(sequence.child(2));
    frames(2);
    press(ImGuiKey_A, ImGuiMod_Ctrl);
    EXPECT_EQ(session.selection.size(), 5u); // the Sequence's children
    press(ImGuiKey_A, ImGuiMod_Ctrl);
    EXPECT_EQ(session.selection.size(), 8u); // every node of the tree
    session.setCursor(sequence.child(3));
    frames(2);
    press(ImGuiKey_L, ImGuiMod_Ctrl | ImGuiMod_Shift);
    EXPECT_EQ(session.selection, (std::vector<core::NodeRef>{sequence.child(3), sequence.child(5)}));
    press(ImGuiKey_A, ImGuiMod_Ctrl | ImGuiMod_Shift);
    EXPECT_EQ(session.selection, std::vector<core::NodeRef>{sequence.child(3)});

    // Ctrl+arrows move the cursor and keep the selection; Ctrl+Space adds the node.
    press(ImGuiKey_RightArrow, ImGuiMod_Ctrl);
    EXPECT_EQ(cursorId(), "Fallback");
    press(ImGuiKey_Space, ImGuiMod_Ctrl);
    EXPECT_EQ(session.selection.size(), 2u);
}

// Every command's key is written down in docs/keys.md.
TEST_F(Harness, KeysDocListsEveryShortcut) {
    std::ifstream file(NOTGROOT_KEYS_DOC);
    ASSERT_TRUE(file) << NOTGROOT_KEYS_DOC;
    const std::string doc((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    for (const auto &command : session.commands) {
        if (command.chord) {
            EXPECT_NE(doc.find(ui::chordName(command.chord)), std::string::npos)
                << command.id << " (" << ui::chordName(command.chord) << ") is missing from docs/keys.md";
        }
    }
}

// A call inside a subtree opened in place opens too: Move's call to Settle, then Settle's own nodes.
TEST_F(Harness, SubtreesOpenInsideOpenedSubtrees) {
    session.setCursor(core::NodeRef{"Toy.xml", {0, 3}}); // SubTree Move
    frames(2);
    press(ImGuiKey_Space);
    press(ImGuiKey_DownArrow);
    EXPECT_EQ(cursorId(), "Sequence");
    EXPECT_TRUE(session.readOnly(session.cursor));
    press(ImGuiKey_DownArrow);
    press(ImGuiKey_RightArrow);
    press(ImGuiKey_RightArrow);
    ASSERT_EQ(session.cursor.ref, (core::NodeRef{"Move", {0, 2}})); // SubTree Settle, in Move
    press(ImGuiKey_Space);
    press(ImGuiKey_DownArrow);
    EXPECT_EQ(cursorId(), "RetryUntilSuccessfulV3");
    EXPECT_EQ(session.cursor.via, std::vector<core::NodeRef>{(core::NodeRef{"Move", {0, 2}})});
    press(ImGuiKey_UpArrow);
    press(ImGuiKey_Space); // closes Settle again
    press(ImGuiKey_DownArrow);
    EXPECT_NE(cursorId(), "RetryUntilSuccessfulV3");
}

// Alt+Down into a leaf floats the node; saving is refused (and names the tree) until it moves on.
TEST_F(Harness, FloatingNodesBlockSaving) {
    press(ImGuiKey_DownArrow);
    press(ImGuiKey_DownArrow);
    press(ImGuiKey_RightArrow); // Info, after the SetBlackboardV3 leaf
    ASSERT_EQ(cursorId(), "Info");
    press(ImGuiKey_DownArrow, ImGuiMod_Alt);
    EXPECT_EQ(cursorId(), "Info");
    EXPECT_EQ(session.cursor.ref.path.size(), 3u); // under the setter
    ASSERT_EQ(core::floatingNodes(session.workspace, "Toy.xml").size(), 1u);

    std::string before;
    ASSERT_TRUE(core::readFile(copy.path() / "Toy.xml", before));
    press(ImGuiKey_S, ImGuiMod_Ctrl);
    EXPECT_EQ(session.dialog.kind, ui::Session::Dialog::Floating);
    EXPECT_EQ(session.floating.front().tree, "Toy.xml");
    std::string after;
    ASSERT_TRUE(core::readFile(copy.path() / "Toy.xml", after));
    EXPECT_EQ(after, before);
    press(ImGuiKey_Escape);
    frames(3);

    // Alt+Up lands it back beside the setter; now it saves.
    press(ImGuiKey_UpArrow, ImGuiMod_Alt);
    EXPECT_TRUE(core::floatingNodes(session.workspace, "Toy.xml").empty());
    press(ImGuiKey_S, ImGuiMod_Ctrl);
    EXPECT_EQ(session.dialog.kind, ui::Session::Dialog::None);
    EXPECT_FALSE(session.dirty());
}

// The keyboard guide closes with Esc, or a click outside it.
TEST_F(Harness, ShortcutsCloseWithEscOrClickOutside) {
    press(ImGuiKey_F1);
    EXPECT_TRUE(session.showShortcuts);
    frames(2);
    press(ImGuiKey_Escape);
    EXPECT_FALSE(session.showShortcuts);

    press(ImGuiKey_F1);
    frames(2);
    auto &io = ImGui::GetIO();
    io.AddMousePosEvent(5, 995); // a corner of the canvas, away from the centered guide
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frames(2);
    EXPECT_FALSE(session.showShortcuts);
}

// Make global from the node menu: the setter leaves the canvas, the key is a global.
TEST_F(Harness, MakeGlobalFromTheNodeMenu) {
    session.setCursor(core::NodeRef{"Toy.xml", {0, 1}}); // SetBlackboardV3 depth
    frames(2);
    ASSERT_TRUE(ui::Session::applies(*session.command("node.makeGlobal")));
    session.run("node.makeGlobal");
    const auto &vars = session.variables("Toy.xml");
    EXPECT_EQ(vars.keys.at("depth").scope, core::Scope::Global);
    EXPECT_EQ(core::readGlobals(*session.workspace.tree("Toy.xml")->tree).globals,
              (std::vector<core::Global>{{"depth", "-1.5"}}));
    frames(2);
    session.setCursor(core::NodeRef{"Toy.xml", {1, 1}}); // the Sequence, now in the wrapper
    frames(2);
    EXPECT_EQ(cursorId(), "Sequence");
    EXPECT_FALSE(ui::Session::applies(*session.command("node.makeGlobal")));
}

// Key colors in one tree stay apart in every theme: the full distance for up to 20 keys, and still apart for 47 (the
// most any tree has); the same keys get the same colors, whatever their order.
TEST_F(Harness, KeyColorsStayApartInEveryTheme) {
    ui::loadThemes(NOTGROOT_THEMES);
    const auto keys = [](int n) {
        std::vector<std::string> list;
        for (int i = 0; i < n; ++i)
            list.push_back("key_" + std::to_string(i * 7919 % 1000));
        return list;
    };
    const auto least = [](const std::map<std::string, ImVec4> &colors) {
        float apart = 1e9f;
        for (auto a = colors.begin(); a != colors.end(); ++a)
            for (auto b = std::next(a); b != colors.end(); ++b)
                apart = std::min(apart, ui::colorDistance(a->second, b->second));
        return apart;
    };
    for (const auto &theme : ui::themes()) {
        ASSERT_TRUE(ui::applyTheme(theme.id));
        EXPECT_GE(least(ui::keyColors(keys(20))), ui::keyColorDistance) << theme.id;
        EXPECT_GE(least(ui::keyColors(keys(47))), 4.f) << theme.id;
    }
    auto reversed = keys(30);
    std::reverse(reversed.begin(), reversed.end());
    const auto a = ui::keyColors(keys(30)), b = ui::keyColors(reversed);
    for (const auto &[key, color] : a)
        EXPECT_EQ(ui::colorDistance(color, b.at(key)), 0.f) << key;
}

// Adding a global from the Variables panel with the mouse: the panel goes on drawing after the edit (a regression:
// it kept pointers into keys the edit had replaced).
TEST_F(Harness, AddGlobalFromThePanel) {
    variablesPanel = true;
    session.openTree("Move");
    frames(3);
    ASSERT_TRUE(hover("##sections", "##newname", 40));
    click();
    type("speed");
    ASSERT_TRUE(hover("##sections", "+", 470));
    click();
    frames(3);
    EXPECT_EQ(core::readGlobals(*session.workspace.tree("Move")->tree).globals,
              (std::vector<core::Global>{{"speed", ""}}));
    EXPECT_EQ(session.variables("Move").keys.at("speed").scope, core::Scope::Global);
}

TEST_F(Harness, OpeningAsksBeforeDroppingEdits) {
    bool opened = false;
    session.confirmThen([&] { opened = true; });
    EXPECT_TRUE(opened); // nothing edited yet

    opened = false;
    press(ImGuiKey_DownArrow);
    press(ImGuiKey_Delete);
    ASSERT_TRUE(session.dirty());
    session.confirmThen([&] { opened = true; });
    frames(3);
    EXPECT_FALSE(opened);
    EXPECT_EQ(session.dialog.kind, ui::Session::Dialog::Unsaved);
    press(ImGuiKey_Escape);
    EXPECT_FALSE(opened);
    EXPECT_TRUE(session.dirty());
}

TEST_F(Harness, FailedOpenKeepsTheWorkspaceAndUndo) {
    press(ImGuiKey_DownArrow);
    press(ImGuiKey_Delete);
    ASSERT_TRUE(session.history.canUndo());

    std::string error;
    EXPECT_FALSE(session.open(copy.path() / "gone", error));
    EXPECT_FALSE(session.workspace.trees().empty());
    EXPECT_TRUE(session.history.canUndo());
    press(ImGuiKey_Z, ImGuiMod_Ctrl);
    EXPECT_FALSE(session.dirty());
}

TEST_F(Harness, InspectorFieldCommitsWhenItLosesFocus) {
    press(ImGuiKey_DownArrow);
    press(ImGuiKey_Tab);
    type("Wait");
    press(ImGuiKey_Enter);
    ASSERT_EQ(cursorId(), "Wait");
    const auto wait = session.cursor.ref;
    frames(2);
    type("3.5");
    EXPECT_EQ(at(wait)->get("seconds"), "");

    // A click on the empty canvas ends the edit.
    ImGui::GetIO().AddMousePosEvent(50, 950);
    frames(2);
    click();
    EXPECT_EQ(at(wait)->get("seconds"), "3.5");
}

TEST_F(Harness, InspectorFieldCommitsOnTab) {
    press(ImGuiKey_DownArrow);
    press(ImGuiKey_Tab);
    type("Wait");
    press(ImGuiKey_Enter);
    ASSERT_EQ(cursorId(), "Wait");
    const auto wait = session.cursor.ref;
    frames(2);
    type("3.5");
    press(ImGuiKey_Tab);
    EXPECT_EQ(at(wait)->get("seconds"), "3.5");
}

TEST_F(Harness, SaveFromTheUnsavedDialogShowsTheFloatingDialog) {
    press(ImGuiKey_DownArrow);
    press(ImGuiKey_DownArrow);
    press(ImGuiKey_RightArrow);
    press(ImGuiKey_DownArrow, ImGuiMod_Alt); // Info floats
    ASSERT_EQ(core::floatingNodes(session.workspace, "Toy.xml").size(), 1u);

    session.confirmThen([] {});
    frames(3);
    ASSERT_EQ(session.dialog.kind, ui::Session::Dialog::Unsaved);
    // Click down the left edge of the dialog until the Save button gives.
    ASSERT_NE(ImGui::FindWindowByName("Unsaved changes"), nullptr);
    const ImVec2 corner = ImGui::FindWindowByName("Unsaved changes")->Pos;
    for (float y = corner.y + 20; y < corner.y + 120 && session.dialog.kind == ui::Session::Dialog::Unsaved; y += 4) {
        ImGui::GetIO().AddMousePosEvent(corner.x + 20, y);
        frames(2);
        click();
    }
    frames(3);
    EXPECT_EQ(session.dialog.kind, ui::Session::Dialog::Floating);
    EXPECT_NE(ImGui::FindWindowByName("Can't save: floating nodes"), nullptr);
}

TEST_F(Harness, SavingWhileACheckRunsChecksAgain) {
    // A stand-in checker that logs each run.
    const auto log = copy.path() / "runs.log";
    const auto script = copy.path() / "check.sh";
    std::ofstream(script) << "#!/bin/sh\nsleep 0.3\necho run >> " << log.string() << "\n";
    std::filesystem::permissions(script, std::filesystem::perms::owner_all);
    session.setChecker(script);

    press(ImGuiKey_DownArrow);
    press(ImGuiKey_DownArrow);
    press(ImGuiKey_Delete);
    press(ImGuiKey_S, ImGuiMod_Ctrl);
    ASSERT_TRUE(session.checking());
    press(ImGuiKey_Delete);
    press(ImGuiKey_S, ImGuiMod_Ctrl);

    const auto runs = [&] {
        std::string text;
        core::readFile(log, text);
        return std::count(text.begin(), text.end(), '\n');
    };
    for (int i = 0; i < 100 && (session.checking() || runs() < 2); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        session.pollCheck();
    }
    EXPECT_EQ(runs(), 2);
}

TEST_F(Harness, TabsOfTreesThatUndoRemovedClose) {
    ASSERT_FALSE(session.editor.createSubtree("Fresh").empty());
    session.openTree("Fresh");
    frames(3);
    ASSERT_NE(std::find(session.tabs.begin(), session.tabs.end(), "Fresh"), session.tabs.end());
    ASSERT_TRUE(session.history.undo());
    frames(3);
    EXPECT_EQ(std::find(session.tabs.begin(), session.tabs.end(), "Fresh"), session.tabs.end());
    EXPECT_EQ(session.active, "Toy.xml");
}

TEST_F(Harness, DownReturnsToTheChildYouCameUpFrom) {
    press(ImGuiKey_DownArrow);
    press(ImGuiKey_DownArrow);
    press(ImGuiKey_RightArrow);
    ASSERT_EQ(cursorId(), "Info");
    press(ImGuiKey_UpArrow);
    ASSERT_EQ(cursorId(), "Sequence");
    press(ImGuiKey_DownArrow);
    EXPECT_EQ(cursorId(), "Info");
}

// Backspace deletes on the canvas as Delete does, and stays Backspace in a text field.
TEST_F(Harness, BackspaceDeletesNodesButNotSearchText) {
    press(ImGuiKey_DownArrow);
    press(ImGuiKey_DownArrow);
    ASSERT_EQ(cursorId(), "SetBlackboardV3");
    const auto sequence = core::NodeRef{"Toy.xml", {0}};
    const auto before = core::nodeChildren(*at(sequence)).size();
    press(ImGuiKey_Backspace);
    EXPECT_EQ(core::nodeChildren(*at(sequence)).size(), before - 1);
    press(ImGuiKey_F, ImGuiMod_Ctrl);
    type("Info");
    press(ImGuiKey_Backspace);
    EXPECT_EQ(session.search, "Inf");
    EXPECT_EQ(core::nodeChildren(*at(sequence)).size(), before - 1);
}

// Ctrl+F while the search bar is open but the canvas has the keys: typing goes straight back into the search.
TEST_F(Harness, CtrlFAgainRefocusesTheSearch) {
    press(ImGuiKey_F, ImGuiMod_Ctrl);
    type("Info");
    clickAt({50, 950});
    EXPECT_FALSE(ImGui::GetIO().WantTextInput);
    press(ImGuiKey_F, ImGuiMod_Ctrl);
    frames(2);
    EXPECT_TRUE(ImGui::GetIO().WantTextInput);
    type("Wait");
    EXPECT_EQ(session.search, "Wait"); // the old query was selected, so typing replaced it
}

// A subtree opened in place is locked; Ctrl+U unlocks it, edits there go to its own file, and the cursor stays in
// place. Locking again stops them.
TEST_F(Harness, UnlockedSubtreeEditsInPlace) {
    session.setCursor(core::NodeRef{"Toy.xml", {0, 3}}); // SubTree Move
    frames(2);
    press(ImGuiKey_Space);
    press(ImGuiKey_DownArrow);
    ASSERT_EQ(cursorId(), "Sequence");
    EXPECT_TRUE(session.readOnly(session.cursor));
    const auto moveBody = core::NodeRef{"Move", {0}};
    press(ImGuiKey_DownArrow);
    press(ImGuiKey_Delete);
    EXPECT_EQ(core::nodeChildren(*at(moveBody)).size(), 4u); // locked: nothing happens

    press(ImGuiKey_U, ImGuiMod_Ctrl);
    EXPECT_FALSE(session.readOnly(session.cursor));
    EXPECT_TRUE(session.unlocked("Move"));
    ASSERT_EQ(cursorId(), "GetOdometry");
    press(ImGuiKey_Delete);
    EXPECT_EQ(core::nodeChildren(*at(moveBody)).size(), 3u);
    EXPECT_TRUE(session.workspace.tree("Move")->document->dirty());
    EXPECT_FALSE(session.workspace.tree("Toy.xml")->document->dirty());
    EXPECT_TRUE(session.cursor.inside());

    // A node added there lands in Move, and the cursor follows it under the same call.
    press(ImGuiKey_Tab, ImGuiMod_Shift);
    type("AlwaysSuccess");
    press(ImGuiKey_Enter);
    EXPECT_EQ(cursorId(), "AlwaysSuccess");
    EXPECT_EQ(session.cursor.ref.tree, "Move");
    EXPECT_EQ(session.cursor.origin, (core::NodeRef{"Toy.xml", {0, 3}}));
    EXPECT_EQ(core::nodeChildren(*at(moveBody)).size(), 4u);

    press(ImGuiKey_Z, ImGuiMod_Ctrl);
    press(ImGuiKey_Z, ImGuiMod_Ctrl);
    EXPECT_EQ(core::nodeChildren(*at(moveBody)).size(), 4u);
    EXPECT_EQ(core::nodeId(*core::nodeChildren(*at(moveBody)).front()), "GetOdometry");

    press(ImGuiKey_U, ImGuiMod_Ctrl);
    EXPECT_TRUE(session.readOnly(session.cursor));
    // Locks are per view: the subtree's own tab never was.
    session.openTree("Move");
    EXPECT_FALSE(session.unlocked("Move"));
}

// Locking a subtree locks the ones under it, however deep, and leaves the ones above it alone.
TEST_F(Harness, LockingReachesTheSubtreesUnderIt) {
    session.toggleLock("Move");
    session.toggleLock("Settle"); // called by Move
    session.toggleLock("Settle");
    EXPECT_TRUE(session.unlocked("Move"));
    EXPECT_FALSE(session.unlocked("Settle"));

    session.toggleLock("Settle");
    session.toggleLock("Move");
    EXPECT_FALSE(session.unlocked("Move"));
    EXPECT_FALSE(session.unlocked("Settle"));
}

// Closing an opened subtree locks it and what is under it, even when it was the one still locked.
TEST_F(Harness, ClosingASubtreeLocksIt) {
    session.setCursor(core::NodeRef{"Toy.xml", {0, 3}}); // SubTree Move
    frames(2);
    press(ImGuiKey_Space);
    session.toggleLock("Move");
    session.toggleLock("Settle");
    press(ImGuiKey_Space);
    EXPECT_FALSE(session.unlocked("Move"));
    EXPECT_FALSE(session.unlocked("Settle"));

    press(ImGuiKey_Space);
    EXPECT_FALSE(session.unlocked("Move")); // and it opens locked
    session.toggleLock("Settle");
    press(ImGuiKey_Space);
    EXPECT_FALSE(session.unlocked("Settle"));
}

// The box of an opened subtree's globals has fields only while the subtree is unlocked; there it adds and removes.
TEST_F(Harness, GlobalsBoxEditsOnlyWhenUnlocked) {
    session.setCursor(core::NodeRef{"Toy.xml", {0, 3}});
    frames(2);
    press(ImGuiKey_Space);
    press(ImGuiKey_DownArrow);
    EXPECT_FALSE(hover("##globals_box", "##newname", 20)); // locked: shown, no fields
    press(ImGuiKey_U, ImGuiMod_Ctrl);
    frames(3); // the layout makes room for the fields, and the view settles
    ASSERT_TRUE(hover("##globals_box", "##newname", 20));
    click();
    type("gain");
    press(ImGuiKey_Enter);
    EXPECT_EQ(core::readGlobals(*session.workspace.tree("Move")->tree).globals,
              (std::vector<core::Global>{{"gain", ""}}));
    frames(2);
    ASSERT_TRUE(hoverId(
        "##globals_box", [](ImGuiID box) { return ImHashStr("x", 0, ImHashStr("gain", 0, box)); }, 305));
    click();
    EXPECT_TRUE(core::readGlobals(*session.workspace.tree("Move")->tree).globals.empty());
}

// The padlock on a call's corner is there only while it is open; a click there locks and unlocks it.
TEST_F(Harness, PadlockOnAnOpenedCall) {
    session.setCursor(core::NodeRef{"Toy.xml", {0, 3}}); // SubTree Move
    frames(2);
    const auto call = session.cursor;
    ImVec2 lo, hi;
    ASSERT_TRUE(findCard(call, lo, hi));
    clickAt({hi.x, lo.y});
    EXPECT_FALSE(session.unlocked("Move")); // closed: no padlock

    session.setCursor(call);
    press(ImGuiKey_Space);
    frames(3);
    ASSERT_TRUE(findCard(call, lo, hi));
    clickAt({hi.x, lo.y});
    EXPECT_TRUE(session.unlocked("Move"));
    clickAt({hi.x, lo.y});
    EXPECT_FALSE(session.unlocked("Move"));
}

// The count above a call: the trees that call it, each once.
TEST_F(Harness, TreesUsingCountsCallers) {
    EXPECT_EQ(session.treesUsing("Move"), 1);   // Toy, twice
    EXPECT_EQ(session.treesUsing("Settle"), 2); // Toy and Move
    EXPECT_EQ(session.treesUsing("Nothing"), 0);
}

// A port edited on its card: Enter keeps it, Esc drops it, and Backspace there edits text instead of deleting.
TEST_F(Harness, PortEditsOnItsCard) {
    press(ImGuiKey_DownArrow);
    press(ImGuiKey_DownArrow);
    ASSERT_EQ(cursorId(), "SetBlackboardV3");
    const auto setter = session.cursor.ref;
    session.inlineEdit = {session.cursor, "value", true, {}};
    frames(3);
    EXPECT_TRUE(ImGui::GetIO().WantTextInput);
    press(ImGuiKey_A, ImGuiMod_Ctrl);
    type("2.5x");
    press(ImGuiKey_Backspace);
    press(ImGuiKey_Enter);
    EXPECT_EQ(at(setter)->get("value"), "2.5");
    EXPECT_TRUE(session.inlineEdit.port.empty());
    EXPECT_EQ(cursorId(), "SetBlackboardV3");

    session.inlineEdit = {session.cursor, "value", true, {}};
    frames(3);
    type("9");
    press(ImGuiKey_Escape);
    EXPECT_EQ(at(setter)->get("value"), "2.5");
    EXPECT_TRUE(session.inlineEdit.port.empty());
}

// Double-clicking a port row edits it on the card; double-clicking a call's title still opens its tree.
TEST_F(Harness, DoubleClickARowEditsItAndACallTitleOpensIt) {
    const ui::CardKey setter{{"Toy.xml", {0, 1}}, {}};
    ImVec2 lo, hi;
    ASSERT_TRUE(findCard(setter, lo, hi));
    doubleClickAt({(lo.x + hi.x) / 2, hi.y - 12}); // the last row: value
    EXPECT_EQ(session.inlineEdit.port, "value");
    press(ImGuiKey_Escape);

    const ui::CardKey call{{"Toy.xml", {0, 3}}, {}};
    ASSERT_TRUE(findCard(call, lo, hi));
    doubleClickAt({(lo.x + hi.x) / 2, lo.y + 6});
    EXPECT_EQ(session.active, "Move");
}

// Double-clicking a node in a locked subtree opens that subtree's tab with the cursor on that node.
TEST_F(Harness, DoubleClickInsideASubtreeOpensItOnThatNode) {
    const ui::CardKey call{{"Toy.xml", {0, 3}}, {}};
    session.setCursor(call);
    frames(2);
    press(ImGuiKey_Space);
    press(ImGuiKey_DownArrow);
    press(ImGuiKey_DownArrow);
    ASSERT_EQ(cursorId(), "GetOdometry");
    const auto node = session.cursor;
    ImVec2 lo, hi;
    ASSERT_TRUE(findCard(node, lo, hi));
    doubleClickAt({(lo.x + hi.x) / 2, lo.y + 6});
    frames(5);
    EXPECT_EQ(session.active, "Move");
    EXPECT_EQ(session.cursor.ref, node.ref);
    EXPECT_FALSE(session.cursor.inside());
}

// Double-clicking a node's title names it on the card, and the field grows with the text. Clearing it takes the
// attribute away. A call's title still opens its tree (above).
TEST_F(Harness, DoubleClickATitleNamesTheNode) {
    const ui::CardKey setter{{"Toy.xml", {0, 1}}, {}};
    ImVec2 lo, hi;
    ASSERT_TRUE(findCard(setter, lo, hi));
    doubleClickAt({(lo.x + hi.x) / 2, lo.y + 6});
    EXPECT_EQ(session.inlineEdit.port, "name");
    frames(3);
    const auto field = [] {
        for (auto *w : GImGui->Windows)
            if (w->WasActive && std::string(w->Name).find("##inline_edit") != std::string::npos)
                return w;
        return static_cast<ImGuiWindow *>(nullptr);
    };
    ASSERT_NE(field(), nullptr);
    const float left = field()->Pos.x, width = field()->Size.x;
    type("a name long enough to outgrow the node it sits on");
    frames(3);
    EXPECT_EQ(session.inlineEdit.text, "a name long enough to outgrow the node it sits on");
    EXPECT_NEAR(field()->Pos.x, left, 1); // the card stays put while it grows
    EXPECT_GT(field()->Size.x, width);
    press(ImGuiKey_Enter);
    EXPECT_EQ(at(setter.ref)->get("name"), "a name long enough to outgrow the node it sits on");
    EXPECT_TRUE(session.inlineEdit.port.empty());

    session.inlineEdit = {setter, "name", true, {}};
    frames(3);
    press(ImGuiKey_A, ImGuiMod_Ctrl);
    press(ImGuiKey_Backspace);
    press(ImGuiKey_Enter);
    EXPECT_FALSE(at(setter.ref)->has("name"));
}

// A palette node let go on empty canvas is not added, and the status says why; on a node's middle it goes inside.
TEST_F(Harness, PaletteDropsExplainWhereTheyCantGo) {
    const ui::CardKey fallback{{"Toy.xml", {0, 4}}, {}};
    ImVec2 lo, hi;
    ASSERT_TRUE(findCard(fallback, lo, hi));
    palettePanel = true;
    frames(2);
    ASSERT_TRUE(hover("Palette", "##palette", 40));
    click();
    type("AlwaysSuccess");
    const auto item = [](ImGuiID list) { return ImHashStr("AlwaysSuccess", 0, ImHashStr("AlwaysSuccess", 0, list)); };
    ASSERT_TRUE(hoverId("##palette_list", item, 60));
    dragTo({50, 950});
    EXPECT_NE(session.statusText.find("Not added"), std::string::npos) << session.statusText;
    EXPECT_FALSE(session.workspace.tree("Toy.xml")->document->dirty());

    ASSERT_TRUE(hoverId("##palette_list", item, 60));
    dragTo({(lo.x + hi.x) / 2, (lo.y + hi.y) / 2});
    const auto children = core::nodeChildren(*at(fallback.ref));
    ASSERT_EQ(children.size(), 3u);
    EXPECT_EQ(core::nodeId(*children.back()), "AlwaysSuccess");
}

// A touchpad pinch zooms by steps at the pointer.
TEST_F(Harness, PinchZooms) {
    ImGui::GetIO().AddMousePosEvent(550, 500);
    frames(2);
    const float before = session.view(session.active).zoom;
    session.pinch = {true, 1.f};
    frame();
    session.pinch.scale = 1.2f;
    frame();
    session.pinch.scale = 1.42f;
    frame();
    session.pinch = {};
    frame();
    EXPECT_FLOAT_EQ(session.view(session.active).zoom, ui::stepZoom(ui::stepZoom(before, 1), 1));
}


// The tour opens from Help, pages with the arrows and its buttons, and closes on Esc, remembering it was seen.
TEST_F(Harness, TourPagesAndCloses) {
    int seen = 0;
    session.tourDone = [&] { ++seen; };
    session.run("help.tour");
    frames(3);
    EXPECT_TRUE(shown("###tour"));
    press(ImGuiKey_RightArrow);
    press(ImGuiKey_RightArrow);
    EXPECT_EQ(session.tourPage, 2);
    press(ImGuiKey_LeftArrow);
    EXPECT_EQ(session.tourPage, 1);
    ASSERT_TRUE(hover("Getting started###tour", "Next", 700));
    click();
    EXPECT_EQ(session.tourPage, 2);
    // The canvas keys don't reach the tree behind it.
    const auto cursor = session.cursor;
    press(ImGuiKey_DownArrow);
    EXPECT_EQ(session.cursor, cursor);
    press(ImGuiKey_Escape);
    EXPECT_FALSE(session.showTour);
    EXPECT_EQ(seen, 1);
    frames(2);
    EXPECT_FALSE(shown("###tour"));
}

// Closing a subtree opened in place drops what was selected in it: nothing out of sight gets edited.
TEST_F(Harness, ClosingASubtreeDropsItsSelection) {
    session.setCursor(core::NodeRef{"Toy.xml", {0, 3}});
    frames(2);
    press(ImGuiKey_Space);
    press(ImGuiKey_DownArrow);
    press(ImGuiKey_U, ImGuiMod_Ctrl);
    press(ImGuiKey_DownArrow);
    press(ImGuiKey_Space, ImGuiMod_Ctrl);
    ASSERT_EQ(session.selection.size(), 1u);
    press(ImGuiKey_UpArrow);
    press(ImGuiKey_UpArrow);
    ASSERT_EQ(session.cursor.ref, (core::NodeRef{"Toy.xml", {0, 3}}));
    press(ImGuiKey_Space);
    EXPECT_TRUE(session.selection.empty());
}
