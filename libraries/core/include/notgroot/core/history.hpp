// Undo / redo by snapshots: a change records the documents it touched as they were before and after. Documents are
// small (one tree each), so whole copies are cheap and an undo can never be wrong about what changed.
#pragma once
#include "notgroot/core/workspace.hpp"
#include <memory>
#include <string>
#include <vector>

namespace notgroot::core {
class History {
  public:
    explicit History(Workspace &workspace) : workspace_(workspace) {}

    // Starts a change; touch() each document before modifying it, then commit() (or cancel() to roll back).
    // Nested begin/commit pairs join the outer change.
    void begin(std::string label);
    void touch(Document &);
    void commit();
    void cancel();
    bool active() const {
        return depth_ > 0;
    }

    bool canUndo() const {
        return !undo_.empty();
    }
    bool canRedo() const {
        return !redo_.empty();
    }
    std::string undoLabel() const;
    std::string redoLabel() const;
    bool undo();
    bool redo();
    void clear();

    // A change commits only if it altered some document; the views can ask what the last undo/redo touched.
    std::vector<Document *> lastTouched() const {
        return lastTouched_;
    }

  private:
    struct Snapshot {
        Document *document = nullptr;
        std::unique_ptr<Element> root;
        bool deleted = false;
    };
    struct Step {
        std::string label;
        std::vector<Snapshot> before, after;
    };
    static Snapshot capture(Document &);
    static void restore(const Snapshot &);
    bool apply(std::vector<Step> &from, std::vector<Step> &to, bool undoing);

    Workspace &workspace_;
    std::vector<Step> undo_, redo_;
    Step pending_;
    int depth_ = 0;
    std::vector<Document *> lastTouched_;
};

// Begins a change on construction and commits it on destruction (unless canceled).
class Change {
  public:
    Change(History &history, std::string label) : history_(history) {
        history_.begin(std::move(label));
    }
    ~Change() {
        if (open_)
            history_.commit();
    }
    Change(const Change &) = delete;
    Change &operator=(const Change &) = delete;
    void touch(Document &document) {
        history_.touch(document);
    }
    void cancel() {
        history_.cancel();
        open_ = false;
    }

  private:
    History &history_;
    bool open_ = true;
};
} // namespace notgroot::core
