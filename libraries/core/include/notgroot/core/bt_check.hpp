// The BehaviorTree.CPP check in the background: the workspace as it is now (saved or not) is copied to a temporary
// folder and every mission is loaded by notgroot-check (BT.CPP 4.10, as doTask loads it). Problems come back mapped
// to the real files.
#pragma once
#include "notgroot/core/validate.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace notgroot::core {
class BtCheck {
  public:
    explicit BtCheck(std::filesystem::path program) : program_(std::move(program)) {}
    ~BtCheck();
    BtCheck(const BtCheck &) = delete;
    BtCheck &operator=(const BtCheck &) = delete;

    bool available() const;
    // Starts a check of the workspace's current text; false if one is running or the checker is missing.
    bool start(const Workspace &);
    bool running() const;
    // The result once a check has finished (once): problems, and how many missions loaded.
    struct Result {
        std::vector<Problem> problems;
        int loaded = 0, failed = 0;
        std::string error; // the checker itself failed
    };
    std::optional<Result> take();

    // Parses the checker's output lines, mapping mirror paths back to the workspace's.
    static Result parse(const std::string &output, const std::filesystem::path &mirror,
                        const std::filesystem::path &directory);

  private:
    struct State;
    std::shared_ptr<State> state_;
    std::filesystem::path program_;
};
} // namespace notgroot::core
