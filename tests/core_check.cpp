// The BehaviorTree.CPP check: the toy workspace loads; an unsaved edit BT.CPP rejects is reported on its file.
#include "notgroot/core/bt_check.hpp"
#include "notgroot/core/history.hpp"
#include "test_support.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

using namespace notgroot::core;

namespace {
BtCheck::Result waitFor(BtCheck &check) {
    for (int i = 0; i < 600; ++i) {
        if (auto result = check.take())
            return *result;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ADD_FAILURE() << "the check did not finish";
    return {};
}
} // namespace

// The toy mission passes x="2" into Move, where Math reads it as text: BT.CPP refuses to build the tree.
TEST(BtCheck, CatchesALiteralTypedIntoASubtree) {
    Workspace workspace;
    notgroot::test::openWorkspace(workspace, notgroot::test::data("toy"));
    BtCheck check(NOTGROOT_CHECK);
    ASSERT_TRUE(check.available());
    ASSERT_TRUE(check.start(workspace));
    auto result = waitFor(check);
    EXPECT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.failed, 1);
    ASSERT_EQ(result.problems.size(), 1u);
    EXPECT_NE(result.problems.front().message.find("type"), std::string::npos) << result.problems.front().message;

    // Passed as a key instead, it loads.
    for (auto *node : workspace.tree("Toy.xml")->tree->elements().front()->elements())
        if (node->tag == "SubTree" && node->get("x") == "2")
            node->set("x", "{depth}");
    workspace.reindex();
    ASSERT_TRUE(check.start(workspace));
    result = waitFor(check);
    EXPECT_EQ(result.loaded, 1);
    EXPECT_EQ(result.failed, 0);
}

TEST(BtCheck, UnsavedEditIsCheckedAndMappedBack) {
    Workspace workspace;
    notgroot::test::openWorkspace(workspace, notgroot::test::data("toy"));
    // Not saved: the check sees the edit through its mirror, and the fixture on disk stays as it is.
    for (auto *node : workspace.tree("Toy.xml")->tree->elements().front()->elements())
        if (node->tag == "SubTree" && node->get("x") == "2")
            node->set("x", "{depth}");
    auto *settle = workspace.tree("Settle");
    settle->tree->elements().front()->set("colour", "red");
    workspace.reindex();
    BtCheck check(NOTGROOT_CHECK);
    ASSERT_TRUE(check.start(workspace));
    const auto result = waitFor(check);
    EXPECT_EQ(result.failed, 1);
    ASSERT_EQ(result.problems.size(), 1u);
    EXPECT_EQ(result.problems.front().file, notgroot::test::data("toy") / "Toy.xml");
    EXPECT_NE(result.problems.front().message.find("colour"), std::string::npos) << result.problems.front().message;
    EXPECT_TRUE(workspace.tree("Settle")->document->dirty()); // (the index was rebuilt: look it up again)
}

TEST(BtCheck, ParsesCheckerOutput) {
    const auto result = BtCheck::parse("ok\t/m/A.xml\t12\nerror\t/m/B.xml\tbad port\n", "/m", "/w");
    EXPECT_EQ(result.loaded, 1);
    ASSERT_EQ(result.problems.size(), 1u);
    EXPECT_EQ(result.problems.front().file, "/w/B.xml");
    EXPECT_EQ(result.problems.front().message, "BehaviorTree.CPP: bad port");
}

TEST(BtCheck, RealTreesAllLoad) {
    const auto trees = notgroot::test::realTrees();
    if (trees.empty())
        GTEST_SKIP() << "no trees directory (set NOTGROOT_TREES_DIR)";
    Workspace workspace;
    notgroot::test::openWorkspace(workspace, trees);
    BtCheck check(NOTGROOT_CHECK);
    ASSERT_TRUE(check.start(workspace));
    const auto result = waitFor(check);
    EXPECT_GT(result.loaded, 5);
    for (const auto &problem : result.problems)
        ADD_FAILURE() << problem.file << ": " << problem.message;
}
