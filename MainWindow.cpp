/**
 * @file MainWindow.cpp
 * @brief Implementation of the main window.
 */

#include "MainWindow.h"

#include "LegendDialog.h"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDate>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextCursor>
#include <QTextStream>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <libxml/tree.h>

#include <utility>

#include "StartWizard.h"
#include "TextEditor.h"

/**
 * @brief How many recently opened files the File menu remembers.
 */
const int MaxRecentFiles = 4;

/**
 * @brief The details pages, in the order they are added to the stack.
 */
enum DetailPage {
    DetailNone,
    DetailGroup,
    DetailAuthors,
    DetailAuthor,
    DetailAbstract,
    DetailDate,
    DetailSection,
    DetailReferences,
    DetailReferenceGroup,
    DetailReferenceEntry,
    DetailDocumentSettings
};

// The schema install helpers are defined with the validation code
// below; the constructor calls one of them, so they are declared
// here first.
static QString SchemasDir();
static void EnsureSchemasInstalled();

// The dictionary install helpers are defined with the validation
// code below; the constructor calls them, so they are declared
// here first.
static QString DictionariesDir();
static void EnsureDictionariesInstalled();

// The section-number helper is defined with the outline code
// below; the title-edit handler calls it, so it is declared
// here first.
static QString SectionNumberFromPath(const QString& Path);

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
 * @brief Builds free-form postal lines from the structured parts.
 *
 * The non-empty parts join in writer order: post office box,
 * street, extended address, city area, city, region, code,
 * country, sorting code. The lines carry no attributes: the
 * parts keep theirs, so switching back restores them.
 * @param Author The author node (borrowed).
 * @return The lines.
 */
static QVector<QString> PostalLinesFromParts(xmlNodePtr Author)
{
    QStringList Parts;
    Parts.append(DocDocument::AuthorPostalPart(Author, "pobox"));
    Parts.append(DocDocument::AuthorPostalPart(Author, "street"));
    Parts.append(DocDocument::AuthorPostalPart(Author, "extaddr"));
    Parts.append(DocDocument::AuthorPostalPart(Author, "cityarea"));
    Parts.append(DocDocument::AuthorPostalPart(Author, "city"));
    Parts.append(DocDocument::AuthorPostalPart(Author, "region"));
    Parts.append(DocDocument::AuthorPostalPart(Author, "code"));
    Parts.append(DocDocument::AuthorPostalPart(Author, "country"));
    Parts.append(DocDocument::AuthorPostalPart(Author, "sortingcode"));
    QVector<QString> Lines;
    for (const QString& Part : Parts) {
        if (!Part.trimmed().isEmpty()) {
            Lines.append(Part.trimmed());
        }
    }
    return Lines;
}

/**
 * @brief True when a node carries an attribute outside the kept set.
 * @param Node The node to check (borrowed).
 * @param Kept The attribute names that are modeled, not preserved.
 * @return True when an attribute outside the set exists.
 */
static bool NodeHasAttributesOtherThan(xmlNodePtr Node, const QStringList& Kept)
{
    if (Node == nullptr) {
        return false;
    }
    for (xmlAttrPtr Attr = Node->properties; Attr != nullptr; Attr = Attr->next) {
        if (Attr->name == nullptr) {
            continue;
        }
        QString Name = QString::fromUtf8(
            reinterpret_cast<const char*>(Attr->name));
        if (!Kept.contains(Name)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief True when a node holds a child outside the modeled set.
 *
 * Element children outside the set count, as do non-whitespace
 * text children and comment children: the reader kept all of
 * those as preserved content.
 * @param Node The node to check (borrowed).
 * @param Kept The child element names that are modeled.
 * @return True when a child outside the set exists.
 */
static bool NodeHasChildrenOtherThan(xmlNodePtr Node, const QStringList& Kept)
{
    if (Node == nullptr) {
        return false;
    }
    for (xmlNodePtr Child = Node->children; Child != nullptr; Child = Child->next) {
        if (Child->type == XML_ELEMENT_NODE) {
            if (Child->name == nullptr) {
                return true;
            }
            QString Name = QString::fromUtf8(
                reinterpret_cast<const char*>(Child->name));
            if (!Kept.contains(Name)) {
                return true;
            }
        } else if (Child->type == XML_TEXT_NODE) {
            if (!DocDocument::IsWhitespaceText(Child)) {
                return true;
            }
        } else if (Child->type == XML_COMMENT_NODE) {
            return true;
        }
    }
    return false;
}

/**
 * @brief True when a section node carries attributes the editor
 *        does not model.
 *
 * The title attribute is modeled (it is the title when no
 * <name> child exists); every other attribute is preserved.
 * @param Section The section node (borrowed).
 * @return True when a preserved attribute exists.
 */
static bool SectionHasPreservedAttributes(xmlNodePtr Section)
{
    return NodeHasAttributesOtherThan(Section, QStringList{"title"});
}

/**
 * @brief True when a note node holds content the editor does not
 *        model: preserved body nodes, or attributes other than
 *        removeInRFC.
 * @param Note The note node (borrowed).
 * @return True when preserved content exists.
 */
static bool NoteHasPreservedContent(xmlNodePtr Note)
{
    if (!DocDocument::PreservedBodyNodes(Note).isEmpty()) {
        return true;
    }
    return NodeHasAttributesOtherThan(Note, QStringList{"removeInRFC"});
}

/**
 * @brief True when the front matter holds content the editor does
 *        not model: a child outside the modeled set (title,
 *        seriesInfo, author, date, area, workgroup, keyword,
 *        abstract, note).
 * @param Front The front node (borrowed).
 * @return True when preserved content exists.
 */
static bool FrontHasPreservedContent(xmlNodePtr Front)
{
    return NodeHasChildrenOtherThan(Front, QStringList{
        "title", "seriesInfo", "author", "date", "area",
        "workgroup", "keyword", "abstract", "note"});
}

/**
 * @brief True when a reference entry holds preserved content.
 *
 * Attributes other than anchor and target, children other than
 * stream and front, front children outside the modeled set
 * (title, seriesInfo, author, date), date attributes outside
 * day, month, and year, series info attributes outside name
 * and value, and authors holding preserved content all count.
 * @param Entry The reference entry node (borrowed).
 * @return True when preserved content exists.
 */
static bool ReferenceEntryHasPreservedContent(xmlNodePtr Entry)
{
    if (NodeHasAttributesOtherThan(Entry, QStringList{"anchor", "target"})) {
        return true;
    }
    if (NodeHasChildrenOtherThan(Entry, QStringList{"stream", "front"})) {
        return true;
    }
    xmlNodePtr Front = DocDocument::ChildElement(Entry, "front");
    if (Front == nullptr) {
        return false;
    }
    if (NodeHasChildrenOtherThan(Front, QStringList{
            "title", "seriesInfo", "author", "date"})) {
        return true;
    }
    xmlNodePtr Date = DocDocument::ChildElement(Front, "date");
    if (NodeHasAttributesOtherThan(Date, QStringList{"day", "month", "year"})) {
        return true;
    }
    for (xmlNodePtr Series : DocDocument::ChildElements(Front, "seriesInfo")) {
        if (NodeHasAttributesOtherThan(Series, QStringList{"name", "value"})) {
            return true;
        }
    }
    for (xmlNodePtr Author : DocDocument::ReferenceAuthors(Entry)) {
        if (DocDocument::AuthorHasPreservedContent(Author)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief True when a reference list holds preserved content.
 *
 * Attributes other than anchor, children other than the name
 * and the entries, and any entry holding preserved content
 * all count.
 * @param Group The reference list node (borrowed).
 * @return True when preserved content exists.
 */
static bool ReferenceGroupHasPreservedContent(xmlNodePtr Group)
{
    if (NodeHasAttributesOtherThan(Group, QStringList{"anchor"})) {
        return true;
    }
    if (NodeHasChildrenOtherThan(Group, QStringList{"name", "reference",
                                                    "references"})) {
        return true;
    }
    for (xmlNodePtr Entry : DocDocument::ReferenceEntries(Group)) {
        if (ReferenceEntryHasPreservedContent(Entry)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Splits plain text into one trimmed value per line.
 *
 * Blank lines are skipped: an area, workgroup, or keyword list
 * holds one value per line, with no empty values.
 * @param Text The text to split.
 * @return The values.
 */
static QStringList LinesFromPlainText(const QString& Text)
{
    QStringList Lines;
    for (const QString& Line : Text.split(QLatin1Char('\n'))) {
        QString Trimmed = Line.trimmed();
        if (!Trimmed.isEmpty()) {
            Lines.append(Trimmed);
        }
    }
    return Lines;
}

/**
 * @brief Finds the sibling sections of the section at a path.
 *
 * The siblings are the parent's child sections, or the
 * top-level sections for a top-level path, from
 * DocDocument::ChildSectionNodes plus index arithmetic.
 * @param Document The document to search.
 * @param Path The dot-separated path of the section.
 * @param Siblings Set to the sibling section nodes (borrowed).
 * @param Index Set to the section's index among its siblings.
 * @return True when the path resolves to a section.
 */
static bool SectionSiblings(const DocDocument& Document, const QString& Path,
                            QVector<xmlNodePtr>& Siblings, int& Index)
{
    Index = -1;
    if (Path.isEmpty()) {
        return false;
    }
    QStringList Parts = Path.split(QLatin1Char('.'));
    bool Ok = false;
    Index = Parts.last().toInt(&Ok);
    if (!Ok) {
        Index = -1;
        return false;
    }
    if (Parts.size() == 1) {
        Siblings = Document.TopSectionNodes();
    } else {
        QString ParentPath = Parts.mid(0, Parts.size() - 1)
                                 .join(QLatin1Char('.'));
        xmlNodePtr Parent = Document.FindSectionNode(ParentPath);
        if (Parent == nullptr) {
            Index = -1;
            return false;
        }
        Siblings = DocDocument::ChildSectionNodes(Parent);
    }
    if (Index < 0 || Index >= Siblings.size()) {
        Index = -1;
        return false;
    }
    return true;
}

/**
 * @brief Sets the text of a named child element, creating it
 *        first when missing.
 *
 * A created child goes before the parent's first child element,
 * so a leading child (a <name>, or a reference front's
 * <title>) stays leading. An empty text removes the child
 * when it exists.
 * @param Parent The parent element.
 * @param Name The child element name.
 * @param Text The text to set.
 */
static void SetLeadingChildText(xmlNodePtr Parent, const char* Name,
                                const QString& Text)
{
    if (Parent == nullptr) {
        return;
    }
    xmlNodePtr Child = DocDocument::ChildElement(Parent, Name);
    if (Child != nullptr) {
        DocDocument::SetChildText(Parent, Name, Text);
        return;
    }
    if (Text.trimmed().isEmpty()) {
        return;
    }
    xmlNodePtr Created = xmlNewNode(nullptr, XmlChars(Name));
    xmlNodePtr Before = nullptr;
    for (xmlNodePtr Sibling = Parent->children; Sibling != nullptr;
         Sibling = Sibling->next) {
        if (Sibling->type == XML_ELEMENT_NODE) {
            Before = Sibling;
            break;
        }
    }
    if (Before != nullptr) {
        xmlAddPrevSibling(Before, Created);
    } else {
        xmlAddChild(Parent, Created);
    }
    DocDocument::SetNodeText(Created, Text);
}

/**
 * @brief Returns the <front> child of a reference entry, creating
 *        it when asked.
 *
 * A created front goes before the entry's first child that is
 * not a <stream>, so it keeps its place ahead of any tail
 * content the file holds.
 * @param Entry The reference entry node (borrowed).
 * @param Create True to create the front when it is missing.
 * @return The front node (borrowed), or nullptr when missing
 *         and not created.
 */
static xmlNodePtr ReferenceFrontNode(xmlNodePtr Entry, bool Create)
{
    if (Entry == nullptr) {
        return nullptr;
    }
    xmlNodePtr Front = DocDocument::ChildElement(Entry, "front");
    if (Front != nullptr || !Create) {
        return Front;
    }
    Front = xmlNewNode(nullptr, XmlChars("front"));
    xmlNodePtr Before = nullptr;
    for (xmlNodePtr Child = Entry->children; Child != nullptr;
         Child = Child->next) {
        if (Child->type == XML_ELEMENT_NODE &&
            !DocDocument::NodeNameEquals(Child, "stream")) {
            Before = Child;
            break;
        }
    }
    if (Before != nullptr) {
        xmlAddPrevSibling(Before, Front);
    } else {
        xmlAddChild(Entry, Front);
    }
    return Front;
}

/**
 * @brief Writes a reference entry's stream choice.
 *
 * The <stream> child sits before the <front>; an empty stream
 * removes the element.
 * @param Entry The reference entry node.
 * @param Stream The stream value, or empty to remove it.
 */
static void SetReferenceStream(xmlNodePtr Entry, const QString& Stream)
{
    if (Entry == nullptr) {
        return;
    }
    xmlNodePtr StreamNode = DocDocument::ChildElement(Entry, "stream");
    if (StreamNode != nullptr) {
        DocDocument::SetChildText(Entry, "stream", Stream);
        return;
    }
    if (Stream.trimmed().isEmpty()) {
        return;
    }
    xmlNodePtr Created = xmlNewNode(nullptr, XmlChars("stream"));
    xmlNodePtr Front = DocDocument::ChildElement(Entry, "front");
    if (Front != nullptr) {
        xmlAddPrevSibling(Front, Created);
    } else {
        xmlAddChild(Entry, Created);
    }
    DocDocument::SetNodeText(Created, Stream);
}

/**
 * @brief Writes a reference entry's title into its front.
 *
 * The title is the front's leading child; an empty title
 * removes the element.
 * @param Entry The reference entry node.
 * @param Title The title text.
 */
static void SetReferenceTitle(xmlNodePtr Entry, const QString& Title)
{
    xmlNodePtr Front = ReferenceFrontNode(Entry, !Title.trimmed().isEmpty());
    if (Front == nullptr) {
        return;
    }
    SetLeadingChildText(Front, "title", Title);
}

/**
 * @brief Writes one part of a reference entry's date.
 *
 * The <date> child is created in the front when a value is
 * set and it is missing, ahead of any area, workgroup,
 * keyword, abstract, or note child. A date left with no
 * day, month, or year and no other attributes is removed.
 * @param Entry The reference entry node.
 * @param Part The date part: "day", "month", or "year".
 * @param Value The value, or empty to clear that part.
 */
static void SetReferenceDatePart(xmlNodePtr Entry, const char* Part,
                                 const QString& Value)
{
    xmlNodePtr Front = ReferenceFrontNode(Entry, !Value.trimmed().isEmpty());
    if (Front == nullptr) {
        return;
    }
    xmlNodePtr Date = DocDocument::ChildElement(Front, "date");
    if (Date == nullptr) {
        if (Value.trimmed().isEmpty()) {
            return;
        }
        Date = xmlNewNode(nullptr, XmlChars("date"));
        xmlNodePtr Before = nullptr;
        for (xmlNodePtr Child = Front->children; Child != nullptr;
             Child = Child->next) {
            if (Child->type == XML_ELEMENT_NODE &&
                (DocDocument::NodeNameEquals(Child, "area") ||
                 DocDocument::NodeNameEquals(Child, "workgroup") ||
                 DocDocument::NodeNameEquals(Child, "keyword") ||
                 DocDocument::NodeNameEquals(Child, "abstract") ||
                 DocDocument::NodeNameEquals(Child, "note"))) {
                Before = Child;
                break;
            }
        }
        if (Before != nullptr) {
            xmlAddPrevSibling(Before, Date);
        } else {
            xmlAddChild(Front, Date);
        }
    }
    DocDocument::SetNodeAttribute(Date, Part, Value);
    if (DocDocument::NodeAttribute(Date, "day").isEmpty() &&
        DocDocument::NodeAttribute(Date, "month").isEmpty() &&
        DocDocument::NodeAttribute(Date, "year").isEmpty() &&
        !NodeHasAttributesOtherThan(Date,
                                    QStringList{"day", "month", "year"})) {
        xmlUnlinkNode(Date);
        xmlFreeNode(Date);
    }
}

/**
 * @brief Returns a reference entry's series info rows, in order.
 * @param Entry The reference entry node (borrowed).
 * @return The <seriesInfo> nodes (borrowed), empty when the
 *         entry has no front.
 */
static QVector<xmlNodePtr> ReferenceSeriesNodes(xmlNodePtr Entry)
{
    xmlNodePtr Front = DocDocument::ChildElement(Entry, "front");
    if (Front == nullptr) {
        return QVector<xmlNodePtr>();
    }
    return DocDocument::ChildElements(Front, "seriesInfo");
}

/**
 * @brief Adds an empty series info row to a reference entry.
 *
 * The row goes before the front's first author or date child,
 * so it keeps its place after the title.
 * @param Entry The reference entry node.
 * @return The new row node (borrowed), or nullptr when the
 *         entry has no front to hold it.
 */
static xmlNodePtr AddReferenceSeriesNode(xmlNodePtr Entry)
{
    xmlNodePtr Front = ReferenceFrontNode(Entry, true);
    if (Front == nullptr) {
        return nullptr;
    }
    xmlNodePtr Series = xmlNewNode(nullptr, XmlChars("seriesInfo"));
    xmlNodePtr Before = nullptr;
    for (xmlNodePtr Child = Front->children; Child != nullptr;
         Child = Child->next) {
        if (Child->type == XML_ELEMENT_NODE &&
            (DocDocument::NodeNameEquals(Child, "author") ||
             DocDocument::NodeNameEquals(Child, "date"))) {
            Before = Child;
            break;
        }
    }
    if (Before != nullptr) {
        xmlAddPrevSibling(Before, Series);
    } else {
        xmlAddChild(Front, Series);
    }
    return Series;
}

/**
 * @brief Removes one series info row from a reference entry.
 * @param Entry The reference entry node.
 * @param Index The row index.
 */
static void RemoveReferenceSeriesNode(xmlNodePtr Entry, int Index)
{
    QVector<xmlNodePtr> Series = ReferenceSeriesNodes(Entry);
    if (Index < 0 || Index >= Series.size()) {
        return;
    }
    xmlUnlinkNode(Series[Index]);
    xmlFreeNode(Series[Index]);
}

/**
 * @brief Inserts a reference entry node into a list, after its
 *        last entry.
 *
 * The node lands after the list's last <reference> child, or
 * before the list's first preserved child when it holds no
 * entries, so a moved or added entry never lands after
 * preserved content the file anchors after the entries.
 * @param Group The reference list node.
 * @param Entry The entry node to insert (adopted).
 */
static void AppendReferenceEntry(xmlNodePtr Group, xmlNodePtr Entry)
{
    if (Group == nullptr || Entry == nullptr) {
        return;
    }
    xmlNodePtr LastReference = nullptr;
    xmlNodePtr FirstOther = nullptr;
    for (xmlNodePtr Child = Group->children; Child != nullptr;
         Child = Child->next) {
        if (Child->type != XML_ELEMENT_NODE) {
            continue;
        }
        if (DocDocument::NodeNameEquals(Child, "reference")) {
            LastReference = Child;
        } else if (FirstOther == nullptr &&
                   !DocDocument::NodeNameEquals(Child, "name")) {
            FirstOther = Child;
        }
    }
    if (LastReference != nullptr) {
        xmlAddNextSibling(LastReference, Entry);
    } else if (FirstOther != nullptr) {
        xmlAddPrevSibling(FirstOther, Entry);
    } else {
        xmlAddChild(Group, Entry);
    }
}

/**
 * @brief Creates the main window: menus, outline, details, and buttons.
 */
MainWindow::MainWindow(QWidget* Parent)
    : QMainWindow(Parent)
    , _Document()
    , _BibCache(this)
    , _FilePath()
    , _Modified(false)
    , _SelectedKey()
    , _AuthorIndex(-1)
    , _AuthorGroupPath()
    , _AuthorEntry(-1)
    , _ReferenceGroupPath()
    , _ReferenceEntryIndex(-1)
    , _SectionPath()
    , _RunFailed(false)
    , _FileMenu(nullptr)
    , _SaveAction(nullptr)
    , _RecentActions()
    , _Outline(nullptr)
    , _Details(nullptr)
    , _AddSectionButton(nullptr)
    , _InsertSectionButton(nullptr)
    , _RemoveSectionButton(nullptr)
    , _MoveUpButton(nullptr)
    , _MoveDownButton(nullptr)
    , _GroupLabel(nullptr)
    , _AbstractEdit(nullptr)
    , _AbstractPreservedLabel(nullptr)
    , _DateLabel(nullptr)
    , _ReferencesLabel(nullptr)
    , _AuthorFullnameEdit(nullptr)
    , _AuthorInitialsEdit(nullptr)
    , _AuthorSurnameEdit(nullptr)
    , _AuthorOrganizationEdit(nullptr)
    , _AuthorEmailEdit(nullptr)
    , _AuthorPhoneEdit(nullptr)
    , _AuthorUriEdit(nullptr)
    , _AuthorPostalStreetEdit(nullptr)
    , _AuthorPostalCityareaEdit(nullptr)
    , _AuthorPostalExtaddrEdit(nullptr)
    , _AuthorPostalPoboxEdit(nullptr)
    , _AuthorPostalCityEdit(nullptr)
    , _AuthorPostalRegionEdit(nullptr)
    , _AuthorPostalCodeEdit(nullptr)
    , _AuthorPostalCountryEdit(nullptr)
    , _AuthorPostalSortingcodeEdit(nullptr)
    , _AuthorPostalModeCombo(nullptr)
    , _AuthorPostalStack(nullptr)
    , _AuthorPostalLinesEdit(nullptr)
    , _AuthorRadio(nullptr)
    , _EditorRadio(nullptr)
    , _AuthorPreservedLabel(nullptr)
    , _SectionTitleEdit(nullptr)
    , _SectionBodyEdit(nullptr)
    , _SectionPreservedLabel(nullptr)
    , _SectionRequiredLabel(nullptr)
    , _ReferenceGroupNameEdit(nullptr)
    , _AddReferenceEntryButton(nullptr)
    , _AddNestedListButton(nullptr)
    , _ReferenceGroupPreservedLabel(nullptr)
    , _ReferenceAnchorEdit(nullptr)
    , _ReferenceTargetEdit(nullptr)
    , _ReferenceTitleEdit(nullptr)
    , _ReferenceDateDayEdit(nullptr)
    , _ReferenceDateMonthEdit(nullptr)
    , _ReferenceDateYearEdit(nullptr)
    , _ReferenceStreamCombo(nullptr)
    , _ReferenceSeriesTable(nullptr)
    , _ReferenceMoveToCombo(nullptr)
    , _ReferenceEntryPreservedLabel(nullptr)
    , _SettingsTitleEdit(nullptr)
    , _SettingsTitleAbbrevEdit(nullptr)
    , _SettingsTitleAsciiEdit(nullptr)
    , _SettingsTitleBreaksLabel(nullptr)
    , _SettingsSeriesTable(nullptr)
    , _SettingsSeriesNoteLabel(nullptr)
    , _SettingsAreasEdit(nullptr)
    , _SettingsWorkgroupsEdit(nullptr)
    , _SettingsKeywordsEdit(nullptr)
    , _SettingsNoteGroup(nullptr)
    , _SettingsNoteNameEdit(nullptr)
    , _SettingsNoteRemoveCheck(nullptr)
    , _SettingsNoteEdit(nullptr)
    , _SettingsAddNoteButton(nullptr)
    , _SettingsNoteExtraLabel(nullptr)
    , _SettingsNotePreservedLabel(nullptr)
    , _SettingsPreservedLabel(nullptr)
    , _TextFormatCheck(nullptr)
    , _PdfFormatCheck(nullptr)
    , _HtmlFormatCheck(nullptr)
    , _TextViewButton(nullptr)
    , _PdfViewButton(nullptr)
    , _HtmlViewButton(nullptr)
    , _RunProcess(nullptr)
    , _RunDialog(nullptr)
    , _RunLog(nullptr)
    , _RunCloseButton(nullptr)
    , _ValidateDialog(nullptr)
    , _ValidateLog(nullptr)
    , _ValidateCloseButton(nullptr)
{
    EnsureSchemasInstalled();
    EnsureDictionariesInstalled();
    // The spell checker loads the installed en_US dictionary,
    // falling back to a dictionaries folder beside the program.
    // With neither, it stays off and no words are underlined.
    QString AffPath = QDir(DictionariesDir()).filePath(
        QStringLiteral("en_US.aff"));
    QString DicPath = QDir(DictionariesDir()).filePath(
        QStringLiteral("en_US.dic"));
    if (!QFile::exists(AffPath) || !QFile::exists(DicPath)) {
        QDir AppDir(QCoreApplication::applicationDirPath());
        QString BesideAff = AppDir.filePath(
            QStringLiteral("dictionaries/en_US.aff"));
        QString BesideDic = AppDir.filePath(
            QStringLiteral("dictionaries/en_US.dic"));
        if (QFile::exists(BesideAff) && QFile::exists(BesideDic)) {
            AffPath = BesideAff;
            DicPath = BesideDic;
        }
    }
    QString PersonalPath = QDir(DictionariesDir()).filePath(
        QStringLiteral("user.dic"));
    _SpellChecker = std::make_unique<SpellChecker>(
        AffPath, DicPath, PersonalPath);

    _FileMenu = menuBar()->addMenu(tr("&File"));

    QAction* NewAction = _FileMenu->addAction(tr("New Draft..."));
    NewAction->setShortcut(QKeySequence::New);
    NewAction->setStatusTip(tr("Create a new draft."));
    connect(NewAction, &QAction::triggered, this, &MainWindow::OnNewDraft);

    QAction* OpenAction = _FileMenu->addAction(tr("Open..."));
    OpenAction->setShortcut(QKeySequence::Open);
    OpenAction->setStatusTip(tr("Open a draft XML file."));
    connect(OpenAction, &QAction::triggered, this, &MainWindow::OnOpenDraft);

    _SaveAction = _FileMenu->addAction(tr("Save"));
    _SaveAction->setShortcut(QKeySequence::Save);
    _SaveAction->setStatusTip(tr("Save the draft as XML."));
    connect(_SaveAction, &QAction::triggered, this, &MainWindow::OnSave);

    QAction* SaveAsAction = _FileMenu->addAction(tr("Save As..."));
    SaveAsAction->setShortcut(QKeySequence::SaveAs);
    SaveAsAction->setStatusTip(tr("Save the draft under a new file name."));
    connect(SaveAsAction, &QAction::triggered, this, &MainWindow::OnSaveAs);

    _FileMenu->addSeparator();

    QAction* RunAction = _FileMenu->addAction(tr("Run xml2rfc"));
    RunAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    RunAction->setStatusTip(tr("Run xml2rfc on the saved file."));
    connect(RunAction, &QAction::triggered, this, &MainWindow::OnRunXml2rfc);

    QAction* ValidateAction = _FileMenu->addAction(tr("Validate"));
    ValidateAction->setStatusTip(tr("Validate the saved file against the v3 schema."));
    connect(ValidateAction, &QAction::triggered, this, &MainWindow::OnValidate);

    _FileMenu->addSeparator();

    QAction* ExitAction = _FileMenu->addAction(tr("Exit"));
    ExitAction->setStatusTip(tr("Close the editor."));
    connect(ExitAction, &QAction::triggered, this, &QWidget::close);

    QMenu* SettingsMenu = menuBar()->addMenu(tr("&Settings"));

    QAction* PathAction = SettingsMenu->addAction(tr("Set xml2rfc Path..."));
    PathAction->setStatusTip(tr("Set the xml2rfc program path."));
    connect(PathAction, &QAction::triggered, this, &MainWindow::OnSetXml2rfcPath);

    QAction* BibliographyAction = SettingsMenu->addAction(tr("Bibliography..."));
    BibliographyAction->setStatusTip(tr("Manage the cached bibliography entries."));
    connect(BibliographyAction, &QAction::triggered, this, &MainWindow::OnBibliography);

    QMenu* HelpMenu = menuBar()->addMenu(tr("&Help"));

    QAction* LegendAction = HelpMenu->addAction(tr("&Legend"));
    LegendAction->setStatusTip(
        tr("Show what the text editor's special characters mean."));
    connect(LegendAction, &QAction::triggered, this, &MainWindow::OnLegend);

    QAction* Xml2rfcLegendAction = HelpMenu->addAction(tr("&xml2rfc Legend"));
    Xml2rfcLegendAction->setStatusTip(
        tr("Show the xml2rfc language, described by topic."));
    connect(Xml2rfcLegendAction, &QAction::triggered,
            this, &MainWindow::OnXml2rfcLegend);

    QAction* AboutAction = HelpMenu->addAction(tr("&About ProtocolDocEditor"));
    AboutAction->setStatusTip(
        tr("Show information about this editor, including the copyright and license."));
    connect(AboutAction, &QAction::triggered, this, &MainWindow::OnAbout);

    RebuildRecentMenu();

    // Outline: one column, no header, like the CoOp editor's outline.
    _Outline = new QTreeWidget(this);
    _Outline->setColumnCount(1);
    _Outline->setHeaderHidden(true);
    _Outline->setToolTip(tr("The parts of the draft. Select one to edit it."));
    connect(_Outline, &QTreeWidget::currentItemChanged,
            this, &MainWindow::OnOutlineSelectionChanged);
    // Right-click on a section opens the subsection menu (Bug 446).
    // The policy sits on the outline itself, as on the text edit.
    _Outline->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(_Outline, &QTreeWidget::customContextMenuRequested,
            this, &MainWindow::OnOutlineContextMenu);

    _AddSectionButton = new QPushButton(tr("Add"), this);
    _AddSectionButton->setToolTip(tr("Add a section after the selected one."));
    connect(_AddSectionButton, &QPushButton::clicked, this, &MainWindow::OnAddSection);
    _InsertSectionButton = new QPushButton(tr("Insert"), this);
    _InsertSectionButton->setToolTip(tr("Insert a section before the selected one."));
    connect(_InsertSectionButton, &QPushButton::clicked, this, &MainWindow::OnInsertSection);
    _RemoveSectionButton = new QPushButton(tr("Remove"), this);
    _RemoveSectionButton->setToolTip(tr("Remove the selected section."));
    connect(_RemoveSectionButton, &QPushButton::clicked, this, &MainWindow::OnRemoveSection);
    _MoveUpButton = new QPushButton(tr("Up"), this);
    _MoveUpButton->setToolTip(tr("Move the selected section up."));
    connect(_MoveUpButton, &QPushButton::clicked, this, &MainWindow::OnMoveSectionUp);
    _MoveDownButton = new QPushButton(tr("Down"), this);
    _MoveDownButton->setToolTip(tr("Move the selected section down."));
    connect(_MoveDownButton, &QPushButton::clicked, this, &MainWindow::OnMoveSectionDown);

    QHBoxLayout* SectionButtons = new QHBoxLayout();
    SectionButtons->addWidget(_AddSectionButton);
    SectionButtons->addWidget(_InsertSectionButton);
    SectionButtons->addWidget(_RemoveSectionButton);
    SectionButtons->addWidget(_MoveUpButton);
    SectionButtons->addWidget(_MoveDownButton);

    QWidget* OutlinePanel = new QWidget(this);
    QVBoxLayout* OutlineLayout = new QVBoxLayout(OutlinePanel);
    OutlineLayout->addWidget(_Outline, 1);
    OutlineLayout->addLayout(SectionButtons);
    OutlineLayout->setContentsMargins(0, 0, 0, 0);

    // Details pages, in DetailPage order.
    _Details = new QStackedWidget(this);

    QLabel* NoneLabel = new QLabel(tr("Select a part of the draft on the left."), this);
    NoneLabel->setWordWrap(true);
    _Details->addWidget(NoneLabel);

    _GroupLabel = new QLabel(this);
    _GroupLabel->setWordWrap(true);
    _GroupLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    _Details->addWidget(_GroupLabel);

    QWidget* AuthorsPageWidget = new QWidget(this);
    QLabel* AuthorsLabel = new QLabel(
        tr("The draft's authors. Authors can change over the lifetime "
           "of a draft: add and remove them here, and select an author "
           "to edit their details."), AuthorsPageWidget);
    AuthorsLabel->setWordWrap(true);
    QPushButton* AddAuthorButton = new QPushButton(tr("Add Author"), AuthorsPageWidget);
    AddAuthorButton->setToolTip(tr("Add an author to the draft."));
    connect(AddAuthorButton, &QPushButton::clicked, this, &MainWindow::OnAddAuthor);
    QVBoxLayout* AuthorsLayout = new QVBoxLayout(AuthorsPageWidget);
    AuthorsLayout->addWidget(AuthorsLabel);
    AuthorsLayout->addWidget(AddAuthorButton, 0, Qt::AlignLeft);
    AuthorsLayout->addStretch(1);
    _Details->addWidget(AuthorsPageWidget);

    QWidget* AuthorPageWidget = new QWidget(this);
    _AuthorFullnameEdit = new QLineEdit(AuthorPageWidget);
    _AuthorInitialsEdit = new QLineEdit(AuthorPageWidget);
    _AuthorSurnameEdit = new QLineEdit(AuthorPageWidget);
    _AuthorOrganizationEdit = new QLineEdit(AuthorPageWidget);
    _AuthorEmailEdit = new QLineEdit(AuthorPageWidget);
    _AuthorPhoneEdit = new QLineEdit(AuthorPageWidget);
    _AuthorUriEdit = new QLineEdit(AuthorPageWidget);
    _AuthorPostalStreetEdit = new QLineEdit(AuthorPageWidget);
    _AuthorPostalCityareaEdit = new QLineEdit(AuthorPageWidget);
    _AuthorPostalExtaddrEdit = new QLineEdit(AuthorPageWidget);
    _AuthorPostalPoboxEdit = new QLineEdit(AuthorPageWidget);
    _AuthorPostalCityEdit = new QLineEdit(AuthorPageWidget);
    _AuthorPostalRegionEdit = new QLineEdit(AuthorPageWidget);
    _AuthorPostalCodeEdit = new QLineEdit(AuthorPageWidget);
    _AuthorPostalCountryEdit = new QLineEdit(AuthorPageWidget);
    _AuthorPostalSortingcodeEdit = new QLineEdit(AuthorPageWidget);
    connect(_AuthorFullnameEdit, &QLineEdit::textEdited, this, &MainWindow::OnAuthorEdited);
    connect(_AuthorInitialsEdit, &QLineEdit::textEdited, this, &MainWindow::OnAuthorEdited);
    connect(_AuthorSurnameEdit, &QLineEdit::textEdited, this, &MainWindow::OnAuthorEdited);
    connect(_AuthorOrganizationEdit, &QLineEdit::textEdited, this, &MainWindow::OnAuthorEdited);
    connect(_AuthorEmailEdit, &QLineEdit::textEdited, this, &MainWindow::OnAuthorEdited);
    connect(_AuthorPhoneEdit, &QLineEdit::textEdited, this, &MainWindow::OnAuthorEdited);
    connect(_AuthorUriEdit, &QLineEdit::textEdited, this, &MainWindow::OnAuthorEdited);
    connect(_AuthorPostalStreetEdit, &QLineEdit::textEdited, this, &MainWindow::OnAuthorEdited);
    connect(_AuthorPostalCityareaEdit, &QLineEdit::textEdited, this, &MainWindow::OnAuthorEdited);
    connect(_AuthorPostalExtaddrEdit, &QLineEdit::textEdited, this, &MainWindow::OnAuthorEdited);
    connect(_AuthorPostalPoboxEdit, &QLineEdit::textEdited, this, &MainWindow::OnAuthorEdited);
    connect(_AuthorPostalCityEdit, &QLineEdit::textEdited, this, &MainWindow::OnAuthorEdited);
    connect(_AuthorPostalRegionEdit, &QLineEdit::textEdited, this, &MainWindow::OnAuthorEdited);
    connect(_AuthorPostalCodeEdit, &QLineEdit::textEdited, this, &MainWindow::OnAuthorEdited);
    connect(_AuthorPostalCountryEdit, &QLineEdit::textEdited, this, &MainWindow::OnAuthorEdited);
    connect(_AuthorPostalSortingcodeEdit, &QLineEdit::textEdited, this, &MainWindow::OnAuthorEdited);
    // The postal address is either structured parts or free-form
    // lines, never both: the mode picker switches the stacked
    // widget between the parts form and the lines editor.
    _AuthorPostalModeCombo = new QComboBox(AuthorPageWidget);
    _AuthorPostalModeCombo->setToolTip(tr("How the postal address is written."));
    _AuthorPostalModeCombo->addItem(tr("Structured address"));
    _AuthorPostalModeCombo->addItem(tr("Free-form lines"));
    connect(_AuthorPostalModeCombo,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &MainWindow::OnPostalModeChanged);
    QWidget* StructuredPostalWidget = new QWidget(AuthorPageWidget);
    QFormLayout* StructuredPostalLayout = new QFormLayout(StructuredPostalWidget);
    StructuredPostalLayout->setContentsMargins(0, 0, 0, 0);
    StructuredPostalLayout->addRow(tr("PO Box:"), _AuthorPostalPoboxEdit);
    StructuredPostalLayout->addRow(tr("Street:"), _AuthorPostalStreetEdit);
    StructuredPostalLayout->addRow(tr("Extended address:"), _AuthorPostalExtaddrEdit);
    StructuredPostalLayout->addRow(tr("City area:"), _AuthorPostalCityareaEdit);
    StructuredPostalLayout->addRow(tr("City:"), _AuthorPostalCityEdit);
    StructuredPostalLayout->addRow(tr("Region:"), _AuthorPostalRegionEdit);
    StructuredPostalLayout->addRow(tr("Code:"), _AuthorPostalCodeEdit);
    StructuredPostalLayout->addRow(tr("Country:"), _AuthorPostalCountryEdit);
    StructuredPostalLayout->addRow(tr("Sorting code:"), _AuthorPostalSortingcodeEdit);
    _AuthorPostalLinesEdit = new QPlainTextEdit(AuthorPageWidget);
    _AuthorPostalLinesEdit->setToolTip(tr("One address line per line."));
    connect(_AuthorPostalLinesEdit, &QPlainTextEdit::textChanged,
            this, &MainWindow::OnAuthorEdited);
    _AuthorPostalStack = new QStackedWidget(AuthorPageWidget);
    _AuthorPostalStack->addWidget(StructuredPostalWidget);
    _AuthorPostalStack->addWidget(_AuthorPostalLinesEdit);
    QGroupBox* PostalBox = new QGroupBox(tr("Postal address"), AuthorPageWidget);
    QVBoxLayout* PostalLayout = new QVBoxLayout(PostalBox);
    PostalLayout->addWidget(_AuthorPostalModeCombo);
    PostalLayout->addWidget(_AuthorPostalStack);
    QGroupBox* RoleBox = new QGroupBox(tr("Role"), AuthorPageWidget);
    _AuthorRadio = new QRadioButton(tr("Author"), RoleBox);
    _EditorRadio = new QRadioButton(tr("Editor"), RoleBox);
    connect(_AuthorRadio, &QRadioButton::toggled, this, &MainWindow::OnAuthorRoleChanged);
    connect(_EditorRadio, &QRadioButton::toggled, this, &MainWindow::OnAuthorRoleChanged);
    QHBoxLayout* RoleLayout = new QHBoxLayout(RoleBox);
    RoleLayout->addWidget(_AuthorRadio);
    RoleLayout->addWidget(_EditorRadio);
    RoleLayout->addStretch(1);
    QPushButton* RemoveAuthorButton = new QPushButton(tr("Remove Author"), AuthorPageWidget);
    RemoveAuthorButton->setToolTip(tr("Remove this author from the draft."));
    connect(RemoveAuthorButton, &QPushButton::clicked, this, &MainWindow::OnRemoveAuthor);
    _AuthorPreservedLabel = new QLabel(AuthorPageWidget);
    _AuthorPreservedLabel->setWordWrap(true);
    QLabel* RequiredLabel = new QLabel(tr("* Required"), AuthorPageWidget);
    QFormLayout* AuthorLayout = new QFormLayout(AuthorPageWidget);
    AuthorLayout->addRow(tr("Full name *:"), _AuthorFullnameEdit);
    AuthorLayout->addRow(tr("Initials:"), _AuthorInitialsEdit);
    AuthorLayout->addRow(tr("Surname:"), _AuthorSurnameEdit);
    AuthorLayout->addRow(tr("Organization:"), _AuthorOrganizationEdit);
    AuthorLayout->addRow(tr("Email:"), _AuthorEmailEdit);
    AuthorLayout->addRow(tr("Phone:"), _AuthorPhoneEdit);
    AuthorLayout->addRow(tr("URI:"), _AuthorUriEdit);
    AuthorLayout->addRow(PostalBox);
    AuthorLayout->addRow(RoleBox);
    AuthorLayout->addRow(RemoveAuthorButton);
    AuthorLayout->addRow(RequiredLabel);
    AuthorLayout->addRow(_AuthorPreservedLabel);
    _Details->addWidget(AuthorPageWidget);

    QWidget* AbstractPageWidget = new QWidget(this);
    _AbstractEdit = new TextEditor(AbstractPageWidget);
    _AbstractEdit->SetSpellChecker(_SpellChecker.get());
    _AbstractEdit->SetDocument(&_Document);
    connect(_AbstractEdit, &TextEditor::TextChanged, this, &MainWindow::OnAbstractEdited);
    connect(_AbstractEdit, &TextEditor::MakeReferenceRequested, this,
            [this](int Action, xmlNodePtr Target, const QString& Anchor, const QString& ListName) {
                HandleMakeReference(_AbstractEdit, Action, Target, Anchor, ListName);
            });
    _AbstractPreservedLabel = new QLabel(AbstractPageWidget);
    _AbstractPreservedLabel->setWordWrap(true);
    QVBoxLayout* AbstractLayout = new QVBoxLayout(AbstractPageWidget);
    AbstractLayout->addWidget(new QLabel(tr("Abstract:"), AbstractPageWidget));
    AbstractLayout->addWidget(_AbstractEdit);
    AbstractLayout->addWidget(_AbstractPreservedLabel);
    _Details->addWidget(AbstractPageWidget);

    QWidget* DatePageWidget = new QWidget(this);
    _DateLabel = new QLabel(DatePageWidget);
    QLabel* DateNote = new QLabel(
        tr("A draft's date is always the current date until the draft "
           "is submitted. A draft expires 6 months after its date, so "
           "the newest date keeps the most time."), DatePageWidget);
    DateNote->setWordWrap(true);
    QVBoxLayout* DateLayout = new QVBoxLayout(DatePageWidget);
    DateLayout->addWidget(new QLabel(tr("Date:"), DatePageWidget));
    DateLayout->addWidget(_DateLabel);
    DateLayout->addWidget(DateNote);
    DateLayout->addStretch(1);
    _Details->addWidget(DatePageWidget);

    QWidget* SectionPageWidget = new QWidget(this);
    _SectionTitleEdit = new QLineEdit(SectionPageWidget);
    _SectionTitleEdit->setToolTip(tr("The section title."));
    connect(_SectionTitleEdit, &QLineEdit::textEdited, this, &MainWindow::OnSectionTitleEdited);
    _SectionBodyEdit = new TextEditor(SectionPageWidget);
    _SectionBodyEdit->SetSpellChecker(_SpellChecker.get());
    _SectionBodyEdit->SetDocument(&_Document);
    connect(_SectionBodyEdit, &TextEditor::TextChanged, this, &MainWindow::OnSectionBodyEdited);
    connect(_SectionBodyEdit, &TextEditor::MakeReferenceRequested, this,
            [this](int Action, xmlNodePtr Target, const QString& Anchor, const QString& ListName) {
                HandleMakeReference(_SectionBodyEdit, Action, Target, Anchor, ListName);
            });
    _SectionPreservedLabel = new QLabel(SectionPageWidget);
    _SectionPreservedLabel->setWordWrap(true);
    _SectionRequiredLabel = new QLabel(SectionPageWidget);
    _SectionRequiredLabel->setWordWrap(true);
    _SectionRequiredLabel->setText(
        tr("This is a required section. Its title, position, and "
           "presence are fixed."));
    _SectionRequiredLabel->hide();
    QVBoxLayout* SectionLayout = new QVBoxLayout(SectionPageWidget);
    SectionLayout->addWidget(new QLabel(tr("Section title:"), SectionPageWidget));
    SectionLayout->addWidget(_SectionTitleEdit);
    SectionLayout->addWidget(_SectionRequiredLabel);
    SectionLayout->addWidget(new QLabel(tr("Body:"), SectionPageWidget));
    SectionLayout->addWidget(_SectionBodyEdit);
    SectionLayout->addWidget(_SectionPreservedLabel);
    _Details->addWidget(SectionPageWidget);

    QWidget* ReferencesPageWidget = new QWidget(this);
    _ReferencesLabel = new QLabel(ReferencesPageWidget);
    _ReferencesLabel->setWordWrap(true);
    _ReferencesLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    QPushButton* AddGroupButton = new QPushButton(tr("Add List"), ReferencesPageWidget);
    AddGroupButton->setToolTip(tr("Add a list of references."));
    connect(AddGroupButton, &QPushButton::clicked, this, &MainWindow::OnAddReferenceGroup);
    QVBoxLayout* ReferencesLayout = new QVBoxLayout(ReferencesPageWidget);
    ReferencesLayout->addWidget(_ReferencesLabel);
    ReferencesLayout->addWidget(AddGroupButton, 0, Qt::AlignLeft);
    ReferencesLayout->addStretch(1);
    _Details->addWidget(ReferencesPageWidget);

    QWidget* ReferenceGroupPageWidget = new QWidget(this);
    _ReferenceGroupNameEdit = new QLineEdit(ReferenceGroupPageWidget);
    _ReferenceGroupNameEdit->setToolTip(tr("The list name, such as Normative References."));
    connect(_ReferenceGroupNameEdit, &QLineEdit::textEdited,
            this, &MainWindow::OnReferenceGroupNameEdited);
    QPushButton* AddEntryButton = new QPushButton(tr("Add Entry"), ReferenceGroupPageWidget);
    AddEntryButton->setToolTip(tr("Add a reference entry to this list."));
    connect(AddEntryButton, &QPushButton::clicked, this, &MainWindow::OnAddReferenceEntry);
    _AddReferenceEntryButton = AddEntryButton;
    QPushButton* AddNestedListButton = new QPushButton(tr("Add Nested List"), ReferenceGroupPageWidget);
    AddNestedListButton->setToolTip(tr("Add a list inside this list, such as a normative list inside a references list."));
    connect(AddNestedListButton, &QPushButton::clicked, this, &MainWindow::OnAddNestedReferenceGroup);
    _AddNestedListButton = AddNestedListButton;
    QPushButton* RemoveGroupButton = new QPushButton(tr("Remove List"), ReferenceGroupPageWidget);
    RemoveGroupButton->setToolTip(tr("Remove this list and its entries."));
    connect(RemoveGroupButton, &QPushButton::clicked, this, &MainWindow::OnRemoveReferenceGroup);
    QPushButton* MoveGroupUpButton = new QPushButton(tr("Up"), ReferenceGroupPageWidget);
    MoveGroupUpButton->setToolTip(tr("Move this list up."));
    connect(MoveGroupUpButton, &QPushButton::clicked, this, &MainWindow::OnMoveReferenceGroupUp);
    QPushButton* MoveGroupDownButton = new QPushButton(tr("Down"), ReferenceGroupPageWidget);
    MoveGroupDownButton->setToolTip(tr("Move this list down."));
    connect(MoveGroupDownButton, &QPushButton::clicked, this, &MainWindow::OnMoveReferenceGroupDown);
    _ReferenceGroupPreservedLabel = new QLabel(ReferenceGroupPageWidget);
    _ReferenceGroupPreservedLabel->setWordWrap(true);
    QHBoxLayout* GroupButtons = new QHBoxLayout();
    GroupButtons->addWidget(AddEntryButton);
    GroupButtons->addWidget(AddNestedListButton);
    GroupButtons->addWidget(RemoveGroupButton);
    GroupButtons->addWidget(MoveGroupUpButton);
    GroupButtons->addWidget(MoveGroupDownButton);
    GroupButtons->addStretch(1);
    QFormLayout* ReferenceGroupLayout = new QFormLayout(ReferenceGroupPageWidget);
    ReferenceGroupLayout->addRow(tr("List name:"), _ReferenceGroupNameEdit);
    ReferenceGroupLayout->addRow(GroupButtons);
    ReferenceGroupLayout->addRow(_ReferenceGroupPreservedLabel);
    _Details->addWidget(ReferenceGroupPageWidget);

    QWidget* ReferenceEntryPageWidget = new QWidget(this);
    _ReferenceAnchorEdit = new QLineEdit(ReferenceEntryPageWidget);
    _ReferenceAnchorEdit->setToolTip(tr("The anchor that cites this reference."));
    connect(_ReferenceAnchorEdit, &QLineEdit::textEdited, this, &MainWindow::OnReferenceEntryEdited);
    _ReferenceTargetEdit = new QLineEdit(ReferenceEntryPageWidget);
    _ReferenceTargetEdit->setToolTip(tr("The web address of the reference."));
    connect(_ReferenceTargetEdit, &QLineEdit::textEdited, this, &MainWindow::OnReferenceEntryEdited);
    _ReferenceTitleEdit = new QLineEdit(ReferenceEntryPageWidget);
    _ReferenceTitleEdit->setToolTip(tr("The title of the referenced work."));
    connect(_ReferenceTitleEdit, &QLineEdit::textEdited, this, &MainWindow::OnReferenceEntryEdited);
    _ReferenceDateDayEdit = new QLineEdit(ReferenceEntryPageWidget);
    _ReferenceDateDayEdit->setToolTip(tr("The day of publication, when known."));
    connect(_ReferenceDateDayEdit, &QLineEdit::textEdited, this, &MainWindow::OnReferenceEntryEdited);
    _ReferenceDateMonthEdit = new QLineEdit(ReferenceEntryPageWidget);
    _ReferenceDateMonthEdit->setToolTip(tr("The month of publication, which may be vague."));
    connect(_ReferenceDateMonthEdit, &QLineEdit::textEdited, this, &MainWindow::OnReferenceEntryEdited);
    _ReferenceDateYearEdit = new QLineEdit(ReferenceEntryPageWidget);
    _ReferenceDateYearEdit->setToolTip(tr("The year of publication, which may be vague or a range."));
    connect(_ReferenceDateYearEdit, &QLineEdit::textEdited, this, &MainWindow::OnReferenceEntryEdited);
    _ReferenceStreamCombo = new QComboBox(ReferenceEntryPageWidget);
    _ReferenceStreamCombo->setToolTip(tr("The stream the referenced document belongs to."));
    _ReferenceStreamCombo->addItem(QString());
    _ReferenceStreamCombo->addItem(QStringLiteral("IETF"));
    _ReferenceStreamCombo->addItem(QStringLiteral("IAB"));
    _ReferenceStreamCombo->addItem(QStringLiteral("IRTF"));
    _ReferenceStreamCombo->addItem(QStringLiteral("independent"));
    _ReferenceStreamCombo->addItem(QStringLiteral("editorial"));
    connect(_ReferenceStreamCombo,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &MainWindow::OnReferenceStreamChanged);
    _ReferenceSeriesTable = new QTableWidget(ReferenceEntryPageWidget);
    _ReferenceSeriesTable->setColumnCount(2);
    _ReferenceSeriesTable->setHorizontalHeaderLabels({tr("Series"), tr("Value")});
    _ReferenceSeriesTable->horizontalHeader()->setStretchLastSection(true);
    _ReferenceSeriesTable->setToolTip(tr("The series this reference belongs to, such as RFC 2119."));
    connect(_ReferenceSeriesTable, &QTableWidget::cellChanged,
            this, &MainWindow::OnSeriesInfoChanged);
    QPushButton* AddSeriesButton = new QPushButton(tr("Add Series Info"), ReferenceEntryPageWidget);
    AddSeriesButton->setToolTip(tr("Add a series info row."));
    connect(AddSeriesButton, &QPushButton::clicked, this, &MainWindow::OnAddSeriesInfo);
    QPushButton* RemoveSeriesButton = new QPushButton(tr("Remove Series Info"), ReferenceEntryPageWidget);
    RemoveSeriesButton->setToolTip(tr("Remove the selected series info row."));
    connect(RemoveSeriesButton, &QPushButton::clicked, this, &MainWindow::OnRemoveSeriesInfo);
    QWidget* SeriesWidget = new QWidget(ReferenceEntryPageWidget);
    QVBoxLayout* SeriesLayout = new QVBoxLayout(SeriesWidget);
    SeriesLayout->setContentsMargins(0, 0, 0, 0);
    SeriesLayout->addWidget(_ReferenceSeriesTable);
    QHBoxLayout* SeriesButtons = new QHBoxLayout();
    SeriesButtons->addWidget(AddSeriesButton);
    SeriesButtons->addWidget(RemoveSeriesButton);
    SeriesButtons->addStretch(1);
    SeriesLayout->addLayout(SeriesButtons);
    _ReferenceMoveToCombo = new QComboBox(ReferenceEntryPageWidget);
    _ReferenceMoveToCombo->setToolTip(tr("Move this entry to another list."));
    connect(_ReferenceMoveToCombo,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &MainWindow::OnReferenceMoveToGroupChanged);
    QPushButton* AddReferenceAuthorButton = new QPushButton(tr("Add Author"), ReferenceEntryPageWidget);
    AddReferenceAuthorButton->setToolTip(tr("Add an author to this reference."));
    connect(AddReferenceAuthorButton, &QPushButton::clicked, this, &MainWindow::OnAddReferenceAuthor);
    QPushButton* RemoveEntryButton = new QPushButton(tr("Remove Entry"), ReferenceEntryPageWidget);
    RemoveEntryButton->setToolTip(tr("Remove this reference entry."));
    connect(RemoveEntryButton, &QPushButton::clicked, this, &MainWindow::OnRemoveReferenceEntry);
    QPushButton* MoveEntryUpButton = new QPushButton(tr("Up"), ReferenceEntryPageWidget);
    MoveEntryUpButton->setToolTip(tr("Move this entry up."));
    connect(MoveEntryUpButton, &QPushButton::clicked, this, &MainWindow::OnMoveReferenceEntryUp);
    QPushButton* MoveEntryDownButton = new QPushButton(tr("Down"), ReferenceEntryPageWidget);
    MoveEntryDownButton->setToolTip(tr("Move this entry down."));
    connect(MoveEntryDownButton, &QPushButton::clicked, this, &MainWindow::OnMoveReferenceEntryDown);
    _ReferenceEntryPreservedLabel = new QLabel(ReferenceEntryPageWidget);
    _ReferenceEntryPreservedLabel->setWordWrap(true);
    QLabel* EntryRequiredLabel = new QLabel(tr("* Required"), ReferenceEntryPageWidget);
    QHBoxLayout* EntryButtons = new QHBoxLayout();
    EntryButtons->addWidget(AddReferenceAuthorButton);
    EntryButtons->addWidget(RemoveEntryButton);
    EntryButtons->addWidget(MoveEntryUpButton);
    EntryButtons->addWidget(MoveEntryDownButton);
    EntryButtons->addStretch(1);
    QFormLayout* ReferenceEntryLayout = new QFormLayout(ReferenceEntryPageWidget);
    ReferenceEntryLayout->addRow(tr("Anchor *:"), _ReferenceAnchorEdit);
    ReferenceEntryLayout->addRow(tr("Target:"), _ReferenceTargetEdit);
    ReferenceEntryLayout->addRow(tr("Title:"), _ReferenceTitleEdit);
    ReferenceEntryLayout->addRow(tr("Day:"), _ReferenceDateDayEdit);
    ReferenceEntryLayout->addRow(tr("Month:"), _ReferenceDateMonthEdit);
    ReferenceEntryLayout->addRow(tr("Year:"), _ReferenceDateYearEdit);
    ReferenceEntryLayout->addRow(tr("Stream:"), _ReferenceStreamCombo);
    ReferenceEntryLayout->addRow(tr("Series info:"), SeriesWidget);
    ReferenceEntryLayout->addRow(tr("Move to list:"), _ReferenceMoveToCombo);
    ReferenceEntryLayout->addRow(EntryButtons);
    ReferenceEntryLayout->addRow(EntryRequiredLabel);
    ReferenceEntryLayout->addRow(_ReferenceEntryPreservedLabel);
    _Details->addWidget(ReferenceEntryPageWidget);

    // Document Settings: the title settings, the series info,
    // the area, workgroup, and keyword lists, and the first
    // note (Bugs 332-359).
    QWidget* SettingsPageWidget = new QWidget(this);
    _SettingsTitleEdit = new QLineEdit(SettingsPageWidget);
    _SettingsTitleEdit->setToolTip(tr("The draft title."));
    connect(_SettingsTitleEdit, &QLineEdit::textEdited,
            this, &MainWindow::OnSettingsTitleEdited);
    _SettingsTitleAbbrevEdit = new QLineEdit(SettingsPageWidget);
    _SettingsTitleAbbrevEdit->setToolTip(tr("The abbreviated title, shown in page headers. At most 40 characters, and required."));
    _SettingsTitleAbbrevEdit->setMaxLength(40);
    connect(_SettingsTitleAbbrevEdit, &QLineEdit::textEdited,
            this, &MainWindow::OnSettingsTitleDetailsEdited);
    _SettingsTitleAsciiEdit = new QLineEdit(SettingsPageWidget);
    _SettingsTitleAsciiEdit->setToolTip(tr("The ASCII form of the title, for non-ASCII titles."));
    connect(_SettingsTitleAsciiEdit, &QLineEdit::textEdited,
            this, &MainWindow::OnSettingsTitleDetailsEdited);
    _SettingsTitleBreaksLabel = new QLabel(SettingsPageWidget);
    _SettingsTitleBreaksLabel->setWordWrap(true);
    _SettingsSeriesTable = new QTableWidget(SettingsPageWidget);
    _SettingsSeriesTable->setColumnCount(2);
    _SettingsSeriesTable->setHorizontalHeaderLabels({tr("Series"), tr("Value")});
    _SettingsSeriesTable->horizontalHeader()->setStretchLastSection(true);
    _SettingsSeriesTable->setToolTip(tr("The series this draft belongs to."));
    connect(_SettingsSeriesTable, &QTableWidget::cellChanged,
            this, &MainWindow::OnDocumentSeriesChanged);
    QPushButton* AddDocumentSeriesButton =
        new QPushButton(tr("Add Series Info"), SettingsPageWidget);
    AddDocumentSeriesButton->setToolTip(tr("Add a series info row."));
    connect(AddDocumentSeriesButton, &QPushButton::clicked,
            this, &MainWindow::OnAddDocumentSeriesInfo);
    QPushButton* RemoveDocumentSeriesButton =
        new QPushButton(tr("Remove Series Info"), SettingsPageWidget);
    RemoveDocumentSeriesButton->setToolTip(tr("Remove the selected series info row."));
    connect(RemoveDocumentSeriesButton, &QPushButton::clicked,
            this, &MainWindow::OnRemoveDocumentSeriesInfo);
    _SettingsSeriesNoteLabel = new QLabel(
        tr("A row named Internet-Draft gets its value from the draft name."),
        SettingsPageWidget);
    _SettingsSeriesNoteLabel->setWordWrap(true);
    _SettingsAreasEdit = new QPlainTextEdit(SettingsPageWidget);
    _SettingsAreasEdit->setToolTip(tr("One area per line."));
    connect(_SettingsAreasEdit, &QPlainTextEdit::textChanged,
            this, &MainWindow::OnSettingsListsEdited);
    _SettingsWorkgroupsEdit = new QPlainTextEdit(SettingsPageWidget);
    _SettingsWorkgroupsEdit->setToolTip(tr("One workgroup per line."));
    connect(_SettingsWorkgroupsEdit, &QPlainTextEdit::textChanged,
            this, &MainWindow::OnSettingsListsEdited);
    _SettingsKeywordsEdit = new QPlainTextEdit(SettingsPageWidget);
    _SettingsKeywordsEdit->setToolTip(tr("One keyword per line."));
    connect(_SettingsKeywordsEdit, &QPlainTextEdit::textChanged,
            this, &MainWindow::OnSettingsListsEdited);
    // The lists show three lines each; the note editor below
    // takes the rest of the pane.
    int ListEditHeight =
        QFontMetrics(_SettingsAreasEdit->font()).lineSpacing() * 3 + 12;
    _SettingsAreasEdit->setMaximumHeight(ListEditHeight);
    _SettingsWorkgroupsEdit->setMaximumHeight(ListEditHeight);
    _SettingsKeywordsEdit->setMaximumHeight(ListEditHeight);
    _SettingsNoteGroup = new QGroupBox(tr("Note"), SettingsPageWidget);
    _SettingsNoteNameEdit = new QLineEdit(_SettingsNoteGroup);
    _SettingsNoteNameEdit->setToolTip(tr("The note's name, shown above it."));
    connect(_SettingsNoteNameEdit, &QLineEdit::textEdited,
            this, &MainWindow::OnSettingsNoteNameEdited);
    _SettingsNoteRemoveCheck = new QCheckBox(
        tr("Remove before publishing as an RFC"), _SettingsNoteGroup);
    _SettingsNoteRemoveCheck->setToolTip(
        tr("Ask xml2rfc to drop this note when the draft becomes an RFC."));
    connect(_SettingsNoteRemoveCheck, &QCheckBox::toggled,
            this, &MainWindow::OnSettingsNoteRemoveChanged);
    _SettingsNoteEdit = new TextEditor(_SettingsNoteGroup);
    _SettingsNoteEdit->SetSpellChecker(_SpellChecker.get());
    _SettingsNoteEdit->SetDocument(&_Document);
    connect(_SettingsNoteEdit, &TextEditor::TextChanged,
            this, &MainWindow::OnSettingsNoteEdited);
    connect(_SettingsNoteEdit, &TextEditor::MakeReferenceRequested, this,
            [this](int Action, xmlNodePtr Target, const QString& Anchor, const QString& ListName) {
                HandleMakeReference(_SettingsNoteEdit, Action, Target, Anchor, ListName);
            });
    QPushButton* RemoveNoteButton = new QPushButton(tr("Remove Note"), _SettingsNoteGroup);
    RemoveNoteButton->setToolTip(tr("Remove this note from the draft."));
    connect(RemoveNoteButton, &QPushButton::clicked, this, &MainWindow::OnRemoveNote);
    _SettingsNoteExtraLabel = new QLabel(_SettingsNoteGroup);
    _SettingsNoteExtraLabel->setWordWrap(true);
    _SettingsNotePreservedLabel = new QLabel(_SettingsNoteGroup);
    _SettingsNotePreservedLabel->setWordWrap(true);
    QVBoxLayout* NoteLayout = new QVBoxLayout(_SettingsNoteGroup);
    NoteLayout->addWidget(new QLabel(tr("Name:"), _SettingsNoteGroup));
    NoteLayout->addWidget(_SettingsNoteNameEdit);
    NoteLayout->addWidget(_SettingsNoteRemoveCheck);
    NoteLayout->addWidget(_SettingsNoteEdit);
    NoteLayout->addWidget(RemoveNoteButton, 0, Qt::AlignLeft);
    NoteLayout->addWidget(_SettingsNoteExtraLabel);
    NoteLayout->addWidget(_SettingsNotePreservedLabel);
    _SettingsAddNoteButton = new QPushButton(tr("Add Note"), SettingsPageWidget);
    _SettingsAddNoteButton->setToolTip(tr("Add a note to the front matter."));
    connect(_SettingsAddNoteButton, &QPushButton::clicked,
            this, &MainWindow::OnAddNote);
    _SettingsPreservedLabel = new QLabel(SettingsPageWidget);
    _SettingsPreservedLabel->setWordWrap(true);
    QFormLayout* SettingsTitleLayout = new QFormLayout();
    SettingsTitleLayout->addRow(tr("Title:"), _SettingsTitleEdit);
    SettingsTitleLayout->addRow(tr("Abbreviated title:"), _SettingsTitleAbbrevEdit);
    SettingsTitleLayout->addRow(tr("ASCII title:"), _SettingsTitleAsciiEdit);
    QHBoxLayout* SettingsSeriesButtons = new QHBoxLayout();
    SettingsSeriesButtons->addWidget(AddDocumentSeriesButton);
    SettingsSeriesButtons->addWidget(RemoveDocumentSeriesButton);
    SettingsSeriesButtons->addStretch(1);
    QVBoxLayout* SettingsLayout = new QVBoxLayout(SettingsPageWidget);
    SettingsLayout->addLayout(SettingsTitleLayout);
    SettingsLayout->addWidget(_SettingsTitleBreaksLabel);
    SettingsLayout->addWidget(new QLabel(tr("Series info:"), SettingsPageWidget));
    SettingsLayout->addWidget(_SettingsSeriesTable);
    SettingsLayout->addLayout(SettingsSeriesButtons);
    SettingsLayout->addWidget(_SettingsSeriesNoteLabel);
    SettingsLayout->addWidget(new QLabel(tr("Areas (one per line):"), SettingsPageWidget));
    SettingsLayout->addWidget(_SettingsAreasEdit);
    SettingsLayout->addWidget(new QLabel(tr("Workgroups (one per line):"), SettingsPageWidget));
    SettingsLayout->addWidget(_SettingsWorkgroupsEdit);
    SettingsLayout->addWidget(new QLabel(tr("Keywords (one per line):"), SettingsPageWidget));
    SettingsLayout->addWidget(_SettingsKeywordsEdit);
    SettingsLayout->addWidget(_SettingsNoteGroup);
    SettingsLayout->addWidget(_SettingsAddNoteButton, 0, Qt::AlignLeft);
    SettingsLayout->addWidget(_SettingsPreservedLabel);
    SettingsLayout->addStretch(1);
    _Details->addWidget(SettingsPageWidget);

    QSplitter* Splitter = new QSplitter(this);
    Splitter->addWidget(OutlinePanel);
    Splitter->addWidget(_Details);
    Splitter->setStretchFactor(0, 1);
    Splitter->setStretchFactor(1, 3);

    QPushButton* SaveButton = new QPushButton(tr("Save"), this);
    SaveButton->setToolTip(tr("Save the draft as XML."));
    connect(SaveButton, &QPushButton::clicked, this, &MainWindow::OnSave);
    QPushButton* RunButton = new QPushButton(tr("Run xml2rfc"), this);
    RunButton->setToolTip(tr("Run xml2rfc on the saved file."));
    connect(RunButton, &QPushButton::clicked, this, &MainWindow::OnRunXml2rfc);
    // The output formats for the run: any combination may be
    // checked, and one run produces every checked format (Bug
    // 320). The choices are remembered across runs; Text alone
    // is the starting choice.
    QSettings FormatSettings;
    _TextFormatCheck = new QCheckBox(tr("Text"), this);
    _TextFormatCheck->setToolTip(tr("Produce the .txt output."));
    _TextFormatCheck->setChecked(
        FormatSettings.value(QStringLiteral("xml2rfc/text"), true).toBool());
    _PdfFormatCheck = new QCheckBox(tr("PDF"), this);
    _PdfFormatCheck->setToolTip(tr("Produce the .pdf output."));
    _PdfFormatCheck->setChecked(
        FormatSettings.value(QStringLiteral("xml2rfc/pdf"), false).toBool());
    _HtmlFormatCheck = new QCheckBox(tr("HTML"), this);
    _HtmlFormatCheck->setToolTip(tr("Produce the .html output."));
    _HtmlFormatCheck->setChecked(
        FormatSettings.value(QStringLiteral("xml2rfc/html"), false).toBool());
    connect(_TextFormatCheck, &QCheckBox::toggled,
            this, &MainWindow::OnFormatChoiceChanged);
    connect(_PdfFormatCheck, &QCheckBox::toggled,
            this, &MainWindow::OnFormatChoiceChanged);
    connect(_HtmlFormatCheck, &QCheckBox::toggled,
            this, &MainWindow::OnFormatChoiceChanged);
    // Each format gets its checkbox over its View button (Bug
    // 325): the button is enabled only when the format is
    // checked and its output file exists.
    QWidget* TextGroup = new QWidget(this);
    QVBoxLayout* TextGroupLayout = new QVBoxLayout(TextGroup);
    TextGroupLayout->setContentsMargins(0, 0, 0, 0);
    TextGroupLayout->addWidget(_TextFormatCheck);
    _TextViewButton = new QPushButton(tr("View"), TextGroup);
    _TextViewButton->setToolTip(tr("Open the .txt output in Notepad."));
    connect(_TextViewButton, &QPushButton::clicked, this, &MainWindow::OnViewText);
    TextGroupLayout->addWidget(_TextViewButton);
    QWidget* PdfGroup = new QWidget(this);
    QVBoxLayout* PdfGroupLayout = new QVBoxLayout(PdfGroup);
    PdfGroupLayout->setContentsMargins(0, 0, 0, 0);
    PdfGroupLayout->addWidget(_PdfFormatCheck);
    _PdfViewButton = new QPushButton(tr("View"), PdfGroup);
    _PdfViewButton->setToolTip(tr("Open the .pdf output in the default PDF viewer."));
    connect(_PdfViewButton, &QPushButton::clicked, this, &MainWindow::OnViewPdf);
    PdfGroupLayout->addWidget(_PdfViewButton);
    QWidget* HtmlGroup = new QWidget(this);
    QVBoxLayout* HtmlGroupLayout = new QVBoxLayout(HtmlGroup);
    HtmlGroupLayout->setContentsMargins(0, 0, 0, 0);
    HtmlGroupLayout->addWidget(_HtmlFormatCheck);
    _HtmlViewButton = new QPushButton(tr("View"), HtmlGroup);
    _HtmlViewButton->setToolTip(tr("Open the .html output in the default browser."));
    connect(_HtmlViewButton, &QPushButton::clicked, this, &MainWindow::OnViewHtml);
    HtmlGroupLayout->addWidget(_HtmlViewButton);
    // The formats and the run sit on the left; Save sits alone
    // on the right (Bug 321).
    QHBoxLayout* BottomButtons = new QHBoxLayout();
    BottomButtons->addWidget(TextGroup);
    BottomButtons->addWidget(PdfGroup);
    BottomButtons->addWidget(HtmlGroup);
    BottomButtons->addWidget(RunButton);
    BottomButtons->addStretch(1);
    BottomButtons->addWidget(SaveButton);

    QWidget* Central = new QWidget(this);
    QVBoxLayout* CentralLayout = new QVBoxLayout(Central);
    CentralLayout->addWidget(Splitter, 1);
    CentralLayout->addLayout(BottomButtons);
    setCentralWidget(Central);

    // The xml2rfc run log dialog, reused for every run.
    _RunDialog = new QDialog(this);
    _RunDialog->setWindowTitle(tr("xml2rfc"));
    _RunDialog->resize(700, 400);
    _RunLog = new QPlainTextEdit(_RunDialog);
    _RunLog->setReadOnly(true);
    _RunCloseButton = new QPushButton(tr("Close"), _RunDialog);
    connect(_RunCloseButton, &QPushButton::clicked, _RunDialog, &QDialog::hide);
    QVBoxLayout* RunLayout = new QVBoxLayout(_RunDialog);
    RunLayout->addWidget(_RunLog, 1);
    RunLayout->addWidget(_RunCloseButton, 0, Qt::AlignRight);

    // The validation results dialog, reused for every check.
    _ValidateDialog = new QDialog(this);
    _ValidateDialog->setWindowTitle(tr("Validate"));
    _ValidateDialog->resize(700, 400);
    _ValidateLog = new QPlainTextEdit(_ValidateDialog);
    _ValidateLog->setReadOnly(true);
    _ValidateCloseButton = new QPushButton(tr("Close"), _ValidateDialog);
    connect(_ValidateCloseButton, &QPushButton::clicked, _ValidateDialog, &QDialog::hide);
    QVBoxLayout* ValidateLayout = new QVBoxLayout(_ValidateDialog);
    ValidateLayout->addWidget(_ValidateLog, 1);
    ValidateLayout->addWidget(_ValidateCloseButton, 0, Qt::AlignRight);

    _RunProcess = new QProcess(this);
    connect(_RunProcess, &QProcess::readyReadStandardOutput, this, &MainWindow::OnRunOutput);
    connect(_RunProcess, &QProcess::readyReadStandardError, this, &MainWindow::OnRunErrorOutput);
    connect(_RunProcess, &QProcess::finished, this, &MainWindow::OnRunFinished);
    connect(_RunProcess, &QProcess::errorOccurred, this, &MainWindow::OnRunFailed);

    statusBar()->showMessage(tr("Ready."));
    resize(900, 600);
    RebuildOutline();
    UpdateWindowTitle();
    UpdateViewButtons();
}

void MainWindow::SetDocument(const DocDocument& Document, const QString& FilePath)
{
    _Document = Document;
    _FilePath = FilePath;
    UpdateProjectDictionary();
    _SelectedKey = QStringLiteral("title");
    // A new draft starts modified: its file is not written yet.
    SetModified(true);
    RebuildOutline();
    UpdateViewButtons();
}

void MainWindow::OpenDraftFile(const QString& Path)
{
    DocDocument Loaded;
    QString Error;
    if (!Loaded.ReadXml(Path, Error)) {
        QMessageBox::critical(this, tr("Open Draft"),
                              tr("Could not open %1:\n%2").arg(Path, Error));
        return;
    }
    _Document = Loaded;
    _FilePath = Path;
    UpdateProjectDictionary();
    _SelectedKey = QStringLiteral("docset");
    SetModified(false);
    RebuildOutline();
    statusBar()->showMessage(tr("Opened %1.").arg(Path));
    QStringList Notes;
    if (_Document.HasPreservedContent()) {
        Notes.append(tr("This file holds content this version does not edit yet "
                        "(the generated boilerplate, the table of contents, "
                        "or other details, for example). That content is kept "
                        "and will be written back when you save, but it is "
                        "not shown here."));
    }
    if (_Document.HadSkippedContent) {
        Notes.append(tr("A few small details could not be kept and will be "
                        "lost on save."));
    }
    if (!Notes.isEmpty()) {
        QMessageBox::information(this, tr("Open Draft"),
                                 Notes.join(QStringLiteral("\n\n")));
    }
    // A file edited by hand gets checked on the way in: it must be
    // usable before it is edited further.
    DocValidationResult Checked = ValidateAndReport(Path, false);
    if (Checked.Valid) {
        statusBar()->showMessage(tr("Opened %1 (valid).").arg(Path));
    }
    AddRecentFile(Path);
    RebuildRecentMenu();
    UpdateViewButtons();
}

void MainWindow::closeEvent(QCloseEvent* Event)
{
    if (ConfirmDiscard()) {
        Event->accept();
    } else {
        Event->ignore();
    }
}

void MainWindow::OnOutlineSelectionChanged()
{
    ShowDetailsFor(_Outline->currentItem());
}

void MainWindow::OnAddSection()
{
    xmlNodePtr Parent = _Document.MiddleNode();
    int InsertAt = _Document.TopSectionNodes().size();
    QString ParentPath;
    QTreeWidgetItem* Current = _Outline->currentItem();
    if (Current != nullptr && Current->data(0, Qt::UserRole).toInt() == KindSection) {
        QString Path = Current->data(0, Qt::UserRole + 1).toString();
        QVector<xmlNodePtr> Siblings;
        int Index = -1;
        if (SectionSiblings(_Document, Path, Siblings, Index)) {
            InsertAt = Index + 1;
            int Dot = Path.lastIndexOf(QLatin1Char('.'));
            ParentPath = (Dot >= 0) ? Path.left(Dot) : QString();
            xmlNodePtr Selected = Siblings[Index];
            xmlNodePtr Added = DocDocument::NewSectionNode(tr("New Section"));
            xmlAddNextSibling(Selected, Added);
            QString NewPath = ParentPath.isEmpty()
                ? QString::number(InsertAt)
                : ParentPath + QStringLiteral(".") + QString::number(InsertAt);
            _SelectedKey = QStringLiteral("section:") + NewPath;
            SetModified(true);
            RebuildOutline();
            return;
        }
    }
    if (Parent == nullptr) {
        return;
    }
    xmlNodePtr Added = DocDocument::NewSectionNode(tr("New Section"));
    xmlAddChild(Parent, Added);
    QString NewPath = ParentPath.isEmpty()
        ? QString::number(InsertAt)
        : ParentPath + QStringLiteral(".") + QString::number(InsertAt);
    _SelectedKey = QStringLiteral("section:") + NewPath;
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnInsertSection()
{
    QTreeWidgetItem* Current = _Outline->currentItem();
    if (Current == nullptr || Current->data(0, Qt::UserRole).toInt() != KindSection) {
        return;
    }
    QString Path = Current->data(0, Qt::UserRole + 1).toString();
    QVector<xmlNodePtr> Siblings;
    int Index = -1;
    if (!SectionSiblings(_Document, Path, Siblings, Index)) {
        return;
    }
    xmlNodePtr Added = DocDocument::NewSectionNode(tr("New Section"));
    xmlAddPrevSibling(Siblings[Index], Added);
    _SelectedKey = QStringLiteral("section:") + Path;
    SetModified(true);
    RebuildOutline();
}

/**
 * @brief Counts every subsection under a section, at any depth.
 * @param Section The section node (borrowed).
 * @return The number of descendant sections.
 */
static int CountSubsections(xmlNodePtr Section)
{
    QVector<xmlNodePtr> Children = DocDocument::ChildSectionNodes(Section);
    int Count = Children.size();
    for (xmlNodePtr Child : Children) {
        Count += CountSubsections(Child);
    }
    return Count;
}

void MainWindow::OnRemoveSection()
{
    QTreeWidgetItem* Current = _Outline->currentItem();
    if (Current == nullptr || Current->data(0, Qt::UserRole).toInt() != KindSection) {
        return;
    }
    QString Path = Current->data(0, Qt::UserRole + 1).toString();
    QVector<xmlNodePtr> Siblings;
    int Index = -1;
    if (!SectionSiblings(_Document, Path, Siblings, Index)) {
        return;
    }
    // A required section cannot be removed (Bug 422). The button
    // is disabled for one; this guard covers any other path here.
    // Only a top-level section locks: a subsection with a
    // matching title stays removable.
    if (!Path.contains(QLatin1Char('.')) &&
        DocDocument::IsRequiredSectionTitle(
            DocDocument::SectionTitle(Siblings[Index]))) {
        return;
    }
    QString Title = DocDocument::SectionTitle(Siblings[Index]);
    // Removing a section takes its subsections with it, so the
    // confirm names how many go along (Bug 446). The button and
    // the outline menu share this text.
    int SubsectionCount = CountSubsections(Siblings[Index]);
    QString Question;
    if (SubsectionCount == 1) {
        Question = tr("Remove the section \"%1\" and its 1 subsection?")
            .arg(Title);
    } else if (SubsectionCount > 1) {
        Question = tr("Remove the section \"%1\" and its %2 subsections?")
            .arg(Title).arg(SubsectionCount);
    } else {
        Question = tr("Remove the section \"%1\"?").arg(Title);
    }
    QMessageBox::StandardButton Answer = QMessageBox::question(
        this, tr("Remove Section"),
        Question,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (Answer != QMessageBox::Yes) {
        return;
    }
    xmlNodePtr Removed = Siblings[Index];
    int Remaining = Siblings.size() - 1;
    xmlUnlinkNode(Removed);
    xmlFreeNode(Removed);
    // Select a neighbor: the next sibling, the previous one, or the
    // middle group when none is left.
    QString ParentPath;
    int Dot = Path.lastIndexOf(QLatin1Char('.'));
    ParentPath = (Dot >= 0) ? Path.left(Dot) : QString();
    if (Index < Remaining) {
        QString NextPath = ParentPath.isEmpty()
            ? QString::number(Index)
            : ParentPath + QStringLiteral(".") + QString::number(Index);
        _SelectedKey = QStringLiteral("section:") + NextPath;
    } else if (Index > 0) {
        QString PrevPath = ParentPath.isEmpty()
            ? QString::number(Index - 1)
            : ParentPath + QStringLiteral(".") + QString::number(Index - 1);
        _SelectedKey = QStringLiteral("section:") + PrevPath;
    } else {
        _SelectedKey = QStringLiteral("middle");
    }
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnMoveSectionUp()
{
    QTreeWidgetItem* Current = _Outline->currentItem();
    if (Current == nullptr || Current->data(0, Qt::UserRole).toInt() != KindSection) {
        return;
    }
    QString Path = Current->data(0, Qt::UserRole + 1).toString();
    QVector<xmlNodePtr> Siblings;
    int Index = -1;
    if (!SectionSiblings(_Document, Path, Siblings, Index) || Index <= 0) {
        return;
    }
    // No move may cross a required section (Bug 422): a required
    // section never moves, and no free section swaps with one.
    // Only top-level sections lock; a move within a child list
    // never crosses one.
    if (!Path.contains(QLatin1Char('.')) &&
        (DocDocument::IsRequiredSectionTitle(
             DocDocument::SectionTitle(Siblings[Index])) ||
         DocDocument::IsRequiredSectionTitle(
             DocDocument::SectionTitle(Siblings[Index - 1])))) {
        return;
    }
    xmlNodePtr Moved = Siblings[Index];
    xmlUnlinkNode(Moved);
    xmlAddPrevSibling(Siblings[Index - 1], Moved);
    QString ParentPath;
    int Dot = Path.lastIndexOf(QLatin1Char('.'));
    ParentPath = (Dot >= 0) ? Path.left(Dot) : QString();
    QString NewPath = ParentPath.isEmpty()
        ? QString::number(Index - 1)
        : ParentPath + QStringLiteral(".") + QString::number(Index - 1);
    _SelectedKey = QStringLiteral("section:") + NewPath;
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnMoveSectionDown()
{
    QTreeWidgetItem* Current = _Outline->currentItem();
    if (Current == nullptr || Current->data(0, Qt::UserRole).toInt() != KindSection) {
        return;
    }
    QString Path = Current->data(0, Qt::UserRole + 1).toString();
    QVector<xmlNodePtr> Siblings;
    int Index = -1;
    if (!SectionSiblings(_Document, Path, Siblings, Index) ||
        Index < 0 || Index + 1 >= Siblings.size()) {
        return;
    }
    // No move may cross a required section (Bug 422): a required
    // section never moves, and no free section swaps with one.
    // Only top-level sections lock; a move within a child list
    // never crosses one.
    if (!Path.contains(QLatin1Char('.')) &&
        (DocDocument::IsRequiredSectionTitle(
             DocDocument::SectionTitle(Siblings[Index])) ||
         DocDocument::IsRequiredSectionTitle(
             DocDocument::SectionTitle(Siblings[Index + 1])))) {
        return;
    }
    xmlNodePtr Moved = Siblings[Index];
    xmlUnlinkNode(Moved);
    xmlAddNextSibling(Siblings[Index + 1], Moved);
    QString ParentPath;
    int Dot = Path.lastIndexOf(QLatin1Char('.'));
    ParentPath = (Dot >= 0) ? Path.left(Dot) : QString();
    QString NewPath = ParentPath.isEmpty()
        ? QString::number(Index + 1)
        : ParentPath + QStringLiteral(".") + QString::number(Index + 1);
    _SelectedKey = QStringLiteral("section:") + NewPath;
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnOutlineContextMenu(const QPoint& Pos)
{
    // The menu acts on the item under the cursor: select it first,
    // so the details pane follows and the shared slots (Remove,
    // Move Up/Down) read the section that was clicked. Only
    // sections get a menu; a right-click anywhere else in the
    // outline shows nothing (Bug 446).
    QTreeWidgetItem* Item = _Outline->itemAt(Pos);
    if (Item == nullptr || Item->data(0, Qt::UserRole).toInt() != KindSection) {
        return;
    }
    _Outline->setCurrentItem(Item);
    QString Path = Item->data(0, Qt::UserRole + 1).toString();
    QVector<xmlNodePtr> Siblings;
    int Index = -1;
    if (!SectionSiblings(_Document, Path, Siblings, Index)) {
        return;
    }
    // Enablement mirrors RefreshSectionControls: only a top-level
    // section locks, and no move may cross a required section.
    // Keep the two computations in step.
    bool TopLevel = !Path.contains(QLatin1Char('.'));
    bool Required = TopLevel &&
        DocDocument::IsRequiredSectionTitle(
            DocDocument::SectionTitle(Siblings[Index]));
    bool CanMoveUp = (Index > 0);
    bool CanMoveDown = (Index + 1 < Siblings.size());
    if (TopLevel) {
        if (Required) {
            CanMoveUp = false;
            CanMoveDown = false;
        } else {
            if (CanMoveUp &&
                DocDocument::IsRequiredSectionTitle(
                    DocDocument::SectionTitle(Siblings[Index - 1]))) {
                CanMoveUp = false;
            }
            if (CanMoveDown &&
                DocDocument::IsRequiredSectionTitle(
                    DocDocument::SectionTitle(Siblings[Index + 1]))) {
                CanMoveDown = false;
            }
        }
    }
    QMenu Menu(this);
    QAction* AddAction = Menu.addAction(tr("Add Subsection"));
    connect(AddAction, &QAction::triggered, this, &MainWindow::OnAddSubsection);
    QAction* InsertAction = Menu.addAction(tr("Insert Subsection"));
    connect(InsertAction, &QAction::triggered, this, &MainWindow::OnInsertSubsection);
    QAction* RemoveAction = Menu.addAction(tr("Remove Subsection"));
    connect(RemoveAction, &QAction::triggered, this, &MainWindow::OnRemoveSection);
    RemoveAction->setEnabled(!Required);
    QAction* UpAction = Menu.addAction(tr("Move Subsection Up"));
    connect(UpAction, &QAction::triggered, this, &MainWindow::OnMoveSectionUp);
    UpAction->setEnabled(CanMoveUp);
    QAction* DownAction = Menu.addAction(tr("Move Subsection Down"));
    connect(DownAction, &QAction::triggered, this, &MainWindow::OnMoveSectionDown);
    DownAction->setEnabled(CanMoveDown);
    QAction* PromoteAction = Menu.addAction(tr("Promote Subsection"));
    connect(PromoteAction, &QAction::triggered, this, &MainWindow::OnPromoteSubsection);
    PromoteAction->setEnabled(!TopLevel);
    QAction* DemoteAction = Menu.addAction(tr("Demote Subsection"));
    connect(DemoteAction, &QAction::triggered, this, &MainWindow::OnDemoteSubsection);
    // Demote is off for the first section in a list, and for a
    // top-level required section: demoting one would unseat it
    // from the top level, which the Remove and Move guards forbid.
    DemoteAction->setEnabled(Index > 0 && !Required);
    // The position arrives in viewport coordinates, as for the
    // text edit's menu, so it maps through the viewport.
    Menu.exec(_Outline->viewport()->mapToGlobal(Pos));
}

void MainWindow::OnAddSubsection()
{
    QTreeWidgetItem* Current = _Outline->currentItem();
    if (Current == nullptr || Current->data(0, Qt::UserRole).toInt() != KindSection) {
        return;
    }
    QString Path = Current->data(0, Qt::UserRole + 1).toString();
    xmlNodePtr Section = FindSection(Path);
    if (Section == nullptr) {
        return;
    }
    int InsertAt = DocDocument::ChildSectionNodes(Section).size();
    xmlNodePtr Added = DocDocument::NewSectionNode(tr("New Section"));
    xmlAddChild(Section, Added);
    _SelectedKey = QStringLiteral("section:") + Path +
        QStringLiteral(".") + QString::number(InsertAt);
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnInsertSubsection()
{
    QTreeWidgetItem* Current = _Outline->currentItem();
    if (Current == nullptr || Current->data(0, Qt::UserRole).toInt() != KindSection) {
        return;
    }
    QString Path = Current->data(0, Qt::UserRole + 1).toString();
    xmlNodePtr Section = FindSection(Path);
    if (Section == nullptr) {
        return;
    }
    xmlNodePtr Added = DocDocument::NewSectionNode(tr("New Section"));
    QVector<xmlNodePtr> Children = DocDocument::ChildSectionNodes(Section);
    if (!Children.isEmpty()) {
        xmlAddPrevSibling(Children.first(), Added);
    } else {
        xmlAddChild(Section, Added);
    }
    _SelectedKey = QStringLiteral("section:") + Path + QStringLiteral(".0");
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnPromoteSubsection()
{
    QTreeWidgetItem* Current = _Outline->currentItem();
    if (Current == nullptr || Current->data(0, Qt::UserRole).toInt() != KindSection) {
        return;
    }
    QString Path = Current->data(0, Qt::UserRole + 1).toString();
    int Dot = Path.lastIndexOf(QLatin1Char('.'));
    if (Dot < 0) {
        return;
    }
    // The parent's path: the promoted section lands right after
    // its parent, in the parent's own list.
    QString ParentPath = Path.left(Dot);
    QVector<xmlNodePtr> Siblings;
    int Index = -1;
    if (!SectionSiblings(_Document, Path, Siblings, Index)) {
        return;
    }
    xmlNodePtr Parent = FindSection(ParentPath);
    if (Parent == nullptr) {
        return;
    }
    // Unlink first, then re-insert: the section node itself
    // moves, with its children, and is never freed.
    xmlNodePtr Moved = Siblings[Index];
    xmlUnlinkNode(Moved);
    xmlAddNextSibling(Parent, Moved);
    QVector<xmlNodePtr> ParentSiblings;
    int ParentIndex = -1;
    if (!SectionSiblings(_Document, ParentPath, ParentSiblings, ParentIndex)) {
        return;
    }
    int ParentDot = ParentPath.lastIndexOf(QLatin1Char('.'));
    QString GrandPath = (ParentDot >= 0) ? ParentPath.left(ParentDot) : QString();
    QString NewPath = GrandPath.isEmpty()
        ? QString::number(ParentIndex + 1)
        : GrandPath + QStringLiteral(".") + QString::number(ParentIndex + 1);
    _SelectedKey = QStringLiteral("section:") + NewPath;
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnDemoteSubsection()
{
    QTreeWidgetItem* Current = _Outline->currentItem();
    if (Current == nullptr || Current->data(0, Qt::UserRole).toInt() != KindSection) {
        return;
    }
    QString Path = Current->data(0, Qt::UserRole + 1).toString();
    QVector<xmlNodePtr> Siblings;
    int Index = -1;
    if (!SectionSiblings(_Document, Path, Siblings, Index) || Index <= 0) {
        return;
    }
    // A top-level required section cannot be demoted: it would
    // leave the top level, and its presence there is fixed. The
    // menu disables this too; the guard covers any other path.
    if (!Path.contains(QLatin1Char('.')) &&
        DocDocument::IsRequiredSectionTitle(
            DocDocument::SectionTitle(Siblings[Index]))) {
        return;
    }
    // Unlink first, then re-insert: the section node itself
    // moves, with its children, and is never freed.
    xmlNodePtr Moved = Siblings[Index];
    xmlUnlinkNode(Moved);
    // The previous sibling sits at Index - 1 after the removal;
    // the demoted section becomes its last child.
    xmlNodePtr Previous = Siblings[Index - 1];
    int ChildAt = DocDocument::ChildSectionNodes(Previous).size();
    xmlAddChild(Previous, Moved);
    int Dot = Path.lastIndexOf(QLatin1Char('.'));
    QString ParentPath = (Dot >= 0) ? Path.left(Dot) : QString();
    QString PrevPath = ParentPath.isEmpty()
        ? QString::number(Index - 1)
        : ParentPath + QStringLiteral(".") + QString::number(Index - 1);
    _SelectedKey = QStringLiteral("section:") + PrevPath +
        QStringLiteral(".") + QString::number(ChildAt);
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnAddAuthor()
{
    // Ask for the name first: an author with no name is no use.
    bool Ok = false;
    QString Name = QInputDialog::getText(this, tr("Add Author"),
                                         tr("Full name:"), QLineEdit::Normal,
                                         QString(), &Ok);
    while (Ok && Name.trimmed().isEmpty()) {
        Name = QInputDialog::getText(this, tr("Add Author"),
                                     tr("Full name:"), QLineEdit::Normal,
                                     QString(), &Ok);
    }
    if (!Ok) {
        return;
    }
    _Document.AddAuthorNode(
        DocDocument::AuthorNodeFromFullname(Name, QString(), QString()));
    _SelectedKey = QStringLiteral("author:") +
        QString::number(_Document.AuthorNodes().size() - 1);
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnRemoveAuthor()
{
    xmlNodePtr Author = ShownAuthor();
    if (Author == nullptr) {
        return;
    }
    if (_AuthorGroupPath.isEmpty() && _Document.AuthorNodes().size() == 1) {
        QMessageBox::warning(this, tr("Remove Author"),
                             tr("A draft needs at least one author."));
        return;
    }
    QString Name = DocDocument::AuthorAttribute(Author, "fullname");
    QMessageBox::StandardButton Answer = QMessageBox::question(
        this, tr("Remove Author"),
        tr("Remove the author \"%1\"?").arg(Name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (Answer != QMessageBox::Yes) {
        return;
    }
    if (_AuthorGroupPath.isEmpty()) {
        _Document.RemoveAuthorNode(_AuthorIndex);
        _SelectedKey = QStringLiteral("authors");
    } else {
        xmlNodePtr Entry = FindReferenceEntry(_AuthorGroupPath, _AuthorEntry);
        if (Entry == nullptr) {
            return;
        }
        QVector<xmlNodePtr> Authors = DocDocument::ReferenceAuthors(Entry);
        if (_AuthorIndex < 0 || _AuthorIndex >= Authors.size()) {
            return;
        }
        xmlNodePtr Removed = Authors[_AuthorIndex];
        xmlUnlinkNode(Removed);
        xmlFreeNode(Removed);
        _SelectedKey = QStringLiteral("refentry:") + _AuthorGroupPath +
            QStringLiteral(".") + QString::number(_AuthorEntry);
    }
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnNewDraft()
{
    if (!ConfirmDiscard()) {
        return;
    }
    StartWizard Wizard(this, true);
    if (Wizard.exec() != QDialog::Accepted) {
        return;
    }
    SetDocument(Wizard.GetDocument(), Wizard.GetDocumentFilePath());
    statusBar()->showMessage(tr("New draft: %1.").arg(_Document.DocName()));
}

void MainWindow::OnOpenDraft()
{
    if (!ConfirmDiscard()) {
        return;
    }
    QString File = QFileDialog::getOpenFileName(
        this, tr("Open Draft"), QDir::homePath(),
        tr("Draft files (*.xml);;All files (*)"));
    if (File.isEmpty()) {
        return;
    }
    OpenDraftFile(File);
}

void MainWindow::OnOpenRecentFile()
{
    QAction* Action = qobject_cast<QAction*>(sender());
    if (Action == nullptr) {
        return;
    }
    QString Path = Action->data().toString();
    if (Path.isEmpty() || !QFile::exists(Path)) {
        statusBar()->showMessage(tr("That file is no longer there."));
        RebuildRecentMenu();
        return;
    }
    if (!ConfirmDiscard()) {
        return;
    }
    OpenDraftFile(Path);
}

void MainWindow::RebuildRecentMenu()
{
    for (QAction* Action : _RecentActions) {
        _FileMenu->removeAction(Action);
        delete Action;
    }
    _RecentActions.clear();
    QStringList Stored = RecentFiles();
    QStringList Files;
    for (const QString& Path : Stored) {
        if (QFile::exists(Path)) {
            Files.append(Path);
        }
    }
    if (Files.size() != Stored.size()) {
        QSettings Settings;
        Settings.setValue(QStringLiteral("recent/files"), Files);
    }
    if (Files.isEmpty()) {
        return;
    }
    _RecentActions.append(_FileMenu->insertSeparator(_SaveAction));
    for (const QString& Path : Files) {
        QAction* Action = new QAction(QFileInfo(Path).fileName(), this);
        Action->setData(Path);
        Action->setStatusTip(Path);
        connect(Action, &QAction::triggered,
                this, &MainWindow::OnOpenRecentFile);
        _FileMenu->insertAction(_SaveAction, Action);
        _RecentActions.append(Action);
    }
    _RecentActions.append(_FileMenu->insertSeparator(_SaveAction));
}

void MainWindow::OnSave()
{
    if (_FilePath.isEmpty()) {
        OnSaveAs();
        return;
    }
    SaveToFile(_FilePath);
}

void MainWindow::OnSaveAs()
{
    QString DefaultPath = _FilePath;
    if (DefaultPath.isEmpty()) {
        DefaultPath = QDir(QDir::homePath()).filePath(
            _Document.DocName() + QStringLiteral(".xml"));
    }
    QString File = QFileDialog::getSaveFileName(
        this, tr("Save Draft"), DefaultPath,
        tr("Draft files (*.xml);;All files (*)"));
    if (File.isEmpty()) {
        return;
    }
    if (SaveToFile(File)) {
        _FilePath = File;
        UpdateProjectDictionary();
        UpdateWindowTitle();
    }
}

/**
 * @brief True when the saved draft pulls in an external file
 *        through a src attribute (Bug 465).
 *
 * xml2rfc reads src files only with --allow-local-file-access
 * (since 3.27.0). The saved file is scanned, so the check
 * covers modeled and preserved content alike, and checks
 * exactly what xml2rfc is about to read. A src mentioned
 * inside a comment also matches, which only passes the flag.
 * @param FilePath The saved draft file.
 * @return True when a sourcecode or artwork element carries src.
 */
static bool SavedFileUsesSrc(const QString& FilePath)
{
    QFile File(FilePath);
    if (!File.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream Stream(&File);
    QString Text = Stream.readAll();
    static const QRegularExpression Pattern(
        QStringLiteral("<(sourcecode|artwork)[^>]*\\ssrc\\s*="));
    return Pattern.match(Text).hasMatch();
}

void MainWindow::OnRunXml2rfc()
{
    if (_RunProcess->state() == QProcess::Running) {
        statusBar()->showMessage(tr("xml2rfc is already running."));
        return;
    }
    // One run produces every checked format (Bug 320). With
    // nothing checked there is nothing to run, so ask before
    // asking about unsaved changes.
    QStringList FormatArgs;
    if (_TextFormatCheck->isChecked()) {
        FormatArgs << QStringLiteral("--text");
    }
    if (_PdfFormatCheck->isChecked()) {
        FormatArgs << QStringLiteral("--pdf");
    }
    if (_HtmlFormatCheck->isChecked()) {
        FormatArgs << QStringLiteral("--html");
    }
    if (FormatArgs.isEmpty()) {
        QMessageBox::information(this, tr("Run xml2rfc"),
                                 tr("Check at least one output format: "
                                    "Text, PDF, or HTML."));
        return;
    }
    if (_Modified) {
        QMessageBox::StandardButton Answer = QMessageBox::question(
            this, tr("Run xml2rfc"),
            tr("Save the draft before running xml2rfc?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (Answer == QMessageBox::Cancel) {
            return;
        }
        if (Answer == QMessageBox::Save) {
            OnSave();
            if (_Modified) {
                return;
            }
        }
    }
    if (_FilePath.isEmpty()) {
        statusBar()->showMessage(tr("Save the draft first."));
        return;
    }
    _RunFailed = false;
    _RunLog->clear();
    _RunCloseButton->setEnabled(false);
    _RunDialog->show();
    _RunDialog->raise();
    QString Program = Xml2rfcPath();
    // A draft that pulls in an external file through src needs
    // xml2rfc's local-file access (Bug 465); the flag is passed
    // only when the saved file actually uses src, so runs that
    // need no local files stay restricted. It joins the
    // arguments before the log line, so the log shows the full
    // command.
    if (SavedFileUsesSrc(_FilePath)) {
        FormatArgs << QStringLiteral("--allow-local-file-access");
    }
    AppendRunLog(tr("Running: %1 %2 %3\n")
                 .arg(Program, FormatArgs.join(QLatin1Char(' ')), _FilePath));
    _RunProcess->setWorkingDirectory(QFileInfo(_FilePath).absolutePath());
    FormatArgs << _FilePath;
    _RunProcess->start(Program, FormatArgs);
}

void MainWindow::OnFormatChoiceChanged()
{
    QSettings Settings;
    Settings.setValue(QStringLiteral("xml2rfc/text"),
                      _TextFormatCheck->isChecked());
    Settings.setValue(QStringLiteral("xml2rfc/pdf"),
                      _PdfFormatCheck->isChecked());
    Settings.setValue(QStringLiteral("xml2rfc/html"),
                      _HtmlFormatCheck->isChecked());
    UpdateViewButtons();
}

QString MainWindow::OutputFilePath(const QString& Extension) const
{
    if (_FilePath.isEmpty()) {
        return QString();
    }
    QFileInfo Info(_FilePath);
    return QDir(Info.absolutePath()).filePath(
        Info.completeBaseName() + QLatin1Char('.') + Extension);
}

void MainWindow::UpdateViewButtons()
{
    _TextViewButton->setEnabled(
        _TextFormatCheck->isChecked() &&
        QFile::exists(OutputFilePath(QStringLiteral("txt"))));
    _PdfViewButton->setEnabled(
        _PdfFormatCheck->isChecked() &&
        QFile::exists(OutputFilePath(QStringLiteral("pdf"))));
    _HtmlViewButton->setEnabled(
        _HtmlFormatCheck->isChecked() &&
        QFile::exists(OutputFilePath(QStringLiteral("html"))));
}

void MainWindow::ViewOutput(const QString& Extension)
{
    QString Path = OutputFilePath(Extension);
    if (Path.isEmpty() || !QFile::exists(Path)) {
        return;
    }
    if (Extension == QLatin1String("txt")) {
        if (!QProcess::startDetached(QStringLiteral("notepad"),
                                     QStringList() << Path)) {
            QMessageBox::warning(this, tr("View"),
                                 tr("Could not open Notepad."));
        }
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(Path))) {
        QMessageBox::warning(this, tr("View"),
            Extension == QLatin1String("pdf")
                ? tr("No default PDF viewer is set for this system.")
                : tr("Could not open the default browser."));
    }
}

void MainWindow::OnViewText()
{
    ViewOutput(QStringLiteral("txt"));
}

void MainWindow::OnViewPdf()
{
    ViewOutput(QStringLiteral("pdf"));
}

void MainWindow::OnViewHtml()
{
    ViewOutput(QStringLiteral("html"));
}

void MainWindow::OnSetXml2rfcPath()
{
    // Start where the current program lives, or at home when the
    // current value is only a program name found on the PATH.
    QString StartDir = QDir::homePath();
    QFileInfo CurrentInfo(Xml2rfcPath());
    if (CurrentInfo.isAbsolute()) {
        StartDir = CurrentInfo.absolutePath();
    }
    QString Path = QFileDialog::getOpenFileName(
        this, tr("xml2rfc Program"), StartDir,
        tr("Programs (*.exe *.bat *.cmd);;All files (*)"));
    if (Path.isEmpty()) {
        return;
    }
    QSettings Settings;
    Settings.setValue(QStringLiteral("xml2rfc/path"), Path);
    statusBar()->showMessage(tr("xml2rfc path set to %1.").arg(Path));
}

void MainWindow::OnBibliography()
{
    QDialog Dialog(this);
    Dialog.setWindowTitle(tr("Bibliography"));
    Dialog.resize(680, 420);

    QLabel* LocationLabel = new QLabel(tr("Cache location:"), &Dialog);
    QLineEdit* LocationEdit = new QLineEdit(_BibCache.Location(), &Dialog);
    LocationEdit->setReadOnly(true);
    QPushButton* BrowseButton = new QPushButton(tr("Browse..."), &Dialog);
    QHBoxLayout* LocationLayout = new QHBoxLayout();
    LocationLayout->addWidget(LocationLabel);
    LocationLayout->addWidget(LocationEdit, 1);
    LocationLayout->addWidget(BrowseButton);

    QTableWidget* Table = new QTableWidget(&Dialog);
    Table->setColumnCount(4);
    Table->setHorizontalHeaderLabels(QStringList()
        << tr("Reference") << tr("Source") << tr("Fetched") << tr("Size"));
    Table->setSelectionBehavior(QAbstractItemView::SelectRows);
    Table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    Table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    Table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    Table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    Table->verticalHeader()->setVisible(false);

    // Fills the table from the cache index. The address rides on
    // the first column's item, so the buttons can find it again.
    auto FillTable = [&]() {
        Table->setRowCount(0);
        QVector<BibCacheEntry> Entries = _BibCache.Entries();
        for (const BibCacheEntry& Entry : Entries) {
            int Row = Table->rowCount();
            Table->insertRow(Row);
            QString Reference = Entry.Anchor;
            if (!Entry.Title.isEmpty()) {
                Reference = tr("%1 (%2)").arg(Entry.Title, Entry.Anchor);
            }
            QTableWidgetItem* ReferenceItem =
                new QTableWidgetItem(Reference);
            ReferenceItem->setData(Qt::UserRole, Entry.Url);
            ReferenceItem->setToolTip(Entry.Url);
            Table->setItem(Row, 0, ReferenceItem);
            Table->setItem(Row, 1, new QTableWidgetItem(Entry.Url));
            Table->setItem(Row, 2, new QTableWidgetItem(Entry.Fetched));
            Table->setItem(Row, 3, new QTableWidgetItem(
                tr("%1 bytes").arg(QString::number(Entry.Size))));
        }
    };
    FillTable();

    QLabel* StatusLabel = new QLabel(&Dialog);
    StatusLabel->setWordWrap(true);

    QPushButton* RefreshButton = new QPushButton(tr("Refresh"), &Dialog);
    RefreshButton->setToolTip(
        tr("Fetch the selected entries again. Only matters for "
           "Internet-Drafts; an RFC never changes."));
    QPushButton* DeleteButton = new QPushButton(tr("Delete"), &Dialog);
    DeleteButton->setToolTip(tr("Delete the selected entries."));
    QPushButton* ClearButton = new QPushButton(tr("Clear All"), &Dialog);
    ClearButton->setToolTip(tr("Delete every cached entry."));
    QPushButton* CloseButton = new QPushButton(tr("Close"), &Dialog);
    QHBoxLayout* ButtonLayout = new QHBoxLayout();
    ButtonLayout->addWidget(RefreshButton);
    ButtonLayout->addWidget(DeleteButton);
    ButtonLayout->addWidget(ClearButton);
    ButtonLayout->addStretch(1);
    ButtonLayout->addWidget(CloseButton);

    QVBoxLayout* Layout = new QVBoxLayout(&Dialog);
    Layout->addLayout(LocationLayout);
    Layout->addWidget(Table, 1);
    Layout->addWidget(StatusLabel);
    Layout->addLayout(ButtonLayout);

    auto SelectedUrls = [&]() {
        QStringList Urls;
        for (QTableWidgetItem* Item : Table->selectedItems()) {
            if (Item->column() != 0) {
                continue;
            }
            Urls.append(Item->data(Qt::UserRole).toString());
        }
        return Urls;
    };

    connect(BrowseButton, &QPushButton::clicked, &Dialog, [&]() {
        QString Current = _BibCache.Location();
        QString NewPath = QFileDialog::getExistingDirectory(
            &Dialog, tr("Bibliography cache"), Current);
        if (NewPath.isEmpty() ||
            QDir::cleanPath(NewPath) == QDir::cleanPath(Current)) {
            return;
        }
        QMessageBox Box(&Dialog);
        Box.setWindowTitle(tr("Move bibliography cache"));
        Box.setText(tr("Move the cached entries to %1, or delete them?")
            .arg(NewPath));
        Box.setInformativeText(
            tr("Copy keeps the cached entries at the new location. "
               "Delete throws them away; they can be fetched again later."));
        QPushButton* CopyButton =
            Box.addButton(tr("Copy"), QMessageBox::AcceptRole);
        Box.addButton(tr("Delete"), QMessageBox::DestructiveRole);
        Box.setDefaultButton(CopyButton);
        Box.exec();
        if (Box.clickedButton() == nullptr) {
            return;
        }
        bool CopyOld = (Box.clickedButton() == CopyButton);
        if (!_BibCache.MoveLocation(NewPath, CopyOld)) {
            QMessageBox::warning(
                &Dialog, tr("Bibliography"),
                tr("Could not move the cache: %1").arg(_BibCache.LastError()));
            return;
        }
        LocationEdit->setText(_BibCache.Location());
        FillTable();
        StatusLabel->setText(CopyOld
            ? tr("The cached entries moved to the new location.")
            : tr("The cached entries were deleted."));
    });

    connect(RefreshButton, &QPushButton::clicked, &Dialog, [&]() {
        QStringList Urls = SelectedUrls();
        if (Urls.isEmpty()) {
            return;
        }
        StatusLabel->setText(tr("Fetching the selected entries..."));
        for (const QString& Url : Urls) {
            _BibCache.RefreshEntry(Url);
        }
    });
    connect(DeleteButton, &QPushButton::clicked, &Dialog, [&]() {
        for (const QString& Url : SelectedUrls()) {
            _BibCache.DeleteEntry(Url);
        }
        FillTable();
    });
    connect(ClearButton, &QPushButton::clicked, &Dialog, [&]() {
        QMessageBox::StandardButton Answer = QMessageBox::question(
            &Dialog, tr("Bibliography"),
            tr("Delete every cached entry? "
               "They can be fetched again later."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (Answer != QMessageBox::Yes) {
            return;
        }
        _BibCache.ClearAll();
        FillTable();
    });
    connect(CloseButton, &QPushButton::clicked, &Dialog, &QDialog::accept);

    // A finished download refreshes the list; a failed one is named.
    connect(&_BibCache, &BibCache::FetchFinished, &Dialog, [&]() {
        FillTable();
        QMap<QString, QString> Failed = _BibCache.FailedFetches();
        if (Failed.isEmpty()) {
            StatusLabel->setText(tr("The selected entries are up to date."));
        } else {
            QStringList Notes;
            for (auto It = Failed.constBegin(); It != Failed.constEnd();
                 ++It) {
                Notes.append(tr("Could not fetch %1: %2")
                    .arg(It.key(), It.value()));
            }
            StatusLabel->setText(Notes.join(QStringLiteral("\n")));
        }
    });
    connect(&_BibCache, &BibCache::EntriesChanged, &Dialog, [&]() {
        FillTable();
    });

    Dialog.exec();
}

void MainWindow::OnLegend()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("Legend"));
    box.setText(
        tr("Special characters in the text editor:\n\n"
           "⏎  Line Break — a break in the text, shown as a symbol "
           "so it cannot be mistaken for typed text.\n\n"
           "↕2  Vertical Space — the number is how many blank lines "
           "it inserts.\n\n"
           "Item text  Index Entry — shows the index item, "
           "link-styled; � when the item is empty.\n\n"
           "�  Element with no text — a cross-reference, external "
           "link, or other element carrying no words of its own; "
           "hover for what it points to.\n\n"
           "<!-- -->  Comment — an XML comment, shown in italics; "
           "the text between the markers is the comment.\n\n"
           "Soft line break (Shift+Enter) — breaks the line without "
           "starting a new paragraph; it becomes a space when the "
           "file is saved.\n\n"
           "List items show a bullet, number, or letter, according "
           "to the list's style.\n\n"
           "Source code shows in a monospace font, indented. "
           "Enter adds a line to the code; the Blocks button "
           "starts and stops the block."));
    box.addButton(QMessageBox::Ok);
    box.exec();
}

void MainWindow::OnXml2rfcLegend()
{
    LegendDialog Dialog(this);
    Dialog.exec();
}

void MainWindow::OnAbout()
{
    // Same shape as the CoOp editor's About box: version, what
    // the tool is, copyright, and the MIT license text.
    QMessageBox box(this);
    box.setWindowTitle(tr("About ProtocolDocEditor"));
    box.setText(
        tr("ProtocolDocEditor %1\n"
           "Built %2 %3\n\n"
           "An editor for IETF Internet-Drafts in RFCXML (xml2rfc) format.\n\n"
           "Copyright (c) 2026 RiverExplorer LLC\n"
           "RiverExplorer%4 is a registered trademark of "
           "Douglas Mark Royer.\n\n"
           "MIT License — free to use and modify, keep the copyright "
           "notice.\n\n"
           "Permission is hereby granted, free of charge, to any person "
           "obtaining a copy of this software and associated documentation "
           "files (the \"Software\"), to deal in the Software without "
           "restriction, including without limitation the rights to use, "
           "copy, modify, merge, publish, distribute, sublicense, and/or "
           "sell copies of the Software, and to permit persons to whom the "
           "Software is furnished to do so, subject to the following "
           "conditions:\n\n"
           "The above copyright notice and this permission notice shall be "
           "included in all copies or substantial portions of the "
           "Software.\n\n"
           "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY "
           "KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE "
           "WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE "
           "AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT "
           "HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, "
           "WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING "
           "FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR "
           "OTHER DEALINGS IN THE SOFTWARE.")
            .arg(QCoreApplication::applicationVersion())
            .arg(QStringLiteral(__DATE__))
            .arg(QStringLiteral(__TIME__))
            .arg(QString(QChar(0x00AE))));
    box.addButton(QMessageBox::Ok);
    box.exec();
}

void MainWindow::OnValidate()
{
    if (_Modified) {
        QMessageBox::StandardButton Answer = QMessageBox::question(
            this, tr("Validate"),
            tr("Save the draft before validating?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (Answer == QMessageBox::Cancel) {
            return;
        }
        if (Answer == QMessageBox::Save) {
            OnSave();
            if (_Modified) {
                return;
            }
        }
    }
    if (_FilePath.isEmpty()) {
        statusBar()->showMessage(tr("Save the draft first."));
        return;
    }
    // Included references come from the bibliography cache: fetch
    // what is missing first, then validate. The report leaves out
    // the errors a resolved include explains.
    QStringList Hrefs;
    for (xmlNodePtr Include :
         DocDocument::DocumentIncludeNodes(_Document.RfcNode())) {
        QString Href = DocDocument::NodeAttribute(Include, "href");
        if (!Href.isEmpty() && !Hrefs.contains(Href)) {
            Hrefs.append(Href);
        }
    }
    if (Hrefs.isEmpty()) {
        ValidateAndReport(_FilePath, true);
        return;
    }
    statusBar()->showMessage(tr("Fetching bibliography entries..."));
    connect(&_BibCache, &BibCache::FetchFinished, this,
            [this]() { ValidateAndReport(_FilePath, true); },
            Qt::SingleShotConnection);
    _BibCache.EnsureCached(Hrefs);
}

/**
 * @brief The folder holding the installed v3 schemas.
 *
 * The schemas live under the user's roaming profile, shared by the
 * CoOp tools: %APPDATA%\\RiverExplorer\\CoOp\\Schemas on Windows.
 * GenericConfigLocation is used, not AppDataLocation: the folder
 * is shared under CoOp, not per application.
 * @return The schemas folder path.
 */
static QString SchemasDir()
{
    QString Config = QStandardPaths::writableLocation(
        QStandardPaths::GenericConfigLocation);
    return QDir(Config).filePath(QStringLiteral("RiverExplorer/CoOp/Schemas"));
}

/**
 * @brief The folder holding the spell checker dictionaries.
 *
 * The en_US dictionary and the personal dictionary live here,
 * in the user's roaming profile, beside the schemas folder.
 * @return The dictionaries folder path.
 */
static QString DictionariesDir()
{
    QString Config = QStandardPaths::writableLocation(
        QStandardPaths::GenericConfigLocation);
    return QDir(Config).filePath(QStringLiteral("RiverExplorer/CoOp/Dictionaries"));
}

/**
 * @brief Installs the en_US dictionary into the user's roaming profile.
 *
 * The dictionary files ride inside the program as Qt resources,
 * so spell checking works with nothing beside the exe. On
 * startup each file is copied to the dictionaries folder when
 * it is missing or differs from the embedded copy, so a newer
 * program refreshes the installed dictionary. A failed copy is
 * silent: spell checking then falls back to a dictionaries
 * folder beside the program, and stays off when neither exists.
 * The personal dictionary (user.dic) is never touched here: it
 * holds the writer's own words and is created on first use.
 */
static void EnsureDictionariesInstalled()
{
    static const char* Names[] = {
        "en_US.dic",
        "en_US.aff",
    };
    QDir Dir(DictionariesDir());
    if (!Dir.exists() && !QDir().mkpath(Dir.path())) {
        return;
    }
    for (const char* Name : Names) {
        QString FileName = QString::fromLatin1(Name);
        QFile Embedded(QStringLiteral(":/dictionaries/") + FileName);
        if (!Embedded.open(QIODevice::ReadOnly)) {
            continue;
        }
        QByteArray Wanted = Embedded.readAll();
        Embedded.close();
        QString Target = Dir.filePath(FileName);
        QFile Existing(Target);
        if (Existing.open(QIODevice::ReadOnly)) {
            bool Same = Existing.readAll() == Wanted;
            Existing.close();
            if (Same) {
                continue;
            }
        }
        QFile Out(Target);
        if (Out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            Out.write(Wanted);
        }
    }
}

/**
 * @brief Installs the v3 schemas into the user's roaming profile.
 *
 * The schema files ride inside the program as Qt resources, so
 * validation works with nothing beside the exe. On startup each
 * file is copied to the schemas folder when it is missing or
 * differs from the embedded copy, so a newer program refreshes
 * the installed schemas. A failed copy is silent: validation
 * then falls back to a schemas folder beside the program, and
 * reports the schema as not found when neither exists.
 */
static void EnsureSchemasInstalled()
{
    static const char* Names[] = {
        "v2.rng",
        "rfc7991.rng",
        "referencegroup.rng",
        "reference.rng",
        "SVG-1.2-RFC.rng",
        "v3.rng",
    };
    QDir Dir(SchemasDir());
    if (!Dir.exists() && !QDir().mkpath(Dir.path())) {
        return;
    }
    for (const char* Name : Names) {
        QString FileName = QString::fromLatin1(Name);
        QFile Embedded(QStringLiteral(":/schemas/") + FileName);
        if (!Embedded.open(QIODevice::ReadOnly)) {
            continue;
        }
        QByteArray Wanted = Embedded.readAll();
        Embedded.close();
        QString Target = Dir.filePath(FileName);
        QFile Existing(Target);
        if (Existing.open(QIODevice::ReadOnly)) {
            bool Same = Existing.readAll() == Wanted;
            Existing.close();
            if (Same) {
                continue;
            }
        }
        QFile Out(Target);
        if (Out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            Out.write(Wanted);
        }
    }
}

QString MainWindow::FindSchemaPath() const
{
    QString Installed = QDir(SchemasDir()).filePath(QStringLiteral("v3.rng"));
    if (QFile::exists(Installed)) {
        return Installed;
    }
    QDir AppDir(QCoreApplication::applicationDirPath());
    QString InSchemas = AppDir.filePath(QStringLiteral("schemas/v3.rng"));
    if (QFile::exists(InSchemas)) {
        return InSchemas;
    }
    QString Beside = AppDir.filePath(QStringLiteral("v3.rng"));
    if (QFile::exists(Beside)) {
        return Beside;
    }
    return QString();
}

DocValidationResult MainWindow::ValidateAndReport(const QString& Path, bool AlwaysShow)
{
    DocValidationResult Result;
    Result.Valid = false;
    Result.Checked = false;
    QString Schema = FindSchemaPath();
    int ResolvedIncludes = 0;
    if (Schema.isEmpty()) {
        Result.Errors.append(
            tr("The v3 schema (schemas/v3.rng) was not found next to the program. "
               "Put the schemas folder beside ProtocolDocEditor.exe."));
        statusBar()->showMessage(tr("Validation skipped: the v3 schema was not found."));
    } else {
        Result = ValidateRfcXml(Path, Schema);
        // The filter only runs when the open document is the file
        // being checked: its include lines must match the file's.
        if (Path == _FilePath && !_Modified) {
            ResolvedIncludes = FilterIncludeErrors(Result);
        }
        if (Result.Valid) {
            statusBar()->showMessage(
                tr("Validated %1: the file matches the v3 schema.").arg(Path));
        } else {
            statusBar()->showMessage(
                tr("Validation found problems in %1.").arg(Path));
        }
    }
    if (AlwaysShow || (Result.Checked && !Result.Valid)) {
        QString Report = tr("Validating %1\n").arg(Path);
        if (!Schema.isEmpty()) {
            Report += tr("Schema: %1\n").arg(Schema);
        }
        if (ResolvedIncludes == 1) {
            Report += tr("Bibliography: 1 included reference resolved "
                         "from the cache.\n");
        } else if (ResolvedIncludes > 1) {
            Report += tr("Bibliography: %1 included references resolved "
                         "from the cache.\n").arg(ResolvedIncludes);
        }
        Report += QStringLiteral("\n");
        if (Result.Valid) {
            Report += tr("The file matches the v3 schema.\n");
        } else {
            Report += Result.Errors.join(QStringLiteral("\n"));
            Report += QStringLiteral("\n");
        }
        _ValidateLog->setPlainText(Report);
        _ValidateDialog->show();
        _ValidateDialog->raise();
    }
    return Result;
}

int MainWindow::FilterIncludeErrors(DocValidationResult& Result)
{
    QVector<xmlNodePtr> Includes =
        DocDocument::DocumentIncludeNodes(_Document.RfcNode());
    if (Includes.isEmpty()) {
        return 0;
    }
    // Which includes the cache resolved, and the anchor each carries.
    QMap<QString, QString> Resolved;
    QVector<int> ResolvedLines;
    QStringList UnresolvedHrefs;
    for (xmlNodePtr Include : Includes) {
        QString Href = DocDocument::NodeAttribute(Include, "href");
        if (Href.isEmpty()) {
            continue;
        }
        if (_BibCache.IsCached(Href)) {
            Resolved[Href] =
                DocDocument::ReferenceAnchorFromIncludeHref(Href);
            ResolvedLines.append(static_cast<int>(Include->line));
        } else {
            UnresolvedHrefs.append(Href);
        }
    }
    if (!Resolved.isEmpty()) {
        // The report lines carry "Line N: " (Bug 485), so an include
        // error is matched to its include by line. When a line was
        // not recorded, include errors are paired with includes in
        // file order instead: validation reports them in that order.
        QRegularExpression IncludeError(QStringLiteral(
            "^Line (\\d+): Did not expect element include there$"));
        QRegularExpression IdrefError(QStringLiteral(
            "IDREF attribute target references an unknown ID \"([^\"]+)\""));
        QStringList Kept;
        int IncludeErrorIndex = 0;
        for (const QString& Error : Result.Errors) {
            bool Drop = false;
            QRegularExpressionMatch IncludeMatch = IncludeError.match(Error);
            if (IncludeMatch.hasMatch()) {
                int Line = IncludeMatch.captured(1).toInt();
                Drop = ResolvedLines.contains(Line);
                if (!Drop && IncludeErrorIndex < Includes.size()) {
                    QString Href = DocDocument::NodeAttribute(
                        Includes[IncludeErrorIndex], "href");
                    Drop = Resolved.contains(Href);
                }
                ++IncludeErrorIndex;
            } else {
                QRegularExpressionMatch IdrefMatch = IdrefError.match(Error);
                if (IdrefMatch.hasMatch()) {
                    Drop = Resolved.values().contains(
                        IdrefMatch.captured(1));
                }
            }
            if (!Drop) {
                Kept.append(Error);
            }
        }
        Result.Errors = Kept;
        if (Result.Errors.isEmpty()) {
            Result.Valid = true;
        }
    }
    // A download that failed gets a plain note, so a reference that
    // was not checked is never mistaken for a clean file.
    for (const QString& Href : UnresolvedHrefs) {
        QString Failure = _BibCache.FailedError(Href);
        if (!Failure.isEmpty()) {
            Result.Errors.append(
                tr("Could not fetch %1: %2").arg(Href, Failure));
        }
    }
    return Resolved.size();
}

void MainWindow::OnRunOutput()
{
    AppendRunLog(QString::fromUtf8(_RunProcess->readAllStandardOutput()));
}

void MainWindow::OnRunErrorOutput()
{
    AppendRunLog(QString::fromUtf8(_RunProcess->readAllStandardError()));
}

void MainWindow::OnRunFinished(int ExitCode, QProcess::ExitStatus /*ExitStatus*/)
{
    _RunCloseButton->setEnabled(true);
    // A run may have produced the outputs the View buttons open.
    UpdateViewButtons();
    if (_RunFailed) {
        return;
    }
    AppendRunLog(tr("\nxml2rfc finished with exit code %1.\n").arg(ExitCode));
    if (ExitCode == 0) {
        QFileInfo Info(_FilePath);
        QString TextPath = QDir(Info.absolutePath()).filePath(
            Info.completeBaseName() + QStringLiteral(".txt"));
        if (QFile::exists(TextPath)) {
            AppendRunLog(tr("Wrote %1.\n").arg(TextPath));
            statusBar()->showMessage(tr("xml2rfc wrote %1.").arg(TextPath));
        } else {
            AppendRunLog(tr("No .txt file was produced.\n"));
            statusBar()->showMessage(tr("xml2rfc finished, but no .txt file was produced."));
        }
    } else {
        statusBar()->showMessage(tr("xml2rfc failed with exit code %1.").arg(ExitCode));
    }
}

void MainWindow::OnRunFailed(QProcess::ProcessError Error)
{
    if (Error != QProcess::FailedToStart) {
        AppendRunLog(_RunProcess->errorString() + QStringLiteral("\n"));
        return;
    }
    _RunFailed = true;
    _RunCloseButton->setEnabled(true);
    AppendRunLog(tr("Could not start xml2rfc: %1\n"
                    "Check the xml2rfc path (Settings > Set xml2rfc Path...).\n")
        .arg(_RunProcess->errorString()));
    statusBar()->showMessage(tr("Could not start xml2rfc."));
}

void MainWindow::OnSettingsTitleEdited(const QString& Text)
{
    _Document.SetTitle(Text, _Document.TitleBreaks());
    SetModified(true);
}

void MainWindow::OnSettingsTitleDetailsEdited()
{
    _Document.SetTitleAbbrev(_SettingsTitleAbbrevEdit->text());
    _Document.SetTitleAscii(_SettingsTitleAsciiEdit->text());
    SetModified(true);
}

void MainWindow::OnSettingsListsEdited()
{
    _Document.SetAreas(LinesFromPlainText(_SettingsAreasEdit->toPlainText()));
    _Document.SetWorkgroups(
        LinesFromPlainText(_SettingsWorkgroupsEdit->toPlainText()));
    _Document.SetKeywords(
        LinesFromPlainText(_SettingsKeywordsEdit->toPlainText()));
    SetModified(true);
}

void MainWindow::OnAddDocumentSeriesInfo()
{
    _Document.AddSeriesInfoNode();
    SetModified(true);
    FillDocumentSettingsForm();
    int Row = _SettingsSeriesTable->rowCount() - 1;
    if (Row >= 0) {
        _SettingsSeriesTable->setCurrentCell(Row, 0);
        QTableWidgetItem* Cell = _SettingsSeriesTable->item(Row, 0);
        if (Cell != nullptr) {
            _SettingsSeriesTable->editItem(Cell);
        }
    }
}

void MainWindow::OnRemoveDocumentSeriesInfo()
{
    int Row = _SettingsSeriesTable->currentRow();
    if (Row < 0 || Row >= _Document.SeriesInfoNodes().size()) {
        return;
    }
    _Document.RemoveSeriesInfoNode(Row);
    SetModified(true);
    FillDocumentSettingsForm();
}

void MainWindow::OnDocumentSeriesChanged()
{
    // Rows map one-to-one onto the series info; editing a cell
    // keeps the row's kept attributes. A row naming
    // Internet-Draft takes its value from the draft name.
    QVector<xmlNodePtr> Series = _Document.SeriesInfoNodes();
    if (_SettingsSeriesTable->rowCount() != Series.size()) {
        return;
    }
    for (int i = 0; i < Series.size(); ++i) {
        QTableWidgetItem* NameItem = _SettingsSeriesTable->item(i, 0);
        QTableWidgetItem* ValueItem = _SettingsSeriesTable->item(i, 1);
        QString Name = (NameItem == nullptr) ? QString() : NameItem->text();
        DocDocument::SetNodeAttribute(Series[i], "name", Name);
        if (Name.trimmed() == QLatin1String("Internet-Draft")) {
            DocDocument::SetNodeAttribute(Series[i], "value", _Document.DocName());
        } else {
            DocDocument::SetNodeAttribute(
                Series[i], "value",
                (ValueItem == nullptr) ? QString() : ValueItem->text());
        }
    }
    SetModified(true);
}

void MainWindow::OnSettingsNoteNameEdited(const QString& Text)
{
    QVector<xmlNodePtr> Notes = _Document.NoteNodes();
    if (Notes.isEmpty()) {
        return;
    }
    SetLeadingChildText(Notes.first(), "name", Text);
    SetModified(true);
}

void MainWindow::OnSettingsNoteRemoveChanged()
{
    QVector<xmlNodePtr> Notes = _Document.NoteNodes();
    if (Notes.isEmpty()) {
        return;
    }
    DocDocument::SetNodeAttribute(
        Notes.first(), "removeInRFC",
        _SettingsNoteRemoveCheck->isChecked() ? QStringLiteral("true")
                                              : QString());
    SetModified(true);
}

void MainWindow::OnSettingsNoteEdited()
{
    QVector<xmlNodePtr> Notes = _Document.NoteNodes();
    if (Notes.isEmpty()) {
        return;
    }
    _Document.SetNoteBody(Notes.first(), _SettingsNoteEdit->BodyNodes(),
                          DocDocument::PreservedBodyNodes(Notes.first()));
    SetModified(true);
}

void MainWindow::OnAddNote()
{
    _Document.AddNoteNode();
    SetModified(true);
    FillDocumentSettingsForm();
}

void MainWindow::OnRemoveNote()
{
    if (_Document.NoteNodes().isEmpty()) {
        return;
    }
    QMessageBox::StandardButton Answer = QMessageBox::question(
        this, tr("Remove Note"),
        tr("Remove this note?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (Answer != QMessageBox::Yes) {
        return;
    }
    _Document.RemoveNoteNode(0);
    SetModified(true);
    FillDocumentSettingsForm();
}

void MainWindow::OnAbstractEdited()
{
    _Document.SetAbstractBody(_AbstractEdit->BodyNodes(),
                              _Document.AbstractPreservedNodes());
    SetModified(true);
}

void MainWindow::OnAuthorEdited()
{
    xmlNodePtr Author = ShownAuthor();
    if (Author == nullptr) {
        return;
    }
    DocDocument::SetAuthorAttribute(
        Author, "fullname", _AuthorFullnameEdit->text());
    DocDocument::SetAuthorAttribute(
        Author, "initials", _AuthorInitialsEdit->text());
    DocDocument::SetAuthorAttribute(
        Author, "surname", _AuthorSurnameEdit->text());
    DocDocument::SetAuthorOrganization(
        Author, _AuthorOrganizationEdit->text());
    DocDocument::SetAuthorContactField(
        Author, "email", _AuthorEmailEdit->text());
    DocDocument::SetAuthorContactField(
        Author, "phone", _AuthorPhoneEdit->text());
    DocDocument::SetAuthorContactField(
        Author, "uri", _AuthorUriEdit->text());
    DocDocument::SetAuthorPostalPart(
        Author, "street", _AuthorPostalStreetEdit->text());
    DocDocument::SetAuthorPostalPart(
        Author, "cityarea", _AuthorPostalCityareaEdit->text());
    DocDocument::SetAuthorPostalPart(
        Author, "extaddr", _AuthorPostalExtaddrEdit->text());
    DocDocument::SetAuthorPostalPart(
        Author, "pobox", _AuthorPostalPoboxEdit->text());
    DocDocument::SetAuthorPostalPart(
        Author, "city", _AuthorPostalCityEdit->text());
    DocDocument::SetAuthorPostalPart(
        Author, "region", _AuthorPostalRegionEdit->text());
    DocDocument::SetAuthorPostalPart(
        Author, "code", _AuthorPostalCodeEdit->text());
    DocDocument::SetAuthorPostalPart(
        Author, "country", _AuthorPostalCountryEdit->text());
    DocDocument::SetAuthorPostalPart(
        Author, "sortingcode", _AuthorPostalSortingcodeEdit->text());
    if (DocDocument::AuthorPostalUsesLines(Author)) {
        // Lines map one-to-one onto the lines editor; trailing
        // blank lines are dropped.
        QStringList Lines = _AuthorPostalLinesEdit->toPlainText()
                                .split(QLatin1Char('\n'));
        while (!Lines.isEmpty() && Lines.last().trimmed().isEmpty()) {
            Lines.removeLast();
        }
        DocDocument::SetAuthorPostalLines(
            Author, QVector<QString>(Lines));
    }
    QTreeWidgetItem* Current = _Outline->currentItem();
    if (Current != nullptr) {
        QString Fullname =
            DocDocument::AuthorAttribute(Author, "fullname");
        Current->setText(0, Fullname.trimmed().isEmpty()
            ? tr("(unnamed)") : Fullname);
    }
    SetModified(true);
}

void MainWindow::OnAuthorRoleChanged()
{
    xmlNodePtr Author = ShownAuthor();
    if (Author == nullptr) {
        return;
    }
    DocDocument::SetAuthorAttribute(
        Author, "role",
        _EditorRadio->isChecked() ? QStringLiteral("editor") : QString());
    SetModified(true);
}

void MainWindow::OnPostalModeChanged()
{
    xmlNodePtr Author = ShownAuthor();
    if (Author == nullptr) {
        return;
    }
    bool WantLines = (_AuthorPostalModeCombo->currentIndex() == 1);
    if (WantLines == DocDocument::AuthorPostalUsesLines(Author)) {
        return;
    }
    if (WantLines) {
        // Structured to lines: the non-empty parts join into
        // lines, and the parts stay in the model, so switching
        // back restores them.
        DocDocument::SetAuthorPostalLines(Author, PostalLinesFromParts(Author));
    } else {
        // Lines to structured: the lines are discarded, so ask
        // first, defaulting to No.
        QMessageBox::StandardButton Answer = QMessageBox::question(
            this, tr("Postal Address"),
            tr("Switching to a structured address discards the "
               "free-form lines. Switch anyway?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (Answer != QMessageBox::Yes) {
            QSignalBlocker Blocker(_AuthorPostalModeCombo);
            _AuthorPostalModeCombo->setCurrentIndex(1);
            return;
        }
        DocDocument::SetAuthorPostalLines(Author, QVector<QString>());
    }
    SetModified(true);
    FillAuthorForm();
}

void MainWindow::OnSectionTitleEdited(const QString& Text)
{
    xmlNodePtr Section = FindSection(_SectionPath);
    if (Section == nullptr) {
        return;
    }
    DocDocument::SetSectionTitle(Section, Text);
    QTreeWidgetItem* Current = _Outline->currentItem();
    if (Current != nullptr) {
        // The outline shows the section number in front of the
        // title, so a rename keeps the number.
        QString Title = Text.trimmed().isEmpty() ? tr("(untitled)") : Text;
        Current->setText(0, SectionNumberFromPath(_SectionPath) +
                            QStringLiteral(" ") + Title);
    }
    SetModified(true);
    // A section renamed to a required or recommended title locks
    // at once (Bugs 422, 427).
    RefreshSectionControls(Current);
}

void MainWindow::OnSectionBodyEdited()
{
    xmlNodePtr Section = FindSection(_SectionPath);
    if (Section == nullptr) {
        return;
    }
    _Document.SetSectionBody(Section, _SectionBodyEdit->BodyNodes(),
                             DocDocument::PreservedBodyNodes(Section));
    SetModified(true);
}

void MainWindow::RebuildOutline()
{
    QSignalBlocker Blocker(_Outline);
    _Outline->clear();

    QTreeWidgetItem* Settings = new QTreeWidgetItem(_Outline);
    Settings->setText(0, tr("Document Settings"));
    Settings->setData(0, Qt::UserRole, KindDocumentSettings);

    QTreeWidgetItem* Front = new QTreeWidgetItem(_Outline);
    Front->setText(0, tr("Front"));
    Front->setData(0, Qt::UserRole, KindFront);

    QTreeWidgetItem* Authors = new QTreeWidgetItem(Front);
    Authors->setText(0, tr("Authors"));
    Authors->setData(0, Qt::UserRole, KindAuthors);
    QVector<xmlNodePtr> AuthorNodes = _Document.AuthorNodes();
    for (int i = 0; i < AuthorNodes.size(); ++i) {
        QTreeWidgetItem* Author = new QTreeWidgetItem(Authors);
        QString Name = DocDocument::AuthorAttribute(AuthorNodes[i], "fullname");
        Author->setText(0, Name.trimmed().isEmpty() ? tr("(unnamed)") : Name);
        Author->setData(0, Qt::UserRole, KindAuthor);
        Author->setData(0, Qt::UserRole + 1, i);
    }

    QTreeWidgetItem* Abstract = new QTreeWidgetItem(Front);
    Abstract->setText(0, tr("Abstract"));
    Abstract->setData(0, Qt::UserRole, KindAbstract);

    QTreeWidgetItem* Date = new QTreeWidgetItem(Front);
    Date->setText(0, tr("Date"));
    Date->setData(0, Qt::UserRole, KindDate);

    QTreeWidgetItem* Middle = new QTreeWidgetItem(_Outline);
    Middle->setText(0, tr("Middle"));
    Middle->setData(0, Qt::UserRole, KindMiddle);
    QVector<xmlNodePtr> TopSections = _Document.TopSectionNodes();
    for (int i = 0; i < TopSections.size(); ++i) {
        AddSectionItem(Middle, TopSections[i], QString::number(i));
    }

    QTreeWidgetItem* Back = new QTreeWidgetItem(_Outline);
    Back->setText(0, tr("Back"));
    Back->setData(0, Qt::UserRole, KindBack);

    QTreeWidgetItem* References = new QTreeWidgetItem(Back);
    References->setText(0, tr("References"));
    References->setData(0, Qt::UserRole, KindReferences);
    QVector<xmlNodePtr> Groups = _Document.ReferenceGroupNodes();
    for (int g = 0; g < Groups.size(); ++g) {
        AddReferenceGroupItem(References, Groups[g], QString::number(g));
    }

    _Outline->expandAll();

    QTreeWidgetItem* Selected = nullptr;
    if (!_SelectedKey.isEmpty()) {
        Selected = FindItemByKey(nullptr, _SelectedKey);
    }
    if (Selected == nullptr) {
        Selected = Settings;
    }
    Blocker.unblock();
    _Outline->setCurrentItem(Selected);
    ShowDetailsFor(Selected);
}

/**
 * @brief The display number for a section, from its outline path.
 *
 * The path is zero-based (0, 0.1, 2.0.1); the number is the same
 * path with each part one higher, matching the numbers xml2rfc
 * prints (1, 1.2, 3.1.2). The number is display only: it is not
 * stored in the file and does not change the title or the anchor.
 * @param Path The zero-based dotted path of the section.
 * @return The one-based dotted number for display.
 */
static QString SectionNumberFromPath(const QString& Path)
{
    QStringList Parts = Path.split(QLatin1Char('.'));
    for (QString& Part : Parts) {
        Part = QString::number(Part.toInt() + 1);
    }
    return Parts.join(QLatin1Char('.'));
}

void MainWindow::AddSectionItem(QTreeWidgetItem* Parent, xmlNodePtr Section,
                                const QString& Path)
{
    QTreeWidgetItem* Item = new QTreeWidgetItem(Parent);
    QString Title = DocDocument::SectionTitle(Section).trimmed().isEmpty()
        ? tr("(untitled)") : DocDocument::SectionTitle(Section);
    Item->setText(0, SectionNumberFromPath(Path) + QStringLiteral(" ") + Title);
    Item->setData(0, Qt::UserRole, KindSection);
    Item->setData(0, Qt::UserRole + 1, Path);
    QVector<xmlNodePtr> Children = DocDocument::ChildSectionNodes(Section);
    for (int i = 0; i < Children.size(); ++i) {
        AddSectionItem(Item, Children[i],
                       Path + QStringLiteral(".") + QString::number(i));
    }
}

/**
 * @brief Adds one reference list, with its nested lists, entries,
 *        and their authors, to the outline.
 * @param Parent The outline item to add under.
 * @param Group The <references> node to add (borrowed).
 * @param Path The list's path (dot-separated indices).
 */
void MainWindow::AddReferenceGroupItem(QTreeWidgetItem* Parent, xmlNodePtr Group,
                                       const QString& Path)
{
    QTreeWidgetItem* GroupItem = new QTreeWidgetItem(Parent);
    QString GroupName = DocDocument::ChildText(Group, "name");
    GroupItem->setText(0, GroupName.trimmed().isEmpty()
        ? tr("(unnamed list)") : GroupName);
    GroupItem->setData(0, Qt::UserRole, KindReferenceGroup);
    GroupItem->setData(0, Qt::UserRole + 1, Path);
    QVector<xmlNodePtr> Nested = DocDocument::NestedReferenceGroups(Group);
    for (int n = 0; n < Nested.size(); ++n) {
        AddReferenceGroupItem(GroupItem, Nested[n],
                              Path + QStringLiteral(".") + QString::number(n));
    }
    QVector<xmlNodePtr> Entries = DocDocument::ReferenceEntries(Group);
    for (int e = 0; e < Entries.size(); ++e) {
        xmlNodePtr Entry = Entries[e];
        QTreeWidgetItem* EntryItem = new QTreeWidgetItem(GroupItem);
        QString Label = DocDocument::ReferenceLabel(Entry);
        EntryItem->setText(0, Label.isEmpty() ? tr("(untitled)") : Label);
        EntryItem->setData(0, Qt::UserRole, KindReferenceEntry);
        EntryItem->setData(0, Qt::UserRole + 1, Path);
        EntryItem->setData(0, Qt::UserRole + 2, e);
        QVector<xmlNodePtr> EntryAuthors =
            DocDocument::ReferenceAuthors(Entry);
        for (int a = 0; a < EntryAuthors.size(); ++a) {
            QTreeWidgetItem* AuthorItem = new QTreeWidgetItem(EntryItem);
            QString Name = DocDocument::AuthorAttribute(
                EntryAuthors[a], "fullname");
            AuthorItem->setText(0, Name.trimmed().isEmpty() ? tr("(unnamed)") : Name);
            AuthorItem->setData(0, Qt::UserRole, KindReferenceAuthor);
            AuthorItem->setData(0, Qt::UserRole + 1, Path);
            AuthorItem->setData(0, Qt::UserRole + 2, e);
            AuthorItem->setData(0, Qt::UserRole + 3, a);
        }
    }
    // Included references are entries too, fetched by xml2rfc
    // rather than written in the file: they list under their
    // group by the anchor their href carries, marked included
    // so they are not mistaken for an editable entry.
    QVector<xmlNodePtr> Includes = DocDocument::ReferenceIncludeNodes(Group);
    for (int i = 0; i < Includes.size(); ++i) {
        xmlNodePtr Include = Includes[i];
        QTreeWidgetItem* IncludeItem = new QTreeWidgetItem(GroupItem);
        QString Href = DocDocument::NodeAttribute(Include, "href");
        QString Anchor = DocDocument::ReferenceAnchorFromIncludeHref(Href);
        // A cached entry shows its real title; until it is fetched
        // the anchor with "(included)" says what it is.
        QString Title = _BibCache.CachedTitle(Href);
        QString Label;
        if (!Title.isEmpty() && !Anchor.isEmpty()) {
            Label = tr("%1 (%2)").arg(Title, Anchor);
        } else if (Anchor.isEmpty()) {
            Label = Href.isEmpty() ? tr("(included reference)") : Href;
        } else {
            Label = tr("%1 (included)").arg(Anchor);
        }
        IncludeItem->setText(0, Label);
        IncludeItem->setData(0, Qt::UserRole, KindReferenceInclude);
        IncludeItem->setData(0, Qt::UserRole + 1, Path);
        IncludeItem->setData(0, Qt::UserRole + 2, i);
    }
}

void MainWindow::RefreshSectionControls(QTreeWidgetItem* Item)
{
    bool IsSection = (Item != nullptr &&
        Item->data(0, Qt::UserRole).toInt() == KindSection);
    bool Required = false;
    bool Recommended = false;
    bool CanMoveUp = false;
    bool CanMoveDown = false;
    if (IsSection) {
        QString Path = Item->data(0, Qt::UserRole + 1).toString();
        QVector<xmlNodePtr> Siblings;
        int Index = -1;
        if (SectionSiblings(_Document, Path, Siblings, Index)) {
            // Only a top-level section locks: a subsection with a
            // matching title, or a back-matter section, stays free.
            bool TopLevel = !Path.contains(QLatin1Char('.'));
            QString Title = DocDocument::SectionTitle(Siblings[Index]);
            Required = TopLevel &&
                DocDocument::IsRequiredSectionTitle(Title);
            Recommended = TopLevel &&
                DocDocument::IsRecommendedSectionTitle(Title);
            CanMoveUp = (Index > 0);
            CanMoveDown = (Index + 1 < Siblings.size());
            if (TopLevel) {
                // No move may cross a required section: a required
                // section never moves, and no free section swaps
                // with one, so Up is also off for the section just
                // below one, and Down for the section just above.
                if (Required) {
                    CanMoveUp = false;
                    CanMoveDown = false;
                } else {
                    if (CanMoveUp &&
                        DocDocument::IsRequiredSectionTitle(
                            DocDocument::SectionTitle(Siblings[Index - 1]))) {
                        CanMoveUp = false;
                    }
                    if (CanMoveDown &&
                        DocDocument::IsRequiredSectionTitle(
                            DocDocument::SectionTitle(Siblings[Index + 1]))) {
                        CanMoveDown = false;
                    }
                }
            }
        }
    }
    // Add and Insert stay enabled: adding a section never moves a
    // required section relative to an existing one. Add is never
    // disabled; it appends at the end of the middle list when no
    // section is selected.
    _InsertSectionButton->setEnabled(IsSection);
    _RemoveSectionButton->setEnabled(IsSection && !Required);
    _MoveUpButton->setEnabled(CanMoveUp);
    _MoveDownButton->setEnabled(CanMoveDown);
    // A recommended section's title is fixed like a required
    // one's, but it can be removed and moved freely (Bug 427),
    // so only the title edit takes the recommended lock. The
    // same label carries both notes, with its text set here.
    _SectionTitleEdit->setEnabled(!Required && !Recommended);
    if (Required) {
        _SectionRequiredLabel->setText(
            tr("This is a required section. Its title, position, and "
               "presence are fixed."));
    } else if (Recommended) {
        _SectionRequiredLabel->setText(
            tr("This is a recommended section. Its title is fixed, "
               "but it can be removed or moved."));
    }
    _SectionRequiredLabel->setVisible(Required || Recommended);
}

void MainWindow::ShowDetailsFor(QTreeWidgetItem* Item)
{
    if (Item == nullptr) {
        _Details->setCurrentIndex(DetailNone);
        RefreshSectionControls(nullptr);
        return;
    }
    _SelectedKey = SelectionKey(Item);
    int Kind = Item->data(0, Qt::UserRole).toInt();

    RefreshSectionControls(Item);

    switch (Kind) {
    case KindDocumentSettings:
        FillDocumentSettingsForm();
        _Details->setCurrentIndex(DetailDocumentSettings);
        break;
    case KindFront:
        _GroupLabel->setText(tr("The front matter: the authors, "
                                "the date, and the abstract."));
        _Details->setCurrentIndex(DetailGroup);
        break;
    case KindMiddle:
        _GroupLabel->setText(tr("The numbered sections of the draft. Add, "
                                "insert, remove, and rearrange them with "
                                "the buttons below the outline."));
        _Details->setCurrentIndex(DetailGroup);
        break;
    case KindBack:
        _GroupLabel->setText(tr("The back matter: references and appendices."));
        _Details->setCurrentIndex(DetailGroup);
        break;
    case KindAuthors:
        _Details->setCurrentIndex(DetailAuthors);
        break;
    case KindAuthor:
        _AuthorGroupPath.clear();
        _AuthorEntry = -1;
        _AuthorIndex = Item->data(0, Qt::UserRole + 1).toInt();
        FillAuthorForm();
        _Details->setCurrentIndex(DetailAuthor);
        break;
    case KindAbstract: {
        QSignalBlocker Blocker(_AbstractEdit);
        _AbstractEdit->SetBodyNodes(_Document.AbstractBodyNodes());
        QVector<DocPreservedItem> Preserved = _Document.AbstractPreservedNodes();
        _AbstractEdit->SetPreservedNodes(Preserved);
        int Blocks = Preserved.size();
        if (Blocks > 0) {
            QString Word = (Blocks == 1) ? tr("block") : tr("blocks");
            _AbstractPreservedLabel->setText(
                tr("The abstract also holds %1 preserved %2, kept when you "
                   "save and shown in the XML tab.").arg(Blocks).arg(Word));
            _AbstractPreservedLabel->show();
        } else {
            _AbstractPreservedLabel->hide();
        }
        _Details->setCurrentIndex(DetailAbstract);
        break;
    }
    case KindDate:
        _DateLabel->setText(QDate::currentDate().toString(QStringLiteral("MMMM d, yyyy")));
        _Details->setCurrentIndex(DetailDate);
        break;
    case KindSection:
        _SectionPath = Item->data(0, Qt::UserRole + 1).toString();
        FillSectionForm();
        _Details->setCurrentIndex(DetailSection);
        break;
    case KindReferences: {
        QString Text = tr("The lists of references: normative and informative, "
                          "for example. Add a list, then add entries to it.");
        Text += QStringLiteral("\n\n") +
            tr("Note to editor -") +
            QStringLiteral("\n") +
            tr("• Normative References: Standards or documents that must be "
               "read and implemented to conform to your specification.") +
            QStringLiteral("\n") +
            tr("• Informative References: Background reading, examples, or "
               "helpful context that are not required for implementation.");
        int BackItems = 0;
        for (xmlNodePtr Child : _Document.BackChildNodes()) {
            if (Child->type == XML_ELEMENT_NODE &&
                !DocDocument::NodeNameEquals(Child, "references")) {
                ++BackItems;
            }
        }
        if (BackItems > 0) {
            QString Word = (BackItems == 1) ? tr("item") : tr("items");
            Text += QStringLiteral("\n\n") +
                tr("This file also holds back matter (%1 %2), kept when "
                   "you save but not shown here.").arg(BackItems).arg(Word);
        }
        _ReferencesLabel->setText(Text);
        _Details->setCurrentIndex(DetailReferences);
        break;
    }
    case KindReferenceGroup:
        _ReferenceGroupPath = Item->data(0, Qt::UserRole + 1).toString();
        FillReferenceGroupForm();
        _Details->setCurrentIndex(DetailReferenceGroup);
        break;
    case KindReferenceEntry:
        _ReferenceGroupPath = Item->data(0, Qt::UserRole + 1).toString();
        _ReferenceEntryIndex = Item->data(0, Qt::UserRole + 2).toInt();
        FillReferenceEntryForm();
        _Details->setCurrentIndex(DetailReferenceEntry);
        break;
    case KindReferenceInclude:
        // An included reference has no entry form: show the
        // list it lives in. The list page notes that the list
        // holds details this version does not edit.
        _ReferenceGroupPath = Item->data(0, Qt::UserRole + 1).toString();
        FillReferenceGroupForm();
        _Details->setCurrentIndex(DetailReferenceGroup);
        break;
    case KindReferenceAuthor:
        _AuthorGroupPath = Item->data(0, Qt::UserRole + 1).toString();
        _AuthorEntry = Item->data(0, Qt::UserRole + 2).toInt();
        _AuthorIndex = Item->data(0, Qt::UserRole + 3).toInt();
        FillAuthorForm();
        _Details->setCurrentIndex(DetailAuthor);
        break;
    default:
        _Details->setCurrentIndex(DetailNone);
        break;
    }
}

void MainWindow::FillDocumentSettingsForm()
{
    QSignalBlocker BlockTitle(_SettingsTitleEdit);
    QSignalBlocker BlockAbbrev(_SettingsTitleAbbrevEdit);
    QSignalBlocker BlockAscii(_SettingsTitleAsciiEdit);
    QSignalBlocker BlockSeries(_SettingsSeriesTable);
    QSignalBlocker BlockAreas(_SettingsAreasEdit);
    QSignalBlocker BlockWorkgroups(_SettingsWorkgroupsEdit);
    QSignalBlocker BlockKeywords(_SettingsKeywordsEdit);
    QSignalBlocker BlockNoteName(_SettingsNoteNameEdit);
    QSignalBlocker BlockNoteRemove(_SettingsNoteRemoveCheck);
    QSignalBlocker BlockNoteEdit(_SettingsNoteEdit);
    _SettingsTitleEdit->setText(_Document.Title());
    _SettingsTitleAbbrevEdit->setText(_Document.TitleAbbrev());
    _SettingsTitleAsciiEdit->setText(_Document.TitleAscii());
    int Breaks = _Document.TitleBreaks().size();
    if (Breaks > 0) {
        QString Word = (Breaks == 1) ? tr("line break") : tr("line breaks");
        _SettingsTitleBreaksLabel->setText(
            tr("The title holds %1 %2, kept when you save.")
            .arg(Breaks).arg(Word));
        _SettingsTitleBreaksLabel->show();
    } else {
        _SettingsTitleBreaksLabel->hide();
    }
    // Series info rows map one-to-one onto the table; the
    // Internet-Draft row's value is the draft name and is not
    // editable here.
    QVector<xmlNodePtr> Series = _Document.SeriesInfoNodes();
    _SettingsSeriesTable->setRowCount(Series.size());
    for (int i = 0; i < Series.size(); ++i) {
        QString Name = DocDocument::NodeAttribute(Series[i], "name");
        QTableWidgetItem* NameItem = new QTableWidgetItem(Name);
        _SettingsSeriesTable->setItem(i, 0, NameItem);
        bool IsInternetDraft =
            (Name.trimmed() == QLatin1String("Internet-Draft"));
        QTableWidgetItem* ValueItem = new QTableWidgetItem(
            IsInternetDraft ? _Document.DocName()
                            : DocDocument::NodeAttribute(Series[i], "value"));
        if (IsInternetDraft) {
            ValueItem->setFlags(ValueItem->flags() & ~Qt::ItemIsEditable);
        }
        _SettingsSeriesTable->setItem(i, 1, ValueItem);
    }
    _SettingsAreasEdit->setPlainText(_Document.Areas().join(QStringLiteral("\n")));
    _SettingsWorkgroupsEdit->setPlainText(
        _Document.Workgroups().join(QStringLiteral("\n")));
    _SettingsKeywordsEdit->setPlainText(
        _Document.Keywords().join(QStringLiteral("\n")));
    QVector<xmlNodePtr> Notes = _Document.NoteNodes();
    bool HasNote = !Notes.isEmpty();
    _SettingsNoteGroup->setVisible(HasNote);
    _SettingsAddNoteButton->setVisible(!HasNote);
    if (HasNote) {
        xmlNodePtr Note = Notes.first();
        _SettingsNoteNameEdit->setText(
            DocDocument::ChildText(Note, "name"));
        _SettingsNoteRemoveCheck->setChecked(
            DocDocument::NodeAttribute(Note, "removeInRFC") ==
            QLatin1String("true"));
        _SettingsNoteEdit->SetBodyNodes(DocDocument::BodyNodes(Note));
        _SettingsNoteEdit->SetPreservedNodes(
            DocDocument::PreservedBodyNodes(Note));
        if (Notes.size() > 1) {
            _SettingsNoteExtraLabel->setText(
                tr("This file holds %1 notes; the first is shown here and "
                   "the others are kept when you save.")
                .arg(Notes.size()));
            _SettingsNoteExtraLabel->show();
        } else {
            _SettingsNoteExtraLabel->hide();
        }
        if (NoteHasPreservedContent(Note)) {
            _SettingsNotePreservedLabel->setText(
                tr("This note holds details this version does not edit. "
                   "They are kept when you save."));
            _SettingsNotePreservedLabel->show();
        } else {
            _SettingsNotePreservedLabel->hide();
        }
    }
    if (FrontHasPreservedContent(_Document.FrontNode())) {
        _SettingsPreservedLabel->setText(
            tr("This file also holds front matter this version does not "
               "edit (the generated boilerplate, for example). It is "
               "kept when you save."));
        _SettingsPreservedLabel->show();
    } else {
        _SettingsPreservedLabel->hide();
    }
}

void MainWindow::FillAuthorForm()
{
    QSignalBlocker BlockFullname(_AuthorFullnameEdit);
    QSignalBlocker BlockInitials(_AuthorInitialsEdit);
    QSignalBlocker BlockSurname(_AuthorSurnameEdit);
    QSignalBlocker BlockOrganization(_AuthorOrganizationEdit);
    QSignalBlocker BlockEmail(_AuthorEmailEdit);
    QSignalBlocker BlockPhone(_AuthorPhoneEdit);
    QSignalBlocker BlockUri(_AuthorUriEdit);
    QSignalBlocker BlockStreet(_AuthorPostalStreetEdit);
    QSignalBlocker BlockCityarea(_AuthorPostalCityareaEdit);
    QSignalBlocker BlockExtaddr(_AuthorPostalExtaddrEdit);
    QSignalBlocker BlockPobox(_AuthorPostalPoboxEdit);
    QSignalBlocker BlockCity(_AuthorPostalCityEdit);
    QSignalBlocker BlockRegion(_AuthorPostalRegionEdit);
    QSignalBlocker BlockCode(_AuthorPostalCodeEdit);
    QSignalBlocker BlockCountry(_AuthorPostalCountryEdit);
    QSignalBlocker BlockSortingcode(_AuthorPostalSortingcodeEdit);
    QSignalBlocker BlockPostalMode(_AuthorPostalModeCombo);
    QSignalBlocker BlockPostalLines(_AuthorPostalLinesEdit);
    QSignalBlocker BlockAuthorRadio(_AuthorRadio);
    QSignalBlocker BlockEditorRadio(_EditorRadio);
    const xmlNodePtr Author = ShownAuthor();
    if (Author == nullptr) {
        _AuthorFullnameEdit->clear();
        _AuthorInitialsEdit->clear();
        _AuthorSurnameEdit->clear();
        _AuthorOrganizationEdit->clear();
        _AuthorEmailEdit->clear();
        _AuthorPhoneEdit->clear();
        _AuthorUriEdit->clear();
        _AuthorPostalStreetEdit->clear();
        _AuthorPostalCityareaEdit->clear();
        _AuthorPostalExtaddrEdit->clear();
        _AuthorPostalPoboxEdit->clear();
        _AuthorPostalCityEdit->clear();
        _AuthorPostalRegionEdit->clear();
        _AuthorPostalCodeEdit->clear();
        _AuthorPostalCountryEdit->clear();
        _AuthorPostalSortingcodeEdit->clear();
        _AuthorPostalLinesEdit->clear();
        _AuthorPostalModeCombo->setCurrentIndex(0);
        _AuthorPostalStack->setCurrentIndex(0);
        _AuthorRadio->setChecked(true);
        _AuthorPreservedLabel->hide();
        return;
    }
    _AuthorFullnameEdit->setText(
        DocDocument::AuthorAttribute(Author, "fullname"));
    _AuthorInitialsEdit->setText(
        DocDocument::AuthorAttribute(Author, "initials"));
    _AuthorSurnameEdit->setText(
        DocDocument::AuthorAttribute(Author, "surname"));
    _AuthorOrganizationEdit->setText(
        DocDocument::AuthorOrganization(Author));
    _AuthorEmailEdit->setText(
        DocDocument::AuthorContactField(Author, "email"));
    _AuthorPhoneEdit->setText(
        DocDocument::AuthorContactField(Author, "phone"));
    _AuthorUriEdit->setText(
        DocDocument::AuthorContactField(Author, "uri"));
    _AuthorPostalStreetEdit->setText(
        DocDocument::AuthorPostalPart(Author, "street"));
    _AuthorPostalCityareaEdit->setText(
        DocDocument::AuthorPostalPart(Author, "cityarea"));
    _AuthorPostalExtaddrEdit->setText(
        DocDocument::AuthorPostalPart(Author, "extaddr"));
    _AuthorPostalPoboxEdit->setText(
        DocDocument::AuthorPostalPart(Author, "pobox"));
    _AuthorPostalCityEdit->setText(
        DocDocument::AuthorPostalPart(Author, "city"));
    _AuthorPostalRegionEdit->setText(
        DocDocument::AuthorPostalPart(Author, "region"));
    _AuthorPostalCodeEdit->setText(
        DocDocument::AuthorPostalPart(Author, "code"));
    _AuthorPostalCountryEdit->setText(
        DocDocument::AuthorPostalPart(Author, "country"));
    _AuthorPostalSortingcodeEdit->setText(
        DocDocument::AuthorPostalPart(Author, "sortingcode"));
    _AuthorPostalLinesEdit->setPlainText(
        QStringList(DocDocument::AuthorPostalLines(Author))
            .join(QStringLiteral("\n")));
    bool UsesLines = DocDocument::AuthorPostalUsesLines(Author);
    _AuthorPostalModeCombo->setCurrentIndex(UsesLines ? 1 : 0);
    _AuthorPostalStack->setCurrentIndex(UsesLines ? 1 : 0);
    bool IsEditor = DocDocument::AuthorAttribute(Author, "role") ==
        QLatin1String("editor");
    _AuthorRadio->setChecked(!IsEditor);
    _EditorRadio->setChecked(IsEditor);
    if (DocDocument::AuthorHasPreservedContent(Author)) {
        _AuthorPreservedLabel->setText(
            tr("This author holds details this version does not edit (a fax "
               "number, another email address, or other details, for "
               "example). They are kept when you save."));
        _AuthorPreservedLabel->show();
    } else {
        _AuthorPreservedLabel->hide();
    }
}

xmlNodePtr MainWindow::ShownAuthor()
{
    if (_AuthorGroupPath.isEmpty()) {
        QVector<xmlNodePtr> Authors = _Document.AuthorNodes();
        if (_AuthorIndex < 0 || _AuthorIndex >= Authors.size()) {
            return nullptr;
        }
        return Authors[_AuthorIndex];
    }
    xmlNodePtr Entry = FindReferenceEntry(_AuthorGroupPath, _AuthorEntry);
    if (Entry == nullptr) {
        return nullptr;
    }
    QVector<xmlNodePtr> Authors = DocDocument::ReferenceAuthors(Entry);
    if (_AuthorIndex < 0 || _AuthorIndex >= Authors.size()) {
        return nullptr;
    }
    return Authors[_AuthorIndex];
}

/**
 * @brief Returns the sibling lists of the list at a path, and its index among them.
 *
 * Top-level lists are siblings under <back>; a nested list's
 * siblings are the other nested lists under the same parent.
 * @param Document The document.
 * @param Path The dotted path of the list.
 * @param Siblings The sibling lists, in file order.
 * @param Index The list's index among its siblings.
 * @return True when the path resolves to a list.
 */
static bool ReferenceGroupSiblings(DocDocument& Document, const QString& Path,
                                   QVector<xmlNodePtr>& Siblings, int& Index)
{
    if (Path.trimmed().isEmpty()) {
        return false;
    }
    QStringList Parts = Path.split(QLatin1Char('.'));
    bool Ok = false;
    Index = Parts.last().toInt(&Ok);
    if (!Ok || Index < 0) {
        return false;
    }
    if (Parts.size() == 1) {
        Siblings = Document.ReferenceGroupNodes();
    } else {
        QString ParentPath = Parts.mid(0, Parts.size() - 1).join(QLatin1Char('.'));
        xmlNodePtr Parent = Document.ReferenceGroupAtPath(ParentPath);
        if (Parent == nullptr) {
            return false;
        }
        Siblings = DocDocument::NestedReferenceGroups(Parent);
    }
    if (Index >= Siblings.size()) {
        return false;
    }
    return true;
}

/**
 * @brief Returns a list path with its last part replaced.
 * @param Path The dotted path, such as "0.1".
 * @param Index The new last part.
 * @return The path with the last part replaced.
 */
static QString ReferenceGroupPathWithIndex(const QString& Path, int Index)
{
    QStringList Parts = Path.split(QLatin1Char('.'));
    if (Parts.isEmpty()) {
        return QString::number(Index);
    }
    Parts[Parts.size() - 1] = QString::number(Index);
    return Parts.join(QLatin1Char('.'));
}

/**
 * @brief Returns the display name of the list at a path, with its parents.
 *
 * A nested list shows under its parents, such as "References >
 * Normative References", so a list of lists names each one
 * unambiguously.
 * @param Document The document.
 * @param Path The dotted path of the list.
 * @return The display name, or an empty string when the path is bad.
 */
static QString ReferenceGroupDisplayName(DocDocument& Document,
                                         const QString& Path)
{
    if (Path.trimmed().isEmpty()) {
        return QString();
    }
    QStringList Parts = Path.split(QLatin1Char('.'));
    QStringList Names;
    QString Prefix;
    for (const QString& Part : Parts) {
        Prefix = Prefix.isEmpty()
            ? Part : Prefix + QStringLiteral(".") + Part;
        xmlNodePtr Group = Document.ReferenceGroupAtPath(Prefix);
        if (Group == nullptr) {
            return QString();
        }
        QString Name = DocDocument::ChildText(Group, "name").trimmed();
        Names.append(Name.isEmpty() ? QStringLiteral("(unnamed list)") : Name);
    }
    return Names.join(QStringLiteral(" > "));
}

xmlNodePtr MainWindow::FindReferenceGroup(const QString& Group)
{
    return _Document.ReferenceGroupAtPath(Group);
}

xmlNodePtr MainWindow::FindReferenceEntry(const QString& Group, int Entry)
{
    xmlNodePtr List = FindReferenceGroup(Group);
    if (List == nullptr) {
        return nullptr;
    }
    QVector<xmlNodePtr> Entries = DocDocument::ReferenceEntries(List);
    if (Entry < 0 || Entry >= Entries.size()) {
        return nullptr;
    }
    return Entries[Entry];
}

void MainWindow::FillReferenceGroupForm()
{
    QSignalBlocker Blocker(_ReferenceGroupNameEdit);
    xmlNodePtr Group = FindReferenceGroup(_ReferenceGroupPath);
    if (Group == nullptr) {
        _ReferenceGroupNameEdit->clear();
        _AddReferenceEntryButton->setEnabled(false);
        _AddNestedListButton->setEnabled(false);
        _ReferenceGroupPreservedLabel->hide();
        return;
    }
    _ReferenceGroupNameEdit->setText(DocDocument::ChildText(Group, "name"));
    // A list holds either nested lists or entries, never both:
    // entries go only in a list with no nested lists, and a
    // nested list goes only in a list with no entries. An empty
    // list takes either, and the first child decides.
    bool HasNested = DocDocument::ReferenceGroupHasNestedLists(Group);
    bool HasEntries = DocDocument::ReferenceGroupHasEntries(Group);
    _AddReferenceEntryButton->setEnabled(!HasNested);
    _AddNestedListButton->setEnabled(!HasEntries);
    if (ReferenceGroupHasPreservedContent(Group)) {
        _ReferenceGroupPreservedLabel->setText(
            tr("This list holds details this version does not edit (a "
               "reference group or other entries, for example). They are "
               "kept when you save."));
        _ReferenceGroupPreservedLabel->show();
    } else {
        _ReferenceGroupPreservedLabel->hide();
    }
}

void MainWindow::FillReferenceEntryForm()
{
    QSignalBlocker BlockAnchor(_ReferenceAnchorEdit);
    QSignalBlocker BlockTarget(_ReferenceTargetEdit);
    QSignalBlocker BlockTitle(_ReferenceTitleEdit);
    QSignalBlocker BlockDay(_ReferenceDateDayEdit);
    QSignalBlocker BlockMonth(_ReferenceDateMonthEdit);
    QSignalBlocker BlockYear(_ReferenceDateYearEdit);
    QSignalBlocker BlockStream(_ReferenceStreamCombo);
    QSignalBlocker BlockSeries(_ReferenceSeriesTable);
    QSignalBlocker BlockMoveTo(_ReferenceMoveToCombo);
    xmlNodePtr Entry = FindReferenceEntry(_ReferenceGroupPath, _ReferenceEntryIndex);
    if (Entry == nullptr) {
        _ReferenceAnchorEdit->clear();
        _ReferenceTargetEdit->clear();
        _ReferenceTitleEdit->clear();
        _ReferenceDateDayEdit->clear();
        _ReferenceDateMonthEdit->clear();
        _ReferenceDateYearEdit->clear();
        _ReferenceStreamCombo->setCurrentIndex(0);
        _ReferenceSeriesTable->setRowCount(0);
        _ReferenceMoveToCombo->clear();
        _ReferenceEntryPreservedLabel->hide();
        return;
    }
    _ReferenceAnchorEdit->setText(DocDocument::NodeAttribute(Entry, "anchor"));
    _ReferenceTargetEdit->setText(DocDocument::NodeAttribute(Entry, "target"));
    xmlNodePtr Front = DocDocument::ChildElement(Entry, "front");
    _ReferenceTitleEdit->setText(
        Front == nullptr ? QString()
                         : DocDocument::ChildText(Front, "title"));
    _ReferenceDateDayEdit->setText(
        DocDocument::ReferenceDatePart(Entry, "day"));
    _ReferenceDateMonthEdit->setText(
        DocDocument::ReferenceDatePart(Entry, "month"));
    _ReferenceDateYearEdit->setText(
        DocDocument::ReferenceDatePart(Entry, "year"));
    // A stream value the file holds that is not one of the
    // pulldown's values is added, so the file's value stays
    // visible instead of showing as blank.
    QString Stream = DocDocument::ChildText(Entry, "stream");
    int StreamIndex = _ReferenceStreamCombo->findText(Stream);
    if (StreamIndex < 0 && !Stream.trimmed().isEmpty()) {
        _ReferenceStreamCombo->addItem(Stream);
        StreamIndex = _ReferenceStreamCombo->findText(Stream);
    }
    _ReferenceStreamCombo->setCurrentIndex(StreamIndex < 0 ? 0 : StreamIndex);
    QVector<xmlNodePtr> Series = ReferenceSeriesNodes(Entry);
    _ReferenceSeriesTable->setRowCount(Series.size());
    for (int i = 0; i < Series.size(); ++i) {
        _ReferenceSeriesTable->setItem(
            i, 0, new QTableWidgetItem(
                DocDocument::NodeAttribute(Series[i], "name")));
        _ReferenceSeriesTable->setItem(
            i, 1, new QTableWidgetItem(
                DocDocument::NodeAttribute(Series[i], "value")));
    }
    _ReferenceMoveToCombo->clear();
    // Only a list that can hold entries is offered: a list with
    // nested lists takes no entries. A nested list shows under
    // its parents, so each list is named unambiguously.
    for (const QString& Path : _Document.ReferenceGroupPaths()) {
        xmlNodePtr List = _Document.ReferenceGroupAtPath(Path);
        if (List == nullptr ||
            DocDocument::ReferenceGroupHasNestedLists(List)) {
            continue;
        }
        _ReferenceMoveToCombo->addItem(
            ReferenceGroupDisplayName(_Document, Path), Path);
    }
    int MoveIndex = _ReferenceMoveToCombo->findData(_ReferenceGroupPath);
    _ReferenceMoveToCombo->setCurrentIndex(MoveIndex);
    if (ReferenceEntryHasPreservedContent(Entry)) {
        _ReferenceEntryPreservedLabel->setText(
            tr("This reference holds details this version does not edit "
               "(notes, extra text, or other details, for example). They "
               "are kept when you save."));
        _ReferenceEntryPreservedLabel->show();
    } else {
        _ReferenceEntryPreservedLabel->hide();
    }
}

void MainWindow::OnAddReferenceGroup()
{
    bool Ok = false;
    QString Name = QInputDialog::getText(this, tr("Add List"),
                                         tr("List name:"), QLineEdit::Normal,
                                         QString(), &Ok);
    if (!Ok) {
        return;
    }
    xmlNodePtr Back = _Document.BackNode();
    if (Back == nullptr) {
        xmlNodePtr Rfc = _Document.RfcNode();
        if (Rfc == nullptr) {
            return;
        }
        Back = DocDocument::NewChildElement(Rfc, "back");
    }
    xmlNodePtr Group = xmlNewNode(nullptr, XmlChars("references"));
    xmlAddChild(Back, Group);
    if (!Name.trimmed().isEmpty()) {
        SetLeadingChildText(Group, "name", Name);
    }
    _SelectedKey = QStringLiteral("refgroup:") +
        QString::number(_Document.ReferenceGroupNodes().size() - 1);
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnAddNestedReferenceGroup()
{
    xmlNodePtr Parent = FindReferenceGroup(_ReferenceGroupPath);
    if (Parent == nullptr) {
        return;
    }
    // A list with entries takes no nested list: the schema lets
    // a list hold nested lists or entries, never both. The
    // button is disabled for such a list; this is the same rule
    // enforced here.
    if (DocDocument::ReferenceGroupHasEntries(Parent)) {
        return;
    }
    bool Ok = false;
    QString Name = QInputDialog::getText(this, tr("Add Nested List"),
                                         tr("List name:"), QLineEdit::Normal,
                                         QString(), &Ok);
    if (!Ok) {
        return;
    }
    xmlNodePtr Group = xmlNewNode(nullptr, XmlChars("references"));
    xmlAddChild(Parent, Group);
    if (!Name.trimmed().isEmpty()) {
        SetLeadingChildText(Group, "name", Name);
    }
    _SelectedKey = QStringLiteral("refgroup:") + _ReferenceGroupPath +
        QStringLiteral(".") +
        QString::number(DocDocument::NestedReferenceGroups(Parent).size() - 1);
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnRemoveReferenceGroup()
{
    xmlNodePtr Group = FindReferenceGroup(_ReferenceGroupPath);
    if (Group == nullptr) {
        return;
    }
    QString Name = DocDocument::ChildText(Group, "name");
    QString Question = Name.trimmed().isEmpty()
        ? tr("Remove this list and its entries?")
        : tr("Remove the list \"%1\" and its entries?").arg(Name);
    QMessageBox::StandardButton Answer = QMessageBox::question(
        this, tr("Remove List"), Question,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (Answer != QMessageBox::Yes) {
        return;
    }
    xmlUnlinkNode(Group);
    xmlFreeNode(Group);
    _SelectedKey = QStringLiteral("references");
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnMoveReferenceGroupUp()
{
    QVector<xmlNodePtr> Siblings;
    int Index = -1;
    if (!ReferenceGroupSiblings(_Document, _ReferenceGroupPath, Siblings,
                                Index)) {
        return;
    }
    if (Index <= 0) {
        return;
    }
    xmlNodePtr Moved = Siblings[Index];
    xmlUnlinkNode(Moved);
    xmlAddPrevSibling(Siblings[Index - 1], Moved);
    _ReferenceGroupPath =
        ReferenceGroupPathWithIndex(_ReferenceGroupPath, Index - 1);
    _SelectedKey = QStringLiteral("refgroup:") + _ReferenceGroupPath;
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnMoveReferenceGroupDown()
{
    QVector<xmlNodePtr> Siblings;
    int Index = -1;
    if (!ReferenceGroupSiblings(_Document, _ReferenceGroupPath, Siblings,
                                Index)) {
        return;
    }
    if (Index + 1 >= Siblings.size()) {
        return;
    }
    xmlNodePtr Moved = Siblings[Index];
    xmlUnlinkNode(Moved);
    xmlAddNextSibling(Siblings[Index + 1], Moved);
    _ReferenceGroupPath =
        ReferenceGroupPathWithIndex(_ReferenceGroupPath, Index + 1);
    _SelectedKey = QStringLiteral("refgroup:") + _ReferenceGroupPath;
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnReferenceGroupNameEdited(const QString& Text)
{
    xmlNodePtr Group = FindReferenceGroup(_ReferenceGroupPath);
    if (Group == nullptr) {
        return;
    }
    SetLeadingChildText(Group, "name", Text);
    QTreeWidgetItem* Current = _Outline->currentItem();
    if (Current != nullptr) {
        Current->setText(0, Text.trimmed().isEmpty() ? tr("(unnamed list)") : Text);
    }
    SetModified(true);
}

/**
 * @brief A title turned into an anchor: lower case, words joined by dashes.
 * @param Title The title.
 * @return The anchor, or "section" when the title holds no letters or digits.
 */
static QString SlugAnchor(const QString& Title)
{
    QString Slug = Title.trimmed().toLower();
    Slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")),
                 QStringLiteral("-"));
    while (Slug.startsWith(QLatin1Char('-'))) {
        Slug.remove(0, 1);
    }
    while (Slug.endsWith(QLatin1Char('-'))) {
        Slug.chop(1);
    }
    if (Slug.isEmpty()) {
        Slug = QStringLiteral("section");
    }
    return Slug;
}

/**
 * @brief The RFC number an anchor names, or empty.
 * @param Anchor The anchor.
 * @return The digits, or empty when the anchor is not an RFC number.
 */
static QString RfcNumberFromAnchor(const QString& Anchor)
{
    if (Anchor.length() <= 3 ||
        !Anchor.startsWith(QStringLiteral("RFC"), Qt::CaseInsensitive)) {
        return QString();
    }
    QString Digits = Anchor.mid(3);
    for (QChar Ch : Digits) {
        if (!Ch.isDigit()) {
            return QString();
        }
    }
    return Digits;
}

/**
 * @brief True when any reference list already carries this anchor.
 * @param Document The document.
 * @param Anchor The anchor to look for.
 * @return True when the anchor is already used.
 */
static bool ReferenceAnchorExists(const DocDocument& Document,
                                  const QString& Anchor)
{
    for (const QString& Path : Document.ReferenceGroupPaths()) {
        xmlNodePtr Group = Document.ReferenceGroupAtPath(Path);
        if (Group == nullptr) {
            continue;
        }
        for (xmlNodePtr Entry : DocDocument::ReferenceEntries(Group)) {
            if (DocDocument::NodeAttribute(Entry, "anchor").trimmed() ==
                Anchor) {
                return true;
            }
        }
        for (xmlNodePtr Include : DocDocument::ReferenceIncludeNodes(Group)) {
            if (DocDocument::ReferenceAnchorFromIncludeHref(
                    DocDocument::NodeAttribute(Include, "href")) == Anchor) {
                return true;
            }
        }
    }
    return false;
}

xmlNodePtr MainWindow::EnsureReferenceList(const QString& ListName)
{
    // The menu names the list short ("Normative"); the IETF
    // form names it in full ("Normative References"). Either
    // spelling finds the list; a new one takes the full form.
    // Matching only the short name never found the list: the
    // entry went into a stray list just called "Normative".
    QString FullName = ListName.endsWith(QStringLiteral(" References"))
        ? ListName : ListName + QStringLiteral(" References");
    for (const QString& Path : _Document.ReferenceGroupPaths()) {
        if (Path.contains(QLatin1Char('.'))) {
            continue;
        }
        xmlNodePtr Group = _Document.ReferenceGroupAtPath(Path);
        if (Group == nullptr ||
            DocDocument::ChildText(Group, "name") !=
                QLatin1String("References")) {
            continue;
        }
        if (DocDocument::ReferenceGroupHasNestedLists(Group)) {
            for (xmlNodePtr Nested :
                 DocDocument::NestedReferenceGroups(Group)) {
                QString NestedName =
                    DocDocument::ChildText(Nested, "name");
                if (NestedName == ListName || NestedName == FullName) {
                    return Nested;
                }
            }
            xmlNodePtr Nested =
                xmlNewNode(nullptr, XmlChars("references"));
            SetLeadingChildText(Nested, "name", FullName);
            xmlAddChild(Group, Nested);
            return Nested;
        }
        // A flat list holds entries, not nested lists, so it
        // cannot gain a nested list: it is the references list.
        return Group;
    }
    // No "References" list at all: build the nested IETF form,
    // both nested lists empty, and hand back the one asked for.
    xmlNodePtr Back = _Document.BackNode();
    if (Back == nullptr) {
        xmlNodePtr Rfc = _Document.RfcNode();
        if (Rfc == nullptr) {
            return nullptr;
        }
        Back = DocDocument::NewChildElement(Rfc, "back");
    }
    xmlNodePtr Outer = xmlNewNode(nullptr, XmlChars("references"));
    SetLeadingChildText(Outer, "name", QStringLiteral("References"));
    xmlAddChild(Back, Outer);
    xmlNodePtr Wanted = nullptr;
    for (const QString& Name : {QStringLiteral("Normative References"),
                                QStringLiteral("Informative References")}) {
        xmlNodePtr Nested = xmlNewNode(nullptr, XmlChars("references"));
        SetLeadingChildText(Nested, "name", Name);
        xmlAddChild(Outer, Nested);
        if (Name == FullName) {
            Wanted = Nested;
        }
    }
    return Wanted;
}

void MainWindow::HandleMakeReference(TextEditor* Editor, int Action,
                                     xmlNodePtr Target,
                                     const QString& Anchor,
                                     const QString& ListName)
{
    if (Editor == nullptr) {
        return;
    }
    // The outline rebuild below reloads the section body and
    // drops the live selection, so the highlighted range is
    // captured here, before anything that rebuilds. Without
    // it the cross-reference landed at the start of the
    // section as a bare marker instead of on the highlight.
    int SelectionStart = -1;
    int SelectionEnd = -1;
    Editor->SelectedRange(SelectionStart, SelectionEnd);
    if (Action == TextEditor::InternalReferenceAction) {
        if (Target == nullptr) {
            return;
        }
        QString TargetAnchor =
            DocDocument::NodeAttribute(Target, "anchor").trimmed();
        if (TargetAnchor.isEmpty()) {
            // Sections carry no anchor until one is picked:
            // derive one from the title, unique in the document.
            QString Title = DocDocument::SectionTitle(Target);
            if (Title.trimmed().isEmpty()) {
                Title = DocDocument::ChildText(Target, "name");
            }
            TargetAnchor = SlugAnchor(Title);
            QString Base = TargetAnchor;
            int Suffix = 2;
            while (DocDocument::DocumentHasAnchor(_Document.RfcNode(),
                                                  TargetAnchor)) {
                TargetAnchor = Base + QStringLiteral("-") +
                    QString::number(Suffix++);
            }
            DocDocument::SetNodeAttribute(Target, "anchor", TargetAnchor);
            SetModified(true);
        }
        Editor->InsertXrefMarker(TargetAnchor, SelectionStart, SelectionEnd);
        return;
    }
    if (Action != TextEditor::NewReferenceAction ||
        Anchor.trimmed().isEmpty() || ListName.isEmpty()) {
        return;
    }
    QString Wanted = Anchor.trimmed();
    xmlNodePtr Group = EnsureReferenceList(ListName);
    if (Group == nullptr) {
        return;
    }
    if (ReferenceAnchorExists(_Document, Wanted)) {
        // The entry is already there: cite it instead of a duplicate.
        Editor->InsertXrefMarker(Wanted, SelectionStart, SelectionEnd);
        return;
    }
    QString RfcNumber = RfcNumberFromAnchor(Wanted);
    if (!RfcNumber.isEmpty()) {
        // xml2rfc pulls the full reference from its database; the
        // editor only supplies the tag. The included file IS the
        // whole <reference>, so nothing else is needed.
        xmlNsPtr XiNs =
            DocDocument::EnsureXIncludeNamespace(_Document.RfcNode());
        if (XiNs == nullptr) {
            return;
        }
        xmlNodePtr Include =
            xmlNewChild(Group, XiNs, XmlChars("include"), nullptr);
        DocDocument::SetNodeAttribute(
            Include, "href",
            QStringLiteral(
                "https://bib.ietf.org/public/rfc/bibxml/reference.RFC.") +
                RfcNumber + QStringLiteral(".xml"));
    } else {
        // A placeholder: the anchor, with the title and author
        // prefilled to say what still needs providing. The author
        // completes it in the entry form. The entry is valid as
        // it stands; only the placeholder text says it is not
        // done. The author carries only the fullname: no
        // initials or surname are invented for a placeholder,
        // since the form writes each author field on its own
        // and derived values would linger after the real name
        // is typed in.
        xmlNodePtr Entry = xmlNewNode(nullptr, XmlChars("reference"));
        DocDocument::SetNodeAttribute(Entry, "anchor", Wanted);
        xmlNodePtr Front = ReferenceFrontNode(Entry, true);
        if (Front != nullptr) {
            SetLeadingChildText(Front, "title",
                                QStringLiteral("Please Supply Title"));
            xmlNodePtr Author = xmlNewNode(nullptr, XmlChars("author"));
            DocDocument::SetNodeAttribute(
                Author, "fullname",
                QStringLiteral("Please supply Author"));
            xmlAddChild(Front, Author);
        }
        AppendReferenceEntry(Group, Entry);
    }
    SetModified(true);
    RebuildOutline();
    Editor->InsertXrefMarker(Wanted, SelectionStart, SelectionEnd);
}

void MainWindow::OnAddReferenceEntry()
{
    xmlNodePtr Group = FindReferenceGroup(_ReferenceGroupPath);
    if (Group == nullptr) {
        return;
    }
    // A list with nested lists takes no entries: the schema lets
    // a list hold nested lists or entries, never both. The
    // button is disabled for such a list; this is the same rule
    // enforced here.
    if (DocDocument::ReferenceGroupHasNestedLists(Group)) {
        return;
    }
    // Ask for the anchor first: an entry with no anchor cannot save.
    bool Ok = false;
    QString Anchor = QInputDialog::getText(this, tr("Add Entry"),
                                           tr("Anchor:"), QLineEdit::Normal,
                                           QString(), &Ok);
    while (Ok && Anchor.trimmed().isEmpty()) {
        Anchor = QInputDialog::getText(this, tr("Add Entry"),
                                       tr("Anchor:"), QLineEdit::Normal,
                                       QString(), &Ok);
    }
    if (!Ok) {
        return;
    }
    xmlNodePtr Entry = xmlNewNode(nullptr, XmlChars("reference"));
    DocDocument::SetNodeAttribute(Entry, "anchor", Anchor);
    AppendReferenceEntry(Group, Entry);
    _SelectedKey = QStringLiteral("refentry:") + _ReferenceGroupPath +
        QStringLiteral(".") +
        QString::number(DocDocument::ReferenceEntries(Group).size() - 1);
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnRemoveReferenceEntry()
{
    xmlNodePtr Entry = FindReferenceEntry(_ReferenceGroupPath, _ReferenceEntryIndex);
    if (Entry == nullptr) {
        return;
    }
    QString Label = DocDocument::NodeAttribute(Entry, "anchor");
    if (Label.trimmed().isEmpty()) {
        xmlNodePtr Front = DocDocument::ChildElement(Entry, "front");
        Label = (Front == nullptr)
            ? QString() : DocDocument::ChildText(Front, "title");
    }
    QString Question = Label.trimmed().isEmpty()
        ? tr("Remove this reference entry?")
        : tr("Remove the reference \"%1\"?").arg(Label);
    QMessageBox::StandardButton Answer = QMessageBox::question(
        this, tr("Remove Entry"), Question,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (Answer != QMessageBox::Yes) {
        return;
    }
    xmlUnlinkNode(Entry);
    xmlFreeNode(Entry);
    _SelectedKey = QStringLiteral("refgroup:") + _ReferenceGroupPath;
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnMoveReferenceEntryUp()
{
    xmlNodePtr Group = FindReferenceGroup(_ReferenceGroupPath);
    if (Group == nullptr || _ReferenceEntryIndex <= 0) {
        return;
    }
    QVector<xmlNodePtr> Entries = DocDocument::ReferenceEntries(Group);
    if (_ReferenceEntryIndex >= Entries.size()) {
        return;
    }
    xmlNodePtr Moved = Entries[_ReferenceEntryIndex];
    xmlUnlinkNode(Moved);
    xmlAddPrevSibling(Entries[_ReferenceEntryIndex - 1], Moved);
    --_ReferenceEntryIndex;
    _SelectedKey = QStringLiteral("refentry:") + _ReferenceGroupPath +
        QStringLiteral(".") + QString::number(_ReferenceEntryIndex);
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnMoveReferenceEntryDown()
{
    xmlNodePtr Group = FindReferenceGroup(_ReferenceGroupPath);
    if (Group == nullptr || _ReferenceEntryIndex < 0) {
        return;
    }
    QVector<xmlNodePtr> Entries = DocDocument::ReferenceEntries(Group);
    if (_ReferenceEntryIndex + 1 >= Entries.size()) {
        return;
    }
    xmlNodePtr Moved = Entries[_ReferenceEntryIndex];
    xmlUnlinkNode(Moved);
    xmlAddNextSibling(Entries[_ReferenceEntryIndex + 1], Moved);
    ++_ReferenceEntryIndex;
    _SelectedKey = QStringLiteral("refentry:") + _ReferenceGroupPath +
        QStringLiteral(".") + QString::number(_ReferenceEntryIndex);
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnReferenceEntryEdited()
{
    xmlNodePtr Entry = FindReferenceEntry(_ReferenceGroupPath, _ReferenceEntryIndex);
    if (Entry == nullptr) {
        return;
    }
    QString OldAnchor = DocDocument::NodeAttribute(Entry, "anchor");
    QString NewAnchor = _ReferenceAnchorEdit->text();
    DocDocument::SetNodeAttribute(Entry, "anchor", _ReferenceAnchorEdit->text());
    if (!OldAnchor.trimmed().isEmpty() && !NewAnchor.trimmed().isEmpty() &&
        OldAnchor.trimmed() != NewAnchor.trimmed()) {
        // The anchor names the reference: every cross-reference
        // that cited the old anchor now cites the new one, so
        // entering the data here updates the <xref> values too.
        DocDocument::RenameXrefTargets(_Document.RfcNode(),
                                       OldAnchor.trimmed(),
                                       NewAnchor.trimmed());
    }
    DocDocument::SetNodeAttribute(Entry, "target", _ReferenceTargetEdit->text());
    SetReferenceTitle(Entry, _ReferenceTitleEdit->text());
    SetReferenceDatePart(Entry, "day", _ReferenceDateDayEdit->text());
    SetReferenceDatePart(Entry, "month", _ReferenceDateMonthEdit->text());
    SetReferenceDatePart(Entry, "year", _ReferenceDateYearEdit->text());
    QTreeWidgetItem* Current = _Outline->currentItem();
    if (Current != nullptr) {
        QString Label = DocDocument::ReferenceLabel(Entry);
        Current->setText(0, Label.isEmpty() ? tr("(untitled)") : Label);
    }
    SetModified(true);
}

void MainWindow::OnReferenceStreamChanged()
{
    xmlNodePtr Entry = FindReferenceEntry(_ReferenceGroupPath, _ReferenceEntryIndex);
    if (Entry == nullptr) {
        return;
    }
    SetReferenceStream(Entry, _ReferenceStreamCombo->currentText());
    SetModified(true);
}

void MainWindow::OnReferenceMoveToGroupChanged()
{
    xmlNodePtr Entry = FindReferenceEntry(_ReferenceGroupPath, _ReferenceEntryIndex);
    if (Entry == nullptr) {
        return;
    }
    QString Target = _ReferenceMoveToCombo->currentData().toString();
    xmlNodePtr TargetGroup = FindReferenceGroup(Target);
    if (TargetGroup == nullptr || Target == _ReferenceGroupPath) {
        return;
    }
    xmlUnlinkNode(Entry);
    AppendReferenceEntry(TargetGroup, Entry);
    _ReferenceGroupPath = Target;
    _ReferenceEntryIndex =
        DocDocument::ReferenceEntries(TargetGroup).size() - 1;
    _SelectedKey = QStringLiteral("refentry:") + _ReferenceGroupPath +
        QStringLiteral(".") + QString::number(_ReferenceEntryIndex);
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnAddSeriesInfo()
{
    xmlNodePtr Entry = FindReferenceEntry(_ReferenceGroupPath, _ReferenceEntryIndex);
    if (Entry == nullptr) {
        return;
    }
    AddReferenceSeriesNode(Entry);
    SetModified(true);
    FillReferenceEntryForm();
    int Row = _ReferenceSeriesTable->rowCount() - 1;
    if (Row >= 0) {
        _ReferenceSeriesTable->setCurrentCell(Row, 0);
        QTableWidgetItem* Cell = _ReferenceSeriesTable->item(Row, 0);
        if (Cell != nullptr) {
            _ReferenceSeriesTable->editItem(Cell);
        }
    }
}

void MainWindow::OnRemoveSeriesInfo()
{
    xmlNodePtr Entry = FindReferenceEntry(_ReferenceGroupPath, _ReferenceEntryIndex);
    if (Entry == nullptr) {
        return;
    }
    int Row = _ReferenceSeriesTable->currentRow();
    if (Row < 0 || Row >= ReferenceSeriesNodes(Entry).size()) {
        return;
    }
    RemoveReferenceSeriesNode(Entry, Row);
    SetModified(true);
    FillReferenceEntryForm();
}

void MainWindow::OnSeriesInfoChanged()
{
    xmlNodePtr Entry = FindReferenceEntry(_ReferenceGroupPath, _ReferenceEntryIndex);
    if (Entry == nullptr) {
        return;
    }
    // Rows map one-to-one onto the series info; editing a cell
    // keeps the row's kept attributes.
    QVector<xmlNodePtr> Series = ReferenceSeriesNodes(Entry);
    if (_ReferenceSeriesTable->rowCount() != Series.size()) {
        return;
    }
    for (int i = 0; i < Series.size(); ++i) {
        QTableWidgetItem* NameItem = _ReferenceSeriesTable->item(i, 0);
        QTableWidgetItem* ValueItem = _ReferenceSeriesTable->item(i, 1);
        DocDocument::SetNodeAttribute(
            Series[i], "name",
            (NameItem == nullptr) ? QString() : NameItem->text());
        DocDocument::SetNodeAttribute(
            Series[i], "value",
            (ValueItem == nullptr) ? QString() : ValueItem->text());
    }
    SetModified(true);
}

void MainWindow::OnAddReferenceAuthor()
{
    xmlNodePtr Entry = FindReferenceEntry(_ReferenceGroupPath, _ReferenceEntryIndex);
    if (Entry == nullptr) {
        return;
    }
    // Ask for the name first: an author with no name is no use.
    bool Ok = false;
    QString Name = QInputDialog::getText(this, tr("Add Author"),
                                         tr("Full name:"), QLineEdit::Normal,
                                         QString(), &Ok);
    while (Ok && Name.trimmed().isEmpty()) {
        Name = QInputDialog::getText(this, tr("Add Author"),
                                     tr("Full name:"), QLineEdit::Normal,
                                     QString(), &Ok);
    }
    if (!Ok) {
        return;
    }
    xmlNodePtr Front = ReferenceFrontNode(Entry, true);
    if (Front == nullptr) {
        return;
    }
    xmlNodePtr Author =
        DocDocument::AuthorNodeFromFullname(Name, QString(), QString());
    // Authors sit before the date in a reference front.
    xmlNodePtr Date = DocDocument::ChildElement(Front, "date");
    if (Date != nullptr) {
        xmlAddPrevSibling(Date, Author);
    } else {
        xmlAddChild(Front, Author);
    }
    _SelectedKey = QStringLiteral("refauthor:") + _ReferenceGroupPath +
        QStringLiteral(".") + QString::number(_ReferenceEntryIndex) +
        QStringLiteral(".") +
        QString::number(DocDocument::ReferenceAuthors(Entry).size() - 1);
    SetModified(true);
    RebuildOutline();
}

void MainWindow::FillSectionForm()
{
    QSignalBlocker BlockTitle(_SectionTitleEdit);
    QSignalBlocker BlockBody(_SectionBodyEdit);
    xmlNodePtr Section = FindSection(_SectionPath);
    if (Section == nullptr) {
        _SectionTitleEdit->clear();
        _SectionBodyEdit->Clear();
        _SectionPreservedLabel->hide();
        _SectionRequiredLabel->hide();
        return;
    }
    _SectionTitleEdit->setText(DocDocument::SectionTitle(Section));
    _SectionBodyEdit->SetBodyNodes(DocDocument::BodyNodes(Section));
    QVector<DocPreservedItem> Preserved =
        DocDocument::PreservedBodyNodes(Section);
    _SectionBodyEdit->SetPreservedNodes(Preserved);
    int Blocks = Preserved.size();
    if (Blocks > 0) {
        QString Word = (Blocks == 1) ? tr("block") : tr("blocks");
        _SectionPreservedLabel->setText(
            tr("This section also holds %1 preserved %2, kept when you save "
               "and shown in the XML tab.").arg(Blocks).arg(Word));
        _SectionPreservedLabel->show();
    } else if (SectionHasPreservedAttributes(Section)) {
        _SectionPreservedLabel->setText(
            tr("This section holds preserved details, kept when you save "
               "but not shown here."));
        _SectionPreservedLabel->show();
    } else {
        _SectionPreservedLabel->hide();
    }
}

xmlNodePtr MainWindow::FindSection(const QString& Path)
{
    if (Path.isEmpty()) {
        return nullptr;
    }
    return _Document.FindSectionNode(Path);
}

QString MainWindow::SelectionKey(QTreeWidgetItem* Item) const
{
    if (Item == nullptr) {
        return QString();
    }
    int Kind = Item->data(0, Qt::UserRole).toInt();
    switch (Kind) {
    case KindDocumentSettings:
        return QStringLiteral("docset");
    case KindFront:
        return QStringLiteral("front");
    case KindAuthors:
        return QStringLiteral("authors");
    case KindAuthor:
        return QStringLiteral("author:") + Item->data(0, Qt::UserRole + 1).toString();
    case KindAbstract:
        return QStringLiteral("abstract");
    case KindDate:
        return QStringLiteral("date");
    case KindMiddle:
        return QStringLiteral("middle");
    case KindSection:
        return QStringLiteral("section:") + Item->data(0, Qt::UserRole + 1).toString();
    case KindBack:
        return QStringLiteral("back");
    case KindReferences:
        return QStringLiteral("references");
    case KindReferenceGroup:
        return QStringLiteral("refgroup:") + Item->data(0, Qt::UserRole + 1).toString();
    case KindReferenceEntry:
        return QStringLiteral("refentry:") + Item->data(0, Qt::UserRole + 1).toString() +
            QStringLiteral(".") + Item->data(0, Qt::UserRole + 2).toString();
    case KindReferenceAuthor:
        return QStringLiteral("refauthor:") + Item->data(0, Qt::UserRole + 1).toString() +
            QStringLiteral(".") + Item->data(0, Qt::UserRole + 2).toString() +
            QStringLiteral(".") + Item->data(0, Qt::UserRole + 3).toString();
    case KindReferenceInclude:
        return QStringLiteral("refinclude:") + Item->data(0, Qt::UserRole + 1).toString() +
            QStringLiteral(".") + Item->data(0, Qt::UserRole + 2).toString();
    default:
        return QString();
    }
}

QTreeWidgetItem* MainWindow::FindItemByKey(QTreeWidgetItem* Parent, const QString& Key) const
{
    int Count = (Parent == nullptr) ? _Outline->topLevelItemCount() : Parent->childCount();
    for (int i = 0; i < Count; ++i) {
        QTreeWidgetItem* Child = (Parent == nullptr)
            ? _Outline->topLevelItem(i) : Parent->child(i);
        if (SelectionKey(Child) == Key) {
            return Child;
        }
        QTreeWidgetItem* Found = FindItemByKey(Child, Key);
        if (Found != nullptr) {
            return Found;
        }
    }
    return nullptr;
}

void MainWindow::SetModified(bool Modified)
{
    _Modified = Modified;
    UpdateWindowTitle();
}

void MainWindow::UpdateWindowTitle()
{
    QString Title = tr("ProtocolDocEditor - V%1")
                        .arg(QCoreApplication::applicationVersion());
    if (!_Document.DocName().isEmpty()) {
        Title += QStringLiteral(" - ") + _Document.DocName();
    }
    if (_Modified) {
        Title += QStringLiteral(" *");
    }
    setWindowTitle(Title);
}

void MainWindow::UpdateProjectDictionary()
{
    if (_SpellChecker == nullptr) {
        return;
    }
    QString Path;
    if (!_FilePath.isEmpty()) {
        QFileInfo Info(_FilePath);
        Path = Info.dir().filePath(Info.completeBaseName() +
                                   QStringLiteral(".dic"));
    }
    _SpellChecker->SetProjectDictionary(Path);
    // The project words changed, so the underlines may have
    // changed too: re-check every text pane.
    if (_AbstractEdit != nullptr) {
        _AbstractEdit->RefreshSpelling();
    }
    if (_SectionBodyEdit != nullptr) {
        _SectionBodyEdit->RefreshSpelling();
    }
    if (_SettingsNoteEdit != nullptr) {
        _SettingsNoteEdit->RefreshSpelling();
    }
}

bool MainWindow::ConfirmDiscard()
{
    if (!_Modified) {
        return true;
    }
    QMessageBox::StandardButton Answer = QMessageBox::question(
        this, tr("Unsaved Changes"),
        tr("The draft has unsaved changes. Save them?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (Answer == QMessageBox::Cancel) {
        return false;
    }
    if (Answer == QMessageBox::Save) {
        OnSave();
        return !_Modified;
    }
    return true;
}

bool MainWindow::SaveToFile(const QString& Path)
{
    QStringList Found = _Document.Problems();
    if (!Found.isEmpty()) {
        QMessageBox::warning(this, tr("Save Draft"),
                             tr("The draft is not ready to save:\n%1")
                             .arg(Found.join(QStringLiteral("\n"))));
        return false;
    }
    if (_Document.HadSkippedContent) {
        QMessageBox::StandardButton Answer = QMessageBox::warning(
            this, tr("Save Draft"),
            tr("This draft holds some details this version could not keep "
               "(markup inside a title, for example). Saving will drop "
               "those details."),
            QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel);
        if (Answer != QMessageBox::Save) {
            return false;
        }
    }
    // A literal line over 69 characters draws xml2rfc warning
    // L106 when the draft is rendered (Bug 458): xml2rfc never
    // wraps source code or ASCII art, and renders it indented.
    // This warns and lets the user go back or save anyway; it
    // is not a Problems() gate. The button pattern follows the
    // empty-version warning (Bug 305).
    QStringList LongLines = _Document.LongLiteralLines();
    if (!LongLines.isEmpty()) {
        QString Warning = tr("These literal lines are longer than 69 "
                             "characters. xml2rfc never wraps source code "
                             "or ASCII art, so each line draws a warning "
                             "when the draft is rendered:\n%1")
                              .arg(LongLines.join(QStringLiteral("\n")));
        // The closing sentence is Doug's wording, verbatim.
        Warning += tr("\nThis must be fixed before submitting to the IETF for review.");
        QMessageBox Box(QMessageBox::Warning, tr("Long literal lines"),
                        Warning, QMessageBox::NoButton, this);
        QPushButton* GoBackButton =
            Box.addButton(tr("Go back"), QMessageBox::RejectRole);
        Box.addButton(tr("Save anyway"), QMessageBox::AcceptRole);
        Box.setDefaultButton(GoBackButton);
        Box.exec();
        if (Box.clickedButton() == GoBackButton) {
            statusBar()->showMessage(tr("Save cancelled; nothing was written."),
                                     5000);
            return false;
        }
    }
    QString Error;
    if (!_Document.WriteXml(Path, Error)) {
        QMessageBox::critical(this, tr("Save Draft"),
                              tr("Could not save %1:\n%2").arg(Path, Error));
        return false;
    }
    // The file now holds exactly what the editor holds, so the
    // skipped-content warning does not fire on the next save.
    _Document.HadSkippedContent = false;
    SetModified(false);
    // What was saved gets read back in and checked.
    DocValidationResult Checked = ValidateAndReport(Path, false);
    if (Checked.Valid) {
        statusBar()->showMessage(tr("Saved %1 (valid).").arg(Path));
    }
    AddRecentFile(Path);
    RebuildRecentMenu();
    UpdateViewButtons();
    return true;
}

QStringList MainWindow::RecentFiles()
{
    QSettings Settings;
    return Settings.value(QStringLiteral("recent/files")).toStringList();
}

void MainWindow::AddRecentFile(const QString& Path)
{
    QStringList Files = RecentFiles();
    Files.removeAll(Path);
    Files.prepend(Path);
    while (Files.size() > MaxRecentFiles) {
        Files.removeLast();
    }
    QSettings Settings;
    Settings.setValue(QStringLiteral("recent/files"), Files);
}

QString MainWindow::Xml2rfcPath() const
{
    QSettings Settings;
    return Settings.value(QStringLiteral("xml2rfc/path"),
                          QStringLiteral("xml2rfc")).toString();
}

void MainWindow::AppendRunLog(const QString& Text)
{
    _RunLog->moveCursor(QTextCursor::End);
    _RunLog->insertPlainText(Text);
    _RunLog->moveCursor(QTextCursor::End);
}
