// RunClient: polling doTask's run monitor over ZeroMQ on a thread, and the bits of JSON its replies hold.
#include "notgroot/core/run_client.hpp"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <zlib.h>
#include <zmq.h>

namespace notgroot::core {
namespace {
using Clock = std::chrono::steady_clock;
constexpr auto kPoll = std::chrono::milliseconds(50);
constexpr auto kRetry = std::chrono::milliseconds(1000);
constexpr int kTimeoutMs = 3000;
constexpr std::size_t kRecord = 9;

// Just enough JSON for the monitor's replies: every value keeps its text as written (a string's is unescaped).
struct Json {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    std::string text;
    std::vector<std::pair<std::string, Json>> members; // an object's

    const Json *find(const std::string &name) const {
        for (const auto &[key, value] : members)
            if (key == name)
                return &value;
        return nullptr;
    }
};

class JsonReader {
  public:
    explicit JsonReader(const std::string &text) : s_(text) {}
    bool read(Json &out) {
        return value(out) && (space(), at_ == s_.size());
    }

  private:
    void space() {
        while (at_ < s_.size() && (s_[at_] == ' ' || s_[at_] == '\t' || s_[at_] == '\n' || s_[at_] == '\r'))
            ++at_;
    }
    bool literal(const char *word) {
        const std::string_view w(word);
        if (s_.compare(at_, w.size(), w) != 0)
            return false;
        at_ += w.size();
        return true;
    }
    static void utf8(std::string &out, unsigned code) {
        if (code < 0x80)
            out += char(code);
        else if (code < 0x800) {
            out += char(0xc0 | (code >> 6));
            out += char(0x80 | (code & 0x3f));
        } else if (code < 0x10000) {
            out += char(0xe0 | (code >> 12));
            out += char(0x80 | ((code >> 6) & 0x3f));
            out += char(0x80 | (code & 0x3f));
        } else {
            out += char(0xf0 | (code >> 18));
            out += char(0x80 | ((code >> 12) & 0x3f));
            out += char(0x80 | ((code >> 6) & 0x3f));
            out += char(0x80 | (code & 0x3f));
        }
    }
    bool hex4(unsigned &code) {
        if (at_ + 4 > s_.size())
            return false;
        code = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[at_++];
            code <<= 4;
            if (c >= '0' && c <= '9')
                code |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f')
                code |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                code |= unsigned(c - 'A' + 10);
            else
                return false;
        }
        return true;
    }
    bool string(std::string &out) {
        if (at_ >= s_.size() || s_[at_] != '"')
            return false;
        ++at_;
        while (at_ < s_.size() && s_[at_] != '"') {
            if (s_[at_] != '\\') {
                out += s_[at_++];
                continue;
            }
            if (++at_ >= s_.size())
                return false;
            const char c = s_[at_++];
            switch (c) {
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u': {
                unsigned code = 0;
                if (!hex4(code))
                    return false;
                // a surrogate pair
                if (code >= 0xd800 && code < 0xdc00 && s_.compare(at_, 2, "\\u") == 0) {
                    at_ += 2;
                    unsigned low = 0;
                    if (!hex4(low))
                        return false;
                    code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                }
                utf8(out, code);
                break;
            }
            default:
                out += c; // " \ /
            }
        }
        if (at_ >= s_.size())
            return false;
        ++at_;
        return true;
    }
    bool value(Json &out) {
        space();
        if (at_ >= s_.size() || ++depth_ > 64)
            return false;
        const std::size_t start = at_;
        const char c = s_[at_];
        bool ok = true;
        if (c == '{') {
            out.kind = Json::Object;
            ++at_;
            space();
            if (at_ < s_.size() && s_[at_] == '}')
                ++at_;
            else
                for (;;) {
                    std::string key;
                    Json member;
                    space();
                    if (!string(key) || (space(), at_ >= s_.size() || s_[at_++] != ':') || !value(member))
                        return false;
                    out.members.emplace_back(std::move(key), std::move(member));
                    space();
                    if (at_ < s_.size() && s_[at_] == ',') {
                        ++at_;
                        continue;
                    }
                    if (at_ >= s_.size() || s_[at_++] != '}')
                        return false;
                    break;
                }
        } else if (c == '[') {
            out.kind = Json::Array;
            ++at_;
            space();
            if (at_ < s_.size() && s_[at_] == ']')
                ++at_;
            else
                for (;;) {
                    Json item;
                    if (!value(item))
                        return false;
                    space();
                    if (at_ < s_.size() && s_[at_] == ',') {
                        ++at_;
                        continue;
                    }
                    if (at_ >= s_.size() || s_[at_++] != ']')
                        return false;
                    break;
                }
        } else if (c == '"') {
            out.kind = Json::String;
            --depth_;
            return string(out.text);
        } else if (literal("true") || literal("false"))
            out.kind = Json::Bool;
        else if (literal("null"))
            out.kind = Json::Null;
        else {
            out.kind = Json::Number;
            while (at_ < s_.size() && std::string_view("+-.0123456789eE").find(s_[at_]) != std::string_view::npos)
                ++at_;
            ok = at_ > start;
        }
        out.text = s_.substr(start, at_ - start);
        --depth_;
        return ok;
    }

    const std::string &s_;
    std::size_t at_ = 0;
    int depth_ = 0;
};

bool parseJson(const std::string &text, Json &out) {
    return JsonReader(text).read(out);
}

// A whole number member (false: missing or not one).
bool number(const Json &object, const char *name, std::int64_t &out) {
    const auto *value = object.find(name);
    if (!value || value->kind != Json::Number || value->text.empty())
        return false;
    out = 0;
    bool negative = false;
    for (std::size_t i = 0; i < value->text.size(); ++i) {
        const char c = value->text[i];
        if (c == '-' && i == 0)
            negative = true;
        else if (c >= '0' && c <= '9')
            out = out * 10 + (c - '0');
        else
            return false;
    }
    if (negative)
        out = -out;
    return true;
}

std::uint64_t readLe(const char *bytes, int size) {
    std::uint64_t value = 0;
    for (int i = size - 1; i >= 0; --i)
        value = (value << 8) | std::uint8_t(bytes[i]);
    return value;
}
} // namespace

struct RunClient::State {
    std::string endpoint;
    std::thread thread;
    std::mutex mutex;
    std::condition_variable wake;
    bool stop = false;
    Update pending;
    bool fresh = false;
    std::optional<std::pair<std::string, std::vector<std::string>>> askBlackboards;
    std::optional<BlackboardReply> blackboards;
};

namespace {
// One REQ socket, reset when an answer doesn't come (a REQ socket can't ask again until it has one).
class Link {
  public:
    explicit Link(const std::string &endpoint) : endpoint_(endpoint), context_(zmq_ctx_new()) {}
    ~Link() {
        reset();
        zmq_ctx_term(context_);
    }
    void reset() {
        if (socket_)
            zmq_close(socket_);
        socket_ = nullptr;
    }
    // Sends a request and waits for the whole reply. False on a timeout, an error or `stop` (the socket is reset).
    bool ask(const std::vector<std::string> &request, std::vector<std::string> &reply, const bool &stop,
             std::mutex &mutex) {
        reply.clear();
        if (!socket_) {
            socket_ = zmq_socket(context_, ZMQ_REQ);
            const int linger = 0;
            zmq_setsockopt(socket_, ZMQ_LINGER, &linger, sizeof(linger));
            if (zmq_connect(socket_, endpoint_.c_str()) != 0) {
                error = zmq_strerror(zmq_errno());
                reset();
                return false;
            }
        }
        for (std::size_t i = 0; i < request.size(); ++i)
            if (zmq_send(socket_, request[i].data(), request[i].size(), i + 1 < request.size() ? ZMQ_SNDMORE : 0) < 0) {
                error = zmq_strerror(zmq_errno());
                reset();
                return false;
            }
        // Waits in short slices, so stopping doesn't wait for a timeout.
        const auto until = Clock::now() + std::chrono::milliseconds(kTimeoutMs);
        for (;;) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (stop) {
                    reset();
                    return false;
                }
            }
            zmq_pollitem_t item = {socket_, 0, ZMQ_POLLIN, 0};
            if (zmq_poll(&item, 1, 100) > 0 && (item.revents & ZMQ_POLLIN))
                break;
            if (Clock::now() > until) {
                error = "no answer from " + endpoint_;
                reset();
                return false;
            }
        }
        for (int more = 1; more;) {
            zmq_msg_t message;
            zmq_msg_init(&message);
            if (zmq_msg_recv(&message, socket_, 0) < 0) {
                zmq_msg_close(&message);
                error = zmq_strerror(zmq_errno());
                reset();
                return false;
            }
            reply.emplace_back(static_cast<const char *>(zmq_msg_data(&message)), zmq_msg_size(&message));
            more = zmq_msg_more(&message);
            zmq_msg_close(&message);
        }
        return !reply.empty();
    }
    std::string error;

  private:
    std::string endpoint_;
    void *context_ = nullptr;
    void *socket_ = nullptr;
};

} // namespace

void RunClient::watch(std::shared_ptr<State> state) {
    Link link(state->endpoint);
    std::string run; // the run followed, and how many of its transitions came
    std::size_t cursor = 0;
    std::vector<std::string> reply;

    // Tells the UI (only what changed is marked fresh).
    const auto report = [&](bool online, const std::string &status) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->pending.online != online || state->pending.status != status)
            state->fresh = true;
        state->pending.online = online;
        state->pending.status = status;
    };
    // Waits (a blackboard asked for cuts a poll's wait short); false once stopped.
    const auto pause = [&](std::chrono::milliseconds duration) {
        std::unique_lock<std::mutex> lock(state->mutex);
        const bool poll = duration == kPoll;
        state->wake.wait_for(lock, duration, [&] { return state->stop || (poll && state->askBlackboards); });
        return !state->stop;
    };
    const auto offline = [&] {
        report(false, link.error);
        return pause(kRetry);
    };

    for (;;) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->stop)
                break;
        }
        if (!link.ask({"RUN"}, reply, state->stop, state->mutex)) {
            if (!offline())
                break;
            continue;
        }
        if (reply[0] == "NONE") {
            report(true, "waiting for a run");
            if (!pause(kPoll))
                break;
            continue;
        }
        RunInfo info;
        if (reply[0] != "OK" || reply.size() < 2 || !parseInfo(reply[1], info)) {
            report(true, "the monitor's answer to RUN isn't one notgroot reads" +
                             (reply.size() > 1 && reply[0] == "ERR" ? ": " + reply[1] : std::string()));
            if (!pause(kRetry))
                break;
            continue;
        }

        // A new run: its tree first.
        if (info.run != run) {
            std::int64_t size = 0;
            Json meta;
            parseJson(reply[1], meta);
            number(meta, "xml_bytes", size);
            if (!link.ask({"TREE", info.run}, reply, state->stop, state->mutex)) {
                if (!offline())
                    break;
                continue;
            }
            std::string xml(std::size_t(std::max<std::int64_t>(size, 0)), '\0');
            uLongf length = uLongf(xml.size());
            if (reply[0] != "OK" || reply.size() < 2 || size <= 0 ||
                uncompress(reinterpret_cast<Bytef *>(xml.data()), &length,
                           reinterpret_cast<const Bytef *>(reply[1].data()), uLong(reply[1].size())) != Z_OK ||
                length != xml.size()) {
                // The run may have been replaced meanwhile: ask again.
                if (!pause(kPoll))
                    break;
                continue;
            }
            run = info.run;
            cursor = 0;
            std::lock_guard<std::mutex> lock(state->mutex);
            state->pending.started = info;
            state->pending.xml = std::move(xml);
            state->pending.transitions.clear();
            state->fresh = true;
        }

        // Its transitions from where they got to, in pieces of at most 65536.
        bool lost = false;
        for (;;) {
            if (!link.ask({"TRANS", run, std::to_string(cursor)}, reply, state->stop, state->mutex)) {
                lost = true;
                break;
            }
            Json meta;
            std::int64_t from = 0, to = 0, count = 0;
            if (reply[0] != "OK" || reply.size() < 3 || !parseJson(reply[1], meta) || !number(meta, "from", from) ||
                !number(meta, "to", to) || !number(meta, "count", count) || from != std::int64_t(cursor) || to < from ||
                reply[2].size() != std::size_t(to - from) * kRecord)
                break; // a run replaced since RUN ("unknown run"): the next RUN finds it
            std::vector<Transition> more;
            more.reserve(std::size_t(to - from));
            for (std::size_t at = 0; at < reply[2].size(); at += kRecord) {
                const char *record = reply[2].data() + at;
                more.push_back({std::int64_t(readLe(record, 6)), std::uint16_t(readLe(record + 6, 2)),
                                RunStatus(std::uint8_t(record[8]))});
            }
            cursor = std::size_t(to);
            info.transitions = std::size_t(count);
            if (const auto *ended = meta.find("ended"))
                info.ended = ended->text == "true";
            if (const auto *result = meta.find("result"); result && result->kind == Json::String)
                info.result = result->text;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                auto &pending = state->pending.transitions;
                pending.insert(pending.end(), more.begin(), more.end());
                state->fresh = state->fresh || !more.empty();
            }
            if (to >= count)
                break;
        }
        if (lost) {
            if (!offline())
                break;
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            const auto &last = state->pending.info;
            if (!last || last->ended != info.ended || last->result != info.result ||
                last->transitions != info.transitions)
                state->fresh = true;
            state->pending.info = info;
        }
        report(true, info.ended ? "run ended: " + info.result : "live");

        // Blackboards, when asked for.
        std::optional<std::pair<std::string, std::vector<std::string>>> asked;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            asked = std::move(state->askBlackboards);
            state->askBlackboards.reset();
        }
        if (asked) {
            std::vector<std::string> request{"BB", asked->first};
            request.insert(request.end(), asked->second.begin(), asked->second.end());
            BlackboardReply answer;
            answer.run = asked->first;
            if (!link.ask(request, reply, state->stop, state->mutex)) {
                if (!offline())
                    break;
                continue;
            }
            if (reply[0] == "OK" && reply.size() >= 2) {
                if (!parseBlackboards(reply[1], answer.values))
                    answer.error = "unreadable blackboards";
            } else
                answer.error = reply.size() >= 2 ? reply[1] : reply[0];
            std::lock_guard<std::mutex> lock(state->mutex);
            state->blackboards = std::move(answer);
        }
        if (!pause(kPoll))
            break;
    }
}

RunClient::RunClient() = default;

RunClient::~RunClient() {
    disconnect();
}

bool RunClient::endpoint(const std::string &address, std::string &out) {
    std::string rest = address;
    while (!rest.empty() && rest.front() == ' ')
        rest.erase(0, 1);
    while (!rest.empty() && rest.back() == ' ')
        rest.pop_back();
    if (rest.rfind("tcp://", 0) == 0)
        rest = rest.substr(6);
    std::string host = rest, port = std::to_string(kDefaultPort);
    if (const auto colon = rest.rfind(':'); colon != std::string::npos) {
        host = rest.substr(0, colon);
        port = rest.substr(colon + 1);
    }
    if (host.empty() || host.find_first_of(" /:") != std::string::npos || port.empty() || port.size() > 5 ||
        port.find_first_not_of("0123456789") != std::string::npos)
        return false;
    const int number = std::stoi(port);
    if (number < 1 || number > 65535)
        return false;
    out = "tcp://" + host + ":" + std::to_string(number);
    return true;
}

bool RunClient::connect(const std::string &address) {
    std::string where;
    if (!endpoint(address, where))
        return false;
    disconnect();
    address_ = where;
    state_ = std::make_shared<State>();
    state_->endpoint = where;
    state_->pending.status = "connecting to " + where;
    state_->fresh = true;
    state_->thread = std::thread(watch, state_);
    return true;
}

void RunClient::disconnect() {
    if (!state_)
        return;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->stop = true;
    }
    state_->wake.notify_all();
    state_->thread.join();
    state_.reset();
    address_.clear();
}

std::optional<RunClient::Update> RunClient::take() {
    if (!state_)
        return std::nullopt;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!state_->fresh)
        return std::nullopt;
    state_->fresh = false;
    Update out = std::move(state_->pending);
    // The state carries on.
    state_->pending = {};
    state_->pending.info = out.info;
    state_->pending.online = out.online;
    state_->pending.status = out.status;
    return out;
}

void RunClient::askBlackboards(const std::string &run, const std::vector<std::string> &paths) {
    if (!state_)
        return;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->askBlackboards = std::make_pair(run, paths);
    }
    state_->wake.notify_all();
}

std::optional<RunClient::BlackboardReply> RunClient::takeBlackboards() {
    if (!state_)
        return std::nullopt;
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto out = std::move(state_->blackboards);
    state_->blackboards.reset();
    return out;
}

bool RunClient::parseInfo(const std::string &json, RunInfo &info) {
    Json object;
    if (!parseJson(json, object) || object.kind != Json::Object)
        return false;
    const auto text = [&](const char *name, std::string &out) {
        const auto *value = object.find(name);
        if (value && value->kind == Json::String)
            out = value->text;
        return value != nullptr;
    };
    std::int64_t transitions = 0;
    if (!text("run", info.run) || info.run.empty() || !number(object, "start_usec", info.startUsec) ||
        !number(object, "transitions", transitions))
        return false;
    info.transitions = std::size_t(std::max<std::int64_t>(transitions, 0));
    text("mission", info.mission);
    text("log", info.log);
    text("result", info.result);
    const auto *ended = object.find("ended");
    info.ended = ended && ended->text == "true";
    return true;
}

bool RunClient::parseBlackboards(const std::string &json, Blackboards &out) {
    Json object;
    if (!parseJson(json, object) || object.kind != Json::Object)
        return false;
    out.clear();
    for (const auto &[path, values] : object.members) {
        auto &entries = out[path];
        for (const auto &[key, value] : values.members)
            entries.emplace_back(key, value.text);
    }
    return true;
}
} // namespace notgroot::core
