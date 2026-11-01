// Workspace: reading the trees directory, the tree index, includes, saving and external changes.
#include "notgroot/core/workspace.hpp"
#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace notgroot::core {
bool readFile(const fs::path &path, std::string &text) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    text = buffer.str();
    return true;
}

namespace {
// The canonical text of a document as read, so a document is dirty only when edited, not because its file was
// written by another tool in another format.
std::string canonical(const Document &document) {
    return document.root ? writeXml(*document.root) : document.savedText;
}

fs::file_time_type modified(const fs::path &path) {
    std::error_code error;
    const auto time = fs::last_write_time(path, error);
    return error ? fs::file_time_type() : time;
}

bool hasModels(const fs::path &directory) {
    std::error_code error;
    return fs::is_regular_file(directory / "lib" / "node_models.xml", error);
}

// The text a document had when it was read or saved, kept beside it (the dirty check compares against it).
std::map<const Document *, std::string> &cleanTexts() {
    static std::map<const Document *, std::string> texts;
    return texts;
}
} // namespace

std::string Document::text() const {
    return root ? writeXml(*root) : savedText;
}

bool Document::dirty() const {
    if (deleted)
        return onDisk;
    const auto clean = cleanTexts().find(this);
    return !onDisk || clean == cleanTexts().end() || text() != clean->second;
}

std::vector<Element *> Document::trees() const {
    std::vector<Element *> list;
    if (root)
        for (auto *child : root->elements())
            if (child->tag == "BehaviorTree")
                list.push_back(child);
    return list;
}

bool Workspace::loadBuiltinModels(const fs::path &file, std::string &error) {
    std::string text;
    if (!readFile(file, text)) {
        error = "cannot read " + file.string();
        return false;
    }
    ParseError parse;
    builtins_ = parseXml(text, parse);
    if (!builtins_) {
        error = file.string() + ":" + std::to_string(parse.line) + ": " + parse.message;
        return false;
    }
    reindex();
    return true;
}

void Workspace::close() {
    for (const auto &document : documents_)
        cleanTexts().erase(document.get());
    documents_.clear();
    models_ = nullptr;
    directory_.clear();
    focus_.clear();
    loose_ = false;
    reindex();
}

Document *Workspace::add(std::unique_ptr<Document> document) {
    documents_.push_back(std::move(document));
    return documents_.back().get();
}

Document *Workspace::load(const fs::path &path, DocumentRole role) {
    if (auto *existing = document(path))
        return existing;
    auto document = std::make_unique<Document>();
    document->path = path;
    document->role = role;
    document->onDisk = readFile(path, document->savedText);
    document->savedTime = modified(path);
    if (!document->onDisk)
        document->error = {"cannot read " + path.string(), 0};
    else
        document->root = parseXml(document->savedText, document->error);
    cleanTexts()[document.get()] = canonical(*document);
    return add(std::move(document));
}

// Follows <include path="…"/> from a document (paths relative to its folder), loading what is not open yet.
void Workspace::loadIncludes(Document &document, int depth) {
    if (!document.root || depth > 64)
        return;
    for (const auto *child : document.root->elements()) {
        if (child->tag != "include" || !child->has("path"))
            continue;
        const auto path = fs::weakly_canonical(document.path.parent_path() / child->get("path"));
        if (this->document(path))
            continue;
        auto *included = load(path, loose_ ? DocumentRole::Loose : DocumentRole::Subtree);
        loadIncludes(*included, depth + 1);
    }
}

bool Workspace::open(const fs::path &target, std::string &error) {
    std::error_code fsError;
    const auto path = fs::weakly_canonical(fs::absolute(target), fsError);
    if (fsError || !fs::exists(path)) {
        error = "no such file or directory: " + target.string();
        return false;
    }
    close();

    // The workspace directory: the target, its trees/ subfolder, or the folder holding the file (or its lib/).
    if (fs::is_directory(path)) {
        directory_ = hasModels(path / "trees") && !hasModels(path) ? path / "trees" : path;
    } else {
        focus_ = path;
        if (path.parent_path().filename() == "lib" && hasModels(path.parent_path().parent_path()))
            directory_ = path.parent_path().parent_path();
        else if (hasModels(path.parent_path()))
            directory_ = path.parent_path();
        else {
            loose_ = true;
            directory_ = path.parent_path();
        }
    }

    if (loose_) {
        loadIncludes(*load(path, DocumentRole::Mission), 0);
    } else {
        // Missions are the files directly in the directory; lib/ holds one subtree per file and the models.
        std::vector<fs::path> missions, subtrees;
        for (const auto &entry : fs::directory_iterator(directory_, fsError))
            if (entry.is_regular_file() && entry.path().extension() == ".xml")
                missions.push_back(entry.path());
        if (fs::is_directory(directory_ / "lib"))
            for (const auto &entry : fs::directory_iterator(directory_ / "lib", fsError))
                if (entry.is_regular_file() && entry.path().extension() == ".xml" &&
                    entry.path().filename() != "node_models.xml")
                    subtrees.push_back(entry.path());
        std::sort(missions.begin(), missions.end());
        std::sort(subtrees.begin(), subtrees.end());
        for (const auto &mission : missions)
            load(mission, DocumentRole::Mission);
        for (const auto &subtree : subtrees)
            load(subtree, DocumentRole::Subtree);
        if (hasModels(directory_))
            models_ = load(directory_ / "lib" / "node_models.xml", DocumentRole::Models);
        // Anything included from elsewhere.
        for (std::size_t i = 0; i < documents_.size(); ++i)
            loadIncludes(*documents_[i], 0);
    }
    reindex();
    return true;
}

Document *Workspace::document(const fs::path &path) const {
    for (const auto &document : documents_)
        if (document->path == path)
            return document.get();
    return nullptr;
}

const TreeEntry *Workspace::tree(std::string_view id) const {
    const auto found = trees_.find(std::string(id));
    return found == trees_.end() ? nullptr : &found->second;
}

std::string Workspace::mainTreeId(const Document &document) {
    if (!document.root)
        return {};
    if (document.root->has("main_tree_to_execute"))
        return document.root->get("main_tree_to_execute");
    const auto trees = document.trees();
    return trees.size() == 1 ? trees.front()->get("ID") : std::string();
}

std::string Workspace::treeKey(const Document &document, const Element &tree) const {
    if (document.role == DocumentRole::Mission && tree.get("ID") == mainTreeId(document))
        return document.path.filename().string();
    return tree.get("ID");
}

std::vector<std::string> Workspace::missions() const {
    std::vector<std::string> keys;
    for (const auto &[key, entry] : trees_)
        if (entry.document->role == DocumentRole::Mission && entry.id == mainTreeId(*entry.document))
            keys.push_back(key);
    return keys;
}

std::vector<std::string> Workspace::subtrees() const {
    std::vector<std::string> keys;
    for (const auto &[key, entry] : trees_)
        if (!(entry.document->role == DocumentRole::Mission && entry.id == mainTreeId(*entry.document)))
            keys.push_back(key);
    return keys;
}

std::vector<std::string> Workspace::duplicateIds() const {
    return duplicates_;
}

void Workspace::reindex() {
    trees_.clear();
    duplicates_.clear();
    for (const auto &document : documents_) {
        if (document->deleted)
            continue;
        for (auto *tree : document->trees()) {
            const auto key = treeKey(*document, *tree);
            if (!trees_.emplace(key, TreeEntry{key, tree->get("ID"), document.get(), tree}).second)
                duplicates_.push_back(key);
        }
    }

    catalog_.clear();
    if (builtins_)
        catalog_.addTreeNodesModel(*builtins_, true);
    if (models_ && !models_->deleted && models_->root)
        catalog_.addTreeNodesModel(*models_->root, false);
    for (const auto &[id, entry] : trees_)
        if (id == entry.id && !catalog_.find(id)) {
            NodeModel model;
            model.id = id;
            model.category = Category::SubTree;
            catalog_.add(std::move(model));
        }
    ++version_;
}

fs::path Workspace::subtreePath(const std::string &id) const {
    return loose_ ? directory_ / (id + ".xml") : directory_ / "lib" / (id + ".xml");
}

Document *Workspace::createSubtreeDocument(const std::string &id) {
    const auto path = subtreePath(id);
    if (auto *existing = document(path)) { // a deleted one comes back empty
        existing->deleted = false;
        existing->root = std::make_unique<Element>("root");
        existing->root->set("BTCPP_format", "4");
        existing->root->append(std::make_unique<Element>("BehaviorTree"))->set("ID", id);
        return existing;
    }
    auto document = std::make_unique<Document>();
    document->path = path;
    document->role = loose_ ? DocumentRole::Loose : DocumentRole::Subtree;
    document->root = std::make_unique<Element>("root");
    document->root->set("BTCPP_format", "4");
    document->root->append(std::make_unique<Element>("BehaviorTree"))->set("ID", id);
    return add(std::move(document));
}

Document *Workspace::createMissionDocument(const std::string &name, const std::string &mainId) {
    const auto path = directory_ / (name + ".xml");
    if (document(path) && !document(path)->deleted)
        return nullptr;
    auto document = std::make_unique<Document>();
    document->path = path;
    document->role = DocumentRole::Mission;
    document->root = std::make_unique<Element>("root");
    document->root->set("BTCPP_format", "4");
    document->root->set("main_tree_to_execute", mainId);
    document->root->append(std::make_unique<Element>("BehaviorTree"))->set("ID", mainId);
    if (auto *old = this->document(path)) { // a mission deleted earlier this session: reuse its slot
        old->deleted = false;
        old->root = std::move(document->root);
        return old;
    }
    return add(std::move(document));
}

NodeModel Workspace::subtreeModel(const std::string &id) const {
    if (const auto *model = catalog_.find(id); model && model->category == Category::SubTree)
        return *model;
    NodeModel model;
    model.id = id;
    model.category = Category::SubTree;
    return model;
}

void Workspace::setSubtreeModel(const NodeModel &model) {
    if (!models_) {
        auto document = std::make_unique<Document>();
        document->path = directory_ / "lib" / "node_models.xml";
        document->role = DocumentRole::Models;
        document->root = std::make_unique<Element>("root");
        document->root->set("BTCPP_format", "4");
        document->root->append(std::make_unique<Element>("TreeNodesModel"));
        models_ = add(std::move(document));
    }
    models_->deleted = false;
    auto *list = models_->root->firstChild("TreeNodesModel");
    if (!list)
        list = models_->root->append(std::make_unique<Element>("TreeNodesModel"));

    // Replace the entry in place, else insert it where its ID sorts.
    for (std::size_t i = 0; i < list->children.size(); ++i) {
        auto &child = list->children[i];
        if (child->isComment())
            continue;
        if (child->tag == "SubTree" && child->get("ID") == model.id) {
            list->remove(i);
            list->insert(i, modelElement(model));
            return;
        }
    }
    std::size_t at = list->children.size();
    for (std::size_t i = 0; i < list->children.size(); ++i)
        if (!list->children[i]->isComment() && list->children[i]->get("ID") > model.id) {
            at = i;
            break;
        }
    list->insert(at, modelElement(model));
}

void Workspace::removeSubtreeModel(const std::string &id) {
    if (!models_ || !models_->root)
        return;
    if (auto *list = models_->root->firstChild("TreeNodesModel"))
        for (std::size_t i = 0; i < list->children.size(); ++i)
            if (!list->children[i]->isComment() && list->children[i]->tag == "SubTree" &&
                list->children[i]->get("ID") == id) {
                list->remove(i);
                return;
            }
}

namespace {
void collectSubtrees(const Element &element, std::vector<std::string> &ids) {
    for (const auto *child : element.elements()) {
        if (isSubTree(*child)) {
            const auto id = child->get("ID");
            if (!id.empty() && std::find(ids.begin(), ids.end(), id) == ids.end())
                ids.push_back(id);
        }
        collectSubtrees(*child, ids);
    }
}
} // namespace

std::vector<std::string> Workspace::requiredIncludes(const Document &document) const {
    std::vector<std::string> ids, paths;
    std::set<std::string> own;
    for (const auto *tree : document.trees()) {
        own.insert(tree->get("ID"));
        collectSubtrees(*tree, ids);
    }
    for (const auto &id : ids) {
        if (own.count(id))
            continue;
        const auto *entry = tree(id);
        const auto target = entry ? entry->document->path : subtreePath(id);
        paths.push_back(fs::path(target).lexically_relative(document.path.parent_path()).generic_string());
    }
    return paths;
}

bool Workspace::updateIncludes(Document &document) {
    if (!document.root)
        return false;
    const auto wanted = requiredIncludes(document);
    std::vector<std::string> present;
    std::size_t first = document.root->children.size();
    for (std::size_t i = 0; i < document.root->children.size(); ++i) {
        const auto &child = document.root->children[i];
        if (!child->isComment() && child->tag == "include") {
            present.push_back(child->get("path"));
            first = std::min(first, i);
        }
    }
    if (present == wanted)
        return false;

    // New includes go where the old ones were, else before the first tree.
    if (present.empty())
        for (std::size_t i = 0; i < document.root->children.size(); ++i)
            if (document.root->children[i]->tag == "BehaviorTree") {
                first = i;
                break;
            }
    auto &children = document.root->children;
    for (std::size_t i = children.size(); i-- > 0;)
        if (!children[i]->isComment() && children[i]->tag == "include") {
            document.root->remove(i);
            if (i < first)
                --first;
        }
    for (std::size_t i = 0; i < wanted.size(); ++i) {
        auto include = std::make_unique<Element>("include");
        include->set("path", wanted[i]);
        document.root->insert(first + i, std::move(include));
    }
    return true;
}

std::vector<fs::path> Workspace::save(std::vector<std::string> &errors) {
    std::vector<fs::path> written;
    for (const auto &document : documents_)
        if (document->dirty() && !document->deleted && document->role != DocumentRole::Models)
            updateIncludes(*document);

    for (const auto &document : documents_) {
        if (!document->dirty())
            continue;
        std::error_code error;
        if (document->deleted) {
            fs::remove(document->path, error);
            if (error) {
                errors.push_back("cannot remove " + document->path.string() + ": " + error.message());
                continue;
            }
            document->onDisk = false;
            document->savedText.clear();
            written.push_back(document->path);
            continue;
        }
        if (!document->root)
            continue;

        // Write beside the file and rename over it, so a failed write never leaves half a tree.
        const auto text = document->text();
        fs::create_directories(document->path.parent_path(), error);
        const auto temporary = fs::path(document->path.string() + ".notgroot-save");
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out << text;
            if (!out) {
                errors.push_back("cannot write " + temporary.string());
                continue;
            }
        }
        fs::rename(temporary, document->path, error);
        if (error) {
            errors.push_back("cannot replace " + document->path.string() + ": " + error.message());
            fs::remove(temporary, error);
            continue;
        }
        document->savedText = text;
        document->savedTime = modified(document->path);
        document->onDisk = true;
        cleanTexts()[document.get()] = text;
        written.push_back(document->path);
    }
    reindex();
    return written;
}

std::vector<Document *> Workspace::changedOnDisk() const {
    std::vector<Document *> changed;
    for (const auto &document : documents_) {
        if (!document->onDisk)
            continue;
        if (modified(document->path) == document->savedTime)
            continue;
        std::string text;
        if (!readFile(document->path, text) || text != document->savedText)
            changed.push_back(document.get());
        else
            document->savedTime = modified(document->path); // touched, same bytes
    }
    return changed;
}

void Workspace::reload(Document &document) {
    document.error = {};
    document.deleted = false;
    document.onDisk = readFile(document.path, document.savedText);
    document.savedTime = modified(document.path);
    document.root = document.onDisk ? parseXml(document.savedText, document.error) : nullptr;
    if (!document.onDisk) {
        document.savedText.clear();
        document.error = {"cannot read " + document.path.string(), 0};
    }
    cleanTexts()[&document] = canonical(document);
    reindex();
}
} // namespace notgroot::core
