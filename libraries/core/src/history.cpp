// History: snapshot undo / redo.
#include "notgroot/core/history.hpp"
#include <algorithm>

namespace notgroot::core {
History::Snapshot History::capture(Document &document) {
    Snapshot snapshot;
    snapshot.document = &document;
    snapshot.root = document.root ? document.root->clone() : nullptr;
    snapshot.deleted = document.deleted;
    return snapshot;
}

void History::restore(const Snapshot &snapshot) {
    snapshot.document->root = snapshot.root ? snapshot.root->clone() : nullptr;
    snapshot.document->deleted = snapshot.deleted;
}

void History::begin(std::string label) {
    if (depth_++ == 0)
        pending_ = Step{std::move(label), {}, {}};
}

void History::touch(Document &document) {
    if (depth_ == 0)
        return;
    for (const auto &snapshot : pending_.before)
        if (snapshot.document == &document)
            return;
    pending_.before.push_back(capture(document));
}

void History::commit() {
    if (depth_ == 0 || --depth_ > 0)
        return;
    // Keep only documents that really changed; nothing changed, no step.
    Step step{pending_.label, {}, {}};
    for (auto &before : pending_.before) {
        auto after = capture(*before.document);
        const bool same =
            before.deleted == after.deleted &&
            (before.root && after.root ? writeXml(*before.root) == writeXml(*after.root) : !before.root && !after.root);
        if (same)
            continue;
        step.before.push_back(std::move(before));
        step.after.push_back(std::move(after));
    }
    pending_ = {};
    workspace_.reindex();
    if (step.before.empty())
        return;
    undo_.push_back(std::move(step));
    redo_.clear();
    constexpr std::size_t limit = 500;
    if (undo_.size() > limit)
        undo_.erase(undo_.begin());
}

void History::cancel() {
    if (depth_ == 0)
        return;
    depth_ = 0;
    for (const auto &snapshot : pending_.before)
        restore(snapshot);
    pending_ = {};
    workspace_.reindex();
}

std::string History::undoLabel() const {
    return undo_.empty() ? std::string() : undo_.back().label;
}

std::string History::redoLabel() const {
    return redo_.empty() ? std::string() : redo_.back().label;
}

bool History::apply(std::vector<Step> &from, std::vector<Step> &to, bool undoing) {
    if (from.empty() || depth_ > 0)
        return false;
    Step step = std::move(from.back());
    from.pop_back();
    lastTouched_.clear();
    for (const auto &snapshot : undoing ? step.before : step.after) {
        restore(snapshot);
        lastTouched_.push_back(snapshot.document);
    }
    to.push_back(std::move(step));
    workspace_.reindex();
    return true;
}

bool History::undo() {
    return apply(undo_, redo_, true);
}

bool History::redo() {
    return apply(redo_, undo_, false);
}

void History::clear() {
    undo_.clear();
    redo_.clear();
    pending_ = {};
    depth_ = 0;
}
} // namespace notgroot::core
