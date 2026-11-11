// Runs: reading .btlog bytes whole or in pieces, the instances and trees of a run, and replaying it to any moment.
#include "notgroot/core/edit.hpp"
#include "notgroot/core/globals.hpp"
#include "notgroot/core/run.hpp"
#include "test_support.hpp"
#include <gtest/gtest.h>
#include <random>

using namespace notgroot::core;
namespace fs = std::filesystem;
namespace core = notgroot::core; // Run alone means gtest's Test::Run inside a test

namespace {
// A sim run of a small mission: the main tree calls WaitForKillSwitch, Dive (which calls two moves) and
// KillController.
fs::path simRun() {
    return notgroot::test::data("runs") / "sim.btlog";
}

std::string bytesOf(const fs::path &path) {
    std::string bytes;
    EXPECT_TRUE(readFile(path, bytes));
    return bytes;
}

int instanceOf(const core::Run &run, const std::string &id) {
    for (std::size_t i = 0; i < run.instances().size(); ++i)
        if (run.instances()[i].id == id)
            return int(i);
    return -1;
}

// A tree as BehaviorTree.CPP writes it, with globals in its subtree.
const char *kGlobalsXml = R"(<root BTCPP_format="4">
    <BehaviorTree ID="BehaviorTree" _fullpath="">
        <Sequence name="Sequence" _uid="1">
            <SubTree ID="Go" _fullpath="Go::2" _uid="2" depth="2" />
            <SubTree ID="Go" _fullpath="Go::7" _uid="7" depth="3" />
        </Sequence>
    </BehaviorTree>
    <BehaviorTree ID="Go" _fullpath="Go::2">
        <Sequence name="globals" _uid="3">
            <Script name="Script" _uid="4" code="speed := '1'" />
            <Sequence name="Sequence" _uid="5">
                <AlwaysFailure name="AlwaysFailure" _uid="6" />
            </Sequence>
        </Sequence>
    </BehaviorTree>
    <BehaviorTree ID="Go" _fullpath="Go::7">
        <Sequence name="globals" _uid="8">
            <Script name="Script" _uid="9" code="speed := '1'" />
            <Sequence name="Sequence" _uid="10">
                <AlwaysFailure name="AlwaysFailure" _uid="11" />
            </Sequence>
        </Sequence>
    </BehaviorTree>
    <TreeNodesModel />
</root>)";
} // namespace

TEST(Run, ReadsAndWritesTheSameBytes) {
    const auto bytes = bytesOf(simRun());
    RunLog log;
    std::string error;
    ASSERT_TRUE(log.feed(bytes, error)) << error;
    ASSERT_TRUE(log.hasHeader());
    const auto transitions = log.takeTransitions();
    EXPECT_EQ(transitions.size(), 323u);
    EXPECT_TRUE(log.takeTransitions().empty());
    EXPECT_EQ(writeRunLog(log.xml(), log.startUsec(), transitions), bytes);
}

TEST(Run, ReadsALogInPieces) {
    const auto bytes = bytesOf(simRun());
    RunLog whole;
    std::string error;
    ASSERT_TRUE(whole.feed(bytes, error));
    const auto expected = whole.takeTransitions();

    std::mt19937 random(7);
    RunLog pieces;
    std::vector<Transition> got;
    for (std::size_t at = 0; at < bytes.size();) {
        const auto size = std::min<std::size_t>(bytes.size() - at, 1 + random() % 700);
        ASSERT_TRUE(pieces.feed(std::string_view(bytes).substr(at, size), error)) << error;
        for (const auto &t : pieces.takeTransitions())
            got.push_back(t);
        at += size;
    }
    ASSERT_EQ(got.size(), expected.size());
    for (std::size_t i = 0; i < got.size(); ++i)
        EXPECT_TRUE(got[i].usec == expected[i].usec && got[i].uid == expected[i].uid &&
                    got[i].status == expected[i].status)
            << i;
}

TEST(Run, RefusesOtherFiles) {
    RunLog log;
    std::string error;
    EXPECT_FALSE(log.feed("<root BTCPP_format=\"4\"></root> and some more", error));
    EXPECT_FALSE(error.empty());
}

TEST(Run, InstancesFollowTheCalls) {
    core::Run run;
    std::string error;
    ASSERT_TRUE(readRun(simRun(), run, error)) << error;
    ASSERT_EQ(run.instances().size(), 6u);
    EXPECT_EQ(run.mainTree(), "BehaviorTree");
    EXPECT_EQ(run.mission(), "BehaviorTree"); // this main tree calls three subtrees

    const int dive = instanceOf(run, "Dive"), move = instanceOf(run, "PrimitiveMovePosition");
    ASSERT_GE(dive, 0);
    ASSERT_GE(move, 0);
    EXPECT_EQ(run.instances()[std::size_t(move)].parent, dive);
    EXPECT_EQ(run.instances()[std::size_t(move)].depth, 2);
    EXPECT_EQ(run.instances()[std::size_t(dive)].children.size(), 2u);
    EXPECT_EQ(run.calledBy(24), move);
    EXPECT_EQ(run.instances()[std::size_t(move)].callUid, 24);
    EXPECT_EQ(run.instances()[0].parent, -1);
}

TEST(Run, EveryNodeIsInTheTreesAsTheyRan) {
    core::Run run;
    std::string error;
    ASSERT_TRUE(readRun(simRun(), run, error)) << error;
    Workspace workspace;
    ASSERT_TRUE(workspace.loadBuiltinModels(NOTGROOT_BUILTIN_MODELS, error)) << error;
    ASSERT_TRUE(workspace.openText("/run/sim.btlog", run.definitions(), error)) << error;
    EXPECT_TRUE(workspace.readOnly());
    EXPECT_TRUE(workspace.duplicateIds().empty());
    ASSERT_TRUE(workspace.catalog().find("GetBoolTopic")); // a custom node, from the log's models

    for (std::size_t i = 0; i < run.instances().size(); ++i) {
        const auto &instance = run.instances()[i];
        ASSERT_TRUE(workspace.tree(instance.id)) << instance.id;
        for (const auto uid : instance.uids) {
            const auto *node = run.node(uid);
            ASSERT_TRUE(node);
            EXPECT_EQ(node->instance, int(i));
            const auto *element = resolve(workspace, NodeRef{instance.id, node->path});
            ASSERT_TRUE(element) << uid;
            EXPECT_FALSE(element->has("_uid"));
            EXPECT_EQ(run.uidAt(int(i), node->path), uid);
            if (run.calledBy(uid) >= 0) {
                EXPECT_TRUE(isSubTree(*element));
            }
        }
    }
    for (const auto &t : run.transitions())
        EXPECT_TRUE(run.node(t.uid)) << t.uid;
}

TEST(Run, SeekingMatchesReplayingFromTheStart) {
    core::Run run;
    std::string error;
    ASSERT_TRUE(readRun(simRun(), run, error)) << error;
    const auto &transitions = run.transitions();
    std::mt19937 random(3);
    RunState state;
    for (int trial = 0; trial < 60; ++trial) {
        const auto count = std::size_t(random() % (transitions.size() + 1));
        run.seekIndex(state, count);
        std::map<std::uint16_t, RunStatus> naive;
        for (std::size_t i = 0; i < count; ++i)
            naive[transitions[i].uid] = transitions[i].status;
        for (const auto &[uid, status] : naive)
            ASSERT_EQ(state.status[uid], status) << "after " << count << ", uid " << uid;
        EXPECT_EQ(state.applied, count);
    }

    run.seek(state, run.duration());
    EXPECT_EQ(state.applied, transitions.size());
    run.seek(state, -1);
    EXPECT_EQ(state.applied, 0u);
}

TEST(Run, SpansFailuresAndWhatRuns) {
    core::Run run;
    std::string error;
    ASSERT_TRUE(readRun(simRun(), run, error)) << error;
    const int dive = instanceOf(run, "Dive");
    ASSERT_FALSE(run.spans(dive).empty());
    EXPECT_GE(run.spans(dive).front().end, run.spans(dive).front().start);
    EXPECT_TRUE(run.ran(0));

    // Partway through, something runs, and the deepest running instance is inside what runs.
    RunState state;
    bool sawRunning = false;
    for (std::size_t count = 1; count <= run.transitions().size(); count += 7) {
        run.seekIndex(state, count);
        const int deepest = run.deepestRunning(state);
        if (deepest < 0)
            continue;
        sawRunning = true;
        EXPECT_EQ(run.status(state, deepest), RunStatus::Running);
        for (int up = run.instances()[std::size_t(deepest)].parent; up >= 0;
             up = run.instances()[std::size_t(up)].parent)
            EXPECT_EQ(run.status(state, up), RunStatus::Running);
    }
    EXPECT_TRUE(sawRunning);

    // Stepping through failures lands on a failing subtree.
    long failure = run.nextFailure(0), previous = -1;
    int seen = 0;
    while (failure >= 0) {
        const auto *node = run.node(run.transitions()[std::size_t(failure)].uid);
        ASSERT_TRUE(node);
        EXPECT_EQ(run.transitions()[std::size_t(failure)].status, RunStatus::Failure);
        EXPECT_EQ(run.previousFailure(std::size_t(failure) + 1), previous);
        previous = failure;
        ++seen;
        failure = run.nextFailure(std::size_t(failure) + 1);
    }
    int total = 0;
    for (std::size_t i = 0; i < run.instances().size(); ++i)
        total += run.failures(int(i));
    EXPECT_EQ(seen, total);
}

TEST(Run, GlobalsAreHiddenAndRepeatedCallsNumbered) {
    core::Run run;
    std::string error;
    ASSERT_TRUE(run.load(kGlobalsXml, error)) << error;
    ASSERT_EQ(run.instances().size(), 3u);
    EXPECT_EQ(run.mission(), "BehaviorTree");
    EXPECT_EQ(run.instances()[1].label, "Go #1");
    EXPECT_EQ(run.instances()[2].label, "Go #2");

    Workspace workspace;
    ASSERT_TRUE(workspace.openText("/run/globals.btlog", run.definitions(), error)) << error;
    const auto *go = workspace.tree("Go");
    ASSERT_TRUE(go);
    const auto globals = readGlobals(*go->tree);
    ASSERT_TRUE(globals.wrapper);
    ASSERT_EQ(globals.globals.size(), 1u);
    EXPECT_EQ(globals.globals.front().name, "speed");

    // The failing leaf maps to the same place in both instances.
    const auto *first = run.node(6), *second = run.node(11);
    ASSERT_TRUE(first && second);
    EXPECT_EQ(first->path, second->path);
    EXPECT_EQ(resolve(workspace, NodeRef{"Go", first->path})->tag, "AlwaysFailure");
    EXPECT_FALSE(resolve(workspace, NodeRef{"Go", first->path})->has("name"));

    run.append({{0, 1, RunStatus::Running},
                {5, 2, RunStatus::Running},
                {9, 2, RunStatus::Failure},
                {9, 1, RunStatus::Failure},
                {10, 2, RunStatus::Idle}});
    EXPECT_EQ(run.failures(1), 1);
    EXPECT_EQ(run.spans(1).size(), 1u);
    EXPECT_EQ(run.spans(1).front().result, RunStatus::Failure);
    EXPECT_EQ(run.spans(1).front().end, 9);
    EXPECT_FALSE(run.ran(2));
    RunState state;
    run.seekIndex(state, 5);
    EXPECT_EQ(state.status[2], RunStatus::Idle);
    EXPECT_EQ(state.last[2], RunStatus::Failure); // shown faded
}

TEST(Run, ARunWorkspaceIsNeverSaved) {
    core::Run run;
    std::string error;
    ASSERT_TRUE(run.load(kGlobalsXml, error)) << error;
    Workspace workspace;
    ASSERT_TRUE(workspace.openText("/nonexistent/run.btlog", run.definitions(), error)) << error;
    for (const auto &document : workspace.documents())
        EXPECT_FALSE(document->dirty());
    std::vector<std::string> errors;
    EXPECT_TRUE(workspace.save(errors).empty());
    EXPECT_TRUE(errors.empty());
    EXPECT_TRUE(workspace.changedOnDisk().empty());
    EXPECT_FALSE(fs::exists("/nonexistent/run.btlog"));
}

TEST(Run, RealRunsLoad) {
    const auto runs = notgroot::test::realRuns();
    if (runs.empty())
        GTEST_SKIP() << "no runs directory (NOTGROOT_RUNS_DIR or ~/btlogs)";
    int loaded = 0;
    for (const auto &entry : fs::directory_iterator(runs)) {
        if (entry.path().extension() != ".btlog")
            continue;
        core::Run run;
        std::string error;
        ASSERT_TRUE(readRun(entry.path(), run, error)) << entry.path() << ": " << error;
        Workspace workspace;
        ASSERT_TRUE(workspace.openText(entry.path(), run.definitions(), error)) << entry.path() << ": " << error;
        for (const auto &t : run.transitions())
            ASSERT_TRUE(run.node(t.uid)) << entry.path() << ": uid " << t.uid;
        ++loaded;
    }
    if (!loaded)
        GTEST_SKIP() << "no .btlog in " << runs;
}
