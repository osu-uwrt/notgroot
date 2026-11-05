# Moving from Groot to notgroot

For people who know Groot (1) and BehaviorTree.CPP 3 and now edit `riptide_autonomy`'s trees in notgroot. It
covers what changed in the trees (BehaviorTree.CPP 4), what notgroot does differently from Groot, and the
mistakes that are easy to make on the way. The file rules themselves are in [contract.md](contract.md); every key
is in [keys.md](keys.md) (F1 in the editor).

## The short version

- The trees are BehaviorTree.CPP **4** files now. Groot 1 can't open them; notgroot (or Groot2) can.
- Every subtree lives once, in `trees/lib/<ID>.xml`. Missions `<include>` what they use. notgroot keeps the
  includes right for you.
- A port value is a **key** only as `{key}`. Anything else is literal text, including in a `SubTree` call. This is
  the change that bites most often (see [Ports and keys](#ports-and-keys)).
- Subtrees have their **own blackboard**. They see only what their call passes in through their ports.
- Layout is automatic. You never place nodes; you move them in the tree (drag, or Alt+arrows).
- Keys come in three kinds in the Variables panel: **Inputs** (the subtree's ports), **Globals** (set before the
  tree runs, no nodes on the canvas) and **Locals** (set while it runs).
- Saving runs BehaviorTree.CPP 4.10 itself on every mission. Its errors show in Problems before you ever start
  `doTask`.

## What's where

| In Groot | In notgroot |
|---|---|
| Open one XML file, with its subtrees and `TreeNodesModel` inside it | Open the `trees/` folder: every mission and subtree at once, each in a tab when you open it |
| Node models embedded in each file, or loaded from a palette file | One `trees/lib/node_models.xml`. `updateNodeModels` writes the action/condition/decorator entries from the plugins; notgroot writes the subtree entries |
| Copies of each subtree in every file that used it | One file per subtree. Changing it changes it for every mission |
| Drag nodes anywhere, then Arrange | Automatic layout. Child order on screen is the order in the file |
| Connect nodes by drawing edges | Add with Tab / Shift+Tab (child / sibling), drag from the palette, or paste. Move by dragging onto a node, or with Alt+arrows |
| Edit ports in the node box | Double-click a port's row to edit it on the card (a node's title names it), or use the Inspector (Enter on a node). The `{}` button switches a value between text and key |
| Expand a subtree in place | Space, or the round button under the call. It starts locked (grayed); the padlock on the call's corner or Ctrl+U unlocks it to edit there, globals included. Ctrl+Enter opens it in its own tab |
| Save rewrites the file from Groot's model | Saves only the files you changed, in one canonical format. Comments and attribute order are kept, and an unchanged file keeps its bytes |
| Monitor and log replay modes | Not in notgroot. `doTask` can publish to Groot2 (below) |

notgroot writes nothing into the XML of its own except `<!-- notgroot:… -->` comments (a subtree's color, the
globals marker). BehaviorTree.CPP ignores them; keep them when you hand-edit.

## BehaviorTree.CPP 4 for v3 users

### Ports and keys

- **`{key}` is a key; anything else is text.** In v3, a `SubTree` call remapped by key *name*:
  `<SubTree ID="Move" x="target_x"/>` connected the subtree's `x` to the parent's `target_x`. In v4 the same line
  passes the text `target_x`. Write `x="{target_x}"`.
- A value is a key only when the *whole* value is `{name}` (at least three characters, spaces around it ignored).
  `{{name}}` is a key literally called `{name}`, a typo that once kept `RandomPingerTree`'s `pinger_is_table` from
  ever arriving.
- Text with braces inside it (`"{object}_frame"`) is plain text to most nodes. Only nodes that format text
  (`Format`, `Info`, `Error`) fill keys into it.
- `{}` is not a key in v4. Leave a port out to mean "not used".
- **A port you leave out gets its C++ default**, if the node has one. v3 code that relied on an empty key making
  `getInput` fail now gets the default value instead.
- BT4 has a `{=}` shorthand (a key with the port's own name). notgroot doesn't expand it, and the trees don't use
  it: write the key name out.

### Subtrees

- A v4 `SubTree` is v3's `SubTreePlus`: an **isolated blackboard**, with ports passed by `{key}` or as text. There
  is no shared blackboard, and the trees don't use `_autoremap`. If a subtree needs a value, it's one of its ports.
- Its ports are declared in `node_models.xml` (Tree > Edit subtree ports, Ctrl+Shift+E). Renaming a port there
  renames it at every call.
- **Text passed into a subtree gets a type from how it looks.** `x="2"` arrives as an `int`, `x="2.5"` as a
  `double`; `true`, `false`, `+5` or ` 5` stay text. (`inf` and `nan` count as doubles, as in BT.CPP 4.10.) A port
  inside that reads `x` with a different fixed type fails the whole tree's load: *the port [x] was initially
  created with type [int]…*. Every UWRT input port is untyped (`BT::AnyTypeAllowed`), so literals into them are
  fine. Builtin ports with fixed types are where it can still happen (an `int` port reading a key that arrived as
  `2.5`). notgroot warns about it on the call, naming the port.
- Subtree port defaults (`default=` on a `<SubTree>` port in `node_models.xml`) are not allowed. BT.CPP ≥ 4.6
  applies them to calls that leave the port out, silently. notgroot flags one, and so does
  `updateNodeModels --check`.

### Setting keys

- **Use `SetBlackboardV3`, not BT4's `SetBlackboard`**, for keys set while the tree runs. BT.CPP 4.10's
  `SetBlackboard` leaves the key empty when an untyped port declared it, which every UWRT port is. `SetBlackboardV3`
  stores text as v3 did, or copies a key's value (`value="{other}"`). Its `output_key` is the bare key name, no
  braces.
- For keys that never change while the tree runs, use a **global** (below) instead of a setter.
- `Format` builds text from keys: `format="{object_name}_frame" out="{object_frame}"`.

### Scripts

BT4 has a small script language, used by `Script`, `ScriptCondition`, `Precondition` and the attributes below.

- Keys are named bare in scripts: `depth > 0`, not `{depth} > 0`.
- Text is single-quoted (`'buoy'`). There is no escape, so text can't hold a `'`.
- `:=` creates or sets a key; `=` only sets one that already exists, and throws if it doesn't. `+=`, `-=`, `*=` and
  `/=` work too.
- `..` joins text (`object .. '_frame'`). `==`, `!=`, `<`, `<=`, `>`, `>=`, `&&`, `||`, `!` and `? :` work as
  expected.
- **A key the script reads that was never set throws** and stops the tree. `Precondition` replaced v3's
  `BlackboardCheckString`, which failed quietly instead. The trees always set the keys first; keep it that way.
- Any node can take these attributes:

  | Attribute | Runs | Effect |
  |---|---|---|
  | `_skipIf`, `_successIf`, `_failureIf` | before the node starts | true: return SKIPPED / SUCCESS / FAILURE without running the node |
  | `_while` | before and while it runs | false: halt it and return SKIPPED |
  | `_onSuccess`, `_onFailure`, `_onHalted`, `_post` | after it finishes | run the script |

  notgroot's Inspector adds them ("+ precondition / script"). They show on the card as `IF` rows, and the keys in
  them count in the Variables panel.

### Statuses and timing

- **SKIPPED** is new. A skipped child is passed over: a Sequence moves to the next child, a Fallback too, and a
  parent whose children were all skipped is itself SKIPPED.
- **`Repeat` and `RetryUntilSuccessful`** return RUNNING between rounds when the child finishes in the same tick
  it started. That costs one tick per round (about 30 ms in `doTask`), and reactive parents re-check in between.
  The v3 copies looped within one tick.
- Reactive controls (`ReactiveSequence`, `ReactiveFallback`) re-tick their earlier children every tick, as in v3.

### Renamed and removed nodes

| v3 | Now |
|---|---|
| `SequenceStar` | `SequenceWithMemory` |
| `BlackboardCheckString` | `Precondition if="a == 'b'" else="FAILURE"` (or `_skipIf`/`_failureIf` on the child) |
| `Repeat` / `RetryUntilSuccessful` (v3 behavior, `RepeatV3` etc. during the port) | BT4's own `Repeat` / `RetryUntilSuccessful` |
| `SetBlackboard` | `SetBlackboardV3` (see above), or a global |
| `SubTreePlus` | `SubTree` |
| constants (`SetBlackboardV3 output_key="0" value="0"`, then `{0}`) | the literal `0` |
| `{__v3_empty}` (v3's empty remap) | leave the port out |

New in v4 and worth knowing: `Precondition`, `Script`, `ScriptCondition`, `Sleep`, `RunOnce`, `ParallelAll`,
`UnsetBlackboard`, and the `WasEntryUpdated` / `SkipUnlessUpdated` / `WaitValueUpdate` family.

## Keys in notgroot: Inputs, Globals, Locals

The Variables panel splits the open tree's keys three ways:

- **Inputs** are the subtree's ports, also shown on its root card. Edit them with Edit ports.
- **Globals** are set before anything in the tree runs and can be used anywhere in it. They have no nodes on the
  canvas; the root card says how many there are.
  - Write a value as text, or build it from inputs and other globals as `Format` would: `{object_name}_frame`.
    After a `{`, the rest of the nearest key shows in gray; Tab takes it.
  - notgroot writes them in the order they need and refuses globals that use each other in a circle (shown in
    red).
  - Right-click a `SetBlackboardV3` or `Format` and choose **Make global** to turn it into one. It refuses when
    the key is set anywhere else, read before that node, or the node has `_` script attributes. It doesn't check
    that the node always runs: a setter under a Fallback only ran when that branch did, and as a global it always
    is set. Look before you promote one.
  - In the file they are one `Script` at the start of the tree, in a `Sequence name="globals"` after a
    `<!-- notgroot:globals -->` comment. Don't hand-edit that Script: if notgroot can't read it back, it becomes an
    ordinary node.
- **Locals** are every key bound while the tree runs: a node's `{key}` output, a setter, a script. Output ports
  you leave unbound don't appear.

A global is still the tree's own key: a subtree it calls gets it only through a port.

Each key keeps one color on every card and in every panel. A tree's keys are colored together, so no two look
alike (up to about 20 keys in every theme; the biggest trees have around 47, where some get close).

## Things notgroot does that Groot didn't

- **Checks.** As you edit: unknown nodes and ports, child counts, missing subtrees and subtree loops, outputs that
  store nothing, keys read but never set, literals a subtree would mistype. On every save, and on F7, every
  mission is loaded by BehaviorTree.CPP 4.10 itself (`notgroot-check`). F8 walks the problems.
- **Refactors, one undo step each, across files.** Rename a key (F2; a port's calls and the globals follow),
  rename a tree (its file follows), edit a subtree's ports, extract a selection into a new subtree (Ctrl+E; ports
  come from the keys it uses), wrap, unwrap, change a node's type.
- **Find.** Ctrl+P finds any tree, any node in any tree (by type, name or a port value) or any key; prefixes
  narrow it: `>` commands, `@` nodes here, `$` keys here, `#` trees.
- **Keyboard everything**, with a guide on F1. Alt+arrows always move a node. Where its new parent can't hold it, it
  *floats*, drawn dotted, until you move it on. Nothing saves while a node floats.
- **Files changed on disk** reload by themselves; if you also have edits there, notgroot asks which to keep.

## Common pitfalls

1. **A key without braces.** `x="target"` is the text `target`, not the key. Use `{target}`. Watch for this in
   anything pasted from a v3 tree.
2. **Double braces.** `{{key}}` is a different key (one called `{key}`). notgroot warns about it in Problems, and
   riptide's tests fail on one in the trees.
3. **A number into a typed port.** `x="2"` is an `int` in the subtree; a fixed `double` port reading it fails the
   load. Pass `2.0`, or route it through an untyped port.
4. **`true` and `false` are text** when passed into a subtree, not bools.
5. **Builtin `SetBlackboard`.** It can leave the key empty. Use `SetBlackboardV3` or a global.
6. **`=` instead of `:=`** in a script throws when the key doesn't exist yet.
7. **A script or Precondition reading an unset key** throws and stops the tree, where v3's checks just failed.
8. **Assuming a subtree sees the parent's keys.** It doesn't: add a port and pass `{key}`.
9. **`default=` on a subtree port** silently fills calls that leave it out. Not allowed; pass the value.
10. **Numbers that end up in text** (an `Info` message, a text port) print with six decimals: `-0.560000`.
11. **Expecting a Repeat or Retry round to take no time.** Each round of a one-tick child costs a tick now.
12. **Editing the globals Script by hand.** Use the Variables panel.
13. **Hand-editing `node_models.xml` action or condition entries.** `updateNodeModels` owns them; change the C++
    node and regenerate.
14. **Copying v3 XML in whole.** It pastes (the clipboard is plain XML), but convert it first: braces on keys,
    renamed nodes, constants as literals.

## Checking and running

- `build/release/tools/check/notgroot-check trees/` loads every mission in BehaviorTree.CPP 4.10 from a shell or CI,
  the same check notgroot runs on save.
- In riptide_autonomy, `ros2 run riptide_autonomy2 updateNodeModels --check` fails when `node_models.xml` is out of
  date or a subtree port has a default. `TestTreesLoad` loads every mission with the real plugins.
- `doTask` lists missions only (`list_trees`), never `lib/`, and logs each tree's load time.
- **Live view:** notgroot has none yet. Set `doTask`'s `enable_groot2` parameter to `true` (port `groot2_port`,
  default 1667) and attach Groot2 to watch a run. This has been checked to start, not yet with Groot2 attached.
