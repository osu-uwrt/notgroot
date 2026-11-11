// Run: reading .btlog bytes, the instances and trees of the run's XML, and replaying its transitions.
#include "notgroot/core/run.hpp"
#include "notgroot/core/workspace.hpp"
#include <algorithm>
#include <cstring>
#include <functional>

namespace notgroot::core {
namespace {
constexpr std::string_view kMagic = "BTCPP4-FileLogger2";
constexpr std::size_t kKeyframe = 4096;

std::uint64_t readLe(const char *bytes, int size) {
    std::uint64_t value = 0;
    for (int i = size - 1; i >= 0; --i)
        value = (value << 8) | std::uint8_t(bytes[i]);
    return value;
}

void writeLe(std::string &out, std::uint64_t value, int size) {
    for (int i = 0; i < size; ++i)
        out += char((value >> (8 * i)) & 0xff);
}

std::uint16_t uidOf(const Element &element) {
    const auto *uid = element.find("_uid");
    return uid ? std::uint16_t(std::stoul(*uid)) : 0;
}

// A run's node as the tree holds it: no BehaviorTree.CPP metadata, and no name that only repeats the node's ID.
void clean(Element &element) {
    element.erase("_uid");
    element.erase("_fullpath");
    if (element.get("name") == element.tag)
        element.erase("name");
    for (auto *child : element.elements())
        clean(*child);
}

// Puts notgroot's marker before a globals wrapper (contract section 5), which BehaviorTree.CPP's XML has no comments
// for.
void markGlobals(Element &tree) {
    const auto nodes = tree.elements();
    if (nodes.size() != 1 || nodes.front()->tag != "Sequence" || nodes.front()->get("name") != "globals")
        return;
    const auto inside = nodes.front()->elements();
    if (!inside.empty() && inside.front()->tag == "Script")
        tree.insert(nodes.front()->index(), Element::comment(" notgroot:globals "));
}

bool isResult(RunStatus status) {
    return status == RunStatus::Success || status == RunStatus::Failure || status == RunStatus::Skipped;
}
} // namespace

bool RunLog::feed(std::string_view bytes, std::string &error) {
    pending_.append(bytes);
    std::size_t at = 0;
    if (!headerDone_) {
        const auto fixed = kMagic.size() + 1 + 4;
        if (pending_.size() < fixed)
            return true;
        if (pending_.compare(0, kMagic.size(), kMagic) != 0 || pending_[kMagic.size()] != 1) {
            error = "not a BehaviorTree.CPP 4 log (.btlog)";
            return false;
        }
        const auto size = std::size_t(readLe(pending_.data() + kMagic.size() + 1, 4));
        if (pending_.size() < fixed + size + 8)
            return true;
        xml_ = pending_.substr(fixed, size);
        start_ = std::int64_t(readLe(pending_.data() + fixed + size, 8));
        headerDone_ = true;
        at = fixed + size + 8;
    }
    for (; pending_.size() - at >= 9; at += 9) {
        const char *record = pending_.data() + at;
        fresh_.push_back({std::int64_t(readLe(record, 6)), std::uint16_t(readLe(record + 6, 2)),
                          RunStatus(std::uint8_t(record[8]))});
    }
    pending_.erase(0, at);
    return true;
}

std::vector<Transition> RunLog::takeTransitions() {
    std::vector<Transition> out;
    std::swap(out, fresh_);
    return out;
}

std::string writeRunLog(const std::string &xml, std::int64_t startUsec, const std::vector<Transition> &transitions) {
    std::string out(kMagic);
    out += char(1);
    writeLe(out, xml.size(), 4);
    out += xml;
    writeLe(out, std::uint64_t(startUsec), 8);
    out.reserve(out.size() + 9 * transitions.size());
    for (const auto &t : transitions) {
        writeLe(out, std::uint64_t(t.usec), 6);
        writeLe(out, t.uid, 2);
        out += char(t.status);
    }
    return out;
}

bool Run::load(const std::string &xml, std::string &error) {
    *this = Run();
    ParseError parse;
    auto root = parseXml(xml, parse);
    if (!root) {
        error = "the run's tree: line " + std::to_string(parse.line) + ": " + parse.message;
        return false;
    }

    // The instances, in the order BehaviorTree.CPP made them (a caller before what it calls).
    std::map<std::string, int> byPath;
    std::vector<Element *> elements;
    for (auto *tree : root->elements())
        if (tree->tag == "BehaviorTree") {
            RunInstance instance;
            instance.path = tree->get("_fullpath");
            instance.id = tree->get("ID");
            byPath[instance.path] = int(instances_.size());
            instances_.push_back(std::move(instance));
            elements.push_back(tree);
        }
    const auto main = byPath.find("");
    if (instances_.empty() || main == byPath.end() || main->second != 0) {
        error = "the run's tree has no main tree";
        return false;
    }
    mainTree_ = instances_[0].id;

    // The trees as they ran: the first instance of each ID, cleaned.
    auto document = std::make_unique<Element>("root");
    document->set("BTCPP_format", "4");
    document->set("main_tree_to_execute", mainTree_);
    std::map<std::string, Element *> definitions;
    for (std::size_t i = 0; i < instances_.size(); ++i)
        if (!definitions.count(instances_[i].id)) {
            auto copy = elements[i]->clone();
            clean(*copy);
            copy->set("ID", instances_[i].id);
            markGlobals(*copy);
            definitions[instances_[i].id] = document->append(std::move(copy));
        }
    if (const auto *models = root->firstChild("TreeNodesModel"))
        document->append(models->clone());
    definitions_ = writeXml(*document);

    // Every node's place, walking each instance beside its tree.
    byPath_.resize(instances_.size());
    for (std::size_t i = 0; i < instances_.size(); ++i) {
        auto &instance = instances_[i];
        std::map<std::string, int> seen;
        std::function<bool(const Element &, const Element &, std::vector<std::size_t>)> walk;
        walk = [&](const Element &ran, const Element &tree, std::vector<std::size_t> path) {
            if (!path.empty()) {
                const auto uid = uidOf(ran);
                nodes_[uid] = {int(i), path};
                byPath_[i][path] = uid;
                instance.uids.push_back(uid);
                maxUid_ = std::max(maxUid_, uid);
                if (ran.tag == "SubTree") {
                    const auto called = byPath.find(ran.get("_fullpath"));
                    if (called != byPath.end()) {
                        auto &child = instances_[std::size_t(called->second)];
                        child.parent = int(i);
                        child.callUid = uid;
                        child.label = ran.get("name", child.id);
                        instance.children.push_back(called->second);
                        calls_[uid] = called->second;
                    }
                }
            }
            const auto ranChildren = ran.elements();
            const auto treeChildren = tree.elements();
            if (ranChildren.size() != treeChildren.size())
                return false;
            for (std::size_t c = 0; c < ranChildren.size(); ++c) {
                auto next = path;
                next.push_back(treeChildren[c]->index());
                if (!walk(*ranChildren[c], *treeChildren[c], next))
                    return false;
            }
            return true;
        };
        if (!walk(*elements[i], *definitions[instance.id], {})) {
            error = "instance " + instance.path + " of " + instance.id + " differs from the first one";
            return false;
        }
    }

    // Depths, labels ("#n" where a caller calls one ID again) and the node each instance's status comes from.
    for (auto &instance : instances_)
        if (instance.parent >= 0)
            instance.depth = instances_[std::size_t(instance.parent)].depth + 1;
    instances_[0].label = mainTree_;
    for (auto &instance : instances_) {
        std::map<std::string, std::vector<int>> byLabel;
        for (const int child : instance.children)
            byLabel[instances_[std::size_t(child)].label].push_back(child);
        for (const auto &[label, same] : byLabel)
            if (same.size() > 1)
                for (std::size_t n = 0; n < same.size(); ++n)
                    instances_[std::size_t(same[n])].label = label + " #" + std::to_string(n + 1);
    }
    for (std::size_t i = 0; i < instances_.size(); ++i) {
        const auto uid = i == 0 ? (instances_[0].uids.empty() ? 0 : instances_[0].uids.front()) : instances_[i].callUid;
        rootOf_.push_back(uid);
        rootInstance_[uid] = int(i);
    }
    spans_.assign(instances_.size(), {});
    failures_.assign(instances_.size(), 0);
    tail_.status.assign(std::size_t(maxUid_) + 1, RunStatus::Idle);
    tail_.last = tail_.status;
    keyframes_.push_back(tail_);
    return true;
}

void Run::append(const std::vector<Transition> &more) {
    for (const auto &t : more) {
        transitions_.push_back(t);
        apply(tail_, transitions_.size() - 1, transitions_.size());
        if (transitions_.size() % kKeyframe == 0)
            keyframes_.push_back(tail_);

        const auto root = rootInstance_.find(t.uid);
        if (root == rootInstance_.end())
            continue;
        auto &spans = spans_[std::size_t(root->second)];
        const bool open = !spans.empty() && spans.back().end < 0;
        if (t.status == RunStatus::Running && !open)
            spans.push_back({t.usec, -1, RunStatus::Running});
        else if (isResult(t.status)) {
            if (open)
                spans.back() = {spans.back().start, t.usec, t.status};
            else
                spans.push_back({t.usec, t.usec, t.status});
            if (t.status == RunStatus::Failure) {
                ++failures_[std::size_t(root->second)];
                failuresAt_.push_back(transitions_.size() - 1);
            }
        }
    }
}

void Run::apply(RunState &state, std::size_t from, std::size_t to) const {
    for (std::size_t i = from; i < to; ++i) {
        const auto &t = transitions_[i];
        if (t.uid > maxUid_)
            continue;
        if (t.status == RunStatus::Idle && state.status[t.uid] != RunStatus::Idle)
            state.last[t.uid] = state.status[t.uid];
        state.status[t.uid] = t.status;
    }
    state.applied = to;
}

void Run::seekIndex(RunState &state, std::size_t count) const {
    count = std::min(count, transitions_.size());
    const bool sized = state.status.size() == std::size_t(maxUid_) + 1;
    if (!sized || count < state.applied || count - state.applied > kKeyframe)
        state = keyframes_[std::min(count / kKeyframe, keyframes_.size() - 1)];
    apply(state, state.applied, count);
}

void Run::seek(RunState &state, std::int64_t usec) const {
    const auto after = std::upper_bound(transitions_.begin(), transitions_.end(), usec,
                                        [](std::int64_t t, const Transition &x) { return t < x.usec; });
    seekIndex(state, std::size_t(after - transitions_.begin()));
}

std::string Run::mission() const {
    if (!instances_.empty() && instances_[0].children.size() == 1)
        return instances_[std::size_t(instances_[0].children.front())].id;
    return mainTree_;
}

const RunNode *Run::node(std::uint16_t uid) const {
    const auto found = nodes_.find(uid);
    return found == nodes_.end() ? nullptr : &found->second;
}

std::uint16_t Run::uidAt(int instance, const std::vector<std::size_t> &path) const {
    if (instance < 0 || std::size_t(instance) >= byPath_.size())
        return 0;
    const auto found = byPath_[std::size_t(instance)].find(path);
    return found == byPath_[std::size_t(instance)].end() ? 0 : found->second;
}

int Run::calledBy(std::uint16_t callUid) const {
    const auto found = calls_.find(callUid);
    return found == calls_.end() ? -1 : found->second;
}

const std::vector<RunSpan> &Run::spans(int instance) const {
    static const std::vector<RunSpan> none;
    return instance < 0 || std::size_t(instance) >= spans_.size() ? none : spans_[std::size_t(instance)];
}

int Run::failures(int instance) const {
    return instance < 0 || std::size_t(instance) >= failures_.size() ? 0 : failures_[std::size_t(instance)];
}

RunStatus Run::status(const RunState &state, int instance) const {
    if (instance < 0 || std::size_t(instance) >= rootOf_.size() || state.status.empty())
        return RunStatus::Idle;
    return state.status[rootOf_[std::size_t(instance)]];
}

int Run::deepestRunning(const RunState &state) const {
    int best = -1;
    for (std::size_t i = 0; i < instances_.size(); ++i)
        if (status(state, int(i)) == RunStatus::Running &&
            (best < 0 || instances_[i].depth >= instances_[std::size_t(best)].depth))
            best = int(i);
    return best;
}

long Run::nextFailure(std::size_t applied) const {
    const auto found = std::lower_bound(failuresAt_.begin(), failuresAt_.end(), applied);
    return found == failuresAt_.end() ? -1 : long(*found);
}

long Run::previousFailure(std::size_t applied) const {
    if (applied < 2)
        return -1;
    const auto found = std::lower_bound(failuresAt_.begin(), failuresAt_.end(), applied - 1);
    return found == failuresAt_.begin() ? -1 : long(*std::prev(found));
}

bool readRun(const std::filesystem::path &path, Run &run, std::string &error) {
    std::string bytes;
    if (!readFile(path, bytes)) {
        error = "cannot read " + path.string();
        return false;
    }
    RunLog log;
    if (!log.feed(bytes, error))
        return false;
    if (!log.hasHeader()) {
        error = path.filename().string() + " ends inside its header";
        return false;
    }
    if (!run.load(log.xml(), error))
        return false;
    run.append(log.takeTransitions());
    return true;
}
} // namespace notgroot::core
