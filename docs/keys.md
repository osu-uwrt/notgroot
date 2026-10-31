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
| Space | fold / unfold a node; on a SubTree call, open the subtree in place (read-only) |
| Ctrl+Enter | open the subtree's own tree (on a call or on a node opened in place) |
| Ctrl+= / Ctrl+- | zoom in / out a step (Ctrl+mouse wheel zooms at the mouse) |
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

Alt+↑ and Alt+↓ always move. Where the new parent can't hold the node (a leaf, a decorator that already has its
child, a tree's second root) the node *floats* there: drawn dotted, with a dotted edge, ready to move on. Saving is
refused while any node floats; the message lists the trees, and clicking one goes to its floating node.
| Delete | delete the selection (and the comments right above each node) |
| Ctrl+X / Ctrl+C / Ctrl+V | cut / copy / paste nodes. Paste goes into the cursor's node, else after it. The clipboard is plain XML, so trees from a text editor or Groot paste too |
| Ctrl+D | duplicate the selection |
| Ctrl+E | extract the selection into a new subtree: its ports come from the keys the nodes use |
| Ctrl+Z / Ctrl+Shift+Z (Ctrl+Y) | undo / redo. Every edit is one step, including multi-file renames |

## In the inspector

| Key | Edit |
|---|---|
| Enter | commit the value and go back to the canvas |
| Esc | cancel and go back to the canvas |
| `{` | start a key: the tree's keys are suggested. ↑/↓ choose, Tab or Enter take one |
| Tab | (no suggestion showing) the next field |

The `{}` button beside a value turns text into a key (`2` → `{2}`) and back.

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

## Mouse

| Mouse | Does |
|---|---|
| Click | puts the cursor on a node. Ctrl+click or Shift+click adds it to the selection or takes it out |
| Shift + drag on empty canvas | selects the nodes in the box (with Ctrl: adds them) |
| Drag a node | moves it, with the rest of the selection when it is selected. Dropped on the ends of a node it goes beside it; on the middle, inside it as the last child. A red cross: it can't go there |
| Double-click | edits the node; on a SubTree call, opens its tree |
| The round button under a call | opens the subtree in place, read-only (as Space) |
| Right-click | the node's menu: what applies to it, greyed with the reason when it can't run now |
| Drag on empty canvas, or the middle button | pans |
| Wheel / Shift+wheel / Ctrl+wheel | scroll / scroll sideways / zoom |
