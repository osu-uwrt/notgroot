// Tree globals: reading the wrapper, ordering, and the Script they are written as.
#include "notgroot/core/globals.hpp"
#include "notgroot/core/models.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>

namespace notgroot::core {
namespace {
bool identifier(const std::string &text) {
    if (text.empty() || !(std::isalpha(static_cast<unsigned char>(text[0])) || text[0] == '_'))
        return false;
    return std::all_of(text.begin(), text.end(),
                       [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
}

// A value as text and {key} pieces, in order (second: true for a key).
std::vector<std::pair<std::string, bool>> pieces(const std::string &value) {
    std::vector<std::pair<std::string, bool>> out;
    std::string text;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '{') {
            const auto end = value.find('}', i);
            if (end != std::string::npos && identifier(value.substr(i + 1, end - i - 1))) {
                if (!text.empty())
                    out.push_back({text, false});
                text.clear();
                out.push_back({value.substr(i + 1, end - i - 1), true});
                i = end;
                continue;
            }
        }
        text += value[i];
    }
    if (!text.empty())
        out.push_back({text, false});
    return out;
}

bool isMarker(const Element &element) {
    return element.isComment() && element.text.find("notgroot:globals") != std::string::npos;
}

// Splits on `separator` outside '…' strings.
std::vector<std::string> split(const std::string &text, const std::string &separator) {
    std::vector<std::string> out;
    std::string part;
    bool quoted = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\'')
            quoted = !quoted;
        if (!quoted && text.compare(i, separator.size(), separator) == 0) {
            out.push_back(part);
            part.clear();
            i += separator.size() - 1;
            continue;
        }
        part += text[i];
    }
    out.push_back(part);
    return out;
}
} // namespace

TreeGlobals readGlobals(const Element &behaviorTree) {
    TreeGlobals result;
    const auto nodes = behaviorTree.elements();
    if (nodes.size() != 1 || nodes.front()->tag != "Sequence" || nodes.front()->get("name") != "globals")
        return result;
    Element *wrapper = nodes.front();
    const auto at = wrapper->index();
    if (at == 0 || !isMarker(*behaviorTree.children[at - 1]))
        return result;
    const auto inside = wrapper->elements();
    if (inside.empty() || inside.front()->tag != "Script" ||
        !parseScript(inside.front()->get("code"), result.globals)) {
        result.globals.clear();
        return result;
    }
    result.wrapper = wrapper;
    result.script = inside.front();
    return result;
}

Element *treeBody(const Element &behaviorTree) {
    const auto globals = readGlobals(behaviorTree);
    return globals.wrapper ? globals.wrapper : const_cast<Element *>(&behaviorTree);
}

bool isGlobalsPart(const Element &element) {
    if (element.isComment() || !element.parent)
        return false;
    if (element.parent->tag == "BehaviorTree")
        return readGlobals(*element.parent).wrapper == &element;
    if (element.tag == "Script" && element.parent->parent && element.parent->parent->tag == "BehaviorTree")
        return readGlobals(*element.parent->parent).script == &element;
    return false;
}

int capacity(const Catalog &catalog, const Element &parent) {
    if (parent.tag == "BehaviorTree")
        return 1;
    if (parent.parent && parent.parent->tag == "BehaviorTree" && readGlobals(*parent.parent).wrapper == &parent)
        return 2;
    return maxChildren(nodeCategory(parent, catalog));
}

std::vector<std::string> valueKeys(const std::string &value) {
    std::vector<std::string> keys;
    for (const auto &[text, key] : pieces(value))
        if (key)
            keys.push_back(text);
    return keys;
}

std::vector<Global> orderGlobals(std::vector<Global> globals, std::vector<std::vector<std::string>> &cycles) {
    cycles.clear();
    std::map<std::string, std::size_t> index;
    for (std::size_t i = 0; i < globals.size(); ++i)
        index[globals[i].name] = i;
    std::vector<std::vector<std::size_t>> uses(globals.size());
    for (std::size_t i = 0; i < globals.size(); ++i)
        for (const auto &key : valueKeys(globals[i].value))
            if (const auto found = index.find(key); found != index.end())
                uses[i].push_back(found->second);

    // Cycles: strongly connected groups of two or more, or a global that uses itself (Tarjan).
    std::vector<int> order(globals.size(), -1), low(globals.size(), 0);
    std::vector<bool> stacked(globals.size(), false);
    std::vector<std::size_t> stack;
    int counter = 0;
    const std::function<void(std::size_t)> connect = [&](std::size_t v) {
        order[v] = low[v] = counter++;
        stack.push_back(v);
        stacked[v] = true;
        for (const auto w : uses[v]) {
            if (order[w] < 0) {
                connect(w);
                low[v] = std::min(low[v], low[w]);
            } else if (stacked[w])
                low[v] = std::min(low[v], order[w]);
        }
        if (low[v] != order[v])
            return;
        std::vector<std::string> group;
        std::size_t w = 0;
        do {
            w = stack.back();
            stack.pop_back();
            stacked[w] = false;
            group.push_back(globals[w].name);
        } while (w != v);
        if (group.size() > 1 || std::find(uses[v].begin(), uses[v].end(), v) != uses[v].end()) {
            std::sort(group.begin(), group.end());
            cycles.push_back(group);
        }
    };
    for (std::size_t v = 0; v < globals.size(); ++v)
        if (order[v] < 0)
            connect(v);
    if (!cycles.empty()) {
        std::sort(cycles.begin(), cycles.end());
        return globals;
    }

    // Each after what it uses; among the ready ones, by name.
    std::vector<int> waiting(globals.size(), 0);
    for (std::size_t i = 0; i < globals.size(); ++i)
        waiting[i] = int(uses[i].size());
    std::set<std::pair<std::string, std::size_t>> ready;
    for (std::size_t i = 0; i < globals.size(); ++i)
        if (!waiting[i])
            ready.insert({globals[i].name, i});
    std::vector<Global> out;
    while (!ready.empty()) {
        const auto next = ready.begin()->second;
        ready.erase(ready.begin());
        out.push_back(globals[next]);
        for (std::size_t i = 0; i < globals.size(); ++i)
            for (const auto used : uses[i])
                if (used == next && --waiting[i] == 0)
                    ready.insert({globals[i].name, i});
    }
    return out;
}

std::string scriptFor(const std::vector<Global> &globals) {
    std::string code;
    for (const auto &global : globals) {
        if (!code.empty())
            code += "; ";
        code += global.name + " := ";
        const auto parts = pieces(global.value);
        if (parts.empty())
            code += "''";
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i)
                code += " .. ";
            code += parts[i].second ? parts[i].first : "'" + parts[i].first + "'";
        }
    }
    return code;
}

bool parseScript(const std::string &code, std::vector<Global> &out) {
    out.clear();
    if (code.empty())
        return false;
    for (const auto &statement : split(code, "; ")) {
        const auto assign = statement.find(" := ");
        if (assign == std::string::npos)
            return false;
        Global global{statement.substr(0, assign), ""};
        const auto expression = statement.substr(assign + 4);
        if (expression != "''")
            for (const auto &term : split(expression, " .. ")) {
                if (term.size() >= 2 && term.front() == '\'' && term.back() == '\'')
                    global.value += term.substr(1, term.size() - 2);
                else if (identifier(term))
                    global.value += "{" + term + "}";
                else
                    return false;
            }
        out.push_back(global);
    }
    // Only code notgroot would write the same way is its own.
    return scriptFor(out) == code;
}

std::string globalProblem(const Global &global) {
    if (!identifier(global.name))
        return "\"" + global.name + "\" can't be a global: use letters, digits and _";
    if (global.value.find('\'') != std::string::npos)
        return global.name + ": a global's text can't hold ' (BehaviorTree.CPP scripts can't escape it)";
    return {};
}
} // namespace notgroot::core
