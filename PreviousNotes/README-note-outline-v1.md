# doceditor-note-outline-v1

Two changes, delivered together as Doug asked.

## 1. Empty `<note>` breaks xml2rfc and save validation (Bug 475)

### The fault

Running xml2rfc on the draft failed:

```
draft-royer-coop-00.xml(16): Error: Expecting an element , got nothing, at /rfc/front/note[1]
```

Saving in the editor reported the same thing from its own
schema check (v3.rng): "Expecting an element, got nothing".

The first `<note>` in the file was empty (just whitespace, no
child element). The schema requires a `<note>` to hold an
element, so an empty one fails validation and xml2rfc.

### What made it empty

The note once held `<t>DRAFT - FOR DISCUSSION ONLY - NOT AN
IETF SUBMISSION</t>`. Its text was removed while editing, so the
note held only a blank paragraph. The writer then wrote that
blank paragraph as a blank line inside `<note>`, leaving an
empty `<note>` in the file.

### The fix (DocModel.cpp)

A note that holds nothing is no longer written, and an empty
`<note>` already in a file is dropped when the file loads:

- `NoteHasContent` (new): true when a note has a name, a kept
  attribute or block, or any paragraph holding content. A note
  whose paragraphs are all blank lines holds nothing.
- The front-matter write loop skips notes where
  `NoteHasContent` is false. Removing the text removes the
  note, the same way an empty abstract is already skipped.
- `PreservedNoteIsEmpty` (new) and `AddNoteOrPreserved`: an
  empty `<note>` found while loading is dropped instead of
  being kept as a preserved item and written back invalid.
  A note holding a comment, an attribute, or any element is
  kept, not dropped.
- The same blank-only guard was applied to the abstract: an
  abstract holding only blank lines is skipped, not written
  as an empty `<abstract>`.

### Verification

- The fixed file (empty note removed) validates clean against
  `schemas/v3.rng`; the original fails at line 16 with exactly
  the error Doug reported. Checked with lxml RelaxNG.
- The note logic was exercised in a probe: an empty note is
  dropped, a note with a `<t>` is kept, a note with a comment
  or an attribute is kept, a named note with no paragraphs is
  written, a modeled note emptied to one blank paragraph is
  skipped, and a blank-only abstract is skipped.

### What to test

Open the draft and save it. The empty `<note>` should be gone
from the file, the save validation should pass, and xml2rfc
should run past the note. The other two notes ("Changes in
this -00 revision" and "Author's Address") are untouched.

## 2. Section numbers in the outline

The outline now shows each section's number in front of its
title: "1 Introduction", "1.1 The coordination problem",
"3.1.2 ...". The number comes from the section's position,
the same way xml2rfc numbers it, so it matches what xml2rfc
prints and updates itself when sections are added, removed,
or moved. It is display only: it is not stored in the file
and does not change the title or the anchor. A rename keeps
the number in front of the new title.

### The change (MainWindow.cpp)

- `SectionNumberFromPath` (new): turns the outline's
  zero-based dotted path (0, 0.1, 2.0.1) into the one-based
  display number (1, 1.2, 3.1.2). Forward-declared near the
  top, since the title-edit handler calls it.
- `AddSectionItem` prepends the number to the item text.
  The path stored as the item's key is unchanged.
- `OnSectionTitleEdited` prepends the number when a rename
  updates the outline item, using `_SectionPath`.

### What to test

Open the draft. The outline should read "1 Introduction",
"1.1 ...", "1.2 ...", "2 ...", and so on, matching the
numbers xml2rfc prints. Rename a section; the number should
stay in front of the new title.

## Files

- `DocModel.cpp` (200,621 bytes) — note/abstract fix. This
  copy includes the list-inline fix (Bug 473, doceditor-
  list-inline-v1.zip), so it supersedes that file.
- `MainWindow.cpp` (136,922 bytes) — outline numbers.

Flat zip: extract into the DocEditor source folder, replacing
those two files. No project-file change.

C++ code-reviewed and logic-probed only; Doug's build is the
real test.
