# InternetDraftsEditor

A Qt desktop editor for writing Internet-Drafts. The audience is a
person knowledgeable about protocols and not knowledgeable about
xml2rfc: the editor asks the questions, writes the RFCXML, and runs
xml2rfc. The CoOp editor designs the protocol; this editor is where
the text of the document goes.

## Goal #1 (this version)

- A start wizard asks the minimum a draft needs: the first
  author, the title with the draft name and version, and the
  save folder. It also offers starting from one of the IETF
  templates instead of from scratch. The abstract comes
  prefilled with the IETF example text and edits in the main
  window.
- The main window is split outline-left / details-right. The outline
  is prefilled with the required parts: front (title, authors, date,
  abstract), middle (Introduction, Security Considerations, IANA
  Considerations), back (References). Sections add, insert, remove,
  and rearrange from the buttons under the outline.
- File > Save writes the draft as RFCXML version 3, valid against the
  v3.rng schema from the xml2rfc 3.35.0 distribution. When the tool
  created or modified the content, the file starts with a comment
  naming it and its version ("InternetDraftEditor <version> was
  used as an aid to create this document", with the project GitHub
  address, Bug 505); a file merely opened and saved unchanged, that
  never had the comment, gets none, and a file that already has it
  keeps it. The date
  written is always the current date: a draft's date stays current
  until the draft is submitted (a draft expires 6 months after its
  date). A submit button, which seals the draft, comes later. The writer
  defaults the document to category "std", ipr "trust200902", and
  submissionType "IETF" (xml2rfc requires a category for IETF
  submissions); the reader keeps the category a file already has.
- File > Run xml2rfc runs xml2rfc on the saved file and shows its
  output. The xml2rfc program path is remembered (File > Set xml2rfc
  Path...); it defaults to `xml2rfc` on the PATH.
- File > Open reads back the subset this version writes. A file
  holding content this version does not edit yet (artwork,
  tables) opens with a warning, and saving
  warns before dropping that content.

## Goal #2 (this version)

- What was saved gets read back in and validated against the v3
  schema, inside the app, with libxml2's RelaxNG checker. Saving
  checks the file just written; opening checks the file just read,
  so a file edited by hand is checked before it is edited further.
  File > Validate checks the saved file on demand and always shows
  the result. A problem shows the checker's messages, each with
  the line number in the saved file, so the author can open the
  file and go straight to it; a valid file
  shows in the status bar.
- The schema files ride inside the program and are installed on
  startup into the user's roaming profile
  (`RiverExplorer\CoOp\Schemas` under AppData\Roaming), missing
  or changed files copied fresh each launch. The checker
  resolves the included files from the schema file's own
  folder. A `schemas` folder beside the program is still used
  as a fallback when the installed copy is missing; without
  either, validation is skipped and the status bar says so.

## Goal #3 (this version)

- Loading a file keeps everything in it. Parts the editor does not
  edit yet — tables, figures, artwork, author
  address details, nested lists, extra attributes, comments — are
  kept verbatim and written back in their original position when
  the file is saved, so load-save round-trips lose nothing. The
  pages that hold kept content say so, and opening a file with
  kept content says so.
- Only three things cannot be kept: a document type declaration,
  markup inside a title, section name, organization, or email, and
  stray text outside elements. Those still warn on open and on save.
- A paragraph holding markup the text editor knows (cross-references,
  bold, lists, and the like) is read as structured paragraphs and
  edited in the text editor (Bug 322), as is a section-level list
  holding only structurable items (Bugs 382-387). A paragraph or
  list holding anything else, or mixing a list with other text, is
  kept whole as a block, in place among the editable paragraphs;
  editing the body text does not touch it.

## Text editor (Bugs 322, 382-387, 393)

- Section bodies and the abstract edit in a two-tab editor: Text
  and XML. The Text tab is rich text: tags show as formatting, never
  as markup. The XML tab shows the raw `<t>` XML of the same
  paragraphs, regenerated as the text changes, formatted
  with indentation for readability (Bug 516). The title
  stays a plain line: the schema allows it text plus a break only.
- The XML tab has a Read-only checkbox at the top, checked by
  default (Bug 520). Unchecking it makes the pane editable, so XML
  can be pasted and edited directly instead of hand-editing the file
  outside the editor. Re-checking it applies the edits: the XML is
  re-parsed and the text rebuilt from it. Switching to the Text tab
  or saving applies pending edits the same way. A pasted `<section>`
  becomes a subsection of the current section, added after the
  existing subsections; a pasted anchor that already exists in the
  document is made unique (base-2, base-3, ...), and references
  inside the pasted section that point at it are updated. While the
  pane is editable it shows only the body nodes; preserved content
  is kept but hidden, and shown again when Read-only is re-checked.
  On a parse error the error is shown with its line, the box stays
  unchecked, the edited XML stays in the pane, and the document is
  untouched, so a bad paste cannot corrupt the document or drop
  edits. A `<section>` pasted into the abstract or the settings
  note is refused. An element the editor does not model renders
  as plain text (its wrapper is dropped).
- A right-click menu offers the 16 inline tags of `<t>` in the
  xml2rfc words, always alphabetical. With text selected, a tag
  wraps the selection. Without a selection, a tag starts or ends
  at the cursor, for typing. Tags already present can be removed.
  The standard cut/copy/paste set sits alongside the tag options.
  Tags are offered only where the schema allows them to nest.
  spanx is not offered: the reader converts it to `<em>`,
  `<strong>`, or `<tt>` on import, per RFC 7991, and any other
  spanx stays a spanx.
- Line Break, Vertical Space, and Index Entry show as marker
  symbols (a Break glyph, a vertical-space glyph with its
  blank-line count, the index item text). Links and comments show
  link-styled with hover tooltips saying what they point to. A
  cross-reference hover names the section, figure, or table it
  points at by its current number and title ("Cross-reference to
  Section 11: Versioning and Compatibility"), not just its anchor,
  so it stays right when sections are renumbered (Bug 515); an
  anchor that points elsewhere keeps the anchor text.
  Unicode text shows plain, with a Unicode hover note.
- Cross-Reference, External Link, and Relative Reference ask for
  a target when inserted; an Index Entry asks for its item and an
  optional sub-item; a Vertical Space asks for its blank-line
  count. Other tag attributes are kept when a file is read and
  written back, but not edited yet.
- A list stands alone as its own paragraphs: its items show as
  list blocks. The List menu entry opens a submenu with Bulleted
  list, Definition list, and Numbered list, and three buttons
  above the text do the same, showing which kind the cursor is
  in. A button is greyed out while the cursor touches a
  different kind of list, so lists cannot nest. With paragraphs
  selected, each becomes an item; without
  a selection, the paragraph at the cursor becomes the first
  item; a new definition list leaves the cursor in the term,
  ready to type it. Lists follow the Word keys: Enter starts
  the next item,
  Shift+Enter adds a paragraph to the same item, and Enter on
  an empty item leaves the list. In a definition list, Enter in
  a term starts its definition, Enter in a definition starts
  the next term, and Enter on an empty term leaves the list; a
  definition list shows its terms bold on a tinted background,
  each term followed by its indented definition paragraphs. An
  item holding several paragraphs writes as several <t> children.
  A Remove action naming the kind turns a list back into
  paragraphs. A paragraph mixing a list with other text stays a
  preserved block. Nested lists stay preserved, untouched. List
  options are edited through the list's gear marker: see Option
  markers below. Figures and tables get their markers in later
  builds.
- A sourcecode block stands alone as its own lines: each line is
  a block, shown monospace and indented. The Blocks button above
  the text starts and stops one: its menu picks the kind of
  block, its label names the picked kind, and pressing it again
  stops the block: the cursor moves to a new plain paragraph
  after the code, which stays code. A right-click Source code
  action starts one, and a Remove action turns the block back
  into paragraphs, one per line. Enter adds a line and never leaves
  the block, because code keeps every line it is given, blank
  lines included; Shift+Enter adds a line too. The Tab key
  inserts four spaces, because tabs are prohibited in
  sourcecode. Code holds literal text only: no tags are offered
  inside it, and turning a paragraph into code strips its tags
  and markers. Sourcecode options (name, type, markers, src) are
  edited through the block's gear marker: see Option markers
  below. A sourcecode block with a src attribute and no content
  shows its file name in place of the missing text, in italics:
  see Option markers below. A src block with content stays
  preserved, since src and content cannot go together. Pasting
  plain text into a code block keeps every line, blank lines and
  leading spaces included, and expands tabs to spaces.
- Pasting plain text follows the paragraph rule: a new paragraph
  starts at a blank line (two or more line endings), and wrapped
  lines join with a space. Pasted plain text carries no tags
  (Bug 412). Pasting rich text translates what maps onto the
  editor's tags -- strong, em, tt, sub, sup, external links,
  lists, and preformatted blocks -- and drops the rest, keeping
  the text (Bug 430).

## Option markers (Bug 420, lists Bug 464)

- A structure with settable options shows one gear marker at the
  start of its first line. In this build sourcecode blocks, lists,
  and tables carry markers; figures get theirs in a later build.
  A table carries a gear per part: the table itself, its head,
  body, and foot sections, each row, and each cell. A cell's
  first line can carry up to three gears, outer part first:
  section, row, cell. The gear shows plain: no underline, no
  special color. It is display only: it never reaches the
  paragraphs, the XML tab, or the file.
- Hovering on the gear shows the block's currently selected
  options, as set, plus a hint that right-clicking changes
  them. For sourcecode: its name, type, markers state, and
  src. For a list: its style, hang indent, and counter, or its
  empty and bare switches plus spacing and indent for a <ul>;
  an <ol> shows its type, start, group, spacing, and indent,
  and a <dl> its spacing, newline, and indent. For a table
  part: a table or a cell shows its align when set; a section
  or a row shows only its anchor. The anchor always shows, on
  its own line, its value when the element has one and
  "(none)" when it has none, and it is settable first in the
  gear menu: renaming one updates the cross-references, and
  deleting a cited one is refused. The pn never shows either:
  it is prep-tool assigned.
- Right-clicking the gear adds an options submenu to the menu,
  with the block's options in alphabetical order. For
  sourcecode: "Source code options" with Markers (on or off),
  Name, Src, and Type. Type offers the preferred values plus
  Custom and None. Clearing a value removes its attribute. A
  src is refused while the block holds content, since src and
  content cannot go together. No anchor is offered in the menu.
  Options set on one line of a block apply to the whole block,
  and the existing read-back and writer carry them to the file.
- For an inline <list>, the gear adds a "List options" submenu
  with Counter, Hang indent, and Style. Style offers the plain
  values counter, empty, hanging, letters, numbers, and
  symbols, plus Custom and None; a format string such as
  "format (%d)" goes through Custom. Clearing a value removes
  its attribute. A style change re-labels the list at once:
  numbers show numbers, letters show letters, the rest show
  bullets. For a <ul>, the submenu offers Bare, Empty, Indent,
  and Spacing. Bare means the list renders with no bullets and
  no indentation; turning it on also writes empty, because the
  schema nests bare inside empty's group and a file with bare
  but no empty fails validation. Turning bare off removes bare
  and leaves empty as it was. Spacing offers normal or compact
  plus None; Indent takes free text (the schema default is
  "3"). For an <ol>, the submenu offers Group, Indent, Spacing,
  Start, and Type, all free text except Spacing (normal or
  compact); the schema gives no value list for type, so it is
  typed in. For a <dl>, the submenu offers Indent, Newline, and
  Spacing. Options set on one item apply to the whole list, and
  the existing read-back and writer carry them to the file. A
  blank line between items does not split the list, and the gear
  stays on the first item.
- The gear cannot be deleted: Backspace, Delete, and selection
  deletion refuse to take it, and cutting or typing over a
  selection covering it keeps the gear and takes the rest. The
  "Show option markers" checkbox at the right end of the button
  row turns the markers off and on; the choice is shared by
  every text editor pane and remembered across runs, and
  toggling it never marks the document changed. Table cell
  gears show on startup as well as after a toggle (Bug 517):
  the sweep that inserts them runs after the edit block that
  built the table closes, so the table frame is there for it
  to find each cell's row and column.
- Copying a sourcecode block carries its option settings with
  it: pasting the copy makes a new block with the same options.
  The anchor never travels with a copy. Src travels when the
  copied block holds no content (Bug 469), so copying a src-only
  block pastes a block with the same src and its placeholder;
  a copy holding real content drops src, since src and content
  cannot go together. Pasting into a code block keeps the block
  it lands in, and its options. Copying a list carries its
  option settings with it too: pasting the copy makes a new
  list with the same style, hang indent, counter, bare, and
  empty, and the gear appears on its first item. The anchor
  and pn never travel with a copy.
- A sourcecode block with a src attribute and no content shows
  an italic placeholder in place of the missing text: a block
  with src "Examples/TestCode.cpp" shows
  `<Src=Examples/TestCode.cpp>` (Bug 466). It sits on the same
  line as the gear when markers show, and alone when they do
  not. The placeholder is display only: it never reaches the
  paragraphs, the XML tab, or the file, and it cannot be deleted
  or typed over -- typing beside it writes normal code text, and
  the placeholder goes when real content arrives. Right-clicking
  the placeholder opens the options menu, like right-clicking
  the gear. A src block loaded from a file is modeled, so it
  shows its gear and placeholder like any other block (Bug 467);
  a src block with content stays preserved, since src and
  content cannot go together.

## Figures (Bugs 388, 389, 390)

- A figure stands alone as its own blocks: a bold caption block
  (the `<name>`), then the artwork lines, shown monospace and
  indented like code. A bare `<artwork>` and a bare `<artset>`
  directly in a section edit the same way, without a caption.
  The Blocks button menu and the right-click menu offer ASCII
  art and Figure alongside Source code, alphabetical; the
  Blocks button label names the kind the cursor is in, or the
  picked kind. A new figure leaves the cursor in its empty
  caption, ready to type it; a new artwork block starts with
  type "ascii-art". Pressing the Blocks button again stops the
  figure: the cursor moves to a new plain paragraph after it.
  A Remove action turns a figure back into plain paragraphs,
  one per line, with the caption as a plain paragraph. Every
  version goes with the figure. Removing a figure holding a
  version kept only in the document asks first, since there is
  no other copy of it; a version included from a file needs no
  question, because the file still holds a copy.
- Artwork holds literal text, like code: Enter and Shift+Enter
  both add a line and never leave the block, the Tab key
  inserts four spaces, and pasting keeps every line, blank
  lines and leading spaces included, with tabs expanded. No
  tags are offered inside a figure, captions included: a
  caption keeps the markup it came with, and typing extends
  it. Enter in a caption moves to the artwork lines instead
  of splitting it; pasting into a caption keeps one line, with
  line endings as spaces.
- An artset holds several versions of the same artwork, and
  one version shows at a time. A picker beside the Blocks
  button lists the versions by type and switches between
  them, saving the text showing into its version first. The
  picker is greyed out everywhere except a figure holding
  more than one version. A figure opens on its first text
  version, so it opens editable.
- SVG versions are preserved, never edited: an SVG version
  shows as one placeholder line, and typing, deleting, and
  pasting in it are blocked, while navigation, selection, and
  copying still work. The preserved SVG element, comments and
  namespace declarations included, writes back unchanged.
  Generating SVG from ASCII art (aasvg, Bug 419) is not wired
  up yet.
- First-version limits: figures edit only at section level;
  a figure inside a list item, a table cell, or a blockquote
  stays preserved, untouched. A figure holding sourcecode, a
  preamble or postamble, an index entry, a version with a src
  attribute, namespaced elements, or an entity reference stays
  preserved, whole. Preamble and postamble are never offered
  for creation (Bugs 391, 392). Figure, artset, and artwork
  options (anchor, name, type, src, and the like) are kept
  when a file is read and written back, but not edited yet:
  they belong to Bug 445, including figure anchors.

## Tables (Bugs 395, 396, 397, 399, 400, 401, 402)

- A table shows a bold caption block (the `<name>`) over a
  grid, with header cells bold. The grid spans the full width
  of the text pane, so cells are wide enough to click into;
  the width is display only and never reaches the file. The
  Blocks button menu and the right-click menu offer Table
  alongside ASCII art, Figure, and Source code, alphabetical.
  Inserting a table asks for the number of body rows and
  columns, and whether a header row and a footer row are
  wanted (two body rows by two columns, plus a header row, to
  start; the footer row starts off), and leaves the cursor in
  the empty caption, ready to type it. A table always shows a
  caption block, even when it has no caption, so a caption can
  be typed for a bare table; an empty caption writes no
  `<name>`.
- Cells hold inline text and tags, filtered to the cell set:
  BCP 14, line break, comment reference, emphasis, external
  link, index entry, relative reference, strong, subscript,
  superscript, teletype, Unicode, and cross-reference. The
  tag menu works in cells, and typing works as in a paragraph.
  Enter in a cell inserts a line break, since a cell holds one
  paragraph; Tab moves to the next cell, and Shift+Tab to the
  previous one. Enter in the caption moves to the first cell.
  Pasting into a cell or a caption keeps one line, with line
  endings as spaces; rich-text paste is flattened to plain
  text there. Lists, source code, figures, and comments cannot
  be started in a table.
- The right-click menu in a table offers Insert row above and
  below, Delete row, Insert column left and right, Delete
  column, Body rows..., Columns..., Header row, Footer row,
  Merge cells, and Split cell. An inserted row takes
  the section of the cursor's row, and its cells take the
  header flag of that section; the last body row and the last
  column cannot be deleted, and deleting the only header row
  removes the header section. Body rows... sets the body row
  count, leaving the header and footer untouched, and
  Columns... sets the column count of the whole grid, each
  showing the current count; growing appends empty rows or
  columns at the end, shrinking removes them from the end.
  Header row and Footer row are checkable: toggling one on
  adds a one-row section, toggling it off removes the section,
  with no question asked, since one Undo restores it. Merging
  needs a selection spanning two or more cells: the merged
  cell keeps the top-left cell's text, header flag, and kept
  attributes, the other cells' text joins it, and their kept
  attributes are dropped. Splitting returns a spanned cell to
  single cells: the text stays in the top-left cell, the
  header flag is copied to every new cell, and the kept
  attributes stay on the top-left cell only. Spans read from a
  file (colspan and rowspan) show in the grid and write back
  unchanged.
- A Remove table action turns a table back into plain
  paragraphs: the caption as a plain paragraph, then one
  paragraph per cell, in row order. Removing a table asks
  first, since the table is kept only in the document and
  there is no other copy of it. A selection crossing a table
  boundary cannot be deleted or typed over: Qt would delete
  the whole table, so those keys do nothing, while a selection
  inside the table, or one covering it whole, still works.
- First-version limits: tables edit only at section level; a
  table inside a list item or an aside stays preserved,
  untouched. Cells hold inline content only: a cell holding
  block content (a paragraph, a list, source code, a figure,
  or artwork) keeps the whole table preserved. A table
  holding an index entry, two or more bodies, a comment
  inside a cell or between cells, namespaced elements, or an
  entity reference stays preserved, whole, as does a table
  whose rows span different widths. Comments between the
  table's members, or between its rows, are kept as preserved
  blocks just before the table. The deprecated texttable,
  ttcol, and c elements are preserved, never offered for
  creation (Bugs 394, 398, 403). Table, section, row, and
  cell options are editable through the table gears (Bug 448):
  the table gear offers its anchor, its alignment, and its
  caption. A cell's first block carries ONE gear, whose menu
  holds a submenu for every part that applies there: the
  section when it is the first cell of the first row of its
  section, the row when it is the first cell of its row, and
  the cell itself, always. So the header gear shows the Table
  head, Row, and Header cell submenus; a row gear shows the
  Row and Cell submenus; a plain cell gear shows only its
  own. A section or a row submenu offers only its anchor; a
  cell submenu offers its anchor and its alignment. The
  gear's hover text lists every part it covers. Colspan and
  rowspan are not in the gears: they are already handled by
  the grid editing.

## Output formats (Bug 320)

- The bottom of the main window holds three checkboxes: Text, PDF,
  HTML. Any combination may be checked, and one run of xml2rfc
  produces every checked format. The choices are remembered across
  runs. With nothing checked, Run xml2rfc asks for at least one
  format instead of running.
- One run means one xml2rfc call with every checked format flag:
  when a checked format cannot be produced (the PDF extras are
  missing, for example), the run produces nothing, and the log
  shows why.
- When the saved draft carries a src attribute on a sourcecode
  or artwork element, the run also passes --allow-local-file-
  access, which xml2rfc needs (since 3.27.0) to read the external
  file. Drafts without src run without it (Bug 465).

## Recent files (Bug 319)

- The File menu lists the last four drafts opened, newest first,
  between Open and Save. Picking one opens it, after the usual
  unsaved-changes check. Files that are no longer there drop off
  the list.
- The start wizard offers the last draft opened: its file is
  already shown on the first page, so opening it again needs no
  Browse. The choice itself stays "Create a new draft" until the
  user picks "Open a draft file".

## Help menu (Bug 323)

- The Help menu holds Legend, Feedback, and About InternetDraftsEditor. Legend
  lists what the text editor's special characters mean: the Line
  Break and Vertical Space markers, the Index Entry marker, the
  soft line break, and list markers. Feedback opens the GitHub
  issues page (https://github.com/RiverExplorer/InternetDraftEditor/issues)
  in the default browser. The first time the app starts, a
  one-time note points at Help > Feedback. About shows the version, a
  one-line description, the copyright, and the MIT license text,
  in the same shape as the CoOp editor's About box, plus a
  clickable link to the project page
  (https://github.com/RiverExplorer/InternetDraftEditor, Bug 518).

## Author role (Bug 324)

- The author page holds a Role box with two radio buttons,
  Author and Editor. An author carries no role attribute; an
  editor carries role="editor", the only value the schema
  allows. A file that says role="author" reads as an author,
  and saving drops the invalid attribute.
- Saving already required at least one author: Save refuses a
  draft with no authors, and the last author cannot be removed.

## View buttons (Bug 325)

- A View button sits under each of the Text, PDF, and HTML
  checkboxes. A button is enabled only when its format is
  checked and its output file exists beside the saved draft.
- Text opens in Notepad, HTML in the default browser, PDF in
  the default PDF viewer. When no default PDF viewer is set,
  viewing a PDF says so instead of opening anything.

## xml2rfc legend (Bug 326)

- The Help menu holds an xml2rfc Legend: a tabbed panel
  describing the language, one tab per topic (Document, Front
  Matter, Sections & Text, Inline Marks, Lists, Figures & Code,
  Tables, References). Each tab lists its elements with a
  plain-word description and where each element goes;
  deprecated elements are marked, with what replaces them.

## Draft name and version (Bug 314)

- The wizard's title page asks for the draft name and the
  version in two fields: the name (no spaces) and the version
  (two digits). Joined with a dash, they become the XML file
  name and the draft's docName. The version is checked again
  when the draft is created: a missing or short version sends
  the wizard back to the title page instead of creating the
  draft (Bug 414).

## Wizard redo (Bug 413)

- The wizard asks the author first, then the title: choice,
  author, title, folder, create. The draft name and version
  sit on the title page, not on a page of their own. File >
  New Draft starts at the author page, skipping the choice
  page as before.
- The draft name prefills once, when the title page is first
  shown, as draft-<last name in lower case>, from the same
  last-word derivation the document build uses. The version
  prefills with the actual value 00. Both stay editable.
  Going Back and changing the author name refreshes the
  draft name only when it has not been edited by hand; a
  manual edit is never overwritten.
- The author's own information (full name, organization,
  email) is remembered between runs and prefilled on the
  author page; it is still asked, not assumed, and creating
  a new draft updates the stored values. Adding a co-author
  in the editor is a different person and is not prefilled.
- The folder a draft is created in is remembered between
  runs and prefilled on the folder page (Bug 457). With
  nothing stored yet, the home folder stays the default.

## New version of a draft (Bug 435)

- The start wizard offers "Create new version of existing
  draft": pick a draft file, and the wizard copies its entire
  contents into a new version with the version number bumped
  by one. The old file is untouched, and the new version
  starts unsaved, like any wizard creation.
- The author, title, and folder pages prefill from the
  selected draft: the first author, the title, the bumped
  draft name and version, and the source file's folder. The
  draft name is prefilled and read-only: a new version keeps
  the original draft's name. The rest stay editable, and
  going Back never overwrites a manual edit. Section anchors
  are kept from the old version, and the date takes the
  current date on save, as always.
- The only exception to the read-only name is a draft
  becoming a working group product, handled by a manual copy
  of the draft, outside the new-version flow.
- The loaded draft's name must end with a two-digit version.
  A draft at version 99, or one whose name has no two-digit
  version, cannot be versioned: the wizard says so and stays
  on the choice page. A file that cannot be loaded is refused
  the same way.
- Title and first-author edits apply only when changed, so an
  unchanged title keeps its line breaks and an unchanged
  first author keeps its full record. File > New Draft has
  no choice page and offers no new-version option.

## Long literal lines (Bug 458)

- On Save and Save As, before writing, the editor scans the
  literal content for lines longer than 69 characters: every
  sourcecode block and every ASCII-art artwork, modeled and
  preserved alike. xml2rfc never wraps literal content and
  renders it with a 3-character indent, so a longer line draws
  warning L106 when the draft is rendered. SVG artwork is not
  checked, and prose is not checked: xml2rfc re-wraps prose.
- When an over-long line is found, one warning lists each
  offending line with its block kind, section, line number
  within the block, and length. "Go back" aborts the save and
  returns to editing; "Save anyway" writes the file. The
  check is a warning, not a save gate, and the writer never
  wraps literal lines: wrapping would change the content.
  With no over-long lines, the save proceeds silently.

## List rebuild indent (Bug 459)

- Pressing Enter in a continuation paragraph starts the next
  item, and the list is rebuilt so the numbers follow the
  document. Rebuilding takes every item block off the list
  and adds it back, and removing a block from a list sets
  its indent to 1, so the rebuilt items sat one level deeper
  than the new block and the bullets no longer aligned. The
  rebuild now restores indent 0 on every item afterwards.
  The continuation paragraph keeps its indent 1, lined up
  with the item text.

## Paragraph spacing (Bug 460)

- Plain paragraphs show one blank line between them: every
  plain paragraph block carries a bottom margin of one line
  of the edit font's spacing, so reloaded paragraphs read
  the way typed ones do. The empty block typed between
  paragraphs never reaches the file, and on reload nothing
  drew the gap, so paragraphs sat tight together. List
  blocks, sourcecode lines, figure blocks, and table blocks
  carry no margin and stay tight. The spacing is display
  only: the writer never reads block formats, so the saved
  XML is unchanged. One line is the spacing; when more space
  is needed, a Vertical-Space is inserted.

## Outline subsection menu (Bug 446)

- Right-clicking a section in the outline shows a menu: Add
  Subsection, Insert Subsection, Remove, Move Up, Move Down,
  Promote Subsection, Demote Subsection. The move and remove
  items say Section for a top-level section and Subsection for
  one below it (Bug 512). The clicked section is selected first,
  so the menu always acts on what was clicked, and the menu
  appears only on section items. The Add, Insert, Remove,
  Up, and Down buttons below the outline stay as they are.
- Add Subsection appends a new child as the last child of
  the clicked section; Insert Subsection makes a new first
  child. The new subsection is titled "New Section" with an
  empty body, and selection moves to it. Remove Subsection
  removes the clicked section with its subsections, after
  the same confirm the Remove button uses, naming how many
  subsections go along. Move Subsection Up and Down are the
  buttons' moves: a required section itself never moves, but
  a free section may move past a required neighbour (Bug 512).
  Promote Subsection makes the section the sibling right after
  its parent; Demote Subsection makes it the last child of its
  previous sibling. A section's own children move with it.
  Promote is off at the top level; Demote is off for the first
  section in a list and for a top-level required section, which
  must stay at the top level. There is no limit on nesting
  depth, and moving a section never renames its anchor.

## Enter in a paragraph (Bug 461)

- Enter in a plain paragraph makes a new line in the same
  paragraph, not a new paragraph: one sentence per line
  stays one paragraph, which makes editing easier. Only
  Enter on an empty line, the second of two Enters in a
  row, starts a new paragraph. The typed line breaks are
  real newlines in the file inside the paragraph element,
  so what is typed is how the paragraph shows when the
  file is loaded again. A file whose paragraph source is
  wrapped shows those wraps as line breaks on load, since
  the file's layout is the editor's layout. The rendered
  output is unchanged: xml2rfc reflows paragraph text.
  A forced break that renders as a break still comes from
  the Break menu item. Lists, sourcecode, artwork, figure
  captions, and table cells keep their own Enter behavior.

## Author questions (Bug 315)

- The author page asks the optional questions too: phone, URI,
  and a postal address (all nine structured parts: post office
  box, street, extended address, city area, city, region, code,
  country, sorting code; or free-form lines — see Front Matter
  below). Initials and surname were already on the page. The
  full name is the only required question, marked with '*', with
  a '* Required' footnote below the form. The save check already
  required every author to have a name.
- The postal address holds the first of each structured part;
  further postal details (a second street, for example) are kept
  when saving, as are a fax number and further email addresses.

## References (Bugs 404-411)

- The outline grows under Back: References holds one node per
  list (by name), each list holds its entries, and each entry's
  label leads with the words that cite it (the text the author
  tagged when they marked it as a reference, up to three), then
  the anchor and title; an entry nothing cites keeps its plain
  label. The authors no longer list under an entry: they edit
  on the entry form. A file with a single
  "References" list loads as one editable list. A new draft
  starts with the IETF form of the lists: one outer
  "References" list holding a "Normative References" list and
  an "Informative References" list. The normative list starts
  with RFC 7991 (the document that defines this format); the
  informative list starts with this editor's public
  repository, authored with the draft author's public details.
  Either list, and either entry, can be removed when not needed.
- A list page edits the list name and offers Add Entry, Add
  Nested List, Remove List (which removes its entries too), and
  Up and Down to reorder the lists among their siblings. Add
  Entry asks for the anchor and refuses one that is already
  used, naming what provides it. When
  an included reference is picked in the outline, the list page
  names it and offers Remove Included Reference for it.
- A nested list shows under its parent list in the outline, and
  edits on the same list and entry pages as a top-level list.
  A list holds either nested lists or entries, never both, so
  the schema lets Add Entry and Add Nested List take turns: on
  a list with nested lists only Add Nested List is offered, on
  a list with entries only Add Entry, and on an empty list
  either, with the first child deciding. The Move-to-list
  pulldown names a nested list under its parents, such as
  "References > Normative References", and offers only lists
  that can hold entries.
- The References page carries a note to the editor explaining
  the two lists: normative references are the standards or
  documents that must be read and implemented to conform to
  the specification; informative references are background
  reading, examples, or helpful context that are not required
  for implementation.
- An entry form holds the anchor (required, marked with '*'),
  the target web address, the title, the date (day, month, and
  year as free text, since a reference date may be vague or a
  range), the stream pulldown (blank, IETF, IAB, IRTF,
  independent, editorial), a series info table (series name and
  value, with Add and Remove), a pulldown to move the entry to
  another list, the authors, and Add Author, Remove Author,
  Remove Entry, Up, and Down.
- A reference's authors edit on the entry form itself: a list of
  the authors (a reference can have more than one) with the full
  name, initials, surname, organization, email, phone, URI, and
  role of the selected one. The postal address a reference
  author carries in the file is kept on save but not edited on
  the entry form. A draft author still edits on the author page,
  with the postal address; a draft still needs at least one
  author. A reference author can always be removed.
- Saving requires every entry to have an anchor, and anchors
  must be unique among the reference lists and entries. The
  check covers only reference anchors, since the editor does
  not edit section anchors; a clash with one of those is left
  to schema validation. The check walks nested lists too, and
  sees inside a group include: an anchor carried twice (a group
  include providing the same anchor as another reference, BCP14
  providing RFC2119 alongside the RFC2119 include) is reported
  at validation, naming both references, and one must be
  removed. Neither a manual entry nor an include is removed
  while a cross-reference still cites it: the editor says how
  many citations stand.
- Kept, not edited yet: reference groups, the notes after an
  entry's front matter other than the annotation (refcontent
  and the like), the quoting and derived-anchor attributes,
  and the series info attributes beyond name and value. They
  round-trip on save.
- The entry form has an Annotation field (Bug 404): extra prose
  shown after the entry in the rendered output. Typing creates
  the <annotation> element after the front; clearing the field
  removes it. Plain text only: an annotation holding inline
  markup loses that markup when edited.
- Display names (Bug 405): a list page holds a Display names
  table, one row per mapping, with Add Display Name and Remove
  Display Name. A mapping names a reference by its anchor and
  the name to show instead, such as TCP for RFC793, and is
  written as <displayreference target="RFC793" to="TCP"/>
  before the first reference list. One mapping per reference:
  adding a second one for the same anchor warns and adds
  nothing. The display name edits in the table; the reference
  does not — changing the reference is a remove plus an add.
  A plain bracket cross-reference to a mapped anchor shows
  the display name, [TCP] instead of [RFC793], in the body
  text and in the reference list; section and counter forms
  keep the anchor. A mapping whose reference is removed stays
  in the file until removed from the table.
- Author names may hold any text, including non-ASCII names.
  The ASCII fallback attributes (on the author, the
  organization, and the postal parts) are kept when a file is
  read and written back, but not edited yet.
- Make a reference from the text (Bug 484): highlight text in
  the body, right-click, References. New > Normative or
  Informative creates the entry in that list and cites it;
  Internal picks a section, figure, or table by title and
  cites it — a section shows its outline number in front of
  its title, as in the outline ("1.2 Terminology (Section)");
  Existing cites an entry already in the picked
  list. The highlighted text stays as the cross-reference's
  content: <xref target="RFC4324">[iCal]</xref> shows as
  [iCal] and points at RFC4324. The anchor carries no
  brackets (iCal); the citation shows as [iCal] in the
  rendered output. The tag menu's Cross-Reference, External
  Link, and Relative Reference already wrap a selection the
  same way. After a reference goes in, the caret sits just
  after it in plain text, so typing continues after the link
  instead of joining it; the same holds for a link wrapped
  from the tag menu. The caret keeps that behaviour whenever
  it lands just after a link — clicking at the end of the line
  or pressing End, for example — while a caret inside a link
  is read-only: typing or deleting there is refused, and the
  editor says to right-click the reference and choose Rename
  instead (Bug 514 v2). Rename prompts for the new text,
  prefilled with the current one, and replaces the link's
  text while keeping the reference itself: same target, same
  underline and colour. The reference is re-applied to the
  new text explicitly (Bug 514 v3), so it survives in the
  saved file: insertText alone does not reliably carry the
  reference through in every Qt build. Selecting a whole reference and
  pressing Delete still removes it. An index entry is a marker
  the same way (Bug 503): it shows as a circled i (ⓘ) beside
  the word being indexed, never the item text again, so the
  word shows once. Highlighting a word and choosing Index
  Entry prefills the Item prompt with it; a sub-item is a
  narrower term filed under the item, e.g. item "RTT",
  sub-item "Round Trip Time". The marker is read-only: typing
  or deleting into it is refused, and the editor says to
  right-click it and choose Edit index entry to change the
  item or sub-item. Selecting the whole marker and pressing
  Delete removes the entry. An RFC number (RFC4324, with or without spaces
  or brackets) is written as an xi:include under the picked
  list, with xmlns:xi declared on the root when missing:
  xml2rfc pulls the full reference from its database, so no
  details are fetched or stored. Any other name asks for the
  name, prefilled from the highlight, and creates a
  placeholder entry holding the anchor with a front whose
  title reads "Please Supply Title" and whose author reads
  "Please supply Author", to be completed in the entry form;
  the author carries only the fullname, no invented initials
  or surname. The entry is valid as it stands. When the
  author enters the data in the references section, the
  editor updates the <xref> values: changing a reference
  entry's anchor renames the target on every <xref> that
  cited it. When the anchor is already provided, by a manual
  entry, by an include, or inside a group include (BCP14
  provides RFC2119), the existing reference is cited instead
  of a duplicate, and it is moved into the list the author
  chose first (a group include moves as a whole, carrying
  every anchor its file provides); an RFC include is fetched
  first so the anchors it provides are known before it is
  added.
- Cross-reference forms: every cite (References > New,
  References > Existing, and the Bibliography dialog's
  Insert) asks how the reference should appear, offering
  the forms published RFCs actually use. Bracket puts an
  empty xref after the highlighted words, so the words stay
  plain and the reference follows them: "means [RFC793]"
  (RFC 8933). Link text wraps the highlighted words as the
  link's content, as before. Section of asks for the section
  number and cites it as "Section 4.2.11 of [RFC8446]"
  (RFC 8773). Section bare cites it as just "Section 5.4",
  with no bracket shown (RFC 8933). Counter shows only the
  target's number, for a section, figure, or table. The
  forms the spec defines but no published RFC uses
  (format="title", format="none", sectionFormat "comma" and
  "parens", pageno, relative) are not offered. In the
  editor the empty forms show as the words they render as,
  not as the placeholder glyph: a bracket shows "[RFC793]"
  after the words, a section "of" shows "Section 4.2.11 of
  [RFC8446]", a bare section shows "Section 5.4", and a
  counter shows the bracketed target (the number itself is
  resolved when the document renders). Hovering the marker
  names the target, the section, and whether it is a counter
  or a bare section. A file with no back
  matter gains the nested IETF form (outer References with
  both nested lists, empty). A section, figure, or table
  with no anchor gets one derived from its title when
  picked. The Existing branches list included references
  too, by the anchor their href carries. A file with
  xi:include entries passes File > Validate and the check on
  opening once the includes are resolved: the editor fetches
  each bibxml file over HTTPS into the bibliography cache
  (Settings > Bibliography...) when the file is opened or
  validated, and leaves out the errors a resolved
  include explains, so the report stays about the file
  itself and its line numbers match the file. An include
  that could not be fetched keeps its errors, with a plain
  note saying so. The file itself keeps the xi:include.
  Included references list in the outline under their list;
  a cached one shows its anchor first, then its real title
  ("RFC4324 - Calendar Access Protocol (CAP)"), an unfetched
  one the anchor with "(included)". Clicking one shows the list, since an
  included reference has no entry form; the list page offers
  Remove Included Reference for the selected include. The top
  of the entry page, and the top of the list page for a picked
  include, carries Normative and Informative radios: picking
  one moves that reference into the Normative References or
  Informative References list under its outer list, creating
  that list when it is not there yet. A list holds nested lists
  or entries, never both, so moving the first entry leaves the
  outer list mixed until the last direct entry moves; the file
  is schema-valid again once every entry has a section.
  Settings > Bibliography... manages the cache: the table lists
  every cached entry with its title and anchor, the target name
  this document cites it by (blank when the document does not
  cite it — the name belongs to the document, not the cache),
  the address it was fetched from, when, and its size. Refresh
  fetches the selected entries again, Delete removes them,
  Clear All empties the cache. Add fetches a new entry: type an
  RFC number (2119, or RFC 2119) and it is looked up at
  bib.ietf.org, or type a full bibliography address. Insert puts
  a cached entry into the document: pick a row, type the target
  name (prefilled with the entry's anchor; it must be one of the
  anchors the entry provides), pick Normative or Informative, and
  press Insert. The include goes into that References list and a
  cross-reference to it is put at the cursor, showing the anchor
  as its words, so the reference reads instead of showing a bare
  marker. If the target box is empty, the dialog asks which of
  the anchors the entry provides to cite. If the document already
  provides that target, it cites the existing reference instead of
  adding a second copy; if
  another anchor the entry carries is already provided, it refuses,
  since adding the include would define that anchor twice. The Target
  column fills in for an entry the document cites, whether the
  citation came from a new include or from an existing entry (a
  manual entry provides its anchor too). When the cursor is not in
  the text, the include is added and the dialog says to cite it from
  there. The cache
  lives under %APPDATA%\RiverExplorer\InternetDraftEditor\
  Bibliography; a cache from the old CoOp folder is carried
  over the first time the editor runs. Browse moves it (Copy
  carries the entries over, Delete throws them away).
  View Published... looks up published documents instead of
  the draft: RFC asks for the number and offers the XML,
  HTML, text, and PDF formats that actually exist for that
  RFC (an old RFC has no XML or PDF); ID asks for the title
  and the version separately and offers the formats that
  exist for that draft. Check finds the formats; View
  downloads the picked one to a temporary file and opens it
  with the system viewer. ID Difference asks for the draft
  name and two versions, fetches the two text files, and
  shows the differences in a window: three spaces before a
  line that is the same in both versions, the older version's
  number before a line only the older version has, the newer
  version's number before a line only the newer version has.
  All three dialogs remember the last values typed and the
  last format picked; Clear empties the dialog and forgets
  them. The Internet-Draft dialogs are wider, since draft
  titles are often wide. The differences window has Save As, defaulting to
  title-old-new.txt in the Documents folder.

## Front Matter (Bugs 332-359)

- The outline starts with Document Settings, a top-level entry
  holding the title settings, the series info, the area,
  workgroup, and keyword lists, and the first note. The title
  text moved there from its own outline entry, so each title
  field has exactly one editor.
- The title settings are the title text, the abbreviated title,
  and the ASCII title. A line break inside a loaded title is
  kept at its character offset and written back in place; the
  title field stays single-line, and a note under it says how
  many breaks the title holds. Editing the title text keeps the
  breaks at their offsets, clamped to the new length.
- The abbreviated title is required, at most 40 characters
  (Bug 455). A new draft prefills it with the title when the
  title is 40 characters or fewer. The ASCII title is required
  when the title holds non-ASCII characters.
- The series info table holds series name and value rows, with
  Add and Remove. A row named Internet-Draft takes its value
  from the draft name when written, so the two always match;
  its value cell is read-only and shows the draft name. The
  other rows are editable, and their kept attributes (the ASCII
  forms, the status, and the stream) round-trip. Saving
  requires every row to have a name and a value, except the
  Internet-Draft row (whose value is generated) and a row
  carrying a status, which may use an empty name — the
  status-row form of RFC 7991 section 2.47.
- Areas, workgroups, and keywords each edit as one value per
  line; blank lines are skipped. The values carry no attributes.
- A new draft created from scratch prefills one series info
  row named Internet-Draft and one area, General (Bug 454).
  Workgroups and keywords start empty. Drafts created from a
  template keep the template's front matter.
- A note edits with a plain-text name, a remove-before-
  publishing checkbox, and a text body (paragraphs and lists,
  like a section body). With no note, an Add Note button
  appears; with more than one note, the first edits here and
  the others are kept when saving, until the multi-note design
  is decided. A note holding source code, a figure, a table, or
  a name with inline tags stays preserved whole.
- A new draft created from scratch prefills one note named
  Created With, holding the text InternetDraftsEditor and the
  application version (Bug 456). A GitHub repo URL joins it
  later.
- The postal address on the author page holds all nine
  structured parts, each with its kept attributes, plus a
  free-form lines mode: one address line per line. The two
  forms never mix. Switching from structured parts to lines
  joins the non-empty parts in order; switching from lines to
  structured parts asks first (defaulting to No) because it
  discards the lines. A file mixing both forms models the kind
  that comes first and keeps the rest.
- Kept, not edited: the generated boilerplate and the table of
  contents (xml2rfc fills them in; the editor never generates
  them), and the fax number. They round-trip on save.

## Required sections (Bug 422)

- The three required sections — Introduction, Security
  Considerations, and IANA Considerations — are locked in the
  editor: their titles cannot change, they cannot be removed,
  and they cannot be reordered. A section is recognized by its
  exact title, at the top level of the middle matter only; a
  subsection with a matching title stays free.
- When a required section is selected, its title field is
  disabled, the Remove, Up, and Down buttons are disabled, and
  the page says the section is required. A free section may
  move past a required neighbour (Bug 512): the required
  sections keep their relative order, only their numbers shift
  as free sections move past them. Add and Insert stay enabled:
  adding a section never moves a required section relative to
  an existing one, and Add with no section selected puts the
  new section at the end of the free body sections, before the
  required back-matter (Bug 512), so it lands where it can be
  moved.

## Appendices in the outline (Bug 513)

- The appendix sections in `<back>` show in the outline under
  Back, after References, like sections: "Appendix A" for the
  first, "Appendix B" for the next, and "A.1" for a subsection
  of Appendix A, matching what xml2rfc prints. Their
  subsections nest under them.
- An appendix edits, moves, and is added, inserted, removed,
  promoted, and demoted like a section, against the back's own
  list. A top-level appendix never locks as a required section,
  whatever its title. Adding a section after a selected appendix
  adds another appendix after it; Add with no section selected
  still appends to the middle.
- Appendix anchors are cross-reference targets too, lettered
  (A, A.1) in the target list.
- A section renamed to a required title locks at once. A draft
  missing a required section is not flagged: the locks are
  editor enforcement on recognized titles, not a save check.

## Prefilled sections (Bug 427)

- New drafts created from scratch prefill seven middle
  sections, in the recommended order: Introduction,
  Terminology, Implementation Status, Privacy Considerations,
  Security Considerations, IANA Considerations,
  Acknowledgements. Status of This Memo, Copyright Notice,
  and Authors' Addresses are generated by xml2rfc, not
  prefilled here. Appendices are not prefilled: back
  sections are preserved, not modeled (Bug 453). Drafts
  created from an IETF template keep exactly the template's
  sections, and an existing draft without these sections is
  left alone: they are optional, and the author can always
  add them with the Add button.
- Each optional section starts with an XML comment saying it
  is optional and what it is for, so a newcomer learns the
  section exists. The author deletes the section when it is
  not needed.
- A recommended section's title cannot change, but unlike a
  required section it can be removed and moved freely. A
  section is recognized by its exact title, at the top level
  of the middle matter only, so a section the author adds with
  one of these titles locks its title the same way. When one
  is selected, the page says its title is fixed but it can be
  removed or moved. Terminology and Acknowledgements carry no
  title lock: they can be renamed, removed, and moved freely.

## IETF templates (Bug 431)

- The wizard's first page offers a third choice: create from an
  IETF template, with a pulldown for Standard, Bare, or
  Annotated. The author and title pages still run, and
  what is entered there replaces the template's placeholders
  (docName, title, first author); the rest of the template
  loads as the starting draft, markers and all, for editing.
- Standard holds examples of the most commonly used features,
  marked [REPLACE] and [CHECK] where they need editing. Bare
  holds the minimum that validates: an empty title and author,
  one empty section, and no abstract (a draft from Bare gets
  the example-text prefill). Annotated holds most features,
  with comments explaining customization and formatting.
- The three template files ride inside the program as Qt
  resources (templates.qrc) and are read at creation time.
  The template's DOCTYPE and header comments drop silently;
  they are scaffolding, not draft content.

Not in goals #1-#3: reading .coop files, the CoOp editor as a child
process, the external reference lookup, the HTML help pages, and
reading any RFCXML document for editing (a later goal). The
external lookup will draw on the IETF's repository of references
that work with xml2rfc (Bug 428).

## Sourcecode copy, paste, delete, and blank lines (Bug 469)

Copying a sourcecode block that names a file (`src`) and pasting
it back used to lose the file name and leave the placeholder text
`<Src=...>` inside the block as literal text. Six fixes, all in
`TextEditor.cpp` except where noted:

- Copy: a placeholder fragment whose text has been dirtied --
  text typed or pasted into the display-only placeholder region,
  so the fragment reads `<Src=pastedExamples/...>` while its
  stored file name is still `Examples/...` -- is recognised by
  the fragment's `<Src=` start and its whole display text is
  stripped from the copy. The file name is still carried only
  when the copied text is empty after stripping: copied text is
  real content, and a file name forbids content. The sweep (next
  point) and the copy both had to change: the sweep already
  cleared the placeholder property from a dirtied fragment and
  kept its text as plain content, so the copy alone would never
  have reached the document state. Accepted edge case: a fragment
  starting with `<Src=` is treated as display text, so typed text
  that only looks like a placeholder is stripped too.
- Sweep: a dirtied placeholder fragment is deleted instead of
  surviving as plain text, and the clean placeholder is stamped
  back in the same pass, so the block shows its file name again.
- Paste: when the cursor's block still holds real text, the paste
  gets a fresh empty block after the paragraph instead of
  swallowing its text; an empty paragraph is used as it stands.
  When the block before the paste point is a sourcecode block
  with the same options, the paste would have merged into it as
  an invisible continuation with no gear and no placeholder, so a
  blank paragraph is left between the two elements as their
  separator and the pasted block stands as its own element with
  its own gear and placeholder. Deleting that separator later
  merges the two elements again; that follows from how the
  document model groups adjacent identical blocks.
- Delete: Backspace, Delete, and Cut queue a check that runs
  once the deletion has landed. When the deletion leaves the
  cursor's sourcecode element with no real content, the element's
  blocks are removed, so no empty `<sourcecode type="c++"/>`
  survives. A block that still names a file is never removed: a
  file-only block is a real block and shows its placeholder. A
  fresh empty block from the Blocks button is never removed
  either: it never held content. The check is hooked in the
  event filter, not in the option-marker sweep, because the sweep
  cannot tell a fresh empty block (which must stay) from an
  emptied one (which must go), and the sweep runs after every
  change. The queue point sits just after the key branches: a
  plain deletion key returns from the Enter section below and
  never reaches the end of the event filter, so a queue point
  there would never fire for it.
- Copy with a blank line: a selection that reaches into a blank
  line after a sourcecode block still carries that block's
  options. Empty blocks are ignored when deciding whether the
  selection lies wholly in one sourcecode element. The blank
  lines themselves stay in the document and in the copied text.
- Save keeps blank lines: blank paragraphs are collected on save
  like any other paragraph, so the file gets an empty `<t>` for
  each one. This is a readability matter on its own, not a copy
  matter: blank lines are how the writer tells where one thing
  ends and the next begins while typing, and xml2rfc formats them
  as blank lines. `DocModel.cpp` already wrote an empty `<t>` for
  a blank paragraph and the reader already accepted one on reload.
- Bulleted list: a paragraph holds its lines separated by soft
  breaks, so the five lines of one `<t>` are one block in the
  editor. The Bulleted list button now splits the cursor's line
  out of its block first, so the bullet lands on the line the
  cursor is in, then marks that line as the list item. Bulleting
  the next line joins the same list, so bulleting lines one at a
  time makes one list instead of one list per line. Numbered and
  definition lists split the same way; a definition list does not
  join (it has no list object to join).

## Build

Open `InternetDraftsEditor.pro` in Qt Creator with the "Desktop Qt
6.12.0" kit (llvm-mingw 64-bit) and build. Besides Qt Core, Gui, and
Widgets, the build needs the static libxml2 in
`RelatedProjects/include` and `RelatedProjects/lib` (see
`README-libxml2-mingw.md` there): the project file points at it
with a relative path, and defines `LIBXML_STATIC`.

## Run

xml2rfc must be installed for File > Run xml2rfc: `pip install
xml2rfc` (Python). If `xml2rfc` is not on the PATH, set the full
program path with Settings > Set xml2rfc Path...

For validation, nothing needs placing by hand: the program
installs the schemas itself on startup (see Goal #2 above). A
`schemas` folder beside `InternetDraftsEditor.exe` is only a
fallback.

## Files

- `main.cpp` — entry point, theme, start wizard.
- `StartWizard.h`, `StartWizard.cpp` — the start wizard and its pages.
- `DocModel.h`, `DocModel.cpp` — the document, the RFCXML writer,
  and the reader, which keeps unedited content verbatim (goal #3)
  and reads paragraphs as structured runs (Bug 322).
- `DocValidator.h`, `DocValidator.cpp` — in-app RelaxNG validation
  with libxml2 (goal #2).
- `TextEditor.h`, `TextEditor.cpp` — the two-tab text editor: rich
  text with the smart tag menu, and the read-only XML tab (Bug 322).
- `MainWindow.h`, `MainWindow.cpp` — outline, details, save, and the
  xml2rfc run.
