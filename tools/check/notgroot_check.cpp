// notgroot-check: loads missions with BehaviorTree.CPP itself (the version doTask runs), every custom node a
// placeholder with the ports node_models.xml gives it, and reports what BT.CPP rejects.
//
//   notgroot-check TREES_DIR             every mission in the directory (models from TREES_DIR/lib/node_models.xml)
//   notgroot-check [--models FILE] FILE… the given mission files
//   notgroot-check --print-builtins      BT.CPP's own node models (to compare with content/builtin_models.xml)
//
// One line per mission on stdout: "ok<TAB>file<TAB>nodes" or "error<TAB>file<TAB>message". Exit 1 if any failed.
#include <behaviortree_cpp/bt_factory.h>
#include <behaviortree_cpp/xml_parsing.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <tinyxml2.h>
#include <vector>

namespace fs = std::filesystem;

namespace {
// Placeholders: they load like the real nodes (same ports, same child rules) and do nothing.
class Action : public BT::SyncActionNode {
  public:
    Action(const std::string &name, const BT::NodeConfig &config) : BT::SyncActionNode(name, config) {}
    BT::NodeStatus tick() override {
        return BT::NodeStatus::SUCCESS;
    }
};

class Condition : public BT::ConditionNode {
  public:
    Condition(const std::string &name, const BT::NodeConfig &config) : BT::ConditionNode(name, config) {}
    BT::NodeStatus tick() override {
        return BT::NodeStatus::SUCCESS;
    }
};

class Decorator : public BT::DecoratorNode {
  public:
    Decorator(const std::string &name, const BT::NodeConfig &config) : BT::DecoratorNode(name, config) {}
    BT::NodeStatus tick() override {
        return child()->executeTick();
    }
};

class Control : public BT::ControlNode {
  public:
    Control(const std::string &name, const BT::NodeConfig &config) : BT::ControlNode(name, config) {}
    BT::NodeStatus tick() override {
        return BT::NodeStatus::SUCCESS;
    }
};

// A model port with its type from node_models.xml (the plugins' types decide what a key may hold at load); no type
// or one this check doesn't know is untyped, as BehaviorTree.CPP's own InputPort(name).
template <typename T> std::pair<std::string, BT::PortInfo> typedPort(const std::string &kind, const std::string &name) {
    if (kind == "input_port")
        return BT::InputPort<T>(name);
    if (kind == "output_port")
        return BT::OutputPort<T>(name);
    return BT::BidirectionalPort<T>(name);
}

std::pair<std::string, BT::PortInfo> modelPort(const std::string &kind, const std::string &name,
                                               const std::string &type) {
    if (type == "std::string")
        return typedPort<std::string>(kind, name);
    if (type == "int")
        return typedPort<int>(kind, name);
    if (type == "double")
        return typedPort<double>(kind, name);
    if (type == "float")
        return typedPort<float>(kind, name);
    if (type == "bool")
        return typedPort<bool>(kind, name);
    if (type == "unsigned int")
        return typedPort<unsigned>(kind, name);
    return typedPort<BT::AnyTypeAllowed>(kind, name);
}

template <typename T> BT::NodeBuilder builder() {
    return [](const std::string &name, const BT::NodeConfig &config) { return std::make_unique<T>(name, config); };
}

// Registers every Action / Condition / Decorator / Control of a node_models.xml; false if it can't be read.
bool registerModels(BT::BehaviorTreeFactory &factory, const fs::path &file, std::string &error) {
    tinyxml2::XMLDocument document;
    if (document.LoadFile(file.c_str()) != tinyxml2::XML_SUCCESS) {
        error = "cannot read " + file.string();
        return false;
    }
    const auto *root = document.RootElement();
    const auto *model = root ? root->FirstChildElement("TreeNodesModel") : nullptr;
    for (auto *entry = model ? model->FirstChildElement() : nullptr; entry; entry = entry->NextSiblingElement()) {
        const std::string tag = entry->Name(), id = entry->Attribute("ID") ? entry->Attribute("ID") : "";
        if (id.empty() || tag == "SubTree" || factory.builders().count(id))
            continue;
        BT::PortsList ports;
        for (auto *port = entry->FirstChildElement(); port; port = port->NextSiblingElement()) {
            const std::string kind = port->Name(), name = port->Attribute("name") ? port->Attribute("name") : "";
            if (kind == "input_port" || kind == "output_port" || kind == "inout_port")
                ports.insert(modelPort(kind, name, port->Attribute("type") ? port->Attribute("type") : ""));
        }
        BT::TreeNodeManifest manifest;
        manifest.registration_ID = id;
        manifest.ports = ports;
        if (tag == "Action") {
            manifest.type = BT::NodeType::ACTION;
            factory.registerBuilder(manifest, builder<Action>());
        } else if (tag == "Condition") {
            manifest.type = BT::NodeType::CONDITION;
            factory.registerBuilder(manifest, builder<Condition>());
        } else if (tag == "Decorator") {
            manifest.type = BT::NodeType::DECORATOR;
            factory.registerBuilder(manifest, builder<Decorator>());
        } else if (tag == "Control") {
            manifest.type = BT::NodeType::CONTROL;
            factory.registerBuilder(manifest, builder<Control>());
        }
    }
    return true;
}
} // namespace

int main(int argc, char **argv) {
    fs::path models;
    std::vector<fs::path> missions;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--print-builtins") {
            BT::BehaviorTreeFactory factory;
            std::cout << BT::writeTreeNodesModelXML(factory, true);
            return 0;
        }
        if (arg == "--models" && i + 1 < argc) {
            models = argv[++i];
            continue;
        }
        if (arg == "-h" || arg == "--help") {
            std::cout << "usage: notgroot-check TREES_DIR | [--models FILE] FILE... | --print-builtins\n";
            return 0;
        }
        const fs::path path = fs::absolute(arg);
        if (fs::is_directory(path)) {
            if (models.empty())
                models = path / "lib" / "node_models.xml";
            std::vector<fs::path> found;
            for (const auto &entry : fs::directory_iterator(path))
                if (entry.is_regular_file() && entry.path().extension() == ".xml")
                    found.push_back(entry.path());
            std::sort(found.begin(), found.end());
            missions.insert(missions.end(), found.begin(), found.end());
        } else
            missions.push_back(path);
    }
    if (missions.empty()) {
        std::cerr << "notgroot-check: no missions given\n";
        return 2;
    }
    if (models.empty())
        models = missions.front().parent_path() / "lib" / "node_models.xml";

    BT::BehaviorTreeFactory factory;
    std::string error;
    if (fs::exists(models) && !registerModels(factory, models, error)) {
        std::cerr << "notgroot-check: " << error << '\n';
        return 2;
    }
    bool failed = false;
    for (const auto &mission : missions) {
        try {
            // As doTask loads a goal: no trees left from the last one, includes against the file's folder.
            factory.clearRegisteredBehaviorTrees();
            auto tree = factory.createTreeFromFile(mission);
            std::size_t nodes = 0;
            tree.applyVisitor([&](const BT::TreeNode *) { ++nodes; });
            std::cout << "ok\t" << mission.string() << '\t' << nodes << '\n';
        } catch (const std::exception &exception) {
            failed = true;
            std::string message = exception.what();
            for (auto &c : message)
                if (c == '\n' || c == '\t')
                    c = ' ';
            std::cout << "error\t" << mission.string() << '\t' << message << '\n';
        }
    }
    return failed ? 1 : 0;
}
