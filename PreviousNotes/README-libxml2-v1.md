# ProtocolDocEditor — libxml2 full replacement (v1)

**Status: PARTLY DONE, COMPILE-UNTESTED.** This is a large rewrite
done on a machine with no Qt C++ toolchain and no libxml2 headers,
so nothing here has been compiled. A fix counts as verified only
when your own build and run pass. Do not mark Bug 478 resolved on
the strength of this delivery — only your own build can verify it.

## What this is

Doug's direction, 2026-11-21: "All to libxml2", "full replacement".
The document model is now a libxml2 tree. The structs that only
existed to mirror XML are gone: `DocPreservedNode`, `DocInline`,
`DocParagraph`, `DocFigure`, `DocArtworkVersion`, `DocTable`,
`DocTableRow`, `DocTableCell`, `DocAuthor`, `DocPostalLine`,
`DocSection`, `DocSeriesInfo`, `DocNote`, `DocReference`,
`DocReferenceGroup`, `DocPreservedBlock`, all the `DocPreserved*Item`
structs, and all the `*Anchor` enums. The document IS an `xmlDocPtr`
owned by `DocDocument`; sections, authors, references, and body
content are `xmlNodePtr`s in that tree. The editor UI stays Qt
(`QTextEdit`, `QTextDocument`, `QTextCursor`); only the model
underneath changed. The XML file format does not change: same RFCXML
v3 out, valid against `v3.rng`.

## How to install

This zip is FLAT. Extract it into the DocEditor folder
(`D:\Unreal\RiverStudio\RelatedProjects\CoOp\DocEditor`), so the
files land beside the existing sources. Then move the two test files
into `tests/`:

- `DocEditorTests.cpp` → `tests/DocEditorTests.cpp`
- `DocEditorTests.pro` → `tests/DocEditorTests.pro`

The editor `.pro` is unchanged (libxml2 was already linked there)
and is not in this zip. `StartWizard.h` is unchanged and is not in
this zip. Close the editor before replacing files, then Clean and
Rebuild (the Bug 480 PowerShell clean fix applies as usual).

## Files in this zip (expected sizes)

| File | Bytes | Notes |
|---|---|---|
| `DocModel.h` | 34,016 | New model API |
| `DocModel.cpp` | 169,992 | Tree owner, custom serializer |
| `TextEditor.h` | 28,362 | New bridge API |
| `TextEditor.cpp` | 404,266 | Boundary conversion + Bug 478 |
| `MainWindow.cpp` | 163,957 | Node navigation, forms |
| `MainWindow.h` | 22,289 | Changed signatures |
| `StartWizard.cpp` | 40,103 | New-version / template paths |
| `DocEditorTests.cpp` | 34,370 | 12 tests, new model (move to `tests/`) |
| `DocEditorTests.pro` | 1,307 | Now links libxml2 (move to `tests/`) |
| `README-libxml2-v1.md` | (this file) | |

## The model, briefly

- `DocDocument` owns one `xmlDocPtr`. Copying a document is a deep
  copy (`xmlCopyDoc`). Node pointers handed out are borrowed into
  the tree unless a function says the caller owns them.
- `TextEditor::SetBodyNodes` / `SetPreservedNodes` deep-copy the
  nodes they are given; `BodyNodes()` returns deep copies the
  caller owns (or hands to `RebuildBodyChildren`, which adopts
  them). `TextEditor` frees its owned copies in `Clear()` and in
  its destructor.
- Reading uses `xmlReadFile` with `XML_PARSE_NONET` and no
  `NOBLANKS` (blank-stripping would eat the space between two
  inline elements). The tree therefore holds the file's formatting
  whitespace; child-enumeration helpers skip whitespace-only text
  nodes, and the writer never writes stored whitespace — it adds
  its own indentation.
- Writing uses a custom recursive serializer (NOT
  `xmlSaveFormatFile`, which reflows mixed content and would inject
  whitespace into `<t>` text). Block containers get a newline plus
  **4 spaces per depth** (matching the draft file on disk and the
  old writer); mixed-content elements (`t`, `name`, `title`, `li`,
  `dt`, `dd`, `th`, `td`, `sourcecode`, `artwork`, inline elements)
  are written inline with no added whitespace. The `<date>` is
  updated to the current date on save, as before.
- Preserved content is no longer a mirror tree: anything the editor
  cannot structure stays as ordinary nodes in the tree, and the
  editor receives them as `DocPreservedItem` (an anchor count plus
  a borrowed node) for display in the XML tab.
- `DocValidator` is unchanged — it already used libxml2.

## Bug 478 in the new model

Copy builds a detached `<dl>` / `<ul>` / `<ol>` node from the
selected blocks; paste inserts that node through `InsertListNode`.
The split happens at COPY time, on the node being built: a term
block of a `<dl>` is split at the first offset `TermSplitOffsets`
finds (soft break U+2028, break glyph U+23CE, bold-to-normal
fragment boundary, known-term match with the 20-char guard, plus
the v8.3 fallback for long term text when no other rule fires).
Paste never splits. The old HTML+Roles paste path is deleted
entirely; the clipboard carries the serialized node in mime type
`application/x-doceditor-list-run`.

## What was verified on this machine (no compiler)

- A Python/Qt harness probe (`dl-libxml2-split-probe.py`, 33 cases)
  simulating the copy-time split: the Bug.xml merged "Care factor"
  entry splits at offset 11 into a proper `<dt>` + `<dd>`; all 9
  clean terms (`Application identity`, `Record`, `Participant`,
  `Host`, `Light-host`, `Field`, `Shared value`, `Data dictionary`,
  `Care factor`) do NOT split; the result is independent of what
  else the document holds. This verifies the split LOGIC, not the
  C++ implementation.
- An lxml simulation of the serializer on the real draft file
  (`draft-royer-coop-00-f9498919.xml`): the re-serialized tree is
  semantically identical to the original, with the same single
  validity finding (see below) and the Terminology `<dl>` intact
  (9 dt/dd pairs, both `<bcp14>MUST NOT</bcp14>` inline). This
  verifies the serializer DESIGN, not the C++ code.
- Read-back of all delivered files: no remaining reference to any
  deleted struct; every `DocDocument` method called by the other
  files has a definition; braces/parens/brackets balance in all
  files; no `///` comments; Doxygen `/** */` throughout; new
  identifiers were grep-checked for collisions before adding.

## What was NOT verified

- Nothing has been compiled. The libxml2 2.15.4 headers in
  `RelatedProjects/include` are not on the build machine used
  here, so no libxml2 symbol was checked against a header; the
  code stays to the stable core API (`xmlReadMemory` /
  `xmlReadFile`, `xmlNewNode` / `xmlNewChild` / `xmlNewText` /
  `xmlNewComment`, `xmlAddChild` / `xmlAddPrevSibling` /
  `xmlAddNextSibling` / `xmlUnlinkNode`, `xmlCopyNode` /
  `xmlCopyDoc`, `xmlFreeDoc` / `xmlFreeNode` / `xmlFree`,
  `xmlGetProp` / `xmlSetProp` / `xmlUnsetProp` / `xmlHasProp`,
  `xmlNodeGetContent` / `xmlNodeSetContent`, `xmlStrcmp` /
  `xmlStrEqual`, `xmlBuffer*`, `xmlNodeDump`, `xmlGetLastError`,
  `xmlInitParser`, `xmlResetLastError`, `xmlDocGetRootElement`,
  `xmlNewDoc`, `xmlDocSetRootElement`) plus the RelaxNG symbols
  already used by `DocValidator`.
- Bug 478 is NOT resolved by this delivery. The first paste and
  the second paste both need your own build test.
- The 12 Qt tests were updated to the new API but not run
  (code-inspection only, as before).

## Behavior that changed (read before testing)

These are deliberate consequences of the tree model, not accidents.
Everything else is intended to behave exactly as before (Bugs
472–480, lists, sourcecode options gear, tables, figures,
references, front matter, blank lines, spell checker, XML tab,
Blocks button).

- **Indent is 4 spaces per depth** in the writer, matching the
  draft file on disk and the old writer. (An early draft of this
  rewrite used 2; corrected at integration.)
- **Blank lines between blocks normalize away on load+save without
  editing.** The writer skips whitespace-only text nodes between
  block siblings and adds its own indentation. Blank `<t>`
  elements the editor creates still write as blank lines, and
  blank lines typed inside sourcecode/artwork are literal text and
  survive. This is the one save-what-you-type case that does not
  round-trip byte-for-byte; the content is unchanged.
- **Title / name / organization / email markup:** text is now kept
  and only the markup is dropped (the old reader dropped the
  markup text too). Same for `<br/>` in a reference title.
  `HadSkippedContent` still warns.
- **spanx is no longer converted on load.** The old reader turned
  spanx into em/strong/tt; the tree keeps spanx, the editor shows
  it italic (bold when `style="strong"`), and it writes back as
  spanx. Valid RFCXML, same display, different element name in
  the file.
- **Front `<date>` is now kept in the tree** (the old reader
  dropped it) and updated to the current date on save, as before.
- **Address attributes and an invalid author `role` are now kept**
  as preserved content (the old reader dropped them).
- **`xml:lang` / `number` / `seriesNo` on `<rfc>` are preserved,
  not modeled** (the old writer modeled only `docName`,
  `category`, `ipr`, `submissionType`).
- **Per-line postal attributes are dropped on a lines-mode edit.**
  `SetAuthorPostalLines` takes plain strings, so a `<postalLine>`'s
  attributes do not survive being edited. They survive untouched
  while the lines are not edited.
- **Postal mode switch depends on `SetAuthorPostalLines` managing
  only the lines portion** of `<postal>` (structured parts are
  kept alongside). If switching structured → lines → structured
  loses the parts, that is a bug in that function, not the design.
- **A note whose `<name>` holds inline tags** is now listed in
  Document Settings with its concatenated name text, and editing
  it flattens the tags. The old reader kept such a note preserved
  whole, so it never reached the form.
- **An empty reference anchor or target** now omits the attribute
  instead of writing `anchor=""` / `target=""`. Unsavable either
  way (`Problems()` flags a missing anchor).
- **The draft file itself is invalid against `v3.rng`** with one
  error: the first `<note>` holds only a `<name>` and no body
  element. The old reader drops such notes on load (Bug 479), so
  loading and saving the file in the editor removes it and the
  file becomes valid. This is pre-existing, not caused by this
  rewrite.
- **Line endings:** the writer writes LF. The draft file on disk
  uses CRLF; a save will normalize endings (semantically neutral).
- **Duplicated serializer:** `TextEditor.cpp` has its own
  file-local `SerializeNode` / `FragmentNodeFromXml`, and
  `DocDocument::SerializeNode` / `SerializeBodyNodes` /
  `ListRunToXml` / `ListRunFromXml` exist but are currently unused
  by the editor. Harmless duplication; a cleanup can unify them
  later.
- **`DocTableRowMark`** in `TextEditor.cpp` is a new table-layout
  helper struct (row/section bookkeeping), not the deleted
  `DocTableRow`. The `DocFigure*Property` / `DocTable*Property`
  constants are the pre-existing `QTextFormat` block-property
  keys, kept unchanged.
