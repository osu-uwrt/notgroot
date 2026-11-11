// Viewing a run (core/run.hpp) like a debugger: the run's trees open read-only in their own Session, the canvas shows
// one subtree instance at a time (a frame) with each card's status at the playhead, the Run panel lists the instances
// as a call stack, and the Timeline plays, scrubs and steps through the transitions.
#pragma once
#include "notgroot/core/run.hpp"
#include "notgroot/ui/session.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <string>

namespace notgroot::ui {
struct RunView {
    core::Run run;
    std::filesystem::path file;
    std::string title;    // "11-09 02:08:30 · CompMissionTreeMpc"
    core::RunState state; // at the playhead
    int frame = 0;        // the instance on the canvas
    bool playing = false;
    bool follow = false;    // the canvas follows the deepest running instance
    float speed = 1;        // playback, times real time
    std::int64_t clock = 0; // the playhead in µs since the start (between transitions while playing)
    enum class Filter { Ran, Failed, All } filter = Filter::Ran;
    std::set<std::string> drifted;           // trees that differ from the editor's (by ID)
    bool revealFrame = true;                 // the Run panel scrolls to the frame's row
    std::int64_t shownFrom = 0, shownTo = 0; // the Timeline's span (equal: the whole run)
    core::NodeRef running;                   // the frame's deepest running node last shown (Follow keeps it in view)
};

// Opens a .btlog in a session made for it; false and `error` when it can't be read.
bool openRun(Session &, const std::filesystem::path &, std::string &error);

ImVec4 runStatusColor(core::RunStatus);
const char *runStatusName(core::RunStatus);
// The instance a card is in (its frame, or a subtree opened in place under a call), -1 if none.
int cardInstance(const Session &, const CardKey &);
// What a card shows at the playhead: false when it has nothing to show. `faded`: idle again, showing its last result.
bool cardStatus(const Session &, const CardKey &, core::RunStatus &status, bool &faded);

// The deepest node running in the frame (empty ref: none runs).
core::NodeRef runningNode(const Session &);
// Puts an instance on the canvas, with its deepest running node in view and under the cursor.
void focusFrame(Session &, int instance);
// The playhead: `applied` transitions, or a moment.
void seekTransitions(Session &, std::size_t applied);
void seekTime(Session &, std::int64_t usec);
// Moves playback on by `seconds` of wall time; true while it plays (keep drawing).
bool advanceRun(Session &, double seconds);
// Which of the run's trees differ from the editor's (a mission's main tree is never compared).
void compareTrees(RunView &, const core::Workspace &runTrees, const core::Workspace &editor);
// The node in the editor's trees that a node of the run's trees is (null ref when its tree drifted or is gone).
core::NodeRef sourceRef(const core::Workspace &runTrees, const core::Workspace &editor, const RunView &,
                        const core::NodeRef &);

// The Run panel: the run, its instances as a call stack, and the cursor node's history.
void drawRunPanel(Session &);
// The Timeline: transport, scrub bar with failures, and lanes for the frame's callers and calls.
void drawTimeline(Session &);
// Above the canvas while viewing a run: where the frame is (click a caller to go up to it).
void drawBreadcrumb(Session &);
} // namespace notgroot::ui
