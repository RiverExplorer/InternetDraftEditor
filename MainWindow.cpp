// Copyright (c) 2026 RiverExplorer LLC
// RiverExplorer is a registered trademark of Douglas Mark Royer.
//
// This software is released under the MIT License.
// See LICENSE.md for the full license text.

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
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QPixmap>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextCursor>
#include <QTextStream>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

// Bug 532: telling Windows the font list changed needs the
// Win32 API (SendMessageTimeout for WM_FONTCHANGE). NOMINMAX
// keeps windows.h from defining min/max macros that clash
// with the C++ standard library.
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <functional>
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

// The title-to-anchor helper is defined with the reference
// code below; the section title and anchor handlers call it,
// so it is declared here first.
static QString SlugAnchor(const QString& Title);

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
    if (NodeHasChildrenOtherThan(Entry, QStringList{"stream", "front",
                                                    "annotation"})) {
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
 * @brief True when a section path names an appendix section.
 *
 * Appendix paths carry a "back:" prefix ("back:0", "back:0.1");
 * middle paths are bare dot-separated indices ("0", "1.2").
 * @param Path The section path.
 * @return True for an appendix path.
 */
static bool IsBackPath(const QString& Path)
{
    return Path.startsWith(QStringLiteral("back:"));
}

/**
 * @brief True when a path names a top-level section of the middle.
 *
 * Only those lock as required sections (Bug 422): a top-level
 * appendix never locks, whatever its title.
 * @param Path The section path.
 * @return True for a top-level middle section.
 */
static bool IsMiddleTopLevel(const QString& Path)
{
    return !IsBackPath(Path) && !Path.contains(QLatin1Char('.'));
}

/**
 * @brief The path prefix for a section landing in the same list as
 *        the section at a path: "back:" for a top-level appendix,
 *        "" for a top-level middle section, or the parent path
 *        plus "." for a subsection.
 * @param Path The section path.
 * @return The list prefix.
 */
static QString ParentListPrefix(const QString& Path)
{
    int Dot = Path.lastIndexOf(QLatin1Char('.'));
    if (Dot >= 0) {
        return Path.left(Dot) + QStringLiteral(".");
    }
    if (IsBackPath(Path)) {
        return QStringLiteral("back:");
    }
    return QString();
}

/**
 * @brief Finds the sibling sections of the section at a path.
 *
 * The siblings are the parent's child sections, or the
 * top-level sections for a top-level path, from
 * DocDocument::ChildSectionNodes plus index arithmetic. A path
 * starting with "back:" walks the appendix sections instead of
 * the middle ones (Bug 513).
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
    QString Work = Path;
    bool Back = false;
    if (IsBackPath(Work)) {
        Work = Work.mid(QStringLiteral("back:").size());
        Back = true;
    }
    QStringList Parts = Work.split(QLatin1Char('.'));
    bool Ok = false;
    Index = Parts.last().toInt(&Ok);
    if (!Ok) {
        Index = -1;
        return false;
    }
    if (Parts.size() == 1) {
        Siblings = Back ? Document.BackSectionNodes()
                        : Document.TopSectionNodes();
    } else {
        QString ParentPath = Parts.mid(0, Parts.size() - 1)
                                 .join(QLatin1Char('.'));
        if (Back) {
            ParentPath = QStringLiteral("back:") + ParentPath;
        }
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
 * @brief Writes a reference entry's annotation.
 *
 * The <annotation> child sits after the <front>; an empty
 * annotation removes the element. Only the first annotation
 * is edited; any further ones in the file are left alone.
 * The text is plain: an annotation holding inline markup
 * loses that markup when edited here.
 * @param Entry The reference entry node.
 * @param Annotation The annotation text, or empty to remove it.
 */
static void SetReferenceAnnotation(xmlNodePtr Entry, const QString& Annotation)
{
    if (Entry == nullptr) {
        return;
    }
    xmlNodePtr AnnotationNode = DocDocument::ChildElement(Entry, "annotation");
    if (AnnotationNode != nullptr) {
        DocDocument::SetChildText(Entry, "annotation", Annotation);
        return;
    }
    if (Annotation.trimmed().isEmpty()) {
        return;
    }
    xmlNodePtr Created = xmlNewNode(nullptr, XmlChars("annotation"));
    xmlNodePtr Front = DocDocument::ChildElement(Entry, "front");
    if (Front != nullptr) {
        xmlAddNextSibling(Front, Created);
    } else {
        xmlAddChild(Entry, Created);
    }
    DocDocument::SetNodeText(Created, Annotation);
}

/**
 * @brief The displayreference elements in the back matter, in file order.
 *
 * A displayreference is a direct child of <back>, before the
 * reference lists; it is not part of any one entry (Bug 405).
 * @param Document The document.
 * @return The <displayreference> nodes, borrowed, file order.
 */
static QVector<xmlNodePtr> DisplayReferenceNodes(DocDocument& Document)
{
    return DocDocument::ChildElements(Document.BackNode(),
                                      "displayreference");
}

/**
 * @brief The display name for one anchor, from its displayreference.
 *
 * The first mapping for the anchor wins; a mapping with an
 * empty display name counts as no mapping.
 * @param Document The document.
 * @param Anchor The reference anchor.
 * @return The "to" value, or empty when no mapping exists.
 */
static QString DisplayNameForAnchor(DocDocument& Document,
                                    const QString& Anchor)
{
    QString Wanted = Anchor.trimmed();
    if (Wanted.isEmpty()) {
        return QString();
    }
    for (xmlNodePtr Node : DisplayReferenceNodes(Document)) {
        if (DocDocument::NodeAttribute(Node, "target").trimmed() ==
            Wanted) {
            return DocDocument::NodeAttribute(Node, "to").trimmed();
        }
    }
    return QString();
}

/**
 * @brief Inserts a displayreference node into the back matter.
 *
 * The node lands before the first <references> element, as
 * the schema orders them (displayreferences, then lists,
 * then sections); with no list yet it goes at the end of
 * the back matter. The back matter itself is created when
 * the document has none.
 * @param Document The document.
 * @param Node The node to insert (adopted on success).
 * @return True when the node was inserted.
 */
static bool InsertDisplayReferenceNode(DocDocument& Document,
                                       xmlNodePtr Node)
{
    if (Node == nullptr) {
        return false;
    }
    xmlNodePtr Back = Document.BackNode();
    if (Back == nullptr) {
        xmlNodePtr Rfc = Document.RfcNode();
        if (Rfc == nullptr) {
            return false;
        }
        Back = DocDocument::NewChildElement(Rfc, "back");
    }
    for (xmlNodePtr Child = Back->children; Child != nullptr;
         Child = Child->next) {
        if (Child->type == XML_ELEMENT_NODE &&
            DocDocument::NodeNameEquals(Child, "references")) {
            xmlAddPrevSibling(Child, Node);
            return true;
        }
    }
    xmlAddChild(Back, Node);
    return true;
}

/**
 * @brief Removes one displayreference node from the back matter.
 * @param Node The node to remove (freed), or nullptr.
 */
static void RemoveDisplayReferenceNode(xmlNodePtr Node)
{
    if (Node == nullptr) {
        return;
    }
    xmlUnlinkNode(Node);
    xmlFreeNode(Node);
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
 * @brief Moves a reference entry or include into the given list.
 *
 * The cite flow's move: unlike MoveReferenceToSection it does
 * not touch the selection, so the author stays in the section
 * body they were citing from. Entries go after the last entry
 * in the list; includes go at the end. A group include moves
 * as a whole, carrying every anchor its file provides.
 * @param Item The entry or include to move.
 * @param Group The list to move it into.
 * @return True when the item moved.
 */
static bool MoveReferenceIntoList(xmlNodePtr Item, xmlNodePtr Group)
{
    if (Item == nullptr || Group == nullptr || Item->parent == Group) {
        return false;
    }
    xmlUnlinkNode(Item);
    if (DocDocument::NodeNameEquals(Item, "reference")) {
        AppendReferenceEntry(Group, Item);
    } else {
        xmlAddChild(Group, Item);
    }
    return true;
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
    , _AbstractEditCurrent(false)
    , _SelectedKey()
    , _AuthorIndex(-1)
    , _AuthorGroupPath()
    , _AuthorEntry(-1)
    , _ReferenceGroupPath()
    , _ReferenceEntryIndex(-1)
    , _ReferenceAuthorIndex(-1)
    , _ReferenceIncludeIndex(-1)
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
    , _SectionAnchorEdit(nullptr)
    , _SectionBodyEdit(nullptr)
    , _SectionPreservedLabel(nullptr)
    , _SectionRequiredLabel(nullptr)
    , _ReferenceGroupNameEdit(nullptr)
    , _AddReferenceEntryButton(nullptr)
    , _AddNestedListButton(nullptr)
    , _ReferenceGroupPreservedLabel(nullptr)
    , _ReferenceIncludeLabel(nullptr)
    , _RemoveReferenceIncludeButton(nullptr)
    , _ReferenceIncludeNormativeRadio(nullptr)
    , _ReferenceIncludeInformativeRadio(nullptr)
    , _DisplayReferenceTable(nullptr)
    , _ReferenceAnchorEdit(nullptr)
    , _ReferenceTargetEdit(nullptr)
    , _ReferenceTitleEdit(nullptr)
    , _ReferenceDateDayEdit(nullptr)
    , _ReferenceDateMonthEdit(nullptr)
    , _ReferenceDateYearEdit(nullptr)
    , _ReferenceStreamCombo(nullptr)
    , _ReferenceSeriesTable(nullptr)
    , _ReferenceMoveToCombo(nullptr)
    , _ReferenceNormativeRadio(nullptr)
    , _ReferenceInformativeRadio(nullptr)
    , _ReferenceAuthorList(nullptr)
    , _ReferenceAuthorFullnameEdit(nullptr)
    , _ReferenceAuthorInitialsEdit(nullptr)
    , _ReferenceAuthorSurnameEdit(nullptr)
    , _ReferenceAuthorOrganizationEdit(nullptr)
    , _ReferenceAuthorEmailEdit(nullptr)
    , _ReferenceAuthorPhoneEdit(nullptr)
    , _ReferenceAuthorUriEdit(nullptr)
    , _ReferenceAuthorRadio(nullptr)
    , _ReferenceEditorRadio(nullptr)
    , _RemoveReferenceAuthorButton(nullptr)
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

    QMenu* ViewPublishedMenu = menuBar()->addMenu(tr("&View Published"));

    QAction* ViewRfcAction = ViewPublishedMenu->addAction(tr("&RFC..."));
    ViewRfcAction->setStatusTip(
        tr("View a published RFC as XML, HTML, text, or PDF."));
    connect(ViewRfcAction, &QAction::triggered,
            this, &MainWindow::OnViewPublishedRfc);

    QAction* ViewIdAction = ViewPublishedMenu->addAction(tr("&ID..."));
    ViewIdAction->setStatusTip(
        tr("View a published Internet-Draft as XML, HTML, text, or PDF."));
    connect(ViewIdAction, &QAction::triggered,
            this, &MainWindow::OnViewPublishedId);

    QAction* ViewIdDiffAction =
        ViewPublishedMenu->addAction(tr("ID &Difference..."));
    ViewIdDiffAction->setStatusTip(
        tr("Show the differences between two versions of an Internet-Draft."));
    connect(ViewIdDiffAction, &QAction::triggered,
            this, &MainWindow::OnViewPublishedIdDiff);

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

    QAction* FeedbackAction = HelpMenu->addAction(tr("&Feedback"));
    FeedbackAction->setStatusTip(
        tr("Open the GitHub issues page in the default browser."));
    connect(FeedbackAction, &QAction::triggered,
            this, &MainWindow::OnFeedback);

    QAction* AboutAction = HelpMenu->addAction(tr("&About InternetDraftsEditor"));
    AboutAction->setStatusTip(
        tr("Show information about this editor, including the copyright and license."));
    connect(AboutAction, &QAction::triggered, this, &MainWindow::OnAbout);

    // A one-time note, the first time the app starts, pointing at
    // Help > Feedback (Bug 494). The single-shot timer waits until
    // the window is showing before the box appears; the start
    // wizard already ran before this window was made, so the two
    // never overlap.
    QTimer::singleShot(0, this, [this]() {
        QSettings Settings;
        if (Settings.value(
                QStringLiteral("help/feedback-notice-shown"),
                false).toBool()) {
            return;
        }
        Settings.setValue(
            QStringLiteral("help/feedback-notice-shown"), true);
        QMessageBox::information(
            this, tr("Feedback"),
            tr("You can report issues and make suggestions from the "
               "Help menu: Help > Feedback opens the GitHub issues "
               "page in your default browser."));
    });

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
    _AbstractEdit->SetBibCache(&_BibCache);
    connect(_AbstractEdit, &TextEditor::TextChanged, this, &MainWindow::OnAbstractEdited);
    connect(_AbstractEdit, &TextEditor::LinkActivated, this, &MainWindow::OnLinkActivated);
    connect(_AbstractEdit, &TextEditor::MakeReferenceRequested, this,
            [this](int Action, xmlNodePtr Target, const QString& Anchor, const QString& ListName) {
                HandleMakeReference(_AbstractEdit, Action, Target, Anchor, ListName);
            });
    connect(_AbstractEdit, &TextEditor::AnchorChangeRequested, this,
            [this](const QString& OldAnchor, const QString& NewAnchor,
                   int FirstBlockPos, bool IsSourceCode,
                   const QString& PartKind) {
                auto SetAnchor = [this, FirstBlockPos, IsSourceCode,
                                  PartKind](const QString& Value) {
                    if (!PartKind.isEmpty()) {
                        _AbstractEdit->SetTablePartAnchor(FirstBlockPos,
                                                          PartKind, Value);
                    } else {
                        _AbstractEdit->SetElementAnchor(FirstBlockPos,
                                                        IsSourceCode, Value);
                    }
                };
                HandleAnchorChange(_AbstractEdit, OldAnchor, NewAnchor,
                                   SetAnchor);
            });
    // Bug 504: a figure's anchor change goes through the same
    // owner check (duplicate refused, cited delete refused,
    // cross-references renamed), with the setter writing the
    // figure's anchor.
    connect(_AbstractEdit, &TextEditor::FigureAnchorChangeRequested, this,
            [this](const QString& OldAnchor, const QString& NewAnchor,
                   int FirstBlockPos, bool* Accepted) {
                auto SetAnchor = [this, FirstBlockPos](const QString& Value) {
                    _AbstractEdit->SetFigureAnchor(FirstBlockPos, Value);
                };
                if (Accepted != nullptr) {
                    *Accepted = HandleAnchorChange(_AbstractEdit, OldAnchor,
                                                   NewAnchor, SetAnchor);
                }
            });
    // Bug 445: the artset and artwork anchors go through the
    // same owner check, with setters writing those anchors.
    connect(_AbstractEdit, &TextEditor::ArtsetAnchorChangeRequested, this,
            [this](const QString& OldAnchor, const QString& NewAnchor,
                   int FirstBlockPos, bool* Accepted) {
                auto SetAnchor = [this, FirstBlockPos](const QString& Value) {
                    _AbstractEdit->SetArtsetAnchor(FirstBlockPos, Value);
                };
                if (Accepted != nullptr) {
                    *Accepted = HandleAnchorChange(_AbstractEdit, OldAnchor,
                                                   NewAnchor, SetAnchor);
                }
            });
    connect(_AbstractEdit, &TextEditor::ArtworkAnchorChangeRequested, this,
            [this](const QString& OldAnchor, const QString& NewAnchor,
                   int FirstBlockPos, bool* Accepted) {
                auto SetAnchor = [this, FirstBlockPos](const QString& Value) {
                    _AbstractEdit->SetArtworkAttribute(
                        FirstBlockPos, QStringLiteral("anchor"), Value);
                };
                if (Accepted != nullptr) {
                    *Accepted = HandleAnchorChange(_AbstractEdit, OldAnchor,
                                                   NewAnchor, SetAnchor);
                }
            });
    _AbstractPreservedLabel = new QLabel(AbstractPageWidget);
    _AbstractPreservedLabel->setWordWrap(true);
    _AbstractPreservedLabel->setTextInteractionFlags(
        Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
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
    // The section anchor, below the title: it defaults to a
    // string derived from the section name and can be edited.
    // It commits when editing finishes (Enter, or leaving the
    // field), so a rename runs once on the final value rather
    // than on every keystroke.
    _SectionAnchorEdit = new QLineEdit(SectionPageWidget);
    _SectionAnchorEdit->setToolTip(tr("The section anchor, used by cross-references."));
    connect(_SectionAnchorEdit, &QLineEdit::editingFinished, this, &MainWindow::OnSectionAnchorEdited);
    _SectionBodyEdit = new TextEditor(SectionPageWidget);
    _SectionBodyEdit->SetSpellChecker(_SpellChecker.get());
    _SectionBodyEdit->SetDocument(&_Document);
    _SectionBodyEdit->SetBibCache(&_BibCache);
    connect(_SectionBodyEdit, &TextEditor::TextChanged, this, &MainWindow::OnSectionBodyEdited);
    connect(_SectionBodyEdit, &TextEditor::LinkActivated, this, &MainWindow::OnLinkActivated);
    connect(_SectionBodyEdit, &TextEditor::MakeReferenceRequested, this,
            [this](int Action, xmlNodePtr Target, const QString& Anchor, const QString& ListName) {
                HandleMakeReference(_SectionBodyEdit, Action, Target, Anchor, ListName);
            });
    connect(_SectionBodyEdit, &TextEditor::AnchorChangeRequested, this,
            [this](const QString& OldAnchor, const QString& NewAnchor,
                   int FirstBlockPos, bool IsSourceCode,
                   const QString& PartKind) {
                auto SetAnchor = [this, FirstBlockPos, IsSourceCode,
                                  PartKind](const QString& Value) {
                    if (!PartKind.isEmpty()) {
                        _SectionBodyEdit->SetTablePartAnchor(FirstBlockPos,
                                                             PartKind, Value);
                    } else {
                        _SectionBodyEdit->SetElementAnchor(FirstBlockPos,
                                                           IsSourceCode,
                                                           Value);
                    }
                };
                HandleAnchorChange(_SectionBodyEdit, OldAnchor, NewAnchor,
                                   SetAnchor);
            });
    // Bug 504: a figure's anchor change goes through the same
    // owner check (duplicate refused, cited delete refused,
    // cross-references renamed), with the setter writing the
    // figure's anchor.
    connect(_SectionBodyEdit, &TextEditor::FigureAnchorChangeRequested, this,
            [this](const QString& OldAnchor, const QString& NewAnchor,
                   int FirstBlockPos, bool* Accepted) {
                auto SetAnchor = [this, FirstBlockPos](const QString& Value) {
                    _SectionBodyEdit->SetFigureAnchor(FirstBlockPos, Value);
                };
                if (Accepted != nullptr) {
                    *Accepted = HandleAnchorChange(_SectionBodyEdit, OldAnchor,
                                                   NewAnchor, SetAnchor);
                }
            });
    // Bug 445: the artset and artwork anchors go through the
    // same owner check, with setters writing those anchors.
    connect(_SectionBodyEdit, &TextEditor::ArtsetAnchorChangeRequested, this,
            [this](const QString& OldAnchor, const QString& NewAnchor,
                   int FirstBlockPos, bool* Accepted) {
                auto SetAnchor = [this, FirstBlockPos](const QString& Value) {
                    _SectionBodyEdit->SetArtsetAnchor(FirstBlockPos, Value);
                };
                if (Accepted != nullptr) {
                    *Accepted = HandleAnchorChange(_SectionBodyEdit, OldAnchor,
                                                   NewAnchor, SetAnchor);
                }
            });
    connect(_SectionBodyEdit, &TextEditor::ArtworkAnchorChangeRequested, this,
            [this](const QString& OldAnchor, const QString& NewAnchor,
                   int FirstBlockPos, bool* Accepted) {
                auto SetAnchor = [this, FirstBlockPos](const QString& Value) {
                    _SectionBodyEdit->SetArtworkAttribute(
                        FirstBlockPos, QStringLiteral("anchor"), Value);
                };
                if (Accepted != nullptr) {
                    *Accepted = HandleAnchorChange(_SectionBodyEdit, OldAnchor,
                                                   NewAnchor, SetAnchor);
                }
            });
    // Bug 520: pasted <section> nodes in the section body's XML
    // tab become subsections of the current section.
    _SectionBodyEdit->SetAllowSubsections(true);
    connect(_SectionBodyEdit, &TextEditor::SubsectionsPasted, this,
            &MainWindow::OnSubsectionsPasted);
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
    SectionLayout->addWidget(new QLabel(tr("Anchor:"), SectionPageWidget));
    SectionLayout->addWidget(_SectionAnchorEdit);
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
    _ReferenceIncludeLabel = new QLabel(ReferenceGroupPageWidget);
    _ReferenceIncludeLabel->setWordWrap(true);
    _RemoveReferenceIncludeButton = new QPushButton(tr("Remove Included Reference"), ReferenceGroupPageWidget);
    _RemoveReferenceIncludeButton->setToolTip(tr("Remove the selected included reference from this list."));
    connect(_RemoveReferenceIncludeButton, &QPushButton::clicked, this, &MainWindow::OnRemoveReferenceInclude);
    // The section radios move the picked included reference into
    // the Normative or Informative nested list, creating it when
    // it is not there yet. They live in their own widget so they
    // do not join any other radio pair on this page.
    _ReferenceIncludeNormativeRadio = new QRadioButton(tr("Normative"), ReferenceGroupPageWidget);
    _ReferenceIncludeNormativeRadio->setToolTip(tr("Move the selected included reference to the Normative References list."));
    _ReferenceIncludeInformativeRadio = new QRadioButton(tr("Informative"), ReferenceGroupPageWidget);
    _ReferenceIncludeInformativeRadio->setToolTip(tr("Move the selected included reference to the Informative References list."));
    connect(_ReferenceIncludeNormativeRadio, &QRadioButton::toggled, this, &MainWindow::OnReferenceIncludeSectionChanged);
    connect(_ReferenceIncludeInformativeRadio, &QRadioButton::toggled, this, &MainWindow::OnReferenceIncludeSectionChanged);
    QWidget* ReferenceIncludeSectionWidget = new QWidget(ReferenceGroupPageWidget);
    QHBoxLayout* ReferenceIncludeSectionLayout = new QHBoxLayout(ReferenceIncludeSectionWidget);
    ReferenceIncludeSectionLayout->setContentsMargins(0, 0, 0, 0);
    ReferenceIncludeSectionLayout->addWidget(_ReferenceIncludeNormativeRadio);
    ReferenceIncludeSectionLayout->addWidget(_ReferenceIncludeInformativeRadio);
    ReferenceIncludeSectionLayout->addStretch(1);
    QHBoxLayout* GroupButtons = new QHBoxLayout();
    GroupButtons->addWidget(AddEntryButton);
    GroupButtons->addWidget(AddNestedListButton);
    GroupButtons->addWidget(RemoveGroupButton);
    GroupButtons->addWidget(_RemoveReferenceIncludeButton);
    GroupButtons->addWidget(MoveGroupUpButton);
    GroupButtons->addWidget(MoveGroupDownButton);
    GroupButtons->addStretch(1);
    // The display names are document-level, not part of one
    // list, but they are managed here, on the list page, next
    // to the entries they rename (Bug 405). The table follows
    // the series info table: two columns, edited in place,
    // with Add and Remove below.
    _DisplayReferenceTable = new QTableWidget(ReferenceGroupPageWidget);
    _DisplayReferenceTable->setColumnCount(2);
    _DisplayReferenceTable->setHorizontalHeaderLabels(
        {tr("Reference"), tr("Display as")});
    _DisplayReferenceTable->horizontalHeader()->setStretchLastSection(true);
    _DisplayReferenceTable->setToolTip(
        tr("The name shown for a reference instead of its anchor, "
           "such as TCP for RFC793."));
    connect(_DisplayReferenceTable, &QTableWidget::cellChanged,
            this, &MainWindow::OnDisplayReferenceChanged);
    QPushButton* AddDisplayReferenceButton = new QPushButton(
        tr("Add Display Name"), ReferenceGroupPageWidget);
    AddDisplayReferenceButton->setToolTip(
        tr("Add a display name for a reference."));
    connect(AddDisplayReferenceButton, &QPushButton::clicked,
            this, &MainWindow::OnAddDisplayReference);
    QPushButton* RemoveDisplayReferenceButton = new QPushButton(
        tr("Remove Display Name"), ReferenceGroupPageWidget);
    RemoveDisplayReferenceButton->setToolTip(
        tr("Remove the selected display name."));
    connect(RemoveDisplayReferenceButton, &QPushButton::clicked,
            this, &MainWindow::OnRemoveDisplayReference);
    QWidget* DisplayReferenceWidget = new QWidget(ReferenceGroupPageWidget);
    QVBoxLayout* DisplayReferenceLayout =
        new QVBoxLayout(DisplayReferenceWidget);
    DisplayReferenceLayout->setContentsMargins(0, 0, 0, 0);
    DisplayReferenceLayout->addWidget(_DisplayReferenceTable);
    QHBoxLayout* DisplayReferenceButtons = new QHBoxLayout();
    DisplayReferenceButtons->addWidget(AddDisplayReferenceButton);
    DisplayReferenceButtons->addWidget(RemoveDisplayReferenceButton);
    DisplayReferenceButtons->addStretch(1);
    DisplayReferenceLayout->addLayout(DisplayReferenceButtons);
    QFormLayout* ReferenceGroupLayout = new QFormLayout(ReferenceGroupPageWidget);
    ReferenceGroupLayout->addRow(tr("Section:"), ReferenceIncludeSectionWidget);
    ReferenceGroupLayout->addRow(tr("List name:"), _ReferenceGroupNameEdit);
    ReferenceGroupLayout->addRow(tr("Included reference:"), _ReferenceIncludeLabel);
    ReferenceGroupLayout->addRow(GroupButtons);
    ReferenceGroupLayout->addRow(tr("Display names:"), DisplayReferenceWidget);
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
    _ReferenceAnnotationEdit = new QLineEdit(ReferenceEntryPageWidget);
    _ReferenceAnnotationEdit->setToolTip(tr("Extra prose shown after this reference entry."));
    connect(_ReferenceAnnotationEdit, &QLineEdit::textEdited, this, &MainWindow::OnReferenceEntryEdited);
    _ReferenceMoveToCombo = new QComboBox(ReferenceEntryPageWidget);
    _ReferenceMoveToCombo->setToolTip(tr("Move this entry to another list."));
    connect(_ReferenceMoveToCombo,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &MainWindow::OnReferenceMoveToGroupChanged);
    // The section radios move this entry into the Normative or
    // Informative nested list under its outer list, creating
    // that list when it is not there yet. They live in their own
    // widget so they do not join the author-role radios below.
    _ReferenceNormativeRadio = new QRadioButton(tr("Normative"), ReferenceEntryPageWidget);
    _ReferenceNormativeRadio->setToolTip(tr("Move this entry to the Normative References list."));
    _ReferenceInformativeRadio = new QRadioButton(tr("Informative"), ReferenceEntryPageWidget);
    _ReferenceInformativeRadio->setToolTip(tr("Move this entry to the Informative References list."));
    connect(_ReferenceNormativeRadio, &QRadioButton::toggled, this, &MainWindow::OnReferenceSectionChanged);
    connect(_ReferenceInformativeRadio, &QRadioButton::toggled, this, &MainWindow::OnReferenceSectionChanged);
    QWidget* ReferenceSectionWidget = new QWidget(ReferenceEntryPageWidget);
    QHBoxLayout* ReferenceSectionLayout = new QHBoxLayout(ReferenceSectionWidget);
    ReferenceSectionLayout->setContentsMargins(0, 0, 0, 0);
    ReferenceSectionLayout->addWidget(_ReferenceNormativeRadio);
    ReferenceSectionLayout->addWidget(_ReferenceInformativeRadio);
    ReferenceSectionLayout->addStretch(1);
    QPushButton* AddReferenceAuthorButton = new QPushButton(tr("Add Author"), ReferenceEntryPageWidget);
    AddReferenceAuthorButton->setToolTip(tr("Add an author to this reference."));
    connect(AddReferenceAuthorButton, &QPushButton::clicked, this, &MainWindow::OnAddReferenceAuthor);
    // The authors live on this page, not as outline items: a
    // reference can have more than one, so they list here and
    // the fields below edit the selected one.
    _ReferenceAuthorList = new QListWidget(ReferenceEntryPageWidget);
    _ReferenceAuthorList->setToolTip(tr("The authors of this reference."));
    _ReferenceAuthorList->setMaximumHeight(96);
    connect(_ReferenceAuthorList, &QListWidget::currentRowChanged,
            this, &MainWindow::OnReferenceAuthorSelected);
    _ReferenceAuthorFullnameEdit = new QLineEdit(ReferenceEntryPageWidget);
    _ReferenceAuthorFullnameEdit->setToolTip(tr("The full name of the selected author."));
    connect(_ReferenceAuthorFullnameEdit, &QLineEdit::textEdited, this, &MainWindow::OnReferenceAuthorEdited);
    _ReferenceAuthorInitialsEdit = new QLineEdit(ReferenceEntryPageWidget);
    _ReferenceAuthorInitialsEdit->setToolTip(tr("The initials of the selected author."));
    connect(_ReferenceAuthorInitialsEdit, &QLineEdit::textEdited, this, &MainWindow::OnReferenceAuthorEdited);
    _ReferenceAuthorSurnameEdit = new QLineEdit(ReferenceEntryPageWidget);
    _ReferenceAuthorSurnameEdit->setToolTip(tr("The surname of the selected author."));
    connect(_ReferenceAuthorSurnameEdit, &QLineEdit::textEdited, this, &MainWindow::OnReferenceAuthorEdited);
    _ReferenceAuthorOrganizationEdit = new QLineEdit(ReferenceEntryPageWidget);
    _ReferenceAuthorOrganizationEdit->setToolTip(tr("The organization of the selected author."));
    connect(_ReferenceAuthorOrganizationEdit, &QLineEdit::textEdited, this, &MainWindow::OnReferenceAuthorEdited);
    _ReferenceAuthorEmailEdit = new QLineEdit(ReferenceEntryPageWidget);
    _ReferenceAuthorEmailEdit->setToolTip(tr("The email address of the selected author."));
    connect(_ReferenceAuthorEmailEdit, &QLineEdit::textEdited, this, &MainWindow::OnReferenceAuthorEdited);
    _ReferenceAuthorPhoneEdit = new QLineEdit(ReferenceEntryPageWidget);
    _ReferenceAuthorPhoneEdit->setToolTip(tr("The phone number of the selected author."));
    connect(_ReferenceAuthorPhoneEdit, &QLineEdit::textEdited, this, &MainWindow::OnReferenceAuthorEdited);
    _ReferenceAuthorUriEdit = new QLineEdit(ReferenceEntryPageWidget);
    _ReferenceAuthorUriEdit->setToolTip(tr("The web address of the selected author."));
    connect(_ReferenceAuthorUriEdit, &QLineEdit::textEdited, this, &MainWindow::OnReferenceAuthorEdited);
    _ReferenceAuthorRadio = new QRadioButton(tr("Author"), ReferenceEntryPageWidget);
    _ReferenceEditorRadio = new QRadioButton(tr("Editor"), ReferenceEntryPageWidget);
    connect(_ReferenceAuthorRadio, &QRadioButton::toggled, this, &MainWindow::OnReferenceAuthorRoleChanged);
    connect(_ReferenceEditorRadio, &QRadioButton::toggled, this, &MainWindow::OnReferenceAuthorRoleChanged);
    QWidget* ReferenceAuthorRoleWidget = new QWidget(ReferenceEntryPageWidget);
    QHBoxLayout* ReferenceAuthorRoleLayout = new QHBoxLayout(ReferenceAuthorRoleWidget);
    ReferenceAuthorRoleLayout->setContentsMargins(0, 0, 0, 0);
    ReferenceAuthorRoleLayout->addWidget(_ReferenceAuthorRadio);
    ReferenceAuthorRoleLayout->addWidget(_ReferenceEditorRadio);
    ReferenceAuthorRoleLayout->addStretch(1);
    _RemoveReferenceAuthorButton = new QPushButton(tr("Remove Author"), ReferenceEntryPageWidget);
    _RemoveReferenceAuthorButton->setToolTip(tr("Remove the selected author from this reference."));
    connect(_RemoveReferenceAuthorButton, &QPushButton::clicked, this, &MainWindow::OnRemoveReferenceAuthor);
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
    EntryButtons->addWidget(_RemoveReferenceAuthorButton);
    EntryButtons->addWidget(RemoveEntryButton);
    EntryButtons->addWidget(MoveEntryUpButton);
    EntryButtons->addWidget(MoveEntryDownButton);
    EntryButtons->addStretch(1);
    QFormLayout* ReferenceEntryLayout = new QFormLayout(ReferenceEntryPageWidget);
    ReferenceEntryLayout->addRow(tr("Section:"), ReferenceSectionWidget);
    ReferenceEntryLayout->addRow(tr("Anchor *:"), _ReferenceAnchorEdit);
    ReferenceEntryLayout->addRow(tr("Target:"), _ReferenceTargetEdit);
    ReferenceEntryLayout->addRow(tr("Title:"), _ReferenceTitleEdit);
    ReferenceEntryLayout->addRow(tr("Day:"), _ReferenceDateDayEdit);
    ReferenceEntryLayout->addRow(tr("Month:"), _ReferenceDateMonthEdit);
    ReferenceEntryLayout->addRow(tr("Year:"), _ReferenceDateYearEdit);
    ReferenceEntryLayout->addRow(tr("Stream:"), _ReferenceStreamCombo);
    ReferenceEntryLayout->addRow(tr("Series info:"), SeriesWidget);
    ReferenceEntryLayout->addRow(tr("Annotation:"), _ReferenceAnnotationEdit);
    ReferenceEntryLayout->addRow(tr("Move to list:"), _ReferenceMoveToCombo);
    ReferenceEntryLayout->addRow(tr("Authors:"), _ReferenceAuthorList);
    ReferenceEntryLayout->addRow(tr("Author full name *:"), _ReferenceAuthorFullnameEdit);
    ReferenceEntryLayout->addRow(tr("Author initials:"), _ReferenceAuthorInitialsEdit);
    ReferenceEntryLayout->addRow(tr("Author surname:"), _ReferenceAuthorSurnameEdit);
    ReferenceEntryLayout->addRow(tr("Author organization:"), _ReferenceAuthorOrganizationEdit);
    ReferenceEntryLayout->addRow(tr("Author email:"), _ReferenceAuthorEmailEdit);
    ReferenceEntryLayout->addRow(tr("Author phone:"), _ReferenceAuthorPhoneEdit);
    ReferenceEntryLayout->addRow(tr("Author URI:"), _ReferenceAuthorUriEdit);
    ReferenceEntryLayout->addRow(tr("Author role:"), ReferenceAuthorRoleWidget);
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
    _SettingsNoteEdit->SetBibCache(&_BibCache);
    connect(_SettingsNoteEdit, &TextEditor::TextChanged,
            this, &MainWindow::OnSettingsNoteEdited);
    connect(_SettingsNoteEdit, &TextEditor::LinkActivated, this,
            &MainWindow::OnLinkActivated);
    connect(_SettingsNoteEdit, &TextEditor::MakeReferenceRequested, this,
            [this](int Action, xmlNodePtr Target, const QString& Anchor, const QString& ListName) {
                HandleMakeReference(_SettingsNoteEdit, Action, Target, Anchor, ListName);
            });
    connect(_SettingsNoteEdit, &TextEditor::AnchorChangeRequested, this,
            [this](const QString& OldAnchor, const QString& NewAnchor,
                   int FirstBlockPos, bool IsSourceCode,
                   const QString& PartKind) {
                auto SetAnchor = [this, FirstBlockPos, IsSourceCode,
                                  PartKind](const QString& Value) {
                    if (!PartKind.isEmpty()) {
                        _SettingsNoteEdit->SetTablePartAnchor(FirstBlockPos,
                                                              PartKind,
                                                              Value);
                    } else {
                        _SettingsNoteEdit->SetElementAnchor(FirstBlockPos,
                                                            IsSourceCode,
                                                            Value);
                    }
                };
                HandleAnchorChange(_SettingsNoteEdit, OldAnchor, NewAnchor,
                                   SetAnchor);
            });
    // Bug 504: a figure's anchor change goes through the same
    // owner check (duplicate refused, cited delete refused,
    // cross-references renamed), with the setter writing the
    // figure's anchor.
    connect(_SettingsNoteEdit, &TextEditor::FigureAnchorChangeRequested, this,
            [this](const QString& OldAnchor, const QString& NewAnchor,
                   int FirstBlockPos, bool* Accepted) {
                auto SetAnchor = [this, FirstBlockPos](const QString& Value) {
                    _SettingsNoteEdit->SetFigureAnchor(FirstBlockPos, Value);
                };
                if (Accepted != nullptr) {
                    *Accepted = HandleAnchorChange(_SettingsNoteEdit, OldAnchor,
                                                   NewAnchor, SetAnchor);
                }
            });
    // Bug 445: the artset and artwork anchors go through the
    // same owner check, with setters writing those anchors.
    connect(_SettingsNoteEdit, &TextEditor::ArtsetAnchorChangeRequested, this,
            [this](const QString& OldAnchor, const QString& NewAnchor,
                   int FirstBlockPos, bool* Accepted) {
                auto SetAnchor = [this, FirstBlockPos](const QString& Value) {
                    _SettingsNoteEdit->SetArtsetAnchor(FirstBlockPos, Value);
                };
                if (Accepted != nullptr) {
                    *Accepted = HandleAnchorChange(_SettingsNoteEdit, OldAnchor,
                                                   NewAnchor, SetAnchor);
                }
            });
    connect(_SettingsNoteEdit, &TextEditor::ArtworkAnchorChangeRequested, this,
            [this](const QString& OldAnchor, const QString& NewAnchor,
                   int FirstBlockPos, bool* Accepted) {
                auto SetAnchor = [this, FirstBlockPos](const QString& Value) {
                    _SettingsNoteEdit->SetArtworkAttribute(
                        FirstBlockPos, QStringLiteral("anchor"), Value);
                };
                if (Accepted != nullptr) {
                    *Accepted = HandleAnchorChange(_SettingsNoteEdit, OldAnchor,
                                                   NewAnchor, SetAnchor);
                }
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
    // Bug 502 fix: see OpenDraftFile — the abstract editor is not
    // current for this document until its page is filled.
    _AbstractEditCurrent = false;
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
    // Bug 502 fix: the editors still hold the previous document (or
    // nothing). The abstract editor is not current for this file
    // until its page is filled.
    _AbstractEditCurrent = false;
    // Bug 528 fix: the detail editors keep their displayed text
    // across a reload -- they refill only when their page is
    // re-selected -- so opening the same file again showed the
    // discarded edits instead of the file's contents. Clear
    // them here, so no discarded text survives the load; the
    // outline rebuild below refills the shown page from the
    // fresh document, and the rest refill when they are opened.
    // Clear() blocks signals and sets _SettingParagraphs, so
    // the empty editors do not sync back into the new document.
    if (_SectionBodyEdit != nullptr) {
        _SectionBodyEdit->Clear();
    }
    if (_AbstractEdit != nullptr) {
        _AbstractEdit->Clear();
    }
    if (_SettingsNoteEdit != nullptr) {
        _SettingsNoteEdit->Clear();
    }
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
    // usable before it is edited further. Included references come
    // from the bibliography cache: fetch what is missing first,
    // then validate, so a healthy file never shows the include
    // error on load.
    QStringList Hrefs;
    for (xmlNodePtr Include :
         DocDocument::DocumentIncludeNodes(_Document.RfcNode())) {
        QString Href = DocDocument::NodeAttribute(Include, "href");
        if (!Href.isEmpty() && !Hrefs.contains(Href)) {
            Hrefs.append(Href);
        }
    }
    if (Hrefs.isEmpty()) {
        DocValidationResult Checked = ValidateAndReport(Path, false);
        if (Checked.Valid) {
            statusBar()->showMessage(tr("Opened %1 (valid).").arg(Path));
        }
    } else {
        statusBar()->showMessage(tr("Fetching bibliography entries..."));
        connect(&_BibCache, &BibCache::FetchFinished, this,
                [this, Path]() {
                    if (Path != _FilePath) {
                        return;
                    }
                    DocValidationResult Checked =
                        ValidateAndReport(Path, false);
                    if (Checked.Valid) {
                        statusBar()->showMessage(
                            tr("Opened %1 (valid).").arg(Path));
                    }
                },
                Qt::SingleShotConnection);
        _BibCache.EnsureCached(Hrefs);
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
    // Bug 520: apply pending XML edits before leaving the
    // current section, so a paste is not lost by the switch.
    // The clicked item's key is kept first: applying may add
    // subsections and rebuild the outline, which deletes the
    // clicked item itself.
    QTreeWidgetItem* Clicked = _Outline->currentItem();
    QString NewKey = (Clicked == nullptr) ? QString()
                                           : SelectionKey(Clicked);
    if (!ApplyPendingXmlEdits()) {
        // On a parse error the switch is refused; restore the
        // old selection.
        QTreeWidgetItem* Previous = FindItemByKey(nullptr, _SelectedKey);
        if (Previous != nullptr) {
            QSignalBlocker Blocker(_Outline);
            _Outline->setCurrentItem(Previous);
        }
        return;
    }
    QTreeWidgetItem* NewItem = FindItemByKey(nullptr, NewKey);
    ShowDetailsFor(NewItem);
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
            xmlNodePtr Selected = Siblings[Index];
            xmlNodePtr Added = DocDocument::NewSectionNode(tr("New Section"));
            xmlAddNextSibling(Selected, Added);
            QString NewPath = ParentListPrefix(Path) +
                QString::number(InsertAt);
            _SelectedKey = QStringLiteral("section:") + NewPath;
            SetModified(true);
            RebuildOutline();
            return;
        }
    }
    if (Parent == nullptr) {
        return;
    }
    // No section selected: the new section goes at the end of the
    // free body sections, before the required back-matter (Bug
    // 512), so it lands where it can be moved. It used to append
    // at the very end, after the required sections, where the move
    // guard then trapped it.
    QVector<xmlNodePtr> Top = _Document.TopSectionNodes();
    InsertAt = Top.size();
    for (int i = Top.size() - 1; i >= 0; --i) {
        if (!DocDocument::IsRequiredSectionTitle(
                DocDocument::SectionTitle(Top[i]))) {
            InsertAt = i + 1;
            break;
        }
    }
    xmlNodePtr Added = DocDocument::NewSectionNode(tr("New Section"));
    if (InsertAt < Top.size()) {
        xmlAddPrevSibling(Top[InsertAt], Added);
    } else {
        xmlAddChild(Parent, Added);
    }
    QString NewPath = ParentPath.isEmpty()
        ? QString::number(InsertAt)
        : ParentPath + QStringLiteral(".") + QString::number(InsertAt);
    _SelectedKey = QStringLiteral("section:") + NewPath;
    SetModified(true);
    RebuildOutline();
}

/**
 * @brief Makes every anchor in a pasted subtree unique in the
 *        document (Bug 520).
 *
 * A pasted section may carry an anchor that already exists in
 * the document (for example the same fragment pasted twice).
 * Each colliding anchor is renamed (base-2, base-3, ...), and
 * cross-references inside the pasted subtree that point at
 * the old anchor are updated to the new one, so the file stays
 * valid and the pasted references still resolve. The document
 * itself is not changed: the renames happen on the pasted
 * copy before it is added.
 * @param Root The pasted subtree; owned by the caller.
 * @param DocumentRoot The document root to check anchors against.
 */
static void MakePastedAnchorsUnique(xmlNodePtr Root, xmlNodePtr DocumentRoot)
{
    if (Root == nullptr || DocumentRoot == nullptr) {
        return;
    }
    QMap<QString, QString> Renamed;
    QSet<QString> Seen;
    // First pass: rename every anchor that collides with the
    // document or with an anchor already seen in this paste.
    std::function<void(xmlNodePtr)> Rename =
        [&](xmlNodePtr Node) {
            if (Node == nullptr) {
                return;
            }
            if (Node->type == XML_ELEMENT_NODE) {
                QString Anchor =
                    DocDocument::NodeAttribute(Node, "anchor").trimmed();
                if (!Anchor.isEmpty()) {
                    bool Collides =
                        Seen.contains(Anchor) ||
                        DocDocument::DocumentHasAnchor(DocumentRoot, Anchor);
                    if (Collides) {
                        QString Base = Anchor;
                        QString Unique = Base;
                        int Suffix = 2;
                        while (Seen.contains(Unique) ||
                               DocDocument::DocumentHasAnchor(DocumentRoot,
                                                            Unique)) {
                            Unique = Base + QStringLiteral("-") +
                                QString::number(Suffix++);
                        }
                        Renamed.insert(Anchor, Unique);
                        DocDocument::SetNodeAttribute(Node, "anchor", Unique);
                        Seen.insert(Unique);
                    } else {
                        Seen.insert(Anchor);
                    }
                }
            }
            for (xmlNodePtr Child = Node->children; Child != nullptr;
                 Child = Child->next) {
                Rename(Child);
            }
        };
    Rename(Root);
    if (Renamed.isEmpty()) {
        return;
    }
    // Second pass: point references inside the pasted subtree
    // at the renamed anchors.
    std::function<void(xmlNodePtr)> Retarget =
        [&](xmlNodePtr Node) {
            if (Node == nullptr) {
                return;
            }
            if (Node->type == XML_ELEMENT_NODE) {
                QString Target =
                    DocDocument::NodeAttribute(Node, "target").trimmed();
                if (!Target.isEmpty() && Renamed.contains(Target)) {
                    DocDocument::SetNodeAttribute(Node, "target",
                                                Renamed.value(Target));
                }
            }
            for (xmlNodePtr Child = Node->children; Child != nullptr;
                 Child = Child->next) {
                Retarget(Child);
            }
        };
    Retarget(Root);
}

void MainWindow::OnSubsectionsPasted(const QVector<xmlNodePtr>& Sections)
{
    xmlNodePtr Parent = FindSection(_SectionPath);
    if (Parent == nullptr || Sections.isEmpty()) {
        return;
    }
    for (xmlNodePtr Section : Sections) {
        if (Section == nullptr) {
            continue;
        }
        xmlNodePtr Copy = DocDocument::CopyNode(Section);
        if (Copy == nullptr) {
            continue;
        }
        MakePastedAnchorsUnique(Copy, _Document.RfcNode());
        // Subsections go after the block content, at the end of
        // the section's children (Bug 510 ordering).
        xmlAddChild(Parent, Copy);
    }
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
    // Only a top-level middle section locks: a subsection with a
    // matching title, or a top-level appendix, stays removable.
    if (IsMiddleTopLevel(Path) &&
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
    // Select a neighbor: the next sibling, the previous one, or
    // the group (middle or back) when none is left.
    if (Index < Remaining) {
        QString NextPath = ParentListPrefix(Path) + QString::number(Index);
        _SelectedKey = QStringLiteral("section:") + NextPath;
    } else if (Index > 0) {
        QString PrevPath = ParentListPrefix(Path) +
            QString::number(Index - 1);
        _SelectedKey = QStringLiteral("section:") + PrevPath;
    } else {
        _SelectedKey = IsBackPath(Path) ? QStringLiteral("back")
                                        : QStringLiteral("middle");
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
    // A required section never moves (Bug 422). A free section
    // may move past a required neighbour (Bug 512): the required
    // sections keep their relative order, only their numbers
    // shift. Only top-level middle sections lock; a move within
    // a child list, or among appendices, never crosses one.
    if (IsMiddleTopLevel(Path) &&
        DocDocument::IsRequiredSectionTitle(
            DocDocument::SectionTitle(Siblings[Index]))) {
        return;
    }
    xmlNodePtr Moved = Siblings[Index];
    xmlUnlinkNode(Moved);
    xmlAddPrevSibling(Siblings[Index - 1], Moved);
    QString NewPath = ParentListPrefix(Path) + QString::number(Index - 1);
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
    // A required section never moves (Bug 422). A free section
    // may move past a required neighbour (Bug 512): the required
    // sections keep their relative order, only their numbers
    // shift. Only top-level middle sections lock; a move within
    // a child list, or among appendices, never crosses one.
    if (IsMiddleTopLevel(Path) &&
        DocDocument::IsRequiredSectionTitle(
            DocDocument::SectionTitle(Siblings[Index]))) {
        return;
    }
    xmlNodePtr Moved = Siblings[Index];
    xmlUnlinkNode(Moved);
    xmlAddNextSibling(Siblings[Index + 1], Moved);
    QString NewPath = ParentListPrefix(Path) + QString::number(Index + 1);
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
    // Enablement mirrors RefreshSectionControls: a required
    // section never moves, and a free section may move past a
    // required neighbour (Bug 512). Keep the two computations
    // in step. Only a top-level middle section locks; a
    // top-level appendix never does (Bug 513).
    bool TopLevel = !Path.contains(QLatin1Char('.'));
    bool Required = IsMiddleTopLevel(Path) &&
        DocDocument::IsRequiredSectionTitle(
            DocDocument::SectionTitle(Siblings[Index]));
    bool CanMoveUp = (Index > 0);
    bool CanMoveDown = (Index + 1 < Siblings.size());
    if (TopLevel && Required) {
        CanMoveUp = false;
        CanMoveDown = false;
    }
    // The move and remove actions work on the section itself, so
    // they say Section for a top-level section and Subsection for
    // one below it (Bug 512). Add and Insert add a child section,
    // so they always say Subsection.
    QString MoveUpText = TopLevel ? tr("Move Section Up")
                                  : tr("Move Subsection Up");
    QString MoveDownText = TopLevel ? tr("Move Section Down")
                                    : tr("Move Subsection Down");
    QString RemoveText = TopLevel ? tr("Remove Section")
                                  : tr("Remove Subsection");
    QMenu Menu(this);
    QAction* AddAction = Menu.addAction(tr("Add Subsection"));
    connect(AddAction, &QAction::triggered, this, &MainWindow::OnAddSubsection);
    QAction* InsertAction = Menu.addAction(tr("Insert Subsection"));
    connect(InsertAction, &QAction::triggered, this, &MainWindow::OnInsertSubsection);
    QAction* RemoveAction = Menu.addAction(RemoveText);
    connect(RemoveAction, &QAction::triggered, this, &MainWindow::OnRemoveSection);
    RemoveAction->setEnabled(!Required);
    QAction* UpAction = Menu.addAction(MoveUpText);
    connect(UpAction, &QAction::triggered, this, &MainWindow::OnMoveSectionUp);
    UpAction->setEnabled(CanMoveUp);
    QAction* DownAction = Menu.addAction(MoveDownText);
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
    QString NewPath = ParentListPrefix(ParentPath) +
        QString::number(ParentIndex + 1);
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
    // Only a top-level middle section locks; a top-level
    // appendix demotes like any other free section.
    if (IsMiddleTopLevel(Path) &&
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
    QString PrevPath = ParentListPrefix(Path) + QString::number(Index - 1);
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
    // Bug 532: the PDF needs a monospace font for its figures.
    // If PDF is selected and none is installed, offer to
    // download and install Roboto Mono. The run goes ahead
    // either way; the check is advisory, not a gate.
    if (_PdfFormatCheck->isChecked()) {
        CheckMonospaceFontForPdf();
    }
    _RunFailed = false;
    _RunLog->clear();
    _RunLogPending.clear();
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
    // Bug 531: a View button is enabled only when its format
    // is checked, its output exists, and the output is not
    // older than the XML it was made from. An output older
    // than the XML is stale and must be remade, so its button
    // is greyed out until xml2rfc runs again. An output that
    // does not exist is greyed out too. When the XML has never
    // been saved there is no time to compare against, so an
    // existing output is treated as current.
    QFileInfo XmlInfo(_FilePath);
    bool XmlExists = XmlInfo.exists();
    QDateTime XmlTime = XmlInfo.lastModified();
    auto UpdateOne = [&](QPushButton* Button, const QString& Extension,
                         bool FormatChecked, const QString& OpenTip) {
        if (Button == nullptr) {
            return;
        }
        bool Exists = false;
        bool Stale = false;
        if (FormatChecked) {
            QString Path = OutputFilePath(Extension);
            Exists = !Path.isEmpty() && QFile::exists(Path);
            if (Exists && XmlExists &&
                QFileInfo(Path).lastModified() < XmlTime) {
                Stale = true;
            }
        }
        Button->setEnabled(FormatChecked && Exists && !Stale);
        if (Stale) {
            Button->setToolTip(
                tr("The %1 output is out of date -- run xml2rfc to remake it.")
                    .arg(Extension));
        } else {
            Button->setToolTip(OpenTip);
        }
    };
    UpdateOne(_TextViewButton, QStringLiteral("txt"),
              _TextFormatCheck->isChecked(),
              tr("Open the .txt output in Notepad."));
    UpdateOne(_PdfViewButton, QStringLiteral("pdf"),
              _PdfFormatCheck->isChecked(),
              tr("Open the .pdf output in the default PDF viewer."));
    UpdateOne(_HtmlViewButton, QStringLiteral("html"),
              _HtmlFormatCheck->isChecked(),
              tr("Open the .html output in the default browser."));
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

/**
 * @brief Collects the targets of every cross-reference in a tree.
 * @param Node The node to walk (borrowed).
 * @param Targets Receives the target attributes of the <xref> and
 * <relref> elements, each once. These are the names this document
 * actually cites: an anchor name belongs to the document that
 * uses it, not to the cached file.
 */
static void CollectCitedTargets(xmlNodePtr Node, QStringList& Targets)
{
    if (Node == nullptr) {
        return;
    }
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if (Child->type == XML_ELEMENT_NODE && Child->name != nullptr) {
            QString Name = QString::fromUtf8(
                reinterpret_cast<const char*>(Child->name));
            if (Name == QStringLiteral("xref") ||
                Name == QStringLiteral("relref")) {
                QString Target = DocDocument::NodeAttribute(Child, "target");
                if (!Target.isEmpty() && !Targets.contains(Target)) {
                    Targets.append(Target);
                }
            }
        }
        CollectCitedTargets(Child, Targets);
    }
}

/**
 * @brief Turns what the author typed in the Bibliography Add box
 * into the address to fetch, or empty when it is neither.
 * @param Text What was typed.
 * @return The bibliography address: an RFC number (with or without
 * the RFC prefix) becomes its bibxml address, a full http(s)
 * address is used as typed, anything else is empty.
 */
static QString BibliographyAddressForAdd(const QString& Text)
{
    QString Typed = Text.trimmed();
    if (Typed.isEmpty()) {
        return QString();
    }
    if (Typed.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
        Typed.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        return Typed;
    }
    QString Digits = Typed;
    if (Digits.startsWith(QStringLiteral("RFC"), Qt::CaseInsensitive)) {
        Digits = Digits.mid(3).trimmed();
    }
    if (Digits.isEmpty()) {
        return QString();
    }
    for (QChar Ch : Digits) {
        if (!Ch.isDigit()) {
            return QString();
        }
    }
    return QStringLiteral(
        "https://bib.ietf.org/public/rfc/bibxml/reference.RFC.") +
        Digits + QStringLiteral(".xml");
}

void MainWindow::OnBibliography()
{
    QDialog Dialog(this);
    Dialog.setWindowTitle(tr("Bibliography"));
    Dialog.resize(920, 460);

    QLabel* LocationLabel = new QLabel(tr("Cache location:"), &Dialog);
    QLineEdit* LocationEdit = new QLineEdit(_BibCache.Location(), &Dialog);
    LocationEdit->setReadOnly(true);
    QPushButton* BrowseButton = new QPushButton(tr("Browse..."), &Dialog);
    QHBoxLayout* LocationLayout = new QHBoxLayout();
    LocationLayout->addWidget(LocationLabel);
    LocationLayout->addWidget(LocationEdit, 1);
    LocationLayout->addWidget(BrowseButton);

    QTableWidget* Table = new QTableWidget(&Dialog);
    Table->setColumnCount(5);
    Table->setHorizontalHeaderLabels(QStringList()
        << tr("Reference") << tr("Target") << tr("Source")
        << tr("Fetched") << tr("Size"));
    Table->setSelectionBehavior(QAbstractItemView::SelectRows);
    Table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    Table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // The Reference title wraps in its column and the Source address
    // wraps too, so both stay readable instead of being cut off at the
    // column edge; the short columns fit their content, and the
    // Reference column takes whatever width is left. The Source
    // column starts at 280 pixels wide and can be dragged wider.
    Table->setWordWrap(true);
    Table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    Table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    Table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    Table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    Table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    Table->setColumnWidth(2, 280);
    Table->verticalHeader()->setVisible(false);
    Table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    // Fills the table from the cache index. The address rides on
    // the first column's item, so the buttons can find it again.
    // The Target column shows the name this document cites an
    // entry by, which belongs to the document, not to the cached
    // file: the entry gets a target when the document cites one
    // of the anchors it provides. That is not only the included
    // form: a manual entry provides its anchor too, and citing
    // that is a citation of this entry all the same (Bug 495:
    // RFC793 is cited from the manual entry, with no include,
    // and its Target stayed blank until this was counted).
    auto FillTable = [&]() {
        Table->setRowCount(0);
        QStringList Cited;
        CollectCitedTargets(_Document.RfcNode(), Cited);
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
            QStringList Provided = _BibCache.CachedAnchors(Entry.Url);
            if (Provided.isEmpty()) {
                Provided = Entry.Anchors;
            }
            if (Provided.isEmpty()) {
                Provided.append(
                    DocDocument::ReferenceAnchorFromIncludeHref(
                        Entry.Url));
            }
            QString Target;
            for (const QString& Anchor : Provided) {
                if (!Anchor.isEmpty() && Cited.contains(Anchor)) {
                    Target = Anchor;
                    break;
                }
            }
            Table->setItem(Row, 1, new QTableWidgetItem(Target));
            QTableWidgetItem* SourceItem = new QTableWidgetItem(Entry.Url);
            SourceItem->setToolTip(Entry.Url);
            Table->setItem(Row, 2, SourceItem);
            Table->setItem(Row, 3, new QTableWidgetItem(Entry.Fetched));
            Table->setItem(Row, 4, new QTableWidgetItem(
                tr("%1 bytes").arg(QString::number(Entry.Size))));
        }
        // Wrapped titles and addresses grow their rows to fit.
        Table->resizeRowsToContents();
    };
    FillTable();

    QLabel* StatusLabel = new QLabel(&Dialog);
    StatusLabel->setWordWrap(true);

    QLabel* AddLabel = new QLabel(tr("Add:"), &Dialog);
    QLineEdit* AddEdit = new QLineEdit(&Dialog);
    AddEdit->setPlaceholderText(
        tr("RFC number, for example 2119, or a bibliography address"));
    QPushButton* AddButton = new QPushButton(tr("Add"), &Dialog);
    AddButton->setToolTip(
        tr("Fetch the reference and add it to the cache. An RFC "
           "number is looked up at bib.ietf.org; any other "
           "bibliography address is fetched as typed."));
    QHBoxLayout* AddLayout = new QHBoxLayout();
    AddLayout->addWidget(AddLabel);
    AddLayout->addWidget(AddEdit, 1);
    AddLayout->addWidget(AddButton);

    // Insert: pick a cached entry and put it in the document (Bug 495).
    // The target name is prefilled with the entry's anchor when a row
    // is picked, and can be changed to any other anchor the entry
    // provides. The radios pick which list the include goes into.
    QLabel* InsertLabel = new QLabel(tr("Insert:"), &Dialog);
    QLineEdit* TargetEdit = new QLineEdit(&Dialog);
    TargetEdit->setPlaceholderText(tr("Target name"));
    TargetEdit->setToolTip(
        tr("The name this document will cite the entry by."));
    QRadioButton* NormativeButton =
        new QRadioButton(tr("Normative"), &Dialog);
    QRadioButton* InformativeButton =
        new QRadioButton(tr("Informative"), &Dialog);
    InformativeButton->setChecked(true);
    QPushButton* InsertButton = new QPushButton(tr("Insert"), &Dialog);
    InsertButton->setToolTip(
        tr("Add the selected entry to the References list and cite it "
           "at the cursor."));
    InsertButton->setEnabled(false);
    QHBoxLayout* InsertLayout = new QHBoxLayout();
    InsertLayout->addWidget(InsertLabel);
    InsertLayout->addWidget(TargetEdit, 1);
    InsertLayout->addWidget(NormativeButton);
    InsertLayout->addWidget(InformativeButton);
    InsertLayout->addWidget(InsertButton);

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
    Layout->addLayout(AddLayout);
    Layout->addLayout(InsertLayout);
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

    // The selected cached entry, or an entry with no address when
    // nothing is selected.
    auto SelectedEntry = [&]() {
        BibCacheEntry Found;
        for (QTableWidgetItem* Item : Table->selectedItems()) {
            if (Item->column() != 0) {
                continue;
            }
            QString Url = Item->data(Qt::UserRole).toString();
            for (const BibCacheEntry& Entry : _BibCache.Entries()) {
                if (Entry.Url == Url) {
                    return Entry;
                }
            }
            break;
        }
        return Found;
    };

    // Picking a row prefills the target name with the entry's
    // anchor and enables Insert.
    connect(Table, &QTableWidget::itemSelectionChanged, &Dialog, [&]() {
        BibCacheEntry Entry = SelectedEntry();
        InsertButton->setEnabled(!Entry.Url.isEmpty());
        if (!Entry.Url.isEmpty()) {
            TargetEdit->setText(Entry.Anchor);
        }
    });

    connect(InsertButton, &QPushButton::clicked, &Dialog, [&]() {
        BibCacheEntry Entry = SelectedEntry();
        if (Entry.Url.isEmpty()) {
            return;
        }
        QString Target = TargetEdit->text().trimmed();
        // What the entry provides: its own anchor first, then the
        // anchors inside it when it is a group. The target name
        // must be one of those, or the citation would point at
        // nothing the document defines. The list is worked out
        // first, because the ask below needs it.
        QStringList Provided = _BibCache.CachedAnchors(Entry.Url);
        if (Provided.isEmpty()) {
            Provided = Entry.Anchors;
        }
        if (Provided.isEmpty()) {
            Provided.append(
                DocDocument::ReferenceAnchorFromIncludeHref(Entry.Url));
        }
        if (Target.isEmpty()) {
            // No target name yet: it has not been made up, so ask
            // which of the anchors the entry provides to cite.
            if (Provided.isEmpty()) {
                StatusLabel->setText(
                    tr("This entry provides no anchor to cite."));
                return;
            }
            bool Ok = false;
            QString Picked = QInputDialog::getItem(
                &Dialog, tr("Insert Reference"), tr("Target name:"),
                Provided, 0, false, &Ok);
            if (!Ok) {
                return;
            }
            Target = Picked.trimmed();
            TargetEdit->setText(Target);
        }
        if (Target.isEmpty()) {
            StatusLabel->setText(tr("Enter a target name."));
            return;
        }
        if (!Provided.contains(Target)) {
            StatusLabel->setText(
                tr("This entry provides %1. Enter one of those as "
                   "the target name.")
                    .arg(Provided.join(QStringLiteral(", "))));
            return;
        }
        // The text editor the reference is cited from: the one
        // showing. Its selection is captured before the outline
        // rebuild below, which reloads the section body and drops
        // the live selection.
        TextEditor* Editor = nullptr;
        if (_SectionBodyEdit != nullptr &&
            _SectionBodyEdit->isVisible()) {
            Editor = _SectionBodyEdit;
        } else if (_AbstractEdit != nullptr &&
                   _AbstractEdit->isVisible()) {
            Editor = _AbstractEdit;
        } else if (_SettingsNoteEdit != nullptr &&
                   _SettingsNoteEdit->isVisible()) {
            Editor = _SettingsNoteEdit;
        }
        int SelectionStart = -1;
        int SelectionEnd = -1;
        if (Editor != nullptr) {
            Editor->SelectedRange(SelectionStart, SelectionEnd);
        }
        // Which form the cross-reference takes: the forms
        // published RFCs use. Asked once, up front, so the
        // same form is used whether the reference is new or
        // already there. Skipped when no text editor is
        // showing: there is no cursor to cite from.
        TextEditor::XrefForm Form = TextEditor::XrefForm::LinkText;
        QString Section;
        if (Editor != nullptr) {
            if (!TextEditor::PickXrefForm(&Dialog, Target,
                                          Editor->SelectedText(), Form,
                                          Section)) {
                return;
            }
        }
        // Cites at the cursor: with a selection the highlighted
        // text is the reference's content; with none, the anchor
        // itself is the content, so the reference shows words
        // instead of the bare marker.
        auto CiteAtCursor = [&](const QString& Anchor) {
            if (Editor == nullptr) {
                return;
            }
            if (SelectionStart < 0 || SelectionEnd <= SelectionStart) {
                Editor->InsertXrefWithText(Anchor, Anchor, Section, Form);
            } else {
                Editor->InsertXref(Anchor, Section, Form, SelectionStart,
                                   SelectionEnd);
            }
        };
        // Pushes the editor's text back into the document, so the
        // table below reads the citation just placed. The edit
        // normally syncs itself through TextChanged, but the table
        // must not depend on that having fired yet.
        auto SyncEditorToDocument = [&]() {
            if (Editor == _SectionBodyEdit) {
                OnSectionBodyEdited();
            } else if (Editor == _AbstractEdit) {
                OnAbstractEdited();
            } else if (Editor == _SettingsNoteEdit) {
                OnSettingsNoteEdited();
            }
        };
        QString Clash;
        QString Provider;
        QString ListName = NormativeButton->isChecked()
            ? QStringLiteral("Normative")
            : QStringLiteral("Informative");
        if (FindProvidedAnchorClash(Provided, Clash, Provider)) {
            if (Clash == Target) {
                // The document already provides this target:
                // move it into the list the author picked, then
                // cite the existing reference instead of a
                // duplicate.
                xmlNodePtr Group = EnsureReferenceList(ListName);
                bool Moved = false;
                if (Group != nullptr) {
                    Moved = MoveReferenceIntoList(
                        FindAnchorProviderNode(Clash), Group);
                }
                if (Moved) {
                    SetModified(true);
                    RebuildOutline();
                }
                CiteAtCursor(Clash);
                SyncEditorToDocument();
                if (Editor != nullptr) {
                    StatusLabel->setText(
                        tr("%1 already provides %2: citing the "
                           "existing reference.")
                            .arg(Provider, Clash));
                } else {
                    StatusLabel->setText(
                        tr("%1 already provides %2. Place the cursor "
                           "in the text and cite it from there.")
                            .arg(Provider, Clash));
                }
                FillTable();
                return;
            }
            // Another anchor the entry carries is already
            // provided: adding the include would define it
            // twice, so refuse instead of adding it.
            StatusLabel->setText(
                tr("Cannot add this entry: %1 already provides %2.")
                    .arg(Provider, Clash));
            return;
        }
        xmlNodePtr Group = EnsureReferenceList(ListName);
        if (Group == nullptr) {
            StatusLabel->setText(
                tr("There is no References list to add to."));
            return;
        }
        xmlNsPtr XiNs =
            DocDocument::EnsureXIncludeNamespace(_Document.RfcNode());
        if (XiNs == nullptr) {
            StatusLabel->setText(
                tr("Could not add the included reference."));
            return;
        }
        xmlNodePtr Include =
            xmlNewChild(Group, XiNs, XmlChars("include"), nullptr);
        DocDocument::SetNodeAttribute(Include, "href", Entry.Url);
        SetModified(true);
        // The citation goes to the cursor, so it is placed and
        // synced before the outline rebuild below: the rebuild
        // reloads the section body, which would move the cursor
        // and leave the sync pointing at a stale selection.
        CiteAtCursor(Target);
        SyncEditorToDocument();
        RebuildOutline();
        if (Editor != nullptr) {
            StatusLabel->setText(
                tr("Added the included reference %1 and cited it at "
                   "the cursor.")
                    .arg(Target));
        } else {
            StatusLabel->setText(
                tr("Added the included reference %1. Place the cursor "
                   "in the text and cite it from there.")
                    .arg(Target));
        }
        FillTable();
    });

    connect(AddEdit, &QLineEdit::returnPressed,
            AddButton, &QPushButton::click);
    connect(AddButton, &QPushButton::clicked, &Dialog, [&]() {
        QString Address = BibliographyAddressForAdd(AddEdit->text());
        if (Address.isEmpty()) {
            StatusLabel->setText(
                tr("Enter an RFC number (for example 2119) or a full "
                   "bibliography address."));
            return;
        }
        if (_BibCache.IsCached(Address)) {
            StatusLabel->setText(
                tr("%1 is already in the cache.").arg(Address));
            return;
        }
        StatusLabel->setText(tr("Fetching %1...").arg(Address));
        _BibCache.EnsureCached(QStringList(Address));
        AddEdit->clear();
    });

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

// View Published (Bug 493): fetching a published RFC or
// Internet-Draft from the web and opening it, or diffing two
// draft versions. The bibliography cache is not used here: a
// viewed document is a download to a temporary file, not a
// cached bibliography entry.

// How long one published-document download may run (15 seconds,
// the same as a bibliography fetch).
static const int PublishedFetchTimeoutMs = 15000;

// Bug 532: the two Roboto Mono variable fonts, from the Google
// Fonts repo. These are variable fonts (the [wght] axis), so the
// two files cover every weight — there is no separate static
// folder to fetch. Downloaded directly (no zip), so the app
// needs no zip extractor. Verified 2026-11-26: both return a
// real TrueType font.
static const char* RobotoMonoFontUrls[] = {
    "https://github.com/google/fonts/raw/main/ofl/robotomono/"
    "RobotoMono%5Bwght%5D.ttf",
    "https://github.com/google/fonts/raw/main/ofl/robotomono/"
    "RobotoMono-Italic%5Bwght%5D.ttf",
};
static const char* RobotoMonoFontNames[] = {
    "RobotoMono[wght].ttf",
    "RobotoMono-Italic[wght].ttf",
};

// The four formats a published document may be viewed in, in the
// order they are offered. Not every format exists for every
// document, so the dialog probes each address and offers only
// the ones the server answers.
struct PublishedFormat
{
    const char* Extension;
    const char* Label;
};
static const PublishedFormat PublishedFormats[] = {
    { "xml", "XML" },
    { "html", "HTML" },
    { "txt", "Text" },
    { "pdf", "PDF" },
};

// Fetches one address, waiting for the download (or the timeout).
// An empty result means the fetch failed; the reason is in Error.
static QByteArray PublishedFetch(const QUrl& Url, QString* Error)
{
    QNetworkAccessManager Network;
    QNetworkRequest Request(Url);
    Request.setTransferTimeout(PublishedFetchTimeoutMs);
    Request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("InternetDraftsEditor/1.1 "
                                     "(view published)"));
    QNetworkReply* Reply = Network.get(Request);
    QEventLoop Loop;
    QTimer Timer;
    Timer.setSingleShot(true);
    QObject::connect(&Timer, &QTimer::timeout, &Loop, &QEventLoop::quit);
    QObject::connect(Reply, &QNetworkReply::finished,
                     &Loop, &QEventLoop::quit);
    Timer.start(PublishedFetchTimeoutMs);
    Loop.exec();
    QByteArray Data;
    if (Reply->error() != QNetworkReply::NoError) {
        if (Error != nullptr) {
            *Error = Reply->errorString();
        }
    } else if (Reply->attribute(
                   QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
        if (Error != nullptr) {
            *Error = QObject::tr("The server answered %1.")
                .arg(Reply->attribute(
                         QNetworkRequest::HttpStatusCodeAttribute).toInt());
        }
    } else {
        Data = Reply->readAll();
    }
    Reply->deleteLater();
    return Data;
}

// Asks the server whether an address exists, without downloading it.
static bool PublishedExists(const QUrl& Url)
{
    QNetworkAccessManager Network;
    QNetworkRequest Request(Url);
    Request.setTransferTimeout(PublishedFetchTimeoutMs);
    Request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("InternetDraftsEditor/1.1 "
                                     "(view published)"));
    QNetworkReply* Reply = Network.head(Request);
    QEventLoop Loop;
    QTimer Timer;
    Timer.setSingleShot(true);
    QObject::connect(&Timer, &QTimer::timeout, &Loop, &QEventLoop::quit);
    QObject::connect(Reply, &QNetworkReply::finished,
                     &Loop, &QEventLoop::quit);
    Timer.start(PublishedFetchTimeoutMs);
    Loop.exec();
    bool Found = (Reply->error() == QNetworkReply::NoError) &&
        (Reply->attribute(
             QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200);
    Reply->deleteLater();
    return Found;
}

// Bug 532: true when a monospace font xml2rfc's PDF can use is
// installed. The PDF stylesheet lists Roboto Mono, Courier,
// Courier New, then generic monospace, so the three named fonts
// are the check.
bool MainWindow::HasUsableMonospaceFont() const
{
    // QFontDatabase's constructor is deprecated in Qt 6; call
    // the static functions instead.
    const QStringList Families = QFontDatabase::families();
    return Families.contains(QStringLiteral("Roboto Mono")) ||
           Families.contains(QStringLiteral("Courier New")) ||
           Families.contains(QStringLiteral("Courier"));
}

// Bug 532: when a PDF run is about to happen and no usable
// monospace font is installed, offers to download and install
// Roboto Mono. Asked at most once per session, so a "No" does
// not nag on every PDF run; a later install is picked up
// because HasUsableMonospaceFont is checked first. The run goes
// ahead either way — this is advisory, not a gate.
void MainWindow::CheckMonospaceFontForPdf()
{
    static bool OfferShown = false;
    if (OfferShown) {
        return;
    }
    if (HasUsableMonospaceFont()) {
        return;
    }
    OfferShown = true;
    QMessageBox::StandardButton Answer = QMessageBox::question(
        this, tr("Monospace font needed for PDF"),
        tr("The PDF needs a monospace font for its figures, and "
           "none is installed (Roboto Mono, Courier New, or "
           "Courier). Without one the artwork comes out in a "
           "proportional font and looks skewed.\n\n"
           "Would you like me to download and install Roboto "
           "Mono for you?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);
    if (Answer != QMessageBox::Yes) {
        QMessageBox::information(
            this, tr("Monospace font needed for PDF"),
            tr("To fix the PDF by hand, install a monospace font "
               "and re-run xml2rfc. Roboto Mono from Google Fonts "
               "works: install all of its files. Or check that "
               "Courier New is in C:\\Windows\\Fonts."));
        return;
    }
    QString Error;
    if (DownloadAndInstallRobotoMono(&Error)) {
        QMessageBox::information(
            this, tr("Roboto Mono installed"),
            tr("Roboto Mono is installed. The PDF figures should "
               "come out aligned now. If a PDF still looks skewed, "
               "check that it embeds a monospace font."));
    } else {
        QMessageBox::warning(
            this, tr("Roboto Mono install failed"),
            tr("I could not install Roboto Mono (%1). To fix the "
               "PDF by hand, install a monospace font and re-run "
               "xml2rfc. Roboto Mono from Google Fonts works: "
               "install all of its files.").arg(Error));
    }
}

// Bug 532: downloads the two Roboto Mono variable fonts and
// installs them for the current user only (no admin/UAC). The
// variable fonts cover every weight, so there is no zip to
// extract and no static folder to traverse. Returns false, with
// the reason in Error, when the download or the install fails.
bool MainWindow::DownloadAndInstallRobotoMono(QString* Error)
{
#ifdef Q_OS_WIN
    const QString FontsDir =
        QDir(qEnvironmentVariable("LOCALAPPDATA"))
            .filePath(QStringLiteral("Microsoft/Windows/Fonts"));
    if (!QDir().mkpath(FontsDir)) {
        if (Error != nullptr) {
            *Error = tr("Could not create the fonts folder.");
        }
        return false;
    }
    for (int i = 0; i < 2; ++i) {
        QString DownloadError;
        const QByteArray Data =
            PublishedFetch(QUrl(QString::fromUtf8(RobotoMonoFontUrls[i])),
                           &DownloadError);
        if (Data.isEmpty()) {
            if (Error != nullptr) {
                *Error = DownloadError.isEmpty()
                    ? tr("The download failed.")
                    : DownloadError;
            }
            return false;
        }
        // A font file starts with the TrueType scaler tag
        // (0x00010000) or the 'OTTO' tag for CFF fonts.
        // Anything else is an error page, not a font.
        const bool IsTrueType =
            Data.size() >= 4 &&
            static_cast<unsigned char>(Data[0]) == 0x00 &&
            static_cast<unsigned char>(Data[1]) == 0x01 &&
            static_cast<unsigned char>(Data[2]) == 0x00 &&
            static_cast<unsigned char>(Data[3]) == 0x00;
        if (!IsTrueType && !Data.startsWith("OTTO")) {
            if (Error != nullptr) {
                *Error = tr("The download was not a font file.");
            }
            return false;
        }
        const QString FilePath =
            QDir(FontsDir).filePath(
                QString::fromUtf8(RobotoMonoFontNames[i]));
        QFile File(FilePath);
        if (!File.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (Error != nullptr) {
                *Error = tr("Could not write %1.").arg(FilePath);
            }
            return false;
        }
        if (File.write(Data) != Data.size()) {
            if (Error != nullptr) {
                *Error = tr("Could not write %1.").arg(FilePath);
            }
            return false;
        }
        File.close();
        // Register the font for the current user. The value is
        // the file name only; Windows looks in the per-user
        // fonts folder for it.
        QSettings Registry(
            QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\"
                           "Windows NT\\CurrentVersion\\Fonts"),
            QSettings::NativeFormat);
        const QString ValueName =
            (i == 0)
                ? QStringLiteral("Roboto Mono (TrueType)")
                : QStringLiteral("Roboto Mono Italic (TrueType)");
        Registry.setValue(
            ValueName,
            QString::fromUtf8(RobotoMonoFontNames[i]));
    }
    // Tell Windows (and fontconfig) the font list changed, so a
    // new WeasyPrint process sees the new font without a restart.
    SendMessageTimeout(HWND_BROADCAST, WM_FONTCHANGE, 0, 0,
                       SMTO_ABORTIFHUNG, 5000, nullptr);
    return true;
#else
    if (Error != nullptr) {
        *Error = tr("Automatic font install is Windows only.");
    }
    return false;
#endif
}

// Downloads a published document to a temporary file and opens
// it with the system viewer. Returns false, with the reason in
// Error, when the download or the viewer fails.
static bool PublishedOpen(const QUrl& Url, const QString& FileName,
                          QString* Error)
{
    QString Folder = QStandardPaths::writableLocation(
        QStandardPaths::TempLocation) +
        QStringLiteral("/InternetDraftEditor");
    if (!QDir().mkpath(Folder)) {
        if (Error != nullptr) {
            *Error = QObject::tr("Could not create the temporary folder.");
        }
        return false;
    }
    QString FetchError;
    QByteArray Data = PublishedFetch(Url, &FetchError);
    if (Data.isEmpty()) {
        if (Error != nullptr) {
            *Error = FetchError;
        }
        return false;
    }
    QString Path = QDir(Folder).filePath(FileName);
    QFile File(Path);
    if (!File.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        File.write(Data) != Data.size()) {
        if (Error != nullptr) {
            *Error = QObject::tr("Could not write the downloaded file.");
        }
        return false;
    }
    File.close();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(Path))) {
        if (Error != nullptr) {
            *Error = QObject::tr(
                "Could not open the file with the system viewer.");
        }
        return false;
    }
    return true;
}

// Keeps a version number as two digits: "0" and "1" become "00"
// and "01", so the diff prefixes line up. Empty when the text
// holds no number at all.
static QString PublishedVersionNumber(const QString& Text)
{
    QString Digits;
    for (QChar C : Text) {
        if (C.isDigit()) {
            Digits.append(C);
        }
    }
    if (Digits.isEmpty()) {
        return QString();
    }
    bool Ok = false;
    int Number = Digits.toInt(&Ok);
    if (!Ok) {
        return QString();
    }
    return QStringLiteral("%1").arg(Number, 2, 10, QLatin1Char('0'));
}

// Builds the address of one format of an RFC. The number is the
// digits of what was typed, so "RFC 2119" and "2119" both work;
// an empty address means no number was typed.
typedef QString (*PublishedUrlBuilder)(const QString& Name,
                                       const QString& Version,
                                       const QString& Extension);

static QString PublishedRfcUrl(const QString& RawName, const QString&,
                               const QString& Extension)
{
    QString Digits;
    for (QChar C : RawName) {
        if (C.isDigit()) {
            Digits.append(C);
        }
    }
    bool Ok = false;
    int Number = Digits.toInt(&Ok);
    if (!Ok || Number <= 0) {
        return QString();
    }
    return QStringLiteral("https://www.rfc-editor.org/rfc/rfc%1.%2")
        .arg(Number).arg(Extension);
}

// Builds the address of one format of an Internet-Draft. The
// title takes a "draft-" prefix when it has none, and a version
// typed into the title is dropped, since the version has its
// own box. An empty address means no title or version was typed.
static QString PublishedIdUrl(const QString& RawName, const QString& Version,
                              const QString& Extension)
{
    QString Name = RawName.trimmed();
    if (Name.isEmpty()) {
        return QString();
    }
    QString CleanVersion = PublishedVersionNumber(Version);
    if (CleanVersion.isEmpty()) {
        return QString();
    }
    if (!Name.startsWith(QStringLiteral("draft-"))) {
        Name.prepend(QStringLiteral("draft-"));
    }
    if (Name.endsWith(QStringLiteral("-") + CleanVersion)) {
        Name.chop(CleanVersion.size() + 1);
    }
    return QStringLiteral("https://www.ietf.org/archive/id/%1-%2.%3")
        .arg(Name, CleanVersion, Extension);
}

// One dialog serves both the RFC and the Internet-Draft viewer:
// a name (and, for a draft, a version), a Check that probes
// which formats the server actually has, and a View that
// downloads the picked format and opens it with the system
// viewer. Formats that do not exist are never offered.
static void ShowPublishedViewerDialog(
    QWidget* Parent, const QString& Title, const QString& NameLabel,
    const QString& NamePlaceholder, bool HasVersion,
    PublishedUrlBuilder UrlFor, const QString& SettingsPrefix)
{
    QDialog Dialog(Parent);
    Dialog.setWindowTitle(Title);

    QLineEdit* NameEdit = new QLineEdit(&Dialog);
    NameEdit->setPlaceholderText(NamePlaceholder);
    QFormLayout* FormLayout = new QFormLayout();
    FormLayout->addRow(NameLabel, NameEdit);
    QLineEdit* VersionEdit = nullptr;
    if (HasVersion) {
        VersionEdit = new QLineEdit(&Dialog);
        VersionEdit->setPlaceholderText(QObject::tr("00"));
        FormLayout->addRow(QObject::tr("Version:"), VersionEdit);
    }

    // The last values typed here are remembered, so the dialog
    // opens where it was left.
    QSettings Remembered;
    QString LastFormat =
        Remembered.value(SettingsPrefix + QStringLiteral("/format"))
            .toString();
    NameEdit->setText(
        Remembered.value(SettingsPrefix + QStringLiteral("/name"))
            .toString());
    if (HasVersion) {
        VersionEdit->setText(
            Remembered.value(SettingsPrefix + QStringLiteral("/version"))
                .toString());
    }

    QRadioButton* FormatButtons[4];
    QVBoxLayout* FormatLayout = new QVBoxLayout();
    for (int I = 0; I < 4; ++I) {
        FormatButtons[I] = new QRadioButton(
            QString::fromLatin1(PublishedFormats[I].Label), &Dialog);
        FormatButtons[I]->setEnabled(false);
        FormatLayout->addWidget(FormatButtons[I]);
    }

    QLabel* StatusLabel = new QLabel(&Dialog);
    StatusLabel->setWordWrap(true);

    QPushButton* CheckButton =
        new QPushButton(QObject::tr("Check"), &Dialog);
    CheckButton->setToolTip(QObject::tr(
        "Find which formats exist for this document."));
    QPushButton* ViewButton = new QPushButton(QObject::tr("View"), &Dialog);
    ViewButton->setToolTip(QObject::tr(
        "Download the picked format and open it with the system viewer."));
    ViewButton->setEnabled(false);
    QPushButton* ClearButton =
        new QPushButton(QObject::tr("Clear"), &Dialog);
    ClearButton->setToolTip(QObject::tr(
        "Empty this dialog and forget the last values typed here."));
    QPushButton* CloseButton =
        new QPushButton(QObject::tr("Close"), &Dialog);
    QHBoxLayout* ButtonLayout = new QHBoxLayout();
    ButtonLayout->addWidget(CheckButton);
    ButtonLayout->addWidget(ViewButton);
    ButtonLayout->addStretch(1);
    ButtonLayout->addWidget(ClearButton);
    ButtonLayout->addWidget(CloseButton);

    QVBoxLayout* Layout = new QVBoxLayout(&Dialog);
    Layout->addLayout(FormLayout);
    Layout->addWidget(new QLabel(QObject::tr("Format:"), &Dialog));
    Layout->addLayout(FormatLayout);
    Layout->addWidget(StatusLabel);
    Layout->addLayout(ButtonLayout);

    auto CheckFormats = [&]() {
        QString Name = NameEdit->text();
        QString Version = HasVersion ? VersionEdit->text() : QString();
        if (HasVersion && PublishedVersionNumber(Version).isEmpty()) {
            StatusLabel->setText(QObject::tr("Enter a version number."));
            return;
        }
        QString Probe = UrlFor(Name, Version, QStringLiteral("txt"));
        if (Probe.isEmpty()) {
            StatusLabel->setText(HasVersion
                ? QObject::tr("Enter a draft title and a version number.")
                : QObject::tr("Enter an RFC number."));
            return;
        }
        StatusLabel->setText(
            QObject::tr("Checking which formats exist..."));
        QCoreApplication::processEvents();
        int Found = 0;
        int First = -1;
        for (int I = 0; I < 4; ++I) {
            QString Url = UrlFor(
                Name, Version,
                QString::fromLatin1(PublishedFormats[I].Extension));
            bool Exists = !Url.isEmpty() &&
                PublishedExists(QUrl(Url));
            FormatButtons[I]->setEnabled(Exists);
            if (Exists) {
                ++Found;
                if (First < 0) {
                    First = I;
                }
            } else {
                FormatButtons[I]->setChecked(false);
            }
        }
        if (Found == 0) {
            StatusLabel->setText(QObject::tr(
                "No published copy of %1 was found.")
                .arg(Name.trimmed()));
            ViewButton->setEnabled(false);
            return;
        }
        // The last format picked is checked again when it exists;
        // otherwise the first format found is checked.
        int RememberedFormat = -1;
        for (int I = 0; I < 4; ++I) {
            if (FormatButtons[I]->isEnabled() &&
                QString::fromLatin1(PublishedFormats[I].Extension) ==
                    LastFormat) {
                RememberedFormat = I;
                break;
            }
        }
        FormatButtons[RememberedFormat >= 0 ? RememberedFormat : First]
            ->setChecked(true);
        ViewButton->setEnabled(true);
        StatusLabel->setText(Found == 1
            ? QObject::tr("One format found. Pick it and press View.")
            : QObject::tr("%1 formats found. Pick one and press View.")
                .arg(Found));
    };

    QObject::connect(CheckButton, &QPushButton::clicked,
                     &Dialog, CheckFormats);
    QObject::connect(NameEdit, &QLineEdit::returnPressed,
                     CheckButton, &QPushButton::click);
    if (HasVersion) {
        QObject::connect(VersionEdit, &QLineEdit::returnPressed,
                         CheckButton, &QPushButton::click);
    }
    QObject::connect(ViewButton, &QPushButton::clicked, &Dialog, [&]() {
        QString Name = NameEdit->text();
        QString Version = HasVersion ? VersionEdit->text() : QString();
        int Picked = -1;
        for (int I = 0; I < 4; ++I) {
            if (FormatButtons[I]->isChecked()) {
                Picked = I;
                break;
            }
        }
        if (Picked < 0) {
            StatusLabel->setText(QObject::tr("Pick a format first."));
            return;
        }
        QString Extension =
            QString::fromLatin1(PublishedFormats[Picked].Extension);
        QString Url = UrlFor(Name, Version, Extension);
        QString FileName = QUrl(Url).fileName();
        StatusLabel->setText(QObject::tr("Downloading %1...")
            .arg(FileName));
        QCoreApplication::processEvents();
        QString Error;
        if (!PublishedOpen(QUrl(Url), FileName, &Error)) {
            StatusLabel->setText(QObject::tr("Could not view %1: %2")
                .arg(FileName, Error));
            return;
        }
        StatusLabel->setText(QObject::tr("Opened %1 with the system viewer.")
            .arg(FileName));
    });
    QObject::connect(ClearButton, &QPushButton::clicked, &Dialog, [&]() {
        NameEdit->clear();
        if (HasVersion) {
            VersionEdit->clear();
        }
        for (int I = 0; I < 4; ++I) {
            FormatButtons[I]->setChecked(false);
            FormatButtons[I]->setEnabled(false);
        }
        ViewButton->setEnabled(false);
        StatusLabel->clear();
        QSettings Forgotten;
        Forgotten.remove(SettingsPrefix + QStringLiteral("/name"));
        Forgotten.remove(SettingsPrefix + QStringLiteral("/version"));
        Forgotten.remove(SettingsPrefix + QStringLiteral("/format"));
    });
    QObject::connect(CloseButton, &QPushButton::clicked,
                     &Dialog, &QDialog::accept);

    // Draft titles are wide, so the Internet-Draft dialog is
    // wider than the RFC one.
    if (HasVersion) {
        Dialog.resize(680, Dialog.sizeHint().height());
    }

    Dialog.exec();

    // Remember what was typed and picked, so the dialog opens
    // where it was left. An empty box forgets its value.
    auto Remember = [&](const QString& Key, const QString& Value) {
        if (Value.isEmpty()) {
            Remembered.remove(SettingsPrefix + Key);
        } else {
            Remembered.setValue(SettingsPrefix + Key, Value);
        }
    };
    Remember(QStringLiteral("/name"), NameEdit->text());
    if (HasVersion) {
        Remember(QStringLiteral("/version"), VersionEdit->text());
    }
    QString PickedFormat;
    for (int I = 0; I < 4; ++I) {
        if (FormatButtons[I]->isChecked()) {
            PickedFormat =
                QString::fromLatin1(PublishedFormats[I].Extension);
            break;
        }
    }
    Remember(QStringLiteral("/format"), PickedFormat);
}

void MainWindow::OnViewPublishedRfc()
{
    ShowPublishedViewerDialog(this, tr("View Published RFC"),
                              tr("RFC number:"), tr("2119"),
                              false, PublishedRfcUrl,
                              QStringLiteral("viewpublished/rfc"));
}

void MainWindow::OnViewPublishedId()
{
    ShowPublishedViewerDialog(this, tr("View Published Internet-Draft"),
                              tr("Title:"),
                              tr("draft-ietf-6man-rfc4291bis"),
                              true, PublishedIdUrl,
                              QStringLiteral("viewpublished/id"));
}

// One step of the Myers diff: a line the two versions share, a
// line only the older version has, or a line only the newer one
// has.
struct PublishedDiffOp
{
    enum Kind
    {
        Same,
        OldOnly,
        NewOnly
    };
    Kind _Kind;
    QString _Line;
};

// Myers' O(ND) line diff, transcribed from a prototype proven
// against 300 random cases and two real draft versions (1625
// against 1681 lines in 0.08 seconds). The trace keeps the V
// array from before each step, so the walk back can rebuild the
// edit script.
static QVector<PublishedDiffOp> PublishedDiffOps(
    const QStringList& OldLines, const QStringList& NewLines)
{
    int N = OldLines.size();
    int M = NewLines.size();
    QMap<int, int> V;
    V.insert(1, 0);
    QVector<QMap<int, int>> Trace;
    int Found = -1;
    for (int D = 0; D <= N + M; ++D) {
        Trace.append(V);
        for (int K = -D; K <= D; K += 2) {
            int X = 0;
            if (K == -D ||
                (K != D && V.value(K - 1, 0) < V.value(K + 1, 0))) {
                X = V.value(K + 1, 0);
            } else {
                X = V.value(K - 1, 0) + 1;
            }
            int Y = X - K;
            while (X < N && Y < M && OldLines.at(X) == NewLines.at(Y)) {
                ++X;
                ++Y;
            }
            V.insert(K, X);
            if (X >= N && Y >= M) {
                Found = D;
                break;
            }
        }
        if (Found >= 0) {
            break;
        }
    }
    QVector<PublishedDiffOp> Ops;
    int X = N;
    int Y = M;
    for (int D = Found; D > 0; --D) {
        const QMap<int, int>& Before = Trace.at(D);
        int K = X - Y;
        int PrevK = 0;
        if (K == -D ||
            (K != D && Before.value(K - 1, 0) < Before.value(K + 1, 0))) {
            PrevK = K + 1;
        } else {
            PrevK = K - 1;
        }
        int PrevX = Before.value(PrevK, 0);
        int PrevY = PrevX - PrevK;
        while (X > PrevX && Y > PrevY) {
            PublishedDiffOp Op;
            Op._Kind = PublishedDiffOp::Same;
            Op._Line = OldLines.at(X - 1);
            Ops.append(Op);
            --X;
            --Y;
        }
        if (X == PrevX) {
            PublishedDiffOp Op;
            Op._Kind = PublishedDiffOp::NewOnly;
            Op._Line = NewLines.at(Y - 1);
            Ops.append(Op);
            --Y;
        } else {
            PublishedDiffOp Op;
            Op._Kind = PublishedDiffOp::OldOnly;
            Op._Line = OldLines.at(X - 1);
            Ops.append(Op);
            --X;
        }
    }
    while (X > 0 && Y > 0) {
        PublishedDiffOp Op;
        Op._Kind = PublishedDiffOp::Same;
        Op._Line = OldLines.at(X - 1);
        Ops.append(Op);
        --X;
        --Y;
    }
    return Ops;
}

// The diff text: three characters before each line — three
// spaces where the line is the same in both versions, the older
// version's number where the line is only in the older version,
// the newer version's number where it is only in the newer one.
// The ops arrive in reverse, so they are read back to front.
static QStringList PublishedDiffText(const QStringList& OldLines,
                                     const QStringList& NewLines,
                                     const QString& OldVersion,
                                     const QString& NewVersion,
                                     int* OldOnlyCount, int* NewOnlyCount)
{
    QVector<PublishedDiffOp> Ops = PublishedDiffOps(OldLines, NewLines);
    QStringList Lines;
    int OldOnly = 0;
    int NewOnly = 0;
    for (int I = Ops.size() - 1; I >= 0; --I) {
        const PublishedDiffOp& Op = Ops.at(I);
        if (Op._Kind == PublishedDiffOp::Same) {
            Lines.append(QStringLiteral("   ") + Op._Line);
        } else if (Op._Kind == PublishedDiffOp::OldOnly) {
            Lines.append(OldVersion + QStringLiteral(" ") + Op._Line);
            ++OldOnly;
        } else {
            Lines.append(NewVersion + QStringLiteral(" ") + Op._Line);
            ++NewOnly;
        }
    }
    if (OldOnlyCount != nullptr) {
        *OldOnlyCount = OldOnly;
    }
    if (NewOnlyCount != nullptr) {
        *NewOnlyCount = NewOnly;
    }
    return Lines;
}

void MainWindow::OnViewPublishedIdDiff()
{
    QDialog Dialog(this);
    Dialog.setWindowTitle(tr("Compare Internet-Draft Versions"));

    QLineEdit* TitleEdit = new QLineEdit(&Dialog);
    TitleEdit->setPlaceholderText(tr("draft-ietf-6man-rfc4291bis"));
    QLineEdit* OldEdit = new QLineEdit(&Dialog);
    OldEdit->setPlaceholderText(tr("00"));
    QLineEdit* NewEdit = new QLineEdit(&Dialog);
    NewEdit->setPlaceholderText(tr("01"));
    QFormLayout* FormLayout = new QFormLayout();
    FormLayout->addRow(tr("Title:"), TitleEdit);
    FormLayout->addRow(tr("Older version:"), OldEdit);
    FormLayout->addRow(tr("Newer version:"), NewEdit);

    // The last values typed here are remembered, so the dialog
    // opens where it was left.
    QSettings Remembered;
    TitleEdit->setText(Remembered.value(
        QStringLiteral("viewpublished/diff/name")).toString());
    OldEdit->setText(Remembered.value(
        QStringLiteral("viewpublished/diff/old")).toString());
    NewEdit->setText(Remembered.value(
        QStringLiteral("viewpublished/diff/new")).toString());

    QLabel* StatusLabel = new QLabel(&Dialog);
    StatusLabel->setWordWrap(true);

    QPushButton* CompareButton = new QPushButton(tr("Compare"), &Dialog);
    QPushButton* ClearButton = new QPushButton(tr("Clear"), &Dialog);
    ClearButton->setToolTip(tr(
        "Empty this dialog and forget the last values typed here."));
    QPushButton* CloseButton = new QPushButton(tr("Close"), &Dialog);
    QHBoxLayout* ButtonLayout = new QHBoxLayout();
    ButtonLayout->addWidget(CompareButton);
    ButtonLayout->addStretch(1);
    ButtonLayout->addWidget(ClearButton);
    ButtonLayout->addWidget(CloseButton);

    QVBoxLayout* Layout = new QVBoxLayout(&Dialog);
    Layout->addLayout(FormLayout);
    Layout->addWidget(StatusLabel);
    Layout->addLayout(ButtonLayout);

    connect(CompareButton, &QPushButton::clicked, &Dialog, [&]() {
        QString OldVersion = PublishedVersionNumber(OldEdit->text());
        QString NewVersion = PublishedVersionNumber(NewEdit->text());
        if (TitleEdit->text().trimmed().isEmpty() ||
            OldVersion.isEmpty() || NewVersion.isEmpty()) {
            StatusLabel->setText(
                tr("Enter a draft title and two version numbers."));
            return;
        }
        if (OldVersion == NewVersion) {
            StatusLabel->setText(
                tr("The two versions are the same; there is nothing "
                   "to compare."));
            return;
        }
        // The older version is the smaller number, whichever box
        // it was typed in.
        if (OldVersion > NewVersion) {
            OldVersion.swap(NewVersion);
        }
        QString Name = TitleEdit->text().trimmed();
        if (!Name.startsWith(QStringLiteral("draft-"))) {
            Name.prepend(QStringLiteral("draft-"));
        }
        QString OldUrl = QStringLiteral(
            "https://www.ietf.org/archive/id/%1-%2.txt")
            .arg(Name, OldVersion);
        QString NewUrl = QStringLiteral(
            "https://www.ietf.org/archive/id/%1-%2.txt")
            .arg(Name, NewVersion);
        StatusLabel->setText(
            tr("Fetching version %1...").arg(OldVersion));
        QCoreApplication::processEvents();
        QString Error;
        QByteArray OldData = PublishedFetch(QUrl(OldUrl), &Error);
        if (OldData.isEmpty()) {
            StatusLabel->setText(
                tr("Could not fetch version %1: %2")
                .arg(OldVersion, Error));
            return;
        }
        StatusLabel->setText(
            tr("Fetching version %1...").arg(NewVersion));
        QCoreApplication::processEvents();
        QByteArray NewData = PublishedFetch(QUrl(NewUrl), &Error);
        if (NewData.isEmpty()) {
            StatusLabel->setText(
                tr("Could not fetch version %1: %2")
                .arg(NewVersion, Error));
            return;
        }
        StatusLabel->setText(tr("Comparing..."));
        QCoreApplication::processEvents();
        QString OldText = QString::fromUtf8(OldData);
        QString NewText = QString::fromUtf8(NewData);
        OldText.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        NewText.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        OldText.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        NewText.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        int OldOnly = 0;
        int NewOnly = 0;
        QStringList Lines = PublishedDiffText(
            OldText.split(QLatin1Char('\n')),
            NewText.split(QLatin1Char('\n')),
            OldVersion, NewVersion, &OldOnly, &NewOnly);

        QDialog Result(&Dialog);
        Result.setWindowTitle(tr("Differences: %1-%2 vs %1-%3")
            .arg(Name, OldVersion, NewVersion));
        Result.resize(900, 600);
        QLabel* SummaryLabel = new QLabel(
            tr("%1 line(s) only in version %2, %3 line(s) only in "
               "version %4. A line with three spaces is the same in "
               "both versions.")
            .arg(OldOnly).arg(OldVersion).arg(NewOnly).arg(NewVersion),
            &Result);
        SummaryLabel->setWordWrap(true);
        QPlainTextEdit* TextEdit = new QPlainTextEdit(&Result);
        TextEdit->setReadOnly(true);
        TextEdit->setFont(
            QFontDatabase::systemFont(QFontDatabase::FixedFont));
        TextEdit->setPlainText(Lines.join(QLatin1Char('\n')));
        QLabel* SaveStatusLabel = new QLabel(&Result);
        SaveStatusLabel->setWordWrap(true);
        QPushButton* SaveAsButton =
            new QPushButton(tr("Save As..."), &Result);
        SaveAsButton->setToolTip(
            tr("Save these differences to a text file."));
        QPushButton* ResultCloseButton =
            new QPushButton(tr("Close"), &Result);
        connect(ResultCloseButton, &QPushButton::clicked,
                &Result, &QDialog::accept);
        connect(SaveAsButton, &QPushButton::clicked, &Result, [&]() {
            QString DefaultName = Name + QStringLiteral("-") +
                OldVersion + QStringLiteral("-") + NewVersion +
                QStringLiteral(".txt");
            QString DefaultPath = QDir(QStandardPaths::writableLocation(
                QStandardPaths::DocumentsLocation))
                .filePath(DefaultName);
            QString Path = QFileDialog::getSaveFileName(
                &Result, tr("Save differences"), DefaultPath,
                tr("Text files (*.txt)"));
            if (Path.isEmpty()) {
                return;
            }
            QFile File(Path);
            if (!File.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                SaveStatusLabel->setText(
                    tr("Could not write %1.").arg(Path));
                return;
            }
            QByteArray Data = TextEdit->toPlainText().toUtf8();
            if (File.write(Data) != Data.size()) {
                SaveStatusLabel->setText(
                    tr("Could not write %1.").arg(Path));
                File.close();
                return;
            }
            File.close();
            SaveStatusLabel->setText(tr("Saved to %1.").arg(Path));
        });
        QHBoxLayout* ResultButtonLayout = new QHBoxLayout();
        ResultButtonLayout->addStretch(1);
        ResultButtonLayout->addWidget(SaveAsButton);
        ResultButtonLayout->addWidget(ResultCloseButton);
        QVBoxLayout* ResultLayout = new QVBoxLayout(&Result);
        ResultLayout->addWidget(SummaryLabel);
        ResultLayout->addWidget(TextEdit, 1);
        ResultLayout->addWidget(SaveStatusLabel);
        ResultLayout->addLayout(ResultButtonLayout);
        StatusLabel->setText(tr("Done."));
        Result.exec();
    });
    connect(ClearButton, &QPushButton::clicked, &Dialog, [&]() {
        TitleEdit->clear();
        OldEdit->clear();
        NewEdit->clear();
        StatusLabel->clear();
        QSettings Forgotten;
        Forgotten.remove(QStringLiteral("viewpublished/diff/name"));
        Forgotten.remove(QStringLiteral("viewpublished/diff/old"));
        Forgotten.remove(QStringLiteral("viewpublished/diff/new"));
    });
    connect(CloseButton, &QPushButton::clicked, &Dialog, &QDialog::accept);

    // Draft titles are wide, so this dialog is wider than the
    // RFC one.
    Dialog.resize(680, Dialog.sizeHint().height());

    Dialog.exec();

    // Remember what was typed, so the dialog opens where it was
    // left. An empty box forgets its value.
    auto Remember = [&](const QString& Key, const QString& Value) {
        if (Value.isEmpty()) {
            Remembered.remove(
                QStringLiteral("viewpublished/diff") + Key);
        } else {
            Remembered.setValue(
                QStringLiteral("viewpublished/diff") + Key, Value);
        }
    };
    Remember(QStringLiteral("/name"), TitleEdit->text());
    Remember(QStringLiteral("/old"), OldEdit->text());
    Remember(QStringLiteral("/new"), NewEdit->text());
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
    // Bug 518: the project page is a clickable link. The box
    // uses rich text, and the link opens in the default browser.
    // Only the link carries a color: the body text is left to
    // the label, which reads on the dark background as it did
    // before. The link gets an explicit light blue, because the
    // default link color is dark on dark here (the app's dark
    // look comes from a stylesheet, not a dark palette, so the
    // palette's link color stays dark). Wrapping the whole text
    // in the palette's window-text color was tried and was
    // backwards: that color is black here, so it made all the
    // text dark on dark.
    QMessageBox box(this);
    box.setWindowTitle(tr("About InternetDraftsEditor"));
    // RiverExplorer logo in the About dialog. The logo is a Qt
    // resource (images.qrc); if it is missing the dialog still
    // works, just without the picture.
    QPixmap LogoPixmap(QStringLiteral(":/images/RiverExplorer-logo.png"));
    if (!LogoPixmap.isNull()) {
        box.setIconPixmap(
            LogoPixmap.scaled(120, 120, Qt::KeepAspectRatio,
                              Qt::SmoothTransformation));
    }
    box.setTextFormat(Qt::RichText);
    box.setText(
        tr("InternetDraftsEditor %1<br>"
           "Built %2 %3<br><br>"
           "An editor for IETF Internet-Drafts in RFCXML (xml2rfc) format.<br><br>"
           "Project page: <a href=\"https://github.com/RiverExplorer/InternetDraftEditor\" "
           "style=\"color: #6EA8FE;\">"
           "https://github.com/RiverExplorer/InternetDraftEditor</a><br><br>"
           "Copyright (c) 2026 RiverExplorer LLC<br>"
           "RiverExplorer%4 is a registered trademark of "
           "Douglas Mark Royer.<br><br>"
           "MIT License — free to use and modify, keep the copyright "
           "notice.<br><br>"
           "Permission is hereby granted, free of charge, to any person "
           "obtaining a copy of this software and associated documentation "
           "files (the \"Software\"), to deal in the Software without "
           "restriction, including without limitation the rights to use, "
           "copy, modify, merge, publish, distribute, sublicense, and/or "
           "sell copies of the Software, and to permit persons to whom the "
           "Software is furnished to do so, subject to the following "
           "conditions:<br><br>"
           "The above copyright notice and this permission notice shall be "
           "included in all copies or substantial portions of the "
           "Software.<br><br>"
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
    // A QMessageBox label does not open external links on its
    // own: the text label (named "qt_msgbox_label" by Qt) needs
    // openExternalLinks for the click to reach the browser.
    QLabel* Label =
        box.findChild<QLabel*>(QStringLiteral("qt_msgbox_label"));
    if (Label != nullptr) {
        Label->setOpenExternalLinks(true);
    }
    box.addButton(QMessageBox::Ok);
    box.exec();
}

void MainWindow::OnFeedback()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral(
        "https://github.com/RiverExplorer/InternetDraftEditor/issues")));
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
               "Put the schemas folder beside InternetDraftsEditor.exe."));
        statusBar()->showMessage(tr("Validation skipped: the v3 schema was not found."));
    } else {
        Result = ValidateRfcXml(Path, Schema);
        // The filter only runs when the open document is the file
        // being checked: its include lines must match the file's.
        if (Path == _FilePath && !_Modified) {
            ResolvedIncludes = FilterIncludeErrors(Result);
            ReportAnchorCollisions(Result);
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
            // The anchor comes from the cached file itself: a
            // BCP include is BCP14, not BCP.14 from the file
            // name. The derived anchor is only the fallback.
            QString CachedAnchor = _BibCache.CachedAnchor(Href);
            Resolved[Href] = CachedAnchor.isEmpty()
                ? DocDocument::ReferenceAnchorFromIncludeHref(Href)
                : CachedAnchor;
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

void MainWindow::ReportAnchorCollisions(DocValidationResult& Result)
{
    QMap<QString, QStringList> Provided = ProvidedAnchorProviders();
    QStringList Collisions;
    for (auto It = Provided.constBegin(); It != Provided.constEnd(); ++It) {
        if (It.value().size() < 2) {
            continue;
        }
        if (It.value().size() == 2) {
            Collisions.append(tr("%1 and %2 both provide the reference \"%3\". Remove one of them.")
                .arg(It.value().first(), It.value().last(), It.key()));
        } else {
            Collisions.append(tr("%1 all provide the reference \"%2\". Remove all but one of them.")
                .arg(It.value().join(QStringLiteral(", ")), It.key()));
        }
    }
    if (!Collisions.isEmpty()) {
        Result.Errors.append(Collisions);
        Result.Valid = false;
    }
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
    // Bug 541: flush any partial line AppendRunLog was holding
    // (a final line with no trailing newline). It goes through
    // the same filter, so a trailing GIO warning is dropped
    // here too.
    if (!_RunLogPending.isEmpty()) {
        if (!_RunLogPending.contains(QStringLiteral("GLib-GIO-WARNING"))) {
            _RunLog->moveCursor(QTextCursor::End);
            _RunLog->insertPlainText(_RunLogPending + QStringLiteral("\n"));
            _RunLog->moveCursor(QTextCursor::End);
        }
        _RunLogPending.clear();
    }
    // A run may have produced the outputs the View buttons open.
    UpdateViewButtons();
    // Bug 539: xml2rfc leaves a tracker.log in the draft folder
    // on Windows (its debug module falls back to a file log
    // when syslog is missing). Nothing reads it, so remove it
    // after every run; a no-op when the file is not there.
    QFile::remove(QDir(QFileInfo(_FilePath).absolutePath())
                      .filePath(QStringLiteral("tracker.log")));
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
    QVector<xmlNodePtr> Nodes = _AbstractEdit->BodyNodes();
    // Bug 502 fix: ignore a sync from an editor that has not been
    // filled for the current document. A stale editor (empty, or
    // holding a previous document's text) must not overwrite the
    // loaded abstract. The nodes built above are dropped, not
    // adopted, so they are freed here.
    if (!_AbstractEditCurrent) {
        for (xmlNodePtr Node : Nodes) {
            xmlFreeNode(Node);
        }
        return;
    }
    _Document.SetAbstractBody(Nodes,
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
    // The anchor default is derived from the title, so while
    // the section carries no stored anchor the field follows
    // the title. Once an anchor is stored, it is independent
    // of the title and is left alone.
    if (DocDocument::NodeAttribute(Section, "anchor").trimmed().isEmpty()) {
        QSignalBlocker BlockAnchor(_SectionAnchorEdit);
        _SectionAnchorEdit->setText(SlugAnchor(Text));
    }
}

void MainWindow::OnSectionAnchorEdited()
{
    xmlNodePtr Section = FindSection(_SectionPath);
    if (Section == nullptr) {
        return;
    }
    // The old anchor is the one stored on the section, not the
    // value the field showed: the field shows the derived
    // default while no anchor is stored, and no reference can
    // cite a default that was never written.
    QString Old = DocDocument::NodeAttribute(Section, "anchor").trimmed();
    QString New = _SectionAnchorEdit->text().trimmed();
    auto SetAnchor = [Section](const QString& Value) {
        DocDocument::SetNodeAttribute(Section, "anchor", Value);
    };
    if (!HandleAnchorChange(_SectionBodyEdit, Old, New, SetAnchor)) {
        // Refused (a duplicate, or a cited anchor that cannot
        // be deleted): put the field back to the anchor in
        // force, or the derived default when none is stored.
        QSignalBlocker BlockAnchor(_SectionAnchorEdit);
        if (Old.isEmpty()) {
            _SectionAnchorEdit->setText(
                SlugAnchor(DocDocument::SectionTitle(Section)));
        } else {
            _SectionAnchorEdit->setText(Old);
        }
    }
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

/**
 * @brief The outline path of a section node (Bug 529).
 * @param Wanted The section node to find, borrowed.
 * @param Sections The sections to search, borrowed.
 * @param Prefix The path prefix ("back:" for appendices, empty otherwise).
 * @param Path Receives the path ("0.1", "back:0").
 * @return True when the node is among these sections.
 */
static bool FindSectionPath(xmlNodePtr Wanted,
                            const QVector<xmlNodePtr>& Sections,
                            const QString& Prefix, QString& Path)
{
    for (int i = 0; i < Sections.size(); ++i) {
        QString Here = Prefix + QString::number(i);
        if (Sections[i] == Wanted) {
            Path = Here;
            return true;
        }
        if (FindSectionPath(Wanted,
                            DocDocument::ChildSectionNodes(Sections[i]),
                            Here + QStringLiteral("."), Path)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief The outline key of a reference entry node (Bug 529).
 * @param Wanted The <reference> node to find, borrowed.
 * @param Groups The reference lists to search, borrowed.
 * @param GroupPath The list's path so far (empty at the top).
 * @param Key Receives the key ("refentry:0.2").
 * @return True when the node is an entry in these lists.
 */
static bool FindReferenceKey(xmlNodePtr Wanted,
                             const QVector<xmlNodePtr>& Groups,
                             const QString& GroupPath, QString& Key)
{
    for (int g = 0; g < Groups.size(); ++g) {
        QString Here = GroupPath.isEmpty()
            ? QString::number(g)
            : GroupPath + QStringLiteral(".") + QString::number(g);
        if (FindReferenceKey(Wanted,
                             DocDocument::NestedReferenceGroups(Groups[g]),
                             Here, Key)) {
            return true;
        }
        QVector<xmlNodePtr> Entries = DocDocument::ReferenceEntries(Groups[g]);
        for (int e = 0; e < Entries.size(); ++e) {
            if (Entries[e] == Wanted) {
                Key = QStringLiteral("refentry:") + Here +
                      QStringLiteral(".") + QString::number(e);
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief The outline key of an included reference by its anchor (Bug 529).
 *
 * An included reference carries no anchor attribute: its anchor
 * is derived from its href, so it is found by href, not by node.
 * @param Anchor The anchor to look for.
 * @param Groups The reference lists to search, borrowed.
 * @param GroupPath The list's path so far (empty at the top).
 * @param Key Receives the key ("refinclude:0.1").
 * @return True when an include in these lists carries the anchor.
 */
static bool FindIncludeKey(const QString& Anchor,
                           const QVector<xmlNodePtr>& Groups,
                           const QString& GroupPath, QString& Key)
{
    for (int g = 0; g < Groups.size(); ++g) {
        QString Here = GroupPath.isEmpty()
            ? QString::number(g)
            : GroupPath + QStringLiteral(".") + QString::number(g);
        if (FindIncludeKey(Anchor,
                           DocDocument::NestedReferenceGroups(Groups[g]),
                           Here, Key)) {
            return true;
        }
        QVector<xmlNodePtr> Includes =
            DocDocument::ReferenceIncludeNodes(Groups[g]);
        for (int i = 0; i < Includes.size(); ++i) {
            QString Href = DocDocument::NodeAttribute(Includes[i], "href");
            if (DocDocument::ReferenceAnchorFromIncludeHref(Href) == Anchor) {
                Key = QStringLiteral("refinclude:") + Here +
                      QStringLiteral(".") + QString::number(i);
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief The outline path of a reference list node (Bug 529).
 * @param Wanted The <references> node to find, borrowed.
 * @param Groups The reference lists to search, borrowed.
 * @param Prefix The list's path so far (empty at the top).
 * @param Path Receives the path ("0", "0.1").
 * @return True when the node is among these lists.
 */
static bool FindGroupPath(xmlNodePtr Wanted,
                          const QVector<xmlNodePtr>& Groups,
                          const QString& Prefix, QString& Path)
{
    for (int g = 0; g < Groups.size(); ++g) {
        QString Here = Prefix.isEmpty()
            ? QString::number(g)
            : Prefix + QStringLiteral(".") + QString::number(g);
        if (Groups[g] == Wanted) {
            Path = Here;
            return true;
        }
        if (FindGroupPath(Wanted,
                          DocDocument::NestedReferenceGroups(Groups[g]),
                          Here, Path)) {
            return true;
        }
    }
    return false;
}

void MainWindow::OnLinkActivated(const QString& LinkName,
                                 const QString& Target)
{
    if (LinkName == QLatin1String("eref")) {
        // An external link never opens without asking: the
        // pop-up shows the link, with Yes and Cancel.
        QMessageBox::StandardButton Answer = QMessageBox::question(
            this, tr("Open Link"),
            tr("Open this link in your browser?\n\n%1").arg(Target),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (Answer == QMessageBox::Yes) {
            QDesktopServices::openUrl(QUrl(Target));
        }
        return;
    }
    NavigateToAnchor(Target);
}

void MainWindow::NavigateToAnchor(const QString& Anchor)
{
    QString Key = OutlineKeyForAnchor(Anchor);
    QTreeWidgetItem* Item = Key.isEmpty()
        ? nullptr : FindItemByKey(nullptr, Key);
    if (Item == nullptr) {
        statusBar()->showMessage(
            tr("No element in this document has the anchor \"%1\".")
                .arg(Anchor));
        return;
    }
    _Outline->setCurrentItem(Item);
    ShowDetailsFor(Item);
    _Outline->scrollToItem(Item);
}

QString MainWindow::OutlineKeyForAnchor(const QString& Anchor) const
{
    xmlNodePtr Node = DocDocument::FindNodeByAnchor(_Document.RfcNode(), Anchor);
    if (Node != nullptr) {
        // A section selects its own outline item.
        if (DocDocument::NodeNameEquals(Node, "section")) {
            QString Path;
            if (FindSectionPath(Node, _Document.TopSectionNodes(),
                                QString(), Path) ||
                FindSectionPath(Node, _Document.BackSectionNodes(),
                                QStringLiteral("back:"), Path)) {
                return QStringLiteral("section:") + Path;
            }
            return QString();
        }
        // A reference entry selects its list item.
        if (DocDocument::NodeNameEquals(Node, "reference")) {
            QString Key;
            if (FindReferenceKey(Node, _Document.ReferenceGroupNodes(),
                                 QString(), Key)) {
                return Key;
            }
            return QString();
        }
        // Anything else (a figure, table, list, or sourcecode in
        // a section body, for example) selects its containing
        // section, or the reference list it lives in.
        for (xmlNodePtr At = Node->parent; At != nullptr; At = At->parent) {
            if (DocDocument::NodeNameEquals(At, "section")) {
                QString Path;
                if (FindSectionPath(At, _Document.TopSectionNodes(),
                                    QString(), Path) ||
                    FindSectionPath(At, _Document.BackSectionNodes(),
                                    QStringLiteral("back:"), Path)) {
                    return QStringLiteral("section:") + Path;
                }
                return QString();
            }
            if (DocDocument::NodeNameEquals(At, "references")) {
                QString Path;
                if (FindGroupPath(At, _Document.ReferenceGroupNodes(),
                                  QString(), Path)) {
                    return QStringLiteral("refgroup:") + Path;
                }
                return QString();
            }
        }
        return QString();
    }
    // An included reference carries no anchor attribute: its
    // anchor is derived from its href, so it is found by href.
    QString Key;
    if (FindIncludeKey(Anchor, _Document.ReferenceGroupNodes(),
                       QString(), Key)) {
        return Key;
    }
    return QString();
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

    // The appendix sections, after the references, shown like
    // sections (Bug 513). Their paths carry a "back:" prefix so
    // they never collide with the middle paths.
    QVector<xmlNodePtr> BackSections = _Document.BackSectionNodes();
    for (int i = 0; i < BackSections.size(); ++i) {
        AddSectionItem(Back, BackSections[i],
                       QStringLiteral("back:") + QString::number(i));
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
    if (IsBackPath(Path)) {
        // Appendices are lettered, matching what xml2rfc prints:
        // "Appendix A" at the top level, "A.1" below it.
        QString Work = Path.mid(QStringLiteral("back:").size());
        QStringList Parts = Work.split(QLatin1Char('.'));
        QString Letter = DocDocument::AppendixLetter(Parts.first().toInt());
        if (Parts.size() == 1) {
            return QStringLiteral("Appendix ") + Letter;
        }
        QStringList Rest;
        for (int i = 1; i < Parts.size(); ++i) {
            Rest.append(QString::number(Parts[i].toInt() + 1));
        }
        return Letter + QStringLiteral(".") + Rest.join(QLatin1Char('.'));
    }
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
        // The label leads with the words that cite the entry, so
        // the author can match what they tagged to the entry it
        // points at; an entry nothing cites keeps its plain label.
        // A mapped entry lists under its display name (Bug 405):
        // the list reads the way the rendered references do.
        QString Anchor = DocDocument::NodeAttribute(Entry, "anchor");
        QString Label = CitedLabelPrefix(Anchor) +
            DocDocument::ReferenceLabel(
                Entry, DisplayNameForAnchor(_Document, Anchor));
        EntryItem->setText(0, Label.isEmpty() ? tr("(untitled)") : Label);
        EntryItem->setData(0, Qt::UserRole, KindReferenceEntry);
        EntryItem->setData(0, Qt::UserRole + 1, Path);
        EntryItem->setData(0, Qt::UserRole + 2, e);
    }
    // Included references are entries too, fetched by xml2rfc
    // rather than written in the file: they list under their
    // group by the anchor their href carries, marked included
    // so they are not mistaken for an editable entry.
    QVector<xmlNodePtr> Includes = DocDocument::ReferenceIncludeNodes(Group);
    for (int i = 0; i < Includes.size(); ++i) {
        xmlNodePtr Include = Includes[i];
        QTreeWidgetItem* IncludeItem = new QTreeWidgetItem(GroupItem);
        IncludeItem->setText(0, IncludeLabel(Include));
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
            bool TopLevel = IsMiddleTopLevel(Path);
            QString Title = DocDocument::SectionTitle(Siblings[Index]);
            Required = TopLevel &&
                DocDocument::IsRequiredSectionTitle(Title);
            Recommended = TopLevel &&
                DocDocument::IsRecommendedSectionTitle(Title);
            CanMoveUp = (Index > 0);
            CanMoveDown = (Index + 1 < Siblings.size());
            if (TopLevel && Required) {
                // A required section never moves (Bug 422). A free
                // section may move past a required neighbour (Bug
                // 512): the required sections keep their relative
                // order, only their numbers shift as free sections
                // move past them.
                CanMoveUp = false;
                CanMoveDown = false;
            }
        }
    }
    // Add and Insert stay enabled: adding a section never moves a
    // required section relative to an existing one. Add is never
    // disabled; with no section selected it goes at the end of the
    // free body sections, before the required back-matter (Bug 512).
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
        // Bug 502 fix: from here on the editor holds this document's
        // abstract, so its edits may sync back into the model.
        _AbstractEditCurrent = true;
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
        _ReferencesLabel->setText(Text);
        _Details->setCurrentIndex(DetailReferences);
        break;
    }
    case KindReferenceGroup:
        _ReferenceGroupPath = Item->data(0, Qt::UserRole + 1).toString();
        _ReferenceIncludeIndex = -1;
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
        // holds details this version does not edit, and offers
        // the remove for the selected include.
        _ReferenceGroupPath = Item->data(0, Qt::UserRole + 1).toString();
        _ReferenceIncludeIndex = Item->data(0, Qt::UserRole + 2).toInt();
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

QString MainWindow::CitedLabelPrefix(const QString& Anchor)
{
    QStringList Cited = DocDocument::CitedTextsForAnchor(_Document.RfcNode(), Anchor);
    if (Cited.isEmpty()) {
        return QString();
    }
    QStringList Shown = Cited.mid(0, 3);
    QString Head = Shown.join(QStringLiteral(", "));
    if (Cited.size() > 3) {
        Head += QStringLiteral(", ...");
    }
    return Head + QStringLiteral(" - ");
}

QString MainWindow::IncludeAnchor(xmlNodePtr Include)
{
    if (Include == nullptr) {
        return QString();
    }
    QString Href = DocDocument::NodeAttribute(Include, "href");
    // The cached file carries the real anchor (BCP14, not
    // BCP.14 from the file name); the derived anchor is only
    // the fallback until it is fetched.
    QString CachedAnchor = _BibCache.CachedAnchor(Href);
    return CachedAnchor.isEmpty()
        ? DocDocument::ReferenceAnchorFromIncludeHref(Href)
        : CachedAnchor;
}

QString MainWindow::IncludeLabel(xmlNodePtr Include)
{
    if (Include == nullptr) {
        return QString();
    }
    QString Href = DocDocument::NodeAttribute(Include, "href");
    QString Anchor = IncludeAnchor(Include);
    // A cached entry shows its real title; until it is fetched
    // the anchor with "(included)" says what it is. The anchor
    // leads, as on a manual entry, so the name stays visible when
    // the title is longer than the outline is wide.
    QString Title = _BibCache.CachedTitle(Href);
    QString Label;
    if (!Title.isEmpty() && !Anchor.isEmpty()) {
        Label = tr("%1 - %2").arg(Anchor, Title);
    } else if (Anchor.isEmpty()) {
        Label = Href.isEmpty() ? tr("(included reference)") : Href;
    } else {
        Label = tr("%1 (included)").arg(Anchor);
    }
    return CitedLabelPrefix(Anchor) + Label;
}

bool MainWindow::ReferenceAnchorUnused(const QString& Anchor)
{
    int Count = DocDocument::CitationCountForAnchor(_Document.RfcNode(), Anchor);
    if (Count <= 0) {
        return true;
    }
    QStringList Cited = DocDocument::CitedTextsForAnchor(_Document.RfcNode(), Anchor);
    QString Detail = Cited.isEmpty()
        ? QString()
        : tr(" It is cited as \"%1\".").arg(Cited.first());
    QMessageBox::warning(this, tr("Reference in use"),
        tr("This reference is cited by %1 cross-reference(s).%2 Remove the cross-references first.")
            .arg(Count).arg(Detail));
    return false;
}

void MainWindow::FillReferenceGroupForm()
{
    QSignalBlocker Blocker(_ReferenceGroupNameEdit);
    QSignalBlocker BlockSectionNormative(_ReferenceIncludeNormativeRadio);
    QSignalBlocker BlockSectionInformative(_ReferenceIncludeInformativeRadio);
    QSignalBlocker BlockDisplay(_DisplayReferenceTable);
    xmlNodePtr Group = FindReferenceGroup(_ReferenceGroupPath);
    if (Group == nullptr) {
        _ReferenceGroupNameEdit->clear();
        _AddReferenceEntryButton->setEnabled(false);
        _AddNestedListButton->setEnabled(false);
        _ReferenceIncludeLabel->clear();
        _RemoveReferenceIncludeButton->setEnabled(false);
        _ReferenceIncludeNormativeRadio->setEnabled(false);
        _ReferenceIncludeNormativeRadio->setChecked(false);
        _ReferenceIncludeInformativeRadio->setEnabled(false);
        _ReferenceIncludeInformativeRadio->setChecked(false);
        _DisplayReferenceTable->setRowCount(0);
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
    // The remove for an included reference acts on the include
    // the author picked in the outline; with the list itself
    // picked there is no include to remove.
    QVector<xmlNodePtr> Includes = DocDocument::ReferenceIncludeNodes(Group);
    xmlNodePtr Include = nullptr;
    if (_ReferenceIncludeIndex >= 0 &&
        _ReferenceIncludeIndex < Includes.size()) {
        Include = Includes[_ReferenceIncludeIndex];
    }
    if (Include == nullptr) {
        _ReferenceIncludeLabel->clear();
        _RemoveReferenceIncludeButton->setEnabled(false);
    } else {
        _ReferenceIncludeLabel->setText(IncludeLabel(Include));
        _RemoveReferenceIncludeButton->setEnabled(true);
    }
    // The section radios move the picked included reference; with
    // no include picked they have nothing to move. The checked
    // one names the list the include already sits in, if any.
    _ReferenceIncludeNormativeRadio->setEnabled(Include != nullptr);
    _ReferenceIncludeInformativeRadio->setEnabled(Include != nullptr);
    QString Section = DocDocument::ChildText(Group, "name");
    _ReferenceIncludeNormativeRadio->setChecked(
        Include != nullptr &&
        Section == QStringLiteral("Normative References"));
    _ReferenceIncludeInformativeRadio->setChecked(
        Include != nullptr &&
        Section == QStringLiteral("Informative References"));
    if (ReferenceGroupHasPreservedContent(Group)) {
        _ReferenceGroupPreservedLabel->setText(
            tr("This list holds details this version does not edit (a "
               "reference group or other entries, for example). They are "
               "kept when you save."));
        _ReferenceGroupPreservedLabel->show();
    } else {
        _ReferenceGroupPreservedLabel->hide();
    }
    // The display names are document-level, so every list page
    // shows the same mappings (Bug 405). The reference column
    // is the mapping's key and never edits here: changing the
    // reference is a remove plus an add.
    QVector<xmlNodePtr> DisplayNodes = DisplayReferenceNodes(_Document);
    _DisplayReferenceTable->setRowCount(DisplayNodes.size());
    for (int i = 0; i < DisplayNodes.size(); ++i) {
        QTableWidgetItem* TargetItem = new QTableWidgetItem(
            DocDocument::NodeAttribute(DisplayNodes[i], "target"));
        TargetItem->setFlags(TargetItem->flags() &
                             ~Qt::ItemIsEditable);
        _DisplayReferenceTable->setItem(i, 0, TargetItem);
        _DisplayReferenceTable->setItem(
            i, 1,
            new QTableWidgetItem(
                DocDocument::NodeAttribute(DisplayNodes[i], "to")));
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
    QSignalBlocker BlockAnnotation(_ReferenceAnnotationEdit);
    QSignalBlocker BlockMoveTo(_ReferenceMoveToCombo);
    QSignalBlocker BlockSectionNormative(_ReferenceNormativeRadio);
    QSignalBlocker BlockSectionInformative(_ReferenceInformativeRadio);
    QSignalBlocker BlockAuthorList(_ReferenceAuthorList);
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
        _ReferenceAnnotationEdit->clear();
        _ReferenceMoveToCombo->clear();
        _ReferenceNormativeRadio->setChecked(false);
        _ReferenceInformativeRadio->setChecked(false);
        _ReferenceAuthorList->clear();
        _ReferenceAuthorIndex = -1;
        FillReferenceAuthorFields();
        _ReferenceEntryPreservedLabel->hide();
        return;
    }
    // The checked section radio names the list this entry sits
    // in, if it is a section list; an entry in a flat list has
    // neither checked.
    xmlNodePtr ParentGroup = FindReferenceGroup(_ReferenceGroupPath);
    QString Section = ParentGroup == nullptr
        ? QString()
        : DocDocument::ChildText(ParentGroup, "name");
    _ReferenceNormativeRadio->setChecked(
        Section == QStringLiteral("Normative References"));
    _ReferenceInformativeRadio->setChecked(
        Section == QStringLiteral("Informative References"));
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
    _ReferenceAnnotationEdit->setText(DocDocument::ChildText(Entry, "annotation"));
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
    // The authors list on this page: the authors no longer have
    // outline items of their own, so they are picked here.
    QVector<xmlNodePtr> Authors = DocDocument::ReferenceAuthors(Entry);
    _ReferenceAuthorList->clear();
    for (xmlNodePtr Author : Authors) {
        QString Name = DocDocument::AuthorAttribute(Author, "fullname");
        _ReferenceAuthorList->addItem(
            Name.trimmed().isEmpty() ? tr("(unnamed)") : Name);
    }
    if (Authors.isEmpty()) {
        _ReferenceAuthorIndex = -1;
    } else {
        if (_ReferenceAuthorIndex < 0) {
            _ReferenceAuthorIndex = 0;
        } else if (_ReferenceAuthorIndex >= Authors.size()) {
            _ReferenceAuthorIndex = Authors.size() - 1;
        }
    }
    _ReferenceAuthorList->setCurrentRow(_ReferenceAuthorIndex);
    FillReferenceAuthorFields();
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

xmlNodePtr MainWindow::ShownReferenceAuthor()
{
    xmlNodePtr Entry = FindReferenceEntry(_ReferenceGroupPath, _ReferenceEntryIndex);
    if (Entry == nullptr) {
        return nullptr;
    }
    QVector<xmlNodePtr> Authors = DocDocument::ReferenceAuthors(Entry);
    if (_ReferenceAuthorIndex < 0 || _ReferenceAuthorIndex >= Authors.size()) {
        return nullptr;
    }
    return Authors[_ReferenceAuthorIndex];
}

void MainWindow::FillReferenceAuthorFields()
{
    QSignalBlocker BlockFullname(_ReferenceAuthorFullnameEdit);
    QSignalBlocker BlockInitials(_ReferenceAuthorInitialsEdit);
    QSignalBlocker BlockSurname(_ReferenceAuthorSurnameEdit);
    QSignalBlocker BlockOrganization(_ReferenceAuthorOrganizationEdit);
    QSignalBlocker BlockEmail(_ReferenceAuthorEmailEdit);
    QSignalBlocker BlockPhone(_ReferenceAuthorPhoneEdit);
    QSignalBlocker BlockUri(_ReferenceAuthorUriEdit);
    QSignalBlocker BlockAuthorRadio(_ReferenceAuthorRadio);
    QSignalBlocker BlockEditorRadio(_ReferenceEditorRadio);
    xmlNodePtr Author = ShownReferenceAuthor();
    bool HasAuthor = Author != nullptr;
    _ReferenceAuthorFullnameEdit->setEnabled(HasAuthor);
    _ReferenceAuthorInitialsEdit->setEnabled(HasAuthor);
    _ReferenceAuthorSurnameEdit->setEnabled(HasAuthor);
    _ReferenceAuthorOrganizationEdit->setEnabled(HasAuthor);
    _ReferenceAuthorEmailEdit->setEnabled(HasAuthor);
    _ReferenceAuthorPhoneEdit->setEnabled(HasAuthor);
    _ReferenceAuthorUriEdit->setEnabled(HasAuthor);
    _ReferenceAuthorRadio->setEnabled(HasAuthor);
    _ReferenceEditorRadio->setEnabled(HasAuthor);
    _RemoveReferenceAuthorButton->setEnabled(HasAuthor);
    if (!HasAuthor) {
        _ReferenceAuthorFullnameEdit->clear();
        _ReferenceAuthorInitialsEdit->clear();
        _ReferenceAuthorSurnameEdit->clear();
        _ReferenceAuthorOrganizationEdit->clear();
        _ReferenceAuthorEmailEdit->clear();
        _ReferenceAuthorPhoneEdit->clear();
        _ReferenceAuthorUriEdit->clear();
        _ReferenceAuthorRadio->setChecked(true);
        return;
    }
    _ReferenceAuthorFullnameEdit->setText(
        DocDocument::AuthorAttribute(Author, "fullname"));
    _ReferenceAuthorInitialsEdit->setText(
        DocDocument::AuthorAttribute(Author, "initials"));
    _ReferenceAuthorSurnameEdit->setText(
        DocDocument::AuthorAttribute(Author, "surname"));
    _ReferenceAuthorOrganizationEdit->setText(
        DocDocument::AuthorOrganization(Author));
    _ReferenceAuthorEmailEdit->setText(
        DocDocument::AuthorContactField(Author, "email"));
    _ReferenceAuthorPhoneEdit->setText(
        DocDocument::AuthorContactField(Author, "phone"));
    _ReferenceAuthorUriEdit->setText(
        DocDocument::AuthorContactField(Author, "uri"));
    bool IsEditor = DocDocument::AuthorAttribute(Author, "role") ==
        QStringLiteral("editor");
    _ReferenceAuthorRadio->setChecked(!IsEditor);
    _ReferenceEditorRadio->setChecked(IsEditor);
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

void MainWindow::OnAddDisplayReference()
{
    // The target must be a reference the document already
    // provides, manual or included: a displayreference points
    // at an anchor, and a target with no anchor would fail
    // validation.
    QStringList Anchors = ProvidedAnchorProviders().keys();
    if (Anchors.isEmpty()) {
        QMessageBox::information(this, tr("Add Display Name"),
            tr("There are no references to name yet."));
        return;
    }
    bool Ok = false;
    QString Target = QInputDialog::getItem(this, tr("Add Display Name"),
                                           tr("Reference:"), Anchors, 0,
                                           false, &Ok);
    if (!Ok) {
        return;
    }
    // One mapping per target: a second one for the same anchor
    // would leave xml2rfc with two names for it. The check is
    // the same shape as the Add Entry duplicate check (Bug 487).
    if (!DisplayNameForAnchor(_Document, Target).isEmpty()) {
        QMessageBox::warning(this, tr("Add Display Name"),
            tr("A display name for \"%1\" already exists.").arg(Target));
        return;
    }
    QString Shown = QInputDialog::getText(this, tr("Add Display Name"),
                                          tr("Display as:"),
                                          QLineEdit::Normal, QString(), &Ok);
    while (Ok && Shown.trimmed().isEmpty()) {
        Shown = QInputDialog::getText(this, tr("Add Display Name"),
                                      tr("Display as (required):"),
                                      QLineEdit::Normal, QString(), &Ok);
    }
    if (!Ok) {
        return;
    }
    xmlNodePtr Created = xmlNewNode(nullptr, XmlChars("displayreference"));
    DocDocument::SetNodeAttribute(Created, "target", Target.trimmed());
    DocDocument::SetNodeAttribute(Created, "to", Shown.trimmed());
    if (!InsertDisplayReferenceNode(_Document, Created)) {
        xmlFreeNode(Created);
        return;
    }
    SetModified(true);
    RebuildOutline();
    // The outline rebuild refilled the table: put the new
    // mapping's row back under the cursor.
    QVector<xmlNodePtr> DisplayNodes = DisplayReferenceNodes(_Document);
    for (int i = 0; i < DisplayNodes.size(); ++i) {
        if (DocDocument::NodeAttribute(DisplayNodes[i], "target")
                .trimmed() == Target.trimmed()) {
            _DisplayReferenceTable->setCurrentCell(i, 1);
            break;
        }
    }
}

void MainWindow::OnRemoveDisplayReference()
{
    QVector<xmlNodePtr> DisplayNodes = DisplayReferenceNodes(_Document);
    int Row = _DisplayReferenceTable->currentRow();
    if (Row < 0 || Row >= DisplayNodes.size()) {
        return;
    }
    // Removing a mapping breaks no citation: the bracket
    // simply shows the anchor again, so there is no cited
    // guard here as there is for removing an entry.
    RemoveDisplayReferenceNode(DisplayNodes[Row]);
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnDisplayReferenceChanged()
{
    QVector<xmlNodePtr> DisplayNodes = DisplayReferenceNodes(_Document);
    if (_DisplayReferenceTable->rowCount() != DisplayNodes.size()) {
        return;
    }
    // Rows map one-to-one onto the mappings; only the display
    // name edits here, the reference column never does.
    // A display name is required by the schema, so an emptied
    // cell goes back to the name it held, with a warning,
    // instead of writing an empty "to" into the file.
    bool WarnedEmpty = false;
    for (int i = 0; i < DisplayNodes.size(); ++i) {
        QTableWidgetItem* ShownItem = _DisplayReferenceTable->item(i, 1);
        if (ShownItem == nullptr) {
            continue;
        }
        QString Shown = ShownItem->text().trimmed();
        if (Shown.isEmpty()) {
            QSignalBlocker Blocker(_DisplayReferenceTable);
            ShownItem->setText(
                DocDocument::NodeAttribute(DisplayNodes[i], "to"));
            WarnedEmpty = true;
            continue;
        }
        DocDocument::SetNodeAttribute(DisplayNodes[i], "to", Shown);
    }
    if (WarnedEmpty) {
        QMessageBox::warning(this, tr("Display Names"),
            tr("A display name cannot be empty."));
    }
    SetModified(true);
    RebuildOutline();
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
 * @param Cache The bibliography cache, for the anchor a cached
 * include carries (BCP14, not BCP.14 from the file name).
 * @param Anchor The anchor to look for.
 * @return True when the anchor is already used.
 */
QMap<QString, QStringList> MainWindow::ProvidedAnchorProviders() const
{
    QMap<QString, QStringList> Provided;
    for (const QString& Path : _Document.ReferenceGroupPaths()) {
        xmlNodePtr Group = _Document.ReferenceGroupAtPath(Path);
        if (Group == nullptr) {
            continue;
        }
        for (xmlNodePtr Entry : DocDocument::ReferenceEntries(Group)) {
            QString Anchor =
                DocDocument::NodeAttribute(Entry, "anchor").trimmed();
            if (Anchor.isEmpty()) {
                continue;
            }
            QString Label = tr("the reference \"%1\"").arg(Anchor);
            if (!Provided[Anchor].contains(Label)) {
                Provided[Anchor].append(Label);
            }
        }
        for (xmlNodePtr Include : DocDocument::ReferenceIncludeNodes(Group)) {
            QString Href = DocDocument::NodeAttribute(Include, "href");
            QStringList Anchors = _BibCache.CachedAnchors(Href);
            if (Anchors.isEmpty()) {
                QString Anchor = _BibCache.CachedAnchor(Href);
                Anchors.append(Anchor.isEmpty()
                    ? DocDocument::ReferenceAnchorFromIncludeHref(Href)
                    : Anchor);
            }
            // The provider is the include itself, named by its
            // own anchor: the first anchor its file provides.
            QString Label = Anchors.isEmpty()
                ? QString()
                : tr("the included reference \"%1\"").arg(Anchors.first());
            for (const QString& Anchor : Anchors) {
                QString Wanted = Anchor.trimmed();
                if (Wanted.isEmpty() || Label.isEmpty()) {
                    continue;
                }
                if (!Provided[Wanted].contains(Label)) {
                    Provided[Wanted].append(Label);
                }
            }
        }
    }
    return Provided;
}

bool MainWindow::FindProvidedAnchorClash(const QStringList& NewAnchors,
                                         QString& Clash, QString& Provider)
{
    QMap<QString, QStringList> Provided = ProvidedAnchorProviders();
    for (const QString& Anchor : NewAnchors) {
        QString Wanted = Anchor.trimmed();
        if (Wanted.isEmpty()) {
            continue;
        }
        if (Provided.contains(Wanted)) {
            Clash = Wanted;
            Provider = Provided.value(Wanted).first();
            return true;
        }
    }
    Clash.clear();
    Provider.clear();
    return false;
}

xmlNodePtr MainWindow::FindAnchorProviderNode(const QString& Anchor) const
{
    QString Wanted = Anchor.trimmed();
    if (Wanted.isEmpty()) {
        return nullptr;
    }
    for (const QString& Path : _Document.ReferenceGroupPaths()) {
        xmlNodePtr Group = _Document.ReferenceGroupAtPath(Path);
        if (Group == nullptr) {
            continue;
        }
        for (xmlNodePtr Entry : DocDocument::ReferenceEntries(Group)) {
            if (DocDocument::NodeAttribute(Entry, "anchor").trimmed() ==
                Wanted) {
                return Entry;
            }
        }
        for (xmlNodePtr Include :
             DocDocument::ReferenceIncludeNodes(Group)) {
            QString Href = DocDocument::NodeAttribute(Include, "href");
            QStringList Anchors = _BibCache.CachedAnchors(Href);
            if (Anchors.isEmpty()) {
                QString Single = _BibCache.CachedAnchor(Href);
                Anchors.append(Single.isEmpty()
                    ? DocDocument::ReferenceAnchorFromIncludeHref(Href)
                    : Single);
            }
            if (Anchors.contains(Wanted)) {
                return Include;
            }
        }
    }
    return nullptr;
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
    // Which form the cross-reference takes: the forms
    // published RFCs use. Asked once, up front, so the same
    // form is used whether the reference is new or already
    // there, and whether it is an RFC or a placeholder.
    TextEditor::XrefForm Form = TextEditor::XrefForm::LinkText;
    QString Section;
    if (!TextEditor::PickXrefForm(this, Wanted, Editor->SelectedText(),
                                  Form, Section)) {
        return;
    }
    QString RfcNumber = RfcNumberFromAnchor(Wanted);
    if (!RfcNumber.isEmpty()) {
        // An RFC include: fetch its file first, so the anchors
        // it provides are known before it is added. A group
        // already in the document may provide this anchor
        // inside itself (BCP14 provides RFC2119), and adding
        // the include as well would carry the anchor twice.
        // The finish runs once, on the fetch; the selection was
        // captured above, so a moved cursor cannot misplace
        // the cross-reference.
        QString Href = QStringLiteral(
            "https://bib.ietf.org/public/rfc/bibxml/reference.RFC.") +
            RfcNumber + QStringLiteral(".xml");
        statusBar()->showMessage(tr("Fetching bibliography entry..."));
        connect(&_BibCache, &BibCache::FetchFinished, this,
            [this, Editor, Group, Wanted, Href, Form, Section,
             SelectionStart, SelectionEnd]() {
                FinishNewReference(Editor, Group, Wanted, Href, Form,
                                   Section, SelectionStart, SelectionEnd);
            },
            Qt::SingleShotConnection);
        _BibCache.EnsureCached(QStringList(Href));
        return;
    }
    // A placeholder (the anchor is not an RFC): the anchor is
    // all it provides, so the check runs against the document
    // as it stands.
    QString Clash;
    QString Provider;
    if (FindProvidedAnchorClash(QStringList(Wanted), Clash, Provider)) {
        // The entry is already there: move it into the list
        // the author chose, then cite it instead of a
        // duplicate. The move keeps the author in the section
        // body; the outline rebuilds only when something
        // actually moved.
        bool Moved = MoveReferenceIntoList(
            FindAnchorProviderNode(Clash), Group);
        statusBar()->showMessage(
            tr("%1 already provides %2: citing the existing reference.")
                .arg(Provider, Clash));
        if (Moved) {
            SetModified(true);
            RebuildOutline();
        }
        Editor->InsertXref(Clash, Section, Form, SelectionStart,
                           SelectionEnd);
        return;
    }
    {
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
    Editor->InsertXref(Wanted, Section, Form, SelectionStart, SelectionEnd);
}

bool MainWindow::HandleAnchorChange(TextEditor* Editor,
                                    const QString& OldAnchor,
                                    const QString& NewAnchor,
                                    const std::function<void(const QString&)>& SetAnchor)
{
    // Bug 464 v8: the gear menu asks the owner to change an
    // anchor, so the references move with it and a cited
    // anchor cannot be deleted. The section anchor field uses
    // the same path, with a setter that writes the section
    // node's anchor attribute.
    if (Editor == nullptr || !SetAnchor) {
        return false;
    }
    QString Old = OldAnchor.trimmed();
    QString New = NewAnchor.trimmed();
    if (Old == New) {
        return true;
    }
    if (New.isEmpty()) {
        // Deleting: refuse when the anchor is cited, or the
        // cross-references would dangle. The count covers
        // xrefs and relrefs.
        if (Old.isEmpty()) {
            return true;
        }
        int Count = DocDocument::CitationCountForAnchor(_Document.RfcNode(), Old);
        if (Count > 0) {
            QMessageBox::warning(
                this, tr("Anchor in use"),
                tr("This anchor is cited by %1 cross-reference(s). It cannot be deleted. Remove the cross-references first.")
                    .arg(Count));
            return false;
        }
        SetAnchor(QString());
        SetModified(true);
        return true;
    }
    // A new anchor must not collide with one already in use:
    // an anchor names one element.
    if (DocDocument::DocumentHasAnchor(_Document.RfcNode(), New)) {
        QMessageBox::warning(
            this, tr("Anchor in use"),
            tr("Another element already uses the anchor \"%1\". Pick a different one.")
                .arg(New));
        return false;
    }
    int Cited = 0;
    if (!Old.isEmpty()) {
        Cited = DocDocument::CitationCountForAnchor(_Document.RfcNode(), Old);
    }
    SetAnchor(New);
    if (Cited > 0) {
        // The editor's sync above carried the new anchor into
        // the model but left the old cross-reference targets in
        // this section, so the rename runs after it, and the
        // editor is reloaded from the model so its markers
        // match the renamed references.
        DocDocument::RenameXrefTargets(_Document.RfcNode(), Old, New);
        ReloadEditorFromModel(Editor);
    }
    SetModified(true);
    return true;
}

void MainWindow::ReloadEditorFromModel(TextEditor* Editor)
{
    if (Editor == nullptr) {
        return;
    }
    int Start = -1;
    int End = -1;
    Editor->SelectedRange(Start, End);
    if (Editor == _SectionBodyEdit) {
        xmlNodePtr Section = FindSection(_SectionPath);
        if (Section == nullptr) {
            return;
        }
        _SectionBodyEdit->SetBodyNodes(DocDocument::BodyNodes(Section));
        _SectionBodyEdit->SetPreservedNodes(
            DocDocument::PreservedBodyNodes(Section));
    } else if (Editor == _AbstractEdit) {
        _AbstractEdit->SetBodyNodes(_Document.AbstractBodyNodes());
        _AbstractEdit->SetPreservedNodes(_Document.AbstractPreservedNodes());
        _AbstractEditCurrent = true;
    } else if (Editor == _SettingsNoteEdit) {
        QVector<xmlNodePtr> Notes = _Document.NoteNodes();
        if (Notes.isEmpty()) {
            return;
        }
        xmlNodePtr Note = Notes.first();
        _SettingsNoteEdit->SetBodyNodes(DocDocument::BodyNodes(Note));
        _SettingsNoteEdit->SetPreservedNodes(
            DocDocument::PreservedBodyNodes(Note));
    } else {
        return;
    }
    Editor->RestoreSelection(Start, End);
}

void MainWindow::FinishNewReference(TextEditor* Editor, xmlNodePtr Group,
                                    const QString& Wanted, const QString& Href,
                                    TextEditor::XrefForm Form,
                                    const QString& Section,
                                    int SelectionStart, int SelectionEnd)
{
    if (Editor == nullptr) {
        return;
    }
    // The fetch took time: the author may have opened another
    // document since. The group only counts when it still
    // belongs to the open document.
    bool GroupLive = false;
    for (const QString& Path : _Document.ReferenceGroupPaths()) {
        if (_Document.ReferenceGroupAtPath(Path) == Group) {
            GroupLive = true;
            break;
        }
    }
    if (!GroupLive) {
        return;
    }
    QStringList Provided = _BibCache.CachedAnchors(Href);
    if (Provided.isEmpty()) {
        // The fetch failed, or the file carries nothing usable:
        // fall back to the wanted anchor alone, so the direct
        // check still runs. The failed download is reported at
        // validation time.
        Provided.append(Wanted);
    }
    QString Clash;
    QString Provider;
    if (FindProvidedAnchorClash(Provided, Clash, Provider)) {
        // The anchor is already provided (a group carries it
        // inside itself, for example): move it into the list
        // the author chose, then cite it instead of adding a
        // second copy, and say why.
        bool Moved = MoveReferenceIntoList(
            FindAnchorProviderNode(Clash), Group);
        statusBar()->showMessage(
            tr("%1 already provides %2: citing the existing reference.")
                .arg(Provider, Clash));
        if (Moved) {
            SetModified(true);
            RebuildOutline();
        }
        Editor->InsertXref(Clash, Section, Form, SelectionStart,
                           SelectionEnd);
        return;
    }
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
    DocDocument::SetNodeAttribute(Include, "href", Href);
    SetModified(true);
    RebuildOutline();
    statusBar()->showMessage(
        tr("Added the included reference %1.").arg(Wanted));
    Editor->InsertXref(Wanted, Section, Form, SelectionStart, SelectionEnd);
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
    Anchor = Anchor.trimmed();
    // The anchor must not already be provided: a second entry
    // with the same anchor would define it twice. The check is
    // the one the other add flows use, so a manual entry and an
    // included reference both count.
    QString Clash;
    QString Provider;
    if (FindProvidedAnchorClash(QStringList(Anchor), Clash, Provider)) {
        QMessageBox::warning(this, tr("Add Entry"),
            tr("Cannot add this entry: %1 already provides %2.")
                .arg(Provider, Clash));
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
    // A cited reference is not removed: the citations would be
    // left pointing at an anchor that no longer exists.
    if (!ReferenceAnchorUnused(DocDocument::NodeAttribute(Entry, "anchor"))) {
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

void MainWindow::OnRemoveReferenceInclude()
{
    xmlNodePtr Group = FindReferenceGroup(_ReferenceGroupPath);
    if (Group == nullptr) {
        return;
    }
    QVector<xmlNodePtr> Includes = DocDocument::ReferenceIncludeNodes(Group);
    if (_ReferenceIncludeIndex < 0 ||
        _ReferenceIncludeIndex >= Includes.size()) {
        return;
    }
    xmlNodePtr Include = Includes[_ReferenceIncludeIndex];
    QString Anchor = IncludeAnchor(Include);
    // As with a manual entry, a cited include is not removed:
    // the citations would be left pointing at an anchor that
    // no longer exists.
    if (!ReferenceAnchorUnused(Anchor)) {
        return;
    }
    QMessageBox::StandardButton Answer = QMessageBox::question(
        this, tr("Remove Included Reference"),
        Anchor.trimmed().isEmpty()
            ? tr("Remove this included reference?")
            : tr("Remove the included reference \"%1\"?").arg(Anchor),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (Answer != QMessageBox::Yes) {
        return;
    }
    xmlUnlinkNode(Include);
    xmlFreeNode(Include);
    _ReferenceIncludeIndex = -1;
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

void MainWindow::OnReferenceSectionChanged()
{
    // Only the check moves the entry; the matching uncheck of
    // the other radio, and the fill with the form blocked, do
    // not. With neither checked there is nothing to move to.
    if (!_ReferenceNormativeRadio->isChecked() &&
        !_ReferenceInformativeRadio->isChecked()) {
        return;
    }
    xmlNodePtr Entry = FindReferenceEntry(_ReferenceGroupPath, _ReferenceEntryIndex);
    if (Entry == nullptr) {
        return;
    }
    MoveReferenceToSection(Entry, _ReferenceNormativeRadio->isChecked());
}

void MainWindow::OnReferenceIncludeSectionChanged()
{
    if (!_ReferenceIncludeNormativeRadio->isChecked() &&
        !_ReferenceIncludeInformativeRadio->isChecked()) {
        return;
    }
    xmlNodePtr Group = FindReferenceGroup(_ReferenceGroupPath);
    if (Group == nullptr) {
        return;
    }
    QVector<xmlNodePtr> Includes = DocDocument::ReferenceIncludeNodes(Group);
    if (_ReferenceIncludeIndex < 0 ||
        _ReferenceIncludeIndex >= Includes.size()) {
        return;
    }
    MoveReferenceToSection(Includes[_ReferenceIncludeIndex],
                           _ReferenceIncludeNormativeRadio->isChecked());
}

/**
 * @brief Moves a reference entry or an included reference into
 *        the Normative or Informative nested list under its
 *        current outer list, creating that list when needed.
 *
 * The outer list is the topmost <references> ancestor, so an
 * entry already in a section moves between sections, and an
 * entry in a flat list moves under that list. Normative stays
 * ahead of Informative, as in the IETF form.
 *
 * A list holds nested lists or entries, never both, so moving
 * the first entry leaves the outer list mixed until the last
 * direct entry moves: the file is schema-valid again once every
 * entry has a section.
 * @param Item The entry or include node to move (borrowed).
 * @param Normative True for the Normative section, false for
 *        the Informative section.
 */
void MainWindow::MoveReferenceToSection(xmlNodePtr Item, bool Normative)
{
    if (Item == nullptr || Item->parent == nullptr ||
        !DocDocument::NodeNameEquals(Item->parent, "references")) {
        return;
    }
    xmlNodePtr Outer = Item->parent;
    while (Outer->parent != nullptr &&
           DocDocument::NodeNameEquals(Outer->parent, "references")) {
        Outer = Outer->parent;
    }
    QString SectionName = Normative
        ? QStringLiteral("Normative References")
        : QStringLiteral("Informative References");
    xmlNodePtr Target = nullptr;
    xmlNodePtr Informative = nullptr;
    for (xmlNodePtr Child = Outer->children; Child != nullptr;
         Child = Child->next) {
        if (Child->type != XML_ELEMENT_NODE ||
            !DocDocument::NodeNameEquals(Child, "references")) {
            continue;
        }
        QString Name = DocDocument::ChildText(Child, "name");
        if (Name == SectionName) {
            Target = Child;
        }
        if (Name == QStringLiteral("Informative References")) {
            Informative = Child;
        }
    }
    if (Target == nullptr) {
        Target = xmlNewNode(nullptr, XmlChars("references"));
        SetLeadingChildText(Target, "name", SectionName);
        if (Normative && Informative != nullptr) {
            xmlAddPrevSibling(Informative, Target);
        } else {
            xmlAddChild(Outer, Target);
        }
    }
    if (Item->parent == Target) {
        return;
    }
    bool IsEntry = DocDocument::NodeNameEquals(Item, "reference");
    xmlUnlinkNode(Item);
    if (IsEntry) {
        AppendReferenceEntry(Target, Item);
    } else {
        xmlAddChild(Target, Item);
    }
    // The moved item has a new home: point the selection at it
    // so the form stays on it after the outline rebuilds.
    QString TargetPath;
    for (const QString& Path : _Document.ReferenceGroupPaths()) {
        if (_Document.ReferenceGroupAtPath(Path) == Target) {
            TargetPath = Path;
            break;
        }
    }
    if (!TargetPath.isEmpty()) {
        if (IsEntry) {
            _ReferenceGroupPath = TargetPath;
            _ReferenceEntryIndex =
                DocDocument::ReferenceEntries(Target).size() - 1;
            _SelectedKey = QStringLiteral("refentry:") + _ReferenceGroupPath +
                QStringLiteral(".") + QString::number(_ReferenceEntryIndex);
        } else {
            _ReferenceGroupPath = TargetPath;
            _ReferenceIncludeIndex =
                DocDocument::ReferenceIncludeNodes(Target).size() - 1;
            _SelectedKey = QStringLiteral("refinclude:") + _ReferenceGroupPath +
                QStringLiteral(".") + QString::number(_ReferenceIncludeIndex);
        }
    }
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
    SetReferenceAnnotation(Entry, _ReferenceAnnotationEdit->text());
    QTreeWidgetItem* Current = _Outline->currentItem();
    if (Current != nullptr) {
        QString Label = DocDocument::ReferenceLabel(
            Entry, DisplayNameForAnchor(_Document, NewAnchor));
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
    // Select the new author in the list on the entry form: the
    // authors no longer have outline items of their own.
    _ReferenceAuthorIndex = DocDocument::ReferenceAuthors(Entry).size() - 1;
    _SelectedKey = QStringLiteral("refentry:") + _ReferenceGroupPath +
        QStringLiteral(".") + QString::number(_ReferenceEntryIndex);
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnRemoveReferenceAuthor()
{
    xmlNodePtr Author = ShownReferenceAuthor();
    if (Author == nullptr) {
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
    xmlUnlinkNode(Author);
    xmlFreeNode(Author);
    if (_ReferenceAuthorIndex > 0) {
        --_ReferenceAuthorIndex;
    }
    SetModified(true);
    RebuildOutline();
}

void MainWindow::OnReferenceAuthorSelected()
{
    _ReferenceAuthorIndex = _ReferenceAuthorList->currentRow();
    FillReferenceAuthorFields();
}

void MainWindow::OnReferenceAuthorEdited()
{
    xmlNodePtr Author = ShownReferenceAuthor();
    if (Author == nullptr) {
        return;
    }
    DocDocument::SetAuthorAttribute(
        Author, "fullname", _ReferenceAuthorFullnameEdit->text());
    DocDocument::SetAuthorAttribute(
        Author, "initials", _ReferenceAuthorInitialsEdit->text());
    DocDocument::SetAuthorAttribute(
        Author, "surname", _ReferenceAuthorSurnameEdit->text());
    DocDocument::SetAuthorOrganization(
        Author, _ReferenceAuthorOrganizationEdit->text());
    DocDocument::SetAuthorContactField(
        Author, "email", _ReferenceAuthorEmailEdit->text());
    DocDocument::SetAuthorContactField(
        Author, "phone", _ReferenceAuthorPhoneEdit->text());
    DocDocument::SetAuthorContactField(
        Author, "uri", _ReferenceAuthorUriEdit->text());
    // Keep the list row showing the name being typed.
    QListWidgetItem* Item = _ReferenceAuthorList->currentItem();
    if (Item != nullptr) {
        QString Fullname = DocDocument::AuthorAttribute(Author, "fullname");
        Item->setText(Fullname.trimmed().isEmpty()
            ? tr("(unnamed)") : Fullname);
    }
    SetModified(true);
}

void MainWindow::OnReferenceAuthorRoleChanged()
{
    xmlNodePtr Author = ShownReferenceAuthor();
    if (Author == nullptr) {
        return;
    }
    DocDocument::SetAuthorAttribute(
        Author, "role",
        _ReferenceEditorRadio->isChecked() ? QStringLiteral("editor") : QString());
    SetModified(true);
}

void MainWindow::FillSectionForm()
{
    QSignalBlocker BlockTitle(_SectionTitleEdit);
    QSignalBlocker BlockAnchor(_SectionAnchorEdit);
    QSignalBlocker BlockBody(_SectionBodyEdit);
    xmlNodePtr Section = FindSection(_SectionPath);
    if (Section == nullptr) {
        _SectionTitleEdit->clear();
        _SectionAnchorEdit->clear();
        _SectionBodyEdit->Clear();
        _SectionPreservedLabel->hide();
        _SectionRequiredLabel->hide();
        return;
    }
    _SectionTitleEdit->setText(DocDocument::SectionTitle(Section));
    // The anchor field shows the stored anchor, or the default
    // derived from the title while none is stored. The default
    // is only shown, not written: the section gains an anchor
    // when the field is edited, or when a cross-reference is
    // inserted to it.
    QString Anchor = DocDocument::NodeAttribute(Section, "anchor").trimmed();
    if (Anchor.isEmpty()) {
        Anchor = SlugAnchor(DocDocument::SectionTitle(Section));
    }
    _SectionAnchorEdit->setText(Anchor);
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
    QString Title = tr("InternetDraftsEditor - V%1")
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

bool MainWindow::ApplyPendingXmlEdits()
{
    if (_SectionBodyEdit != nullptr &&
        !_SectionBodyEdit->ApplyXmlEdits()) {
        return false;
    }
    if (_AbstractEdit != nullptr && !_AbstractEdit->ApplyXmlEdits()) {
        return false;
    }
    if (_SettingsNoteEdit != nullptr &&
        !_SettingsNoteEdit->ApplyXmlEdits()) {
        return false;
    }
    return true;
}

bool MainWindow::SaveToFile(const QString& Path)
{
    // Bug 520: apply pending XML edits before saving, so a
    // paste is not lost by the save writing the old body.
    if (!ApplyPendingXmlEdits()) {
        return false;
    }
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
    // Bug 505: the "used as an aid" comment goes in when the
    // tool created or modified the content (the document is
    // marked changed), or when the file already carries it (so
    // an unchanged re-save keeps the marker). A file merely
    // opened and saved unchanged, that never had the comment,
    // gets none.
    bool AddAidComment = _Modified || _Document.HasAidComment;
    if (!_Document.WriteXml(Path, Error, AddAidComment)) {
        QMessageBox::critical(this, tr("Save Draft"),
                              tr("Could not save %1:\n%2").arg(Path, Error));
        return false;
    }
    if (AddAidComment) {
        _Document.HasAidComment = true;
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
    // Bug 541: the GTK libraries WeasyPrint uses print
    // GLib-GIO-WARNING lines to stderr when xml2rfc runs
    // (they complain about Windows Store apps that declare
    // file extensions but no open actions). They say nothing
    // about the draft or the output, so drop them from the
    // run log and keep only xml2rfc's own messages. A line is
    // dropped only when it contains the GLib-GIO-WARNING
    // marker; real xml2rfc warnings and errors do not contain
    // it, so they are kept. Process output arrives in chunks
    // that can split a line, so a trailing partial line is
    // held in _RunLogPending and only complete lines are
    // judged; OnRunFinished flushes what is left.
    _RunLogPending += Text;
    QStringList Lines = _RunLogPending.split(QLatin1Char('\n'));
    // The last piece has no newline yet (unless the text ended
    // with one, in which case it is empty); keep it for the
    // next call.
    _RunLogPending = Lines.takeLast();
    QString Kept;
    for (const QString& Line : Lines) {
        if (Line.contains(QStringLiteral("GLib-GIO-WARNING"))) {
            continue;
        }
        Kept += Line;
        Kept += QLatin1Char('\n');
    }
    if (Kept.isEmpty()) {
        return;
    }
    _RunLog->moveCursor(QTextCursor::End);
    _RunLog->insertPlainText(Kept);
    _RunLog->moveCursor(QTextCursor::End);
}
