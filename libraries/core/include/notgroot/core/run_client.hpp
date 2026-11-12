// Watching doTask live through its run monitor (contract section 8): a thread asks for the latest run and its new
// transitions about 20 times a second, so nothing is missed between polls, and fetches each run's tree once. The UI
// takes what came in each frame (take()), as with the BehaviorTree.CPP check.
#pragma once
#include "notgroot/core/run.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace notgroot::core {
// The run the monitor holds (its RUN reply).
struct RunInfo {
    std::string run, mission, log; // id, the mission doTask was asked for, its .btlog on the robot
    std::int64_t startUsec = 0;
    std::size_t transitions = 0;
    bool ended = false;
    std::string result; // RUNNING, then SUCCESS, FAILURE, CANCELED or ERROR
};

// Blackboard values by subtree instance path (_fullpath), as text: strings as they are, anything else as JSON.
using Blackboards = std::map<std::string, std::vector<std::pair<std::string, std::string>>>;

class RunClient {
  public:
    static constexpr int kDefaultPort = 1670;
    RunClient();
    ~RunClient(); // stops the thread (within a tenth of a second)
    RunClient(const RunClient &) = delete;
    RunClient &operator=(const RunClient &) = delete;

    // "host", "host:port" or "tcp://host:port" -> "tcp://host:port". False when it isn't one.
    static bool endpoint(const std::string &address, std::string &out);
    // Starts watching (stops watching anything before). False when the address isn't one.
    bool connect(const std::string &address);
    void disconnect();
    bool active() const {
        return !address_.empty();
    }
    const std::string &address() const {
        return address_;
    }

    // What came in since the last take().
    struct Update {
        // A new run: its info and header XML (the transitions below are its).
        std::optional<RunInfo> started;
        std::string xml;
        std::vector<Transition> transitions;
        std::optional<RunInfo> info; // the latest RUN or TRANS reply (ended, result, count)
        bool online = false;         // the monitor answers
        std::string status;          // "waiting for a run", "no answer from tcp://orin2:1670"...
    };
    std::optional<Update> take();
    // Seconds since the monitor last answered about this run (none: it hasn't since connecting).
    std::optional<double> sinceAnswered(const std::string &run) const;

    // Asks for these instances' blackboards in a run, once (the next poll); the answer comes from takeBlackboards().
    void askBlackboards(const std::string &run, const std::vector<std::string> &paths);
    struct BlackboardReply {
        std::string run;
        Blackboards values;
        std::string error; // "run ended"...
    };
    std::optional<BlackboardReply> takeBlackboards();

    // The JSON replies (exposed for tests).
    static bool parseInfo(const std::string &json, RunInfo &);
    static bool parseBlackboards(const std::string &json, Blackboards &);

  private:
    struct State;
    static void watch(std::shared_ptr<State>); // the thread
    std::shared_ptr<State> state_;
    std::string address_;
};
} // namespace notgroot::core
