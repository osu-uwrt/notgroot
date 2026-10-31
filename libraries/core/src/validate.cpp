// Structural validation.
#include "notgroot/core/validate.hpp"
#include "notgroot/core/variables.hpp"
#include <cstdlib>
#include <functional>
#include <set>

namespace notgroot::core {
const char *severityName(Severity severity) {
    switch (severity) {
    case Severity::Warning:
        return "warning";
    case Severity::Note:
        return "note";
    case Severity::Error:
        break;
    }
    return "error";
}

namespace {
// The type BehaviorTree.CPP 4.10 gives text passed into a subtree: bool, int, double, or "" (it stays text).
std::string literalType(const std::string &text) {
    if (text == "true" || text == "false")
        return "bool";
    char *end = nullptr;
    std::strtol(text.c_str(), &end, 10);
    if (end && *end == '\0')
        return "int";
    std::strtod(text.c_str(), &end);
    return end && *end == '\0' ? "double" : "";
}

// Every model port in a tree whose value is exactly `value` (SubTree calls pass it on and are skipped).
void forEachUse(const Element &element, const std::string &value, const Catalog &catalog,
                const std::function<void(const PortModel &)> &visit) {
    for (const auto *child : element.elements()) {
        if (!isSubTree(*child))
            if (const auto *model = catalog.find(nodeId(*child)))
                for (const auto &attribute : child->attributes)
                    if (attribute.value == value)
                        if (const auto *port = model->port(attribute.name))
                            visit(*port);
        forEachUse(*child, value, catalog, visit);
    }
}

void checkNode(const Workspace &workspace, const Document &document, const Element &node, const NodeRef &ref,
               std::vector<Problem> &out) {
    const auto add = [&](Severity severity, std::string message, std::string attribute = {}) {
        out.push_back({severity, std::move(message), document.path, node.line, ref, std::move(attribute)});
    };
    const auto &catalog = workspace.catalog();
    const auto id = nodeId(node);
    const bool call = isSubTree(node);
    const NodeModel *model = call ? catalog.find(node.get("ID")) : catalog.find(id);

    if (call) {
        if (id.empty())
            add(Severity::Error, "SubTree without an ID");
        else if (!workspace.tree(id))
            add(Severity::Error, "no subtree " + id + " in this workspace");
    } else if (!model) {
        add(Severity::Error, "unknown node " + id + " (not a builtin, not in node_models.xml)");
    }

    // Children by category.
    const auto category = nodeCategory(node, catalog);
    const int children = int(node.elements().size()), least = minChildren(category);
    // Too many children: the extra ones are floating, and say so themselves.
    if (isFloating(workspace, node)) {
        const auto *parent = node.parent;
        add(Severity::Error,
            "floating: " +
                (parent->tag == "BehaviorTree"                      ? std::string("a tree has one root")
                 : maxChildren(nodeCategory(*parent, catalog)) == 0 ? nodeId(*parent) + " takes no children"
                                                                    : nodeId(*parent) + " takes one child") +
                "; move it on (Alt+arrows) before saving");
    }
    if (children < least)
        add(Severity::Error, id + (category == Category::Decorator ? " needs a child" : " needs at least one child"));

    // Ports.
    for (const auto &attribute : node.attributes) {
        const auto &name = attribute.name, &value = attribute.value;
        if (isReservedAttribute(name))
            continue;
        const auto *port = model ? model->port(name) : nullptr;
        if (model && !port) {
            if (call)
                add(Severity::Warning, name + " is not a port of " + id + " (node_models.xml)", name);
            else
                add(Severity::Error, name + " is not a port of " + id, name);
            continue;
        }
        if (value == "{}")
            add(Severity::Warning, name + ": {} is not a key in BehaviorTree.CPP 4", name);
        if (port && port->direction == PortDirection::Output && !port->keyName && !isKeyReference(value))
            add(Severity::Warning, name + " is an output: \"" + value + "\" stores nothing (use {key})", name);
        if (isKeyReference(value) && keyOf(value).find_first_of("{}") != std::string::npos)
            add(Severity::Warning, name + ": a value that starts with { and ends with } is one key", name);
    }
    // BehaviorTree.CPP 4 gives a key passed into a subtree as text the type the text looks like ("2": int, "2.5":
    // double, "true": bool); a node in the subtree whose port has another type (std::string, or double for an int)
    // then fails the tree's load. Untyped ports (BT::AnyTypeAllowed) take anything.
    const auto *called = call ? workspace.tree(id) : nullptr;
    if (called)
        for (const auto &attribute : node.attributes) {
            if (isReservedAttribute(attribute.name) || isKeyReference(attribute.value) || attribute.value.empty())
                continue;
            const auto literal = literalType(attribute.value);
            if (literal.empty())
                continue;
            std::string clash;
            forEachUse(*called->tree, "{" + attribute.name + "}", catalog, [&](const PortModel &port) {
                if (clash.empty() && !port.type.empty() && port.type.find("AnyTypeAllowed") == std::string::npos &&
                    port.type != literal)
                    clash = port.type;
            });
            if (!clash.empty())
                add(Severity::Warning,
                    attribute.name + "=\"" + attribute.value + "\": BehaviorTree.CPP makes this a " + literal +
                        " inside " + id + ", where a port reads it as " + clash +
                        ", so the tree fails to load. Pass a {key}, or make that port untyped",
                    attribute.name);
        }
    if (call && model)
        for (const auto &port : model->ports)
            if (!node.has(port.name))
                add(Severity::Note, "port " + port.name + " of " + id + " is not passed", port.name);
}

void walk(const Workspace &workspace, const Document &document, const Element &element, const NodeRef &ref,
          std::vector<Problem> &out) {
    for (std::size_t i = 0; i < element.children.size(); ++i) {
        const auto &child = element.children[i];
        if (child->isComment())
            continue;
        checkNode(workspace, document, *child, ref.child(i), out);
        walk(workspace, document, *child, ref.child(i), out);
    }
}

void checkTree(const Workspace &workspace, const TreeEntry &entry, std::vector<Problem> &out) {
    const NodeRef ref{entry.key, {}};
    const auto &document = *entry.document;
    const auto add = [&](Severity severity, std::string message) {
        out.push_back({severity, std::move(message), document.path, entry.tree->line, ref, {}});
    };
    const auto roots = entry.tree->elements().size();
    if (roots == 0)
        add(Severity::Error, entry.key + " is empty: a tree needs one root node");
    else if (roots > 1)
        add(Severity::Error, entry.key + " has " + std::to_string(roots) + " root nodes; a tree has one");
    walk(workspace, document, *entry.tree, ref, out);

    // Keys read but never set (and not a port), set but never read.
    for (const auto &[name, key] : analyzeVariables(workspace, entry.key).keys) {
        if (key.uses.empty())
            continue;
        const auto &first = key.uses.front();
        const auto *node = resolve(workspace, first.node);
        if (key.undefined())
            out.push_back({Severity::Warning, "{" + name + "} is read but never set in " + entry.key, document.path,
                           node ? node->line : 0, first.node, first.attribute});
        else if (key.unused())
            out.push_back({Severity::Note, "{" + name + "} is set but never read in " + entry.key, document.path,
                           node ? node->line : 0, first.node, first.attribute});
    }
}

// A subtree that calls itself, directly or through others, never finishes loading.
void checkCycles(const Workspace &workspace, std::vector<Problem> &out) {
    std::map<std::string, std::vector<std::string>> calls;
    for (const auto &[key, entry] : workspace.trees()) {
        std::function<void(const Element &)> collect = [&](const Element &element) {
            for (const auto *child : element.elements()) {
                if (isSubTree(*child))
                    calls[key].push_back(child->get("ID"));
                collect(*child);
            }
        };
        collect(*entry.tree);
    }
    std::map<std::string, int> state; // 1 visiting, 2 done
    std::set<std::string> reported;
    std::function<void(const std::string &, std::vector<std::string> &)> visit = [&](const std::string &key,
                                                                                     std::vector<std::string> &stack) {
        state[key] = 1;
        stack.push_back(key);
        for (const auto &next : calls[key]) {
            if (state[next] == 1 && !reported.count(next)) {
                reported.insert(next);
                std::string loop;
                bool on = false;
                for (const auto &step : stack) {
                    on |= step == next;
                    if (on)
                        loop += step + " → ";
                }
                const auto *entry = workspace.tree(next);
                out.push_back({Severity::Error,
                               "subtrees call each other in a loop: " + loop + next,
                               entry ? entry->document->path : std::filesystem::path(),
                               entry ? entry->tree->line : 0,
                               NodeRef{next, {}},
                               {}});
            } else if (state[next] == 0 && workspace.tree(next))
                visit(next, stack);
        }
        stack.pop_back();
        state[key] = 2;
    };
    for (const auto &[key, entry] : workspace.trees())
        if (state[key] == 0) {
            std::vector<std::string> stack;
            visit(key, stack);
        }
}
} // namespace

std::vector<Problem> validateTree(const Workspace &workspace, const std::string &treeKey) {
    std::vector<Problem> out;
    if (const auto *entry = workspace.tree(treeKey))
        checkTree(workspace, *entry, out);
    return out;
}

std::vector<Problem> validate(const Workspace &workspace) {
    std::vector<Problem> out;
    for (const auto &document : workspace.documents()) {
        if (document->deleted)
            continue;
        if (!document->root) {
            out.push_back({Severity::Error, document->error.message, document->path, document->error.line, {}, {}});
            continue;
        }
        if (document->role == DocumentRole::Models)
            continue;
        if (document->root->tag != "root")
            out.push_back(
                {Severity::Error, "the root element must be <root>", document->path, document->root->line, {}, {}});
        else if (document->root->get("BTCPP_format") != "4")
            out.push_back(
                {Severity::Warning, "BTCPP_format=\"4\" is missing", document->path, document->root->line, {}, {}});
        if (document->role == DocumentRole::Mission) {
            const auto main = Workspace::mainTreeId(*document);
            bool found = false;
            for (const auto *tree : document->trees())
                found |= tree->get("ID") == main;
            if (main.empty() || !found)
                out.push_back({Severity::Error,
                               "main_tree_to_execute names no tree in this file",
                               document->path,
                               document->root->line,
                               {},
                               {}});
        }
        if (document->role == DocumentRole::Subtree) {
            const auto trees = document->trees();
            if (trees.size() != 1)
                out.push_back({Severity::Warning,
                               "a lib/ file holds exactly one tree (it has " + std::to_string(trees.size()) + ")",
                               document->path,
                               document->root->line,
                               {},
                               {}});
            else if (trees.front()->get("ID") != document->path.stem().string())
                out.push_back({Severity::Warning,
                               "lib/" + document->path.filename().string() + " holds " + trees.front()->get("ID") +
                                   "; its file should be named after it",
                               document->path,
                               trees.front()->line,
                               {},
                               {}});
        }
        for (const auto *include : document->root->elements())
            if (include->tag == "include") {
                const auto target = document->path.parent_path() / include->get("path");
                if (!workspace.document(std::filesystem::weakly_canonical(target)) && !std::filesystem::exists(target))
                    out.push_back({Severity::Error,
                                   "included file " + include->get("path") + " does not exist",
                                   document->path,
                                   include->line,
                                   {},
                                   {}});
            }
    }
    for (const auto &id : workspace.duplicateIds()) {
        const auto *entry = workspace.tree(id);
        out.push_back({Severity::Error,
                       "tree " + id + " is defined more than once",
                       entry ? entry->document->path : std::filesystem::path(),
                       entry ? entry->tree->line : 0,
                       NodeRef{id, {}},
                       {}});
    }
    for (const auto &[key, entry] : workspace.trees())
        checkTree(workspace, entry, out);
    checkCycles(workspace, out);
    return out;
}
} // namespace notgroot::core
