// A run of a tree, as BehaviorTree.CPP's FileLogger2 writes it (.btlog): the instantiated tree as XML, then every
// status change. RunLog reads the bytes as they come (a whole file, or a log still being written); Run turns the XML
// into subtree instances and the trees as they ran, and replays the changes to any moment.
#pragma once
#include "notgroot/core/xml.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace notgroot::core {
// BehaviorTree.CPP's NodeStatus.
enum class RunStatus : std::uint8_t { Idle = 0, Running = 1, Success = 2, Failure = 3, Skipped = 4 };

struct Transition {
    std::int64_t usec = 0; // since the run started
    std::uint16_t uid = 0;
    RunStatus status = RunStatus::Idle;
};

// The .btlog format: "BTCPP4-FileLogger2", protocol 1, int32 XML size, the XML, int64 start (µs since the epoch),
// then 9 bytes per transition (6 bytes µs since the start, uint16 uid, uint8 status), all little-endian.
class RunLog {
  public:
    // Takes more bytes. False (and `error`) when they are not a .btlog; a partial record waits for the rest.
    bool feed(std::string_view bytes, std::string &error);
    bool hasHeader() const {
        return headerDone_;
    }
    const std::string &xml() const {
        return xml_;
    }
    std::int64_t startUsec() const {
        return start_;
    }
    // Transitions parsed by the last feed(), handed over once.
    std::vector<Transition> takeTransitions();

  private:
    std::string pending_, xml_;
    std::int64_t start_ = 0;
    bool headerDone_ = false;
    std::vector<Transition> fresh_;
};

// The bytes of a .btlog with this header and these transitions (what RunLog reads back).
std::string writeRunLog(const std::string &xml, std::int64_t startUsec, const std::vector<Transition> &);

// One instance of a tree in the run: the main tree, or a subtree called from another instance.
struct RunInstance {
    std::string path;                // BehaviorTree.CPP's _fullpath ("" for the main tree)
    std::string id;                  // the tree's ID
    std::string label;               // the call's name, else the ID (with "#n" when its parent calls that ID again)
    int parent = -1;                 // instance index; -1 for the main tree
    int depth = 0;                   // calls from the main tree
    std::uint16_t callUid = 0;       // the SubTree node that made it (the main tree: its root node)
    std::vector<int> children;       // instances it calls, in document order
    std::vector<std::uint16_t> uids; // its nodes, in document order
};

// A node of the run: which instance it is in, and where it is in its tree (a NodeRef path into definitions()).
struct RunNode {
    int instance = -1;
    std::vector<std::size_t> path;
};

// A stretch of an instance being run: from its call leaving idle to its result.
struct RunSpan {
    std::int64_t start = 0, end = -1; // end -1: still running at the end of the trace
    RunStatus result = RunStatus::Running;
};

// Every node's status at one moment, and the last result of the nodes that are idle again (shown faded, as Groot2
// does).
struct RunState {
    std::size_t applied = 0; // transitions applied
    std::vector<RunStatus> status, last;
};

class Run {
  public:
    // Reads the header XML. False and `error` if it isn't a tree BehaviorTree.CPP wrote.
    bool load(const std::string &xml, std::string &error);
    // Adds transitions (in order) to the trace.
    void append(const std::vector<Transition> &);

    const std::vector<RunInstance> &instances() const {
        return instances_;
    }
    // The trees as they ran, one <BehaviorTree> per ID with the models, as a document's text (Workspace::openText).
    // Its globals carry notgroot's marker, so they are hidden as in the editor.
    const std::string &definitions() const {
        return definitions_;
    }
    const std::string &mainTree() const {
        return mainTree_;
    }
    // The subtree the main tree only calls (missions are a thin wrapper around one), else the main tree.
    std::string mission() const;
    const RunNode *node(std::uint16_t uid) const;
    // The node at a definition path in an instance (0: none).
    std::uint16_t uidAt(int instance, const std::vector<std::size_t> &path) const;
    // The instance a call node makes (-1: none).
    int calledBy(std::uint16_t callUid) const;
    std::size_t nodeCount() const {
        return nodes_.size();
    }

    const std::vector<Transition> &transitions() const {
        return transitions_;
    }
    std::int64_t duration() const {
        return transitions_.empty() ? 0 : transitions_.back().usec;
    }
    // Moves a state to the moment `usec` (every transition at or before it applied).
    void seek(RunState &, std::int64_t usec) const;
    // Moves a state to `count` transitions applied.
    void seekIndex(RunState &, std::size_t count) const;
    // When each instance ran, and how often it failed.
    const std::vector<RunSpan> &spans(int instance) const;
    int failures(int instance) const;
    bool ran(int instance) const {
        return !spans(instance).empty();
    }
    // The deepest instance running in a state (-1: none).
    int deepestRunning(const RunState &) const;
    // An instance's status in a state (its call node's).
    RunStatus status(const RunState &state, int instance) const;
    // The node whose status is the instance's: its call (the main tree: its root node).
    std::uint16_t statusUid(int instance) const {
        return instance < 0 || std::size_t(instance) >= rootOf_.size() ? 0 : rootOf_[std::size_t(instance)];
    }
    // The transition at which a subtree fails next, or last before the latest applied one, in a state that has
    // `applied` transitions (-1: none). seekIndex(state, failure + 1) shows it.
    long nextFailure(std::size_t applied) const;
    long previousFailure(std::size_t applied) const;

  private:
    void apply(RunState &, std::size_t from, std::size_t to) const;
    std::vector<RunInstance> instances_;
    std::map<std::uint16_t, RunNode> nodes_;
    std::map<std::uint16_t, int> calls_; // call uid -> instance
    std::vector<std::map<std::vector<std::size_t>, std::uint16_t>> byPath_;
    std::vector<std::vector<RunSpan>> spans_;
    std::vector<int> failures_;
    std::vector<std::uint16_t> rootOf_; // the uid whose status is the instance's
    std::uint16_t maxUid_ = 0;
    std::vector<Transition> transitions_;
    std::map<std::uint16_t, int> rootInstance_; // rootOf_ inverted
    RunState tail_;                             // the state after every transition (for keyframes)
    std::vector<RunState> keyframes_;           // keyframes_[k]: k * kKeyframe transitions applied
    std::vector<std::size_t> failuresAt_;
    std::string definitions_, mainTree_;
};

// Reads a whole .btlog file into a Run.
bool readRun(const std::filesystem::path &, Run &, std::string &error);
} // namespace notgroot::core
