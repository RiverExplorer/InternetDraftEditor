// Copyright (c) 2026 RiverExplorer LLC
// RiverExplorer is a registered trademark of Douglas Mark Royer.
//
// This software is released under the MIT License.
// See LICENSE.md for the full license text.

/**
 * @file StartWizard.cpp
 * @brief Implementation of the start wizard and its pages.
 */

#include "StartWizard.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QStringList>
#include <QVBoxLayout>

#include <libxml/tree.h>

#include "MainWindow.h"

/**
 * @brief Casts a string literal to libxml2's xmlChar pointer type.
 * @param Text The text to cast.
 * @return The text as an xmlChar pointer.
 */
static const xmlChar* XmlChars(const char* Text)
{
    return reinterpret_cast<const xmlChar*>(Text);
}

/**
 * @brief Creates the first page: open an existing draft or create one.
 */
ChooseDraftPage::ChooseDraftPage(QWidget* Parent)
    : QWizardPage(Parent)
    , _OpenFileRadio(nullptr)
    , _CreateNewRadio(nullptr)
    , _CreateTemplateRadio(nullptr)
    , _NewVersionRadio(nullptr)
    , _TemplateCombo(nullptr)
    , _BrowseButton(nullptr)
    , _PickedFileLabel(nullptr)
    , _PickedFile()
    , _AuthorPageId(-1)
{
    setTitle(tr("Open or create a draft"));
    setSubTitle(tr("Open a draft XML file you worked on before, create "
                   "a new draft from scratch or from an IETF template, "
                   "or create a new version of an existing draft."));

    _OpenFileRadio = new QRadioButton(tr("Open a draft file"), this);
    _OpenFileRadio->setToolTip(tr("Open a draft by picking its XML file."));
    _CreateNewRadio = new QRadioButton(tr("Create a new draft"), this);
    _CreateNewRadio->setToolTip(tr("Start a new draft from scratch."));
    _CreateNewRadio->setChecked(true);
    _CreateTemplateRadio =
        new QRadioButton(tr("Create from an IETF template"), this);
    _CreateTemplateRadio->setToolTip(
        tr("Start a new draft from one of the IETF's templates."));
    _NewVersionRadio =
        new QRadioButton(tr("Create new version of existing draft"), this);
    _NewVersionRadio->setToolTip(
        tr("Copy an existing draft into a new version, with its "
           "version number bumped by one."));

    // The template picker, enabled only when the template choice
    // is picked. Each item carries its resource path (Bug 431).
    _TemplateCombo = new QComboBox(this);
    _TemplateCombo->setToolTip(tr("The IETF template to start from."));
    _TemplateCombo->addItem(tr("Standard"),
                            QStringLiteral(":/templates/standard.xml"));
    _TemplateCombo->addItem(tr("Bare"), QStringLiteral(":/templates/bare.xml"));
    _TemplateCombo->addItem(tr("Annotated"),
                            QStringLiteral(":/templates/annotated.xml"));
    _TemplateCombo->setEnabled(false);

    // The file picked with Browse, shown beside the button.
    // The label wraps so a long path does not widen the wizard.
    _PickedFileLabel = new QLabel(tr("No file chosen."), this);
    _PickedFileLabel->setWordWrap(true);
    _PickedFileLabel->setToolTip(tr("No draft file chosen yet."));
    _BrowseButton = new QPushButton(tr("Browse..."), this);
    _BrowseButton->setToolTip(tr("Pick a draft file (.xml) to open."));

    connect(_OpenFileRadio, &QRadioButton::toggled,
            this, &ChooseDraftPage::OnChoiceChanged);
    connect(_CreateNewRadio, &QRadioButton::toggled,
            this, &ChooseDraftPage::OnChoiceChanged);
    connect(_CreateTemplateRadio, &QRadioButton::toggled,
            this, &ChooseDraftPage::OnChoiceChanged);
    connect(_NewVersionRadio, &QRadioButton::toggled,
            this, &ChooseDraftPage::OnChoiceChanged);
    connect(_BrowseButton, &QPushButton::clicked,
            this, &ChooseDraftPage::OnBrowseFile);

    QHBoxLayout* BrowseRow = new QHBoxLayout();
    BrowseRow->addWidget(_PickedFileLabel, 1);
    BrowseRow->addWidget(_BrowseButton);

    QHBoxLayout* TemplateRow = new QHBoxLayout();
    TemplateRow->addSpacing(24);
    TemplateRow->addWidget(new QLabel(tr("Template:"), this));
    TemplateRow->addWidget(_TemplateCombo, 1);

    QVBoxLayout* Layout = new QVBoxLayout(this);
    Layout->addWidget(_OpenFileRadio);
    Layout->addLayout(BrowseRow);
    Layout->addWidget(_NewVersionRadio);
    Layout->addWidget(_CreateNewRadio);
    Layout->addWidget(_CreateTemplateRadio);
    Layout->addLayout(TemplateRow);
    setLayout(Layout);

    // Remember the last draft: when a previous session opened a
    // draft file, offer it here so Browse is not needed every
    // time (Bug 319). The choice itself stays "Create a new
    // draft" until the user picks "Open a draft file".
    QStringList Recent = MainWindow::RecentFiles();
    if (!Recent.isEmpty() && QFile::exists(Recent.first())) {
        _PickedFile = Recent.first();
        _PickedFileLabel->setText(_PickedFile);
        _PickedFileLabel->setToolTip(_PickedFile);
    }
}

int ChooseDraftPage::nextId() const
{
    if (IsOpeningDraft()) {
        return -1;
    }
    return _AuthorPageId;
}

bool ChooseDraftPage::isComplete() const
{
    if (IsOpeningDraft() || IsCreatingNewVersion()) {
        return !_PickedFile.isEmpty();
    }
    return true;
}

bool ChooseDraftPage::validatePage()
{
    if (!IsCreatingNewVersion()) {
        return true;
    }
    StartWizard* Wizard = qobject_cast<StartWizard*>(wizard());
    if (Wizard == nullptr) {
        return true;
    }
    // The picked file loads here, with the same load the editor's
    // File > Open uses, so a bad file is refused before the
    // setup pages prefill from it. The version must bump too:
    // a draft whose name has no two-digit version, or one
    // already at 99, cannot start a new version. Either failure
    // explains itself and stays on this page.
    DocDocument Loaded;
    QString Error;
    if (!Loaded.ReadXml(_PickedFile, Error)) {
        QMessageBox::warning(this, tr("Create New Version"),
                             tr("Could not open %1:\n%2")
                                 .arg(_PickedFile, Error));
        return false;
    }
    QString BaseName;
    QString Version;
    QString Reason;
    if (!BumpDraftVersion(Loaded.DocName(), BaseName, Version, Reason)) {
        QMessageBox::warning(this, tr("Create New Version"), Reason);
        return false;
    }
    Wizard->SetNewVersionSource(Loaded, BaseName, Version, _PickedFile);
    return true;
}

void ChooseDraftPage::initializePage()
{
    OnChoiceChanged();
}

bool ChooseDraftPage::IsOpeningDraft() const
{
    return _OpenFileRadio->isChecked();
}

bool ChooseDraftPage::IsCreatingFromTemplate() const
{
    return _CreateTemplateRadio->isChecked();
}

bool ChooseDraftPage::IsCreatingNewVersion() const
{
    return _NewVersionRadio->isChecked();
}

QString ChooseDraftPage::GetTemplateResource() const
{
    if (!IsCreatingFromTemplate()) {
        return QString();
    }
    return _TemplateCombo->currentData().toString();
}

QString ChooseDraftPage::GetSelectedFile() const
{
    return _PickedFile;
}

void ChooseDraftPage::SetAuthorPageId(int PageId)
{
    _AuthorPageId = PageId;
}

bool ChooseDraftPage::BumpDraftVersion(const QString& DocName,
                                       QString& BaseName,
                                       QString& Version,
                                       QString& Reason)
{
    int Dash = DocName.lastIndexOf(QLatin1Char('-'));
    QString Tail = (Dash >= 0) ? DocName.mid(Dash + 1) : QString();
    bool TwoDigits = Tail.size() == 2 && Tail.at(0).isDigit() &&
                     Tail.at(1).isDigit();
    if (Dash <= 0 || !TwoDigits) {
        Reason = tr("The selected draft's name, \"%1\", does not end "
                    "with a two-digit version number (for example "
                    "-00), so a new version cannot be created from "
                    "it.").arg(DocName);
        return false;
    }
    int Number = Tail.toInt();
    if (Number >= 99) {
        Reason = tr("The selected draft is already at version 99, "
                    "so a new version cannot be created from it.");
        return false;
    }
    BaseName = DocName.left(Dash);
    Version = QStringLiteral("%1").arg(Number + 1, 2, 10,
                                       QLatin1Char('0'));
    return true;
}

void ChooseDraftPage::OnChoiceChanged()
{
    // The template picker is only live when the template choice
    // is picked.
    _TemplateCombo->setEnabled(IsCreatingFromTemplate());
    // The Browse button serves both file choices: opening a
    // draft, and copying one into a new version. It stays
    // enabled for the create choices too, as before: picking a
    // file from a create choice switches to opening it.
    if (IsCreatingNewVersion()) {
        _BrowseButton->setToolTip(
            tr("Pick the draft file (.xml) to copy into a new version."));
    } else {
        _BrowseButton->setToolTip(tr("Pick a draft file (.xml) to open."));
    }
    // Bug 304 (from the CoOp editor): emit first, then force the
    // button visibility below. The emit lets QWizard refresh its own
    // button states, and the forcing after it wins: QWizard does
    // not reliably swap Next for Finish when the choice changes, and
    // isFinalPage() is not virtual in Qt 6, so it cannot be
    // overridden to fix this.
    emit completeChanged();
    if (wizard() != nullptr) {
        if (IsOpeningDraft()) {
            wizard()->setButtonText(QWizard::FinishButton, tr("Open"));
            wizard()->button(QWizard::FinishButton)
                ->setToolTip(tr("Open the selected draft."));
            // Opening finishes here, so Finish shows and Next hides.
            wizard()->button(QWizard::FinishButton)->setVisible(true);
            wizard()->button(QWizard::NextButton)->setVisible(false);
        } else {
            wizard()->setButtonText(QWizard::FinishButton, tr("Create now"));
            wizard()->button(QWizard::FinishButton)->setToolTip(
                tr("Create the draft and open it in the editor."));
            // Creating continues to the setup pages, so Next shows
            // and Finish hides. Finish must never accept an unnamed
            // draft from this page.
            wizard()->button(QWizard::NextButton)->setVisible(true);
            wizard()->button(QWizard::FinishButton)->setVisible(false);
        }
        // QWizard does not reliably refresh the button enabled
        // states when the choice changes either, so set them here
        // too: the shown action button follows isComplete().
        wizard()->button(QWizard::NextButton)->setEnabled(isComplete());
        wizard()->button(QWizard::FinishButton)->setEnabled(isComplete());
    }
}

void ChooseDraftPage::OnBrowseFile()
{
    // The dialog opens only when Browse is clicked, never on its own.
    // It starts in the folder of the file already shown, when there
    // is one, so reopening a draft in the same folder is one pick.
    QString StartDir = QDir::homePath();
    if (!_PickedFile.isEmpty()) {
        StartDir = QFileInfo(_PickedFile).absolutePath();
    }
    QString DialogTitle = IsCreatingNewVersion()
        ? tr("Pick Draft for New Version") : tr("Open Draft");
    QString File = QFileDialog::getOpenFileName(
        this, DialogTitle, StartDir,
        tr("Draft files (*.xml);;All files (*)"));
    if (File.isEmpty()) {
        return;
    }
    _PickedFile = File;
    _PickedFileLabel->setText(File);
    _PickedFileLabel->setToolTip(File);
    // Picking a file from a create choice switches to opening it,
    // as before; from the new-version choice it stays there.
    if (!IsCreatingNewVersion()) {
        _OpenFileRadio->setChecked(true);
    }
    OnChoiceChanged();
}

/**
 * @brief Creates the title page: title, draft name, and version.
 *
 * The draft name and version moved here from their own page
 * (Bug 413): the author page comes first, so the name can
 * prefill from the author's last name when this page is shown.
 */
TitlePage::TitlePage(QWidget* Parent)
    : QWizardPage(Parent)
    , _TitleEdit(nullptr)
    , _NameEdit(nullptr)
    , _VersionEdit(nullptr)
    , _LastPrefill()
    , _LastTitlePrefill()
    , _LastVersionPrefill()
{
    setTitle(tr("Title"));
    setSubTitle(tr("The title shown on the draft's first page, and "
                   "the name and version number, for example "
                   "draft-royer-coop, version 00. Joined with a "
                   "dash, they become the XML file name and the "
                   "draft's docName, so the name holds no spaces."));

    _TitleEdit = new QLineEdit(this);
    _TitleEdit->setPlaceholderText(tr("CO-OP: ..."));
    _TitleEdit->setToolTip(tr("The draft title."));
    _NameEdit = new QLineEdit(this);
    _NameEdit->setPlaceholderText(tr("draft-royer-coop"));
    _NameEdit->setToolTip(tr("The draft name, with no spaces."));
    _VersionEdit = new QLineEdit(this);
    _VersionEdit->setPlaceholderText(tr("00"));
    _VersionEdit->setToolTip(tr("The version number, two digits."));
    _VersionEdit->setValidator(
        new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\d{0,2}")), _VersionEdit));
    _VersionEdit->setMaxLength(2);

    // Not mandatory "*" fields: completeness is the custom
    // isComplete() below (title non-empty, name non-empty with
    // no spaces, version two digits), and a mandatory field's
    // changed-detection does not suit a custom rule, nor a
    // prefilled value (Bug 304, from the CoOp editor).
    registerField("title", _TitleEdit);
    registerField("draftBaseName", _NameEdit);
    registerField("draftVersion", _VersionEdit);
    connect(_TitleEdit, &QLineEdit::textChanged,
            this, &QWizardPage::completeChanged);
    connect(_NameEdit, &QLineEdit::textChanged,
            this, &QWizardPage::completeChanged);
    connect(_VersionEdit, &QLineEdit::textChanged,
            this, &QWizardPage::completeChanged);

    QFormLayout* Layout = new QFormLayout(this);
    Layout->addRow(tr("Title:"), _TitleEdit);
    Layout->addRow(tr("Draft name:"), _NameEdit);
    Layout->addRow(tr("Version:"), _VersionEdit);
    setLayout(Layout);
}

void TitlePage::initializePage()
{
    StartWizard* Wizard = qobject_cast<StartWizard*>(wizard());
    bool NewVersion = Wizard != nullptr && Wizard->IsNewVersionMode();
    // A new version keeps the original draft's name (Bug 435,
    // v2): the name field is read-only in new-version mode, so
    // it cannot be changed into a different draft. Set on every
    // entry, so the state holds across Back and Next.
    _NameEdit->setReadOnly(NewVersion);
    _NameEdit->setToolTip(NewVersion
        ? tr("The draft name from the original draft. A new "
             "version keeps the same name.")
        : tr("The draft name, with no spaces."));
    if (NewVersion) {
        // Prefill from the loaded draft (Bug 435): its title, its
        // draft name without the version, and the bumped version.
        // A field refreshes only when it still holds the last
        // prefill (or is empty): a manual edit is never
        // clobbered, and picking a different source file on the
        // way back refreshes the untouched fields.
        QString LoadedTitle = Wizard->NewVersionDocument().Title();
        if (_TitleEdit->text().isEmpty() ||
            _TitleEdit->text() == _LastTitlePrefill) {
            _TitleEdit->setText(LoadedTitle);
        }
        _LastTitlePrefill = LoadedTitle;
    }
    // Prefill once (Bug 413): the draft name from the first
    // author's surname, lowercased, and the version with 00.
    // The surname comes from SurnameFromFullname, the same
    // derivation the document build uses, so the prefill and
    // the author agree. A new version prefills the name from
    // the loaded draft instead, never from the surname: the
    // loaded name is the one being versioned. On re-entry
    // after Back, the name refreshes only when the user has
    // not edited it: the field still holds the last computed
    // prefill. A manual edit is never clobbered. In new-version
    // mode the field is read-only and the name always
    // refreshes, since the user cannot have edited it.
    QString Prefill;
    if (NewVersion) {
        Prefill = Wizard->NewVersionBaseName();
    } else {
        QString Surname = DocDocument::SurnameFromFullname(
            wizard()->field("authorName").toString());
        if (!Surname.isEmpty()) {
            Prefill = QStringLiteral("draft-") + Surname.toLower();
        }
    }
    if (!Prefill.isEmpty()) {
        // In new-version mode the name always refreshes: the
        // field is read-only, so no manual edit can be lost,
        // and picking a different source file must show that
        // draft's name. Otherwise it refreshes only when the
        // field still holds the last prefill.
        if (NewVersion || _NameEdit->text() == _LastPrefill) {
            _NameEdit->setText(Prefill);
        }
        _LastPrefill = Prefill;
    }
    // The version prefills with 00, or the bumped version for a
    // new version. It sets when the field is empty or still
    // holds the last prefill, since it does not depend on the
    // author name, so a value the user entered is never
    // overwritten.
    QString VersionPrefill = NewVersion ? Wizard->NewVersionVersion()
                                        : QStringLiteral("00");
    if (_VersionEdit->text().isEmpty() ||
        _VersionEdit->text() == _LastVersionPrefill) {
        _VersionEdit->setText(VersionPrefill);
    }
    _LastVersionPrefill = VersionPrefill;
}

bool TitlePage::isComplete() const
{
    QString Title = _TitleEdit->text().trimmed();
    QString Name = _NameEdit->text().trimmed();
    QString Version = _VersionEdit->text().trimmed();
    return !Title.isEmpty() && !Name.isEmpty() &&
           !Name.contains(QLatin1Char(' ')) &&
           Version.size() == 2 && Version.at(0).isDigit() &&
           Version.at(1).isDigit();
}

/**
 * @brief Creates the author page: name, organization, and email.
 *
 * The author's own information is remembered between runs and
 * prefilled here (Bug 413); it is still asked, not assumed.
 * The wizard writes the entered values back when the draft is
 * created. Adding a co-author in the editor is a different
 * person and is not prefilled with this information.
 */
AuthorPage::AuthorPage(QWidget* Parent)
    : QWizardPage(Parent)
    , _NameEdit(nullptr)
    , _OrganizationEdit(nullptr)
    , _EmailEdit(nullptr)
    , _LastNamePrefill()
    , _LastOrganizationPrefill()
    , _LastEmailPrefill()
{
    setTitle(tr("First author"));
    setSubTitle(tr("The draft needs at least one author. More authors "
                   "can be added later, in the editor."));

    _NameEdit = new QLineEdit(this);
    _NameEdit->setPlaceholderText(tr("Doug Royer"));
    _NameEdit->setToolTip(tr("The author's full name."));
    _OrganizationEdit = new QLineEdit(this);
    _OrganizationEdit->setToolTip(tr("The author's organization, if any."));
    _EmailEdit = new QLineEdit(this);
    _EmailEdit->setToolTip(tr("The author's email address, if any."));

    // The stored author information prefills the fields, so a
    // returning user only checks them. On the first run there
    // is nothing stored and the fields start empty. The prefill
    // trackers start at these values, so a new version's
    // initializePage can tell an untouched field from an edit.
    QSettings Settings;
    _NameEdit->setText(
        Settings.value(QStringLiteral("author/fullname")).toString());
    _OrganizationEdit->setText(
        Settings.value(QStringLiteral("author/organization")).toString());
    _EmailEdit->setText(
        Settings.value(QStringLiteral("author/email")).toString());
    _LastNamePrefill = _NameEdit->text();
    _LastOrganizationPrefill = _OrganizationEdit->text();
    _LastEmailPrefill = _EmailEdit->text();

    // Only the name is required. Not a mandatory "*" field:
    // the field starts prefilled, and a mandatory field with a
    // prefilled value left Next disabled until the text changed
    // (Bug 304, from the CoOp editor). Completeness is the
    // custom isComplete() below.
    registerField("authorName", _NameEdit);
    registerField("authorOrganization", _OrganizationEdit);
    registerField("authorEmail", _EmailEdit);
    connect(_NameEdit, &QLineEdit::textChanged,
            this, &QWizardPage::completeChanged);

    QFormLayout* Layout = new QFormLayout(this);
    Layout->addRow(tr("Full name:"), _NameEdit);
    Layout->addRow(tr("Organization:"), _OrganizationEdit);
    Layout->addRow(tr("Email:"), _EmailEdit);
    setLayout(Layout);
}

void AuthorPage::initializePage()
{
    StartWizard* Wizard = qobject_cast<StartWizard*>(wizard());
    if (Wizard == nullptr || !Wizard->IsNewVersionMode()) {
        return;
    }
    // Prefill from the loaded draft's first author (Bug 435),
    // or the remembered author information when the draft has
    // no authors. A field refreshes only when it still holds
    // the last prefill: a manual edit is never clobbered, and
    // picking a different source file on the way back
    // refreshes the untouched fields.
    DocDocument Loaded = Wizard->NewVersionDocument();
    QString Fullname;
    QString Organization;
    QString Email;
    QVector<xmlNodePtr> Authors = Loaded.AuthorNodes();
    if (!Authors.isEmpty()) {
        xmlNodePtr First = Authors.first();
        Fullname = DocDocument::AuthorAttribute(First, "fullname");
        Organization = DocDocument::AuthorOrganization(First);
        Email = DocDocument::AuthorContactField(First, "email");
    } else {
        QSettings Settings;
        Fullname =
            Settings.value(QStringLiteral("author/fullname")).toString();
        Organization = Settings.value(
            QStringLiteral("author/organization")).toString();
        Email = Settings.value(QStringLiteral("author/email")).toString();
    }
    if (_NameEdit->text() == _LastNamePrefill) {
        _NameEdit->setText(Fullname);
    }
    _LastNamePrefill = Fullname;
    if (_OrganizationEdit->text() == _LastOrganizationPrefill) {
        _OrganizationEdit->setText(Organization);
    }
    _LastOrganizationPrefill = Organization;
    if (_EmailEdit->text() == _LastEmailPrefill) {
        _EmailEdit->setText(Email);
    }
    _LastEmailPrefill = Email;
}

bool AuthorPage::isComplete() const
{
    return !_NameEdit->text().trimmed().isEmpty();
}

/**
 * @brief Creates the folder page: the folder the XML file goes in.
 */
FolderPage::FolderPage(QWidget* Parent)
    : QWizardPage(Parent)
    , _FolderEdit(nullptr)
    , _BrowseButton(nullptr)
    , _LastPrefill()
{
    setTitle(tr("Save folder"));
    setSubTitle(tr("The folder the draft XML file is saved in. The "
                   "file name is the draft name plus \".xml\"."));

    _FolderEdit = new QLineEdit(this);
    // The folder prefills with the folder last used to create a
    // draft (Bug 457); with nothing stored yet, the home folder
    // stays the default. The prefill tracker starts at this
    // value, so a new version's initializePage can tell an
    // untouched field from an edit.
    QSettings Settings;
    QString LastFolder = Settings.value(
        QStringLiteral("wizard/lastCreateFolder")).toString().trimmed();
    _FolderEdit->setText(LastFolder.isEmpty() ? QDir::homePath() : LastFolder);
    _LastPrefill = _FolderEdit->text();
    _FolderEdit->setToolTip(tr("The folder the XML file is saved in."));
    _BrowseButton = new QPushButton(tr("Browse..."), this);
    _BrowseButton->setToolTip(tr("Pick the folder the XML file is saved in."));

    // Not a mandatory "*" field: the field starts prefilled, and a
    // mandatory field with a prefilled value left Next disabled
    // until the text changed (Bug 304, from the CoOp editor).
    // Completeness is the custom isComplete() below.
    registerField("folder", _FolderEdit);
    connect(_FolderEdit, &QLineEdit::textChanged,
            this, &QWizardPage::completeChanged);
    connect(_BrowseButton, &QPushButton::clicked,
            this, &FolderPage::OnBrowseFolder);

    QHBoxLayout* FolderRow = new QHBoxLayout();
    FolderRow->addWidget(_FolderEdit, 1);
    FolderRow->addWidget(_BrowseButton);

    QVBoxLayout* Layout = new QVBoxLayout(this);
    Layout->addWidget(new QLabel(tr("Folder:"), this));
    Layout->addLayout(FolderRow);
    setLayout(Layout);
}

void FolderPage::initializePage()
{
    StartWizard* Wizard = qobject_cast<StartWizard*>(wizard());
    if (Wizard == nullptr || !Wizard->IsNewVersionMode()) {
        return;
    }
    // Prefill with the source file's folder (Bug 435). The field
    // refreshes only when it still holds the last prefill (or is
    // empty): a manual edit or a Browse pick is never
    // clobbered, and picking a different source file on the way
    // back refreshes the untouched field.
    QString SourceFolder =
        QFileInfo(Wizard->NewVersionSourceFile()).absolutePath();
    if (_FolderEdit->text().isEmpty() ||
        _FolderEdit->text() == _LastPrefill) {
        _FolderEdit->setText(SourceFolder);
    }
    _LastPrefill = SourceFolder;
}

bool FolderPage::isComplete() const
{
    return !_FolderEdit->text().trimmed().isEmpty();
}

void FolderPage::OnBrowseFolder()
{
    QString Folder = QFileDialog::getExistingDirectory(
        this, tr("Save Folder"), _FolderEdit->text().trimmed());
    if (!Folder.isEmpty()) {
        _FolderEdit->setText(Folder);
    }
}

/**
 * @brief Creates the summary page shown just before "Create now".
 */
CreatePage::CreatePage(QWidget* Parent)
    : QWizardPage(Parent)
    , _SummaryLabel(nullptr)
{
    setTitle(tr("Create the draft"));
    setSubTitle(tr("Check the details below, then click \"Create now\" to "
                   "create the draft and open it in the editor."));

    _SummaryLabel = new QLabel(this);
    _SummaryLabel->setWordWrap(true);

    QVBoxLayout* Layout = new QVBoxLayout(this);
    Layout->addWidget(_SummaryLabel);
    setLayout(Layout);

    setFinalPage(true);
}

void CreatePage::initializePage()
{
    // The Finish button is shared with the first page, which may have
    // changed its text to "Open"; here it is "Create now" again.
    wizard()->setButtonText(QWizard::FinishButton, tr("Create now"));
    QString DraftName = wizard()->field("draftBaseName").toString().trimmed() +
        QStringLiteral("-") + wizard()->field("draftVersion").toString().trimmed();
    QString Title = wizard()->field("title").toString();
    QString AuthorName = wizard()->field("authorName").toString();
    QString Folder = wizard()->field("folder").toString();
    QString Summary = tr("Draft name: %1\n"
                         "Title: %2\n"
                         "First author: %3\n"
                         "File: %4")
        .arg(DraftName)
        .arg(Title)
        .arg(AuthorName)
        .arg(QDir(Folder).filePath(DraftName + QStringLiteral(".xml")));
    StartWizard* Wizard = qobject_cast<StartWizard*>(wizard());
    if (Wizard != nullptr && Wizard->IsNewVersionMode()) {
        Summary += QStringLiteral("\n\n") +
            tr("The new version copies the entire contents of the "
               "selected draft, with the version updated. The "
               "selected file itself is left untouched.");
    } else {
        Summary += QStringLiteral("\n\n") +
            tr("The outline will be prefilled with Introduction, "
               "Terminology, Implementation Status, Privacy "
               "Considerations, Security Considerations, IANA "
               "Considerations, and Acknowledgements, ready to write.");
    }
    _SummaryLabel->setText(Summary);
}

/**
 * @brief Creates the wizard with all of its pages in order.
 *
 * In create-only mode the wizard starts at the author page:
 * the open-or-create first page is never shown, and Back cannot
 * reach it (NoBackButtonOnStartPage hides Back on the start page).
 */
StartWizard::StartWizard(QWidget* Parent, bool CreateOnly)
    : QWizard(Parent)
    , _Document()
    , _DocumentFilePath()
    , _ChoicePage(nullptr)
    , _TitlePageId(-1)
    , _OpeningDraft(false)
    , _SelectedDraftFile()
    , _CreateOnly(CreateOnly)
    , _NewVersionDocument()
    , _NewVersionBaseName()
    , _NewVersionVersion()
    , _NewVersionSourceFile()
    , _HasNewVersionSource(false)
{
    setWindowTitle(tr("InternetDraftsEditor"));
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnStartPage, true);
    setButtonText(QWizard::FinishButton, tr("Create now"));
    button(QWizard::FinishButton)->setToolTip(tr("Create the draft and open it in the editor."));

    _ChoicePage = new ChooseDraftPage(this);
    addPage(_ChoicePage);
    int AuthorPageId = addPage(new AuthorPage(this));
    _ChoicePage->SetAuthorPageId(AuthorPageId);
    _TitlePageId = addPage(new TitlePage(this));
    addPage(new FolderPage(this));
    addPage(new CreatePage(this));
    if (_CreateOnly) {
        setStartId(AuthorPageId);
    }
}

DocDocument StartWizard::GetDocument() const
{
    return _Document;
}

QString StartWizard::GetDocumentFilePath() const
{
    return _DocumentFilePath;
}

bool StartWizard::IsOpeningDraft() const
{
    return _OpeningDraft;
}

QString StartWizard::GetSelectedDraftFile() const
{
    return _SelectedDraftFile;
}

bool StartWizard::IsNewVersionMode() const
{
    return !_CreateOnly && _ChoicePage != nullptr &&
           _ChoicePage->IsCreatingNewVersion();
}

DocDocument StartWizard::NewVersionDocument() const
{
    return _NewVersionDocument;
}

QString StartWizard::NewVersionBaseName() const
{
    return _NewVersionBaseName;
}

QString StartWizard::NewVersionVersion() const
{
    return _NewVersionVersion;
}

QString StartWizard::NewVersionSourceFile() const
{
    return _NewVersionSourceFile;
}

void StartWizard::SetNewVersionSource(const DocDocument& Document,
                                      const QString& BaseName,
                                      const QString& Version,
                                      const QString& SourceFile)
{
    _NewVersionDocument = Document;
    _NewVersionBaseName = BaseName;
    _NewVersionVersion = Version;
    _NewVersionSourceFile = SourceFile;
    _HasNewVersionSource = true;
}

/**
 * @brief Returns a document's <abstract> element, creating it when missing.
 *
 * A created abstract goes before the front's first <note> child,
 * so it keeps its place ahead of the notes.
 * @param Document The document holding the abstract.
 * @return The abstract node (borrowed), or nullptr when the
 *         document has no front to hold it.
 */
static xmlNodePtr AbstractNodeOrCreate(DocDocument& Document)
{
    xmlNodePtr Front = Document.FrontNode();
    if (Front == nullptr) {
        return nullptr;
    }
    xmlNodePtr Abstract = DocDocument::ChildElement(Front, "abstract");
    if (Abstract != nullptr) {
        return Abstract;
    }
    Abstract = xmlNewNode(nullptr, XmlChars("abstract"));
    xmlNodePtr Note = DocDocument::ChildElement(Front, "note");
    if (Note != nullptr) {
        xmlAddPrevSibling(Note, Abstract);
    } else {
        xmlAddChild(Front, Abstract);
    }
    return Abstract;
}

void StartWizard::accept()
{
    // In create-only mode the first page is never shown, but its
    // default choice may still read as "open a draft", so the
    // opening branch is taken only outside create-only mode.
    if (!_CreateOnly && _ChoicePage != nullptr && _ChoicePage->IsOpeningDraft()) {
        _OpeningDraft = true;
        _SelectedDraftFile = _ChoicePage->GetSelectedFile();
    } else {
        // Bug 304 (from the CoOp editor): the title page is
        // mandatory. If the draft name is empty or the version
        // is not two digits, the wizard accepted before the
        // details were entered; go to the title page instead of
        // creating a draft with a bad name or version. A
        // finished create flow always has both, so this only
        // fires when the setup pages were skipped (Bug 414).
        QString Version = field("draftVersion").toString().trimmed();
        bool VersionValid = Version.size() == 2 &&
                            Version.at(0).isDigit() &&
                            Version.at(1).isDigit();
        if (field("draftBaseName").toString().trimmed().isEmpty() ||
            !VersionValid) {
            if (_TitlePageId >= 0) {
                setCurrentId(_TitlePageId);
            }
            return;
        }
        _OpeningDraft = false;
        _SelectedDraftFile.clear();
        // A new version keeps the original draft's name (Bug
        // 435, v2): the name comes from the stored source base,
        // never from the draft-name field, so the created draft
        // cannot end up with a different name. The version
        // still comes from the field, which stays editable.
        QString DocName;
        if (IsNewVersionMode() && _HasNewVersionSource) {
            DocName = _NewVersionBaseName + QStringLiteral("-") +
                      field("draftVersion").toString().trimmed();
        } else {
            DocName = field("draftBaseName").toString().trimmed() +
                QStringLiteral("-") +
                field("draftVersion").toString().trimmed();
        }
        QString TemplateResource;
        if (_ChoicePage != nullptr) {
            TemplateResource = _ChoicePage->GetTemplateResource();
        }
        DocDocument Loaded;
        QString LoadError;
        if (IsNewVersionMode() && _HasNewVersionSource) {
            // Creating a new version (Bug 435): the loaded draft
            // IS the new draft, with the version updated. Its
            // entire contents carry over, including the section
            // anchors, which must not change once published. No
            // NewWithDefaults runs here, so no starting outline
            // and no "Created With" note are added; the date
            // takes the current date on save, as the writer
            // already does. The old file is never written: the
            // file path below is built from the new draft name.
            _Document = _NewVersionDocument;
            _Document.SetDocName(DocName);
            // The title and first author from the wizard pages
            // apply only when changed, so loaded detail is never
            // destroyed: an unchanged title keeps its line
            // breaks, and an unchanged first author keeps its
            // full record. Authors beyond the first are never
            // touched.
            QString NewTitle = field("title").toString().trimmed();
            if (NewTitle != _Document.Title().trimmed()) {
                _Document.SetTitle(NewTitle, QVector<int>());
            }
            QString NewName = field("authorName").toString().trimmed();
            QString NewOrganization =
                field("authorOrganization").toString().trimmed();
            QString NewEmail = field("authorEmail").toString().trimmed();
            QVector<xmlNodePtr> Authors = _Document.AuthorNodes();
            if (!Authors.isEmpty()) {
                xmlNodePtr First = Authors.first();
                if (NewName != DocDocument::AuthorAttribute(
                                    First, "fullname").trimmed() ||
                    NewOrganization != DocDocument::AuthorOrganization(
                                           First).trimmed() ||
                    NewEmail != DocDocument::AuthorContactField(
                                    First, "email").trimmed()) {
                    // Insert the replacement ahead of the old
                    // first author, then remove the old one, so
                    // the new author keeps the first position.
                    xmlNodePtr Replacement =
                        DocDocument::AuthorNodeFromFullname(
                            NewName, NewOrganization, NewEmail);
                    xmlAddPrevSibling(First, Replacement);
                    xmlUnlinkNode(First);
                    xmlFreeNode(First);
                }
            } else if (!NewName.isEmpty()) {
                _Document.AddAuthorNode(
                    DocDocument::AuthorNodeFromFullname(
                        NewName, NewOrganization, NewEmail));
            }
        } else if (!TemplateResource.isEmpty() &&
                   Loaded.ReadXml(TemplateResource, LoadError)) {
            // Creating from a template: the entered name, title,
            // and author replace the template's placeholders, and
            // the rest of the template loads as the starting
            // draft (Bug 431).
            Loaded.SetDocName(DocName);
            // The template's seriesInfo needs no rewrite: a row
            // naming Internet-Draft takes its value from the
            // draft name when written (Bugs 353, 439).
            Loaded.SetTitle(field("title").toString().trimmed(),
                            Loaded.TitleBreaks());
            while (!Loaded.AuthorNodes().isEmpty()) {
                Loaded.RemoveAuthorNode(0);
            }
            Loaded.AddAuthorNode(DocDocument::AuthorNodeFromFullname(
                field("authorName").toString(),
                field("authorOrganization").toString(),
                field("authorEmail").toString()));
            // The template's scaffolding (its DOCTYPE and the
            // comments and processing instructions before the
            // root element) cannot be kept; that drop is
            // expected for a template start, so it must not warn
            // on the first save.
            Loaded.HadSkippedContent = false;
            // A template with no abstract (Bare) gets the
            // example-text prefill, like a draft from scratch.
            if (Loaded.AbstractBodyNodes().isEmpty()) {
                xmlNodePtr Abstract = AbstractNodeOrCreate(Loaded);
                if (Abstract != nullptr) {
                    xmlAddChild(Abstract, DocDocument::ExampleAbstractNode());
                }
            }
            _Document = Loaded;
        } else {
            // From scratch, the template could not be read, or a
            // new version reached here with no stored source
            // document: the draft still creates with the
            // starting outline, rather than nothing at all.
            _Document = DocDocument::NewWithDefaults(
                DocName,
                field("title").toString(),
                field("authorName").toString(),
                field("authorOrganization").toString(),
                field("authorEmail").toString());
        }
        // Bug 413: remember the author's own information, so
        // the next new draft prefills it. Both creation paths
        // reach here. An empty name stores nothing, so a
        // skipped author page cannot wipe the stored values.
        QString AuthorName = field("authorName").toString().trimmed();
        if (!AuthorName.isEmpty()) {
            QSettings Settings;
            Settings.setValue(QStringLiteral("author/fullname"),
                              AuthorName);
            Settings.setValue(
                QStringLiteral("author/organization"),
                field("authorOrganization").toString().trimmed());
            Settings.setValue(QStringLiteral("author/email"),
                              field("authorEmail").toString().trimmed());
        }
        QString Folder = field("folder").toString().trimmed();
        // Bug 457: remember the folder the draft was created in,
        // so the next new draft prefills it. Both creation paths
        // reach here; an empty folder stores nothing.
        if (!Folder.isEmpty()) {
            QSettings FolderSettings;
            FolderSettings.setValue(
                QStringLiteral("wizard/lastCreateFolder"), Folder);
        }
        _DocumentFilePath = QDir(Folder).filePath(
            _Document.DocName() + QStringLiteral(".xml"));
    }
    QWizard::accept();
}
