# Fix build for Bug 502 (abstract lost on reopen) — v1

Same code as the 405 build, plus the fix below and the diagnostics
from v2, kept so the fix can be verified. The diagnostics come out
in the final clean build.

## Files

- MainWindow.cpp (296,980 bytes) — the only code file changed.
- MainWindow.h (34,876 bytes) — the only header changed.
- README-DIAGNOSTIC.md (this file) — instructions, do not overwrite README.md.

TextEditor.cpp and TextEditor.h are unchanged from diagnostic v1; the
files from the v1 zip stay as they are.

## The fix

The v2 diagnostic proved the cause: after a file loads, the abstract
editor syncs once with 0 nodes — an empty editor overwriting the
loaded abstract before the Abstract page is ever filled. The fix is
a flag, `_AbstractEditCurrent`: it is false when a document is
loaded or created, and true only after the Abstract page fills the
editor from that document. `OnAbstractEdited` now ignores a sync
while the flag is false (the nodes it built are freed, not adopted),
so a stale empty editor can no longer wipe the loaded abstract.
Once the page is filled, edits sync exactly as before. The Make
Reference flow is unaffected: it only syncs the editor that is
showing, and a showing editor was filled.

## Test 1: Abstract page

Open draft-royer-coop-00.xml, click Abstract in the outline. The
abstract text should now be there. A line starting "Diag (Bug 502):"
appears under it. The line is selectable: drag across it, copy it,
and paste it back.

What to expect now:

  Diag (Bug 502): on load, front yes, abstract yes, 1 element children;
  on this page, body 1, preserved 0, editor blocks 2; syncs since load 1,
  last sync nodes 0.

- body 1 means the model handed the page the abstract — the fix held.
- syncs 1, last sync nodes 0 means the empty sync still fired, but
  the guard blocked it: the wipe was attempted and stopped.
- editor blocks 2 means the page shows the abstract paragraph plus
  the trailing empty paragraph.

Then also check the round trip: edit the abstract, save, close the
file, reopen it, click Abstract — the edited text should still be
there.

## Test 2: Gears (option markers)

Unchanged from v2, still open. Open a section holding sourcecode.
Uncheck "Show option markers", then check it again. Each toggle pops
a one-line report like:

  Markers: shown=1, blocks=12, first source=2, gears inserted=2,
  gears removed=0.

Paste both pop-up lines back.

How to read it:

- first source=0 means the pane holds no sourcecode blocks at all.
- gears inserted=2 but no gears visible means the gear character is
  in the text but not drawing.
- If the numbers look right and the gears still do not show, the
  next step is the font/drawing side.
