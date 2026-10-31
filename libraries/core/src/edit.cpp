// Editor: the structural and refactoring edits, each one History step.
#include "notgroot/core/edit.hpp"
#include "notgroot/core/variables.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <set>

namespace notgroot::core {
NodeRef NodeRef::parent() const {
    NodeRef up = *this;
    if (!up.path.empty())
        up.path.pop_back();
    return up;
}

NodeRef NodeRef::child(std::size_t index) const {
    NodeRef down = *this;
    down.path.push_back(index);
    return down;
}

Element *resolve(const Workspace &workspace, const NodeRef &ref) {
    const auto *entry = workspace.tree(ref.tree);
    if (!entry)
        return nullptr;
    Element *element = entry->tree;
    for (const auto index : ref.path) {
        if (index >= element->children.size())
            return nullptr;
        element = element->children[index].get();
    }
    return element;
}

NodeRef refOf(const Workspace &workspace, const Element *element) {
    std::vector<std::size_t> path;
    const Element *at = element;
    while (at && at->tag != "BehaviorTree") {
        if (!at->parent)
            return {};
        path.push_back(at->index());
        at = at->parent;
    }
    if (!at || !at->parent)
        return {};
    for (const auto &document : workspace.documents())
        if (document->root.get() == at->parent) {
            std::reverse(path.begin(), path.end());
            return {workspace.treeKey(*document, *at), path};
        }
    return {};
}

Document *documentOf(const Workspace &workspace, const NodeRef &ref) {
    const auto *entry = workspace.tree(ref.tree);
    return entry ? entry->document : nullptr;
}

std::vector<Element *> nodeChildren(const Element &element) {
    return element.elements();
}

std::unique_ptr<Element> makeNode(const NodeModel &model) {
    std::unique_ptr<Element> node;
    if (model.category == Category::SubTree) {
        node = std::make_unique<Element>("SubTree");
        node->set("ID", model.id);
        for (const auto &port : model.ports)
            node->set(port.name, "{" + port.name + "}");
        return node;
    }
    node = std::make_unique<Element>(model.id);
    for (const auto &port : model.ports) {
        if (port.keyName)
            node->set(port.name, "");
        else if (port.direction == PortDirection::Output)
            node->set(port.name, "{" + port.name + "}");
        else if (port.direction == PortDirection::InOut && port.defaultValue.empty())
            node->set(port.name, "{" + port.name + "}");
        else
            node->set(port.name, port.defaultValue);
    }
    return node;
}

bool isFloating(const Workspace &workspace, const Element &node) {
    if (!node.parent || node.isComment())
        return false;
    const auto siblings = nodeChildren(*node.parent);
    const auto at = std::size_t(std::find(siblings.begin(), siblings.end(), &node) - siblings.begin());
    const int most =
        node.parent->tag == "BehaviorTree" ? 1 : maxChildren(nodeCategory(*node.parent, workspace.catalog()));
    return most >= 0 && int(at) >= most;
}

std::vector<NodeRef> floatingNodes(const Workspace &workspace, const std::string &tree) {
    std::vector<NodeRef> found;
    const auto *entry = workspace.tree(tree);
    if (!entry)
        return found;
    const std::function<void(const Element &, const NodeRef &)> visit = [&](const Element &element,
                                                                            const NodeRef &ref) {
        for (std::size_t i = 0; i < element.children.size(); ++i) {
            const auto &child = *element.children[i];
            if (child.isComment())
                continue;
            if (isFloating(workspace, child))
                found.push_back(ref.child(i));
            visit(child, ref.child(i));
        }
    };
    visit(*entry->tree, {tree, {}});
    return found;
}

bool acceptsChild(const Workspace &workspace, const Element &parent, bool adding) {
    const auto count = int(nodeChildren(parent).size()) + (adding ? 1 : 0);
    if (parent.tag == "BehaviorTree")
        return count <= 1;
    const int most = maxChildren(nodeCategory(parent, workspace.catalog()));
    return most < 0 || count <= most;
}

std::string copyNodes(const Workspace &workspace, const std::vector<NodeRef> &refs) {
    std::string out;
    for (const auto &ref : outermost(refs))
        if (const auto *element = resolve(workspace, ref); element && !ref.isTree())
            writeElement(out, *element, 0);
    return out;
}

std::vector<std::unique_ptr<Element>> parseClipboard(const std::string &text) {
    std::string body = text;
    if (const auto declaration = body.find("<?xml"); declaration != std::string::npos) {
        const auto end = body.find("?>", declaration);
        body.erase(declaration, end == std::string::npos ? std::string::npos : end + 2 - declaration);
    }
    ParseError error;
    auto clip = parseXml("<notgroot-clip>" + body + "</notgroot-clip>", error);
    std::vector<std::unique_ptr<Element>> nodes;
    if (!clip)
        return nodes;
    // A whole file or tree pasted: its first tree's nodes.
    Element *from = clip.get();
    if (auto *root = clip->firstChild("root"))
        from = root;
    if (auto *tree = from->firstChild("BehaviorTree"))
        from = tree;
    else if (from != clip.get())
        return nodes;
    while (!from->children.empty()) {
        auto child = from->remove(0);
        if (!child->isComment() && child->tag != "include" && child->tag != "TreeNodesModel")
            nodes.push_back(std::move(child));
    }
    return nodes;
}

namespace {
// The comment holding notgroot:<key>= right before an element, if any.
Element *metadataComment(const Element &element, const std::string &key) {
    if (!element.parent)
        return nullptr;
    for (auto at = element.index(); at > 0 && element.parent->children[at - 1]->isComment(); --at) {
        auto *comment = element.parent->children[at - 1].get();
        if (comment->text.find("notgroot:" + key + "=") != std::string::npos)
            return comment;
    }
    return nullptr;
}
} // namespace

std::string metadata(const Element &element, const std::string &key) {
    const auto *comment = metadataComment(element, key);
    if (!comment)
        return {};
    const auto start = comment->text.find("notgroot:" + key + "=") + key.size() + 10;
    const auto end = comment->text.find_first_of(" \t\n", start);
    return comment->text.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

std::string treeColor(const Workspace &workspace, const std::string &treeKey) {
    const auto *entry = workspace.tree(treeKey);
    return entry ? metadata(*entry->tree, "color") : std::string();
}

bool Editor::setTreeColor(const std::string &key, const std::string &hex) {
    const auto *entry = workspace_.tree(key);
    if (!entry)
        return fail("no tree " + key);
    if (!hex.empty() && (hex.size() != 7 || hex[0] != '#'))
        return fail("a color is #rrggbb");
    Change change(history_, hex.empty() ? "Default color" : "Color " + hex);
    change.touch(*entry->document);
    auto *comment = metadataComment(*entry->tree, "color");
    if (hex.empty()) {
        if (comment)
            comment->detach();
        return true;
    }
    if (comment)
        comment->text = " notgroot:color=" + hex + " ";
    else
        entry->tree->parent->insert(entry->tree->index(), Element::comment(" notgroot:color=" + hex + " "));
    return true;
}

bool validTreeId(const std::string &id) {
    if (id.empty() || !(std::isalpha(static_cast<unsigned char>(id[0])) || id[0] == '_'))
        return false;
    return std::all_of(id.begin(), id.end(),
                       [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; });
}

bool Editor::fail(std::string message) {
    error_ = std::move(message);
    return false;
}

namespace {
// The children index for inserting before the nodeIndex-th node child: before the comments that lead into it.
std::size_t childIndexFor(const Element &parent, std::size_t nodeIndex) {
    std::size_t seen = 0;
    for (std::size_t i = 0; i < parent.children.size(); ++i) {
        if (parent.children[i]->isComment())
            continue;
        if (seen++ == nodeIndex) {
            std::size_t at = i;
            while (at > 0 && parent.children[at - 1]->isComment())
                --at;
            return at;
        }
    }
    return parent.children.size();
}

std::size_t nodeIndexOf(const Element &node) {
    std::size_t index = 0;
    for (const auto &child : node.parent->children) {
        if (child.get() == &node)
            return index;
        if (!child->isComment())
            ++index;
    }
    return index;
}

// A node with the comments right before it (they describe it), out of its parent.
std::vector<std::unique_ptr<Element>> detachBlock(Element &node) {
    Element *parent = node.parent;
    std::size_t end = node.index(), start = end;
    while (start > 0 && parent->children[start - 1]->isComment())
        --start;
    std::vector<std::unique_ptr<Element>> block;
    for (std::size_t i = start; i <= end; ++i)
        block.push_back(parent->remove(start));
    return block;
}

Element *insertBlock(Element &parent, std::size_t at, std::vector<std::unique_ptr<Element>> block) {
    Element *node = nullptr;
    for (auto &item : block) {
        const bool comment = item->isComment();
        auto *inserted = parent.insert(at++, std::move(item));
        if (!comment)
            node = inserted;
    }
    return node;
}

template <typename F> void forEachNode(Element &element, const F &visit) {
    for (auto &child : element.children) {
        if (child->isComment())
            continue;
        visit(*child);
        forEachNode(*child, visit);
    }
}

} // namespace

bool isAncestor(const NodeRef &a, const NodeRef &b) {
    return a.tree == b.tree && a.path.size() < b.path.size() &&
           std::equal(a.path.begin(), a.path.end(), b.path.begin());
}

std::vector<NodeRef> outermost(std::vector<NodeRef> refs) {
    refs.erase(std::remove_if(refs.begin(), refs.end(), [](auto &r) { return r.isTree() || r.empty(); }), refs.end());
    std::sort(refs.begin(), refs.end());
    refs.erase(std::unique(refs.begin(), refs.end()), refs.end());
    std::vector<NodeRef> kept;
    for (const auto &ref : refs)
        if (std::none_of(refs.begin(), refs.end(), [&](auto &other) { return isAncestor(other, ref); }))
            kept.push_back(ref);
    return kept;
}

NodeRef Editor::insert(const NodeRef &parentRef, std::size_t nodeIndex, std::unique_ptr<Element> node,
                       const std::string &label) {
    std::vector<std::unique_ptr<Element>> nodes;
    nodes.push_back(std::move(node));
    return insertMany(parentRef, nodeIndex, std::move(nodes), label);
}

NodeRef Editor::insertMany(const NodeRef &parentRef, std::size_t nodeIndex, std::vector<std::unique_ptr<Element>> nodes,
                           const std::string &label) {
    auto *parent = resolve(workspace_, parentRef);
    if (!parent || nodes.empty())
        return fail("nothing to insert there"), NodeRef();
    const auto existing = nodeChildren(*parent).size();
    const int most = parent->tag == "BehaviorTree" ? 1 : maxChildren(nodeCategory(*parent, workspace_.catalog()));
    if (most >= 0 && existing + nodes.size() > std::size_t(most))
        return fail(parent->tag == "BehaviorTree"
                        ? "a tree has one root node"
                        : nodeId(*parent) + " takes " + std::to_string(most) + (most == 1 ? " child" : " children")),
               NodeRef();
    Change change(history_, label);
    change.touch(*documentOf(workspace_, parentRef));
    auto at = childIndexFor(*parent, nodeIndex);
    Element *first = nullptr;
    for (auto &node : nodes) {
        auto *inserted = parent->insert(at++, std::move(node));
        if (!first)
            first = inserted;
    }
    return refOf(workspace_, first);
}

NodeRef Editor::insertAfter(const NodeRef &sibling, std::unique_ptr<Element> node, const std::string &label) {
    auto *element = resolve(workspace_, sibling);
    if (!element || sibling.isTree())
        return fail("select a node first"), NodeRef();
    return insert(sibling.parent(), nodeIndexOf(*element) + 1, std::move(node), label);
}

bool Editor::remove(std::vector<NodeRef> refs) {
    refs.erase(std::remove_if(refs.begin(), refs.end(), [](auto &r) { return r.isTree() || r.empty(); }), refs.end());
    // Nodes inside another removed node go with it.
    std::vector<NodeRef> kept;
    for (const auto &ref : refs)
        if (std::none_of(refs.begin(), refs.end(), [&](auto &other) { return isAncestor(other, ref); }) &&
            std::find(kept.begin(), kept.end(), ref) == kept.end())
            kept.push_back(ref);
    if (kept.empty())
        return fail("nothing to delete");
    std::sort(kept.begin(), kept.end(), [](auto &a, auto &b) { return b < a; }); // last first: indices hold
    Change change(history_, kept.size() == 1 ? "Delete node" : "Delete nodes");
    for (const auto &ref : kept) {
        auto *element = resolve(workspace_, ref);
        if (!element)
            continue;
        change.touch(*documentOf(workspace_, ref));
        detachBlock(*element);
    }
    return true;
}

NodeRef Editor::move(const NodeRef &ref, int delta) {
    auto *element = resolve(workspace_, ref);
    if (!element || ref.isTree() || delta == 0)
        return {};
    Element *parent = element->parent;
    const auto count = nodeChildren(*parent).size();
    const auto from = nodeIndexOf(*element);
    const auto to = std::size_t(std::clamp<long>(long(from) + delta, 0, long(count) - 1));
    if (to == from)
        return ref;
    Change change(history_, "Move node");
    change.touch(*documentOf(workspace_, ref));
    auto block = detachBlock(*element);
    return refOf(workspace_, insertBlock(*parent, childIndexFor(*parent, to), std::move(block)));
}

NodeRef Editor::relocate(std::vector<NodeRef> refs, const NodeRef &parentRef, std::size_t nodeIndex, bool floating) {
    refs = outermost(refs);
    auto *parent = resolve(workspace_, parentRef);
    if (refs.empty() || !parent)
        return fail("nothing to move"), NodeRef();
    std::vector<Element *> moving;
    int arriving = 0;
    for (const auto &ref : refs) {
        if (ref.tree != parentRef.tree)
            return fail("nodes move within one tree (cut and paste them into another)"), NodeRef();
        if (ref == parentRef || isAncestor(ref, parentRef))
            return fail("a node cannot move into itself"), NodeRef();
        auto *element = resolve(workspace_, ref);
        if (!element)
            return fail("nothing to move"), NodeRef();
        moving.push_back(element);
        arriving += element->parent != parent;
    }
    const auto siblings = nodeChildren(*parent);
    const int count = int(siblings.size()) + arriving;
    const int most = parent->tag == "BehaviorTree" ? 1 : maxChildren(nodeCategory(*parent, workspace_.catalog()));
    if (!floating && most >= 0 && count > most)
        return fail((parent->tag == "BehaviorTree" ? std::string("a tree") : nodeId(*parent)) + " cannot take " +
                    (arriving > 1 ? "these children" : "another child")),
               NodeRef();

    // They go before the first node from nodeIndex on that stays put (or at the end).
    Element *anchor = nullptr;
    for (std::size_t i = nodeIndex; i < siblings.size() && !anchor; ++i)
        if (std::find(moving.begin(), moving.end(), siblings[i]) == moving.end())
            anchor = siblings[i];

    Change change(history_, moving.size() == 1 ? "Move node" : "Move nodes");
    change.touch(*documentOf(workspace_, parentRef));
    std::vector<std::vector<std::unique_ptr<Element>>> blocks;
    for (auto *element : moving)
        blocks.push_back(detachBlock(*element));
    std::size_t at = parent->children.size();
    if (anchor) {
        at = anchor->index();
        while (at > 0 && parent->children[at - 1]->isComment())
            --at;
    }
    Element *first = nullptr;
    for (auto &block : blocks) {
        const auto size = block.size();
        auto *node = insertBlock(*parent, at, std::move(block));
        at += size;
        if (!first)
            first = node;
    }
    return refOf(workspace_, first);
}

NodeRef Editor::indent(const NodeRef &ref, bool floating) {
    auto *element = resolve(workspace_, ref);
    if (!element || ref.isTree())
        return {};
    const auto siblings = nodeChildren(*element->parent);
    const auto at = nodeIndexOf(*element);
    if (at == 0)
        return fail("no node before this one to move it into"), NodeRef();
    Element *into = siblings[at - 1];
    if (!floating && !acceptsChild(workspace_, *into))
        return fail(nodeId(*into) + " cannot take another child"), NodeRef();
    Change change(history_, "Move into previous");
    change.touch(*documentOf(workspace_, ref));
    auto block = detachBlock(*element);
    return refOf(workspace_, insertBlock(*into, into->children.size(), std::move(block)));
}

NodeRef Editor::outdent(const NodeRef &ref, bool floating) {
    auto *element = resolve(workspace_, ref);
    if (!element || ref.path.size() < 2)
        return fail("already at the top of the tree"), NodeRef();
    Element *parent = element->parent, *grand = parent->parent;
    if (!floating && !acceptsChild(workspace_, *grand))
        return fail((grand->tag == "BehaviorTree" ? std::string("the tree") : nodeId(*grand)) +
                    " cannot take another child"),
               NodeRef();
    Change change(history_, "Move out of parent");
    change.touch(*documentOf(workspace_, ref));
    auto block = detachBlock(*element);
    return refOf(workspace_, insertBlock(*grand, childIndexFor(*grand, nodeIndexOf(*parent) + 1), std::move(block)));
}

NodeRef Editor::wrap(const std::vector<NodeRef> &refs, const NodeModel &with) {
    if (refs.empty())
        return {};
    std::vector<Element *> nodes;
    for (const auto &ref : refs) {
        auto *element = resolve(workspace_, ref);
        if (!element || ref.isTree() || ref.parent() != refs.front().parent())
            return fail("wrap nodes that share a parent"), NodeRef();
        nodes.push_back(element);
    }
    std::sort(nodes.begin(), nodes.end(), [](auto *a, auto *b) { return a->index() < b->index(); });
    const int most = maxChildren(with.category);
    if (most >= 0 && int(nodes.size()) > most)
        return fail(with.id + " takes " + std::to_string(most) + (most == 1 ? " child" : " children")), NodeRef();
    Change change(history_, "Wrap in " + with.id);
    change.touch(*documentOf(workspace_, refs.front()));
    Element *parent = nodes.front()->parent;
    const auto at = childIndexFor(*parent, nodeIndexOf(*nodes.front()));
    auto wrapper = makeNode(with);
    for (auto *node : nodes)
        for (auto &item : detachBlock(*node))
            wrapper->append(std::move(item));
    return refOf(workspace_, parent->insert(std::min(at, parent->children.size()), std::move(wrapper)));
}

NodeRef Editor::unwrap(const NodeRef &ref) {
    auto *element = resolve(workspace_, ref);
    if (!element || ref.isTree())
        return {};
    const auto children = nodeChildren(*element);
    if (children.empty())
        return fail("nothing inside to keep"), NodeRef();
    Element *parent = element->parent;
    const auto total = nodeChildren(*parent).size() - 1 + children.size();
    const int most = parent->tag == "BehaviorTree" ? 1 : maxChildren(nodeCategory(*parent, workspace_.catalog()));
    if (most >= 0 && total > std::size_t(most))
        return fail("the parent cannot take " + std::to_string(children.size()) + " children"), NodeRef();
    Change change(history_, "Unwrap " + nodeId(*element));
    change.touch(*documentOf(workspace_, ref));
    auto at = element->index();
    auto removed = parent->remove(at);
    Element *first = nullptr;
    while (!removed->children.empty()) {
        auto *inserted = parent->insert(at++, removed->remove(0));
        if (!first && !inserted->isComment())
            first = inserted;
    }
    return refOf(workspace_, first);
}

NodeRef Editor::retype(const NodeRef &ref, const NodeModel &to) {
    auto *element = resolve(workspace_, ref);
    if (!element || ref.isTree())
        return {};
    const auto children = nodeChildren(*element).size();
    const int most = maxChildren(to.category);
    if (most >= 0 && children > std::size_t(most))
        return fail(to.id + " takes " + std::to_string(most) + (most == 1 ? " child" : " children")), NodeRef();
    Change change(history_, "Change to " + to.id);
    change.touch(*documentOf(workspace_, ref));
    auto replacement = makeNode(to);
    // Keep the name, the special attributes and the values of ports the new type also has.
    for (const auto &attribute : element->attributes)
        if (attribute.name == "name" || (isReservedAttribute(attribute.name) && attribute.name != "ID") ||
            to.port(attribute.name))
            replacement->set(attribute.name, attribute.value);
    while (!element->children.empty())
        replacement->append(element->remove(0));
    Element *parent = element->parent;
    const auto at = element->index();
    parent->remove(at);
    return refOf(workspace_, parent->insert(at, std::move(replacement)));
}

bool Editor::setAttribute(const NodeRef &ref, const std::string &name, const std::string &value) {
    auto *element = resolve(workspace_, ref);
    if (!element)
        return false;
    if (element->has(name) && element->get(name) == value)
        return true;
    Change change(history_, "Set " + name);
    change.touch(*documentOf(workspace_, ref));
    element->set(name, value);
    return true;
}

bool Editor::removeAttribute(const NodeRef &ref, const std::string &name) {
    auto *element = resolve(workspace_, ref);
    if (!element || !element->has(name))
        return false;
    Change change(history_, "Remove " + name);
    change.touch(*documentOf(workspace_, ref));
    element->erase(name);
    return true;
}

bool Editor::renameAttribute(const NodeRef &ref, const std::string &from, const std::string &to) {
    auto *element = resolve(workspace_, ref);
    if (!element || from == to)
        return false;
    if (element->has(to))
        return fail(to + " is already set on this node");
    Change change(history_, "Rename " + from);
    change.touch(*documentOf(workspace_, ref));
    return element->rename(from, to);
}

namespace {
// Renames a key inside one tree's nodes: whole {from} values, {from} inside text, and bare key-name ports.
int renameKeyIn(const Workspace &workspace, Element &tree, const std::string &from, const std::string &to) {
    int changed = 0;
    const std::string whole = "{" + from + "}", replacement = "{" + to + "}";
    forEachNode(tree, [&](Element &node) {
        const auto *model = workspace.catalog().find(isSubTree(node) ? std::string() : nodeId(node));
        for (auto &attribute : node.attributes) {
            if (isReservedAttribute(attribute.name) && attribute.name != "_autoremap")
                continue;
            const auto *port = model ? model->port(attribute.name) : nullptr;
            if (attribute.value == whole) {
                attribute.value = replacement;
                ++changed;
            } else if (port && port->keyName && attribute.value == from) {
                attribute.value = to;
                ++changed;
            } else if (!isKeyReference(attribute.value)) {
                std::size_t at = 0;
                while ((at = attribute.value.find(whole, at)) != std::string::npos) {
                    attribute.value.replace(at, whole.size(), replacement);
                    at += replacement.size();
                    ++changed;
                }
            }
        }
    });
    return changed;
}

std::vector<Element *> callsIn(Element &tree, const std::string &id) {
    std::vector<Element *> calls;
    forEachNode(tree, [&](Element &node) {
        if (isSubTree(node) && node.get("ID") == id)
            calls.push_back(&node);
    });
    return calls;
}
} // namespace

int Editor::renameKey(const std::string &treeKey, const std::string &from, const std::string &to) {
    const auto *entry = workspace_.tree(treeKey);
    if (!entry || from == to || to.empty())
        return 0;
    if (to.find_first_of("{} ") != std::string::npos)
        return fail("a key name cannot hold braces or spaces"), 0;
    history_.begin("Rename " + from + " to " + to);
    history_.touch(*entry->document);
    int changed = renameKeyIn(workspace_, *entry->tree, from, to);
    // A subtree port of that name: the port and its call sites follow, in the same step.
    const auto model = workspace_.subtreeModel(entry->id);
    if (treeKey == entry->id && model.port(from)) {
        auto ports = model.ports;
        for (auto &port : ports)
            if (port.name == from)
                port.name = to;
        if (setSubtreePorts(entry->id, ports, {{from, to}}))
            ++changed;
    }
    history_.commit();
    return changed;
}

std::vector<NodeRef> Editor::callSites(const std::string &id) const {
    std::vector<NodeRef> sites;
    for (const auto &[key, entry] : workspace_.trees())
        for (auto *call : callsIn(*entry.tree, id))
            sites.push_back(refOf(workspace_, call));
    return sites;
}

namespace {
// Records a document that did not exist before this change (undo removes it again).
void adopt(Change &change, Document &document) {
    auto root = std::move(document.root);
    const bool deleted = document.deleted;
    document.deleted = true;
    change.touch(document);
    document.root = std::move(root);
    document.deleted = deleted;
}
} // namespace

NodeRef Editor::createSubtree(const std::string &id) {
    if (!validTreeId(id))
        return fail("a tree ID is letters, digits, _ and -, starting with a letter"), NodeRef();
    if (workspace_.tree(id))
        return fail(id + " already exists"), NodeRef();
    Change change(history_, "New subtree " + id);
    auto *document = workspace_.createSubtreeDocument(id);
    adopt(change, *document);
    return {id, {}};
}

NodeRef Editor::createMission(const std::string &name) {
    if (!validTreeId(name))
        return fail("a mission name is letters, digits, _ and -, starting with a letter"), NodeRef();
    if (workspace_.tree(name + ".xml"))
        return fail(name + ".xml already exists"), NodeRef();
    Change change(history_, "New mission " + name);
    auto *document = workspace_.createMissionDocument(name, "BehaviorTree");
    if (!document)
        return change.cancel(), fail(name + ".xml already exists"), NodeRef();
    adopt(change, *document);
    return {name + ".xml", {}};
}

bool Editor::renameTree(const std::string &from, const std::string &to) {
    const auto *entry = workspace_.tree(from);
    if (!entry)
        return fail("no tree " + from);
    const bool mission = from != entry->id; // a mission's main tree: rename the file
    const auto name = mission && to.size() > 4 && to.substr(to.size() - 4) == ".xml" ? to.substr(0, to.size() - 4) : to;
    if (!validTreeId(name))
        return fail("a name is letters, digits, _ and -, starting with a letter");
    if (workspace_.tree(mission ? name + ".xml" : name))
        return fail(name + " already exists");

    Change change(history_, "Rename " + from + " to " + name);
    Document *old = entry->document;
    if (mission) {
        auto *document = workspace_.createMissionDocument(name, Workspace::mainTreeId(*old));
        if (!document)
            return change.cancel(), fail(name + ".xml already exists");
        adopt(change, *document);
        document->root = old->root->clone();
        change.touch(*old);
        old->deleted = true;
        return true;
    }

    // The tree's ID, every call, main_tree_to_execute naming it, and its model entry.
    const auto oldId = entry->id;
    for (const auto &document : workspace_.documents()) {
        if (document->deleted || !document->root)
            continue;
        bool touched = false;
        const auto touch = [&] {
            if (!touched)
                change.touch(*document);
            touched = true;
        };
        for (auto *tree : document->trees()) {
            if (tree->get("ID") == oldId) {
                touch();
                tree->set("ID", name);
            }
            for (auto *call : callsIn(*tree, oldId)) {
                touch();
                call->set("ID", name);
            }
        }
        if (document->root->get("main_tree_to_execute") == oldId) {
            touch();
            document->root->set("main_tree_to_execute", name);
        }
    }
    if (const auto *model = workspace_.catalog().find(oldId); model && model->category == Category::SubTree) {
        change.touch(*workspace_.models());
        auto renamed = *model;
        renamed.id = name;
        workspace_.removeSubtreeModel(oldId);
        workspace_.setSubtreeModel(renamed);
    }
    // Its own file follows the ID (lib/<ID>.xml).
    if (old->role == DocumentRole::Subtree && old->path == workspace_.subtreePath(oldId) && old->trees().size() == 1) {
        auto *moved = workspace_.createSubtreeDocument(name);
        adopt(change, *moved);
        moved->root = old->root->clone();
        old->deleted = true;
    }
    return true;
}

bool Editor::deleteTree(const std::string &key) {
    const auto *entry = workspace_.tree(key);
    if (!entry)
        return fail("no tree " + key);
    if (key == entry->id) {
        const auto calls = callSites(key);
        if (!calls.empty())
            return fail(key + " is still called from " + std::to_string(calls.size()) +
                        (calls.size() == 1 ? " place (" : " places (first: ") + calls.front().tree + ")");
    }
    Change change(history_, "Delete " + key);
    Document *document = entry->document;
    change.touch(*document);
    if (document->trees().size() == 1)
        document->deleted = true;
    else
        entry->tree->detach();
    if (key == entry->id && workspace_.models() && workspace_.catalog().find(key) &&
        !workspace_.catalog().find(key)->builtin) {
        change.touch(*workspace_.models());
        workspace_.removeSubtreeModel(key);
    }
    return true;
}

NodeRef Editor::extractSubtree(const std::vector<NodeRef> &refs, const std::string &id) {
    if (refs.empty())
        return {};
    if (!validTreeId(id))
        return fail("a tree ID is letters, digits, _ and -, starting with a letter"), NodeRef();
    if (workspace_.tree(id))
        return fail(id + " already exists"), NodeRef();
    std::vector<Element *> nodes;
    for (const auto &ref : refs) {
        auto *element = resolve(workspace_, ref);
        if (!element || ref.isTree() || ref.parent() != refs.front().parent())
            return fail("extract nodes that share a parent"), NodeRef();
        nodes.push_back(element);
    }
    std::sort(nodes.begin(), nodes.end(), [](auto *a, auto *b) { return a->index() < b->index(); });

    // Ports from the keys the nodes use: read inside -> input, written inside and used outside -> output.
    const auto treeKey = refs.front().tree;
    const auto variables = analyzeVariables(workspace_, treeKey);
    std::set<NodeRef> inside;
    for (auto *node : nodes) {
        const auto top = refOf(workspace_, node);
        for (const auto &[name, key] : variables.keys)
            for (const auto &use : key.uses)
                if (use.node == top || isAncestor(top, use.node))
                    inside.insert(use.node);
    }
    std::vector<PortModel> ports;
    for (const auto &[name, key] : variables.keys) {
        unsigned in = 0, out = 0, first = 0;
        for (const auto &use : key.uses) {
            const bool here = inside.count(use.node);
            if (here && !in)
                first = use.access;
            (here ? in : out) |= use.access;
        }
        if (!in)
            continue;
        // Read inside before anything inside sets it: it comes in through a port.
        const bool reads = first & Read, writes = in & Write;
        const bool usedOutside = (out & Read) || (key.port && key.portDirection != PortDirection::Input);
        PortModel port;
        port.name = name;
        if (reads && writes && usedOutside)
            port.direction = PortDirection::InOut;
        else if (writes && usedOutside)
            port.direction = PortDirection::Output;
        else if (reads)
            port.direction = PortDirection::Input;
        else
            continue; // written and used only inside
        ports.push_back(port);
    }

    Change change(history_, "Extract " + id);
    auto *document = workspace_.createSubtreeDocument(id);
    adopt(change, *document);
    change.touch(*documentOf(workspace_, refs.front()));
    Element *parent = nodes.front()->parent;
    const auto at = childIndexFor(*parent, nodeIndexOf(*nodes.front()));
    Element *tree = document->trees().front();
    Element *into = tree;
    if (nodes.size() > 1)
        into = tree->append(std::make_unique<Element>("Sequence"));
    for (auto *node : nodes)
        for (auto &item : detachBlock(*node))
            into->append(std::move(item));

    auto call = std::make_unique<Element>("SubTree");
    call->set("ID", id);
    for (const auto &port : ports)
        call->set(port.name, "{" + port.name + "}");
    auto *placed = parent->insert(std::min(at, parent->children.size()), std::move(call));
    const auto ref = refOf(workspace_, placed);
    if (!ports.empty()) {
        if (!workspace_.models()) {
            NodeModel empty;
            empty.id = id;
            empty.category = Category::SubTree;
            workspace_.setSubtreeModel(empty);
            adopt(change, *workspace_.models());
        }
        change.touch(*workspace_.models());
        NodeModel model;
        model.id = id;
        model.category = Category::SubTree;
        model.ports = ports;
        workspace_.setSubtreeModel(model);
    }
    return ref;
}

bool Editor::setSubtreePorts(const std::string &id, const std::vector<PortModel> &ports,
                             const std::map<std::string, std::string> &renamed) {
    const auto *entry = workspace_.tree(id);
    std::set<std::string> names;
    for (const auto &port : ports) {
        if (port.name.empty() || port.name.find_first_of("{} \"") != std::string::npos ||
            isReservedAttribute(port.name))
            return fail("\"" + port.name + "\" is not a usable port name");
        if (!names.insert(port.name).second)
            return fail("two ports are called " + port.name);
    }
    Change change(history_, "Ports of " + id);
    if (!workspace_.models()) {
        NodeModel empty;
        empty.id = id;
        empty.category = Category::SubTree;
        workspace_.setSubtreeModel(empty);
        adopt(change, *workspace_.models());
    }
    change.touch(*workspace_.models());
    NodeModel model = workspace_.subtreeModel(id);
    std::set<std::string> before;
    for (const auto &port : model.ports)
        before.insert(port.name);
    model.ports = ports;
    workspace_.setSubtreeModel(model);

    // Call sites: renamed ports keep their values, removed ports go (other attributes stay).
    for (const auto &[key, tree] : workspace_.trees()) {
        for (auto *call : callsIn(*tree.tree, id)) {
            change.touch(*tree.document);
            for (const auto &[from, to] : renamed)
                call->rename(from, to);
            for (std::size_t i = call->attributes.size(); i-- > 0;) {
                const auto &name = call->attributes[i].name;
                if (before.count(name) && !names.count(name))
                    call->attributes.erase(call->attributes.begin() + std::ptrdiff_t(i));
            }
        }
    }
    // Inside the subtree, a renamed port is a renamed key.
    if (entry) {
        change.touch(*entry->document);
        for (const auto &[from, to] : renamed)
            renameKeyIn(workspace_, *entry->tree, from, to);
    }
    return true;
}
} // namespace notgroot::core
