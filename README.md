# Trailing-paragraph fix, v1 — DocEditor

Fixes: a section ending in a `<sourcecode>` left nowhere to type after
it. The paste fix (v1) did not cover this, because the section was
loaded from the file, not pasted. The editor edits one section at a
time, and this section ends in the code block, so there was no normal
paragraph after it: Enter inside code only adds another code line, and
clicking below it finds nothing to click into.

## What changed

- `TextEditor.cpp` (337,580 bytes)
- `TextEditor.h` (25,901 bytes)

## Behaviour

- On load, a section ending in a sourcecode block gains an empty plain
  paragraph after it, so there is always somewhere to type. A section
  already ending in a normal paragraph is unchanged.
- Backspace at the start of that paragraph, and Delete at the end of
  the code line before it, do nothing — joining the paragraph into the
  code would leave nowhere to type again.
- If the paragraph is deleted by a selection, the idle step after the
  deletion puts it back.
- On save, that empty paragraph is never written, so the file does not
  gain anything. Once you type in it, it holds text and saves normally.
- Known corner: if you delete a trailing code block, the empty
  paragraph stays behind as a blank line at the end of the section. It
  is valid, and xml2rfc ignores it. Likewise, pressing Enter in the
  empty paragraph and then saving keeps both blank lines, because
  blank lines the writer made are preserved.

## Testing

- Harness (`qt-harness/trailing-paragraph-probe.py`): 13/13 passed —
  load ending in code, load ending in text, save skipping the empty
  paragraph, typing in it, the Backspace/Delete guards, the paragraph
  coming back after a selection-delete, an empty section, and Enter in
  the paragraph.
- Paste probe (`qt-harness/paste-cursor-probe.py`): still 28/28 after
  the shared-helper refactor.
- The C++ was code-reviewed only, NOT compiled. Doug's build is the
  real test.

## Install

Extract the zip flat into the DocEditor source folder, replacing
`TextEditor.cpp` and `TextEditor.h`, then rebuild.
