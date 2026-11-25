// Copyright (c) 2026 RiverExplorer LLC
// RiverExplorer is a registered trademark of Douglas Mark Royer.
//
// This software is released under the MIT License.
// See LICENSE.md for the full license text.

/**
 * @file MainWindow.h
 * @brief Main window for the ProtocolDocEditor (goal #1).
 *
 * The window is split like the CoOp editor: the outline on the
 * left, the details on the right. The outline holds the document
 * settings plus the three parts of the draft (front, middle,
 * back) with their sub-parts.
 * Sections in the middle can be added, inserted, removed, and
 * rearranged. The details pane edits the selected part.
 *
 * File > Save writes the draft as RFCXML version 3. File > Run
 * xml2rfc runs xml2rfc on the saved file and shows its output,
 * in the checked formats: Text, PDF, HTML (Bug 320). File >
 * Validate checks the saved file against the v3 schema
 * inside the app (goal #2); opening and saving check it too.
 * Parts of a file the editor does not edit yet are kept when the
 * file is opened and written back on save (goal #3). The File
 * menu remembers the last few drafts opened (Bug 319). Section
 * bodies and the abstract edit in a two-tab text editor (Bug
 * 322): Text for rich text with tags, XML for the raw XML.
 */

#ifndef DOCEDITOR_MAINWINDOW_H
#define DOCEDITOR_MAINWINDOW_H

#include <QMainWindow>
#include <QList>
#include <QPoint>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <memory>

#include <libxml/tree.h>

#include "BibCache.h"
#include "DocModel.h"
#include "DocValidator.h"
#include "SpellChecker.h"
#include "TextEditor.h"

class QAction;
class QCheckBox;
class QComboBox;
class QDialog;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QStackedWidget;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * @brief The main window: outline left, details right.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /**
     * @brief Creates the main window with an empty document.
     * @param Parent The parent widget, or nullptr.
     */
    explicit MainWindow(QWidget* Parent = nullptr);

    /**
     * @brief Shows the given document, to be saved at the given path.
     * @param Document The document to show.
     * @param FilePath The file path the document saves to.
     */
    void SetDocument(const DocDocument& Document, const QString& FilePath);

    /**
     * @brief Opens a draft XML file into the window.
     * @param Path The file to open.
     */
    void OpenDraftFile(const QString& Path);

    /**
     * @brief Returns the files opened most recently, newest first.
     * @return The remembered file paths, newest first.
     */
    static QStringList RecentFiles();

    /**
     * @brief Remembers a file as the most recently opened one.
     * @param Path The file path to remember.
     */
    static void AddRecentFile(const QString& Path);

protected:
    /**
     * @brief Prompts to save when the document was modified.
     * @param Event The close event.
     */
    void closeEvent(QCloseEvent* Event) override;

private slots:
    /**
     * @brief Shows the details pane for the newly selected outline item.
     */
    void OnOutlineSelectionChanged();

    /**
     * @brief Shows the subsection menu for a right-clicked outline
     *        section (Bug 446). The menu appears only on section
     *        items; the clicked section is selected first.
     * @param Pos The click position, in outline viewport
     *        coordinates.
     */
    void OnOutlineContextMenu(const QPoint& Pos);

    /**
     * @brief Adds a section after the selected section (or at the
     *        end of the middle matter when no section is selected).
     */
    void OnAddSection();

    /**
     * @brief Adds sections pasted into the section body's XML
     *        tab as subsections of the current section (Bug 520).
     *
     * The sections are borrowed (the editor frees its copies
     * after this returns), so each is deep-copied into the
     * document first. A pasted anchor that already exists in
     * the document is made unique, and cross-references inside
     * the pasted section that point at it are updated to the
     * new anchor, so the file stays valid.
     * @param Sections The pasted <section> nodes; borrowed.
     */
    void OnSubsectionsPasted(const QVector<xmlNodePtr>& Sections);

    /**
     * @brief Inserts a section before the selected section.
     */
    void OnInsertSection();

    /**
     * @brief Removes the selected section, after confirming.
     */
    void OnRemoveSection();

    /**
     * @brief Moves the selected section up among its siblings.
     */
    void OnMoveSectionUp();

    /**
     * @brief Moves the selected section down among its siblings.
     */
    void OnMoveSectionDown();

    /**
     * @brief Adds a subsection as the last child of the selected
     *        section (Bug 446).
     */
    void OnAddSubsection();

    /**
     * @brief Inserts a subsection as the first child of the
     *        selected section (Bug 446).
     */
    void OnInsertSubsection();

    /**
     * @brief Promotes the selected section to the sibling right
     *        after its parent (Bug 446). A top-level section
     *        does not promote.
     */
    void OnPromoteSubsection();

    /**
     * @brief Demotes the selected section to the last child of
     *        its previous sibling (Bug 446). The first section
     *        in a list, and a top-level required section, do
     *        not demote.
     */
    void OnDemoteSubsection();

    /**
     * @brief Adds an author, asking for the full name first.
     */
    void OnAddAuthor();

    /**
     * @brief Removes the shown author, after confirming. The last
     *        author cannot be removed: a draft needs at least one.
     *        A reference author can always be removed.
     */
    void OnRemoveAuthor();

    /**
     * @brief Adds a reference list, asking for its name first.
     */
    void OnAddReferenceGroup();

    /**
     * @brief Adds a nested reference list under the shown list,
     *        asking for its name first.
     */
    void OnAddNestedReferenceGroup();

    /**
     * @brief Removes the shown reference list and its entries,
     *        after confirming.
     */
    void OnRemoveReferenceGroup();

    /**
     * @brief Moves the shown reference list up among the lists.
     */
    void OnMoveReferenceGroupUp();

    /**
     * @brief Moves the shown reference list down among the lists.
     */
    void OnMoveReferenceGroupDown();

    /**
     * @brief Writes the edited reference list name into the document.
     */
    void OnReferenceGroupNameEdited(const QString& Text);

    /**
     * @brief Adds a display name for a reference, asking for the
     *        reference and the name to show first (Bug 405).
     */
    void OnAddDisplayReference();

    /**
     * @brief Removes the selected display name (Bug 405).
     */
    void OnRemoveDisplayReference();

    /**
     * @brief Writes the edited display names into the document
     *        (Bug 405).
     */
    void OnDisplayReferenceChanged();

    /**
     * @brief Adds a reference entry to the shown list, asking for
     *        its anchor first.
     */
    void OnAddReferenceEntry();

    /**
     * @brief Carries out a References-menu request from a text editor.
     *
     * A new reference makes the entry in the named list (an
     * xi:include for an RFC number, a placeholder entry otherwise)
     * and cites it; an internal reference gives the picked target
     * an anchor when it has none, then cites it. The editor that
     * asked gets the cross-reference marker.
     * @param Editor The text editor that asked, borrowed.
     * @param Action NewReferenceAction or InternalReferenceAction.
     * @param Target The internal target element, for InternalReferenceAction.
     * @param Anchor The reference anchor, for NewReferenceAction.
     * @param ListName The nested list name, for NewReferenceAction.
     */
    void HandleMakeReference(TextEditor* Editor, int Action,
                             xmlNodePtr Target, const QString& Anchor,
                             const QString& ListName);

    /**
     * @brief Finishes changing an anchor from a gear menu.
     *
     * A delete is refused when the anchor is cited: the
     * cross-references would dangle. A rename moves every
     * cross-reference to the new anchor, and the asking editor
     * is reloaded from the model so its markers match. A new
     * anchor must not collide with one already in use.
     * @param Editor The text editor that asked, borrowed.
     * @param OldAnchor The anchor now on the element, empty when none.
     * @param NewAnchor The anchor the user entered, empty to clear it.
     * @param FirstBlockPos The first block of the element, in the editor.
     * @param IsSourceCode True for a sourcecode element, false for a list.
     * @param PartKind The table part for a table gear, empty for a
     *        sourcecode or list gear (Bug 448).
     */
    void HandleAnchorChange(TextEditor* Editor, const QString& OldAnchor,
                            const QString& NewAnchor, int FirstBlockPos,
                            bool IsSourceCode, const QString& PartKind);

    /**
     * @brief Reloads an editor from the model, keeping the cursor.
     * @param Editor The text editor to reload, borrowed.
     */
    void ReloadEditorFromModel(TextEditor* Editor);

    /**
     * @brief Finishes adding a new RFC reference, once its file is fetched.
     *
     * The fetch runs first so the anchors the file provides are
     * known: when one is already provided (a group in the document
     * carries it inside itself, BCP14 providing RFC2119), the
     * existing reference is cited instead of adding a second copy.
     * @param Editor The text editor that asked, borrowed.
     * @param Group The list the include goes in, borrowed.
     * @param Wanted The reference anchor.
     * @param Href The bibliography address of the include.
     * @param Form Which form the cross-reference takes.
     * @param Section The section number, for a section form.
     * @param SelectionStart The start of the highlighted range, or -1.
     * @param SelectionEnd The end of the highlighted range, or -1.
     */
    void FinishNewReference(TextEditor* Editor, xmlNodePtr Group,
                            const QString& Wanted, const QString& Href,
                            TextEditor::XrefForm Form,
                            const QString& Section,
                            int SelectionStart, int SelectionEnd);

    /**
     * @brief The anchors the document's references provide, and what provides each.
     *
     * Manual entries provide their anchor; includes provide the
     * anchors their cached file carries (a group provides its own
     * anchor and its inner anchors), or the anchor derived from
     * the file name until fetched.
     * @return Each provided anchor, with the references that provide it, in document order.
     */
    QMap<QString, QStringList> ProvidedAnchorProviders() const;

    /**
     * @brief The first anchor in NewAnchors the document already provides.
     * @param NewAnchors The anchors a new reference would provide.
     * @param Clash Set to the clashing anchor, or empty.
     * @param Provider Set to the reference that already provides it, or empty.
     * @return True when one of the new anchors is already provided.
     */
    bool FindProvidedAnchorClash(const QStringList& NewAnchors,
                                 QString& Clash, QString& Provider);

    /**
     * @brief The entry or include that provides this anchor, or nullptr.
     *
     * The node the clash paths move into the list the author
     * chose: a manual reference entry whose anchor matches,
     * or an include whose cached file provides the anchor
     * (a group include provides its own anchor and its inner
     * anchors, so the group itself is what moves).
     * @param Anchor The anchor to find a provider for.
     * @return The providing entry or include, or nullptr.
     */
    xmlNodePtr FindAnchorProviderNode(const QString& Anchor) const;

    /**
     * @brief The nested reference list with this name, creating it when missing.
     *
     * The named list under the outer "References" list, creating
     * the back matter, the outer list, and both nested lists when
     * the file has none. A file whose "References" list is flat
     * (entries, no nested lists) returns that list itself: the
     * normative/informative split does not exist there.
     * @param ListName The nested list name.
     * @return The list node, borrowed, or nullptr when there is no document.
     */
    xmlNodePtr EnsureReferenceList(const QString& ListName);

    /**
     * @brief Removes the shown reference entry, after confirming.
     */
    void OnRemoveReferenceEntry();

    /**
     * @brief Removes the selected included reference, after confirming.
     */
    void OnRemoveReferenceInclude();

    /**
     * @brief Moves the shown reference entry up among its list's entries.
     */
    void OnMoveReferenceEntryUp();

    /**
     * @brief Moves the shown reference entry down among its list's entries.
     */
    void OnMoveReferenceEntryDown();

    /**
     * @brief Writes the edited reference entry fields into the document.
     */
    void OnReferenceEntryEdited();

    /**
     * @brief Writes the reference entry's stream choice into the document.
     */
    void OnReferenceStreamChanged();

    /**
     * @brief Moves the shown reference entry to the picked list.
     */
    void OnReferenceMoveToGroupChanged();

    /**
     * @brief Moves the shown reference entry to the picked section.
     */
    void OnReferenceSectionChanged();

    /**
     * @brief Moves the picked included reference to the picked section.
     */
    void OnReferenceIncludeSectionChanged();

    /**
     * @brief Moves a reference entry or an included reference into
     *        the Normative or Informative nested list under its
     *        current outer list, creating that list when needed.
     * @param Item The entry or include node to move (borrowed).
     * @param Normative True for the Normative section, false for
     *        the Informative section.
     */
    void MoveReferenceToSection(xmlNodePtr Item, bool Normative);

    /**
     * @brief Adds a series info row to the shown reference entry.
     */
    void OnAddSeriesInfo();

    /**
     * @brief Removes the selected series info row from the shown entry.
     */
    void OnRemoveSeriesInfo();

    /**
     * @brief Writes the edited series info rows into the document.
     */
    void OnSeriesInfoChanged();

    /**
     * @brief Adds an author to the shown reference entry, asking
     *        for the full name first.
     */
    void OnAddReferenceAuthor();

    /**
     * @brief Removes the selected author of the shown reference
     *        entry, after confirming.
     */
    void OnRemoveReferenceAuthor();

    /**
     * @brief Shows the selected author of the shown reference
     *        entry in the author fields on the entry form.
     */
    void OnReferenceAuthorSelected();

    /**
     * @brief Writes the author fields on the reference entry form
     *        into the selected author of the shown entry.
     */
    void OnReferenceAuthorEdited();

    /**
     * @brief Writes the role choice on the reference entry form
     *        into the selected author of the shown entry.
     */
    void OnReferenceAuthorRoleChanged();

    /**
     * @brief Runs the new-draft wizard and shows the new document.
     */
    void OnNewDraft();

    /**
     * @brief Opens a draft XML file picked with the file dialog.
     */
    void OnOpenDraft();

    /**
     * @brief Opens a draft picked from the recent-files list.
     */
    void OnOpenRecentFile();

    /**
     * @brief Saves the document to its file path.
     */
    void OnSave();

    /**
     * @brief Saves the document to a picked file path.
     */
    void OnSaveAs();

    /**
     * @brief Saves if needed, then runs xml2rfc on the saved file.
     */
    void OnRunXml2rfc();

    /**
     * @brief Saves if needed, then validates the saved file and
     *        shows the result.
     */
    void OnValidate();

    /**
     * @brief Asks for the xml2rfc program path and remembers it.
     */
    void OnSetXml2rfcPath();

    /**
     * @brief Shows the Bibliography dialog: the cache folder and entries.
     */
    void OnBibliography();

    /**
     * @brief Asks for an RFC number and opens the published RFC
     *        in the system viewer, in a format that exists for it.
     */
    void OnViewPublishedRfc();

    /**
     * @brief Asks for an Internet-Draft title and version and opens
     *        the published draft in the system viewer.
     */
    void OnViewPublishedId();

    /**
     * @brief Asks for a draft name and two versions and shows the
     *        differences between them.
     */
    void OnViewPublishedIdDiff();

    /**
     * @brief Shows what the text editor's special characters mean.
     */
    void OnLegend();

    /**
     * @brief Shows the xml2rfc language, described by topic.
     */
    void OnXml2rfcLegend();

    /**
     * @brief Shows information about this editor, including the
     *        copyright and license.
     */
    void OnAbout();

    /**
     * @brief Opens the GitHub issues page in the default browser.
     */
    void OnFeedback();

    /**
     * @brief Remembers the checked output formats when one changes.
     */
    void OnFormatChoiceChanged();

    /**
     * @brief Opens the .txt output in Notepad.
     */
    void OnViewText();

    /**
     * @brief Opens the .pdf output in the default PDF viewer.
     */
    void OnViewPdf();

    /**
     * @brief Opens the .html output in the default browser.
     */
    void OnViewHtml();

    /**
     * @brief Appends xml2rfc's standard output to the run log.
     */
    void OnRunOutput();

    /**
     * @brief Appends xml2rfc's standard error to the run log.
     */
    void OnRunErrorOutput();

    /**
     * @brief Reports xml2rfc's finish in the run log.
     */
    void OnRunFinished(int ExitCode, QProcess::ExitStatus ExitStatus);

    /**
     * @brief Reports a failure to start xml2rfc in the run log.
     */
    void OnRunFailed(QProcess::ProcessError Error);

    /**
     * @brief Writes the edited title into the document.
     */
    void OnSettingsTitleEdited(const QString& Text);

    /**
     * @brief Writes the edited title details (abbreviated and
     *        ASCII forms) into the document.
     */
    void OnSettingsTitleDetailsEdited();

    /**
     * @brief Writes the edited area, workgroup, and keyword
     *        lists into the document.
     */
    void OnSettingsListsEdited();

    /**
     * @brief Adds a series info row to the document front.
     */
    void OnAddDocumentSeriesInfo();

    /**
     * @brief Removes the selected series info row from the
     *        document front.
     */
    void OnRemoveDocumentSeriesInfo();

    /**
     * @brief Writes the edited document series info rows into
     *        the document.
     */
    void OnDocumentSeriesChanged();

    /**
     * @brief Writes the edited note name into the document.
     */
    void OnSettingsNoteNameEdited(const QString& Text);

    /**
     * @brief Writes the note's remove-before-publishing choice
     *        into the document.
     */
    void OnSettingsNoteRemoveChanged();

    /**
     * @brief Writes the edited note body into the document.
     */
    void OnSettingsNoteEdited();

    /**
     * @brief Adds a note to the document front.
     */
    void OnAddNote();

    /**
     * @brief Removes the shown note, after confirming.
     */
    void OnRemoveNote();

    /**
     * @brief Writes the edited abstract into the document.
     */
    void OnAbstractEdited();

    /**
     * @brief Writes the edited author fields into the document.
     */
    void OnAuthorEdited();

    /**
     * @brief Writes the author role radio box into the document.
     */
    void OnAuthorRoleChanged();

    /**
     * @brief Switches the shown author's postal address between
     *        structured parts and free-form lines, asking first
     *        when the switch would discard the lines.
     */
    void OnPostalModeChanged();

    /**
     * @brief Writes the edited section title into the document.
     */
    void OnSectionTitleEdited(const QString& Text);

    /**
     * @brief Writes the edited section body into the document.
     */
    void OnSectionBodyEdited();

private:
    /**
     * @brief The kinds of outline item, stored in Qt::UserRole.
     */
    enum OutlineKind {
        KindFront,
        KindAuthors,
        KindAuthor,
        KindAbstract,
        KindDate,
        KindMiddle,
        KindSection,
        KindBack,
        KindReferences,
        KindReferenceGroup,
        KindReferenceEntry,
        KindReferenceAuthor,
        KindReferenceInclude,
        KindDocumentSettings
    };

    /**
     * @brief Rebuilds the outline from the document, keeping the
     *        selection where the selected part still exists.
     */
    void RebuildOutline();

    /**
     * @brief Applies pending XML edits on the section, abstract,
     *        and settings-note editors (Bug 520).
     *
     * Called before a save and before switching outline items,
     * so an XML paste is not lost. Each editor's ApplyXmlEdits
     * shows its own error on a parse failure.
     * @return False when an editor refused to apply (parse error).
     */
    bool ApplyPendingXmlEdits();

    /**
     * @brief Rebuilds the recent-files entries in the File menu
     *        from the remembered list, dropping files that are
     *        no longer there.
     */
    void RebuildRecentMenu();

    /**
     * @brief Adds one section, with its children, to the outline.
     * @param Parent The outline item to add under.
     * @param Section The section node to add (borrowed).
     * @param Path The section's path (dot-separated indices).
     */
    void AddSectionItem(QTreeWidgetItem* Parent, xmlNodePtr Section,
                        const QString& Path);

    /**
     * @brief Adds one reference list, with its nested lists, entries,
     *        and their authors, to the outline.
     * @param Parent The outline item to add under.
     * @param Group The <references> node to add (borrowed).
     * @param Path The list's path (dot-separated indices).
     */
    void AddReferenceGroupItem(QTreeWidgetItem* Parent, xmlNodePtr Group,
                               const QString& Path);

    /**
     * @brief Shows the details pane for one outline item.
     * @param Item The outline item, or nullptr for the empty pane.
     */
    void ShowDetailsFor(QTreeWidgetItem* Item);

    /**
     * @brief Fills the document settings form with the document's
     *        title settings, series info, lists, and first note.
     */
    void FillDocumentSettingsForm();

    /**
     * @brief Fills the author form with the shown author.
     */
    void FillAuthorForm();

    /**
     * @brief Fills the reference list form with the shown list.
     */
    void FillReferenceGroupForm();

    /**
     * @brief The anchor an xi:include carries: the cached file's
     *        own anchor, or the anchor derived from its file name
     *        until it is fetched.
     * @param Include The <xi:include> node, borrowed.
     * @return The anchor, or empty.
     */
    QString IncludeAnchor(xmlNodePtr Include);

    /**
     * @brief The label for an xi:include: its title and anchor,
     *        with the words that cite it first.
     * @param Include The <xi:include> node, borrowed.
     * @return The label.
     */
    QString IncludeLabel(xmlNodePtr Include);

    /**
     * @brief The cited-words prefix for a reference label.
     *
     * The distinct words that cite the anchor, up to three, plus
     * " - "; empty when nothing cites the anchor.
     * @param Anchor The anchor the xrefs point at.
     * @return The prefix, or empty.
     */
    QString CitedLabelPrefix(const QString& Anchor);

    /**
     * @brief True when no cross-reference cites the anchor.
     *
     * When the anchor is still cited, warns the author how many
     * citations stand and returns false: a cited reference must
     * not be removed while the citations stand.
     * @param Anchor The anchor to check.
     * @return True when nothing cites the anchor.
     */
    bool ReferenceAnchorUnused(const QString& Anchor);

    /**
     * @brief Fills the reference entry form with the shown entry.
     */
    void FillReferenceEntryForm();

    /**
     * @brief Fills the author fields on the reference entry form
     *        with the selected author of the shown entry.
     */
    void FillReferenceAuthorFields();

    /**
     * @brief Returns the selected author of the shown reference
     *        entry.
     * @return The author node (borrowed), or nullptr when none is
     *         selected.
     */
    xmlNodePtr ShownReferenceAuthor();

    /**
     * @brief Returns the author the author form shows: a draft
     *        author, or an author of a reference entry.
     * @return The author node (borrowed), or nullptr when the
     *         shown author is gone.
     */
    xmlNodePtr ShownAuthor();

    /**
     * @brief Finds the reference list at a dotted path.
     * @param Group The list path, such as "0" or "0.1".
     * @return The list node (borrowed), or nullptr when the path
     *         is bad.
     */
    xmlNodePtr FindReferenceGroup(const QString& Group);

    /**
     * @brief Finds the reference entry at a list path and entry index.
     * @param Group The list path, such as "0" or "0.1".
     * @param Entry The entry index in that list.
     * @return The entry node (borrowed), or nullptr when an index
     *         is bad.
     */
    xmlNodePtr FindReferenceEntry(const QString& Group, int Entry);

    /**
     * @brief Fills the section form with the shown section.
     */
    void FillSectionForm();

    /**
     * @brief Refreshes the section controls for one outline item:
     *        the title edit, the required-section note, and the
     *        Insert, Remove, Up, and Down enablement (Bug 422).
     *
     * A required section (a top-level section whose title is one
     * of the three required titles) cannot be renamed, removed,
     * or moved, and no free section can move across one. Add and
     * Insert stay enabled: adding a section never moves a
     * required section relative to an existing one. Called on
     * every selection change and after every title edit, so a
     * section renamed to a required title locks at once.
     * @param Item The outline item, or nullptr when none is
     *        selected.
     */
    void RefreshSectionControls(QTreeWidgetItem* Item);

    /**
     * @brief Finds the section at a dot-separated index path.
     * @param Path The path, for example "0" or "0.2".
     * @return The section node (borrowed), or nullptr when the
     *         path is bad.
     */
    xmlNodePtr FindSection(const QString& Path);

    /**
     * @brief Returns the selection key for an outline item, used to
     *        keep the selection across outline rebuilds.
     * @param Item The outline item.
     * @return The key, for example "section:0.1" or "author:0".
     */
    QString SelectionKey(QTreeWidgetItem* Item) const;

    /**
     * @brief Finds the outline item for a selection key.
     * @param Parent The item to search under.
     * @param Key The key to find.
     * @return The item, or nullptr when not found.
     */
    QTreeWidgetItem* FindItemByKey(QTreeWidgetItem* Parent, const QString& Key) const;

    /**
     * @brief Marks the document modified or not, updating the title.
     * @param Modified True when the document has unsaved changes.
     */
    void SetModified(bool Modified);

    /**
     * @brief Updates the window title from the draft name and the
     *        modified state.
     */
    void UpdateWindowTitle();

    /**
     * @brief Points the spell checker at the open draft's project
     *        dictionary.
     *
     * The project dictionary lives beside the draft file (same
     * name, .dic extension). With no draft file open, the
     * project list is cleared. The text panes re-check their
     * underlines afterwards, since the project words changed.
     */
    void UpdateProjectDictionary();

    /**
     * @brief Asks about unsaved changes: save, discard, or cancel.
     * @return True when the caller may go on (saved or discarded).
     */
    bool ConfirmDiscard();

    /**
     * @brief Writes the document to a file, warning about problems,
     *        about content the reader skipped, and about literal
     *        lines longer than 69 characters.
     * @param Path The file to write.
     * @return True when the file was written.
     */
    bool SaveToFile(const QString& Path);

    /**
     * @brief Returns the remembered xml2rfc program path.
     * @return The path, or "xml2rfc" when none is remembered.
     */
    QString Xml2rfcPath() const;

    /**
     * @brief Finds the v3 schema file next to the program.
     * @return The schema path, or empty when not found.
     */
    QString FindSchemaPath() const;

    /**
     * @brief Returns the output file for a format, beside the
     *        saved draft and sharing its base name.
     * @param Extension The output extension, without the dot.
     * @return The output path, or empty when the draft has no
     *         saved file yet.
     */
    QString OutputFilePath(const QString& Extension) const;

    /**
     * @brief Enables each View button only when its format is
     *        checked and its output file exists.
     */
    void UpdateViewButtons();

    /**
     * @brief Opens an output file in its viewer: Notepad for
     *        text, the default browser for HTML, the default PDF
     *        viewer for PDF.
     * @param Extension The output extension, without the dot.
     */
    void ViewOutput(const QString& Extension);

    /**
     * @brief Validates a file and reports the result.
     * @param Path The file to validate.
     * @param AlwaysShow True to show the results dialog even when
     *        the file is valid; otherwise it shows only on problems.
     * @return The validation result.
     */
    DocValidationResult ValidateAndReport(const QString& Path, bool AlwaysShow);

    /**
     * @brief Leaves out the validation errors a resolved include explains.
     *
     * The report stays about the file itself, so its line numbers
     * match the file: an include the bibliography cache resolved
     * explains its own "did not expect element include there" and
     * the unknown-ID errors for the anchor it carries. An include
     * that did not resolve keeps its errors, and a failed download
     * gets a plain note. Returns how many includes were resolved.
     * @param Result The validation result to filter, in place.
     * @return The number of resolved includes.
     */
    int FilterIncludeErrors(DocValidationResult& Result);

    /**
     * @brief Reports the anchors more than one reference provides.
     *
     * A group include and another reference can both provide the
     * same anchor (BCP14 provides RFC2119, and so does the RFC2119
     * include); the duplicate fails validation after expansion, so
     * it is reported plainly here instead of as a bare duplicate-ID
     * error.
     * @param Result The validation result to append to, in place.
     */
    void ReportAnchorCollisions(DocValidationResult& Result);

    /**
     * @brief Appends text to the run log dialog.
     * @param Text The text to append.
     */
    void AppendRunLog(const QString& Text);

    /**
     * @brief Records what the just-loaded document holds for the
     * abstract (Bug 502 diagnostic), for the Abstract page to show.
     */
    void RecordLoadDiagnostic();

    DocDocument _Document;
    BibCache _BibCache;
    QString _FilePath;
    bool _Modified;
    // Load-time abstract counts (Bug 502 diagnostic).
    bool _DiagLoadFront;
    bool _DiagLoadAbstract;
    int _DiagLoadAbstractChildren;
    // Abstract-editor syncs since load (Bug 502 diagnostic).
    int _DiagSyncCount;
    int _DiagSyncLastNodes;
    // Bug 502 fix: true only while the abstract editor holds content
    // filled from the current document. A sync from an editor that
    // has not been filled for this document must not overwrite the
    // loaded abstract.
    bool _AbstractEditCurrent;
    std::unique_ptr<SpellChecker> _SpellChecker;
    QString _SelectedKey;
    int _AuthorIndex;
    QString _AuthorGroupPath;
    int _AuthorEntry;
    QString _ReferenceGroupPath;
    int _ReferenceEntryIndex;
    int _ReferenceAuthorIndex;
    int _ReferenceIncludeIndex;
    QString _SectionPath;
    bool _RunFailed;

    QMenu* _FileMenu;
    QAction* _SaveAction;
    QList<QAction*> _RecentActions;

    QTreeWidget* _Outline;
    QStackedWidget* _Details;
    QPushButton* _AddSectionButton;
    QPushButton* _InsertSectionButton;
    QPushButton* _RemoveSectionButton;
    QPushButton* _MoveUpButton;
    QPushButton* _MoveDownButton;

    QLabel* _GroupLabel;
    TextEditor* _AbstractEdit;
    QLabel* _AbstractPreservedLabel;
    QLabel* _DateLabel;
    QLabel* _ReferencesLabel;
    QLineEdit* _AuthorFullnameEdit;
    QLineEdit* _AuthorInitialsEdit;
    QLineEdit* _AuthorSurnameEdit;
    QLineEdit* _AuthorOrganizationEdit;
    QLineEdit* _AuthorEmailEdit;
    QLineEdit* _AuthorPhoneEdit;
    QLineEdit* _AuthorUriEdit;
    QLineEdit* _AuthorPostalStreetEdit;
    QLineEdit* _AuthorPostalCityareaEdit;
    QLineEdit* _AuthorPostalExtaddrEdit;
    QLineEdit* _AuthorPostalPoboxEdit;
    QLineEdit* _AuthorPostalCityEdit;
    QLineEdit* _AuthorPostalRegionEdit;
    QLineEdit* _AuthorPostalCodeEdit;
    QLineEdit* _AuthorPostalCountryEdit;
    QLineEdit* _AuthorPostalSortingcodeEdit;
    QComboBox* _AuthorPostalModeCombo;
    QStackedWidget* _AuthorPostalStack;
    QPlainTextEdit* _AuthorPostalLinesEdit;
    QRadioButton* _AuthorRadio;
    QRadioButton* _EditorRadio;
    QLabel* _AuthorPreservedLabel;
    QLineEdit* _SectionTitleEdit;
    TextEditor* _SectionBodyEdit;
    QLabel* _SectionPreservedLabel;
    QLabel* _SectionRequiredLabel;
    QLineEdit* _ReferenceGroupNameEdit;
    QPushButton* _AddReferenceEntryButton;
    QPushButton* _AddNestedListButton;
    QLabel* _ReferenceGroupPreservedLabel;
    QLabel* _ReferenceIncludeLabel;
    QPushButton* _RemoveReferenceIncludeButton;
    QRadioButton* _ReferenceIncludeNormativeRadio;
    QRadioButton* _ReferenceIncludeInformativeRadio;
    QTableWidget* _DisplayReferenceTable;
    QLineEdit* _ReferenceAnchorEdit;
    QLineEdit* _ReferenceTargetEdit;
    QLineEdit* _ReferenceTitleEdit;
    QLineEdit* _ReferenceDateDayEdit;
    QLineEdit* _ReferenceDateMonthEdit;
    QLineEdit* _ReferenceDateYearEdit;
    QComboBox* _ReferenceStreamCombo;
    QTableWidget* _ReferenceSeriesTable;
    QLineEdit* _ReferenceAnnotationEdit;
    QComboBox* _ReferenceMoveToCombo;
    QRadioButton* _ReferenceNormativeRadio;
    QRadioButton* _ReferenceInformativeRadio;
    QListWidget* _ReferenceAuthorList;
    QLineEdit* _ReferenceAuthorFullnameEdit;
    QLineEdit* _ReferenceAuthorInitialsEdit;
    QLineEdit* _ReferenceAuthorSurnameEdit;
    QLineEdit* _ReferenceAuthorOrganizationEdit;
    QLineEdit* _ReferenceAuthorEmailEdit;
    QLineEdit* _ReferenceAuthorPhoneEdit;
    QLineEdit* _ReferenceAuthorUriEdit;
    QRadioButton* _ReferenceAuthorRadio;
    QRadioButton* _ReferenceEditorRadio;
    QPushButton* _RemoveReferenceAuthorButton;
    QLabel* _ReferenceEntryPreservedLabel;
    QLineEdit* _SettingsTitleEdit;
    QLineEdit* _SettingsTitleAbbrevEdit;
    QLineEdit* _SettingsTitleAsciiEdit;
    QLabel* _SettingsTitleBreaksLabel;
    QTableWidget* _SettingsSeriesTable;
    QLabel* _SettingsSeriesNoteLabel;
    QPlainTextEdit* _SettingsAreasEdit;
    QPlainTextEdit* _SettingsWorkgroupsEdit;
    QPlainTextEdit* _SettingsKeywordsEdit;
    QGroupBox* _SettingsNoteGroup;
    QLineEdit* _SettingsNoteNameEdit;
    QCheckBox* _SettingsNoteRemoveCheck;
    TextEditor* _SettingsNoteEdit;
    QPushButton* _SettingsAddNoteButton;
    QLabel* _SettingsNoteExtraLabel;
    QLabel* _SettingsNotePreservedLabel;
    QLabel* _SettingsPreservedLabel;

    QCheckBox* _TextFormatCheck;
    QCheckBox* _PdfFormatCheck;
    QCheckBox* _HtmlFormatCheck;
    QPushButton* _TextViewButton;
    QPushButton* _PdfViewButton;
    QPushButton* _HtmlViewButton;

    QProcess* _RunProcess;
    QDialog* _RunDialog;
    QPlainTextEdit* _RunLog;
    QPushButton* _RunCloseButton;

    QDialog* _ValidateDialog;
    QPlainTextEdit* _ValidateLog;
    QPushButton* _ValidateCloseButton;
};

#endif // DOCEDITOR_MAINWINDOW_H
