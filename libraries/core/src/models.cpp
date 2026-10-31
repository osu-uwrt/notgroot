// Node models and node elements.
#include "notgroot/core/models.hpp"
#include <algorithm>

namespace notgroot::core {
const char *categoryName(Category category) {
    switch (category) {
    case Category::Action:
        return "Action";
    case Category::Condition:
        return "Condition";
    case Category::Control:
        return "Control";
    case Category::Decorator:
        return "Decorator";
    case Category::SubTree:
        return "SubTree";
    case Category::Unknown:
        break;
    }
    return "Unknown";
}

Category categoryFromTag(std::string_view tag) {
    if (tag == "Action")
        return Category::Action;
    if (tag == "Condition")
        return Category::Condition;
    if (tag == "Control")
        return Category::Control;
    if (tag == "Decorator")
        return Category::Decorator;
    if (tag == "SubTree")
        return Category::SubTree;
    return Category::Unknown;
}

const char *directionTag(PortDirection direction) {
    switch (direction) {
    case PortDirection::Output:
        return "output_port";
    case PortDirection::InOut:
        return "inout_port";
    case PortDirection::Input:
        break;
    }
    return "input_port";
}

const PortModel *NodeModel::port(std::string_view name) const {
    for (const auto &p : ports)
        if (p.name == name)
            return &p;
    return nullptr;
}

NodeModel readModel(const Element &entry, bool builtin) {
    NodeModel model;
    model.id = entry.get("ID");
    model.category = categoryFromTag(entry.tag);
    model.builtin = builtin;
    for (const auto *child : entry.elements()) {
        PortModel port;
        if (child->tag == "input_port")
            port.direction = PortDirection::Input;
        else if (child->tag == "output_port")
            port.direction = PortDirection::Output;
        else if (child->tag == "inout_port")
            port.direction = PortDirection::InOut;
        else {
            if (child->tag == "description" || child->tag == "MetadataFields")
                model.description = child->text;
            continue;
        }
        port.name = child->get("name");
        port.type = child->get("type");
        port.defaultValue = child->get("default");
        port.description = child->text;
        model.ports.push_back(std::move(port));
    }
    // Ports that name a key bare (output_key="x"): BehaviorTree.CPP reads them with getInput, so "{x}" would mean
    // "the key whose name x holds".
    for (auto &port : model.ports)
        port.keyName |= ((model.id == "SetBlackboard" || model.id == "SetBlackboardV3") && port.name == "output_key") ||
                        (model.id == "UnsetBlackboard" && port.name == "key");
    return model;
}

std::unique_ptr<Element> modelElement(const NodeModel &model) {
    auto entry = std::make_unique<Element>(categoryName(model.category));
    entry->set("ID", model.id);
    auto ports = model.ports;
    std::stable_sort(ports.begin(), ports.end(), [](auto &a, auto &b) { return a.name < b.name; });
    for (const auto &port : ports) {
        auto *element = entry->append(std::make_unique<Element>(directionTag(port.direction)));
        element->set("name", port.name);
        if (!port.type.empty())
            element->set("type", port.type);
        if (!port.defaultValue.empty())
            element->set("default", port.defaultValue);
        element->text = port.description;
    }
    return entry;
}

void Catalog::clear() {
    models_.clear();
}

void Catalog::addTreeNodesModel(const Element &root, bool builtin) {
    const Element *model = root.tag == "TreeNodesModel" ? &root : root.firstChild("TreeNodesModel");
    if (!model)
        return;
    for (const auto *entry : model->elements())
        if (entry->has("ID") && !(builtin && entry->get("ID") == "SubTree"))
            add(readModel(*entry, builtin));
}

void Catalog::add(NodeModel model) {
    auto id = model.id;
    models_[id] = std::move(model);
}

const NodeModel *Catalog::find(std::string_view id) const {
    const auto found = models_.find(id);
    return found == models_.end() ? nullptr : &found->second;
}

std::vector<const NodeModel *> Catalog::all() const {
    std::vector<const NodeModel *> list;
    list.reserve(models_.size());
    for (const auto &[id, model] : models_)
        list.push_back(&model);
    return list;
}

std::string nodeId(const Element &node) {
    if (categoryFromTag(node.tag) != Category::Unknown)
        return node.get("ID");
    return node.tag;
}

bool isSubTree(const Element &node) {
    return node.tag == "SubTree";
}

bool isReservedAttribute(std::string_view name) {
    return name == "ID" || name == "name" || (!name.empty() && name[0] == '_');
}

Category nodeCategory(const Element &node, const Catalog &catalog) {
    if (isSubTree(node))
        return Category::SubTree;
    if (const auto *model = catalog.find(nodeId(node)))
        return model->category;
    return categoryFromTag(node.tag);
}

int maxChildren(Category category) {
    switch (category) {
    case Category::Control:
    case Category::Unknown:
        return -1;
    case Category::Decorator:
        return 1;
    default:
        return 0;
    }
}

int minChildren(Category category) {
    switch (category) {
    case Category::Control:
    case Category::Decorator:
        return 1;
    default:
        return 0;
    }
}

bool isKeyReference(std::string_view value) {
    return value.size() >= 2 && value.front() == '{' && value.back() == '}';
}

std::string keyOf(std::string_view value) {
    return isKeyReference(value) ? std::string(value.substr(1, value.size() - 2)) : std::string();
}

std::vector<std::string> keysMentioned(std::string_view value) {
    std::vector<std::string> keys;
    std::size_t at = 0;
    while ((at = value.find('{', at)) != std::string_view::npos) {
        const auto end = value.find('}', at + 1);
        if (end == std::string_view::npos)
            break;
        const auto key = value.substr(at + 1, end - at - 1);
        if (!key.empty() && key.find_first_of("{ ") == std::string_view::npos)
            keys.emplace_back(key);
        at = end + 1;
    }
    return keys;
}
} // namespace notgroot::core
