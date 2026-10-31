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
  - Cards are colored by kind, as in btstudio: action green, condition blue, control orange, decorator purple,
    subtree cyan, unknown gray, tree root red. A subtree can have its own color.
  - Subtree calls have a dashed border.
  - Each port shows IN / OUT / IO, and values that are `{keys}` are drawn in that key's own color everywhere.
  - Space, or the round button under a call, opens a subtree in place, read-only; double-click or Ctrl+Enter opens
    it in its own tab.
  - Selection: Ctrl/Shift+click, Shift+drag for a box, and keys for siblings, a whole branch or every node of one
    type.
  - Right-click shows only what applies to that node; what can't run right now is greyed, with the reason.
- **Inspector.**
  - Edits a node's name and ports.
  - The `{}` button switches a value between text and a key.
  - Typing `{` suggests the tree's keys.
  - Also adds preconditions and scripts (`_skipIf`, `_onSuccess`…).
- **Variables.**
  - Lists every blackboard key of the open tree: reads, writes, whether it is one of the subtree's ports, and
    whether it is never set or never read.
  - Select a key to highlight it on the canvas and list its uses.
  - F2 renames it, at the subtree's call sites too when it is a port.
  - Declare inserts a `SetBlackboardV3` for a new key.
  - Constants (`SetBlackboardV3 output_key="5" value="5"`, how converted trees pass literals) are hidden unless you
    ask for them.
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
- **Refactors.** Rename a tree (its file follows), rename a key, edit a subtree's ports (calls follow), and extract
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
