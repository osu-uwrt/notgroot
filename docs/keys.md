# Keyboard

Everything in notgroot can be done from the keyboard. Every command is also listed under Ctrl+Shift+P, with its key,
and F1 (Help > Keyboard shortcuts) shows this list in the editor, with a filter. Keys marked *canvas* work while the
canvas has focus (click it, or press Esc in a text field to get back to it).

## Finding things

| Key | What it does |
|---|---|
| Ctrl+P | Go to anything: missions, subtrees, every node in every tree (by type, name or a port value), and keys of the open tree |
| Ctrl+Shift+P | Commands (the same finder, starting with `>`) |
| Ctrl+Shift+V | Keys of the open tree (`$`) |
| Ctrl+F | Search the open tree: Enter goes to the next hit, Shift+Enter to the previous one, Esc closes |
| F8 | Next problem (errors and warnings) |
| F7 | Check every mission with BehaviorTree.CPP 4.10 (it also runs after every save) |

In the finder, these prefixes narrow what it searches:

| Prefix | Searches |
|---|---|
| `>` | commands |
| `@` | nodes of the open tree |
| `$` | keys of the open tree |
| `#` | trees only |

Typing `pmp` finds `PrimitiveMovePosition`: letters match in order, and word starts count most.

## Moving around (*canvas*)

In the default top-down layout:

| Key | Moves the cursor to |
|---|---|
| ↑ | the parent |
| ↓ | the first child (or back to the child you came up from) |
| ← / → | the previous / next sibling, or the nearest node on the same level |
| Shift + arrows | there, and adds both nodes to the selection |
| Ctrl + arrows | there, and leaves the selection as it is |
| Home | the tree's root |
| Esc | clears the selection and the key highlight |

In the left-right layout (Ctrl+L) the arrows turn with the tree: ← parent, → child, ↑/↓ siblings.

## Selecting (*canvas*)

| Key | Selects |
|---|---|
| Ctrl+A | the cursor's siblings; press it again for every node of the tree |
| Ctrl+Shift+A | the node and everything under it |
| Ctrl+Shift+L | every node of the cursor's type (calls: of the same subtree) |
| Ctrl+Space | adds the cursor's node to the selection, or takes it out |
| Shift+Home / Shift+End | up to the first / last sibling |

Wrap and extract work on nodes under one parent; delete, copy, cut and duplicate on any selection.

## Viewing (*canvas*)

| Key | View |
|---|---|
| Space | fold / unfold a node; on a SubTree call, open the subtree in place (locked: shown, not edited) |
| Ctrl+U | unlock the subtree opened in place to edit it right there, or lock it again (on a call or a node inside it) |
| Ctrl+Enter | open the subtree's own tree (on a call or on a node opened in place) |
| Ctrl+= / Ctrl+- | zoom in / out a step (Ctrl+mouse wheel and a touchpad pinch zoom at the mouse) |
| Ctrl+0 | fit the tree to the view |
| Ctrl+L | switch between top-down and left-right layout |
| Ctrl+PageDown / Ctrl+PageUp | next / previous tree tab |
| Ctrl+F4 | close the tree tab |

## Editing (*canvas*)

| Key | Edit |
|---|---|
| Tab | add a child: the node picker opens beside the cursor. Type to filter, Enter adds. On a leaf, it adds after it instead |
| Shift+Tab | add a sibling after the cursor |
| Enter | edit the node's ports in the inspector |
| Ctrl+W | wrap the selection in a control or decorator |
| Ctrl+Shift+W | unwrap: replace the node by its children |
| Ctrl+R | change the node's type (children and the ports the new type shares are kept) |
| Alt + ← / → | move the node earlier / later among its siblings |
| Alt + ↑ | move the node out of its parent, right after it |
| Alt + ↓ | move the node into the previous sibling, as its last child |
| Delete (Backspace) | delete the selection (and the comments right above each node) |
| Ctrl+X / Ctrl+C / Ctrl+V | cut / copy / paste nodes. Paste goes into the cursor's node, else after it. The clipboard is plain XML, so trees from a text editor or Groot paste too |
| Ctrl+D | duplicate the selection |
| Ctrl+E | extract the selection into a new subtree: its ports come from the keys the nodes use |
| Ctrl+Z / Ctrl+Shift+Z (Ctrl+Y) | undo / redo. Every edit is one step, including multi-file renames |

Alt+↑ and Alt+↓ always move. Where the new parent can't hold the node (a leaf, a decorator that already has its
child, a tree's second root) the node *floats* there: drawn dotted, with a dotted edge, ready to move on. Saving is
refused while any node floats; the message lists the trees, and clicking one goes to its floating node.

A subtree opened in place starts locked: grayed, in its own color. Unlocked, its band turns amber with a dashed
edge, and every edit there goes to its own file (`lib/<ID>.xml`), so it changes every tree that runs it. Locking a
subtree, or closing it, also locks every subtree under it. A locked subtree inside an unlocked one stands out as a
gray island with a solid edge. The count above a call says how many trees call it. Its globals sit in a box beside
the call: a list while locked, editable while unlocked.

## In the inspector

| Key | Edit |
|---|---|
| Enter | commit the value and go back to the canvas |
| Esc | cancel and go back to the canvas |
| `{` | start a key: the tree's keys are suggested. ↑/↓ choose, Tab or Enter take one |
| Tab | (no suggestion showing) the next field |

A port can also be edited on its card: double-click its row. Enter keeps it, Esc cancels, Tab keeps it and moves to
the next row, and `{` suggests keys as in the inspector.

The `{}` button beside a value turns text into a key (`2` → `{2}`) and back.

In the Variables panel's Globals, typing after `{` shows the rest of the nearest input or global in gray; Tab takes
it and closes the brace.

## Trees and keys

| Key | Edit |
|---|---|
| F2 | rename the highlighted key across the tree. For a subtree port, this also renames it at every call. With no key highlighted, it renames the tree |
| Ctrl+Shift+E | edit a subtree's ports (on its tree, or on a call to it) |
| Ctrl+Shift+N | new subtree (lib/<ID>.xml) |
| Ctrl+S | save every changed file. Includes are fixed up, then the BehaviorTree.CPP check runs |
| Ctrl+O | open a trees folder |
| Ctrl+Q | quit. If there are unsaved changes, it asks first |
| F1 | this list, in the editor |

## Runs

doTask writes a log of every run to `~/btlogs` (`BTLog_<date>_<time>.btlog`). Open one to see the run on the trees
as they ran: it opens beside the editor, read-only. The canvas shows one subtree instance at a time, its cards tinted
with their status at the playhead (amber running, green success, red failure; faded once idle again).

| Key | Does |
|---|---|
| Ctrl+Shift+O | open a run (`.btlog`) |
| F6 | switch the canvas, inspector and variables between the editor and the run |
| P | play / pause (*canvas*). The Timeline sets the speed |
| . / , (Period / Comma) | step to the next / previous transition (*canvas*) |
| ] / [ (RightBracket / LeftBracket) | jump to the next / previous subtree failure (*canvas*) |
| F | follow: the canvas goes to the deepest subtree running (*canvas*) |
| Enter (Ctrl+Enter) | go into the subtree call under the cursor (*canvas*) |
| Backspace | go up to the instance that called this one (*canvas*) |
| Ctrl+Shift+Enter | show the cursor's node in the editor (when its tree hasn't changed since the run) |

The *canvas* keys above also work while the Timeline has focus. Space still opens a call in place, showing that
call's own instance. In the Run panel, click an instance to show it and double-click to move the playhead to when it
started. In the Timeline, click or drag the bar or a lane to move the playhead (playing goes on from there), the wheel
zooms, a right drag pans, and a double-click on the bar shows the whole run.

## Mouse

| Mouse | Does |
|---|---|
| Click | puts the cursor on a node. Ctrl+click or Shift+click adds it to the selection or takes it out |
| Shift + drag on empty canvas | selects the nodes in the box (with Ctrl: adds them) |
| Drag a node | moves it, with the rest of the selection when it is selected. Dropped on the ends of a node it goes beside it; on the middle, inside it as the last child. A red cross: it can't go there |
| Drag from the palette | adds the node the same way: on a node's middle inside it, on its ends beside it. Cards that can take it get a faint ring; elsewhere a red note says why it can't go there |
| Double-click a port row | edits that value right on the card, which grows with the text |
| Double-click a node's title | names the node right on the card; on a SubTree call's title, opens its tree; on the tree's own card, renames it |
| Double-click a node in a locked subtree | opens that subtree in its own tab, on that node |
| The round button under a call | opens the subtree in place, locked (as Space) |
| The padlock on an opened call's corner | unlocks it to edit in place, or locks it again (as Ctrl+U) |
| Right-click | the node's menu: what applies to it, grayed with the reason when it can't run now |
| Drag on empty canvas, or the middle button | pans |
| Wheel / Shift+wheel / Ctrl+wheel | scroll / scroll sideways / zoom |
| Touchpad pinch | zoom at the pointer |
