// Problems in a workspace, the way BehaviorTree.CPP 4 would see them at load (errors) and the ones that load but are
// likely mistakes (warnings, notes). Structural checks run as you edit; the factory check (bt_check.hpp) runs on save.
#pragma once
#include "notgroot/core/edit.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace notgroot::core {
enum class Severity { Error, Warning, Note };

struct Problem {
    Severity severity = Severity::Error;
    std::string message;
    std::filesystem::path file;
    int line = 0;
    NodeRef node;          // the node (or tree) it is about, if any
    std::string attribute; // the attribute it is about, if any
};

std::vector<Problem> validate(const Workspace &);
// Only the problems of one tree (for the canvas as you edit).
std::vector<Problem> validateTree(const Workspace &, const std::string &treeKey);

const char *severityName(Severity);
// The type BehaviorTree.CPP 4.10 gives text passed into a subtree: "int", "double", or "" (it stays text).
std::string literalType(const std::string &text);
} // namespace notgroot::core
