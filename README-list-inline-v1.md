# Lists holding inline elements (v1)

## What this fixes
A list whose items hold an inline element — `<bcp14>`, `<xref>`,
`<em>`, and the rest of the 17 — was kept whole as a preserved
block, so it never showed in the Text tab. Only the paragraphs
around it showed. In the draft, 7 lists were hidden this way:
the Terminology definition list, four bulleted lists, and two
numbered lists.

The cause: the list-item reader rejected any item holding an
element child, before the paragraph reader ever saw it. The
paragraph reader already handles inline elements (it does so
for `<t>` paragraphs); it just never got the chance for list
items.

The fix is one change in `DocModel.cpp`, in
`ItemParagraphsFromNode`: the non-`<t>` path no longer rejects
an item for holding an element. It lets the paragraph reader
convert the item, which accepts the 17 inline names and rejects
anything else. A nested list inside an item still keeps the
whole list preserved, as does an unknown element, and a term
holding a forbidden inline (`<u>`, `<spanx>`, and the rest)
still keeps it preserved.

## Files
- `DocModel.cpp` (197,792 bytes) — only this file changed.
  Drop it over the current `DocModel.cpp` and rebuild. It
  includes the blank-lines v1.1 fix (`BlankLinesIn` taking
  `QStringView`).

## How to install
Extract flat into the DocEditor source folder, replacing
`DocModel.cpp`, then rebuild. Close the editor first.

## How to test
Open the draft and go to Terminology and Conventions. The
Text tab should now show the definition list (Participant,
Host, Light-host, Record, Field, Shared value, Care factor,
Data dictionary, Application identity) below the RFC 2119
paragraph, with "MUST NOT" in Shared value shown bold. The
"1 preserved block" note for that section should be gone.
Save and reload: the list and its `<bcp14>` words come back
unchanged.

## Notes
- C++ code-reviewed and test-harness checked: list-inline
  probe 9/9, blank-line probe 20/20, trailing-paragraph 13/13,
  paste-cursor 28/28 (all still pass). Not compiled here.
  Your build is the real test.
