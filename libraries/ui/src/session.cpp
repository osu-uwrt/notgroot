// Session: shared editor state and the commands.
#include "notgroot/ui/session.hpp"
#include "notgroot/ui/run_view.hpp"
#include "notgroot/ui/style.hpp"
#include "notgroot/ui/theme.hpp"
#include <algorithm>
#include <functional>
#include <imgui_internal.h>

namespace notgroot::ui {
using namespace core;

Session::Session() {
    registerCommands();
}

void Session::attachRun(std::shared_ptr<RunView> view) {
    runView = std::move(view);
    commands.clear();
    registerCommands();
}

bool Session::loadBuiltinModels(const std::filesystem::path &file, std::string &error) {
    return workspace.loadBuiltinModels(file, error);
}

void Session::reset() {
    history.clear();
    tabs.clear();
    views_.clear();
    active.clear();
    cursor = {};
    selection.clear();
    highlightKey.clear();
    conflicts.clear();
}

bool Session::openText(const std::filesystem::path &name, const std::string &text, std::string &error) {
    if (!workspace.openText(name, text, error))
        return false;
    reset();
    return true;
}

bool Session::open(const std::filesystem::path &path, std::string &error) {
    if (!workspace.open(path, error))
        return false;
    reset();
    // Open the file asked for, else the first mission.
    std::string first;
    for (const auto &[key, entry] : workspace.trees())
        if (entry.document->path == workspace.focus() && (first.empty() || key != entry.id))
            first = key;
    if (first.empty() && !workspace.missions().empty())
        first = workspace.missions().front();
    if (first.empty() && !workspace.trees().empty())
        first = workspace.trees().begin()->first;
    if (!first.empty())
        openTree(first);
    status("Opened " + workspace.directory().string() + " (" + std::to_string(workspace.trees().size()) + " trees)");
    return true;
}

void Session::confirmThen(std::function<void()> action) {
    if (!dirty()) {
        action();
        return;
    }
    afterUnsaved = std::move(action);
    ask(Dialog::Unsaved);
}

bool Session::dirty() const {
    for (const auto &document : workspace.documents())
        if (document->dirty())
            return true;
    return false;
}

void Session::save() {
    // Nothing is written while a node floats: the files would not load.
    floating.clear();
    for (const auto &[key, entry] : workspace.trees()) {
        const auto found = floatingNodes(workspace, key);
        floating.insert(floating.end(), found.begin(), found.end());
    }
    if (!floating.empty()) {
        ask(Dialog::Floating);
        status("Not saved: nodes are floating", true);
        return;
    }
    std::vector<std::string> errors;
    const auto written = workspace.save(errors);
    if (!errors.empty()) {
        status(errors.front(), true);
        return;
    }
    int bad = 0;
    for (const auto &problem : problems())
        bad += problem.severity == Severity::Error;
    if (!written.empty() && check_ && check_->available() && !workspace.loose()) {
        // One already running is out of date: run again once it finishes.
        if (check_->running())
            checkAgain_ = true;
        else
            check_->start(workspace);
    }
    if (written.empty())
        status("Nothing to save");
    else
        status("Saved " + std::to_string(written.size()) + (written.size() == 1 ? " file" : " files") +
                   (bad ? " — " + std::to_string(bad) + " problems will stop it loading (F8)" : ""),
               bad > 0);
}

void Session::setChecker(const std::filesystem::path &program) {
    check_ = std::make_unique<BtCheck>(program);
}

bool Session::checking() const {
    return check_ && check_->running();
}

void Session::checkNow() {
    if (!check_ || !check_->available()) {
        status("notgroot-check is missing: build it with the editor (tools/check)", true);
        return;
    }
    if (workspace.loose()) {
        status("The BehaviorTree.CPP check needs a trees folder (lib/node_models.xml)", true);
        return;
    }
    if (!check_->start(workspace) && !check_->running())
        status("Could not start the BehaviorTree.CPP check", true);
}

void Session::pollCheck() {
    if (!check_)
        return;
    auto result = check_->take();
    if (!result)
        return;
    if (checkAgain_) {
        checkAgain_ = false;
        check_->start(workspace);
        return;
    }
    btProblems = std::move(result->problems);
    for (auto &problem : btProblems) // on the mission's main tree, so the panel can go there
        for (const auto &key : workspace.missions())
            if (workspace.tree(key)->document->path == problem.file)
                problem.node = {key, {}};
    problemsVersion_ = ~0u; // merge them into the list
    if (!result->error.empty()) {
        btSummary = result->error;
        status(btSummary, true);
    } else if (result->failed == 0) {
        btSummary = std::to_string(result->loaded) + " missions load in BehaviorTree.CPP";
        status(btSummary);
    } else {
        btSummary = std::to_string(result->failed) + " of " + std::to_string(result->loaded + result->failed) +
                    " missions fail to load in BehaviorTree.CPP";
        status(btSummary + " (Problems panel)", true);
    }
}

void Session::pollDisk() {
    if (!workspace.isOpen())
        return;
    for (auto *document : workspace.changedOnDisk()) {
        if (std::find(conflicts.begin(), conflicts.end(), document) != conflicts.end())
            continue;
        if (document->dirty())
            conflicts.push_back(document);
        else {
            workspace.reload(*document);
            history.clear(); // its snapshots predate the file now on disk
            status("Reloaded " + document->path.filename().string() + " (changed on disk)");
        }
    }
}

void Session::openTree(const std::string &key) {
    if (!workspace.tree(key))
        return;
    if (std::find(tabs.begin(), tabs.end(), key) == tabs.end())
        tabs.push_back(key);
    if (active != key) {
        active = key;
        selection.clear();
        highlightKey.clear();
        cursor = {{key, {}}, {}};
    }
    focusCanvas = true;
}

void Session::closeTree(const std::string &key) {
    const auto found = std::find(tabs.begin(), tabs.end(), key);
    if (found == tabs.end())
        return;
    const auto index = found - tabs.begin();
    tabs.erase(found);
    if (active == key) {
        active.clear();
        if (!tabs.empty())
            openTree(tabs[std::size_t(std::min<long>(index, long(tabs.size()) - 1))]);
    }
}

void Session::setCursor(const CardKey &key, bool keepSelection) {
    cursor = key;
    if (!keepSelection)
        selection.clear();
    revealPending = true;
}

void Session::toggleSelected(const NodeRef &ref) {
    const auto found = std::find(selection.begin(), selection.end(), ref);
    if (found == selection.end())
        selection.push_back(ref);
    else
        selection.erase(found);
}

bool Session::selected(const NodeRef &ref) const {
    return std::find(selection.begin(), selection.end(), ref) != selection.end();
}

std::vector<NodeRef> Session::targets() const {
    if (!selection.empty())
        return selection;
    if (!readOnly(cursor) && !cursor.ref.empty())
        return {cursor.ref};
    return {};
}

bool Session::unlocked(const std::string &subtree) const {
    const auto found = views_.find(active);
    return found != views_.end() && found->second.unlocked.count(subtree) > 0;
}

// The subtrees an element calls, at any depth under it.
static void collectCalls(const Element &element, std::set<std::string> &out) {
    for (const auto *child : element.elements()) {
        if (isSubTree(*child))
            out.insert(child->get("ID"));
        collectCalls(*child, out);
    }
}

void Session::lock(const std::string &subtree) {
    // It and everything under it, through any chain of calls.
    std::set<std::string> under{subtree};
    std::vector<std::string> open{subtree};
    while (!open.empty()) {
        const auto *entry = workspace.tree(open.back());
        open.pop_back();
        std::set<std::string> calls;
        if (entry)
            collectCalls(*entry->tree, calls);
        for (const auto &id : calls)
            if (under.insert(id).second)
                open.push_back(id);
    }
    auto &v = view(active);
    int locked = 0;
    for (const auto &id : under)
        if (v.unlocked.erase(id)) {
            ++locked;
            selection.erase(std::remove_if(selection.begin(), selection.end(),
                                           [&](const NodeRef &r) { return r.tree == id; }),
                            selection.end());
        }
    if (locked)
        status("Locked " + subtree + (locked > 1 ? " and the subtrees under it" : ""));
}

void Session::toggleLock(const std::string &subtree) {
    auto &v = view(active);
    if (v.unlocked.count(subtree)) {
        lock(subtree);
        return;
    }
    v.unlocked.insert(subtree);
    const int trees = treesUsing(subtree);
    status("Editing " + subtree + " in place: changes go to its file, for every tree that calls it" +
           (trees ? " (" + std::to_string(trees) + (trees == 1 ? " tree)" : " trees)") : ""));
}

std::string Session::lockTarget() {
    if (cursor.inside())
        return cursor.ref.tree;
    const auto *element = cursor.ref.empty() ? nullptr : resolve(workspace, cursor.ref);
    if (element && isSubTree(*element) && view(active).opened.count(cursor.ref.path) &&
        workspace.tree(element->get("ID")))
        return element->get("ID");
    return {};
}

int Session::treesUsing(const std::string &subtree) const {
    auto &[version, counts] = treesUsing_;
    if (version != workspace.version()) {
        version = workspace.version();
        counts.clear();
        for (const auto &[key, entry] : workspace.trees()) {
            std::set<std::string> calls;
            collectCalls(*entry.tree, calls);
            for (const auto &id : calls)
                ++counts[id];
        }
    }
    const auto found = counts.find(subtree);
    return found != counts.end() ? found->second : 0;
}

void Session::reveal(NodeRef ref) {
    if (ref.empty())
        return;
    openTree(ref.tree);
    // Unfold the way down to it.
    auto &v = view(ref.tree);
    for (std::size_t depth = 0; depth < ref.path.size(); ++depth)
        v.folded.erase(std::vector<std::size_t>(ref.path.begin(), ref.path.begin() + std::ptrdiff_t(depth)));
    setCursor(ref);
}

void Session::edited(const NodeRef &made) {
    if (made.empty()) {
        if (!editor.error().empty())
            status(editor.error(), true);
        return;
    }
    // Made in a subtree opened in place: the cursor stays there, under the same call.
    if (made.tree != active && cursor.inside() && cursor.ref.tree == made.tree)
        setCursor(CardKey{made, cursor.origin, cursor.via});
    else
        setCursor(made);
    focusCanvas = true;
}

void Session::edited(bool ok) {
    if (!ok && !editor.error().empty())
        status(editor.error(), true);
}

// After undo or a reload the cursor may point at nothing: walk up until it does.
void Session::validateCursor() {
    if (!active.empty() && !workspace.tree(active)) {
        closeTree(active);
        return;
    }
    while (!cursor.ref.empty() && !resolve(workspace, cursor.ref)) {
        if (cursor.ref.path.empty()) {
            cursor = {{active, {}}, {}};
            break;
        }
        cursor.ref.path.pop_back();
    }
    if (!cursor.origin.empty() && !resolve(workspace, cursor.origin))
        cursor = {cursor.origin.parent(), {}};
    selection.erase(std::remove_if(selection.begin(), selection.end(),
                                   [&](const NodeRef &r) {
                                       return !resolve(workspace, r) || (r.tree != active && !unlocked(r.tree));
                                   }),
                    selection.end());
}

const std::vector<Problem> &Session::problems() {
    if (problemsVersion_ != workspace.version()) {
        problemsVersion_ = workspace.version();
        problems_ = validate(workspace);
        problems_.insert(problems_.begin(), btProblems.begin(), btProblems.end());
        severity_.clear();
        for (const auto &problem : problems_) {
            if (problem.node.empty())
                continue;
            auto &worst = severity_.try_emplace(problem.node, int(Severity::Note)).first->second;
            worst = std::min(worst, int(problem.severity));
        }
        validateCursor();
    }
    return problems_;
}

int Session::severityAt(const NodeRef &ref) {
    problems();
    const auto found = severity_.find(ref);
    return found == severity_.end() ? -1 : found->second;
}

const TreeVariables &Session::variables(const std::string &key) {
    auto &cached = variables_[key];
    if (cached.first != workspace.version() || cached.second.tree != key) {
        cached.first = workspace.version();
        cached.second = analyzeVariables(workspace, key);
    }
    return cached.second;
}

ImVec4 Session::keyColor(const std::string &tree, const std::string &key) {
    auto &cached = keyColors_[tree];
    if (cached.version != workspace.version() || cached.theme != currentTheme()) {
        std::vector<std::string> keys;
        for (const auto &[name, info] : variables(tree).keys)
            keys.push_back(name);
        cached = {workspace.version(), currentTheme(), keyColors(keys)};
    }
    const auto found = cached.colors.find(key);
    return found != cached.colors.end() ? found->second : keyColors({key}).begin()->second;
}

void Session::pick(PickFor purpose) {
    const char *lockedMessage = "This node belongs to a locked subtree: unlock it (Ctrl+U) or open it (Ctrl+Enter)";
    const auto targets = this->targets();
    if (purpose != PickFor::Child && targets.empty() && !(cursor.ref.isTree() && purpose == PickFor::Child)) {
        status(readOnly(cursor) ? lockedMessage : "Select a node first", true);
        return;
    }
    if (readOnly(cursor)) {
        status(lockedMessage, true);
        return;
    }
    picker.open = true;
    picker.purpose = purpose;
    picker.target = cursor.ref;
}

void Session::ask(Dialog::Kind kind, std::string target, std::string text) {
    dialog = {kind, std::move(target), std::move(text), true};
}

void Session::status(std::string text, bool error) {
    statusText = std::move(text);
    statusError = error;
    statusTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0;
}

const Command *Session::command(const std::string &id) const {
    for (const auto &c : commands)
        if (c.id == id)
            return &c;
    return nullptr;
}

void Session::run(const std::string &id) {
    const auto *c = command(id);
    if (c && enabled(*c))
        c->run();
    else if (c && c->why)
        status(c->why(), true);
}

std::vector<NodeRef> Session::allNodes() const {
    std::vector<NodeRef> all;
    const auto *entry = workspace.tree(active);
    if (!entry)
        return all;
    const std::function<void(const Element &, const NodeRef &)> visit = [&](const Element &element,
                                                                            const NodeRef &ref) {
        for (std::size_t i = 0; i < element.children.size(); ++i) {
            const auto &child = *element.children[i];
            if (child.isComment() || (child.tag == "Script" && isGlobalsPart(child)))
                continue;
            if (!isGlobalsPart(child)) // the globals wrapper: only its root is a node
                all.push_back(ref.child(i));
            visit(child, ref.child(i));
        }
    };
    visit(*entry->tree, {active, {}});
    return all;
}

bool Session::model(const std::string &id, NodeModel &out) const {
    const auto *found = workspace.catalog().find(id);
    if (!found)
        return false;
    out = *found;
    return true;
}

namespace {
int callback(ImGuiInputTextCallbackData *data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto *text = static_cast<std::string *>(data->UserData);
        text->resize(std::size_t(data->BufTextLen));
        data->Buf = text->data();
    }
    return 0;
}

struct Chained {
    std::string *text;
    ImGuiInputTextCallback next;
    void *user;
};

int chained(ImGuiInputTextCallbackData *data) {
    auto *chain = static_cast<Chained *>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        chain->text->resize(std::size_t(data->BufTextLen));
        data->Buf = chain->text->data();
        return 0;
    }
    data->UserData = chain->user;
    const int result = chain->next(data);
    data->UserData = chain;
    return result;
}
} // namespace

bool inputString(const char *label, std::string &text, ImGuiInputTextFlags flags, ImGuiInputTextCallback next,
                 void *user) {
    if (!next)
        return ImGui::InputText(label, text.data(), text.capacity() + 1, flags | ImGuiInputTextFlags_CallbackResize,
                                callback, &text);
    Chained chain{&text, next, user};
    return ImGui::InputText(label, text.data(), text.capacity() + 1, flags | ImGuiInputTextFlags_CallbackResize,
                            chained, &chain);
}

bool inputStringWithHint(const char *label, const char *hint, std::string &text, ImGuiInputTextFlags flags) {
    return ImGui::InputTextWithHint(label, hint, text.data(), text.capacity() + 1,
                                    flags | ImGuiInputTextFlags_CallbackResize, callback, &text);
}

std::string chordName(ImGuiKeyChord chord) {
    if (!chord)
        return {};
    return ImGui::GetKeyChordName(chord);
}

void Session::registerCommands() {
    const auto open = [this] { return workspace.isOpen(); };
    const auto onTree = [this] { return workspace.isOpen() && !active.empty(); };
    const auto onNode = [this] { return !targets().empty(); };
    // A run's trees are read-only: its session leaves out every command that edits, and Enter and Backspace go in
    // and out of subtrees instead.
    const auto edits = [](const std::string &id) {
        static const std::set<std::string> kept = {"edit.copy", "node.definition", "tree.lock"};
        if (kept.count(id))
            return false;
        return id.rfind("edit.", 0) == 0 || id.rfind("node.", 0) == 0 || id.rfind("tree.", 0) == 0 ||
               id == "key.rename" || id == "file.save" || id == "file.check";
    };
    const auto add = [this, edits](Command c) {
        if (!(runView && edits(c.id)))
            commands.push_back(std::move(c));
    };
    // What the node menu and the finder offer depends on the cursor's node.
    const auto at = [this]() -> Element * { return cursor.ref.empty() ? nullptr : resolve(workspace, cursor.ref); };
    const auto onEditable = [this] { return !cursor.ref.empty() && !cursor.ref.isTree() && !readOnly(cursor); };
    const auto onCall = [at] {
        const auto *element = at();
        return element && isSubTree(*element);
    };
    const auto onSubtree = [this] {
        const auto *entry = workspace.tree(active);
        return entry && entry->id == active;
    };
    const auto takesChildren = [this, at] {
        const auto *element = at();
        return !readOnly(cursor) && element &&
               (cursor.ref.isTree() || maxChildren(nodeCategory(*element, workspace.catalog())) != 0);
    };
    // Wrap and extract take nodes under one parent.
    const auto siblings = [this] {
        const auto refs = targets();
        return !refs.empty() && std::all_of(refs.begin(), refs.end(), [&](auto &r) {
            return !r.isTree() && r.parent() == refs.front().parent();
        });
    };
    const auto notSiblings = [] { return std::string("Select nodes under one parent"); };

    // File
    add({"file.open", "Open trees folder…", "File", ImGuiMod_Ctrl | ImGuiKey_O, false,
         [this] {
             if (browse)
                 browse();
         },
         nullptr});
    add({"file.save", "Save", "File", ImGuiMod_Ctrl | ImGuiKey_S, false, [this] { save(); }, open});
    add({"file.check", "Check with BehaviorTree.CPP", "File", ImGuiKey_F7, false, [this] { checkNow(); },
         [this] { return workspace.isOpen() && !checking(); }});
    add({"file.quit", "Quit", "File", ImGuiMod_Ctrl | ImGuiKey_Q, false,
         [this] {
             if (quit)
                 quit();
         },
         nullptr});

    // Edit
    add({"edit.undo", "Undo", "Edit", ImGuiMod_Ctrl | ImGuiKey_Z, false,
         [this] {
             const auto label = history.undoLabel();
             if (history.undo())
                 status("Undid: " + label);
         },
         [this] { return history.canUndo(); }});
    add({"edit.redo", "Redo", "Edit", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, false,
         [this] {
             const auto label = history.redoLabel();
             if (history.redo())
                 status("Redid: " + label);
         },
         [this] { return history.canRedo(); }});
    add({"edit.redo2", "Redo", "Edit", ImGuiMod_Ctrl | ImGuiKey_Y, false, [this] { run("edit.redo"); },
         [this] { return history.canRedo(); }});
    add({"edit.copy", "Copy nodes", "Edit", ImGuiMod_Ctrl | ImGuiKey_C, true,
         [this] {
             const auto text = copyNodes(
                 workspace, targets().empty() && readOnly(cursor) ? std::vector<NodeRef>{cursor.ref} : targets());
             ImGui::SetClipboardText(text.c_str());
             status("Copied");
         },
         [this] { return !targets().empty() || readOnly(cursor); }, [this] { return !cursor.ref.isTree(); }});
    add({"edit.cut", "Cut nodes", "Edit", ImGuiMod_Ctrl | ImGuiKey_X, true,
         [this] {
             ImGui::SetClipboardText(copyNodes(workspace, targets()).c_str());
             edited(editor.remove(targets()));
         },
         onNode, onEditable});
    add({"edit.paste", "Paste nodes (into the cursor, else after it)", "Edit", ImGuiMod_Ctrl | ImGuiKey_V, true,
         [this] {
             const char *clip = ImGui::GetClipboardText();
             auto nodes = parseClipboard(clip ? clip : "");
             if (nodes.empty()) {
                 status("The clipboard holds no tree nodes", true);
                 return;
             }
             auto *element = resolve(workspace, cursor.ref);
             if (element && acceptsChild(workspace, *element))
                 edited(editor.insertMany(cursor.ref, nodeChildren(*element).size(), std::move(nodes), "Paste"));
             else if (element && !cursor.ref.isTree()) {
                 const auto siblings = nodeChildren(*element->parent);
                 const auto index =
                     std::size_t(std::find(siblings.begin(), siblings.end(), element) - siblings.begin());
                 edited(editor.insertMany(cursor.ref.parent(), index + 1, std::move(nodes), "Paste"));
             }
         },
         [this, onTree] {
             const char *clip = ImGui::GetClipboardText();
             return onTree() && !readOnly(cursor) && !parseClipboard(clip ? clip : "").empty();
         },
         [this] { return !readOnly(cursor); }, [] { return std::string("The clipboard holds no tree nodes"); }});
    add({"edit.duplicate", "Duplicate nodes", "Edit", ImGuiMod_Ctrl | ImGuiKey_D, true,
         [this] {
             const auto refs = targets();
             auto *last = resolve(workspace, refs.back());
             if (!last || refs.back().isTree())
                 return;
             auto nodes = parseClipboard(copyNodes(workspace, refs));
             auto siblings = nodeChildren(*last->parent);
             const auto index = std::size_t(std::find(siblings.begin(), siblings.end(), last) - siblings.begin());
             edited(editor.insertMany(refs.back().parent(), index + 1, std::move(nodes), "Duplicate"));
         },
         onNode, onEditable});
    add({"edit.delete", "Delete nodes", "Edit", ImGuiKey_Delete, true, [this] { edited(editor.remove(targets())); },
         onNode, onEditable});
    add({"edit.delete2", "Delete nodes", "Edit", ImGuiKey_Backspace, true, [this] { run("edit.delete"); }, onNode,
         onEditable});

    // Select
    add({"select.siblings", "Select the siblings (again: the whole tree)", "Select", ImGuiMod_Ctrl | ImGuiKey_A, true,
         [this] {
             std::vector<NodeRef> group;
             if (!cursor.ref.isTree() && !readOnly(cursor))
                 if (const auto *element = resolve(workspace, cursor.ref))
                     for (std::size_t i = 0; i < element->parent->children.size(); ++i)
                         if (!element->parent->children[i]->isComment())
                             group.push_back(cursor.ref.parent().child(i));
             auto now = selection;
             std::sort(now.begin(), now.end());
             if (group.empty() || now == group)
                 group = allNodes();
             selection = group;
             status(std::to_string(selection.size()) + " nodes selected");
         },
         onTree});
    add({"select.below", "Select the node and everything under it", "Select",
         ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_A, true,
         [this] {
             selection.clear();
             for (const auto &ref : allNodes())
                 if (ref == cursor.ref || isAncestor(cursor.ref, ref))
                     selection.push_back(ref);
             status(std::to_string(selection.size()) + " nodes selected");
         },
         [this] { return !cursor.inside() && !cursor.ref.empty(); }});
    add({"select.sameType", "Select every node of this type", "Select", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_L,
         true,
         [this, at] {
             const auto *element = at();
             if (!element)
                 return;
             const auto type = isSubTree(*element) ? "SubTree:" + element->get("ID") : nodeId(*element);
             selection.clear();
             for (const auto &ref : allNodes()) {
                 const auto *other = resolve(workspace, ref);
                 if ((isSubTree(*other) ? "SubTree:" + other->get("ID") : nodeId(*other)) == type)
                     selection.push_back(ref);
             }
             status(std::to_string(selection.size()) + " " + type + " selected");
         },
         onEditable, onEditable});
    add({"select.toggle", "Add the node to the selection, or take it out", "Select", ImGuiMod_Ctrl | ImGuiKey_Space,
         true, [this] { toggleSelected(cursor.ref); }, onEditable, onEditable});
    add({"select.none", "Clear the selection", "Select", 0, false,
         [this] {
             selection.clear();
             highlightKey.clear();
         },
         [this] { return !selection.empty(); }});

    // Node
    add({"node.child", "Add child…", "Node", ImGuiKey_Tab, true, [this] { pick(PickFor::Child); }, onTree,
         takesChildren});
    add({"node.sibling", "Add sibling after…", "Node", ImGuiMod_Shift | ImGuiKey_Tab, true,
         [this] { pick(PickFor::Sibling); }, onNode, onEditable});
    add({"node.wrap", "Wrap in…", "Node", ImGuiMod_Ctrl | ImGuiKey_W, true, [this] { pick(PickFor::Wrap); }, siblings,
         onEditable, notSiblings});
    add({"node.retype", "Change node type…", "Node", ImGuiMod_Ctrl | ImGuiKey_R, true,
         [this] { pick(PickFor::Retype); }, onNode, onEditable});
    add({"node.unwrap", "Unwrap (keep the children)", "Node", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_W, true,
         [this] { edited(editor.unwrap(cursor.ref)); }, onNode,
         [onEditable, at] { return onEditable() && !at()->elements().empty(); }});
    add({"node.earlier", "Move earlier among siblings", "Node", 0, false,
         [this] { edited(editor.move(cursor.ref, -1)); }, onNode});
    add({"node.later", "Move later among siblings", "Node", 0, false, [this] { edited(editor.move(cursor.ref, 1)); },
         onNode});
    // Moving out of or into a node never refuses for lack of room: the node floats there until it moves on.
    const auto landed = [this](const NodeRef &made) {
        edited(made);
        if (const auto *element = made.empty() ? nullptr : resolve(workspace, made);
            element && isFloating(workspace, *element))
            status("Floating: " +
                       (element->parent->tag == "BehaviorTree" ? std::string("a tree has one root")
                                                               : nodeId(*element->parent) + " can't hold it") +
                       "; keep moving it (Alt+arrows)",
                   true);
    };
    add({"node.outdent", "Move out of the parent", "Node", 0, false,
         [this, landed] { landed(editor.outdent(cursor.ref, true)); }, onNode});
    add({"node.indent", "Move into the previous sibling", "Node", 0, false,
         [this, landed] { landed(editor.indent(cursor.ref, true)); }, onNode});
    add({"node.edit", "Edit the node's ports", "Node", ImGuiKey_Enter, true,
         [this] {
             focusInspector = true;
             focusPort.clear();
         },
         onTree});
    add({"node.definition", "Open the subtree's definition", "Node", ImGuiMod_Ctrl | ImGuiKey_Enter, true,
         [this] {
             auto *element = resolve(workspace, cursor.ref);
             if (runView) {
                 const int into =
                     element && isSubTree(*element)
                         ? runView->run.calledBy(runView->run.uidAt(cardInstance(*this, cursor), cursor.ref.path))
                         : cardInstance(*this, cursor);
                 if (into >= 0)
                     focusFrame(*this, into);
                 else
                     status("Not a subtree call", true);
             } else if (element && isSubTree(*element) && workspace.tree(element->get("ID")))
                 openTree(element->get("ID"));
             else if (!cursor.origin.empty())
                 reveal(cursor.ref);
             else
                 status("Not a subtree call", true);
         },
         onTree, [this, onCall] { return onCall() || cursor.inside(); }});

    add({"node.makeGlobal", "Make global (set before the tree runs)", "Node", 0, false,
         [this] {
             if (editor.makeGlobal(cursor.ref))
                 status("Now a global: see Variables > Globals");
             else
                 status("Can't make it global: " + editor.error(), true);
         },
         onEditable,
         [this, at, onEditable] {
             const auto *element = at();
             return onEditable() && element && (nodeId(*element) == "SetBlackboardV3" || nodeId(*element) == "Format");
         }});

    // Tree
    add({"tree.newSubtree", "New subtree…", "Tree", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_N, false,
         [this] { ask(Dialog::NewSubtree); }, open});
    add({"tree.newMission", "New mission…", "Tree", 0, false, [this] { ask(Dialog::NewMission); }, open});
    add({"tree.rename", "Rename tree…", "Tree", 0, false, [this] { ask(Dialog::RenameTree, active, active); }, onTree});
    add({"tree.lock", "Lock / unlock the subtree opened here", "Tree", ImGuiMod_Ctrl | ImGuiKey_U, true,
         [this] { toggleLock(lockTarget()); }, [this] { return !lockTarget().empty(); },
         [this] { return !lockTarget().empty(); },
         [] { return std::string("Put the cursor on a subtree opened in place (Space opens a call)"); }});
    add({"tree.extract", "Extract selection to a new subtree…", "Tree", ImGuiMod_Ctrl | ImGuiKey_E, true,
         [this] { ask(Dialog::ExtractSubtree); }, siblings, onEditable, notSiblings});
    add({"tree.ports", "Edit subtree ports…", "Tree", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_E, false,
         [this] {
             auto *element = resolve(workspace, cursor.ref);
             const auto *entry = workspace.tree(active);
             std::string id = element && isSubTree(*element) ? element->get("ID") : entry ? entry->id : "";
             if (entry && id == entry->id && active != entry->id) {
                 status("A mission's main tree has no ports", true);
                 return;
             }
             ask(Dialog::EditPorts, id);
         },
         onTree, [onCall, onSubtree] { return onCall() || onSubtree(); }});
    add({"tree.delete", "Delete tree", "Tree", 0, false,
         [this] {
             const auto key = active;
             if (editor.deleteTree(key)) {
                 closeTree(key);
                 status("Deleted " + key + " (removed from disk on save)");
             } else
                 status(editor.error(), true);
         },
         onTree});
    add({"key.rename", "Rename variable…", "Tree", ImGuiKey_F2, false,
         [this] {
             if (!highlightKey.empty())
                 ask(Dialog::RenameKey, active, highlightKey);
             else if (cursor.ref.isTree())
                 ask(Dialog::RenameTree, active, active);
             else
                 status("Pick a variable first (Variables panel, or $ in the finder)", true);
         },
         onTree});

    // View
    add({"view.finder", "Go to anything…", "View", ImGuiMod_Ctrl | ImGuiKey_P, false,
         [this] {
             showFinder = true;
             finderPrefix.clear();
         },
         nullptr});
    add({"view.commands", "Commands…", "View", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P, false,
         [this] {
             showFinder = true;
             finderPrefix = ">";
         },
         nullptr});
    add({"view.variables", "Go to variable…", "View", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_V, false,
         [this] {
             showFinder = true;
             finderPrefix = "$";
         },
         onTree});
    add({"view.search", "Search this tree", "View", ImGuiMod_Ctrl | ImGuiKey_F, false,
         [this] {
             showSearch = true;
             focusSearch = true;
             focusCanvas = false;
         },
         onTree});
    add({"view.nextProblem", "Next problem", "View", ImGuiKey_F8, false,
         [this] {
             const auto &list = problems();
             std::vector<const Problem *> located;
             for (const auto &p : list)
                 if (!p.node.empty() && p.severity != Severity::Note)
                     located.push_back(&p);
             if (located.empty()) {
                 status("No problems");
                 return;
             }
             std::size_t next = 0;
             for (std::size_t i = 0; i < located.size(); ++i)
                 if (located[i]->node == cursor.ref) {
                     next = (i + 1) % located.size();
                     break;
                 }
             reveal(located[next]->node);
             status(std::string(severityName(located[next]->severity)) + ": " + located[next]->message,
                    located[next]->severity == Severity::Error);
         },
         open});
    add({"view.direction", "Toggle layout: top-down / left-right", "View", ImGuiMod_Ctrl | ImGuiKey_L, false,
         [this] {
             horizontal = !horizontal;
             for (auto &[key, v] : views_)
                 v.fit = true;
         },
         nullptr});
    add({"view.fit", "Fit tree to view", "View", ImGuiMod_Ctrl | ImGuiKey_0, false, [this] { view(active).fit = true; },
         onTree});
    add({"view.unfoldAll", "Unfold everything", "View", 0, false, [this] { view(active).folded.clear(); }, onTree});
    add({"view.closeTab", "Close tree tab", "View", ImGuiMod_Ctrl | ImGuiKey_F4, false, [this] { closeTree(active); },
         onTree});
    add({"view.nextTab", "Next tree tab", "View", ImGuiMod_Ctrl | ImGuiKey_PageDown, false,
         [this] {
             const auto found = std::find(tabs.begin(), tabs.end(), active);
             if (found != tabs.end())
                 openTree(*(found + 1 == tabs.end() ? tabs.begin() : found + 1));
         },
         onTree});
    add({"help.keys", "Keyboard shortcuts", "Help", ImGuiKey_F1, false, [this] { showShortcuts = !showShortcuts; },
         nullptr});
    add({"help.tour", "Getting started tour", "Help", 0, false,
         [this] {
             showTour = true;
             tourPage = 0;
         },
         nullptr});
    add({"view.previousTab", "Previous tree tab", "View", ImGuiMod_Ctrl | ImGuiKey_PageUp, false,
         [this] {
             const auto found = std::find(tabs.begin(), tabs.end(), active);
             if (found != tabs.end())
                 openTree(*(found == tabs.begin() ? tabs.end() - 1 : found - 1));
         },
         onTree});

    // Run: opening one (here and in the editor), then playing it (in its own session).
    const auto host = [](const std::function<void()> &f) {
        return [&f] {
            if (f)
                f();
        };
    };
    add({"run.open", "Open run (.btlog)…", "Run", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_O, false, host(browseRun),
         nullptr});
    add({"run.switch", "Switch between the editor and the run", "Run", ImGuiKey_F6, false, host(switchView), nullptr});
    if (!runView)
        return;
    auto &view = *runView;
    add({"run.close", "Close the run", "Run", 0, false, host(closeRun), nullptr});
    add({"run.play", "Play / pause", "Run", ImGuiKey_P, true,
         [this, &view] {
             if (!view.playing && view.state.applied >= view.run.transitions().size())
                 seekTransitions(*this, 0);
             view.playing = !view.playing;
         },
         nullptr});
    add({"run.follow", "Follow what runs", "Run", ImGuiKey_F, true, [&view] { view.follow = !view.follow; }, nullptr});
    add({"run.next", "Step to the next transition", "Run", ImGuiKey_Period, true,
         [this, &view] { seekTransitions(*this, view.state.applied + 1); }, nullptr});
    add({"run.previous", "Step back a transition", "Run", ImGuiKey_Comma, true,
         [this, &view] { seekTransitions(*this, view.state.applied ? view.state.applied - 1 : 0); }, nullptr});
    add({"run.nextFailure", "Next subtree failure", "Run", ImGuiKey_RightBracket, true,
         [this, &view] {
             const long at = view.run.nextFailure(view.state.applied);
             if (at < 0)
                 status("No failure after this", true);
             else
                 seekTransitions(*this, std::size_t(at) + 1);
         },
         nullptr});
    add({"run.previousFailure", "Previous subtree failure", "Run", ImGuiKey_LeftBracket, true,
         [this, &view] {
             const long at = view.run.previousFailure(view.state.applied);
             if (at < 0)
                 status("No failure before this", true);
             else
                 seekTransitions(*this, std::size_t(at) + 1);
         },
         nullptr});
    add({"run.into", "Go into the subtree", "Run", ImGuiKey_Enter, true, [this] { run("node.definition"); }, nullptr});
    add({"run.up", "Go up to the caller", "Run", ImGuiKey_Backspace, true,
         [this, &view] {
             const int parent = view.run.instances()[std::size_t(view.frame)].parent;
             if (parent >= 0)
                 focusFrame(*this, parent);
         },
         [&view] { return view.run.instances()[std::size_t(view.frame)].parent >= 0; }});
    add({"run.source", "Show the node in the editor", "Run", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Enter, true,
         [this] {
             if (showSource)
                 showSource(cursor.ref);
         },
         [this] { return !cursor.ref.empty(); }});
}
} // namespace notgroot::ui
