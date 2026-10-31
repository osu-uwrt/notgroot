// BtCheck: mirror the workspace, run notgroot-check on a thread, collect its lines.
#include "notgroot/core/bt_check.hpp"
#include <fcntl.h>
#include <fstream>
#include <mutex>
#include <random>
#include <spawn.h>
#include <sstream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char **environ;
namespace fs = std::filesystem;

namespace notgroot::core {
struct BtCheck::State {
    std::mutex mutex;
    bool running = false;
    std::optional<Result> result;
};

BtCheck::~BtCheck() = default;

bool BtCheck::available() const {
    std::error_code error;
    return !program_.empty() && fs::is_regular_file(program_, error);
}

bool BtCheck::running() const {
    if (!state_)
        return false;
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->running;
}

std::optional<BtCheck::Result> BtCheck::take() {
    if (!state_)
        return std::nullopt;
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto result = std::move(state_->result);
    state_->result.reset();
    return result;
}

BtCheck::Result BtCheck::parse(const std::string &output, const fs::path &mirror, const fs::path &directory) {
    Result result;
    std::istringstream lines(output);
    for (std::string line; std::getline(lines, line);) {
        const auto tab = line.find('\t'), tab2 = line.find('\t', tab + 1);
        if (tab == std::string::npos || tab2 == std::string::npos)
            continue;
        const auto status = line.substr(0, tab);
        const fs::path file = directory / fs::path(line.substr(tab + 1, tab2 - tab - 1)).lexically_relative(mirror);
        if (status == "ok")
            ++result.loaded;
        else {
            ++result.failed;
            result.problems.push_back({Severity::Error, "BehaviorTree.CPP: " + line.substr(tab2 + 1), file, 0, {}, {}});
        }
    }
    return result;
}

bool BtCheck::start(const Workspace &workspace) {
    if (!available() || running() || !workspace.isOpen() || workspace.loose())
        return false;

    // The mirror: every document as it would be saved (deleted ones left out), at its path under the workspace.
    std::random_device random;
    const fs::path mirror = fs::temp_directory_path() / ("notgroot-check-" + std::to_string(random()));
    for (const auto &document : workspace.documents()) {
        if (document->deleted || !document->root)
            continue;
        const auto relative = document->path.lexically_relative(workspace.directory());
        if (relative.empty() || *relative.begin() == "..")
            continue;
        const auto target = mirror / relative;
        std::error_code error;
        fs::create_directories(target.parent_path(), error);
        std::ofstream(target, std::ios::binary) << document->text();
    }
    // Includes are rewritten on save; mirror that too, so the check sees what will be on disk.
    {
        Workspace mirrored;
        std::string error;
        if (mirrored.open(mirror, error)) {
            for (const auto &document : mirrored.documents())
                if (document->role != DocumentRole::Models)
                    mirrored.updateIncludes(*document);
            for (const auto &document : mirrored.documents())
                if (document->root && document->role != DocumentRole::Models)
                    std::ofstream(document->path, std::ios::binary) << document->text();
        }
    }

    state_ = std::make_shared<State>();
    state_->running = true;
    std::thread([state = state_, program = program_, mirror, directory = workspace.directory()] {
        int out[2];
        Result result;
        std::string text;
        if (pipe2(out, O_CLOEXEC) == 0) {
            posix_spawn_file_actions_t actions;
            posix_spawn_file_actions_init(&actions);
            posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
            posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
            const std::string programText = program.string(), mirrorText = mirror.string();
            char *argv[] = {const_cast<char *>(programText.c_str()), const_cast<char *>(mirrorText.c_str()), nullptr};
            pid_t pid = 0;
            const int spawned = posix_spawn(&pid, argv[0], &actions, nullptr, argv, environ);
            posix_spawn_file_actions_destroy(&actions);
            close(out[1]);
            if (spawned == 0) {
                char buffer[4096];
                for (ssize_t n; (n = read(out[0], buffer, sizeof(buffer))) != 0;)
                    if (n > 0)
                        text.append(buffer, std::size_t(n));
                    else if (errno != EINTR)
                        break;
                int status = 0;
                while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
                }
                result = parse(text, mirror, directory);
                if (!WIFEXITED(status) || WEXITSTATUS(status) > 1)
                    result.error = "notgroot-check did not finish (" + std::to_string(status) + ")";
            } else
                result.error = "cannot run " + programText;
            close(out[0]);
        } else
            result.error = "cannot start the check";
        std::error_code error;
        fs::remove_all(mirror, error);
        std::lock_guard<std::mutex> lock(state->mutex);
        state->result = std::move(result);
        state->running = false;
    }).detach();
    return true;
}
} // namespace notgroot::core
