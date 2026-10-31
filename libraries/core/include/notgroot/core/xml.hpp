// XML as notgroot keeps it: elements with their attributes in file order, trimmed text and comments, written back in
// the canonical format of docs/contract.md (section 4), so a file that is opened and saved unchanged keeps its bytes.
#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace notgroot::core {
struct Attribute {
    std::string name, value;
};

// An element or a comment. Children own their nodes; `parent` points back up.
struct Element {
    enum class Kind { Element, Comment };
    Kind kind = Kind::Element;
    std::string tag;                   // empty for comments
    std::vector<Attribute> attributes; // in file order
    std::string text;                  // the element's trimmed text, or the comment's text as written
    std::vector<std::unique_ptr<Element>> children;
    Element *parent = nullptr;
    int line = 0; // line in the file it was read from (0: made in the editor)

    Element() = default;
    explicit Element(std::string tagName) : tag(std::move(tagName)) {}
    static std::unique_ptr<Element> comment(std::string text);

    bool isComment() const {
        return kind == Kind::Comment;
    }
    // The attribute's value, or null.
    const std::string *find(std::string_view name) const;
    std::string get(std::string_view name, std::string_view fallback = {}) const;
    bool has(std::string_view name) const {
        return find(name) != nullptr;
    }
    // Sets an attribute in place, or appends it.
    void set(std::string_view name, std::string_view value);
    bool erase(std::string_view name);
    // Renames an attribute in place (keeps its position); false if `from` is missing or `to` exists.
    bool rename(std::string_view from, std::string_view to);

    // Child elements only (no comments).
    std::vector<Element *> elements() const;
    Element *firstChild(std::string_view tag) const;
    // Inserts at `index` (clamped to the end) and returns the inserted node.
    Element *insert(std::size_t index, std::unique_ptr<Element> child);
    Element *append(std::unique_ptr<Element> child) {
        return insert(children.size(), std::move(child));
    }
    std::unique_ptr<Element> remove(std::size_t index);
    std::unique_ptr<Element> detach(); // out of its parent
    std::size_t index() const;         // position among the parent's children (comments included)
    std::unique_ptr<Element> clone() const;
};

struct ParseError {
    std::string message;
    int line = 0;
};

// Parses a document's root element; null and `error` on malformed XML. Whitespace-only text is dropped, other text is
// trimmed; comments keep their text exactly.
std::unique_ptr<Element> parseXml(const std::string &text, ParseError &error);

// The canonical form of a document with this root: declaration, 4-space indent, LF, trailing newline.
std::string writeXml(const Element &root);
// One element (and its subtree) at `depth`, as it appears inside a document.
void writeElement(std::string &out, const Element &element, int depth);

std::string escapeAttribute(std::string_view);
std::string escapeText(std::string_view);
} // namespace notgroot::core
