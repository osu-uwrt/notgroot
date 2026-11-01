// Workspace: opening the contract's layout, the catalog, includes, saving only what changed, and undo.
#include "notgroot/core/history.hpp"
#include "notgroot/core/workspace.hpp"
#include "test_support.hpp"
#include <gtest/gtest.h>

using namespace notgroot::core;
using notgroot::test::openWorkspace;
using notgroot::test::ScratchCopy;
namespace fs = std::filesystem;

TEST(Workspace, OpensTheContractLayout) {
    Workspace workspace;
    openWorkspace(workspace, notgroot::test::data("toy"));
    EXPECT_FALSE(workspace.loose());
    EXPECT_EQ(workspace.missions(), std::vector<std::string>{"Toy.xml"});
    EXPECT_EQ(workspace.subtrees(), (std::vector<std::string>{"Move", "Settle"}));
    ASSERT_TRUE(workspace.models());
    for (const auto &document : workspace.documents())
        EXPECT_FALSE(document->dirty()) << document->path;

    const auto &catalog = workspace.catalog();
    ASSERT_TRUE(catalog.find("Sequence"));
    EXPECT_TRUE(catalog.find("Sequence")->builtin);
    EXPECT_EQ(catalog.find("Sequence")->category, Category::Control);
    ASSERT_TRUE(catalog.find("GetOdometry"));
    EXPECT_EQ(catalog.find("GetOdometry")->port("x")->direction, PortDirection::Output);
    ASSERT_TRUE(catalog.find("Move"));
    EXPECT_EQ(catalog.find("Move")->category, Category::SubTree);
    EXPECT_EQ(catalog.find("Move")->port("x")->description, "Target x");
    // Settle has no model entry: a subtree without ports.
    ASSERT_TRUE(catalog.find("Settle"));
    EXPECT_TRUE(catalog.find("Settle")->ports.empty());
    EXPECT_TRUE(catalog.find("SetBlackboardV3")->port("output_key")->keyName);
}

TEST(Workspace, OpeningAFileOpensItsWorkspace) {
    Workspace workspace;
    openWorkspace(workspace, notgroot::test::data("toy") / "lib" / "Move.xml");
    EXPECT_FALSE(workspace.loose());
    EXPECT_EQ(workspace.focus().filename(), "Move.xml");
    EXPECT_EQ(workspace.missions().size(), 1u);
}

TEST(Workspace, LooseFileFollowsIncludes) {
    ScratchCopy copy("toy");
    fs::remove(copy.path() / "lib" / "node_models.xml");
    Workspace workspace;
    openWorkspace(workspace, copy.path() / "Toy.xml");
    EXPECT_TRUE(workspace.loose());
    EXPECT_TRUE(workspace.tree("Move"));
    EXPECT_TRUE(workspace.tree("Settle"));
}

TEST(Workspace, IncludesAreAlreadyWhatTheyShouldBe) {
    Workspace workspace;
    openWorkspace(workspace, notgroot::test::data("toy"));
    for (const auto &document : workspace.documents())
        if (document->role != DocumentRole::Models) {
            EXPECT_FALSE(workspace.updateIncludes(*document)) << document->path;
        }
}

TEST(Workspace, SaveWritesOnlyEditedFilesAndFixesIncludes) {
    ScratchCopy copy("toy");
    Workspace workspace;
    openWorkspace(workspace, copy.path());
    History history(workspace);

    // Drop Settle from Move: Move's include of Settle.xml must go on save.
    auto *move = workspace.tree("Move")->document;
    {
        Change change(history, "remove settle");
        change.touch(*move);
        auto *sequence = workspace.tree("Move")->tree->elements().front();
        for (std::size_t i = 0; i < sequence->children.size(); ++i)
            if (sequence->children[i]->tag == "SubTree") {
                sequence->remove(i);
                break;
            }
    }
    EXPECT_TRUE(move->dirty());
    std::vector<std::string> errors;
    const auto written = workspace.save(errors);
    EXPECT_TRUE(errors.empty());
    ASSERT_EQ(written.size(), 1u);
    EXPECT_EQ(written.front(), copy.path() / "lib" / "Move.xml");
    std::string text;
    readFile(written.front(), text);
    EXPECT_EQ(text.find("<include"), std::string::npos);
    EXPECT_FALSE(move->dirty());
    EXPECT_TRUE(workspace.changedOnDisk().empty());
}

TEST(Workspace, NewSubtreeIsIncludedAndModeled) {
    ScratchCopy copy("toy");
    Workspace workspace;
    openWorkspace(workspace, copy.path());
    History history(workspace);
    {
        Change change(history, "new subtree");
        auto *document = workspace.createSubtreeDocument("Hover");
        change.touch(*document);
        workspace.tree("Settle"); // untouched
        auto *mission = workspace.tree("Toy.xml")->document;
        change.touch(*mission);
        auto *sequence = workspace.tree("Toy.xml")->tree->elements().front();
        auto call = std::make_unique<Element>("SubTree");
        call->set("ID", "Hover");
        sequence->append(std::move(call));
        document->trees().front()->append(std::make_unique<Element>("AlwaysSuccess"));
        change.touch(*workspace.models());
        NodeModel model;
        model.id = "Hover";
        model.category = Category::SubTree;
        model.ports.push_back({"seconds", "", "How long", "", PortDirection::Input, false});
        workspace.setSubtreeModel(model);
    }
    std::vector<std::string> errors;
    workspace.save(errors);
    ASSERT_TRUE(errors.empty());
    EXPECT_TRUE(fs::exists(copy.path() / "lib" / "Hover.xml"));
    std::string mission, models;
    readFile(copy.path() / "Toy.xml", mission);
    readFile(copy.path() / "lib" / "node_models.xml", models);
    EXPECT_NE(mission.find("<include path=\"lib/Hover.xml\"/>"), std::string::npos);
    // Sorted by ID: Hover lands between GetOdometry and Info.
    EXPECT_LT(models.find("GetOdometry"), models.find("ID=\"Hover\""));
    EXPECT_LT(models.find("ID=\"Hover\""), models.find("ID=\"Info\""));
    EXPECT_NE(models.find("<input_port name=\"seconds\">How long</input_port>"), std::string::npos);
    // The notgroot comment before Move stays.
    EXPECT_NE(models.find("<!-- notgroot:color=#00bcd4 -->"), std::string::npos);
}

TEST(History, UndoRedoRestoresDocuments) {
    Workspace workspace;
    openWorkspace(workspace, notgroot::test::data("toy"));
    History history(workspace);
    auto *settle = workspace.tree("Settle");
    const auto before = writeXml(*settle->document->root);
    {
        Change change(history, "attempts");
        change.touch(*settle->document);
        settle->tree->elements().front()->set("num_attempts", "5");
    }
    EXPECT_TRUE(history.canUndo());
    EXPECT_EQ(history.undoLabel(), "attempts");
    EXPECT_TRUE(history.undo());
    EXPECT_EQ(writeXml(*workspace.tree("Settle")->document->root), before);
    EXPECT_FALSE(workspace.tree("Settle")->document->dirty());
    EXPECT_TRUE(history.redo());
    EXPECT_EQ(workspace.tree("Settle")->tree->elements().front()->get("num_attempts"), "5");

    // A change that alters nothing leaves no step.
    {
        Change change(history, "nothing");
        change.touch(*workspace.tree("Settle")->document);
    }
    EXPECT_EQ(history.undoLabel(), "attempts");
}

TEST(Workspace, RealTreesOpenCleanly) {
    const auto trees = notgroot::test::realTrees();
    if (trees.empty())
        GTEST_SKIP() << "no trees directory (set NOTGROOT_TREES_DIR)";
    Workspace workspace;
    openWorkspace(workspace, trees);
    EXPECT_GT(workspace.missions().size(), 5u);
    EXPECT_TRUE(workspace.duplicateIds().empty());
    for (const auto &document : workspace.documents()) {
        EXPECT_TRUE(document->root) << document->path << ": " << document->error.message;
        EXPECT_FALSE(document->dirty()) << document->path;
        if (document->role != DocumentRole::Models) {
            EXPECT_FALSE(workspace.updateIncludes(*document)) << document->path;
        }
    }
}

TEST(Workspace, KeepingEditsOfADeletedFileDoesNotThrow) {
    ScratchCopy copy("toy");
    Workspace workspace;
    openWorkspace(workspace, copy.path());
    auto *move = workspace.tree("Move")->document;
    fs::remove(copy.path() / "lib" / "Move.xml");
    ASSERT_EQ(workspace.changedOnDisk().size(), 1u);
    EXPECT_NO_THROW(workspace.keepEdits(*move));
    EXPECT_TRUE(workspace.changedOnDisk().empty());
}
