// A workspace: a trees/ directory as docs/contract.md lays it out (missions, lib/<ID>.xml, lib/node_models.xml), or
// one loose file with what it includes. Owns the documents, the index of trees by ID and the node catalog.
#pragma once
#include "notgroot/core/models.hpp"
#include "notgroot/core/xml.hpp"
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace notgroot::core {
enum class DocumentRole { Mission, Subtree, Models, Loose };

struct Document {
    std::filesystem::path path; // absolute
    DocumentRole role = DocumentRole::Loose;
    std::unique_ptr<Element> root; // null when the file could not be read
    ParseError error;
    std::string savedText;                     // the file as last read or written ("" if it never existed)
    std::filesystem::file_time_type savedTime; // its modification time then
    bool onDisk = false;                       // the file existed when read / was written
    bool deleted = false;                      // removed in the editor: deleted from disk on save

    std::string text() const; // what saving would write
    bool dirty() const;
    // The document's <BehaviorTree> elements (missions and subtree files hold one).
    std::vector<Element *> trees() const;
};

// A tree in the index. Its key names it uniquely: the ID, except a mission's main tree (missions all call theirs
// "BehaviorTree"), which goes by its file name, e.g. "CompTree.xml".
struct TreeEntry {
    std::string key, id;
    Document *document = nullptr;
    Element *tree = nullptr; // the <BehaviorTree>
};

class Workspace {
  public:
    // The builtin models (content/builtin_models.xml); read before open().
    bool loadBuiltinModels(const std::filesystem::path &file, std::string &error);

    // Opens a trees directory (it has lib/node_models.xml), a file inside one (opens that workspace), or a loose file
    // and the files it includes. Replaces whatever was open.
    bool open(const std::filesystem::path &target, std::string &error);
    void close();
    bool isOpen() const {
        return !documents_.empty();
    }
    bool loose() const {
        return loose_;
    }
    const std::filesystem::path &directory() const {
        return directory_;
    }
    // The file open() was pointed at (a loose file, or a file inside a workspace), else empty.
    const std::filesystem::path &focus() const {
        return focus_;
    }

    const std::vector<std::unique_ptr<Document>> &documents() const {
        return documents_;
    }
    Document *document(const std::filesystem::path &) const;
    Document *models() const {
        return models_;
    }
    const Catalog &catalog() const {
        return catalog_;
    }

    // Trees by key (a duplicate ID keeps the first and is reported by validation).
    const std::map<std::string, TreeEntry> &trees() const {
        return trees_;
    }
    const TreeEntry *tree(std::string_view key) const;
    // The key of a <BehaviorTree> element in a document.
    std::string treeKey(const Document &, const Element &tree) const;
    std::vector<std::string> missions() const;     // keys of missions' main trees
    std::vector<std::string> subtrees() const;     // keys (IDs) of every other tree
    std::vector<std::string> duplicateIds() const; // IDs defined more than once
    // A mission document's main tree ID.
    static std::string mainTreeId(const Document &);

    // Rebuilds the tree index and the catalog; call after any change to the documents.
    void reindex();
    // Bumped by reindex(): views cache against it.
    unsigned version() const {
        return version_;
    }

    // --- changing the set of documents (through History to make them undoable) ---
    // A new lib/<ID>.xml with an empty tree (not on disk until saved).
    Document *createSubtreeDocument(const std::string &id);
    // A new mission <name>.xml whose main tree is `mainId`.
    Document *createMissionDocument(const std::string &name, const std::string &mainId);
    // Where a subtree file for `id` goes.
    std::filesystem::path subtreePath(const std::string &id) const;

    // Subtree port models: node_models.xml's SubTree entries (made if missing).
    NodeModel subtreeModel(const std::string &id) const;
    void setSubtreeModel(const NodeModel &); // writes the entry into node_models.xml, sorted by ID
    void removeSubtreeModel(const std::string &id);

    // The <include>s a document needs: the trees it references, in first use order, as paths relative to it.
    std::vector<std::string> requiredIncludes(const Document &) const;
    // Rewrites the document's <include> elements to requiredIncludes(); true if anything changed.
    bool updateIncludes(Document &);

    // Saves every dirty document (includes recomputed first); returns the files written or removed.
    std::vector<std::filesystem::path> save(std::vector<std::string> &errors);
    // Documents whose file changed on disk since it was read or saved.
    std::vector<Document *> changedOnDisk() const;
    // Rereads a document from disk (dropping its edits).
    void reload(Document &);
    // Takes the disk's current state as the baseline, keeping the edits (a deleted file counts as empty).
    void keepEdits(Document &);

  private:
    Document *load(const std::filesystem::path &, DocumentRole);
    Document *add(std::unique_ptr<Document>);
    void loadIncludes(Document &, int depth);

    std::vector<std::unique_ptr<Document>> documents_;
    Document *models_ = nullptr;
    std::unique_ptr<Element> builtins_;
    Catalog catalog_;
    std::map<std::string, TreeEntry> trees_;
    std::vector<std::string> duplicates_;
    std::filesystem::path directory_, focus_;
    bool loose_ = false;
    unsigned version_ = 0;
};

// Reads a file into a string; false if it cannot be read.
bool readFile(const std::filesystem::path &, std::string &text);
} // namespace notgroot::core
