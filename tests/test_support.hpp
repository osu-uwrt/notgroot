// Shared test helpers: fixture paths, a scratch copy of a fixture workspace, the real trees directory if present.
#pragma once
#include "notgroot/core/workspace.hpp"
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <random>
#include <string>

namespace notgroot::test {
inline std::filesystem::path data(const std::string &name) {
    return std::filesystem::path(NOTGROOT_TEST_DATA) / name;
}

// A fresh copy of a fixture directory under the system temp directory, removed when the test ends.
class ScratchCopy {
  public:
    explicit ScratchCopy(const std::string &fixture) {
        std::random_device random;
        path_ = std::filesystem::temp_directory_path() / ("notgroot-test-" + std::to_string(random()));
        std::filesystem::copy(data(fixture), path_, std::filesystem::copy_options::recursive);
    }
    ~ScratchCopy() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    const std::filesystem::path &path() const {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

// The real trees directory for the round-trip gate: NOTGROOT_TREES_DIR, else the bt4 worktree, else empty.
inline std::filesystem::path realTrees() {
    if (const char *dir = std::getenv("NOTGROOT_TREES_DIR"); dir && *dir)
        return dir;
    if (const char *home = std::getenv("HOME")) {
        const auto trees = std::filesystem::path(home) / "osu-uwrt/bt4_ws/src/riptide_autonomy/trees";
        if (std::filesystem::exists(trees / "lib/node_models.xml"))
            return trees;
    }
    return {};
}

inline void openWorkspace(core::Workspace &workspace, const std::filesystem::path &path) {
    std::string error;
    ASSERT_TRUE(workspace.loadBuiltinModels(NOTGROOT_BUILTIN_MODELS, error)) << error;
    ASSERT_TRUE(workspace.open(path, error)) << error;
}
} // namespace notgroot::test
