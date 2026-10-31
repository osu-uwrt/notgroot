# Keyboard

Everything in notgroot can be done from the keyboard. Every command is also listed under Ctrl+Shift+P, with its key.
Keys marked *canvas* work while the canvas has focus (click it, or press Esc in a text field to get back to it).

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
| Shift + ← / → | the sibling, and adds both nodes to the selection |
| Home | the tree's root |
| Esc | clears the selection and the key highlight |

In the left-right layout (Ctrl+L) the arrows turn with the tree: ← parent, → child, ↑/↓ siblings.

| Key | View |
|---|---|
| Space | fold / unfold a node; on a SubTree call, open the subtree in place (read-only) |
| Ctrl+Enter | open the subtree's own tree (on a call or on a node opened in place) |
| Ctrl+= / Ctrl+- | zoom in / out (Ctrl+mouse wheel zooms at the mouse) |
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
