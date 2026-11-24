// Copyright (c) 2026 RiverExplorer LLC
// RiverExplorer is a registered trademark of Douglas Mark Royer.
//
// This software is released under the MIT License.
// See LICENSE.md for the full license text.

/**
 * @file TextEditor.h
 * @brief Two-tab text editor for section bodies and the abstract
 *        (Bugs 322, 382-387, 393, 395-402).
 *
 * The Text tab is a rich-text edit: tags show as formatting, never
 * as markup. The XML tab shows the raw <t> and list XML of the
 * same paragraphs, read-only, regenerated as the text changes. A
 * right-click menu offers the 16 inline tags of <t> in the
 * xml2rfc words, always alphabetical: with text selected it
 * wraps the selection, without a selection it starts or ends a
 * tag at the cursor, and it removes tags already present. The
 * List entry opens a submenu (Bulleted list, Definition list,
 * Numbered list) that turns paragraphs into a section-level
 * list, or starts one at the cursor; a Remove action naming the
 * kind turns a list back into paragraphs. spanx is not offered:
 * the reader converts it to em, strong, or tt on import. Empty
 * tags (Line Break, Vertical Space, Index Entry) show as marker
 * symbols; links and comments show link-styled with hover
 * tooltips. Tags are offered only where the schema allows them
 * to nest.
 *
 * Each character carries the stack of tags around it, as a custom
 * character-format property, so the editor converts between the
 * rich text and the structured paragraphs without losing nesting
 * or attributes. Lists live as whole paragraphs: a paragraph
 * holding a list shows its items as list blocks, and a paragraph
 * mixing a list with other text stays a preserved block. A
 * definition list shows its terms bold on a tinted background,
 * each term followed by its indented definition; a term and its
 * definition paragraphs are marked by role, never by position.
 * Lists follow the Word keys: Enter starts the next item (or the
 * next part of a definition entry), Shift+Enter adds a paragraph
 * to the same item, and Enter on an empty item leaves the list.
 * Three buttons above the text (Bulleted list, Numbered list,
 * Definition list) apply or remove a list kind, and show which
 * kind the cursor is in. A Blocks button starts and stops a
 * block: its menu picks the kind of block (ASCII art, Figure,
 * Source code, or Table), its label names the picked kind, and
 * pressing it again stops the block. A table is a one-shot
 * insert from the menu: it asks for rows, columns, and whether
 * a header row is wanted, and leaves the cursor in its caption.
 * Code shows monospace and indented; Enter adds a line and
 * never leaves the block, and the Tab key inserts spaces.
 * ASCII art shows the same way: literal text, one block per
 * line. A figure shows a bold caption block over its artwork
 * lines. A figure holding several artwork versions shows one
 * version at a time, with a picker above the text to switch
 * versions; an SVG version shows as one placeholder line and
 * cannot be typed in, since SVG is preserved, never edited.
 * A table shows a bold caption block over a grid: header cells
 * show bold, Tab moves between cells, Enter in a cell inserts
 * a line break, and the right-click menu offers row and column
 * operations, merging and splitting, and removal. A sourcecode
 * block shows a gear marker at the start of its first line:
 * hovering shows the block's options, right-clicking the gear
 * opens a menu for them, and a "Show option markers" checkbox
 * above the text turns the markers on and off (Bug 420).
 */

#ifndef DOCEDITOR_TEXTEDITOR_H
#define DOCEDITOR_TEXTEDITOR_H

#include <QWidget>

#include <libxml/tree.h>

#include "DocModel.h"

class QEvent;
class QCheckBox;
class QComboBox;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTextEdit;
class QToolButton;

class BibCache;
class SpellChecker;
class SpellHighlighter;

/**
 * @brief One tag in the stack around a character: its name and attributes.
 *
 * The stack is stored outermost first. Attributes stay in file
 * order. A contact contributes two entries, the contact and its
 * organization, because a contact holds an organization element
 * rather than text.
 */
struct DocTagEntry
{
    QString Name;
    QVector<QPair<QString, QString>> Attributes;

    /**
     * @brief True when two entries name the same tag with the same attributes.
     * @param Other The entry to compare with.
     * @return True when the name and attributes match.
     */
    bool operator==(const DocTagEntry& Other) const;
};

/**
 * @brief The text editor: two tabs, Text and XML.
 */
class TextEditor : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief What the References menu asks the document to do.
     *
     * NewReferenceAction creates a reference entry for the anchor
     * in the named list; InternalReferenceAction points the
     * cross-reference at the given target element.
     */
    enum ReferenceAction
    {
        NewReferenceAction = 0,
        InternalReferenceAction = 1
    };

    /**
     * @brief How a cross-reference is shown: the form of the <xref>.
     *
     * These are the forms published RFCs actually use (RFC 8933,
     * RFC 8773, RFC 8792). LinkText wraps the highlighted words
     * as the link's content; Bracket leaves the words plain and
     * puts an empty xref after them, which renders as the
     * bracketed anchor; SectionOf and SectionBare point at a
     * section of the target, rendering as "Section N of [X]" and
     * "Section N"; Counter shows only the target's number. The
     * forms the spec defines but no published RFC uses
     * (format="title", format="none", sectionFormat "comma" and
     * "parens", pageno, relative) are deliberately not offered.
     */
    enum class XrefForm
    {
        LinkText = 0,
        Bracket = 1,
        SectionOf = 2,
        SectionBare = 3,
        Counter = 4
    };

    /**
     * @brief Asks which form a cross-reference should take.
     *
     * Shows the forms published RFCs use, with the anchor in
     * each label so the choice is concrete. A section form
     * asks for the section number afterwards. Returns false
     * when the user cancels either prompt, in which case the
     * form and section are left alone and nothing is cited.
     * @param Parent The dialog parent.
     * @param Anchor The cross-reference target.
     * @param Highlight The highlighted words, or empty.
     * @param Form Set to the picked form.
     * @param Section Set to the section number, for a section form.
     * @return False when the user cancelled.
     */
    static bool PickXrefForm(QWidget* Parent, const QString& Anchor,
                             const QString& Highlight, XrefForm& Form,
                             QString& Section);

    /**
     * @brief Creates the text editor.
     * @param Parent The parent widget, or nullptr.
     */
    explicit TextEditor(QWidget* Parent = nullptr);

    /**
     * @brief Frees the editor-owned node copies.
     */
    ~TextEditor();

    /**
     * @brief Shows the given body nodes in the Text tab.
     *
     * The nodes are deep-copied: the editor owns its copies
     * and frees them in Clear and in the destructor. The XML
     * tab refreshes to match. No TextChanged signal is sent
     * for this fill.
     * @param Nodes The body nodes to show; borrowed.
     */
    void SetBodyNodes(const QVector<xmlNodePtr>& Nodes);

    /**
     * @brief Sets the preserved nodes the XML tab shows.
     *
     * The nodes are deep-copied: the editor owns its copies
     * and frees them in Clear and in the destructor. The items
     * are display only: they appear in the XML tab
     * interleaved with the body nodes at their anchors, and
     * editing the Text tab never changes them. Call this on
     * every fill, before or after SetBodyNodes; Clear forgets
     * them.
     * @param Items The preserved items to show; borrowed.
     */
    void SetPreservedNodes(const QVector<DocPreservedItem>& Items);

    /**
     * @brief Returns the body nodes as currently edited.
     *
     * Blank paragraphs (no text and no elements) are dropped,
     * matching how the old plain-text edit treated blank lines;
     * a list node is blank only when none of its items holds
     * content. The caller owns the returned nodes and must
     * free each with xmlFreeNode, except when passing them to
     * DocDocument::RebuildBodyChildren, which adopts them.
     * @return The body nodes.
     */
    QVector<xmlNodePtr> BodyNodes() const;

    /**
     * @brief Clears both tabs, freeing the editor-owned node copies.
     */
    void Clear();

    /**
     * @brief The number of text blocks currently in the Text tab.
     * @return The block count of the text document.
     */
    int BlockCount() const;

    /**
     * @brief A one-line report of the last option-marker sweep
     * (Bug 502 diagnostic: setting, blocks scanned, first source
     * blocks, gears inserted and removed).
     * @return The report text.
     */
    QString MarkerSweepReport() const;

    /**
     * @brief Gives the editor the document to read references from.
     *
     * The pointer is borrowed, never owned, and only read: the
     * References menu lists the document's entries and internal
     * targets from it. Document writes go back through the
     * MakeReferenceRequested signal. May be nullptr, in which
     * case the References menu is not offered.
     * @param Document The document, borrowed, or nullptr.
     */
    void SetDocument(const DocDocument* Document);

    /**
     * @brief Sets the bibliography cache the References menu reads anchors from.
     *
     * The pointer is borrowed, never owned, and only read: an
     * included reference's anchor comes from its cached file
     * (BCP14, not BCP.14 from the file name) when it is
     * cached, and from the file name otherwise. May be
     * nullptr, in which case the file name is used.
     * @param Cache The cache, borrowed, or nullptr.
     */
    void SetBibCache(const BibCache* Cache);

    /**
     * @brief Inserts a cross-reference to the anchor, in the given form.
     *
     * LinkText wraps the given range in the cross-reference, so
     * the highlighted text stays as its content:
     * <xref target="RFC4324">[iCal]</xref> shows as [iCal] and
     * points at RFC4324. The other forms are empty xrefs (no
     * content): Bracket puts a bare marker after the range,
     * which renders as the bracketed anchor, so the words stay
     * plain and the reference follows them; SectionOf and
     * SectionBare put a bare marker carrying the section after
     * the range; Counter puts a bare marker carrying
     * format="counter" after the range. With no selection (both
     * ends -1, or an empty range) a bare marker is dropped at
     * the cursor instead, for every form. The range is passed
     * in rather than read from the live cursor: the outline
     * rebuild reloads the section body and drops the live
     * selection, so a range read after the rebuild lands at
     * the start of the section instead of the highlight.
     * @param Anchor The cross-reference target.
     * @param Section The section number, for a section form.
     * @param Form Which form the cross-reference takes.
     * @param SelectionStart The first character of the range, or -1.
     * @param SelectionEnd The character after the range, or -1.
     */
    void InsertXref(const QString& Anchor, const QString& Section,
                    XrefForm Form, int SelectionStart, int SelectionEnd);

    /**
     * @brief Wraps the given range in a cross-reference to the anchor.
     *
     * The highlighted text stays as the cross-reference's
     * content: <xref target="RFC4324">[iCal]</xref> shows as
     * [iCal] and points at RFC4324. The range is passed in
     * rather than read from the live cursor: the outline
     * rebuild reloads the section body and drops the live
     * selection, so a range read after the rebuild lands at
     * the start of the section instead of the highlight. With
     * no selection (both ends -1, or an empty range) a bare
     * marker is dropped at the cursor instead. The References
     * menu only offers itself with a selection, so the bare
     * marker is a fallback only.
     * @param Anchor The cross-reference target.
     * @param SelectionStart The first character of the range, or -1.
     * @param SelectionEnd The character after the range, or -1.
     */
    void InsertXrefMarker(const QString& Anchor, int SelectionStart,
                          int SelectionEnd);

    /**
     * @brief Inserts a cross-reference carrying the given text, at the cursor.
     *
     * The text is put in at the cursor and wrapped in the
     * cross-reference, so the reference shows words instead of
     * the bare marker (U+FFFD) a marker with no content shows
     * as. The caret lands just after the reference, with the
     * format the text would have without the link: typing after
     * the reference is plain text, not more of the link. Only
     * the LinkText form inserts the text; the other forms are
     * empty xrefs, so they drop a bare marker at the cursor
     * instead and the text is ignored.
     * @param Anchor The cross-reference target.
     * @param Text The words the reference shows, for LinkText.
     * @param Section The section number, for a section form.
     * @param Form Which form the cross-reference takes.
     */
    void InsertXrefWithText(const QString& Anchor, const QString& Text,
                            const QString& Section, XrefForm Form);

    /**
     * @brief The current selection range, or -1/-1 when there is none.
     * @param Start Set to the first selected character, or -1.
     * @param End Set to the character after the selection, or -1.
     */
    void SelectedRange(int& Start, int& End) const;

    /**
     * @brief The currently selected text, or empty when there is none.
     * @return The selected words.
     */
    QString SelectedText() const;

    /**
     * @brief Makes the option markers match the document and the
     *        show-markers setting (Bug 420).
     *
     * With markers on, every sourcecode element's first block
     * starts with exactly one gear marker carrying the
     * element's current options as its hover text, and gear
     * characters anywhere else are removed. With markers off,
     * every gear character is removed. The markers are
     * display only: they never reach the paragraphs, the XML
     * tab, or the file.
     */
    void UpdateOptionMarkers();

    /**
     * @brief Removes the cursor's sourcecode element when a
     *        deletion just emptied it (Bug 469).
     *
     * Called after a Backspace, Delete, or Cut key, once the
     * deletion has landed. When the cursor's element holds no
     * real content and no src, its blocks go, so no empty
     * <sourcecode> element survives the deletion. A src-only
     * element stays: it names a file and shows its placeholder.
     * @param HadContent True when the element held real content
     *        before the deletion; a fresh empty block (the Blocks
     *        button) never had any, so it is left alone.
     */
    void RemoveEmptySourceBlocks(bool HadContent);

    /**
     * @brief Keeps an empty plain paragraph after a trailing
     *        sourcecode block.
     *
     * A section ending in a sourcecode block leaves the writer
     * with nowhere to type after it: Enter inside code only
     * adds a code line, and there is no paragraph after the
     * code to click into. When the last block is a sourcecode
     * block, an empty plain paragraph is added after it, so
     * there is always somewhere to type. Nothing happens when
     * the last block is already a plain paragraph. The save
     * step never writes that paragraph (see Paragraphs), so
     * the file does not gain anything.
     */
    void EnsureTrailingPlainParagraph();

    /**
     * @brief Turns spell checking on or off for this editor.
     *
     * With a checker, misspelled words in the Text tab get a
     * red wavy underline, and the right-click menu offers
     * suggestions and adding the word to a dictionary. The
     * checker is shared: one checker serves every text pane.
     * With no checker, the underlines go away and the menu
     * offers nothing. Sourcecode and artwork blocks are never
     * checked: they hold literal text, not prose.
     * @param Checker The spell checker to use, or nullptr.
     */
    void SetSpellChecker(SpellChecker* Checker);

    /**
     * @brief Re-checks the underlines in the Text tab.
     *
     * Called when the spell checker's word lists change (a word
     * added, or the project dictionary swapped), so underlines
     * that no longer apply go away and new ones appear.
     */
    void RefreshSpelling();

signals:
    /**
     * @brief Sent when the text in the Text tab changes by editing.
     */
    void TextChanged();

    /**
     * @brief Sent when the References menu asks for a document write.
     *
     * The editor shows the menu, the name prompt, and the internal
     * target picker, but it never writes the document itself. The
     * owner creates the entry or gives the target its anchor, then
     * calls InsertXrefMarker with the anchor to cite.
     * @param Action NewReferenceAction or InternalReferenceAction.
     * @param Target The internal target element, for InternalReferenceAction.
     * @param Anchor The reference anchor, for NewReferenceAction.
     * @param ListName The nested list name, for NewReferenceAction.
     */
    void MakeReferenceRequested(int Action, xmlNodePtr Target,
                                const QString& Anchor,
                                const QString& ListName);

private slots:
    /**
     * @brief Refreshes the XML tab and reports the change.
     */
    void OnTextDocumentChanged();

    /**
     * @brief Refreshes the XML tab when it is shown.
     * @param Index The newly shown tab.
     */
    void OnTabChanged(int Index);

    /**
     * @brief Shows the standard menu plus the smart tag menu.
     * @param Pos The click position, in viewport coordinates.
     */
    void OnContextMenuRequested(const QPoint& Pos);

    /**
     * @brief Switches the figure at the cursor to another artwork version.
     *
     * The text of the version showing is saved into the figure,
     * and the picked version's text (or its placeholder, for an
     * SVG version) takes its place.
     * @param Index The picked picker item.
     */
    void OnVersionPickerChanged(int Index);

    /**
     * @brief Shows or hides the option markers (Bug 420).
     *
     * The choice is remembered across runs, and shared by
     * every text editor pane. Toggling the markers is display
     * only: it never marks the document changed.
     * @param Checked True when the markers are to show.
     */
    void OnShowMarkersToggled(bool Checked);

private:
    /**
     * @brief Rewrites the XML tab from the current paragraphs.
     */
    void RefreshXmlTab();

    /**
     * @brief Frees the owned body and preserved nodes and clears
     *        the vectors holding them.
     */
    void FreeOwnedNodes();

    /**
     * @brief Adds the smart tag actions and the remove actions to a menu.
     * @param Menu The menu being built.
     */
    void AddTagActions(QMenu* Menu);

    /**
     * @brief Adds the References submenu to a menu, when text is selected.
     *
     * New makes a reference entry in the picked list, Internal
     * points at a section, figure, or table in this document,
     * and Existing cites an entry already in the picked list.
     * Nothing is added without a selection, without a document,
     * or where the cursor touches sourcecode, a figure, or a table.
     * @param Menu The menu being built.
     */
    void AddReferenceActions(QMenu* Menu);

    /**
     * @brief Adds the sourcecode options submenu when the click
     *        lands on an option marker (Bug 420).
     *
     * The submenu offers the block's options: its name, its
     * type, its markers switch, and its src. No anchor is
     * offered: anchors are assigned automatically, never typed.
     * Nothing is added when the click is not on a marker.
     * @param Menu The menu being built.
     * @param Pos The click position, in viewport coordinates.
     */
    void AddSourceOptionsActions(QMenu* Menu, const QPoint& Pos);

    /**
     * @brief Adds spelling suggestions and dictionary actions
     *        when the click lands on a misspelled word.
     *
     * The suggestions replace the word. "Add to dictionary"
     * offers the personal dictionary (every draft) and the
     * project dictionary (this draft only); "Ignore" accepts
     * the word for this run only. Nothing is added when the
     * click is not on a misspelled word, or when no spell
     * checker is set.
     * @param Menu The menu being built.
     * @param Pos The click position, in viewport coordinates.
     */
    void AddSpellActions(QMenu* Menu, const QPoint& Pos);

    /**
     * @brief Writes one attribute list onto every block of the
     *        sourcecode element starting at a block (Bug 420).
     *
     * Attributes the new list does not carry are dropped from
     * the element, so clearing an option removes its
     * attribute. The markers are refreshed afterwards, so the
     * gear's hover text shows the new options.
     * @param FirstBlockPos The position of the element's first block.
     * @param Attributes The attribute list to write.
     */
    void ApplySourceAttributes(
        int FirstBlockPos,
        const QVector<QPair<QString, QString>>& Attributes);

    /**
     * @brief Asks for the attributes a tag needs when inserted.
     *
     * Cross-references, external links, and relative links ask for
     * a target; an index entry asks for its item and an optional
     * sub-item; a vertical space asks for its blank-line count.
     * The prompt repeats until an answer is given or the user
     * cancels. Other tags need nothing.
     * @param Name The tag name.
     * @param Entry Filled with the tag name and its attributes.
     * @return False when the user cancelled.
     */
    bool PromptForTag(const QString& Name, DocTagEntry& Entry);

    /**
     * @brief Inserts an empty tag, a list, or a contact at the cursor.
     * @param Name The tag name.
     */
    void InsertTag(const QString& Name);

    /**
     * @brief Turns paragraphs into a section-level list, or starts
     *        one at the cursor.
     *
     * With paragraphs selected, each becomes an item of the new
     * list; without a selection, the paragraph at the cursor
     * becomes the first item. A definition list adds an empty
     * definition block after each term block.
     * @param Kind The list element name: ul, ol, or dl.
     */
    void InsertBlockList(const QString& Kind);

    /**
     * @brief Turns the touched list of one kind back into paragraphs.
     * @param Kind The list element name: list, ul, ol, or dl.
     */
    void RemoveBlockList(const QString& Kind);

    /**
     * @brief Applies a list kind, or removes it when the cursor or
     *        selection already touches that kind.
     *
     * The list buttons above the text use this, so a button works
     * like Word's: click to apply, click again to take it off.
     * @param Kind The list element name: ul, ol, or dl.
     */
    void ToggleBlockList(const QString& Kind);

    /**
     * @brief Turns paragraphs into a sourcecode block, or starts
     *        one at the cursor.
     *
     * With paragraphs selected, each becomes a line of the new
     * block; without a selection, the paragraph at the cursor
     * becomes its first line. Tags and markers are stripped:
     * code holds literal text only. The new block carries no
     * attributes.
     */
    void InsertSourceCode();

    /**
     * @brief Turns the touched sourcecode blocks back into
     *        paragraphs, one paragraph per line.
     */
    void RemoveSourceCode();

    /**
     * @brief Inserts an XML comment at the cursor.
     *
     * The Blocks menu's Comment action: "<!-- -->" appears in
     * italics at the insertion point, with the cursor at the
     * space in the middle, so the comment can be typed. When
     * the cursor sits in a code or artwork block, the block is
     * stopped first, so the comment lands after it as a real
     * comment instead of literal text inside the block.
     * Nothing happens where the cursor touches a table: a
     * comment inside a cell or a caption keeps the whole table
     * preserved, so no comment is offered there.
     */
    void InsertComment();

    /**
     * @brief Turns paragraphs into an artwork block, or starts
     *        one at the cursor.
     *
     * With paragraphs selected, each becomes a line of the new
     * block; without a selection, the paragraph at the cursor
     * becomes its first line. Tags and markers are stripped:
     * artwork holds literal text only. The new block carries
     * type "ascii-art".
     */
    void InsertArtwork();

    /**
     * @brief Turns paragraphs into a figure, or starts one at
     *        the cursor.
     *
     * A caption block is added before the artwork lines, and
     * the cursor is left in it, so the caption is typed first.
     * The figure starts with one ascii-art version and an
     * empty caption.
     */
    void InsertFigure();

    /**
     * @brief Turns the touched figure regions back into
     *        paragraphs: the caption becomes a plain paragraph
     *        and the artwork lines become plain paragraphs, one
     *        per line.
     *
     * Every version goes with the figure. Removing a figure
     * holding a version kept only in the document asks first:
     * there is no other copy of such a version, and the removal
     * cannot be recovered. A version included from a file needs
     * no question, because the file still holds a copy.
     */
    void RemoveFigure();

    /**
     * @brief Asks for a table's size and inserts one at the cursor.
     *
     * The Blocks menu's Table action, a one-shot insert: it
     * asks for the number of body rows and columns, and whether
     * a header row and a footer row are wanted, then inserts a
     * caption block over a grid, and leaves the cursor in the
     * empty caption, so the caption is typed first. The new
     * table carries no attributes. Nothing happens where the
     * cursor touches a list, a sourcecode block, a figure, or
     * a table.
     */
    void InsertTable();

    /**
     * @brief Inserts a row above the cursor's row, in its section.
     *
     * The new row takes the section of the cursor's row, and
     * its cells take the header flag of that section. The
     * cursor moves to the new row's first cell.
     */
    void InsertTableRowAbove();

    /**
     * @brief Inserts a row below the cursor's row, in its section.
     *
     * The new row takes the section of the cursor's row, and
     * its cells take the header flag of that section. The
     * cursor moves to the new row's first cell.
     */
    void InsertTableRowBelow();

    /**
     * @brief Deletes the cursor's row.
     *
     * The last body row cannot be deleted: a table always keeps
     * a body. Deleting the only header row removes the header
     * section. The cursor moves to the row taking its place.
     */
    void DeleteTableRow();

    /**
     * @brief Inserts a column to the left of the cursor's column.
     *
     * The new cells take the header flag of their row's
     * section. The cursor moves to the new column's cell in
     * the cursor's row.
     */
    void InsertTableColumnLeft();

    /**
     * @brief Inserts a column to the right of the cursor's column.
     *
     * The new cells take the header flag of their row's
     * section. The cursor moves to the new column's cell in
     * the cursor's row.
     */
    void InsertTableColumnRight();

    /**
     * @brief Deletes the cursor's column.
     *
     * The last column cannot be deleted. The cursor moves to
     * the column taking its place.
     */
    void DeleteTableColumn();

    /**
     * @brief Sets the number of body rows, leaving the header
     *        and footer sections untouched.
     *
     * Asks for the count, showing the current one. Growing
     * appends empty rows at the end of the body; shrinking
     * removes rows from the end of the body, one at a time.
     * The count never goes below one body row.
     */
    void SetTableBodyRowCount();

    /**
     * @brief Sets the number of columns of the whole grid.
     *
     * Asks for the count, showing the current one. Growing
     * appends empty columns at the right; shrinking removes
     * columns from the right, one at a time. The count never
     * goes below one column.
     */
    void SetTableColumnCount();

    /**
     * @brief Toggles the header section on or off.
     *
     * Toggling on adds one header row of header cells at the
     * top of the grid; toggling off removes the header rows.
     * No question is asked: the change is one edit-block step,
     * so one Undo restores the section.
     */
    void ToggleTableHeaderRow();

    /**
     * @brief Toggles the footer section on or off.
     *
     * Toggling on adds one footer row of plain cells at the
     * bottom of the grid; toggling off removes the footer
     * rows. No question is asked: the change is one edit-block
     * step, so one Undo restores the section.
     */
    void ToggleTableFooterRow();

    /**
     * @brief Merges the selected cells into one.
     *
     * The selection must span two or more cells of one table.
     * The merged cell keeps the top-left cell's text, header
     * flag, and kept attributes; the other cells' text joins
     * it, and their kept attributes are dropped.
     */
    void MergeTableCells();

    /**
     * @brief Splits the cursor's spanned cell back to single cells.
     *
     * The cell's text stays in the top-left cell, and the other
     * cells start empty. The header flag is copied to every new
     * cell; the kept attributes stay on the top-left cell only.
     */
    void SplitTableCell();

    /**
     * @brief Turns the touched table back into paragraphs: the
     *        caption becomes a plain paragraph, and each cell
     *        becomes a plain paragraph, in row order.
     *
     * Removing a table asks first: the table is kept only in
     * this document, there is no other copy of it, and the
     * removal cannot be recovered.
     */
    void RemoveTable();

    /**
     * @brief Inserts a row above or below the cursor's row, in
     *        its section.
     * @param Above True for above the cursor's row, false for
     *        below it.
     */
    void InsertTableRow(bool Above);

    /**
     * @brief Inserts a column to the left or right of the
     *        cursor's column.
     * @param Left True for left of the cursor's column, false
     *        for right of it.
     */
    void InsertTableColumn(bool Left);

    /**
     * @brief Handles a press of the Blocks button itself.
     *
     * With a kind picked from the menu, the press toggles that
     * kind; with the cursor in a block, it stops that block.
     * With no kind picked, the press opens the menu: the
     * button has no default (Bug 444).
     */
    void OnBlocksButtonClicked();

    /**
     * @brief Starts a sourcecode block, or stops it when the
     *        cursor or selection already touches one.
     *
     * The Blocks menu uses this, so picking a kind starts its
     * block and picking it again stops it. Nothing happens
     * where the cursor touches a list or a figure.
     */
    void ToggleSourceCode();

    /**
     * @brief Starts an artwork block, or stops it when the
     *        cursor or selection already touches a figure.
     *
     * Nothing happens where the cursor touches a list or a
     * sourcecode block.
     */
    void ToggleArtwork();

    /**
     * @brief Starts a figure, or stops it when the cursor or
     *        selection already touches a figure.
     *
     * Nothing happens where the cursor touches a list or a
     * sourcecode block.
     */
    void ToggleFigure();

    /**
     * @brief Stops a sourcecode block: the cursor moves to a new
     *        plain paragraph after the block, which stays code.
     *
     * Stopping never changes the code itself, not even a blank
     * line at its end: code keeps every line it was given. The
     * Remove action is what turns code back into paragraphs.
     * Nothing happens when the cursor itself sits outside a
     * code block.
     */
    void StopSourceCode();

    /**
     * @brief Stops a figure: the cursor moves to a new plain
     *        paragraph after the region, which stays a figure.
     *
     * Stopping never changes the figure itself. The Remove
     * action is what turns a figure back into paragraphs.
     * Nothing happens when the cursor itself sits outside a
     * figure region.
     */
    void StopFigure();

    /**
     * @brief Shows on the list and Blocks buttons what the
     *        cursor is in, names the Blocks button for the
     *        picked kind of block, and fills the version picker
     *        when the cursor sits in a figure holding several
     *        versions.
     */
    void UpdateBlockButtons();

    /**
     * @brief Keeps typing after a link plain: when the caret
     *        lands just after a link, its format loses the link
     *        tag, so the next typed text is plain text and not
     *        more of the link. Inside a link the caret is left
     *        alone, so editing the link text still extends it.
     */
    void OnCaretMoved();

    /**
     * @brief Wraps the selection in a tag.
     * @param Name The tag name.
     */
    void WrapSelection(const QString& Name);

    /**
     * @brief Starts or ends a tag at the cursor, for typing.
     * @param Name The tag name.
     * @param Active True when the tag is already active at the
     *        cursor, so this call ends it.
     */
    void ToggleTag(const QString& Name, bool Active);

    /**
     * @brief Removes a tag from the selection, or from the run of
     *        text around the cursor when nothing is selected.
     * @param Name The tag name.
     */
    void RemoveTag(const QString& Name);

    /**
     * @brief Removes the marker at the cursor, or every marker in
     *        the selection.
     */
    void RemoveMarker();

    /**
     * @brief Changes the tag stacks over a character range.
     *
     * When Entry is given, its tag is added to every stack in the
     * range that does not already hold it and where the schema
     * allows it to nest; when Entry is null, the tag is removed
     * from every stack in the range. Marker characters keep their
     * markers either way.
     * @param Start The first character position.
     * @param End The position after the last character.
     * @param Name The tag name.
     * @param Entry The tag entry to add, or nullptr to remove.
     */
    void ChangeStacks(int Start, int End, const QString& Name,
                      const DocTagEntry* Entry);

    /**
     * @brief Handles Enter and Shift+Enter in list blocks, the
     *        Word way, the code keys in sourcecode and artwork
     *        blocks, and the read-only placeholder of an SVG
     *        version.
     *
     * Enter starts the next item, or the next part of a
     * definition entry; Shift+Enter adds a paragraph to the
     * same item or definition. A definition list has no list
     * object, so the edit's own Enter handling cannot end it
     * the way it ends a bulleted or numbered list: Enter in an
     * empty term turns the block plain instead. A continuation
     * paragraph of a bulleted or numbered list sits outside
     * the list object (Qt would give it its own bullet), so
     * Enter there rebuilds the list with the new item in it.
     * In a sourcecode or artwork block, Enter and Shift+Enter
     * both add a line — Enter never leaves the block, because
     * code and artwork keep every line they are given — and
     * the Tab key inserts spaces, because tabs are prohibited
     * in sourcecode and artwork. Enter in a figure caption
     * moves to the artwork lines, since a caption is one line.
     * In a table cell, Enter inserts a line break, since a cell
     * holds one paragraph, and Tab moves to the next cell;
     * Enter in a table caption moves to the first cell. A key
     * that would replace a selection crossing a table boundary
     * is swallowed, since Qt would delete the whole table.
     * In an SVG placeholder block, keys that would change the
     * text are swallowed, so the preserved artwork cannot be
     * typed in; navigation and copying still work.
     * @param Watched The object the event is for.
     * @param Event The event.
     * @return True when the event was handled here.
     */
    bool eventFilter(QObject* Watched, QEvent* Event) override;

    /**
     * @brief Shows the read-only source line hint, once per line.
     *
     * The <Src=...> line of a src-only sourcecode block cannot be
     * typed in. When a key is swallowed there, this pop-up tells
     * the writer why and where to type instead. It shows at most
     * once for a line: further keys on the same line are swallowed
     * silently, so holding a key down does not stack dialogs.
     * @param BlockPos The position of the block the hint is about.
     */
    void ShowSrcPlaceholderHint(int BlockPos);

    QTabWidget* _Tabs;
    QTextEdit* _TextEdit;
    QPlainTextEdit* _XmlEdit;
    QVector<xmlNodePtr> _BodyNodes;
    QVector<DocPreservedItem> _PreservedNodes;
    QPushButton* _BulletedButton;
    QPushButton* _NumberedButton;
    QPushButton* _DefinitionButton;
    QToolButton* _BlocksButton;
    QComboBox* _VersionPicker;
    QCheckBox* _ShowMarkersCheck;
    QString _BlockKind;
    bool _SettingParagraphs;
    // Last option-marker sweep counts (Bug 502 diagnostic).
    bool _DiagMarkersShown;
    int _DiagMarkersBlocks;
    int _DiagMarkersSourceFirst;
    int _DiagMarkersGearsInserted;
    int _DiagMarkersGearsRemoved;
    int _SrcPlaceholderHintBlock;
    SpellChecker* _SpellChecker;
    const DocDocument* _Document;
    const BibCache* _BibCache;
    SpellHighlighter* _SpellHighlighter;
};

#endif // DOCEDITOR_TEXTEDITOR_H
