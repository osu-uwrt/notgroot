// Blackboard key analysis of one tree.
#include "notgroot/core/variables.hpp"
#include <cctype>

namespace notgroot::core {
bool KeyInfo::undefined() const {
    // Read but never set here, and not handed in through a port.
    return reads > 0 && writes == 0 && !(port && portDirection != PortDirection::Output);
}

bool KeyInfo::unused() const {
    // Set but never read here, and not handed out through a port.
    return writes > 0 && reads == 0 && !(port && portDirection != PortDirection::Input);
}

namespace {
unsigned accessOf(const PortModel *port) {
    if (!port)
        return Read;
    switch (port->direction) {
    case PortDirection::Output:
        return Write;
    case PortDirection::InOut:
        return Read | Write;
    case PortDirection::Input:
        break;
    }
    return Read;
}

// Keys a script names: assigned ("key := value", "key = value", "key += 1") or read (any other name). Quoted
// text, numbers and true/false are not keys.
std::vector<std::string> scriptKeys(const std::string &code, unsigned access) {
    std::vector<std::string> keys;
    char quote = 0;
    for (std::size_t i = 0; i < code.size(); ++i) {
        if (quote) {
            quote = code[i] == quote ? 0 : quote;
            continue;
        }
        if (code[i] == '\'' || code[i] == '"') {
            quote = code[i];
            continue;
        }
        if (!(std::isalpha(static_cast<unsigned char>(code[i])) || code[i] == '_' || code[i] == '@'))
            continue;
        if (i > 0 && (std::isalnum(static_cast<unsigned char>(code[i - 1])) || code[i - 1] == '_'))
            continue;
        std::size_t end = code[i] == '@' ? i + 1 : i;
        while (end < code.size() && (std::isalnum(static_cast<unsigned char>(code[end])) || code[end] == '_'))
            ++end;
        std::size_t at = end;
        while (at < code.size() && code[at] == ' ')
            ++at;
        // "k += 1" reads and writes k; "k := 1" only writes it.
        const bool compound =
            at + 1 < code.size() && std::string("+-*/").find(code[at]) != std::string::npos && code[at + 1] == '=';
        const bool assigns = code.compare(at, 2, ":=") == 0 ||
                             (at < code.size() && code[at] == '=' && (at + 1 >= code.size() || code[at + 1] != '='));
        const auto name = code.substr(i, end - i);
        // "@key" is the root blackboard's: not this tree's.
        const bool counted = code[i] != '@' && name != "true" && name != "false";
        if (counted && (((access & Write) && (assigns || compound)) || ((access & Read) && !assigns)))
            keys.push_back(name);
        i = end;
    }
    return keys;
}

// Attributes holding script code.
bool isScript(const std::string &id, const std::string &name) {
    return (!name.empty() && name[0] == '_') || ((id == "Script" || id == "ScriptCondition") && name == "code") ||
           (id == "Precondition" && name == "if");
}
} // namespace

std::vector<KeyUse> keyUses(const Workspace &workspace, const Element &node, const NodeRef &ref) {
    std::vector<KeyUse> uses;
    const bool call = isSubTree(node);
    const NodeModel *model = call ? workspace.catalog().find(node.get("ID")) : workspace.catalog().find(nodeId(node));
    const auto id = nodeId(node);
    for (const auto &attribute : node.attributes) {
        const auto &name = attribute.name, &value = attribute.value;
        if (name == "ID" || name == "name")
            continue;
        // Preconditions, post-scripts and Script code: the keys they read and assign.
        if (isScript(id, name)) {
            if (!scriptKeys(value, Read).empty())
                uses.push_back({ref, name, Read, false, true});
            if (!scriptKeys(value, Write).empty())
                uses.push_back({ref, name, Write, false, true});
            continue;
        }
        const auto *port = model ? model->port(name) : nullptr;
        if (port && port->keyName) {
            // A bare name is the key written (or removed); {k} reads k for that name.
            if (isKeyReference(value))
                uses.push_back({ref, name, Read, false});
            else if (!value.empty())
                uses.push_back({ref, name, Write, false});
            continue;
        }
        if (isKeyReference(value)) {
            uses.push_back({ref, name, accessOf(port), false});
            continue;
        }
        if (!keysMentioned(value).empty())
            uses.push_back({ref, name, Read, true});
    }
    return uses;
}

namespace {
// The key a use names (keyUses keeps the attribute; this reads it back).
std::vector<std::string> keysOf(const Element &node, const KeyUse &use) {
    const auto value = node.get(use.attribute);
    if (use.mention)
        return keysMentioned(value);
    if (isKeyReference(value))
        return {keyOf(value)};
    if (use.script)
        return scriptKeys(value, use.access);
    return {value};
}

void walk(const Workspace &workspace, const Element &element, const NodeRef &ref, TreeVariables &result) {
    for (std::size_t i = 0; i < element.children.size(); ++i) {
        const auto &child = element.children[i];
        if (child->isComment())
            continue;
        const auto childRef = ref.child(i);
        std::vector<std::pair<std::string, KeyUse>> named;
        for (const auto &use : keyUses(workspace, *child, childRef)) {
            auto keys = keysOf(*child, use);
            // A text with several mentions repeats the use once per key; keep each key once per attribute.
            for (const auto &key : keys) {
                bool seen = false;
                for (const auto &[k, u] : named)
                    seen |= k == key && u.attribute == use.attribute && u.access == use.access;
                if (!seen && !key.empty())
                    named.push_back({key, use});
            }
        }
        for (auto &[key, use] : named) {
            auto &info = result.keys[key];
            info.name = key;
            if (use.access & Read)
                ++info.reads;
            if (use.access & Write)
                ++info.writes;
            info.uses.push_back(use);
        }
        walk(workspace, *child, childRef, result);
    }
}
} // namespace

TreeVariables analyzeVariables(const Workspace &workspace, const std::string &treeKey) {
    TreeVariables result;
    result.tree = treeKey;
    const auto *entry = workspace.tree(treeKey);
    if (!entry)
        return result;
    walk(workspace, *entry->tree, {treeKey, {}}, result);

    // The tree's own ports (a subtree's model).
    if (treeKey == entry->id)
        if (const auto *model = workspace.catalog().find(entry->id); model && model->category == Category::SubTree)
            for (const auto &port : model->ports) {
                auto &info = result.keys[port.name];
                info.name = port.name;
                info.port = true;
                info.portDirection = port.direction;
            }
    return result;
}
} // namespace notgroot::core
