# Blank lines — save what you type (v1)

## What this fixes
Blank lines you type are now saved in the file as blank lines, and read
back as blank paragraphs in the same place. Before this, a blank line
typed between paragraphs or inside a list was lost on save, and a blank
line inside a list split the list into two lists in the file.

## What changes
- A blank paragraph between paragraphs writes as a literal blank line
  in the file, not as an empty `<t>`. Loading reads it back as a blank
  paragraph in the same place.
- A blank line inside a list stays inside that list: the list is still
  one list in the file, with the blank line between the items. It reads
  back as a blank line between the same items.
- A blank line inside one list item (for example, after Shift+Enter)
  is saved inside that item and reads back.
- The empty paragraph the editor puts after a code block at the end of
  a section is still not saved, as before: it is there so there is
  somewhere to type, not something you typed.

## Files
- `TextEditor.cpp` (342,622 bytes), `TextEditor.h` (25,901 bytes) —
  the walk now keeps a blank line between list items inside the list,
  and loading a list with a blank line puts it in as a plain blank
  block with no bullet or number.
- `DocModel.cpp` (197,256 bytes), `DocModel.h` (45,141 bytes) — the writer writes
  blank lines, and the loader counts the newlines between elements
  to read them back.

## How to install
Extract flat into the DocEditor source folder, replacing the four
files, then rebuild. Close the editor first, so the build picks up
the new files.

## How to test
1. Open a section, type a paragraph, hit Enter twice for a blank
   line, type another paragraph. Save. The file has a blank line
   between the two paragraphs, and reloading shows the blank line
   in the same place.
2. In a list, enter a value, hit Enter twice for a blank line, enter
   the next value. Save. The file still has one list, with a blank
   line between the two items. Reloading shows the same list, with
   the blank line between the same items.
3. Type two blank lines in a row, between paragraphs and inside a
   list. Both are saved and both come back.

## Notes
- C++ code-reviewed and test-harness checked: blank-line probe
  20/20, trailing-paragraph probe 13/13, paste-cursor probe 28/28
  (all still pass). Not compiled here. Your build is the real test.
- A blank line before a section name, or inside a note, is saved but
  may move to just after the name on the next save; it is not lost.
- xml2rfc ignores blank lines when it formats, so the output is
  unchanged.
