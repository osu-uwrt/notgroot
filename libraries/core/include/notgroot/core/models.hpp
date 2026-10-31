// Node models (what a node is and which ports it has) and how BehaviorTree.CPP 4 reads a node element: its ID, its
// category, its ports and its special attributes.
#pragma once
#include "notgroot/core/xml.hpp"
#include <map>
#include <string>
#include <vector>

namespace notgroot::core {
enum class Category { Action, Condition, Control, Decorator, SubTree, Unknown };
enum class PortDirection { Input, Output, InOut };

const char *categoryName(Category);
Category categoryFromTag(std::string_view tag); // Action, Condition, Control, Decorator, SubTree; else Unknown
const char *directionTag(PortDirection);        // input_port, output_port, inout_port

struct PortModel {
    std::string name, type, description, defaultValue;
    PortDirection direction = PortDirection::Input;
    bool keyName = false; // takes a bare key name, not {key} (SetBlackboardV3's output_key)
};

struct NodeModel {
    std::string id;
    Category category = Category::Unknown;
    std::vector<PortModel> ports;
    std::string description;
    bool builtin = false;

    const PortModel *port(std::string_view name) const;
};

// Every model the editor knows: BehaviorTree.CPP's builtins and node_models.xml (custom nodes and subtree ports).
class Catalog {
  public:
    void clear();
    // Adds the entries of a <root><TreeNodesModel> document; later entries replace earlier ones with the same ID.
    void addTreeNodesModel(const Element &root, bool builtin);
    void add(NodeModel);
    const NodeModel *find(std::string_view id) const;
    std::vector<const NodeModel *> all() const; // by ID
    std::size_t size() const {
        return models_.size();
    }

  private:
    std::map<std::string, NodeModel, std::less<>> models_;
};

// Reads one TreeNodesModel entry (<Action ID=…> with its ports).
NodeModel readModel(const Element &entry, bool builtin);
// Writes a model entry the way node_models.xml holds it (ports by name).
std::unique_ptr<Element> modelElement(const NodeModel &);

// --- a node element inside a <BehaviorTree> ---

// The node's ID: the ID attribute of the explicit forms (<Action ID="X">, <SubTree ID="X">), else the tag.
std::string nodeId(const Element &node);
bool isSubTree(const Element &node);
// Attributes that are not ports: ID, name, and BehaviorTree.CPP's own "_" attributes (preconditions, scripts).
bool isReservedAttribute(std::string_view name);
// The node's category: its model's, else the explicit tag's, else Unknown.
Category nodeCategory(const Element &node, const Catalog &);
// How many children a node of this category takes: leaves none, decorators and subtrees one... (-1: any number).
int maxChildren(Category);
int minChildren(Category);

// Port values: "{key}" is one blackboard key; anything else is text.
bool isKeyReference(std::string_view value);
std::string keyOf(std::string_view value); // "{key}" -> "key", else ""
// Every "{key}" inside a value that is text (Format strings, messages): the keys it mentions.
std::vector<std::string> keysMentioned(std::string_view value);
} // namespace notgroot::core
