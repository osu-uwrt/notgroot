# notgroot ↔ riptide_autonomy file contract

Version 7. Both sides follow this document; a change is agreed by both, then the version here goes up and
riptide_autonomy's README summary is updated to match.

notgroot edits the XML that riptide_autonomy's `doTask` runs with BehaviorTree.CPP 4 (apt
`ros-humble-behaviortree-cpp`, 4.10). The files below are the API; the only other channel is the read-only run
monitor (§8).

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
  which would change tree logic. notgroot flags one as a problem, and `updateNodeModels --check` fails on it.
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
- Comments are kept where they are, those before and after the root element included. A comment's text is kept verbatim between `<!--` and `-->` (inner newlines,
  continuation-line indent and the spaces around the text included); its first line is at the element's indent.
- No blank lines.

notgroot's acceptance test round-trips every file of the workspace byte-for-byte.

## 5. Port values (BehaviorTree.CPP 4)

- A value that starts with `{` and ends with `}` is a blackboard key (`{x}`), at least three characters and spaces
  around it ignored (BT.CPP's `isBlackboardPointer`); anything else is a literal.
- A `<SubTree ID="X" port="{key}"/>` passes the parent's key; `port="text"` passes the text.
- Output ports take `{key}`.
- A port that is not used is left out; `{}` is not a key in BehaviorTree.CPP 4.
- Subtrees run on their own isolated blackboard: no `_autoremap`, no shared blackboard.
- Numbers are written as literals, also when passed to a subtree (`<SubTree ID="Move" z="-1.5"/>`), never through a
  key set to its own value. Text passed to a subtree port is typed by what it looks like: `x="2"` makes the
  subtree's key `x` an `int`, `x="2.5"` a `double`. Only those two are typed (BT.CPP 4.10 reads the whole text as a
  64-bit integer, with no `.`, `e` or `E`, else as a double; no `+` and no spaces); `true`, `false` and anything else
  stay text. Every UWRT input port is untyped
  (`BT::AnyTypeAllowed` in `node_models.xml`) so it takes any of them; a typed port that reads `x` with another type
  fails the tree's load (*the port [x] was initially created with type [int]…*). notgroot warns about such values,
  naming the port, and `notgroot-check` registers each port with its `node_models.xml` type, so it reports the same
  failure.
- Named keys are set with `SetBlackboardV3` (`output_key` is the bare key name, `value` text or `{key}` to copy).
  BehaviorTree.CPP 4.10's own `SetBlackboard` leaves a key empty when an untyped port declared it, so the trees
  don't use it.
- Loops and checks use the builtins: `Repeat`, `RetryUntilSuccessful`, and `Precondition` (or `_skipIf` and the
  other script attributes) for comparisons. Script code names keys bare (`obj == 'table_spoon_pink'`); notgroot
  reads the keys a script names (assigned: written, otherwise read) for its variables panel and checks.
- **Globals** are keys a tree sets before anything in it runs. notgroot owns them, like the SubTree models: a tree
  with globals wraps its root in a `Sequence name="globals"` right after a `<!-- notgroot:globals -->` comment, whose
  first child is one `Script` setting them all, then the real root:

  ```xml
  <BehaviorTree ID="ShootBothObjectsMpc">
      <!-- notgroot:globals -->
      <Sequence name="globals">
          <Script code="ms_prox := '1.5'; object_1_frame := object_1 .. '_frame'"/>
          <Sequence name="Search both holes, then shoot both">…</Sequence>
      </Sequence>
  </BehaviorTree>
  ```

  - Each statement is `name := value`. Text is single-quoted, so it stays text, as `SetBlackboardV3` stores it.
    A value built from other keys (`{object_1}_frame` in the editor, as `Format` writes it) joins them with `..`.
    A lone key (`copy := depth`) copies it.
  - Statements come after the globals they use, otherwise by name; a set of globals that use each other in a
    circle is refused. A value may use the tree's inputs (its ports) and other globals, nothing set at runtime.
  - A global is the tree's own, like every key: subtrees still get it through their ports.
  - `Script` strings can't escape `'`, so a global's text can't hold one. A Script notgroot didn't write this way is
    an ordinary node.
  - `notgroot-globals <trees>` converts the setters each tree always runs first.

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

## 8. Runtime

- `doTask` parameters `enable_groot2` (bool, default `false`) and `groot2_port` (int, default `1667`) attach a
  `BT::Groot2Publisher` to each tree it runs, so a future live view can attach.
- Each goal logs its load time split into parse, instantiate and ROS init.
- Each goal writes a BehaviorTree.CPP `FileLogger2` log, `<log_file_dir>/BTLog_<date>_<time>.btlog` (default
  `~/btlogs`), which notgroot replays. Its header is the instantiated tree (one `<BehaviorTree>` per subtree
  instance, `_uid` on every node, `_fullpath` on instances and calls), so a log is read without the trees folder.
- `doTask` parameters `enable_tree_monitor` (bool, default `true`) and `tree_monitor_port` (int, default `1670`)
  serve a read-only **run monitor** on a ZeroMQ REP socket for the whole process. If the port is taken it only warns.
  It holds the latest run (the `.btlog`'s header and transitions) until the next one starts, so a client that
  connects late or after the end still gets all of it. Any number of clients; nothing they send changes the tree.
  Requests are multipart, first frame the command; replies start with `OK`, `NONE` or `ERR` (then the reason):
  - `RUN` → `NONE` before the first run, else `OK` and JSON `{protocol: 1, run, mission, log, start_usec,
    xml_bytes, transitions, ended, result}`. `run` is an id (text); `result` is `RUNNING` until the end, then
    `SUCCESS`, `FAILURE`, `CANCELED` or `ERROR`.
  - `TREE`, run → `OK` and the `.btlog` header XML, zlib-compressed (`xml_bytes` long uncompressed).
  - `TRANS`, run, from → `OK`, JSON `{from, to, count, ended, result}`, then records `from` to `to` (at most 65536)
    in the `.btlog` format: 9 bytes each, 6-byte µs offset from `start_usec`, `u16` uid, `u8` status, little
    endian. Ask again from `to`; the run is complete once `ended` and `to == count`.
  - `BB`, run, path... → `OK` and JSON `{path: {key: value}}` for each subtree instance `_fullpath` asked for (the
    main tree's is empty); `ERR run ended` once the tree is gone.
  - A run id that is not the latest gets `ERR unknown run`.

  Header and records match the `.btlog` except that a timestamp may differ from the file's by 1 µs: FileLogger2
  keeps its start time to the nanosecond and the file only to the microsecond.
