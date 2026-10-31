# notgroot ↔ riptide_autonomy file contract

Version 2. Both sides follow this document; a change is agreed by both, then the version here goes up and
riptide_autonomy's README summary is updated to match.

notgroot edits the XML that riptide_autonomy's `doTask` runs with BehaviorTree.CPP 4 (apt
`ros-humble-behaviortree-cpp`, 4.10). There is no other channel between them: the files below are the API.

## 1. Workspace layout

A workspace is a `trees/` directory:

```
trees/
    CompTree.xml            mission: one main tree plus the includes it needs
    ZeroTree.xml
    lib/
        Dive.xml            one subtree, ID == file stem
        GateTree.xml
        node_models.xml     node and subtree port models (no tree includes it)
```

- A **mission** is any `*.xml` directly in `trees/`. Root element:
  `<root BTCPP_format="4" main_tree_to_execute="ID">`, then its `<include>`s, then exactly one
  `<BehaviorTree>` whose ID is `main_tree_to_execute`.
- A **subtree file** `lib/<ID>.xml` has `<root BTCPP_format="4">`, its `<include>`s, then exactly one
  `<BehaviorTree ID="<ID>">`. The ID equals the file stem.
- `lib/node_models.xml` holds `<root BTCPP_format="4"><TreeNodesModel>…</TreeNodesModel></root>` (section 3).
- `doTask`'s `list_trees` lists missions only, never `lib/`.
- notgroot recognizes a workspace by `lib/node_models.xml`. It can also open a single file; that file's includes
  are followed, and nothing else is loaded.

## 2. Includes

- `<include path="…"/>` paths are relative to the including file's folder (`doTask` loads by absolute path).
- A file includes exactly the subtrees its own tree references directly (`<SubTree ID>`), in first-use
  order, and nothing it does not use. notgroot recomputes this list on save.
- Missions include `lib/<ID>.xml`; subtree files include siblings as `<ID>.xml`.
- The same file may be reached through several include paths (diamonds); BT.CPP must load each one once.

## 3. Node models (`lib/node_models.xml`)

| Entry | Owner | Source |
|---|---|---|
| `<Action>`, `<Condition>`, `<Decorator>`, `<Control>` | riptide_autonomy | `ros2 run riptide_autonomy2 updateNodeModels` from the plugins |
| `<SubTree>` | notgroot | the subtree's ports, edited in notgroot |

- `updateNodeModels` regenerates its entries and keeps every `<SubTree>` entry as it is; `--check` exits
  non-zero when the file is out of date.
- Entries are sorted by ID; ports within an entry are sorted by name.
- A port is `<input_port>`, `<output_port>` or `<inout_port>` with `name`, an optional `type`, and the description
  as element text.
- `<SubTree>` ports carry **no `default=`**. BT.CPP ≥ 4.6 applies subtree model defaults to missing remaps,
  which would change tree logic.
- Builtin nodes are not listed in this file. `updateNodeModels --builtins FILE` writes BT.CPP's own models
  (`writeTreeNodesModelXML(factory, true)` with the builtin entries only) in this format. notgroot ships that
  output as `content/builtin_models.xml`, regenerated when the BT.CPP version changes.

## 4. Canonical format

Both sides' writers produce exactly this, so a save that changes nothing changes no bytes:

- First line `<?xml version="1.0" encoding="UTF-8"?>`, LF line endings, a trailing newline.
- 4-space indent per depth. One element per line.
- Attributes in their stored order, `name="value"`, separated by one space; values escaped `&amp; &lt; &gt; &quot;`
  and newline as `&#10;`.
- An element without children or text closes itself (`<Foo a="1"/>`). An element with only text is on one line:
  `<input_port name="x">text</input_port>` (text trimmed, escaped `&amp; &lt; &gt;`).
- Comments are kept where they are. A comment's text is kept verbatim between `<!--` and `-->` (inner newlines,
  continuation-line indent and the spaces around the text included); its first line is at the element's indent.
- No blank lines.

notgroot's acceptance test round-trips every file of the workspace byte-for-byte.

## 5. Port values (BehaviorTree.CPP 4)

- A value that starts with `{` and ends with `}` is a blackboard key (`{x}`); anything else is a literal.
- A `<SubTree ID="X" port="{key}"/>` passes the parent's key; `port="text"` passes the text.
- Output ports take `{key}`.
- Converted trees pass v3's empty-key remap (`a=""`) as `{__v3_empty}`, a key that is never set; `{}` is not a key
  in BehaviorTree.CPP 4.
- Subtrees run on their own isolated blackboard: no `_autoremap`, no shared blackboard.
- The v3-behavior nodes `SetBlackboardV3`, `RepeatV3`, `RetryUntilSuccessfulV3` and `BlackboardCheckString` are
  ordinary custom nodes. Converted trees use them; new trees may use the builtins.

## 6. Editor metadata

notgroot stores nothing in the XML except comments directly before the element they describe, of the form
`<!-- notgroot:key=value -->` (e.g. `<!-- notgroot:color=#00bcd4 -->` before a subtree's `<BehaviorTree>`).
BT.CPP ignores comments; riptide_autonomy tools must keep them.

## 7. Validation

- riptide_autonomy is the ground truth: `TestTreesLoad` loads every mission with the real plugins, and
  `updateNodeModels --check` must pass.
- notgroot checks each save with BT.CPP 4.10 itself (built from source, without ROS): every model is registered
  as a placeholder node with its ports, and the saved missions are loaded with `createTreeFromFile`. A file that
  loads there loads in `doTask`, apart from what only the plugins can check.

## 8. Runtime (reserved, not used by notgroot v1)

- `doTask` parameters `enable_groot2` (bool, default `false`) and `groot2_port` (int, default `1667`) attach a
  `BT::Groot2Publisher` to each tree it runs, so a future live view can attach.
- Each goal logs its load time split into parse, instantiate and ROS init.
