# DocEditorTests — the first Qt Test suite (Bug 462)

The first automated tests for the ProtocolDocEditor: the text
editor driven the way it is driven by hand — typing keys,
pressing the list button, and reading back the paragraphs and
the document blocks. Each test gets a fresh `TextEditor`.

The tests compile the editor sources (`DocModel.cpp`,
`TextEditor.cpp`) unchanged. No app source file was modified,
and no hook was added to the editor for testing: the tests use
only the public `TextEditor` API, `findChild`, and `QTest`.

Status: code-inspection-only. Every identifier was checked
against the headers and every assertion was re-read against the
Enter, list, and margin code paths, but the suite has not been
compiled — there is no Qt C++ toolchain on the machine that
wrote it. Doug's build is the verification.

## Build and run

1. Extract the zip into the DocEditor folder, so this folder
   lands at `DocEditor/tests/` beside the editor sources.
2. In Qt Creator, open `tests/DocEditorTests.pro` with the same
   kit as the editor ("Desktop Qt 6.12.0"). Build, then Run.
   The test windows show briefly while the tests run.
3. From the command line instead: in the `tests` folder, run
   `qmake` then `mingw32-make check`. `make check` builds the
   suite and runs it, printing a pass/fail line per test.
4. To run with no windows at all, set the offscreen platform
   plugin before running the test binary:
   `QT_QPA_PLATFORM=offscreen DocEditorTests.exe`.

A passing run prints `PASS` for all twelve tests and ends with
`Totals: 12 passed, 0 failed`.

## What each test asserts

- `Bug461EnterDouble` (Bug 461): in an empty paragraph, typing
  "line 1", Enter, Enter, "word" gives 2 blocks and 2 paragraphs
  holding "line 1" and "word".
- `Bug461EnterSingle` (Bug 461): typing "line 1", Enter, "word"
  gives 1 block and 1 paragraph holding "line 1\nword" — the
  soft break reads back as a newline.
- `Bug461BulletedButton` (Bug 461): after the double-Enter
  typing above, clicking the Bulleted list button puts only the
  second block ("word") in a list; the first block stays plain.
  The button is found by its label text, since the buttons carry
  no object name.
- `Bug459ListIndent` (Bug 459): a two-item bulleted list, cursor
  at the end of the second item, Shift+Enter then Enter. The
  document then holds 4 blocks in order: item, item,
  continuation, new item. All three list members share one list
  and sit at indent 0; the continuation sits outside the list at
  indent 1. Without the fix, the rebuild would leave the two old
  items at indent 1.
- `Bug460ParagraphSpacing` (Bug 460): two plain paragraphs plus
  a two-item list. The plain blocks carry a bottom margin of one
  line (the document default font's line spacing); the list
  blocks carry 0. No keys are simulated.
- `GearGlyphRenders` (options markers, Bug 312): the gear glyph
  (U+2699) is painted into an image in the editor's font next to
  U+FFFF, which no font has. The two pictures must differ, so
  the gear draws as a real glyph and not as the missing-glyph
  box, and the gear picture must hold ink. This answers the
  font-support question on the machine that runs the tests,
  including Qt's font fallback.
- `Bug420GearShowsAndTooltip` (Bug 420): a two-line sourcecode
  block with name, type, and markers set. The gear shows at the
  start of the first block only, its hover text lists the set
  options, and the paragraphs read back with the attributes and
  the two lines, no gear.
- `Bug420MarkersToggle` (Bug 420): toggling the "Show option
  markers" checkbox off removes the gear and toggling it on
  restores it, and the paragraphs read back the same either way.
  The test ends with the markers on. Each test that needs the
  gear sets the checkbox itself, since the setting is remembered
  across runs.
- `Bug466SrcPlaceholder` (Bug 466): a src-only block shows the
  italic `<Src=Examples/TestFile.cpp>` placeholder on the gear's
  line, and alone when markers are off. A block holding real
  text shows the text and no placeholder.
- `Bug466PlaceholderStrippedAndRestamped` (Bug 466): the
  read-back of a src-only block holds the src attribute and no
  text, so the placeholder never reaches the model. Setting the
  paragraphs again with a different src restamps the placeholder
  with the new file.
- `Bug467SrcOnlyLoadsAsParagraph` (Bug 467): a minimal draft
  written to a temporary file, holding one src-only
  `<sourcecode>` element, loads as a modeled paragraph carrying
  the src attribute, and the text edit shows the gear and the
  placeholder for it.
- `Bug420OptionsMenuName` (Bug 420): the gear's menu edits the
  name. The test right-clicks the gear, and timers drive the
  open menu (the "Source code options" submenu, "Name...") and
  the prompt (accepting with a new name), then checks the
  read-back carries the new name. When the menu or the prompt
  is not found, it is closed instead, so a failure fails the
  assertions rather than hanging the run in a modal loop.

## What is not covered

Only these behaviors are covered. Lists beyond the one indent
case, figures, tables, the wizard, the outline, and the file
writer have no tests yet. The XML tab is not asserted: the tests
read the model (`Paragraphs()`) and the document blocks
directly. The Bug 465 xml2rfc-run flag is deliberately not
tested: running xml2rfc as a subprocess is out of scope for
this suite.
