// Run view: the run's session, card statuses, playback, the Run panel, the Timeline and the breadcrumb.
#include "notgroot/ui/run_view.hpp"
#include "notgroot/ui/style.hpp"
#include "notgroot/ui/theme.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace notgroot::ui {
using namespace core;

namespace {
RunView &viewOf(Session &session) {
    return *session.runView;
}

const RunInstance &instanceAt(const RunView &view, int i) {
    return view.run.instances()[std::size_t(i)];
}

// "m:ss.s", or "h:mm:ss" for long runs; with a ruler's step, as many decimals as it needs.
std::string clockText(std::int64_t usec, std::int64_t step = 100000) {
    const double seconds = double(std::max<std::int64_t>(usec, 0)) / 1e6;
    const int decimals = step >= 1000000 ? 0 : step >= 100000 ? 1 : step >= 10000 ? 2 : 3;
    char text[32];
    if (seconds >= 3600)
        std::snprintf(text, sizeof(text), "%d:%02d:%02d", int(seconds / 3600), int(seconds / 60) % 60,
                      int(seconds) % 60);
    else
        std::snprintf(text, sizeof(text), "%d:%0*.*f", int(seconds / 60), decimals ? decimals + 3 : 2, decimals,
                      std::floor(std::fmod(seconds, 60.) * std::pow(10., decimals)) / std::pow(10., decimals));
    return text;
}

// Shows the Timeline from `from` to `to`, kept inside the run and no shorter than a millisecond.
void showSpan(RunView &view, std::int64_t from, std::int64_t to) {
    const auto duration = std::max<std::int64_t>(view.run.duration(), 1);
    const auto length = std::clamp<std::int64_t>(to - from, std::min<std::int64_t>(1000, duration), duration);
    view.shownFrom = std::clamp<std::int64_t>(from, 0, duration - length);
    view.shownTo = view.shownFrom + length;
}

// A round step between ruler marks, at least `gap` pixels apart.
std::int64_t rulerStep(double span, float width, float gap) {
    static const std::int64_t steps[] = {1000,     2000,     5000,      10000,     20000,     50000,     100000,
                                         200000,   500000,   1000000,   2000000,   5000000,   10000000,  15000000,
                                         30000000, 60000000, 120000000, 300000000, 600000000, 900000000, 1800000000};
    for (const auto step : steps)
        if (double(step) / span * width >= gap)
            return step;
    return 3600000000;
}

// After the playhead moves: the canvas follows the deepest running instance, if asked to.
void moved(Session &session) {
    auto &view = viewOf(session);
    if (!view.follow)
        return;
    const int deepest = view.run.deepestRunning(view.state);
    if (deepest >= 0 && deepest != view.frame) {
        focusFrame(session, deepest);
        return;
    }
    // In the same frame, the node running now comes into view when it moves (a scroll only as far as needed).
    const auto running = runningNode(session);
    if (!running.empty() && running != view.running) {
        view.running = running;
        session.setCursor(running);
    }
}

// Whether an instance (or one under it) is worth a row under the filter.
bool shown(const RunView &view, int i) {
    if (view.filter == RunView::Filter::All)
        return true;
    if (view.filter == RunView::Filter::Ran ? view.run.ran(i) : view.run.failures(i) > 0)
        return true;
    for (const int child : instanceAt(view, i).children)
        if (shown(view, child))
            return true;
    return false;
}

void dot(ImVec4 color, float radius) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddCircleFilled({at.x + radius, at.y + h / 2}, radius, ImGui::GetColorU32(color));
    ImGui::Dummy({radius * 2, h});
}

ImVec4 instanceColor(const RunView &view, int i) {
    const auto status = view.run.status(view.state, i);
    if (status != RunStatus::Idle)
        return runStatusColor(status);
    const auto last = view.state.last.empty() ? RunStatus::Idle : view.state.last[view.run.statusUid(i)];
    return last == RunStatus::Idle ? withAlpha(palette().muted, .35f) : withAlpha(runStatusColor(last), .5f);
}

// Two elements are the same node when their IDs, ports and children match; a port BehaviorTree.CPP filled in from
// its model's default counts as left out.
bool sameNode(const Element &ran, const Element &source, const Catalog &catalog) {
    if (nodeId(ran) != nodeId(source))
        return false;
    const auto *model = catalog.find(nodeId(ran));
    const auto value = [](const Element &e, const std::string &name) -> const std::string * {
        if (name == "ID" || (name == "name" && e.get("name") == nodeId(e)))
            return nullptr;
        return e.find(name);
    };
    std::set<std::string> names;
    for (const auto *e : {&ran, &source})
        for (const auto &attribute : e->attributes)
            names.insert(attribute.name);
    for (const auto &name : names) {
        const auto *a = value(ran, name), *b = value(source, name);
        if (a && b && *a == *b)
            continue;
        if (!a && !b)
            continue;
        const auto *port = model ? model->port(name) : nullptr;
        if (a && !b && port && *a == port->defaultValue)
            continue;
        return false;
    }
    const auto ranChildren = ran.elements(), sourceChildren = source.elements();
    if (ranChildren.size() != sourceChildren.size())
        return false;
    for (std::size_t i = 0; i < ranChildren.size(); ++i)
        if (!sameNode(*ranChildren[i], *sourceChildren[i], catalog))
            return false;
    return true;
}

// The transitions of one node, by index (cached for the last node asked about).
const std::vector<std::size_t> &changesOf(const RunView &view, std::uint16_t uid) {
    static const Run *forRun = nullptr;
    static std::uint16_t forUid = 0;
    static std::size_t counted = 0;
    static std::vector<std::size_t> changes;
    const auto &transitions = view.run.transitions();
    if (forRun != &view.run || forUid != uid || counted > transitions.size()) {
        forRun = &view.run;
        forUid = uid;
        counted = 0;
        changes.clear();
    }
    for (; counted < transitions.size(); ++counted)
        if (transitions[counted].uid == uid)
            changes.push_back(counted);
    return changes;
}

// The uid of a card's node (0: none, e.g. the tree's own card).
std::uint16_t cardUid(const Session &session, const CardKey &key) {
    const int instance = cardInstance(session, key);
    if (instance < 0)
        return 0;
    if (key.ref.isTree())
        return session.runView->run.statusUid(instance);
    return session.runView->run.uidAt(instance, key.ref.path);
}
} // namespace

bool openRun(Session &session, const std::filesystem::path &path, std::string &error) {
    auto view = std::make_shared<RunView>();
    if (!readRun(path, view->run, error))
        return false;
    if (!session.openText(path, view->run.definitions(), error))
        return false;
    view->file = path;
    // BTLog_2026_11_09_02:08:30 -> 11-09 02:08:30
    std::string stem = path.stem().string();
    if (stem.rfind("BTLog_", 0) == 0 && stem.size() >= 25)
        stem = stem.substr(11, 2) + "-" + stem.substr(14, 2) + " " + stem.substr(17);
    view->title = stem + " · " + view->run.mission();
    session.attachRun(view);
    // A log is opened to see how it went: start at its end.
    seekTransitions(session, view->run.transitions().size());
    focusFrame(session, 0);
    session.status("Opened run " + path.filename().string() + ": " + std::to_string(view->run.instances().size()) +
                   " subtree instances, " + std::to_string(view->run.transitions().size()) + " transitions");
    return true;
}

ImVec4 runStatusColor(RunStatus status) {
    switch (status) {
    case RunStatus::Running:
        return palette().warn;
    case RunStatus::Success:
        return palette().robotEnabled;
    case RunStatus::Failure:
        return palette().error;
    case RunStatus::Skipped:
        return palette().muted;
    default:
        return withAlpha(palette().muted, .35f);
    }
}

const char *runStatusName(RunStatus status) {
    switch (status) {
    case RunStatus::Running:
        return "running";
    case RunStatus::Success:
        return "success";
    case RunStatus::Failure:
        return "failure";
    case RunStatus::Skipped:
        return "skipped";
    default:
        return "idle";
    }
}

int cardInstance(const Session &session, const CardKey &key) {
    if (!session.runView)
        return -1;
    const auto &run = session.runView->run;
    int instance = session.runView->frame;
    if (key.inside()) {
        std::vector<NodeRef> calls{key.origin};
        calls.insert(calls.end(), key.via.begin(), key.via.end());
        for (const auto &call : calls)
            if ((instance = run.calledBy(run.uidAt(instance, call.path))) < 0)
                return -1;
    }
    return instance;
}

bool cardStatus(const Session &session, const CardKey &key, RunStatus &status, bool &faded) {
    const auto uid = cardUid(session, key);
    const auto &state = session.runView->state;
    if (!uid || uid >= state.status.size())
        return false;
    status = state.status[uid];
    faded = status == RunStatus::Idle;
    if (faded)
        status = state.last[uid];
    return status != RunStatus::Idle;
}

NodeRef runningNode(const Session &session) {
    const auto &view = *session.runView;
    const auto &instance = instanceAt(view, view.frame);
    const std::vector<std::size_t> *deepest = nullptr;
    for (const auto uid : instance.uids)
        if (uid < view.state.status.size() && view.state.status[uid] == RunStatus::Running)
            if (const auto *node = view.run.node(uid); node && (!deepest || node->path.size() >= deepest->size()))
                deepest = &node->path;
    return deepest ? NodeRef{instance.id, *deepest} : NodeRef{};
}

void focusFrame(Session &session, int instance) {
    auto &view = viewOf(session);
    if (instance < 0 || std::size_t(instance) >= view.run.instances().size())
        return;
    const auto &id = instanceAt(view, instance).id;
    const bool otherTree = session.active != id;
    view.frame = instance;
    view.revealFrame = true;
    session.tabs = {id};
    session.openTree(id);
    // The cursor on what runs deepest here (else the root), and that node in view.
    view.running = runningNode(session);
    session.setCursor(view.running.empty() ? NodeRef{id, {}} : view.running);
    session.view(id).fitOn = view.running;
    // A new tree is fitted, around that node: scrolling to it as well would pin it to an edge while the canvas is
    // still settling. The same tree scrolls only as far as it needs.
    if (otherTree) {
        session.view(id).fit = true;
        session.revealPending = false;
    }
}

void seekTransitions(Session &session, std::size_t applied) {
    auto &view = viewOf(session);
    view.run.seekIndex(view.state, applied);
    view.clock = view.state.applied ? view.run.transitions()[view.state.applied - 1].usec : 0;
    moved(session);
}

void seekTime(Session &session, std::int64_t usec) {
    auto &view = viewOf(session);
    view.run.seek(view.state, usec);
    view.clock = std::clamp<std::int64_t>(usec, 0, view.run.duration());
    moved(session);
}

bool advanceRun(Session &session, double seconds) {
    if (!session.runView || !session.runView->playing)
        return false;
    auto &view = viewOf(session);
    const auto to = view.clock + std::int64_t(seconds * 1e6 * view.speed);
    seekTime(session, to);
    if (to >= view.run.duration())
        view.playing = false;
    return true;
}

void compareTrees(RunView &view, const Workspace &runTrees, const Workspace &editor) {
    view.drifted.clear();
    for (const auto &[key, entry] : runTrees.trees()) {
        if (key == view.run.mainTree())
            continue;
        const auto *source = editor.tree(key);
        if (!source || !sameNode(*entry.tree, *source->tree, runTrees.catalog()))
            view.drifted.insert(key);
    }
}

NodeRef sourceRef(const Workspace &runTrees, const Workspace &editor, const RunView &view, const NodeRef &ref) {
    const auto *ran = runTrees.tree(ref.tree);
    const auto *source = editor.tree(ref.tree);
    if (!ran || !source || view.drifted.count(ref.tree) || ref.tree == view.run.mainTree())
        return {};
    // The same element by position among elements (the two may have comments in different places).
    const Element *at = ran->tree, *there = source->tree;
    NodeRef out{ref.tree, {}};
    for (const auto index : ref.path) {
        const auto *child = index < at->children.size() ? at->children[index].get() : nullptr;
        if (!child)
            return {};
        const auto siblings = at->elements();
        const auto nth = std::size_t(std::find(siblings.begin(), siblings.end(), child) - siblings.begin());
        const auto theirs = there->elements();
        if (nth >= theirs.size())
            return {};
        at = child;
        there = theirs[nth];
        out.path.push_back(there->index());
    }
    return out;
}

void drawBreadcrumb(Session &session) {
    auto &view = viewOf(session);
    std::vector<int> chain;
    for (int i = view.frame; i >= 0; i = instanceAt(view, i).parent)
        chain.insert(chain.begin(), i);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ui(4), ImGui::GetStyle().ItemSpacing.y});
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(palette().warn, "RUN");
    for (const int i : chain) {
        ImGui::SameLine();
        ImGui::TextDisabled(i == chain.front() ? "" : "›");
        ImGui::SameLine();
        dot(instanceColor(view, i), ui(4));
        ImGui::SameLine();
        ImGui::PushID(i);
        if (i == view.frame)
            ImGui::TextUnformatted(instanceAt(view, i).label.c_str());
        else if (ImGui::SmallButton(instanceAt(view, i).label.c_str()))
            focusFrame(session, i);
        ImGui::PopID();
    }
    if (view.drifted.count(instanceAt(view, view.frame).id)) {
        ImGui::SameLine();
        ImGui::TextColored(palette().warn, "  changed since this run");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The editor's %s differs from the one that ran: this shows it as it ran.",
                              instanceAt(view, view.frame).id.c_str());
    }
    ImGui::PopStyleVar();
    ImGui::Separator();
}

void drawRunPanel(Session &session) {
    auto &view = viewOf(session);
    const auto &run = view.run;
    ImGui::TextUnformatted(view.title.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", view.file.string().c_str());
    const auto result = run.status(view.state, 0);
    ImGui::TextDisabled("%s · %zu nodes · %zu transitions", clockText(run.duration()).c_str(), run.nodeCount(),
                        run.transitions().size());
    if (result != RunStatus::Idle) {
        ImGui::SameLine();
        ImGui::TextColored(runStatusColor(result), "%s", runStatusName(result));
    }
    if (ImGui::SmallButton("Editor (F6)"))
        session.run("run.switch");
    ImGui::SameLine();
    if (ImGui::SmallButton("Close run"))
        session.run("run.close");
    if (!view.drifted.empty()) {
        ImGui::TextColored(palette().warn, "%zu %s changed since this run", view.drifted.size(),
                           view.drifted.size() == 1 ? "subtree" : "subtrees");
        if (ImGui::IsItemHovered()) {
            std::string list;
            for (const auto &id : view.drifted)
                list += id + "\n";
            ImGui::SetTooltip("Shown as they ran; the editor's differ:\n%s", list.c_str());
        }
    }

    int filter = int(view.filter);
    ImGui::RadioButton("ran", &filter, int(RunView::Filter::Ran));
    ImGui::SameLine();
    ImGui::RadioButton("failed", &filter, int(RunView::Filter::Failed));
    ImGui::SameLine();
    ImGui::RadioButton("all", &filter, int(RunView::Filter::All));
    view.filter = RunView::Filter(filter);

    // The cursor's node: its status and when it changed.
    const float nodeHeight = ImGui::GetTextLineHeightWithSpacing() * 3 + ImGui::GetStyle().ItemSpacing.y * 2;
    std::vector<bool> onPath(run.instances().size(), false);
    for (int i = view.frame; i >= 0; i = instanceAt(view, i).parent)
        onPath[std::size_t(i)] = true;

    const auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    // Narrow indents: the outline goes seven calls deep.
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, ui(9));
    if (ImGui::BeginTable("##instances", 3, flags, {0, -nodeHeight})) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Subtree", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Time");
        ImGui::TableSetupColumn("Fail");
        tableHeaders();
        std::function<void(int)> row = [&](int i) {
            const auto &instance = instanceAt(view, i);
            std::vector<int> children;
            for (const int child : instance.children)
                if (shown(view, child))
                    children.push_back(child);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            dot(instanceColor(view, i), ui(4));
            ImGui::SameLine();
            if (view.revealFrame && onPath[std::size_t(i)] && i != view.frame)
                ImGui::SetNextItemOpen(true);
            ImGuiTreeNodeFlags treeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (instance.depth < 1)
                treeFlags |= ImGuiTreeNodeFlags_DefaultOpen;
            if (children.empty())
                treeFlags |= ImGuiTreeNodeFlags_Leaf;
            if (i == view.frame)
                treeFlags |= ImGuiTreeNodeFlags_Selected;
            ImGui::PushID(i);
            const bool open = ImGui::TreeNodeEx(instance.label.c_str(), treeFlags);
            if (i == view.frame && view.revealFrame) {
                ImGui::SetScrollHereY();
                view.revealFrame = false;
            }
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                focusFrame(session, i);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && run.ran(i))
                seekTime(session, run.spans(i).front().start);
            const auto &spans = run.spans(i);
            std::int64_t total = 0;
            for (const auto &span : spans)
                total += (span.end < 0 ? run.duration() : span.end) - span.start;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n%s\n%s", instance.id.c_str(), instance.path.c_str(),
                                  spans.empty()
                                      ? "not run"
                                      : ("first ran at " + clockText(spans.front().start) + ", " +
                                         std::to_string(spans.size()) + (spans.size() == 1 ? " time" : " times"))
                                            .c_str());
            ImGui::TableNextColumn();
            if (!spans.empty())
                ImGui::TextDisabled("%s", clockText(total).c_str());
            ImGui::TableNextColumn();
            if (const int failures = run.failures(i))
                ImGui::TextColored(palette().error, "%d", failures);
            if (open) {
                for (const int child : children)
                    row(child);
                ImGui::TreePop();
            }
            ImGui::PopID();
        };
        row(0);
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    ImGui::Separator();
    const auto uid = cardUid(session, session.cursor);
    if (!uid || session.cursor.ref.isTree()) {
        ImGui::TextDisabled("Click a node to see when it changed.");
        return;
    }
    RunStatus status = RunStatus::Idle;
    bool faded = false;
    cardStatus(session, session.cursor, status, faded);
    const auto &changes = changesOf(view, uid);
    const auto *element = resolve(session.workspace, session.cursor.ref);
    ImGui::Text("%s", element ? element->get("name", nodeId(*element)).c_str() : "?");
    ImGui::SameLine();
    ImGui::TextColored(faded ? withAlpha(runStatusColor(status), .6f) : runStatusColor(status), "%s%s",
                       runStatusName(status), faded && status != RunStatus::Idle ? " (last)" : "");
    ImGui::TextDisabled("%zu changes", changes.size());
    // Its changes before and after the playhead.
    const auto after = std::lower_bound(changes.begin(), changes.end(), view.state.applied);
    // On a change already (the playhead just after it): the one before that.
    auto before = after;
    if (before != changes.begin() && *std::prev(before) + 1 == view.state.applied)
        --before;
    ImGui::BeginDisabled(before == changes.begin());
    if (ImGui::SmallButton("< previous change"))
        seekTransitions(session, *std::prev(before) + 1);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(after == changes.end());
    if (ImGui::SmallButton("next change >"))
        seekTransitions(session, *after + 1);
    ImGui::EndDisabled();
}

void drawTimeline(Session &session) {
    auto &view = viewOf(session);
    const auto &run = view.run;
    const auto duration = std::max<std::int64_t>(run.duration(), 1);
    // The transport keys (P, , . [ ] F) work here as on the canvas: clicking the timeline shouldn't take them away.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput)
        session.canvasFocused = true;

    // Transport.
    const auto button = [&](const char *label, const char *command, const char *tip) {
        if (ImGui::Button(label))
            session.run(command);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tip);
        ImGui::SameLine();
    };
    if (ImGui::Button("|<"))
        seekTransitions(session, 0);
    ImGui::SameLine();
    button("[", "run.previousFailure", "Previous subtree failure ([)");
    button("<", "run.previous", "Back a transition (,)");
    button(view.playing ? "Pause" : "Play", "run.play", "Play / pause (P)");
    button(">", "run.next", "Next transition (.)");
    button("]", "run.nextFailure", "Next subtree failure (])");
    if (ImGui::Button(">|"))
        seekTransitions(session, run.transitions().size());
    ImGui::SameLine();
    ImGui::Checkbox("Follow (F)", &view.follow);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ui(70));
    static const float speeds[] = {.1f, .25f, .5f, 1, 2, 5, 10, 30};
    char speedText[16];
    std::snprintf(speedText, sizeof(speedText), "%g×", double(view.speed));
    if (ImGui::BeginCombo("##speed", speedText)) {
        for (const float speed : speeds) {
            std::snprintf(speedText, sizeof(speedText), "%g×", double(speed));
            if (ImGui::Selectable(speedText, speed == view.speed))
                view.speed = speed;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Text("%s / %s", clockText(view.clock).c_str(), clockText(run.duration()).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("transition %zu of %zu", view.state.applied, run.transitions().size());

    ImGui::SameLine();
    if (ImGui::SmallButton("Fit"))
        view.shownFrom = view.shownTo = 0;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show the whole run (double-click the bar)");
    ImGui::SameLine();
    if (ImGui::SmallButton("Frame") && run.ran(view.frame)) {
        const auto &spans = run.spans(view.frame);
        const auto last = spans.back().end < 0 ? run.duration() : spans.back().end;
        const auto pad = std::max<std::int64_t>((last - spans.front().start) / 20, 1000);
        showSpan(view, spans.front().start - pad, last + pad);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Zoom to when the subtree on the canvas ran");

    // The span shown, moved as the canvas is: Ctrl+wheel or a pinch zooms at the pointer, Shift+wheel (or a sideways
    // swipe) and dragging the lanes pan, the wheel scrolls the lanes, Shift+drag zooms to a range.
    if (view.shownTo <= view.shownFrom)
        view.shownFrom = 0, view.shownTo = duration;
    // Playing past the edge of a zoomed span pages it along.
    if (view.playing && (view.clock > view.shownTo || view.clock < view.shownFrom)) {
        const auto length = view.shownTo - view.shownFrom;
        showSpan(view, view.clock - length / 10, view.clock - length / 10 + length);
    }
    const float labelWidth = ui(170);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = std::max(ImGui::GetContentRegionAvail().x - labelWidth, ui(40));
    const float laneX = origin.x + labelWidth;
    const auto span = double(view.shownTo - view.shownFrom);
    const auto xOf = [&](std::int64_t t) { return laneX + float(double(t - view.shownFrom) / span * width); };
    const auto timeAt = [&](float x) { return view.shownFrom + std::int64_t(double(x - laneX) / width * span); };
    const auto seekAt = [&](float x) { seekTime(session, std::clamp(timeAt(x), std::int64_t(0), run.duration())); };
    auto *draw = ImGui::GetWindowDrawList();
    const auto &io = ImGui::GetIO();
    const float bottom = ImGui::GetWindowPos().y + ImGui::GetWindowHeight();

    // Scrub bar: a ruler, failures as ticks. Clicking or dragging moves the playhead; playing goes on from there
    // (held while dragging). Double-click shows the whole run.
    const float barHeight = ui(20);
    ImGui::SetCursorScreenPos({laneX, origin.y});
    ImGui::InvisibleButton("##scrub", {width, barHeight});
    const bool barHovered = ImGui::IsItemHovered();
    if (ImGui::IsItemActive())
        seekAt(io.MousePos.x);
    if (barHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        view.shownFrom = view.shownTo = 0;
    draw->AddRectFilled({laneX, origin.y}, {laneX + width, origin.y + barHeight},
                        ImGui::GetColorU32(ImGui::GetStyle().Colors[ImGuiCol_FrameBg]), ui(3));
    const auto step = rulerStep(span, width, ui(90));
    draw->PushClipRect({laneX, origin.y}, {laneX + width, bottom}, true);
    for (auto t = (view.shownFrom / step + 1) * step; t < view.shownTo; t += step)
        draw->AddLine({xOf(t), origin.y}, {xOf(t), bottom}, ImGui::GetColorU32(withAlpha(palette().muted, .15f)));
    for (long at = run.nextFailure(0); at >= 0; at = run.nextFailure(std::size_t(at) + 1)) {
        const auto t = run.transitions()[std::size_t(at)].usec;
        if (t < view.shownFrom || t > view.shownTo)
            continue;
        const float x = xOf(t);
        draw->AddLine({x, origin.y + ui(3)}, {x, origin.y + barHeight - ui(3)}, ImGui::GetColorU32(palette().error),
                      ui(1.5f));
    }
    // The ruler's times over the failures, each on a patch of the bar.
    for (auto t = (view.shownFrom / step + 1) * step; t < view.shownTo; t += step) {
        const auto label = clockText(t, step);
        const ImVec2 at{xOf(t) + ui(3), origin.y + (barHeight - ImGui::GetTextLineHeight()) / 2};
        draw->AddRectFilled({at.x - ui(2), origin.y + ui(2)},
                            {at.x + ImGui::CalcTextSize(label.c_str()).x + ui(2), origin.y + barHeight - ui(2)},
                            ImGui::GetColorU32(ImGui::GetStyle().Colors[ImGuiCol_FrameBg]), ui(2));
        draw->AddText(at, ImGui::GetColorU32(withAlpha(palette().muted, .9f)), label.c_str());
    }
    draw->PopClipRect();
    draw->AddText({origin.x, origin.y + (barHeight - ImGui::GetTextLineHeight()) / 2},
                  ImGui::GetColorU32(palette().muted),
                  (clockText(view.shownFrom, step) + " – " + clockText(view.shownTo, step)).c_str());

    // Lanes: the frame's callers, the frame, then what it called that ran.
    std::vector<int> lanes;
    for (int i = view.frame; i >= 0; i = instanceAt(view, i).parent)
        lanes.insert(lanes.begin(), i);
    const auto callers = lanes.size();
    for (const int child : instanceAt(view, view.frame).children)
        if (run.ran(child))
            lanes.push_back(child);
    const float laneHeight = ImGui::GetTextLineHeight() + ui(4);
    ImGui::SetCursorScreenPos({origin.x, origin.y + barHeight + ui(4)});
    ImGui::BeginChild("##lanes", {0, 0}, false, ImGuiWindowFlags_NoScrollWithMouse);
    auto *lanesDraw = ImGui::GetWindowDrawList();
    const float top = ImGui::GetCursorScreenPos().y; // the first lane, scrolled
    const float shownHeight = ImGui::GetWindowHeight();
    for (std::size_t n = 0; n < lanes.size(); ++n) {
        const int i = lanes[n];
        const float y = top + float(n) * laneHeight;
        ImGui::SetCursorScreenPos({origin.x, y});
        ImGui::PushID(int(n));
        std::string label = std::string(n > callers - 1 ? "  " : "") + instanceAt(view, i).label;
        if (ImGui::Selectable(label.c_str(), i == view.frame, 0, {labelWidth - ui(6), laneHeight - ui(2)}))
            focusFrame(session, i);
        ImGui::PopID();
        lanesDraw->PushClipRect({laneX, y}, {laneX + width, y + laneHeight}, true);
        for (const auto &s : run.spans(i)) {
            const auto end = s.end < 0 ? run.duration() : s.end;
            if (end < view.shownFrom || s.start > view.shownTo)
                continue;
            const float a = std::max(xOf(s.start), laneX - 2), b = std::max(xOf(end), a + 2);
            lanesDraw->AddRectFilled({a, y + ui(2)}, {b, y + laneHeight - ui(2)},
                                     ImGui::GetColorU32(withAlpha(runStatusColor(s.result), .8f)), ui(2));
        }
        lanesDraw->PopClipRect();
        if (n + 1 == callers)
            lanesDraw->AddLine({laneX, y + laneHeight}, {laneX + width, y + laneHeight},
                               ImGui::GetColorU32(withAlpha(palette().muted, .4f)));
    }
    // The lanes' area: a click moves the playhead, a drag (or a middle or right drag) pans, Shift+drag picks a span
    // to zoom to.
    const float areaHeight = std::max(float(lanes.size()) * laneHeight, shownHeight);
    ImGui::SetCursorScreenPos({laneX, top});
    ImGui::InvisibleButton("##area", {width, std::max(areaHeight, 1.f)},
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle |
                               ImGuiButtonFlags_MouseButtonRight);
    const bool lanesHovered = ImGui::IsItemHovered();
    static float pressX = 0;
    static bool ranging = false, panned = false, left = false;
    if (ImGui::IsItemActivated()) {
        pressX = io.MousePos.x;
        left = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        ranging = io.KeyShift && left;
        panned = false;
    }
    if (ImGui::IsItemActive()) {
        const bool dragged = std::abs(io.MousePos.x - pressX) > ui(4) || !left;
        if (ranging) {
            const ImVec2 a{std::min(pressX, io.MousePos.x), ImGui::GetWindowPos().y},
                b{std::max(pressX, io.MousePos.x), ImGui::GetWindowPos().y + ImGui::GetWindowHeight()};
            lanesDraw->AddRectFilled(a, b, ImGui::GetColorU32(withAlpha(palette().accent, .15f)));
            lanesDraw->AddRect(a, b, ImGui::GetColorU32(withAlpha(palette().accent, .7f)));
        } else if (dragged && io.MouseDelta.x != 0) {
            panned = true;
            const auto shift = std::int64_t(double(io.MouseDelta.x) / width * span);
            showSpan(view, view.shownFrom - shift, view.shownTo - shift);
        }
    }
    if (ImGui::IsItemDeactivated()) {
        if (ranging && std::abs(io.MousePos.x - pressX) > ui(4))
            showSpan(view, timeAt(std::min(pressX, io.MousePos.x)), timeAt(std::max(pressX, io.MousePos.x)));
        else if (left && !ranging && !panned && std::abs(io.MousePos.x - pressX) <= ui(4))
            seekAt(pressX);
        ranging = false;
    }
    // The wheel scrolls the lanes (the ones that don't fit).
    if (ImGui::IsWindowHovered() && !io.KeyCtrl && !io.KeyShift && io.MouseWheel != 0)
        ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseWheel * laneHeight * 3);
    ImGui::EndChild();

    // The playhead across the bar and the lanes.
    if (view.clock >= view.shownFrom && view.clock <= view.shownTo) {
        const float x = xOf(view.clock);
        draw->AddLine({x, origin.y}, {x, bottom}, ImGui::GetColorU32(palette().accent), ui(1.5f));
    }

    // Zoom and pan with the wheel, a sideways swipe or a pinch, over the bar or the lanes.
    static bool pinching = false;
    static double pinchSpan = 0;
    static std::int64_t pinchAt = 0;
    const bool over = barHovered || lanesHovered;
    if (session.pinch.active && (over || pinching)) {
        if (!pinching) {
            pinchSpan = span;
            pinchAt = timeAt(io.MousePos.x);
        }
        pinching = true;
        const double length = pinchSpan / std::max(double(session.pinch.scale), .05);
        const double before = double(pinchAt - view.shownFrom) / span;
        showSpan(view, pinchAt - std::int64_t(length * before),
                 pinchAt - std::int64_t(length * before) + std::int64_t(length));
    } else
        pinching = false;
    if (!over)
        return;
    // Scrolling left (a positive sideways wheel, or Shift+wheel up) shows earlier times, as on the canvas.
    const float sideways = io.MouseWheelH != 0 ? io.MouseWheelH : io.KeyShift ? io.MouseWheel : 0;
    if (io.KeyCtrl && io.MouseWheel != 0) {
        const auto at = timeAt(io.MousePos.x);
        const double factor = std::pow(.8, double(io.MouseWheel));
        showSpan(view, at - std::int64_t(double(at - view.shownFrom) * factor),
                 at + std::int64_t(double(view.shownTo - at) * factor));
    } else if (sideways != 0) {
        const auto shift = std::int64_t(double(sideways) * span / 10);
        showSpan(view, view.shownFrom - shift, view.shownTo - shift);
    }
}
} // namespace notgroot::ui
