// The editor driven by the keyboard alone, headless: a real ImGui context and the real panels, with key presses and
// typed text injected frame by frame, checked against the files that come out.
#include "notgroot/ui/panels.hpp"
#include "notgroot/ui/theme.hpp"
#include "test_support.hpp"
#include <gtest/gtest.h>

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
        ImGui::SetNextWindowSize({500, 1000});
        if (ImGui::Begin("Inspector"))
            ui::drawInspector(session);
        ImGui::End();
        ui::drawFinder(session);
        ui::drawPicker(session);
        ui::drawDialogs(session);
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
