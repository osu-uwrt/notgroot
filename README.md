# notgroot

*Anything but Groot.* notgroot is a keyboard-first editor for [BehaviorTree.CPP 4](https://www.behaviortree.dev) XML,
built for UWRT's `riptide_autonomy` trees. It reads and writes the same files `doTask` runs, and it doesn't add
anything to them.

notgroot is built on Nereus's stack (C++17, Dear ImGui with docking, GLFW/OpenGL) and uses Nereus's themes and
fonts. It follows your Nereus theme, so the two look alike and notgroot can later become a Nereus panel. Its
features started from [btstudio](https://github.com/osu-uwrt/btstudio): btstudio's node colors and variable handling
carry over.

## Build and run

```bash
./build.sh                       # release build in build/release (dev, asan presets: ./build.sh dev)
build/release/app/notgroot ~/osu-uwrt/development/software/src/riptide_autonomy/trees
```

- **Dependencies:** `cmake g++ libglfw3-dev libglew-dev libyaml-cpp-dev libfontconfig-dev libpng-dev libtinyxml2-dev
  libgtest-dev`, plus `zenity` (or kdialog) for File > Open.
- **BehaviorTree.CPP:** the first configure downloads BehaviorTree.CPP 4.10.0 (the version the robot runs) and builds
  it without ROS, for the check below.
- **What opens:** notgroot opens a trees folder, a file inside one, or a single loose tree file. Without an argument,
  it reopens the last folder.
- **Tests:** `ctest --preset dev` (after `./build.sh dev`). The tests include a round trip of the real trees when
  `NOTGROOT_TREES_DIR` is set.

## What it edits

notgroot works on the `riptide_autonomy` trees layout, written down in [docs/contract.md](docs/contract.md):

- Missions are the files directly in `trees/`.
- Every subtree is one file, `trees/lib/<ID>.xml`, which `<include>`s the subtrees it calls.
- Node ports live in `trees/lib/node_models.xml`. `ros2 run riptide_autonomy2 updateNodeModels` regenerates the
  Action/Condition/Decorator entries; notgroot edits the SubTree port entries.

Files are written in one canonical format. Opening and saving a file without changing it leaves its bytes alone, and
saving writes only the files you changed. Includes are recomputed on save, so you never edit them by hand.

## The editor

- **Canvas.** Layout is automatic, and child order on screen is the order in the file. Dragging a node (or the
  selection) moves it in the tree: beside another node, or into it.
  - Cards are plain, with a bar down the left for their kind: action slate, condition teal, control orange,
    decorator purple, subtree taupe. A subtree can have its own color.
  - Nodes that steer the flow have Groot's color and icon on their title: Sequence pink, Fallback blue (reactive
    ones deeper), Force/AlwaysSuccess green, Force/AlwaysFailure red, other decorators (Inverter, Repeat, Retry,
    Timeout) yellow, conditions green.
  - Subtree calls have a dashed border.
  - Each port shows IN / OUT / IO, and values that are `{keys}` are drawn in that key's own color everywhere.
  - Space, or the round button under a call, opens a subtree in place, grayed on a band of its color because it is
    read-only there; calls inside it open the same way. Double-click or Ctrl+Enter opens it in its own tab.
  - Selection: Ctrl/Shift+click, Shift+drag for a box, and keys for siblings, a whole branch or every node of one
    type.
  - Right-click shows only what applies to that node; what can't run right now is grayed, with the reason.
- **Inspector.**
  - Edits a node's name and ports.
  - The `{}` button switches a value between text and a key.
  - Typing `{` suggests the tree's keys.
  - Also adds preconditions and scripts (`_skipIf`, `_onSuccess`…).
- **Variables.** The open tree's blackboard keys, in three sections:
  - **Inputs:** the subtree's ports, shown on its root card too.
  - **Globals:** set before the tree runs, usable anywhere in it, with no nodes on the canvas (one hidden `Script`,
    contract section 5). Edit them in place; a value can use other globals and inputs (`{object}_frame`), and
    notgroot writes them in the order that needs, refusing globals that use each other in a circle (shown in red).
    Right-click a `SetBlackboardV3` or `Format` and choose Make global to turn it into one.
  - **Locals:** every key bound while the tree runs (a node's `{key}`, a setter, a script), with reads and writes,
    and whether it is never set or never read.
  - Select a key to highlight it on the canvas and list its uses.
  - F2 renames it, at the subtree's call sites too when it is a port.
  - Keys named in script code (`Precondition if`, `_skipIf` and the other script attributes, `Script`) count as
    reads, or as writes when assigned.
- **Trees panel and palette.** Missions and subtrees, and every node model, each with a fuzzy filter. Drag a model
  onto a card, or double-click it, to add it.
- **Problems.** notgroot checks as you edit, for:
  - unknown nodes and ports, and wrong child counts;
  - SubTree calls to trees that don't exist, and subtrees that call each other in a loop;
  - outputs that store nothing;
  - keys read but never set;
  - text passed to a subtree that BehaviorTree.CPP would type as a number.

  On every save (and on F7), every mission is loaded by BehaviorTree.CPP 4.10 itself, through `notgroot-check`, and
  anything it rejects shows up here. You can also run `build/release/tools/check/notgroot-check trees/` from a shell
  or CI.
- **Moving by keyboard.** Alt+arrows always move a node. Where its new parent can't hold it, it floats there, drawn
  dotted, until you move it on; nothing is saved while a node floats.
- **Refactors. Rename a tree (its file follows), rename a key, edit a subtree's ports (calls follow), and extract
  the selection into a new subtree (ports inferred from the keys it uses). Each refactor is one undo step, however
  many files it touches.
- **Disk.** A file changed on disk reloads by itself. If it also has unsaved edits, notgroot asks which version to
  keep.

[docs/keys.md](docs/keys.md) lists every key and mouse action; F1 shows the same list in the editor. The status bar shows
the main ones, and Ctrl+Shift+P lists every command.

## Layout of the code

| Path | What |
|---|---|
| `libraries/core` | no UI: lossless XML and the canonical writer, node models, the workspace, undo, edits and refactors, variables, validation, layout, fuzzy search, the BehaviorTree.CPP check runner |
| `libraries/ui` | the panels (ImGui): canvas, inspector, variables, problems, finder, picker, dialogs; the theme loader is Nereus's |
| `app` | the window, menus, dock layout and preferences (`~/.config/notgroot/notgroot.yaml`) |
| `tools/check` | `notgroot-check` (BehaviorTree.CPP 4.10 with placeholder nodes) |
| `tests` | GTest suites, including headless ImGui frames driven by key presses |

The theme follows Nereus's `~/.config/nereus/viewer.yaml`, else `timing-board`. View > Theme picks another one, or
"Follow Nereus" again.
