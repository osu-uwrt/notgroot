// Edits, variables, validation, layout and search over the toy workspace (and the real trees when present).
#include "notgroot/core/edit.hpp"
#include "notgroot/core/layout.hpp"
#include "notgroot/core/search.hpp"
#include "notgroot/core/validate.hpp"
#include "notgroot/core/variables.hpp"
#include "test_support.hpp"
#include <fstream>
#include <gtest/gtest.h>

using namespace notgroot::core;
using notgroot::test::openWorkspace;
using notgroot::test::ScratchCopy;

namespace {
struct Fixture : ::testing::Test {
    Workspace workspace;
    History history{workspace};
    Editor editor{workspace, history};
    void SetUp() override {
        openWorkspace(workspace, notgroot::test::data("toy"));
    }
    // The toy mission's Sequence.
    NodeRef sequence() const {
        return refOf(workspace, workspace.tree("Toy.xml")->tree->elements().front());
    }
    std::vector<std::string> ids(const NodeRef &parent) const {
        std::vector<std::string> list;
        for (auto *child : nodeChildren(*resolve(workspace, parent)))
            list.push_back(isSubTree(*child) ? "SubTree:" + child->get("ID") : nodeId(*child));
        return list;
    }
    std::string text(const std::string &key) const {
        return writeXml(*workspace.tree(key)->document->root);
    }
};
} // namespace

TEST_F(Fixture, RefsResolveAndSurviveUndo) {
    const auto seq = sequence();
    EXPECT_EQ(seq.tree, "Toy.xml");
    EXPECT_EQ(nodeId(*resolve(workspace, seq)), "Sequence");
    const auto info = seq.child(2); // after the comment and SetBlackboardV3
    EXPECT_EQ(nodeId(*resolve(workspace, info)), "Info");
    EXPECT_EQ(refOf(workspace, resolve(workspace, info)), info);
}

TEST_F(Fixture, InsertMoveDeleteUndo) {
    const auto before = text("Toy.xml");
    const auto wait = *workspace.catalog().find("Wait");
    const auto made = editor.insert(sequence(), 1, makeNode(wait), "Add Wait");
    ASSERT_FALSE(made.empty()) << editor.error();
    EXPECT_EQ(ids(sequence())[1], "Wait");
    EXPECT_EQ(resolve(workspace, made)->get("seconds"), "");

    // Moving the SetBlackboardV3 down carries the comment above it.
    const auto set = refOf(workspace, nodeChildren(*resolve(workspace, sequence()))[0]);
    const auto moved = editor.move(set, 1);
    EXPECT_EQ(ids(sequence())[0], "Wait");
    EXPECT_EQ(ids(sequence())[1], "SetBlackboardV3");
    const auto *element = resolve(workspace, moved);
    EXPECT_TRUE(element->parent->children[element->index() - 1]->isComment());

    EXPECT_TRUE(editor.remove({moved}));
    EXPECT_EQ(ids(sequence())[1], "Info");
    while (history.undo()) {
    }
    EXPECT_EQ(text("Toy.xml"), before);
}

TEST_F(Fixture, RefusesWhatBtcppWouldReject) {
    const auto *wait = workspace.catalog().find("Wait");
    // A tree has one root; a leaf has no children; a decorator one.
    EXPECT_TRUE(editor.insert({"Toy.xml", {}}, 1, makeNode(*wait), "x").empty());
    const auto info = sequence().child(2);
    EXPECT_TRUE(editor.insert(info, 0, makeNode(*wait), "x").empty());
    const auto retry = refOf(workspace, workspace.tree("Settle")->tree->elements().front());
    EXPECT_TRUE(editor.insert(retry, 1, makeNode(*wait), "x").empty());
    EXPECT_FALSE(history.canUndo());
}

TEST_F(Fixture, WrapUnwrapIndentOutdent) {
    // Copies: an edit rebuilds the catalog.
    const auto inverter = *workspace.catalog().find("Inverter");
    const auto fallback = *workspace.catalog().find("Fallback");
    const auto info = sequence().child(2);
    const auto wrapped = editor.wrap({info}, inverter);
    ASSERT_FALSE(wrapped.empty()) << editor.error();
    EXPECT_EQ(nodeId(*resolve(workspace, wrapped)), "Inverter");
    EXPECT_EQ(nodeId(*nodeChildren(*resolve(workspace, wrapped))[0]), "Info");
    // An Inverter takes one child.
    EXPECT_TRUE(editor.wrap({sequence().child(2), sequence().child(3)}, inverter).empty());
    const auto unwrapped = editor.unwrap(wrapped);
    EXPECT_EQ(nodeId(*resolve(workspace, unwrapped)), "Info");

    const auto group = editor.wrap({sequence().child(2), sequence().child(3)}, fallback);
    ASSERT_FALSE(group.empty());
    EXPECT_EQ(nodeChildren(*resolve(workspace, group)).size(), 2u);
    // Out of the group and back in.
    const auto inner = refOf(workspace, nodeChildren(*resolve(workspace, group))[1]);
    const auto out = editor.outdent(inner);
    ASSERT_FALSE(out.empty()) << editor.error();
    EXPECT_EQ(nodeChildren(*resolve(workspace, group)).size(), 1u);
    const auto back = editor.indent(out);
    ASSERT_FALSE(back.empty()) << editor.error();
    EXPECT_EQ(nodeChildren(*resolve(workspace, group)).size(), 2u);
}

TEST_F(Fixture, RelocateMovesSelectionsInOneStep) {
    const auto before = text("Toy.xml");
    const auto seq = sequence();
    const auto kids = nodeChildren(*resolve(workspace, seq));
    const auto set = refOf(workspace, kids[0]), info = refOf(workspace, kids[1]);
    const auto fallback = refOf(workspace, kids[3]);

    // Into the Fallback, after its children; the comment above the setter goes along.
    const auto moved = editor.relocate({info, set, set}, fallback, 2);
    ASSERT_FALSE(moved.empty()) << editor.error();
    const auto fallbackNow = refOf(workspace, nodeChildren(*resolve(workspace, seq))[1]);
    EXPECT_EQ(ids(fallbackNow), (std::vector<std::string>{"CompareNums", "SubTree:Settle", "SetBlackboardV3", "Info"}));
    EXPECT_EQ(ids(seq), (std::vector<std::string>{"SubTree:Move", "Fallback", "SubTree:Move"}));
    EXPECT_TRUE(resolve(workspace, fallbackNow)->children[2]->isComment());
    EXPECT_EQ(nodeId(*resolve(workspace, moved)), "SetBlackboardV3");
    history.undo();
    EXPECT_EQ(text("Toy.xml"), before);

    // Reorder among siblings: the last call to the front.
    const auto last = seq.child(resolve(workspace, seq)->children.size() - 1); // pointers die with undo
    ASSERT_FALSE(editor.relocate({last}, seq, 0).empty()) << editor.error();
    EXPECT_EQ(ids(seq).front(), "SubTree:Move");
    EXPECT_EQ(ids(seq)[1], "SetBlackboardV3");
    history.undo();

    // Refused: into itself, into a leaf, nothing.
    EXPECT_TRUE(editor.relocate({fallback}, fallback.child(0), 0).empty());
    EXPECT_TRUE(editor.relocate({set}, info, 0).empty());
    EXPECT_TRUE(editor.relocate({}, seq, 0).empty());
    EXPECT_EQ(text("Toy.xml"), before);
}

TEST_F(Fixture, OutermostDropsNestedAndRepeats) {
    const auto seq = sequence();
    const auto fallback = refOf(workspace, nodeChildren(*resolve(workspace, seq))[3]);
    const auto inner = refOf(workspace, nodeChildren(*resolve(workspace, fallback))[0]);
    EXPECT_EQ(outermost({inner, fallback, fallback, {seq.tree, {}}}), std::vector<NodeRef>{fallback});
    EXPECT_EQ(copyNodes(workspace, {inner, fallback}), copyNodes(workspace, {fallback}));
}

TEST_F(Fixture, RetypeKeepsChildrenAndSharedPorts) {
    const auto reactive = *workspace.catalog().find("ReactiveSequence");
    const auto made = editor.retype(sequence(), reactive);
    ASSERT_FALSE(made.empty()) << editor.error();
    EXPECT_EQ(resolve(workspace, made)->tag, "ReactiveSequence");
    EXPECT_EQ(resolve(workspace, made)->get("name"), "TOY");
    EXPECT_EQ(nodeChildren(*resolve(workspace, made)).size(), 5u);
    // A leaf cannot take the Sequence's children.
    const auto wait = *workspace.catalog().find("Wait");
    EXPECT_TRUE(editor.retype(made, wait).empty());
}

TEST_F(Fixture, ClipboardRoundTrip) {
    const auto clip = copyNodes(workspace, {sequence().child(3), sequence().child(4)});
    auto nodes = parseClipboard(clip);
    ASSERT_EQ(nodes.size(), 2u);
    EXPECT_EQ(nodes[0]->get("ID"), "Move");
    EXPECT_EQ(nodes[1]->tag, "Fallback");
    const auto pasted = editor.insertMany(sequence(), 0, std::move(nodes), "Paste");
    ASSERT_FALSE(pasted.empty());
    EXPECT_EQ(ids(sequence())[0], "SubTree:Move");
    // A whole file pastes as its tree's nodes.
    EXPECT_EQ(parseClipboard(text("Settle")).size(), 1u);
    EXPECT_TRUE(parseClipboard("not xml <").empty());
}

TEST_F(Fixture, VariablesOfTheMission) {
    const auto vars = analyzeVariables(workspace, "Toy.xml");
    ASSERT_TRUE(vars.keys.count("depth"));
    const auto &depth = vars.keys.at("depth");
    EXPECT_EQ(depth.writes, 1); // SetBlackboardV3 output_key, a bare key name
    EXPECT_EQ(depth.reads, 2);  // the Info message mention and Move's x
    ASSERT_TRUE(vars.keys.count("moved"));
    EXPECT_EQ(vars.keys.at("moved").writes, 2); // Move's done is an output port
    EXPECT_EQ(vars.keys.at("moved").reads, 1);
    EXPECT_FALSE(vars.keys.at("moved").undefined());
}

TEST_F(Fixture, ScriptsReadAndWriteKeys) {
    const auto info = refOf(workspace, nodeChildren(*resolve(workspace, sequence()))[1]);
    ASSERT_TRUE(editor.setAttribute(info, "_skipIf", "depth > 0 && mode != 'depth'"));
    ASSERT_TRUE(editor.setAttribute(info, "_onSuccess", "count += 1; last := depth; @root := 2"));
    const auto vars = analyzeVariables(workspace, "Toy.xml");
    EXPECT_EQ(vars.keys.at("depth").reads, 4); // the message, the call, _skipIf, _onSuccess
    EXPECT_EQ(vars.keys.at("mode").reads, 1);  // 'depth' is quoted text, not a key
    EXPECT_EQ(vars.keys.at("count").reads, 1); // += reads and writes
    EXPECT_EQ(vars.keys.at("count").writes, 1);
    EXPECT_EQ(vars.keys.at("last").writes, 1);
    EXPECT_EQ(vars.keys.at("last").reads, 0);
    EXPECT_EQ(vars.keys.count("root"), 0u); // @root is the root blackboard's
}

TEST(Globals, ScriptRoundTripAndOrder) {
    std::vector<std::vector<std::string>> cycles;
    const auto ordered = orderGlobals(
        {{"frame", "{object}_frame"}, {"object", "buoy"}, {"depth", "-1.5"}, {"copy", "{depth}"}, {"empty", ""}},
        cycles);
    ASSERT_TRUE(cycles.empty());
    std::vector<std::string> names;
    for (const auto &g : ordered)
        names.push_back(g.name);
    EXPECT_EQ(names, (std::vector<std::string>{"depth", "copy", "empty", "object", "frame"}));
    const auto code = scriptFor(ordered);
    EXPECT_EQ(code, "depth := '-1.5'; copy := depth; empty := ''; object := 'buoy'; frame := object .. '_frame'");
    std::vector<Global> back;
    ASSERT_TRUE(parseScript(code, back));
    EXPECT_EQ(back, ordered);
    EXPECT_FALSE(parseScript("x := 1 + 2", back)); // not notgroot's
    EXPECT_FALSE(globalProblem({"talos/base_link", "x"}).empty());
    EXPECT_FALSE(globalProblem({"a", "it's"}).empty());

    orderGlobals({{"a", "{b}"}, {"b", "x{c}"}, {"c", "{a}"}, {"d", "{d}"}, {"e", "1"}}, cycles);
    EXPECT_EQ(cycles, (std::vector<std::vector<std::string>>{{"a", "b", "c"}, {"d"}}));
}

TEST_F(Fixture, GlobalsWrapTheRootAndCome) {
    const auto before = text("Toy.xml");
    ASSERT_TRUE(editor.setGlobals("Toy.xml", {{"target", "buoy"}, {"frame", "{target}_frame"}})) << editor.error();
    const auto *tree = workspace.tree("Toy.xml")->tree;
    const auto globals = readGlobals(*tree);
    ASSERT_NE(globals.wrapper, nullptr);
    EXPECT_EQ(globals.script->get("code"), "target := 'buoy'; frame := target .. '_frame'");
    EXPECT_EQ(nodeId(*treeBody(*tree)->elements()[1]), "Sequence"); // the root, after the Script
    const auto vars = analyzeVariables(workspace, "Toy.xml");
    EXPECT_EQ(vars.keys.at("frame").scope, Scope::Global);
    EXPECT_EQ(vars.keys.at("moved").scope, Scope::Local);
    EXPECT_TRUE(floatingNodes(workspace, "Toy.xml").empty());

    // Saved and read back, it is the same; a cycle is refused and named.
    EXPECT_FALSE(editor.setGlobals("Toy.xml", {{"a", "{b}"}, {"b", "{a}"}}));
    EXPECT_EQ(editor.cycles(), (std::vector<std::vector<std::string>>{{"a", "b"}}));
    ASSERT_TRUE(editor.setGlobals("Toy.xml", {}));
    EXPECT_EQ(text("Toy.xml"), before);
    history.undo();
    history.undo();
    EXPECT_EQ(text("Toy.xml"), before);
}

TEST_F(Fixture, MakeGlobalFromSetter) {
    const auto seq = sequence();
    const auto setter = refOf(workspace, nodeChildren(*resolve(workspace, seq))[0]);
    ASSERT_TRUE(editor.makeGlobal(setter, true)) << editor.error();
    const auto *tree = workspace.tree("Toy.xml")->tree;
    EXPECT_EQ(readGlobals(*tree).globals, (std::vector<Global>{{"depth", "-1.5"}}));
    const auto body = treeBody(*tree);
    const auto root = nodeChildren(*body)[1];
    EXPECT_EQ(nodeId(*nodeChildren(*root)[0]), "Info");
    EXPECT_TRUE(root->children[0]->isComment()); // the comment stays
    // Inserting into the tree goes into the wrapper, where a second root floats... and is refused.
    EXPECT_TRUE(editor.insert({"Toy.xml", {}}, 1, makeNode(*workspace.catalog().find("Info")), "Add").empty());
    history.undo();
    EXPECT_EQ(nodeId(*nodeChildren(*resolve(workspace, sequence()))[0]), "SetBlackboardV3");

    // Not from a key read before it, or set twice.
    const auto info = refOf(workspace, nodeChildren(*resolve(workspace, sequence()))[1]);
    EXPECT_TRUE(editor.setAttribute(sequence(), "_skipIf", "depth > 0"));
    EXPECT_FALSE(editor.makeGlobal(setter, true));
    (void)info;
}

TEST_F(Fixture, VariablesOfASubtreeIncludeItsPorts) {
    const auto vars = analyzeVariables(workspace, "Move");
    EXPECT_TRUE(vars.keys.at("x").port);
    EXPECT_FALSE(vars.keys.at("x").undefined()); // handed in by the port
    EXPECT_TRUE(vars.keys.at("done").port);
    EXPECT_FALSE(vars.keys.at("done").unused()); // handed out by the port
    EXPECT_TRUE(vars.keys.at("dx").unused());
    EXPECT_TRUE(vars.keys.at("oy").unused());
}

TEST_F(Fixture, RenameKeyAndPortFollowsCallSites) {
    EXPECT_GT(editor.renameKey("Toy.xml", "depth", "target_depth"), 0);
    const auto mission = text("Toy.xml");
    EXPECT_NE(mission.find("output_key=\"target_depth\""), std::string::npos);
    EXPECT_NE(mission.find("Diving to &quot;{target_depth}&quot;"), std::string::npos);
    EXPECT_NE(mission.find("x=\"{target_depth}\""), std::string::npos);

    // Renaming Move's port x renames it in Move, in the model, and at both call sites, in one step.
    EXPECT_GT(editor.renameKey("Move", "x", "goal_x"), 0);
    EXPECT_NE(text("Move").find("a=\"{goal_x}\""), std::string::npos);
    EXPECT_NE(text("Toy.xml").find("goal_x=\"{target_depth}\""), std::string::npos);
    EXPECT_NE(text("Toy.xml").find("goal_x=\"2\""), std::string::npos);
    EXPECT_TRUE(workspace.catalog().find("Move")->port("goal_x"));
    EXPECT_TRUE(history.undo());
    EXPECT_TRUE(workspace.catalog().find("Move")->port("x"));
    EXPECT_NE(text("Toy.xml").find("x=\"2\""), std::string::npos);
}

TEST_F(Fixture, RenameSubtreeMovesItsFile) {
    ASSERT_TRUE(editor.renameTree("Settle", "Steady")) << editor.error();
    ASSERT_TRUE(workspace.tree("Steady"));
    EXPECT_EQ(workspace.tree("Steady")->document->path.filename(), "Steady.xml");
    EXPECT_FALSE(workspace.tree("Settle"));
    EXPECT_NE(text("Toy.xml").find("<SubTree ID=\"Steady\"/>"), std::string::npos);
    EXPECT_NE(text("Move").find("<SubTree ID=\"Steady\"/>"), std::string::npos);
    EXPECT_TRUE(history.undo());
    EXPECT_TRUE(workspace.tree("Settle"));
    EXPECT_FALSE(workspace.tree("Steady"));
}

TEST_F(Fixture, SubtreeColorIsAMetadataComment) {
    EXPECT_EQ(treeColor(workspace, "Settle"), "");
    ASSERT_TRUE(editor.setTreeColor("Settle", "#ff8800"));
    EXPECT_EQ(treeColor(workspace, "Settle"), "#ff8800");
    EXPECT_NE(text("Settle").find("    <!-- notgroot:color=#ff8800 -->\n    <BehaviorTree ID=\"Settle\">"),
              std::string::npos);
    ASSERT_TRUE(editor.setTreeColor("Settle", "#112233")); // replaced, not added
    EXPECT_EQ(text("Settle").find("#ff8800"), std::string::npos);
    EXPECT_TRUE(editor.setTreeColor("Settle", ""));
    EXPECT_EQ(text("Settle").find("notgroot:"), std::string::npos);
    EXPECT_FALSE(editor.setTreeColor("Settle", "orange"));
}

TEST_F(Fixture, DeleteTreeRefusesWhileCalled) {
    EXPECT_FALSE(editor.deleteTree("Settle"));
    EXPECT_NE(editor.error().find("called"), std::string::npos);
}

TEST_F(Fixture, ExtractSubtreeMakesPorts) {
    // Move's GetOdometry + Math: ox is set and read inside (no port), x is read (input), dx set and unused (none).
    const auto move = refOf(workspace, workspace.tree("Move")->tree->elements().front());
    const auto made = editor.extractSubtree({move.child(0), move.child(1)}, "Offset");
    ASSERT_FALSE(made.empty()) << editor.error();
    const auto *call = resolve(workspace, made);
    EXPECT_EQ(call->get("ID"), "Offset");
    EXPECT_EQ(call->get("x"), "{x}");
    EXPECT_FALSE(call->has("ox"));
    ASSERT_TRUE(workspace.tree("Offset"));
    EXPECT_EQ(nodeId(*workspace.tree("Offset")->tree->elements().front()), "Sequence");
    const auto *model = workspace.catalog().find("Offset");
    ASSERT_TRUE(model && model->port("x"));
    EXPECT_EQ(model->port("x")->direction, PortDirection::Input);
    EXPECT_TRUE(history.undo());
    EXPECT_FALSE(workspace.tree("Offset"));
    EXPECT_FALSE(workspace.catalog().find("Offset"));
}

TEST_F(Fixture, NewSubtreeAndMissionSaveAndUndo) {
    ScratchCopy copy("toy");
    Workspace scratch;
    openWorkspace(scratch, copy.path());
    History h(scratch);
    Editor e(scratch, h);
    ASSERT_FALSE(e.createSubtree("Hover").empty());
    EXPECT_TRUE(e.createSubtree("Hover").empty()); // exists
    EXPECT_TRUE(e.createSubtree("2bad").empty());
    ASSERT_FALSE(e.createMission("Demo").empty());
    EXPECT_EQ(scratch.missions().size(), 2u);
    EXPECT_TRUE(h.undo());
    EXPECT_EQ(scratch.missions().size(), 1u);
    std::vector<std::string> errors;
    scratch.save(errors);
    EXPECT_TRUE(std::filesystem::exists(copy.path() / "lib" / "Hover.xml"));
    EXPECT_FALSE(std::filesystem::exists(copy.path() / "Demo.xml"));
}

TEST_F(Fixture, ToyWorkspaceIsValid) {
    int typed = 0;
    for (const auto &problem : validate(workspace)) {
        EXPECT_NE(problem.severity, Severity::Error) << problem.file << ":" << problem.line << ": " << problem.message;
        typed += problem.message.find("fails to load") != std::string::npos;
    }
    EXPECT_EQ(typed, 1); // Move x="2"
}

TEST_F(Fixture, ValidationFindsBtcppErrors) {
    auto *info = resolve(workspace, sequence().child(2));
    info->set("tint", "red");                     // not a port
    info->append(std::make_unique<Element>("X")); // a leaf with a child, and an unknown node
    workspace.reindex();
    int errors = 0;
    for (const auto &problem : validateTree(workspace, "Toy.xml"))
        errors += problem.severity == Severity::Error;
    EXPECT_EQ(errors, 3);
}

TEST(Validate, RealTreesHaveNoErrors) {
    const auto trees = notgroot::test::realTrees();
    if (trees.empty())
        GTEST_SKIP() << "no trees directory (set NOTGROOT_TREES_DIR)";
    Workspace workspace;
    openWorkspace(workspace, trees);
    int warnings = 0;
    for (const auto &problem : validate(workspace)) {
        EXPECT_NE(problem.severity, Severity::Error) << problem.file << ":" << problem.line << ": " << problem.message;
        warnings += problem.severity == Severity::Warning;
    }
    std::cout << warnings << " warnings in the real trees\n";
}

TEST(Layout, ChildrenDoNotOverlapAndParentsCenter) {
    // root with three children; the middle one has two wide children.
    std::vector<LayoutBox> boxes{{100, 40, {1, 2, 3}}, {80, 40, {}},  {60, 40, {4, 5}},
                                 {80, 40, {}},         {200, 40, {}}, {200, 40, {}}};
    const auto rects = layoutTree(boxes, 0);
    for (std::size_t a = 1; a < rects.size(); ++a)
        for (std::size_t b = a + 1; b < rects.size(); ++b) {
            const bool apart = rects[a].x + rects[a].width <= rects[b].x || rects[b].x + rects[b].width <= rects[a].x ||
                               rects[a].y + rects[a].height <= rects[b].y || rects[b].y + rects[b].height <= rects[a].y;
            EXPECT_TRUE(apart) << a << " overlaps " << b;
        }
    EXPECT_LT(rects[1].x, rects[2].x);
    EXPECT_LT(rects[2].x, rects[3].x);
    EXPECT_GT(rects[4].y, rects[2].y);
    const float parent = rects[2].x + rects[2].width / 2;
    const float kids = (rects[4].x + rects[4].width / 2 + rects[5].x + rects[5].width / 2) / 2;
    EXPECT_NEAR(parent, kids, 0.01);
    // Left-right: children stack downward, one column right.
    const auto sideways = layoutTree(boxes, 0, {true});
    EXPECT_GT(sideways[1].x, sideways[0].x);
    EXPECT_LT(sideways[1].y, sideways[3].y);
}

TEST(Search, FuzzyPrefersWordStartsAndRuns) {
    EXPECT_TRUE(fuzzyMatch("pmp", "PrimitiveMovePosition").matched);
    EXPECT_FALSE(fuzzyMatch("xyz", "PrimitiveMovePosition").matched);
    EXPECT_GT(fuzzyMatch("move", "PrimitiveMovePosition").score, fuzzyMatch("move", "RemoveOverlap").score);
    EXPECT_GT(fuzzyMatch("dive", "Dive").score, fuzzyMatch("dive", "DownwardSearchSlowly").score);
    const auto match = fuzzyMatch("gt", "GateTree");
    ASSERT_TRUE(match.matched);
    EXPECT_EQ(match.positions, (std::vector<int>{0, 4}));
    EXPECT_TRUE(fuzzyMatch("", "anything").matched);
    EXPECT_FALSE(fuzzyMatch("D", "dive").matched); // an uppercase query letter must match case
}

TEST(Search, KeyCompletionAfterABrace) {
    const std::vector<std::string> keys{"object_name", "object", "depth"};
    EXPECT_EQ(keyCompletion("{obj", 4, keys), "ect}"); // the first by name: object
    EXPECT_EQ(keyCompletion("{object_n", 9, keys), "ame}");
    EXPECT_EQ(keyCompletion("x_{d}", 4, keys), "epth"); // a brace already closes it
    EXPECT_EQ(keyCompletion("{", 1, keys), "depth}");
    EXPECT_EQ(keyCompletion("{object}_frame", 14, keys), ""); // not in a key
    EXPECT_EQ(keyCompletion("{zz", 3, keys), "");
    EXPECT_EQ(keyCompletion("a b{x y", 7, keys), "");
}

TEST_F(Fixture, GlobalsWrapperIgnoresTheKeyboardMoves) {
    ASSERT_TRUE(editor.setGlobals("Toy.xml", {{"target", "buoy"}}));
    const auto rootRef = refOf(workspace, treeBody(*workspace.tree("Toy.xml")->tree)->elements()[1]);
    const auto before = text("Toy.xml");

    EXPECT_FALSE(editor.move(rootRef, -1).tree.size());
    EXPECT_FALSE(editor.indent(rootRef, true).tree.size());
    EXPECT_FALSE(editor.outdent(rootRef, true).tree.size());
    EXPECT_EQ(text("Toy.xml"), before);
    EXPECT_NE(readGlobals(*workspace.tree("Toy.xml")->tree).wrapper, nullptr);
}

TEST(Workspace, RenamingASubtreeInALooseWorkspace) {
    ScratchCopy copy("toy");
    std::filesystem::remove(copy.path() / "lib" / "node_models.xml");
    Workspace workspace;
    openWorkspace(workspace, copy.path() / "Toy.xml");
    ASSERT_EQ(workspace.models(), nullptr);
    History history{workspace};
    Editor editor{workspace, history};
    ASSERT_TRUE(editor.renameTree("Settle", "Calm")) << editor.error();
    EXPECT_TRUE(workspace.tree("Calm"));
    EXPECT_EQ(workspace.models(), nullptr);
}

TEST(Rename, FollowsBareKeysInScriptsAndGlobals) {
    ScratchCopy copy("toy");
    std::string xml;
    ASSERT_TRUE(readFile(copy.path() / "Toy.xml", xml));
    const auto at = xml.find("<Info");
    xml.insert(at, "<Script code=\"depth += 1; other := depth\"/>\n"
                   "<Precondition if=\"depth == 3\" else=\"SUCCESS\"><AlwaysSuccess/></Precondition>\n");
    xml.replace(xml.find("<Info"), 5, "<Info _skipIf=\"depth &gt; 5\"");
    std::ofstream(copy.path() / "Toy.xml") << xml;

    Workspace workspace;
    openWorkspace(workspace, copy.path());
    History history{workspace};
    Editor editor{workspace, history};
    EXPECT_GT(editor.renameKey("Toy.xml", "depth", "dive"), 0);
    const auto text = writeXml(*workspace.tree("Toy.xml")->document->root);
    EXPECT_NE(text.find("code=\"dive += 1; other := dive\""), std::string::npos);
    EXPECT_NE(text.find("if=\"dive == 3\""), std::string::npos);
    EXPECT_NE(text.find("_skipIf=\"dive &gt; 5\""), std::string::npos);

    // The ports dialog renames a port inside the subtree, globals included.
    ASSERT_TRUE(editor.setGlobals("Move", {{"f", "{x}_f"}})) << editor.error();
    auto ports = workspace.subtreeModel("Move").ports;
    for (auto &port : ports)
        if (port.name == "x")
            port.name = "gx";
    ASSERT_TRUE(editor.setSubtreePorts("Move", ports, {{"x", "gx"}})) << editor.error();
    EXPECT_EQ(readGlobals(*workspace.tree("Move")->tree).globals, (std::vector<Global>{{"f", "{gx}_f"}}));
}

TEST(Validate, LiteralTypesFollowBehaviorTree) {
    EXPECT_EQ(literalType("2"), "int");
    EXPECT_EQ(literalType("-2"), "int");
    EXPECT_EQ(literalType("99999999999"), "int");
    EXPECT_EQ(literalType("2.5"), "double");
    EXPECT_EQ(literalType("1e3"), "double");
    for (const char *text : {"true", "false", "+5", " 5", "5 ", "", "a", "1;2", "0x10"})
        EXPECT_EQ(literalType(text), "") << text;
}

TEST(Models, KeyReferencesFollowBehaviorTree) {
    EXPECT_TRUE(isKeyReference("{x}"));
    EXPECT_TRUE(isKeyReference("  {x} "));
    EXPECT_EQ(keyOf(" {x}"), "x");
    EXPECT_FALSE(isKeyReference("{}"));
    EXPECT_TRUE(isKeyReference("{ }"));
    EXPECT_FALSE(isKeyReference("{x"));
}

TEST_F(Fixture, MakeGlobalRefusesAConditionalSetter) {
    const auto setter = sequence().child(1); // after the comment
    ASSERT_EQ(nodeId(*resolve(workspace, setter)), "SetBlackboardV3");
    resolve(workspace, setter)->set("_skipIf", "depth > 3");
    EXPECT_FALSE(editor.makeGlobal(setter, false));
    EXPECT_NE(editor.error().find("_skipIf"), std::string::npos);
    resolve(workspace, setter)->erase("_skipIf");
    EXPECT_TRUE(editor.makeGlobal(setter, false)) << editor.error();
}
