// A run viewed headless: the run's session with the real canvas, Run panel and Timeline, driven by key presses.
#include "notgroot/ui/panels.hpp"
#include "notgroot/ui/run_view.hpp"
#include "notgroot/ui/theme.hpp"
#include "test_support.hpp"
#include <fstream>
#include <functional>
#include <gtest/gtest.h>
#include <set>

using namespace notgroot;
using core::RunStatus;

namespace {
class RunHarness : public ::testing::Test {
  protected:
    void SetUp() override {
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1600, 1000};
        io.DeltaTime = 1.f / 60;
        io.Fonts->AddFontDefault();
        unsigned char *pixels = nullptr;
        int w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        ui::applyTheme(ui::themes().front().id);
        std::string error;
        ASSERT_TRUE(session.loadBuiltinModels(NOTGROOT_BUILTIN_MODELS, error)) << error;
        ASSERT_TRUE(ui::openRun(session, test::data("runs") / "sim.btlog", error)) << error;
        frames(3);
    }
    void TearDown() override {
        ImGui::DestroyContext();
    }

    void frame() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({1100, 700});
        if (ImGui::Begin("Canvas", nullptr, ImGuiWindowFlags_NoScrollbar))
            ui::drawCanvas(session);
        ImGui::End();
        ImGui::SetNextWindowPos({1100, 0});
        ImGui::SetNextWindowSize({500, 500});
        if (ImGui::Begin("Run"))
            ui::drawRunPanel(session);
        ImGui::End();
        ImGui::SetNextWindowPos({1100, 500});
        ImGui::SetNextWindowSize({500, 500});
        if (ImGui::Begin("Inspector"))
            ui::drawInspector(session);
        ImGui::End();
        ImGui::SetNextWindowPos({0, 700});
        ImGui::SetNextWindowSize({1100, 300});
        if (ImGui::Begin("Timeline"))
            ui::drawTimeline(session);
        ImGui::End();
        ui::handleShortcuts(session);
        ImGui::Render();
    }
    void frames(int n) {
        for (int i = 0; i < n; ++i)
            frame();
    }
    void press(ImGuiKey key) {
        auto &io = ImGui::GetIO();
        io.AddKeyEvent(key, true);
        frame();
        io.AddKeyEvent(key, false);
        frames(2);
    }

    ui::RunView &view() {
        return *session.runView;
    }
    const core::Run &run() {
        return session.runView->run;
    }
    int instance(const std::string &id) {
        for (std::size_t i = 0; i < run().instances().size(); ++i)
            if (run().instances()[i].id == id)
                return int(i);
        return -1;
    }
    // The call that makes an instance, as a card of its caller's frame.
    core::NodeRef callOf(int i) {
        const auto &made = run().instances()[std::size_t(i)];
        return {run().instances()[std::size_t(made.parent)].id, run().node(made.callUid)->path};
    }

    ui::Session session;
};
} // namespace

TEST_F(RunHarness, OpensAtTheEndOnTheMainTree) {
    EXPECT_EQ(view().frame, 0);
    EXPECT_EQ(session.active, "BehaviorTree");
    EXPECT_EQ(view().state.applied, run().transitions().size());
    EXPECT_TRUE(session.workspace.readOnly());
    EXPECT_TRUE(session.command("run.play"));
    EXPECT_FALSE(session.command("edit.delete"));
    EXPECT_FALSE(session.command("node.edit"));
    EXPECT_FALSE(session.command("file.save"));
    EXPECT_TRUE(session.readOnly(session.cursor));
}

TEST_F(RunHarness, EnterGoesIntoACallAndBackspaceComesOut) {
    const int dive = instance("Dive");
    session.setCursor(callOf(dive));
    frames(2);
    press(ImGuiKey_Enter);
    EXPECT_EQ(view().frame, dive);
    EXPECT_EQ(session.active, "Dive");

    const int move = instance("PrimitiveMovePosition");
    session.setCursor(callOf(move));
    frames(2);
    press(ImGuiKey_Enter);
    EXPECT_EQ(view().frame, move);

    press(ImGuiKey_Backspace);
    EXPECT_EQ(view().frame, dive);
    press(ImGuiKey_Backspace);
    press(ImGuiKey_Backspace); // at the main tree: stays
    EXPECT_EQ(view().frame, 0);
    for (const auto &document : session.workspace.documents())
        EXPECT_FALSE(document->dirty());
}

TEST_F(RunHarness, FailureKeysAndStepsMoveThePlayhead) {
    // This run has no subtree failing; one more transition gives it one.
    const int dive = instance("Dive");
    view().run.append({{run().duration() + 5, run().instances()[std::size_t(dive)].callUid, RunStatus::Failure},
                       {run().duration() + 9, 1, RunStatus::Idle}});
    ui::seekTransitions(session, 0);
    frames(1);
    press(ImGuiKey_RightBracket);
    const long first = run().nextFailure(0);
    ASSERT_GE(first, 0);
    EXPECT_EQ(view().state.applied, std::size_t(first) + 1);
    EXPECT_EQ(run().transitions()[std::size_t(first)].status, RunStatus::Failure);

    press(ImGuiKey_Period);
    EXPECT_EQ(view().state.applied, std::size_t(first) + 2);
    press(ImGuiKey_Comma);
    press(ImGuiKey_Comma);
    EXPECT_EQ(view().state.applied, std::size_t(first));
}

TEST_F(RunHarness, FollowShowsTheDeepestRunningInstance) {
    press(ImGuiKey_F);
    ASSERT_TRUE(view().follow);
    // Somewhere the moves run, the canvas is on them.
    const int move = instance("PrimitiveMovePosition");
    const auto start = run().spans(move).front().start;
    ui::seekTime(session, start + 1);
    frames(2);
    const int deepest = run().deepestRunning(view().state);
    ASSERT_GE(deepest, 0);
    EXPECT_EQ(view().frame, deepest);
    EXPECT_EQ(session.active, run().instances()[std::size_t(deepest)].id);
}

TEST_F(RunHarness, PlayingMovesOnUntilTheEnd) {
    ui::seekTransitions(session, 0);
    press(ImGuiKey_P);
    ASSERT_TRUE(view().playing);
    ui::advanceRun(session, 2);
    EXPECT_GT(view().state.applied, 0u);
    view().speed = 1000;
    while (ui::advanceRun(session, 1))
        ;
    EXPECT_FALSE(view().playing);
    EXPECT_EQ(view().state.applied, run().transitions().size());
}

TEST_F(RunHarness, CardsShowTheirOwnInstance) {
    // Partway through the first move: its root runs, and a card opened in place under Dive's call shows the same.
    const int dive = instance("Dive"), move = instance("PrimitiveMovePosition");
    ui::seekTime(session, run().spans(move).front().start + 1000); // the call runs a few µs before its root
    ui::focusFrame(session, dive);
    frames(2);
    const auto call = callOf(move);
    EXPECT_EQ(ui::cardInstance(session, {call, {}}), dive);
    RunStatus status;
    bool faded = false;
    ASSERT_TRUE(ui::cardStatus(session, {call, {}}, status, faded));
    EXPECT_EQ(status, RunStatus::Running);
    EXPECT_FALSE(faded);

    const core::NodeRef root{"PrimitiveMovePosition",
                             run().node(run().instances()[std::size_t(move)].uids.front())->path};
    EXPECT_EQ(ui::cardInstance(session, {root, call}), move);
    ASSERT_TRUE(ui::cardStatus(session, {root, call}, status, faded));
    EXPECT_EQ(status, RunStatus::Running);

    // At the end it is idle again, shown with its last result.
    ui::seekTransitions(session, run().transitions().size());
    ASSERT_TRUE(ui::cardStatus(session, {call, {}}, status, faded));
    EXPECT_TRUE(faded);
    EXPECT_NE(status, RunStatus::Running);
}

TEST_F(RunHarness, TreesThatChangedAreNotShownInTheEditor) {
    core::Workspace same, changed;
    std::string error;
    auto text = run().definitions();
    ASSERT_TRUE(same.openText("/same", text, error)) << error;
    const auto at = text.find("<BehaviorTree ID=\"Dive\"");
    ASSERT_NE(at, std::string::npos);
    const auto port = text.find("=\"", text.find("<SubTree", at) + 10);
    text.insert(port + 2, "9");
    ASSERT_TRUE(changed.openText("/changed", text, error)) << error;

    ui::compareTrees(view(), session.workspace, same);
    EXPECT_TRUE(view().drifted.empty());
    const int move = instance("PrimitiveMovePosition");
    const core::NodeRef node{"PrimitiveMovePosition", run().node(run().instances()[std::size_t(move)].uids[2])->path};
    EXPECT_EQ(ui::sourceRef(session.workspace, same, view(), node), node);
    EXPECT_TRUE(ui::sourceRef(session.workspace, same, view(), {"BehaviorTree", {1}}).empty()); // the main tree

    ui::compareTrees(view(), session.workspace, changed);
    EXPECT_EQ(view().drifted, std::set<std::string>{"Dive"});
    EXPECT_TRUE(ui::sourceRef(session.workspace, changed, view(), callOf(move)).empty());
}

TEST_F(RunHarness, RealTreesMatchWhereTheyHaveNotChanged) {
    const auto trees = test::realTrees();
    if (trees.empty())
        GTEST_SKIP() << "no trees directory";
    core::Workspace editor;
    test::openWorkspace(editor, trees);
    ui::compareTrees(view(), session.workspace, editor);
    for (const auto &[key, entry] : session.workspace.trees()) {
        if (view().drifted.count(key) || key == run().mainTree())
            continue;
        // Every node maps onto a node of the same type.
        std::function<void(const core::NodeRef &)> visit = [&](const core::NodeRef &ref) {
            const auto *ran = core::resolve(session.workspace, ref);
            const auto source = ui::sourceRef(session.workspace, editor, view(), ref);
            ASSERT_FALSE(source.empty()) << key;
            const auto *there = core::resolve(editor, source);
            ASSERT_TRUE(there);
            if (!ref.isTree()) {
                EXPECT_EQ(core::nodeId(*ran), core::nodeId(*there)) << key;
            }
            for (std::size_t i = 0; i < ran->children.size(); ++i)
                if (!ran->children[i]->isComment())
                    visit(ref.child(i));
        };
        visit({key, {}});
    }
}

// The run's own keys are written down in docs/keys.md too.
TEST_F(RunHarness, KeysDocListsTheRunKeys) {
    std::ifstream file(NOTGROOT_KEYS_DOC);
    ASSERT_TRUE(file) << NOTGROOT_KEYS_DOC;
    const std::string doc((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    for (const auto &command : session.commands)
        if (command.chord && command.group == "Run") {
            EXPECT_NE(doc.find(ui::chordName(command.chord)), std::string::npos)
                << command.id << " (" << ui::chordName(command.chord) << ")";
        }
}
