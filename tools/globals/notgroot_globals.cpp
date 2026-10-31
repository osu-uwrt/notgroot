// notgroot-globals <trees directory> [-v]: turns the setters each tree runs first into globals, the batch form of
// Make global. Only SetBlackboardV3 and Format nodes that run whenever the tree does (Sequences above them), set
// their key alone, read nothing set at runtime and aren't read before them. Saves the trees it changed and prints
// one line per global made (-v: also why the others stayed).
#include "notgroot/core/edit.hpp"
#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>

using namespace notgroot::core;

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: notgroot-globals <trees directory> [-v]\n";
        return 2;
    }
    const bool verbose = argc > 2 && std::strcmp(argv[2], "-v") == 0;
    Workspace workspace;
    History history{workspace};
    Editor editor{workspace, history};
    std::string error;
    if (!workspace.loadBuiltinModels(NOTGROOT_BUILTIN_MODELS, error) || !workspace.open(argv[1], error)) {
        std::cerr << "notgroot-globals: " << error << '\n';
        return 1;
    }

    std::vector<std::string> keys;
    for (const auto &[key, entry] : workspace.trees())
        keys.push_back(key);
    int made = 0;
    for (const auto &key : keys) {
        // Setters in document order; each success moves refs, so start over until none is left to try.
        std::vector<std::string> refused;
        for (bool again = true; again;) {
            again = false;
            std::vector<NodeRef> setters;
            const std::function<void(const Element &, const NodeRef &)> visit = [&](const Element &element,
                                                                                    const NodeRef &ref) {
                for (std::size_t i = 0; i < element.children.size(); ++i) {
                    const auto &child = *element.children[i];
                    if (child.isComment())
                        continue;
                    const auto id = nodeId(child);
                    if (id == "SetBlackboardV3" || id == "Format")
                        setters.push_back(ref.child(i));
                    visit(child, ref.child(i));
                }
            };
            visit(*workspace.tree(key)->tree, {key, {}});
            for (const auto &ref : setters) {
                const auto *element = resolve(workspace, ref);
                const auto what = nodeId(*element) + " " + element->get("output_key", element->get("out"));
                if (editor.makeGlobal(ref, true)) {
                    std::cout << key << ": " << what << '\n';
                    ++made;
                    again = true;
                    break;
                }
                if (verbose && std::find(refused.begin(), refused.end(), what) == refused.end()) {
                    refused.push_back(what);
                    std::cout << key << ": kept " << what << " (" << editor.error() << ")\n";
                }
            }
        }
    }

    std::vector<std::string> errors;
    const auto written = workspace.save(errors);
    for (const auto &message : errors)
        std::cerr << "notgroot-globals: " << message << '\n';
    std::cout << made << " globals in " << written.size() << " files\n";
    return errors.empty() ? 0 : 1;
}
