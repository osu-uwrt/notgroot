// Lossless XML read (tinyxml2) and the canonical writer.
#include "notgroot/core/xml.hpp"
#include <algorithm>
#include <tinyxml2.h>

namespace notgroot::core {
std::unique_ptr<Element> Element::comment(std::string text) {
    auto node = std::make_unique<Element>();
    node->kind = Kind::Comment;
    node->text = std::move(text);
    return node;
}

const std::string *Element::find(std::string_view name) const {
    for (const auto &attribute : attributes)
        if (attribute.name == name)
            return &attribute.value;
    return nullptr;
}

std::string Element::get(std::string_view name, std::string_view fallback) const {
    const auto *value = find(name);
    return value ? *value : std::string(fallback);
}

void Element::set(std::string_view name, std::string_view value) {
    for (auto &attribute : attributes)
        if (attribute.name == name) {
            attribute.value = value;
            return;
        }
    attributes.push_back({std::string(name), std::string(value)});
}

bool Element::erase(std::string_view name) {
    const auto found = std::find_if(attributes.begin(), attributes.end(), [&](auto &a) { return a.name == name; });
    if (found == attributes.end())
        return false;
    attributes.erase(found);
    return true;
}

bool Element::rename(std::string_view from, std::string_view to) {
    if (has(to))
        return false;
    for (auto &attribute : attributes)
        if (attribute.name == from) {
            attribute.name = to;
            return true;
        }
    return false;
}

std::vector<Element *> Element::elements() const {
    std::vector<Element *> list;
    for (const auto &child : children)
        if (!child->isComment())
            list.push_back(child.get());
    return list;
}

Element *Element::firstChild(std::string_view name) const {
    for (const auto &child : children)
        if (!child->isComment() && child->tag == name)
            return child.get();
    return nullptr;
}

Element *Element::insert(std::size_t at, std::unique_ptr<Element> child) {
    at = std::min(at, children.size());
    child->parent = this;
    children.insert(children.begin() + std::ptrdiff_t(at), std::move(child));
    return children[at].get();
}

std::unique_ptr<Element> Element::remove(std::size_t at) {
    if (at >= children.size())
        return nullptr;
    auto child = std::move(children[at]);
    children.erase(children.begin() + std::ptrdiff_t(at));
    child->parent = nullptr;
    return child;
}

std::unique_ptr<Element> Element::detach() {
    return parent ? parent->remove(index()) : nullptr;
}

std::size_t Element::index() const {
    if (!parent)
        return 0;
    for (std::size_t i = 0; i < parent->children.size(); ++i)
        if (parent->children[i].get() == this)
            return i;
    return 0;
}

std::unique_ptr<Element> Element::clone() const {
    auto copy = std::make_unique<Element>();
    copy->kind = kind;
    copy->tag = tag;
    copy->attributes = attributes;
    copy->text = text;
    copy->line = line;
    copy->commentsBefore = commentsBefore;
    copy->commentsAfter = commentsAfter;
    for (const auto &child : children)
        copy->append(child->clone());
    return copy;
}

namespace {
std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    return std::string(text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1));
}

std::unique_ptr<Element> convert(const tinyxml2::XMLElement &source) {
    auto element = std::make_unique<Element>(source.Name());
    element->line = source.GetLineNum();
    for (auto *a = source.FirstAttribute(); a; a = a->Next())
        element->attributes.push_back({a->Name(), a->Value()});
    std::string text;
    for (auto *node = source.FirstChild(); node; node = node->NextSibling()) {
        if (auto *child = node->ToElement())
            element->append(convert(*child));
        else if (auto *comment = node->ToComment()) {
            auto made = Element::comment(comment->Value());
            made->line = comment->GetLineNum();
            element->append(std::move(made));
        } else if (auto *content = node->ToText())
            text += content->Value();
    }
    element->text = trim(text);
    return element;
}
} // namespace

std::unique_ptr<Element> parseXml(const std::string &text, ParseError &error) {
    tinyxml2::XMLDocument document(true, tinyxml2::PRESERVE_WHITESPACE);
    if (document.Parse(text.c_str(), text.size()) != tinyxml2::XML_SUCCESS) {
        error.message = document.ErrorStr() ? document.ErrorStr() : "malformed XML";
        error.line = document.ErrorLineNum();
        return nullptr;
    }
    if (!document.RootElement()) {
        error.message = "no root element";
        return nullptr;
    }
    auto root = convert(*document.RootElement());
    bool before = true;
    for (auto *node = document.FirstChild(); node; node = node->NextSibling())
        if (node == document.RootElement())
            before = false;
        else if (auto *comment = node->ToComment())
            (before ? root->commentsBefore : root->commentsAfter).push_back(comment->Value());
    return root;
}

std::string escapeAttribute(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '&')
            out += "&amp;";
        else if (c == '<')
            out += "&lt;";
        else if (c == '>')
            out += "&gt;";
        else if (c == '"')
            out += "&quot;";
        else if (c == '\n')
            out += "&#10;";
        else
            out += c;
    }
    return out;
}

std::string escapeText(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '&')
            out += "&amp;";
        else if (c == '<')
            out += "&lt;";
        else if (c == '>')
            out += "&gt;";
        else
            out += c;
    }
    return out;
}

void writeElement(std::string &out, const Element &element, int depth) {
    const std::string indent(std::size_t(depth) * 4, ' ');
    if (element.isComment()) {
        out += indent + "<!--" + element.text + "-->\n";
        return;
    }
    out += indent + '<' + element.tag;
    for (const auto &attribute : element.attributes)
        out += ' ' + attribute.name + "=\"" + escapeAttribute(attribute.value) + '"';
    if (element.children.empty()) {
        if (element.text.empty())
            out += "/>\n";
        else
            out += '>' + escapeText(element.text) + "</" + element.tag + ">\n";
        return;
    }
    out += ">\n";
    if (!element.text.empty()) // mixed content: the text first, on its own line
        out += indent + "    " + escapeText(element.text) + '\n';
    for (const auto &child : element.children)
        writeElement(out, *child, depth + 1);
    out += indent + "</" + element.tag + ">\n";
}

std::string writeXml(const Element &root) {
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    for (const auto &comment : root.commentsBefore)
        out += "<!--" + comment + "-->\n";
    writeElement(out, root, 0);
    for (const auto &comment : root.commentsAfter)
        out += "<!--" + comment + "-->\n";
    return out;
}
} // namespace notgroot::core
