// Edits the editor makes, each one undoable step: nodes (insert, delete, move, wrap, retype), attributes, variables,
// subtree ports, and whole trees (new, rename, extract, delete). Nodes are named by NodeRef, which survives undo
// (element pointers do not).
#pragma once
#include "notgroot/core/history.hpp"
#include "notgroot/core/models.hpp"
#include "notgroot/core/workspace.hpp"
#include <map>
#include <string>
#include <vector>

namespace notgroot::core {
// A node: its tree's ID and the child indices (comments counted) from the <BehaviorTree> element down. An empty path
// is the <BehaviorTree> element itself.
struct NodeRef {
    std::string tree;
    std::vector<std::size_t> path;

    bool empty() const {
        return tree.empty();
    }
    bool isTree() const {
        return !tree.empty() && path.empty();
    }
    NodeRef parent() const;
    NodeRef child(std::size_t index) const;
    bool operator==(const NodeRef &other) const {
        return tree == other.tree && path == other.path;
    }
    bool operator!=(const NodeRef &other) const {
        return !(*this == other);
    }
    bool operator<(const NodeRef &other) const {
        return tree != other.tree ? tree < other.tree : path < other.path;
    }
};

Element *resolve(const Workspace &, const NodeRef &);
// The ref of an element inside a tree (empty if it is not in one).
NodeRef refOf(const Workspace &, const Element *);
// The document holding a ref's tree.
Document *documentOf(const Workspace &, const NodeRef &);

// A new node element for a model: its ports with their defaults (outputs as {port}), subtree ports as {port}.
std::unique_ptr<Element> makeNode(const NodeModel &);
// Whether `child` may go under `parent` (by category and child count; `adding` counts the new one).
bool acceptsChild(const Workspace &, const Element &parent, bool adding = true);
// Child elements that are nodes (no comments), and a node's position among them.
std::vector<Element *> nodeChildren(const Element &);

// Clipboard text for nodes: their XML, one after another (pasteable here or into a text editor).
std::string copyNodes(const Workspace &, const std::vector<NodeRef> &);
// Parses clipboard text back into node elements (XML of one or more nodes); empty if it is not that.
std::vector<std::unique_ptr<Element>> parseClipboard(const std::string &);

class Editor {
  public:
    Editor(Workspace &workspace, History &history) : workspace_(workspace), history_(history) {}
    Workspace &workspace() {
        return workspace_;
    }
    History &history() {
        return history_;
    }

    // Each returns the ref of what it made or moved (empty on refusal) and sets error() when it refuses.
    NodeRef insert(const NodeRef &parent, std::size_t nodeIndex, std::unique_ptr<Element>, const std::string &label);
    NodeRef insertMany(const NodeRef &parent, std::size_t nodeIndex, std::vector<std::unique_ptr<Element>>,
                       const std::string &label); // returns the first
    NodeRef insertAfter(const NodeRef &sibling, std::unique_ptr<Element>, const std::string &label);
    bool remove(std::vector<NodeRef>);
    NodeRef move(const NodeRef &, int delta); // among siblings
    NodeRef indent(const NodeRef &);          // into the previous sibling, as its last child
    NodeRef outdent(const NodeRef &);         // out of its parent, right after it
    NodeRef wrap(const std::vector<NodeRef> &siblings, const NodeModel &with);
    NodeRef unwrap(const NodeRef &); // replaces a node by its children
    NodeRef retype(const NodeRef &, const NodeModel &to);

    bool setAttribute(const NodeRef &, const std::string &name, const std::string &value);
    bool removeAttribute(const NodeRef &, const std::string &name);
    bool renameAttribute(const NodeRef &, const std::string &from, const std::string &to);

    // Renames a blackboard key everywhere in one tree ({key} values, {key} inside text, bare key-name ports).
    int renameKey(const std::string &tree, const std::string &from, const std::string &to);

    // Trees.
    NodeRef createSubtree(const std::string &id);
    NodeRef createMission(const std::string &name);
    bool renameTree(const std::string &from, const std::string &to);
    bool deleteTree(const std::string &id); // refuses while something calls it
    // Moves sibling nodes into a new subtree and calls it in their place; ports from the keys they use.
    NodeRef extractSubtree(const std::vector<NodeRef> &siblings, const std::string &id);
    // Sets a subtree's ports; `renamed` maps old port names to new ones, so call sites follow.
    bool setSubtreePorts(const std::string &id, const std::vector<PortModel> &ports,
                         const std::map<std::string, std::string> &renamed);

    // Where every subtree is called from.
    std::vector<NodeRef> callSites(const std::string &id) const;

    const std::string &error() const {
        return error_;
    }

  private:
    bool fail(std::string message);
    std::string error_;
    Workspace &workspace_;
    History &history_;
};

// A tree ID or mission name is usable: letters, digits, _ and -, starting with a letter or _.
bool validTreeId(const std::string &);
} // namespace notgroot::core
