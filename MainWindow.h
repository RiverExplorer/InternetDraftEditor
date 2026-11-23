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

#include "DocModel.h"
#include "DocValidator.h"
#include "SpellChecker.h"

class QAction;
class QCheckBox;
class QComboBox;
class QDialog;
class QGroupBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QStackedWidget;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;
class TextEditor;

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
     * @brief Adds a reference entry to the shown list, asking for
     *        its anchor first.
     */
    void OnAddReferenceEntry();

    /**
     * @brief Removes the shown reference entry, after confirming.
     */
    void OnRemoveReferenceEntry();

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
        KindDocumentSettings
    };

    /**
     * @brief Rebuilds the outline from the document, keeping the
     *        selection where the selected part still exists.
     */
    void RebuildOutline();

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
     * @brief Fills the reference entry form with the shown entry.
     */
    void FillReferenceEntryForm();

    /**
     * @brief Returns the author the author form shows: a draft
     *        author, or an author of a reference entry.
     * @return The author node (borrowed), or nullptr when the
     *         shown author is gone.
     */
    xmlNodePtr ShownAuthor();

    /**
     * @brief Finds the reference list at an index.
     * @param Group The list index.
     * @return The list node (borrowed), or nullptr when the index
     *         is bad.
     */
    xmlNodePtr FindReferenceGroup(int Group);

    /**
     * @brief Finds the reference entry at a list and entry index.
     * @param Group The list index.
     * @param Entry The entry index in that list.
     * @return The entry node (borrowed), or nullptr when an index
     *         is bad.
     */
    xmlNodePtr FindReferenceEntry(int Group, int Entry);

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
     * @brief Appends text to the run log dialog.
     * @param Text The text to append.
     */
    void AppendRunLog(const QString& Text);

    DocDocument _Document;
    QString _FilePath;
    bool _Modified;
    std::unique_ptr<SpellChecker> _SpellChecker;
    QString _SelectedKey;
    int _AuthorIndex;
    int _AuthorGroup;
    int _AuthorEntry;
    int _ReferenceGroupIndex;
    int _ReferenceEntryIndex;
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
    QLabel* _ReferenceGroupPreservedLabel;
    QLineEdit* _ReferenceAnchorEdit;
    QLineEdit* _ReferenceTargetEdit;
    QLineEdit* _ReferenceTitleEdit;
    QLineEdit* _ReferenceDateDayEdit;
    QLineEdit* _ReferenceDateMonthEdit;
    QLineEdit* _ReferenceDateYearEdit;
    QComboBox* _ReferenceStreamCombo;
    QTableWidget* _ReferenceSeriesTable;
    QComboBox* _ReferenceMoveToCombo;
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
