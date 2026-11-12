// The live link: RunClient against a stand-in for doTask's run monitor (contract section 8) serving a real .btlog as
// it grows, going away and coming back, and replaced by a new run.
#include "notgroot/core/run_client.hpp"
#include "test_support.hpp"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <zlib.h>
#include <zmq.h>

using namespace notgroot::core;
namespace fs = std::filesystem;

namespace {
// The monitor's side: one run, its transitions let out a few at a time, pages of `page` records.
class FakeMonitor {
  public:
    FakeMonitor(int port, std::size_t page) : port_(port), page_(page) {}
    ~FakeMonitor() {
        stop();
    }
    void begin(const std::string &id, const std::string &xml, std::int64_t start, std::vector<Transition> all) {
        std::lock_guard<std::mutex> lock(mutex_);
        id_ = id;
        xml_ = xml;
        start_ = start;
        all_ = std::move(all);
        shown_ = 0;
        ended_ = false;
    }
    void release(std::size_t count, bool end) {
        std::lock_guard<std::mutex> lock(mutex_);
        shown_ = std::min(count, all_.size());
        ended_ = end;
    }
    void start() {
        running_ = true;
        thread_ = std::thread([this] { serve(); });
    }
    void stop() {
        running_ = false;
        if (thread_.joinable())
            thread_.join();
    }
    std::atomic<int> requests{0};

  private:
    void serve() {
        void *context = zmq_ctx_new();
        void *socket = zmq_socket(context, ZMQ_REP);
        const int linger = 0;
        zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(linger));
        ASSERT_EQ(zmq_bind(socket, ("tcp://127.0.0.1:" + std::to_string(port_)).c_str()), 0);
        while (running_) {
            zmq_pollitem_t item = {socket, 0, ZMQ_POLLIN, 0};
            if (zmq_poll(&item, 1, 50) <= 0)
                continue;
            std::vector<std::string> request;
            for (int more = 1; more;) {
                zmq_msg_t message;
                zmq_msg_init(&message);
                zmq_msg_recv(&message, socket, 0);
                request.emplace_back(static_cast<const char *>(zmq_msg_data(&message)), zmq_msg_size(&message));
                more = zmq_msg_more(&message);
                zmq_msg_close(&message);
            }
            ++requests;
            const auto reply = answer(request);
            for (std::size_t i = 0; i < reply.size(); ++i)
                zmq_send(socket, reply[i].data(), reply[i].size(), i + 1 < reply.size() ? ZMQ_SNDMORE : 0);
        }
        zmq_close(socket);
        zmq_ctx_term(context);
    }
    std::vector<std::string> answer(const std::vector<std::string> &request) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto result = std::string(ended_ ? "FAILURE" : "RUNNING");
        const auto flag = [](bool b) { return b ? "true" : "false"; };
        if (request[0] == "RUN") {
            if (id_.empty())
                return {"NONE"};
            return {"OK", R"({"protocol":1,"run":")" + id_ + R"(","mission":"Sim","log":"/home/ros/btlogs/x.btlog",)" +
                              R"("start_usec":)" + std::to_string(start_) + R"(,"xml_bytes":)" +
                              std::to_string(xml_.size()) + R"(,"transitions":)" + std::to_string(shown_) +
                              R"(,"ended":)" + flag(ended_) + R"(,"result":")" + result + "\"}"};
        }
        if (request.size() < 2 || request[1] != id_)
            return {"ERR", "unknown run"};
        if (request[0] == "TREE") {
            uLongf size = compressBound(uLong(xml_.size()));
            std::string zipped(size, '\0');
            compress(reinterpret_cast<Bytef *>(zipped.data()), &size, reinterpret_cast<const Bytef *>(xml_.data()),
                     uLong(xml_.size()));
            zipped.resize(size);
            return {"OK", zipped};
        }
        if (request[0] == "TRANS") {
            const auto from = std::min<std::size_t>(std::stoul(request[2]), shown_);
            const auto to = std::min(shown_, from + page_);
            std::vector<Transition> part(all_.begin() + long(from), all_.begin() + long(to));
            const auto bytes = writeRunLog("", 0, part);
            const std::size_t header = std::string("BTCPP4-FileLogger2").size() + 1 + 4 + 8;
            return {"OK",
                    R"({"from":)" + std::to_string(from) + R"(,"to":)" + std::to_string(to) + R"(,"count":)" +
                        std::to_string(shown_) + R"(,"ended":)" + flag(ended_) + R"(,"result":")" + result + "\"}",
                    bytes.substr(header)};
        }
        if (request[0] == "BB") {
            if (ended_)
                return {"ERR", "run ended"};
            return {"OK", R"({"":{"depth":"1.5","n":3,"name":"café \"x\"","pose":{"x":1,"y":[2,3]}}})"};
        }
        return {"ERR", "unknown request"};
    }

    int port_;
    std::size_t page_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::string id_, xml_;
    std::int64_t start_ = 0;
    std::vector<Transition> all_;
    std::size_t shown_ = 0;
    bool ended_ = false;
};

// A sim run read back from its file.
struct Log {
    std::string bytes, xml;
    std::int64_t start = 0;
    std::vector<Transition> transitions;
};

Log simLog() {
    Log log;
    EXPECT_TRUE(readFile(notgroot::test::data("runs") / "sim.btlog", log.bytes));
    RunLog reader;
    std::string error;
    EXPECT_TRUE(reader.feed(log.bytes, error)) << error;
    log.xml = reader.xml();
    log.start = reader.startUsec();
    log.transitions = reader.takeTransitions();
    return log;
}

// What a client has seen, gathered from its updates.
struct Seen {
    int runs = 0;
    RunInfo started, info;
    std::string xml, status;
    bool online = false;
    std::vector<Transition> transitions;

    void take(RunClient &client) {
        while (auto update = client.take()) {
            if (update->started) {
                ++runs;
                started = *update->started;
                xml = update->xml;
                transitions.clear();
            }
            transitions.insert(transitions.end(), update->transitions.begin(), update->transitions.end());
            if (update->info)
                info = *update->info;
            online = update->online;
            status = update->status;
        }
    }
};

template <typename F> bool waitFor(RunClient &client, Seen &seen, F done, int ms = 5000) {
    for (int waited = 0; waited < ms; waited += 10) {
        seen.take(client);
        if (done())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

int freePort() {
    static int next = 47000 + int(::getpid() % 1000) * 3;
    return next++;
}

bool sameTransitions(const std::vector<Transition> &a, const std::vector<Transition> &b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].usec != b[i].usec || a[i].uid != b[i].uid || a[i].status != b[i].status)
            return false;
    return true;
}
} // namespace

TEST(RunClient, Endpoints) {
    std::string out;
    EXPECT_TRUE(RunClient::endpoint("orin2", out));
    EXPECT_EQ(out, "tcp://orin2:1670");
    EXPECT_TRUE(RunClient::endpoint(" localhost:1700 ", out));
    EXPECT_EQ(out, "tcp://localhost:1700");
    EXPECT_TRUE(RunClient::endpoint("tcp://10.0.0.2:1670", out));
    EXPECT_EQ(out, "tcp://10.0.0.2:1670");
    EXPECT_FALSE(RunClient::endpoint("", out));
    EXPECT_FALSE(RunClient::endpoint("orin2:", out));
    EXPECT_FALSE(RunClient::endpoint("orin2:99999", out));
    EXPECT_FALSE(RunClient::endpoint("orin 2", out));
}

TEST(RunClient, ParsesReplies) {
    RunInfo info;
    ASSERT_TRUE(RunClient::parseInfo(R"({"protocol": 1, "run": "1794464133174000", "mission": "M", "log": "/a b",
        "start_usec": 1794464133174000, "xml_bytes": 10, "transitions": 42, "ended": true, "result": "SUCCESS"})",
                                     info));
    EXPECT_EQ(info.run, "1794464133174000");
    EXPECT_EQ(info.startUsec, 1794464133174000);
    EXPECT_EQ(info.transitions, 42u);
    EXPECT_TRUE(info.ended);
    EXPECT_EQ(info.result, "SUCCESS");
    EXPECT_FALSE(RunClient::parseInfo(R"({"run": "1"})", info));
    EXPECT_FALSE(RunClient::parseInfo("{", info));

    Blackboards values;
    ASSERT_TRUE(RunClient::parseBlackboards(R"({"":{"a":"x\ny","b":2.5,"c":[1,{"d":null}]},"Go::3":{}})", values));
    ASSERT_EQ(values.size(), 2u);
    ASSERT_EQ(values[""].size(), 3u);
    EXPECT_EQ(values[""][0], std::make_pair(std::string("a"), std::string("x\ny")));
    EXPECT_EQ(values[""][1].second, "2.5");
    EXPECT_EQ(values[""][2].second, R"([1,{"d":null}])");
    EXPECT_TRUE(values["Go::3"].empty());
}

// The whole run arrives in pages as it grows, and makes the same .btlog again.
TEST(RunClient, FollowsAGrowingRun) {
    const auto log = simLog();
    ASSERT_GT(log.transitions.size(), 300u);
    const int port = freePort();
    FakeMonitor monitor(port, 25);
    monitor.start();

    RunClient client;
    ASSERT_TRUE(client.connect("127.0.0.1:" + std::to_string(port)));
    Seen seen;
    ASSERT_TRUE(waitFor(client, seen, [&] { return seen.status == "waiting for a run"; }));
    EXPECT_TRUE(seen.online);

    monitor.begin("7", log.xml, log.start, log.transitions);
    for (std::size_t shown = 0; shown < log.transitions.size(); shown += 40) {
        monitor.release(shown, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    monitor.release(log.transitions.size(), true);
    ASSERT_TRUE(waitFor(client, seen, [&] { return seen.info.ended; }));
    seen.take(client);
    EXPECT_EQ(seen.runs, 1);
    EXPECT_EQ(seen.started.mission, "Sim");
    EXPECT_EQ(seen.info.result, "FAILURE");
    EXPECT_EQ(seen.status, "run ended: FAILURE");
    ASSERT_TRUE(sameTransitions(seen.transitions, log.transitions));
    EXPECT_EQ(writeRunLog(seen.xml, seen.started.startUsec, seen.transitions), log.bytes);

    // Ended: the client keeps asking, but nothing more comes.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_FALSE(client.take());
}

// The monitor goes away mid-run and comes back: no transition is lost or repeated.
TEST(RunClient, Reconnects) {
    const auto log = simLog();
    const int port = freePort();
    auto monitor = std::make_unique<FakeMonitor>(port, 65536);
    monitor->begin("7", log.xml, log.start, log.transitions);
    monitor->release(200, false);
    monitor->start();

    RunClient client;
    ASSERT_TRUE(client.connect("localhost:" + std::to_string(port)));
    Seen seen;
    ASSERT_TRUE(waitFor(client, seen, [&] { return seen.transitions.size() == 200; }));
    monitor.reset();
    ASSERT_TRUE(waitFor(
        client, seen, [&] { return !seen.online; }, 8000));
    EXPECT_NE(seen.status.find("no answer"), std::string::npos) << seen.status;

    monitor = std::make_unique<FakeMonitor>(port, 65536);
    monitor->begin("7", log.xml, log.start, log.transitions);
    monitor->release(log.transitions.size(), true);
    monitor->start();
    ASSERT_TRUE(waitFor(
        client, seen, [&] { return seen.info.ended && seen.online; }, 8000));
    seen.take(client);
    EXPECT_EQ(seen.runs, 1); // the same run, carried on
    EXPECT_TRUE(sameTransitions(seen.transitions, log.transitions));
}

// A new run replaces the last; blackboards come back as text.
TEST(RunClient, NewRunsAndBlackboards) {
    const auto log = simLog();
    const int port = freePort();
    FakeMonitor monitor(port, 65536);
    monitor.begin("1", log.xml, log.start, log.transitions);
    monitor.release(10, false);
    monitor.start();

    RunClient client;
    ASSERT_TRUE(client.connect("127.0.0.1:" + std::to_string(port)));
    Seen seen;
    ASSERT_TRUE(waitFor(client, seen, [&] { return seen.transitions.size() == 10; }));

    client.askBlackboards("1", {""});
    std::optional<RunClient::BlackboardReply> reply;
    for (int i = 0; i < 300 && !reply; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        reply = client.takeBlackboards();
    }
    ASSERT_TRUE(reply);
    EXPECT_TRUE(reply->error.empty()) << reply->error;
    const auto &values = reply->values[""];
    ASSERT_EQ(values.size(), 4u);
    EXPECT_EQ(values[0].second, "1.5");
    EXPECT_EQ(values[1].second, "3");
    EXPECT_EQ(values[2].second, "caf\xc3\xa9 \"x\"");
    EXPECT_EQ(values[3].second, R"({"x":1,"y":[2,3]})");

    monitor.begin("2", log.xml, log.start + 5, log.transitions);
    monitor.release(20, false);
    ASSERT_TRUE(waitFor(client, seen, [&] { return seen.runs == 2 && seen.transitions.size() == 20; }));
    EXPECT_EQ(seen.started.run, "2");
    EXPECT_EQ(seen.started.startUsec, log.start + 5);

    client.disconnect();
    EXPECT_FALSE(client.active());
}

// Nobody there: the client says so, and stops at once when asked.
TEST(RunClient, NoMonitor) {
    RunClient client;
    ASSERT_TRUE(client.connect("127.0.0.1:" + std::to_string(freePort())));
    Seen seen;
    ASSERT_TRUE(waitFor(
        client, seen, [&] { return seen.status.find("no answer") != std::string::npos; }, 8000));
    EXPECT_FALSE(seen.online);
    const auto before = std::chrono::steady_clock::now();
    client.disconnect();
    EXPECT_LT(std::chrono::steady_clock::now() - before, std::chrono::milliseconds(500));
}
