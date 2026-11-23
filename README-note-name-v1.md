# Named-but-empty note fix (Bug 479)

DocModel.cpp only. Drop it over the DocModel.cpp from the
note-outline build (it includes the Bug 473 list-inline fix
and the Bug 475 empty-note fix as well) and rebuild. No .pro
change, no new files.

## What was wrong

Saving and validating the draft reported:

    Expecting an element, got nothing

at `/rfc/front/note[1]` (line 13). The first `<note>`,
"Changes in this -00 revision (2026-11-05)", holds a
`<name>` and nothing else — no `<t>` or other content
element, just a blank line. The schema requires a `<note>`
to hold a content element after the name, and a name alone
does not count.

Why it was written: a note with no `<t>` or list block
never becomes a modeled note (`NoteFromNode` returns
false, since it saw no block), so it falls to the preserved
path. `PreservedNoteIsEmpty` treated *any* element child —
including `<name>` — as content, so the name-only note was
kept and written back verbatim, invalid. Bug 475 fixed the
fully-empty note; this is the named-but-empty case it did
not cover.

## What changed

`PreservedNoteIsEmpty` now treats a `<name>` child as a
title, not content: a note holding only a `<name>` (and
whitespace) is empty and is dropped on load, so the save
is valid. A note with a name and a `<t>` is kept.

A modeled note with a name but no paragraphs is already
fine — `WriteNoteElement` writes an empty `<t/>` for it,
which the schema accepts — so `NoteHasContent` is
unchanged.

## Verification

Against `schemas/v3.rng` (lxml RelaxNG) on the file Doug
sent: the file as-is fails at line 13 with exactly his
error; with the name-only note dropped it validates clean.
The other note ("Author's Address", name + `<t>`) is
untouched. C++ compile-untested here (no Qt dev on this
VM) — Doug's build is the real test.

Note: dropping the note means the "Changes in this -00
revision" note is gone from the file (it held no text).
If you want that note back, add it again with its text.
