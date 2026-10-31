// Blackboard keys of one tree: who reads and writes each, which are the tree's ports, and the ones read but never set
// or set but never read. Each subtree has its own blackboard (contract section 5), so keys are per tree.
#pragma once
#include "notgroot/core/edit.hpp"
#include <map>
#include <string>
#include <vector>

namespace notgroot::core {
enum Access : unsigned { Read = 1, Write = 2 };

struct KeyUse {
    NodeRef node;
    std::string attribute;
    unsigned access = Read;
    bool mention = false; // "{key}" inside text (a message, a Format string), not the whole value
    bool script = false;  // keys named in script code (Script, Precondition if, _skipIf…): reads or writes
};

struct KeyInfo {
    std::string name;
    std::vector<KeyUse> uses;
    int reads = 0, writes = 0;
    bool port = false; // one of the tree's own ports (subtree model)
    PortDirection portDirection = PortDirection::Input;
    bool undefined() const;
    bool unused() const;
};

struct TreeVariables {
    std::string tree;
    std::map<std::string, KeyInfo> keys;
};

TreeVariables analyzeVariables(const Workspace &, const std::string &tree);
// How a node's attribute touches the blackboard: the keys it reads or writes.
std::vector<KeyUse> keyUses(const Workspace &, const Element &node, const NodeRef &ref);
} // namespace notgroot::core
