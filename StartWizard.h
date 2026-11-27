// Copyright (c) 2026 RiverExplorer LLC
// RiverExplorer is a registered trademark of Douglas Mark Royer.
//
// This software is released under the MIT License.
// See LICENSE.md for the full license text.

/**
 * @file StartWizard.h
 * @brief Start wizard for the InternetDraftsEditor (install-tool style,
 *        Next/Previous).
 *
 * The wizard collects the minimum information a draft needs, one
 * pane at a time: the first author, the title with the draft name
 * and version, and the folder the XML file goes in. Then a summary
 * page where the user confirms with "Create now". The first page
 * lets the user open an existing draft XML file instead, or create
 * a new version of one (Bug 435).
 *
 * File > New Draft runs the wizard in create-only mode: it starts
 * at the author page and the first page is skipped, so the user
 * only enters the new draft's details.
 */

#ifndef DOCEDITOR_STARTWIZARD_H
#define DOCEDITOR_STARTWIZARD_H

#include <QLabel>
#include <QLineEdit>
#include <QWizard>
#include <QWizardPage>

#include "DocModel.h"

class QComboBox;
class QPushButton;
class QRadioButton;

/**
 * @brief First wizard page: open an existing draft or create a new one.
 *
 * Four choices: "Open a draft file" picks an XML file with the file
 * dialog; clicking the Finish button ("Open") opens it. "Create a
 * new draft" moves on to the setup pages and starts from scratch.
 * "Create from an IETF template" moves on to the setup pages too,
 * and the entered name, title, and author replace the picked
 * template's placeholders (Bug 431). "Create new version of
 * existing draft" picks an XML file, loads it, bumps its version
 * by one, and moves on to the setup pages prefilled from it; the
 * whole draft is copied into the new version (Bug 435). When a
 * previous session opened a draft, that file is already shown, so
 * opening it again needs no Browse (Bug 319).
 */
class ChooseDraftPage : public QWizardPage
{
    Q_OBJECT

public:
    /**
     * @brief Creates the page.
     * @param Parent The parent widget, or nullptr.
     */
    explicit ChooseDraftPage(QWidget* Parent = nullptr);

    /**
     * @brief Returns the next page: the author page when the
     *        user is creating a draft or a new version, or -1
     *        (finish) when the user is opening a draft file.
     * @return The next page id, or -1 to finish the wizard.
     */
    int nextId() const override;

    /**
     * @brief Returns true when the page can be left: always for
     *        creating a draft, only with a file picked when
     *        opening one or creating a new version of one.
     * @return True when the page is complete.
     */
    bool isComplete() const override;

    /**
     * @brief Loads the picked file when a new version is picked:
     *        the draft must load and its version must bump, or
     *        the wizard explains why and stays on this page.
     *        Other choices always validate.
     * @return True when the page may be left.
     */
    bool validatePage() override;

    /**
     * @brief Resets the Finish button text when the page is shown.
     */
    void initializePage() override;

    /**
     * @brief Returns true when the user picked opening a draft file
     *        (rather than creating a new draft).
     * @return True when opening a draft.
     */
    bool IsOpeningDraft() const;

    /**
     * @brief Returns true when the user picked creating a draft
     *        from an IETF template (rather than from scratch).
     * @return True when creating from a template.
     */
    bool IsCreatingFromTemplate() const;

    /**
     * @brief Returns true when the user picked creating a new
     *        version of an existing draft (Bug 435).
     * @return True when creating a new version.
     */
    bool IsCreatingNewVersion() const;

    /**
     * @brief Returns the resource path of the picked template
     *        (for example :/templates/standard.xml), or empty
     *        when no template is picked.
     * @return The picked template's resource path.
     */
    QString GetTemplateResource() const;

    /**
     * @brief Returns the draft XML file picked with the file dialog
     *        (empty when none is picked).
     * @return The selected file path.
     */
    QString GetSelectedFile() const;

    /**
     * @brief Sets the page id of the author page (the next page
     *        when creating a draft).
     * @param PageId The author page id.
     */
    void SetAuthorPageId(int PageId);

private slots:
    /**
     * @brief Applies a change of choice: updates the Finish button
     *        text and re-checks completeness.
     */
    void OnChoiceChanged();

    /**
     * @brief Opens the file dialog to pick a draft file to open or
     *        to copy into a new version. Picking a file shows its
     *        path and re-checks completeness. When neither the
     *        open choice nor the new-version choice is picked,
     *        picking a file checks the "Open a draft file"
     *        choice. Cancelling the dialog changes nothing.
     */
    void OnBrowseFile();

private:
    /**
     * @brief Splits a draft name into its base and its version
     *        bumped by one (Bug 435).
     *
     * The name must end with a dash and exactly two digits, and
     * the version must be below 99.
     * @param DocName The loaded draft's docName.
     * @param BaseName Set to the part before the version dash.
     * @param Version Set to the bumped version, two digits.
     * @param Reason Set to the explanation when the version
     *        cannot be bumped.
     * @return True when the version bumped.
     */
    static bool BumpDraftVersion(const QString& DocName,
                                 QString& BaseName,
                                 QString& Version,
                                 QString& Reason);

    QRadioButton* _OpenFileRadio;
    QRadioButton* _CreateNewRadio;
    QRadioButton* _CreateTemplateRadio;
    QRadioButton* _NewVersionRadio;
    QComboBox* _TemplateCombo;
    QPushButton* _BrowseButton;
    QLabel* _PickedFileLabel;
    QString _PickedFile;
    int _AuthorPageId;
};

/**
 * @brief Wizard page asking for the draft title, name, and version.
 *
 * The title is shown on the draft's first page. The name and
 * version join with a dash to become the file name and the
 * docName attribute, for example draft-royer-coop-00: the name
 * must not be empty and must not hold spaces, and the version
 * is two digits. The name prefills from the first author's last
 * name and the version prefills with 00 when the page is first
 * shown (Bug 413); both stay editable. When a new version is
 * being created, the title, name, and version prefill from the
 * loaded draft instead, with the version bumped (Bug 435).
 */
class TitlePage : public QWizardPage
{
    Q_OBJECT

public:
    /**
     * @brief Creates the page.
     * @param Parent The parent widget, or nullptr.
     */
    explicit TitlePage(QWidget* Parent = nullptr);

    /**
     * @brief Prefills the draft name and version when the page is
     *        shown: the name from the author's surname, the
     *        version with 00; or, when a new version is being
     *        created, the title, name, and bumped version from
     *        the loaded draft. On re-entry after Back, a field
     *        refreshes only when the user has not edited it.
     */
    void initializePage() override;

    /**
     * @brief Returns true when the title is non-empty, the name
     *        is non-empty and holds no spaces, and the version
     *        is two digits.
     * @return True when the page is complete.
     */
    bool isComplete() const override;

private:
    QLineEdit* _TitleEdit;
    QLineEdit* _NameEdit;
    QLineEdit* _VersionEdit;
    /** The last computed draft-name prefill, to detect manual edits. */
    QString _LastPrefill;
    /** The last computed title prefill, to detect manual edits. */
    QString _LastTitlePrefill;
    /** The last computed version prefill, to detect manual edits. */
    QString _LastVersionPrefill;
};

/**
 * @brief Wizard page asking for the first author.
 *
 * The full name is required; the organization and email are
 * optional and can be added later in the editor. The author's
 * own information is remembered between runs and prefilled
 * here (Bug 413); it is still asked, not assumed. When a new
 * version is being created, the fields prefill from the loaded
 * draft's first author instead (Bug 435).
 */
class AuthorPage : public QWizardPage
{
    Q_OBJECT

public:
    /**
     * @brief Creates the page.
     * @param Parent The parent widget, or nullptr.
     */
    explicit AuthorPage(QWidget* Parent = nullptr);

    /**
     * @brief Prefills the fields from the loaded draft's first
     *        author when a new version is being created. On
     *        re-entry after Back, a field refreshes only when
     *        the user has not edited it. Other creations keep
     *        the remembered values set at construction.
     */
    void initializePage() override;

    /**
     * @brief Returns true when the full name is non-empty.
     * @return True when the page is complete.
     */
    bool isComplete() const override;

private:
    QLineEdit* _NameEdit;
    QLineEdit* _OrganizationEdit;
    QLineEdit* _EmailEdit;
    /** The last computed full-name prefill, to detect manual edits. */
    QString _LastNamePrefill;
    /** The last computed organization prefill, to detect manual edits. */
    QString _LastOrganizationPrefill;
    /** The last computed email prefill, to detect manual edits. */
    QString _LastEmailPrefill;
};

/**
 * @brief Wizard page asking for the folder the XML file goes in.
 */
class FolderPage : public QWizardPage
{
    Q_OBJECT

public:
    /**
     * @brief Creates the page.
     * @param Parent The parent widget, or nullptr.
     */
    explicit FolderPage(QWidget* Parent = nullptr);

    /**
     * @brief Prefills the folder with the source file's folder
     *        when a new version is being created. On re-entry
     *        after Back, the folder refreshes only when the user
     *        has not edited it or picked another. Other
     *        creations keep the remembered folder set at
     *        construction.
     */
    void initializePage() override;

    /**
     * @brief Returns true when a folder is entered.
     * @return True when the page is complete.
     */
    bool isComplete() const override;

private slots:
    /**
     * @brief Opens the folder dialog to pick the save folder.
     */
    void OnBrowseFolder();

private:
    QLineEdit* _FolderEdit;
    QPushButton* _BrowseButton;
    /** The last computed folder prefill, to detect manual edits. */
    QString _LastPrefill;
};

/**
 * @brief Final wizard page showing a summary and the "Create now" button.
 */
class CreatePage : public QWizardPage
{
    Q_OBJECT

public:
    /**
     * @brief Creates the page.
     * @param Parent The parent widget, or nullptr.
     */
    explicit CreatePage(QWidget* Parent = nullptr);

    /**
     * @brief Fills the summary label from the earlier pages when shown.
     *
     * When a new version is being created, the summary says the
     * new version copies the selected draft, instead of listing
     * the prefilled starting outline.
     */
    void initializePage() override;

private:
    QLabel* _SummaryLabel;
};

/**
 * @brief The start wizard: one pop-up stepping through new-draft
 *        setup, or opening an existing draft file.
 *
 * In create-only mode (File > New Draft) the wizard starts at the
 * author page: the open-or-create first page is skipped and the
 * wizard can only collect a new draft.
 */
class StartWizard : public QWizard
{
    Q_OBJECT

public:
    /**
     * @brief Creates the wizard with all of its pages.
     * @param Parent The parent widget, or nullptr.
     * @param CreateOnly True to start at the author page and
     *        skip the open-or-create first page (File > New Draft);
     *        false for the full start wizard.
     */
    explicit StartWizard(QWidget* Parent = nullptr, bool CreateOnly = false);

    /**
     * @brief Returns the document collected by the wizard.
     *
     * The document is filled in when the wizard is accepted with a
     * new draft (the user clicks "Create now"). When the wizard is
     * accepted with an existing draft file, it is empty; use
     * GetSelectedDraftFile() instead.
     * @return The collected document.
     */
    DocDocument GetDocument() const;

    /**
     * @brief Returns the file path the new draft will be saved to:
     *        the picked folder plus the draft name plus ".xml".
     * @return The new draft's file path.
     */
    QString GetDocumentFilePath() const;

    /**
     * @brief Returns true when the wizard was accepted with an
     *        existing draft file (rather than a new draft).
     * @return True when opening a draft.
     */
    bool IsOpeningDraft() const;

    /**
     * @brief Returns the draft file picked in the wizard (empty
     *        unless IsOpeningDraft() is true).
     * @return The selected file path.
     */
    QString GetSelectedDraftFile() const;

    /**
     * @brief Returns true when the wizard is creating a new
     *        version of an existing draft (Bug 435): the choice
     *        page picked "Create new version of existing
     *        draft". False in create-only mode, which has no
     *        choice page.
     * @return True when creating a new version.
     */
    bool IsNewVersionMode() const;

    /**
     * @brief Returns the draft loaded from the file picked for a
     *        new version, or an empty document when no source
     *        was stored.
     * @return The loaded source document.
     */
    DocDocument NewVersionDocument() const;

    /**
     * @brief Returns the draft-name base (without the version)
     *        for the new version, from the loaded draft's
     *        docName.
     * @return The new version's base name.
     */
    QString NewVersionBaseName() const;

    /**
     * @brief Returns the bumped two-digit version for the new
     *        version.
     * @return The bumped version.
     */
    QString NewVersionVersion() const;

    /**
     * @brief Returns the source file picked for the new version.
     * @return The source file path.
     */
    QString NewVersionSourceFile() const;

    /**
     * @brief Stores the loaded source draft and its bumped name
     *        for the new-version flow, called when the choice
     *        page is left with a new version picked.
     * @param Document The loaded source document.
     * @param BaseName The draft-name base, without the version.
     * @param Version The bumped two-digit version.
     * @param SourceFile The picked source file path.
     */
    void SetNewVersionSource(const DocDocument& Document,
                             const QString& BaseName,
                             const QString& Version,
                             const QString& SourceFile);

    /**
     * @brief Collects the entered values into the document (new
     *        draft) or records the picked draft file, then accepts.
     */
    void accept() override;

private:
    DocDocument _Document;
    QString _DocumentFilePath;
    ChooseDraftPage* _ChoicePage;
    int _TitlePageId;
    bool _OpeningDraft;
    QString _SelectedDraftFile;
    bool _CreateOnly;
    DocDocument _NewVersionDocument;
    QString _NewVersionBaseName;
    QString _NewVersionVersion;
    QString _NewVersionSourceFile;
    bool _HasNewVersionSource;
};

#endif // DOCEDITOR_STARTWIZARD_H
