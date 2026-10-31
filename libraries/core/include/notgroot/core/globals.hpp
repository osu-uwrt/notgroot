// Globals: keys a tree sets before it runs (contract section 5). In the file they are one Script, first in a
// Sequence that wraps the tree's root right after a <!-- notgroot:globals --> comment; in the editor they are a list
// of name = value, the value text with {key} mentions as Format writes it. notgroot writes them in dependency order.
#pragma once
#include "notgroot/core/models.hpp"
#include "notgroot/core/xml.hpp"
#include <string>
#include <vector>

namespace notgroot::core {
struct Global {
    std::string name, value; // value: text, {key} mentions joined into it; "{key}" alone copies the key
    bool operator==(const Global &o) const {
        return name == o.name && value == o.value;
    }
};

struct TreeGlobals {
    Element *wrapper = nullptr; // the <Sequence name="globals">, or null
    Element *script = nullptr;  // its Script
    std::vector<Global> globals;
};
// A <BehaviorTree>'s globals; none, and no wrapper, unless the wrapper holds a Script notgroot can read back.
TreeGlobals readGlobals(const Element &behaviorTree);
// What holds the tree's root node: the wrapper, else the <BehaviorTree> itself.
Element *treeBody(const Element &behaviorTree);
// The wrapper or its Script: editor plumbing, not one of the tree's nodes.
bool isGlobalsPart(const Element &);
// How many node children an element can hold, -1 for any: a tree one, the wrapper its Script and one, else the
// node's category.
int capacity(const Catalog &, const Element &parent);

// The keys a value mentions, in order.
std::vector<std::string> valueKeys(const std::string &value);
// Dependency order (each after the globals it mentions, else by name), and the cycles that make one impossible.
std::vector<Global> orderGlobals(std::vector<Global>, std::vector<std::vector<std::string>> &cycles);
// The Script code for ordered globals, and back; parseScript is false for code notgroot didn't write.
std::string scriptFor(const std::vector<Global> &);
bool parseScript(const std::string &code, std::vector<Global> &out);
// Why a global can't be written as it is ("" if it can).
std::string globalProblem(const Global &);
} // namespace notgroot::core
