// Copyright (c) 2026 RiverExplorer LLC
// RiverExplorer is a registered trademark of Douglas Mark Royer.
//
// This software is released under the MIT License.
// See LICENSE.md for the full license text.

/**
 * @file TextEditor.cpp
 * @brief Implementation of the two-tab text editor (Bugs 322, 382-387, 393, 395-402, 420).
 */

#include "TextEditor.h"

#include "BibCache.h"
#include "SpellChecker.h"
#include "SpellHighlighter.h"

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <QAction>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDataStream>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFont>
#include <QFontInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFragment>
#include <QTextList>
#include <QTextListFormat>
#include <QTextTable>
#include <QTextTableFormat>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>

/**
 * @brief Character-format property holding the tag stack.
 *
 * The value is a list of tag entries, outermost first, each a map
 * with the tag name and its attributes in file order. A character
 * with no stack property sits directly in its paragraph.
 */
const int DocStackProperty = QTextFormat::UserProperty + 1;

/**
 * @brief Character-format property marking a marker character.
 *
 * The value is one tag entry: the marker's tag (Break, Vertical
 * Space, or Index Entry) with its attributes. Marker characters
 * also carry a tag stack, so a marker can sit inside a tag.
 */
const int DocMarkerProperty = QTextFormat::UserProperty + 2;

/**
 * @brief Block-format property holding a list block's attributes.
 *
 * The value is the list element's attributes in file order. The
 * style attribute in it decides the bullet or number shown.
 */
const int DocListProperty = QTextFormat::UserProperty + 3;

/**
 * @brief Block-format property holding a list block's element name.
 *
 * The value is "list", "ul", "ol", or "dl". It tells a block's
 * list kind apart from a plain block and from the other kinds.
 */
const int DocListKindProperty = QTextFormat::UserProperty + 4;

/**
 * @brief Block-format property holding a list block's group number.
 *
 * Blocks of one list share a group number, so two lists of the
 * same kind sitting next to each other stay two lists. The
 * number is assigned when the list is built and copied to new
 * blocks as the list is edited.
 */
const int DocListGroupProperty = QTextFormat::UserProperty + 5;

/**
 * @brief Block-format property holding a list block's role.
 *
 * The value is "term" or "definition" in a definition list, and
 * "item" or "continuation" in a bulleted or numbered list. A
 * continuation block holds a further paragraph of the item or
 * definition above it. The inline list kind has no roles.
 */
const int DocListRoleProperty = QTextFormat::UserProperty + 6;

/**
 * @brief Block-format property holding a sourcecode block's attributes.
 *
 * The value is the sourcecode element's attributes in file order.
 * A block carrying this property is one line of a sourcecode
 * element; consecutive blocks with the same value are one
 * element. The property is absent on every other block.
 */
const int DocSourceProperty = QTextFormat::UserProperty + 7;

/**
 * @brief Block-format property marking a sourcecode block.
 *
 * The value is always "sourcecode". It claims the block the way
 * the list kind claims a list block: the attributes property
 * alone cannot claim it, because a block with no attributes
 * carries an empty value, and an empty value does not reliably
 * survive as a stored property.
 */
const int DocSourceKindProperty = QTextFormat::UserProperty + 8;

/**
 * @brief Block-format property holding a figure block's wrapper kind.
 *
 * The value is "figure", "artset", or "artwork". It claims the
 * block as part of a figure region, the way the list kind
 * claims a list block.
 */
const int DocFigureKindProperty = QTextFormat::UserProperty + 9;

/**
 * @brief Block-format property holding a figure block's group number.
 *
 * Blocks of one figure share a group number, so two figures
 * sitting next to each other stay two figures. The number is
 * assigned when the figure is built and copied to new blocks
 * as the figure is edited.
 */
const int DocFigureGroupProperty = QTextFormat::UserProperty + 10;

/**
 * @brief Block-format property holding a figure block's role.
 *
 * The value is "caption" for the caption block of a figure,
 * and "artwork" for the lines of the artwork version showing.
 */
const int DocFigureRoleProperty = QTextFormat::UserProperty + 11;

/**
 * @brief Block-format property holding a figure region's payload.
 *
 * The value is a map with the figure's kind, its artset flag,
 * its kept attributes, its versions, and the index of the
 * version showing. Every block of the region carries the same
 * payload, so any block can rebuild the figure; the caption
 * itself lives in the caption block's text, not here. An SVG
 * version's preserved element travels as an XML string.
 */
const int DocFigurePayloadProperty = QTextFormat::UserProperty + 12;

/**
 * @brief Block-format property holding a table block's kind.
 *
 * The value is "table". It claims the block as part of a table:
 * the caption block before the grid, or the first block of a
 * cell. Later blocks of a multi-block cell carry no claim of
 * their own; the first block's claim covers the cell.
 */
const int DocTableKindProperty = QTextFormat::UserProperty + 13;

/**
 * @brief Block-format property holding a table block's group number.
 *
 * The caption block and the cell blocks of one table share a
 * group number, so two tables sitting next to each other stay
 * two tables. The number is assigned when the table is built
 * and copied to new blocks as the table is edited.
 */
const int DocTableGroupProperty = QTextFormat::UserProperty + 14;

/**
 * @brief Block-format property holding a table block's role.
 *
 * The value is "caption" for the caption block before the grid,
 * and "cell" for the first block of a cell.
 */
const int DocTableRoleProperty = QTextFormat::UserProperty + 15;

/**
 * @brief Block-format property holding a table's payload.
 *
 * The value is a map with the table's kept attributes, its
 * section attributes, and its rows in grid order, each with
 * its section and its kept attributes. The caption block and
 * every cell's first block carry the same payload, so any
 * block can rebuild the table's shape; the caption itself
 * lives in the caption block's text, and the cell text lives
 * in the cells, not here. Spans are not stored: they are read
 * from the grid itself.
 */
const int DocTablePayloadProperty = QTextFormat::UserProperty + 16;

/**
 * @brief Block-format property holding one cell's own data.
 *
 * The value is a map with the cell's header flag (a <th> when
 * set, a <td> when not), its anchor and align attributes as
 * kept, and its other kept attributes. It lives on the first
 * block of the cell only. Spans are not stored here either:
 * they are read from the grid itself.
 */
const int DocTableCellProperty = QTextFormat::UserProperty + 17;

/**
 * @brief Character-format property marking an option marker character.
 *
 * The value is the element name the marker opens the options
 * for: "sourcecode" in this build, other modeled kinds as their
 * options builds land. The marker is display only: the
 * read-back drops the character, and the structure's options
 * live in the block properties, never here. It is a separate
 * property from the marker property, so a gear is never read
 * as a tag marker and a tag marker is never read as a gear.
 */
const int DocOptionMarkerProperty = QTextFormat::UserProperty + 18;

/**
 * @brief Character-format property marking src placeholder text.
 *
 * The value is the src attribute the placeholder was stamped
 * for, so the sweep can tell a stale placeholder from a
 * current one. The placeholder is display only: the read-back
 * drops the placeholder text, and the src itself lives in the
 * block properties, never here.
 */
const int DocSrcPlaceholderProperty = QTextFormat::UserProperty + 19;

/**
 * @brief True when a block is one line of a sourcecode element.
 * @param Block The block.
 * @return True when the block carries the source kind.
 */
static bool IsSourceCodeBlock(const QTextBlock& Block)
{
    return Block.blockFormat().property(DocSourceKindProperty).toString() ==
           QLatin1String("sourcecode");
}

/**
 * @brief True when a block belongs to a figure region.
 * @param Block The block.
 * @return True when the block carries a figure kind.
 */
static bool IsFigureBlock(const QTextBlock& Block)
{
    return !Block.blockFormat().property(DocFigureKindProperty).toString().isEmpty();
}

/**
 * @brief Returns a figure block's wrapper kind: figure, artset,
 *        or artwork.
 * @param Block The block.
 * @return The kind, or empty when the block is no figure block.
 */
static QString FigureKindOf(const QTextBlock& Block)
{
    return Block.blockFormat().property(DocFigureKindProperty).toString();
}

/**
 * @brief Returns a figure block's role: caption or artwork.
 * @param Block The block.
 * @return The role, or empty when the block carries none.
 */
static QString FigureRoleOf(const QTextBlock& Block)
{
    return Block.blockFormat().property(DocFigureRoleProperty).toString();
}

/**
 * @brief True when a block is the caption block of a figure.
 * @param Block The block.
 * @return True when the block carries the caption role.
 */
static bool IsFigureCaptionBlock(const QTextBlock& Block)
{
    return IsFigureBlock(Block) &&
           FigureRoleOf(Block) == QLatin1String("caption");
}

/**
 * @brief True when a block is one line of a figure's artwork.
 * @param Block The block.
 * @return True when the block carries the artwork role.
 */
static bool IsFigureArtworkBlock(const QTextBlock& Block)
{
    return IsFigureBlock(Block) &&
           FigureRoleOf(Block) == QLatin1String("artwork");
}

/**
 * @brief True when a block belongs to a table: its caption block,
 *        or the first block of one of its cells.
 * @param Block The block.
 * @return True when the block carries the table kind.
 */
static bool IsTableBlock(const QTextBlock& Block)
{
    return !Block.blockFormat().property(DocTableKindProperty).toString().isEmpty();
}

/**
 * @brief Returns a table block's role: caption or cell.
 * @param Block The block.
 * @return The role, or empty when the block carries none.
 */
static QString TableRoleOf(const QTextBlock& Block)
{
    return Block.blockFormat().property(DocTableRoleProperty).toString();
}

/**
 * @brief True when a block is the caption block of a table.
 * @param Block The block.
 * @return True when the block carries the caption role.
 */
static bool IsTableCaptionBlock(const QTextBlock& Block)
{
    return IsTableBlock(Block) &&
           TableRoleOf(Block) == QLatin1String("caption");
}

/**
 * @brief True when a block is the first block of a table cell.
 * @param Block The block.
 * @return True when the block carries the cell role.
 */
static bool IsTableCellBlock(const QTextBlock& Block)
{
    return IsTableBlock(Block) &&
           TableRoleOf(Block) == QLatin1String("cell");
}

/**
 * @brief Returns the table frame a block sits in, or nullptr.
 *
 * A caption block sits before the frame, not in it, so it
 * answers nullptr; the frame follows it. A cell block answers
 * its frame.
 * @param Document The document.
 * @param Block The block.
 * @return The frame, or nullptr when the block is in none.
 */
static QTextTable* TableOfBlock(QTextDocument* Document, const QTextBlock& Block)
{
    QTextCursor At(Document);
    At.setPosition(Block.position());
    return At.currentTable();
}

/**
 * @brief The font families code is shown in.
 * @return The monospace families, in preference order.
 */
static QStringList CodeFontFamilies()
{
    return QStringList{QStringLiteral("Courier New")};
}

/**
 * @brief The character format for typed and pasted code: plain
 *        text in the code font, carrying no tag stack.
 * @return The format.
 */
static QTextCharFormat CodeCharFormat()
{
    QTextCharFormat Format;
    Format.setFontFamilies(CodeFontFamilies());
    return Format;
}

/**
 * @brief Text edit whose paste follows the paragraph rule and translates rich text.
 *
 * Pasting plain text (no HTML) treats a blank line -- two or
 * more line endings -- as a paragraph break, and joins wrapped
 * lines with a space, so pasted text lands as the paragraphs it
 * was written in instead of one paragraph per line. Pasting
 * rich text translates the HTML into the tags the editor
 * models -- strong, em, tt, sub, sup, external links, lists,
 * and preformatted blocks -- and drops what has no equivalent,
 * keeping the text (Bug 430). Pasting into code, artwork, or a
 * caption uses the plain text only, since those blocks hold no
 * tags. Copying strips option marker characters out of the
 * copied text, and carries a copied sourcecode block's option
 * settings with it, so pasting them makes a block with the
 * same options (Bug 420).
 */
class DocTextEdit : public QTextEdit
{
public:
    explicit DocTextEdit(QWidget* Parent = nullptr) : QTextEdit(Parent) {}

protected:
    void insertFromMimeData(const QMimeData* Source) override;
    QMimeData* createMimeDataFromSelection() const override;
};

/**
 * @brief The marker glyph for a Break: U+23CE.
 */
const QChar BreakGlyph(0x23CE);

/**
 * @brief The marker glyph for a Vertical Space: U+2195.
 */
const QChar VspaceGlyph(0x2195);

/**
 * @brief The marker glyph for an Index Entry with no item: U+FFFD.
 */
const QChar EmptyItemGlyph(0xFFFD);

/**
 * @brief The index entry marker glyph: U+24D8, a circled "i"
 *        (Bug 503). Doug chose it over the replacement character
 *        U+FFFD, which reads as "this couldn't be displayed",
 *        and over a bullet: the circled i reads as "there is
 *        information here", which is what an index entry is.
 */
const QChar IrefGlyph(0x24D8);

/**
 * @brief The option marker glyph: a gear, U+2699 (Bug 420).
 */
const QChar OptionGlyph(0x2699);

/**
 * @brief The QTextDocument property holding the display names.
 *
 * A QVariantMap from reference anchor to the name a
 * displayreference shows for it (Bug 405). It rides on the
 * text document, so the file-local helpers that build markers
 * and read them back can use the mappings without a document
 * pointer: those helpers only ever see a cursor, a block, or
 * a run of events. SetBodyNodes refreshes it from the
 * document whenever a body is loaded.
 */
const char DisplayNamesProperty[] = "DocEditorDisplayNames";

/**
 * @brief The display names in a document, from its displayreferences.
 *
 * The first mapping for a target wins; a mapping with an
 * empty target or an empty display name counts as no mapping.
 * @param Document The document, or nullptr for no mappings.
 * @return The anchor-to-display-name map.
 */
static QVariantMap DisplayNamesFromDocument(const DocDocument* Document)
{
    QVariantMap Names;
    if (Document == nullptr) {
        return Names;
    }
    for (xmlNodePtr Node : DocDocument::ChildElements(
             Document->BackNode(), "displayreference")) {
        QString Target =
            DocDocument::NodeAttribute(Node, "target").trimmed();
        QString To = DocDocument::NodeAttribute(Node, "to").trimmed();
        if (Target.isEmpty() || To.isEmpty() || Names.contains(Target)) {
            continue;
        }
        Names.insert(Target, To);
    }
    return Names;
}

/**
 * @brief The display names a text document carries (Bug 405).
 *
 * The property SetBodyNodes stored there, with empty or
 * duplicate entries already left out.
 * @param Document The text document, or nullptr for no mappings.
 * @return The anchor-to-display-name map.
 */
static QVariantMap DisplayNamesForDocument(const QTextDocument* Document)
{
    QVariantMap Names;
    if (Document == nullptr) {
        return Names;
    }
    QVariantMap Stored =
        Document->property(DisplayNamesProperty).toMap();
    for (auto It = Stored.constBegin(); It != Stored.constEnd(); ++It) {
        QString To = It.value().toString().trimmed();
        if (!It.key().trimmed().isEmpty() && !To.isEmpty()) {
            Names.insert(It.key().trimmed(), To);
        }
    }
    return Names;
}

/**
 * @brief The clipboard format carrying a copied sourcecode
 *        block's option settings (Bug 420).
 *
 * The data is the attribute list, written by
 * SourceAttributesToData: a count followed by name/value
 * pairs. The anchor, src, and prep-tool attributes never
 * travel with it: an anchor must stay unique, and pasted text
 * is content, which src forbids.
 */
const char SourceOptionsMimeType[] =
    "application/x-doceditor-source-attributes";

/**
 * @brief The clipboard type carrying a copied list (Bug 478).
 *
 * The data is the copied list element itself, serialized
 * with no indentation: a <dl>, <ul>, or <ol> holding its
 * items, each holding <t> children built from the blocks'
 * runs. Qt's clipboard HTML for a definition list is just
 * two paragraphs — a definition list has no list object, so
 * there is no <dl> in the HTML, and the paste lands as
 * plain paragraphs. This type carries the structure the
 * HTML cannot, so a copied term and its definition paste
 * as a definition-list entry again. The old HTML+roles
 * path is gone: the roles mime type and the HTML
 * reconstruction go away entirely, and paste never splits.
 */
const char ListRunMimeType[] =
    "application/x-doceditor-list-run";

/**
 * @brief The soft line break inside a block: U+2028.
 */
const QChar SoftBreakChar(0x2028);

/**
 * @brief The line an SVG version shows as: SVG is preserved,
 *        never edited, so the version has no editable text.
 * @return The placeholder line.
 */
static QString SvgPlaceholderText()
{
    return QStringLiteral("[SVG artwork, preserved]");
}

bool DocTagEntry::operator==(const DocTagEntry& Other) const
{
    return Name == Other.Name && Attributes == Other.Attributes;
}

/**
 * @brief The stack of tags around a character, outermost first.
 */
using DocTagStack = QVector<DocTagEntry>;

/**
 * @brief One tag's menu entry: its name, its shown label (the
 *        xml2rfc word for the tag), and how it is inserted.
 */
struct DocTagInfo
{
    const char* Name;
    const char* Label;
    bool Empty;      /**< Inserted at the cursor, never wraps or starts. */
    bool BlockLevel; /**< Inserted as its own paragraphs (a list). */
};

/**
 * @brief The 16 tags the menu offers, in menu order: alphabetical
 *        by label. spanx is not offered: the reader converts it
 *        to em, strong, or tt on import.
 */
static const DocTagInfo TagInfos[] = {
    {"bcp14", "BCP 14", false, false},
    {"cref", "Comment", false, false},
    {"contact", "Contact", false, false},
    {"xref", "Cross-Reference", false, false},
    {"em", "Emphasis", false, false},
    {"eref", "External Link", false, false},
    {"iref", "Index Entry", true, false},
    {"br", "Line Break", true, false},
    {"list", "List", false, true},
    {"relref", "Relative Reference", false, false},
    {"strong", "Strong", false, false},
    {"sub", "Subscript", false, false},
    {"sup", "Superscript", false, false},
    {"tt", "Teletype", false, false},
    {"u", "Unicode", false, false},
    {"vspace", "Vertical Space", true, false},
};

/**
 * @brief One nesting rule: what one container may hold.
 *
 * The rules come from the content models in v3.rng. "t" is the
 * paragraph itself; "td" and "th" are table cells, which hold
 * the 13-element cell set: no contact, list, spanx, or vspace.
 * Containers missing here (u, spanx, bcp14,
 * eref, relref, contact children) hold no tags the menu offers.
 */
struct DocNestingRule
{
    const char* Parent;
    const char* Children;
};

static const DocNestingRule NestingRules[] = {
    {"t", "bcp14 br contact cref em eref iref list relref spanx strong sub sup tt u vspace xref"},
    {"td", "bcp14 br cref em eref iref relref strong sub sup tt u xref"},
    {"th", "bcp14 br cref em eref iref relref strong sub sup tt u xref"},
    {"em", "bcp14 br cref eref iref relref strong sub sup tt xref"},
    {"strong", "bcp14 br cref em eref iref relref sub sup tt xref"},
    {"sub", "bcp14 cref em eref iref relref strong sub sup tt xref"},
    {"sup", "bcp14 cref em eref iref relref strong sub sup tt xref"},
    {"tt", "bcp14 br cref em eref iref relref strong sub sup xref"},
    {"xref", "em strong sub sup tt"},
    {"cref", "br em eref relref strong sub sup tt xref"},
};

/**
 * @brief Returns the menu label for a tag, or the name when it has none.
 * @param Name The tag name.
 * @return The label.
 */
static QString LabelFor(const QString& Name)
{
    if (Name == QLatin1String("comment")) {
        return QStringLiteral("Comment");
    }
    for (const DocTagInfo& Info : TagInfos) {
        if (Name == QLatin1String(Info.Name)) {
            return QString::fromLatin1(Info.Label);
        }
    }
    return Name;
}

/**
 * @brief Tests whether a tag may nest inside a container, per the schema.
 * @param Parent The container tag, or "t" for the paragraph itself.
 * @param Child The tag to nest.
 * @return True when the schema allows the nesting.
 */
static bool CanContain(const QString& Parent, const QString& Child)
{
    for (const DocNestingRule& Rule : NestingRules) {
        if (Parent == QLatin1String(Rule.Parent)) {
            return QString::fromLatin1(Rule.Children)
                .split(QLatin1Char(' ')).contains(Child);
        }
    }
    return false;
}

/**
 * @brief Tests whether a stack holds a tag.
 * @param Stack The stack.
 * @param Name The tag name.
 * @return True when the tag is in the stack.
 */
static bool StackHolds(const DocTagStack& Stack, const QString& Name)
{
    for (const DocTagEntry& Entry : Stack) {
        if (Entry.Name == Name) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Returns one attribute's value, or empty when absent.
 * @param Attributes The attributes in file order.
 * @param Name The attribute name.
 * @return The value.
 */
static QString AttributeOf(const QVector<QPair<QString, QString>>& Attributes,
                           const QString& Name)
{
    for (const QPair<QString, QString>& Attribute : Attributes) {
        if (Attribute.first == Name) {
            return Attribute.second;
        }
    }
    return QString();
}

/**
 * @brief Sets an attribute on a tag entry's list, or removes it (Bug 503).
 *
 * An empty value removes the attribute; otherwise the existing
 * pair is updated, or a new pair is appended when there is none.
 * @param Attributes The list to change.
 * @param Name The attribute name.
 * @param Value The new value, empty to remove it.
 */
static void SetTagAttribute(QVector<QPair<QString, QString>>& Attributes,
                            const QString& Name, const QString& Value)
{
    for (int i = 0; i < Attributes.size(); ++i) {
        if (Attributes[i].first == Name) {
            if (Value.isEmpty()) {
                Attributes.remove(i);
            } else {
                Attributes[i].second = Value;
            }
            return;
        }
    }
    if (!Value.isEmpty()) {
        Attributes.append(QPair<QString, QString>(Name, Value));
    }
}

/**
 * @brief Returns the value to pre-fill an option prompt with:
 *        the current value when the attribute is set, otherwise
 *        the schema default (Bug 464 v3). A prompt that shows
 *        the default writes it only when the user accepts it;
 *        clearing the box removes the attribute.
 * @param Attributes The attributes in file order.
 * @param Name The attribute name.
 * @param Default The schema default, empty when there is none.
 * @return The current value, or the default.
 */
static QString PrefillAttribute(
    const QVector<QPair<QString, QString>>& Attributes,
    const QString& Name, const QString& Default)
{
    QString Value = AttributeOf(Attributes, Name);
    return Value.isEmpty() ? Default : Value;
}

/**
 * @brief Sets one attribute, replacing it in place or appending it.
 * @param Attributes The attributes in file order.
 * @param Name The attribute name.
 * @param Value The new value.
 */
static void SetAttribute(QVector<QPair<QString, QString>>& Attributes,
                         const QString& Name, const QString& Value)
{
    for (QPair<QString, QString>& Attribute : Attributes) {
        if (Attribute.first == Name) {
            Attribute.second = Value;
            return;
        }
    }
    Attributes.append(QPair<QString, QString>(Name, Value));
}

/**
 * @brief Removes one attribute, leaving the others in file order.
 * @param Attributes The attributes in file order.
 * @param Name The attribute name.
 */
static void RemoveAttribute(QVector<QPair<QString, QString>>& Attributes,
                            const QString& Name)
{
    for (int i = 0; i < Attributes.size(); ++i) {
        if (Attributes[i].first == Name) {
            Attributes.removeAt(i);
            return;
        }
    }
}

/**
 * @brief Stores attributes as a variant: a list of name/value pairs.
 * @param Attributes The attributes in file order.
 * @return The variant.
 */
static QVariant AttributesToVariant(const QVector<QPair<QString, QString>>& Attributes)
{
    QVariantList List;
    for (const QPair<QString, QString>& Attribute : Attributes) {
        List.append(QVariant(QStringList{Attribute.first, Attribute.second}));
    }
    return List;
}

/**
 * @brief Reads attributes back from a variant.
 * @param Value The variant.
 * @return The attributes in file order.
 */
static QVector<QPair<QString, QString>> AttributesFromVariant(const QVariant& Value)
{
    QVector<QPair<QString, QString>> Attributes;
    for (const QVariant& Item : Value.toList()) {
        QStringList Pair = Item.toStringList();
        if (Pair.size() == 2) {
            Attributes.append(QPair<QString, QString>(Pair[0], Pair[1]));
        }
    }
    return Attributes;
}

/**
 * @brief Serializes one artwork version into a variant map.
 *
 * An SVG version's preserved element travels as an XML string,
 * so the map stays plain data the block format can store.
 * @param Version The version.
 * @return The map.
 */
/**
 * @brief Creates a detached element node.
 *
 * The node has no document until it is added to a tree or
 * serialized; the caller owns it.
 * @param Name The element name.
 * @return The node.
 */
static xmlNodePtr NewNode(const char* Name)
{
    return xmlNewNode(nullptr, BAD_CAST Name);
}

/**
 * @brief Creates a detached text node holding literal text.
 * @param Text The text.
 * @return The node; the caller owns it.
 */
static xmlNodePtr NewTextNode(const QString& Text)
{
    QByteArray Utf8 = Text.toUtf8();
    return xmlNewText(BAD_CAST Utf8.constData());
}

/**
 * @brief Creates a detached comment node holding comment content.
 * @param Text The comment content, without delimiters.
 * @return The node; the caller owns it.
 */
static xmlNodePtr NewCommentNode(const QString& Text)
{
    QByteArray Utf8 = Text.toUtf8();
    return xmlNewComment(BAD_CAST Utf8.constData());
}

/**
 * @brief Appends a text child to a node, merging with a trailing
 *        text node, so runs built piece by piece stay one node.
 * @param Parent The parent node.
 * @param Text The text to append; empty text adds nothing.
 */
static void AddTextChild(xmlNodePtr Parent, const QString& Text)
{
    if (Parent == nullptr || Text.isEmpty()) {
        return;
    }
    xmlNodePtr Last = Parent->last;
    if (Last != nullptr && Last->type == XML_TEXT_NODE) {
        DocDocument::SetNodeText(
            Last, DocDocument::NodeText(Last) + Text);
        return;
    }
    xmlAddChild(Parent, NewTextNode(Text));
}

/**
 * @brief Appends a new child element to a node.
 * @param Parent The parent node.
 * @param Name The element name.
 * @return The child, owned by the parent.
 */
static xmlNodePtr AddElementChild(xmlNodePtr Parent, const char* Name)
{
    return xmlNewChild(Parent, nullptr, BAD_CAST Name, nullptr);
}

/**
 * @brief Returns an attribute's qualified name, prefix included.
 * @param Attribute The attribute.
 * @return The name, such as "anchor" or "xml:lang".
 */
static QString AttributeNameOf(xmlAttrPtr Attribute)
{
    if (Attribute == nullptr || Attribute->name == nullptr) {
        return QString();
    }
    QString Name = QString::fromUtf8(
        reinterpret_cast<const char*>(Attribute->name));
    if (Attribute->ns != nullptr && Attribute->ns->prefix != nullptr) {
        QString Prefix = QString::fromUtf8(
            reinterpret_cast<const char*>(Attribute->ns->prefix));
        if (!Prefix.isEmpty()) {
            return Prefix + QLatin1Char(':') + Name;
        }
    }
    return Name;
}

/**
 * @brief Returns a node's attributes in file order.
 * @param Node The node; borrowed.
 * @return The name/value pairs.
 */
static QVector<QPair<QString, QString>> NodeAttributes(xmlNodePtr Node)
{
    QVector<QPair<QString, QString>> Attributes;
    if (Node == nullptr) {
        return Attributes;
    }
    for (xmlAttrPtr Attribute = Node->properties; Attribute != nullptr;
         Attribute = Attribute->next) {
        xmlChar* Value = xmlNodeGetContent(
            reinterpret_cast<xmlNodePtr>(Attribute));
        QString Text;
        if (Value != nullptr) {
            Text = QString::fromUtf8(
                reinterpret_cast<const char*>(Value));
            xmlFree(Value);
        }
        Attributes.append(
            QPair<QString, QString>(AttributeNameOf(Attribute), Text));
    }
    return Attributes;
}

/**
 * @brief Writes an attribute list onto a node, in order.
 *
 * Attributes the list does not carry are left alone. An empty
 * value writes an empty attribute, as the old model kept empty
 * values.
 * @param Node The node.
 * @param Attributes The name/value pairs.
 */
static void SetNodeAttributes(
    xmlNodePtr Node, const QVector<QPair<QString, QString>>& Attributes)
{
    if (Node == nullptr) {
        return;
    }
    for (const QPair<QString, QString>& Attribute : Attributes) {
        QByteArray Name = Attribute.first.toUtf8();
        QByteArray Value = Attribute.second.toUtf8();
        xmlSetProp(Node, BAD_CAST Name.constData(),
                   BAD_CAST Value.constData());
    }
}

/**
 * @brief Serializes one node with no added indentation.
 *
 * The node is copied under a throwaway document for the dump,
 * so detached nodes serialize the same way attached ones do.
 * A comment node writes as a comment, and a text node as its
 * text. This is the display and clipboard form only, at depth
 * 0, as the section-4 serializer writes it.
 * @param Node The node; borrowed.
 * @return The XML, or empty when the node cannot serialize.
 */
static QString SerializeNode(xmlNodePtr Node)
{
    if (Node == nullptr) {
        return QString();
    }
    if (Node->type == XML_COMMENT_NODE) {
        return QStringLiteral("<!--") + DocDocument::NodeText(Node) +
               QStringLiteral("-->");
    }
    if (Node->type == XML_TEXT_NODE ||
        Node->type == XML_CDATA_SECTION_NODE) {
        return DocDocument::NodeText(Node);
    }
    xmlDocPtr Document = xmlNewDoc(BAD_CAST "1.0");
    if (Document == nullptr) {
        return QString();
    }
    xmlNodePtr Copy = xmlCopyNode(Node, 1);
    if (Copy == nullptr) {
        xmlFreeDoc(Document);
        return QString();
    }
    xmlDocSetRootElement(Document, Copy);
    xmlBufferPtr Buffer = xmlBufferCreate();
    if (Buffer == nullptr) {
        xmlFreeDoc(Document);
        return QString();
    }
    QString Xml;
    // Bug 516: format the XML for readability. xmlNodeDump's
    // format argument is 1 here (pretty-print with indentation),
    // so the XML pane shows indented, readable XML instead of
    // one long compact line per node. The pane is read-only, so
    // the formatting is display only and never reaches the
    // saved file (WriteXml serializes separately).
    if (xmlNodeDump(Buffer, Document, Copy, 0, 1) >= 0 &&
        xmlBufferContent(Buffer) != nullptr) {
        Xml = QString::fromUtf8(
            reinterpret_cast<const char*>(xmlBufferContent(Buffer)));
    }
    xmlBufferFree(Buffer);
    xmlFreeDoc(Document);
    return Xml;
}

/**
 * @brief Reads one element from an XML fragment.
 *
 * The fragment must hold a single element; anything else fails.
 * The returned node is a deep copy that owns its strings, so it
 * survives the throwaway document being freed and frees cleanly.
 * Unlinking the parsed node instead does not work: only the
 * root's doc pointer is cleared, while every descendant still
 * points at the freed document, and freeing the tree then reads
 * freed memory and crashes (Bug 478 — the paste crashed at
 * xmlFreeNode(ListNode), in xmlFreeNodeList).
 * @param Xml The fragment.
 * @param Node Receives the copied element; the caller owns it.
 * @return True when the fragment held one element.
 */
static bool FragmentNodeFromXml(const QString& Xml, xmlNodePtr& Node)
{
    Node = nullptr;
    QByteArray Utf8 = Xml.toUtf8();
    xmlDocPtr Document = xmlReadMemory(
        Utf8.constData(), Utf8.size(), nullptr, nullptr,
        XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (Document == nullptr) {
        return false;
    }
    for (xmlNodePtr Child = Document->children; Child != nullptr;
         Child = Child->next) {
        if (Child->type == XML_ELEMENT_NODE) {
            Node = xmlCopyNode(Child, 1);
            break;
        }
    }
    xmlFreeDoc(Document);
    return Node != nullptr;
}

/**
 * @brief Parses an XML fragment into its top-level elements (Bug 520).
 *
 * The text is wrapped in a <fragment> root and parsed, so a pane
 * holding several top-level elements (paragraphs, lists, sections)
 * parses as one document. Only element children are returned; the
 * whitespace between them (from the pane's pretty-printing) is
 * skipped. Each returned node is a deep copy the caller owns.
 * On a parse error the message and its line (in the pane's own
 * lines, the wrapper line removed) are returned instead.
 * @param Xml The fragment text.
 * @param Nodes Receives the owned element nodes, in order.
 * @param Error Receives the parse error, with its line, on failure.
 * @return True when the fragment parsed.
 */
static bool ParseXmlFragment(const QString& Xml, QVector<xmlNodePtr>& Nodes,
                             QString& Error)
{
    Nodes.clear();
    Error.clear();
    // The closing wrapper follows the text directly (no newline
    // before it), so an unclosed element reports its error on
    // the line that opened it, not on the wrapper's line.
    QString Wrapped = QStringLiteral("<fragment>\n") + Xml +
                      QStringLiteral("</fragment>");
    QByteArray Utf8 = Wrapped.toUtf8();
    xmlResetLastError();
    xmlDocPtr Document = xmlReadMemory(
        Utf8.constData(), Utf8.size(), nullptr, nullptr,
        XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (Document == nullptr) {
        const xmlError* LastError = xmlGetLastError();
        QString Text;
        int Line = 0;
        if (LastError != nullptr) {
            Line = LastError->line;
            if (LastError->message != nullptr) {
                Text = QString::fromUtf8(
                    reinterpret_cast<const char*>(LastError->message))
                           .trimmed();
            }
        }
        // The wrapper's <fragment> line is line 1, so the pane's
        // own lines are one less than the reported line.
        if (Line > 1) {
            --Line;
        }
        Error = Text.isEmpty()
            ? QStringLiteral("The XML could not be parsed.")
            : (Line > 0
               ? QStringLiteral("Line %1: %2").arg(Line).arg(Text)
               : Text);
        return false;
    }
    xmlNodePtr Root = xmlDocGetRootElement(Document);
    if (Root != nullptr) {
        for (xmlNodePtr Child = Root->children; Child != nullptr;
             Child = Child->next) {
            if (Child->type == XML_ELEMENT_NODE) {
                Nodes.append(xmlCopyNode(Child, 1));
            }
        }
    }
    xmlFreeDoc(Document);
    return true;
}

/**
 * @brief True when an <artwork> node holds element children: an
 *        SVG version, kept whole and never edited.
 * @param Artwork The <artwork> node; borrowed.
 * @return True when the version is preserved SVG.
 */
static bool ArtworkNodeIsSvg(xmlNodePtr Artwork)
{
    if (Artwork == nullptr) {
        return false;
    }
    for (xmlNodePtr Child = Artwork->children; Child != nullptr;
         Child = Child->next) {
        if (Child->type == XML_ELEMENT_NODE) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Returns the <artwork> version nodes of a figure node, in order.
 * @param FigureNode The <figure>, <artset>, or <artwork> node; borrowed.
 * @return The version nodes; borrowed. One node, itself, for a
 *         bare <artwork>.
 */
static QVector<xmlNodePtr> FigureVersionNodes(xmlNodePtr FigureNode)
{
    QVector<xmlNodePtr> Versions;
    if (FigureNode == nullptr) {
        return Versions;
    }
    if (DocDocument::NodeNameEquals(FigureNode, "artwork")) {
        Versions.append(FigureNode);
        return Versions;
    }
    xmlNodePtr Artset = DocDocument::ChildElement(FigureNode, "artset");
    xmlNodePtr Holder = Artset != nullptr ? Artset : FigureNode;
    for (xmlNodePtr Child : DocDocument::ChildElements(Holder, "artwork")) {
        Versions.append(Child);
    }
    return Versions;
}

/**
 * @brief Serializes a figure node and the index of its visible
 *        version into a variant map for the payload property.
 *
 * The caption is not part of the map: it lives in the caption
 * block's text, and is read back from there. The visible
 * version's text is stored as it stands: the read-back
 * replaces it with the artwork blocks' lines.
 * @param FigureNode The <figure>, <artset>, or <artwork> node; borrowed.
 * @param Visible The index of the version showing.
 * @return The map.
 */
static QVariant FigureToVariant(xmlNodePtr FigureNode, int Visible)
{
    QVariantMap Map;
    Map[QStringLiteral("node")] = SerializeNode(FigureNode);
    Map[QStringLiteral("visible")] = Visible;
    return Map;
}

/**
 * @brief Rebuilds a figure node and its visible version index
 *        from a variant map.
 * @param Value The map.
 * @param FigureNode Receives the detached figure node; the caller owns it.
 * @param Visible Receives the visible version index, clamped
 *                to the versions.
 * @return False when the map is not a figure payload.
 */
static bool FigureFromVariant(const QVariant& Value, xmlNodePtr& FigureNode,
                              int& Visible)
{
    FigureNode = nullptr;
    Visible = 0;
    if (!Value.isValid()) {
        return false;
    }
    QVariantMap Map = Value.toMap();
    if (!Map.contains(QStringLiteral("node"))) {
        return false;
    }
    xmlNodePtr Node = nullptr;
    if (!FragmentNodeFromXml(Map.value(QStringLiteral("node")).toString(),
                             Node)) {
        return false;
    }
    if (!DocDocument::NodeNameEquals(Node, "figure") &&
        !DocDocument::NodeNameEquals(Node, "artset") &&
        !DocDocument::NodeNameEquals(Node, "artwork")) {
        xmlFreeNode(Node);
        return false;
    }
    if (FigureVersionNodes(Node).isEmpty()) {
        xmlFreeNode(Node);
        return false;
    }
    FigureNode = Node;
    Visible = Map.value(QStringLiteral("visible")).toInt();
    int Count = FigureVersionNodes(FigureNode).size();
    if (Visible < 0 || Visible >= Count) {
        Visible = 0;
    }
    return true;
}

/**
 * @brief One row's mark in a table payload: its section and its
 *        kept attributes.
 *
 * Marks travel in grid order, header rows first, then body,
 * then footer, so a grid row finds its mark by index. The
 * anchor is kept apart from the other attributes, as in the
 * model.
 */
struct DocTableRowMark
{
    QString Section;
    QString Anchor;
    QVector<QPair<QString, QString>> Attributes;
};

/**
 * @brief Builds the row marks of a table, in grid order.
 * @param Table The table.
 * @return The marks: header rows, body rows, then footer rows.
 */
/**
 * @brief Builds the row marks of a table node, in grid order.
 * @param TableNode The <table> node; borrowed.
 * @return The marks: header rows, body rows, then footer rows.
 */
static QVector<DocTableRowMark> TableMarksOf(xmlNodePtr TableNode)
{
    QVector<DocTableRowMark> Marks;
    if (TableNode == nullptr) {
        return Marks;
    }
    static const char* Sections[] = {"thead", "tbody", "tfoot"};
    for (const char* Section : Sections) {
        xmlNodePtr SectionNode =
            DocDocument::ChildElement(TableNode, Section);
        if (SectionNode == nullptr) {
            continue;
        }
        for (xmlNodePtr Row :
             DocDocument::ChildElements(SectionNode, "tr")) {
            DocTableRowMark Mark;
            Mark.Section = QString::fromLatin1(Section);
            for (const QPair<QString, QString>& Attribute :
                 NodeAttributes(Row)) {
                if (Attribute.first == QLatin1String("anchor")) {
                    Mark.Anchor = Attribute.second;
                } else {
                    Mark.Attributes.append(Attribute);
                }
            }
            Marks.append(Mark);
        }
    }
    return Marks;
}

/**
 * @brief Returns the section of one grid row, from its marks.
 *
 * A row past the end of the marks takes the last mark's
 * section, and a table with no marks at all reads every row as
 * a body row, so a grid the payload does not describe still
 * reads back as a table.
 * @param Marks The row marks, in grid order.
 * @param Row The grid row index.
 * @return The section name: thead, tbody, or tfoot.
 */
static QString SectionOfRow(const QVector<DocTableRowMark>& Marks, int Row)
{
    if (Marks.isEmpty()) {
        return QStringLiteral("tbody");
    }
    int At = Row < Marks.size() ? Row : Marks.size() - 1;
    if (At < 0) {
        At = 0;
    }
    QString Section = Marks[At].Section;
    if (Section != QLatin1String("thead") &&
        Section != QLatin1String("tfoot")) {
        return QStringLiteral("tbody");
    }
    return Section;
}

/**
 * @brief Converts a table node's kept attributes and row marks
 *        to a payload variant.
 *
 * The map holds the table's own attributes, the three section
 * attributes, and the row marks in grid order. The caption and
 * the cell text are not stored: they live in the blocks. Spans
 * are not stored either: they are read from the grid itself.
 * @param TableNode The <table> node; borrowed. Only its
 *        attribute vectors are used.
 * @param Marks The row marks, in grid order.
 * @return The payload map.
 */
static QVariant TableToVariant(xmlNodePtr TableNode,
                               const QVector<DocTableRowMark>& Marks)
{
    QVariantMap Map;
    Map[QStringLiteral("attrs")] =
        AttributesToVariant(NodeAttributes(TableNode));
    Map[QStringLiteral("headattrs")] = AttributesToVariant(
        NodeAttributes(DocDocument::ChildElement(TableNode, "thead")));
    Map[QStringLiteral("bodyattrs")] = AttributesToVariant(
        NodeAttributes(DocDocument::ChildElement(TableNode, "tbody")));
    Map[QStringLiteral("footattrs")] = AttributesToVariant(
        NodeAttributes(DocDocument::ChildElement(TableNode, "tfoot")));
    QVariantList Rows;
    for (const DocTableRowMark& Mark : Marks) {
        QVariantMap RowMap;
        RowMap[QStringLiteral("section")] = Mark.Section;
        RowMap[QStringLiteral("anchor")] = Mark.Anchor;
        RowMap[QStringLiteral("attrs")] =
            AttributesToVariant(Mark.Attributes);
        Rows.append(RowMap);
    }
    Map[QStringLiteral("rows")] = Rows;
    return Map;
}

/**
 * @brief Rebuilds a table shell node and its row marks from a
 *        payload variant.
 *
 * The shell is a <table> holding the table's own attributes
 * and the three section attributes; its caption and rows are
 * left empty, since they come from the blocks and the grid.
 * The sections are always present, so the attributes round
 * trip even when a section holds no rows.
 * @param Value The map.
 * @param TableNode Receives the detached shell node; the caller owns it.
 * @param Marks Receives the row marks, in grid order.
 * @return False when the map is not a table payload.
 */
static bool TableFromVariant(const QVariant& Value, xmlNodePtr& TableNode,
                             QVector<DocTableRowMark>& Marks)
{
    TableNode = nullptr;
    if (!Value.isValid()) {
        return false;
    }
    QVariantMap Map = Value.toMap();
    if (!Map.contains(QStringLiteral("rows"))) {
        return false;
    }
    xmlNodePtr Table = NewNode("table");
    SetNodeAttributes(
        Table, AttributesFromVariant(Map.value(QStringLiteral("attrs"))));
    auto AddSection = [&](const char* Name, const QString& Key) {
        xmlNodePtr Section = AddElementChild(Table, Name);
        SetNodeAttributes(
            Section, AttributesFromVariant(Map.value(Key)));
    };
    AddSection("thead", QStringLiteral("headattrs"));
    AddSection("tbody", QStringLiteral("bodyattrs"));
    AddSection("tfoot", QStringLiteral("footattrs"));
    Marks.clear();
    for (const QVariant& Item : Map.value(QStringLiteral("rows")).toList()) {
        QVariantMap RowMap = Item.toMap();
        DocTableRowMark Mark;
        Mark.Section = RowMap.value(QStringLiteral("section")).toString();
        Mark.Anchor = RowMap.value(QStringLiteral("anchor")).toString();
        Mark.Attributes =
            AttributesFromVariant(RowMap.value(QStringLiteral("attrs")));
        Marks.append(Mark);
    }
    TableNode = Table;
    return true;
}

/**
 * @brief Converts one cell node's own data to a variant.
 *
 * The map holds the header flag, the anchor and align
 * attributes as kept, and the other kept attributes. The runs
 * are not stored: they live in the cell's blocks. Spans are
 * not stored either: they are read from the grid itself.
 * @param CellNode The <td> or <th> node; borrowed. Only its
 *        header flag and attributes are used.
 * @return The cell map.
 */
static QVariant TableCellToVariant(xmlNodePtr CellNode)
{
    QVariantMap Map;
    Map[QStringLiteral("header")] =
        DocDocument::NodeNameEquals(CellNode, "th");
    QString Anchor;
    QString Align;
    QVector<QPair<QString, QString>> Kept;
    for (const QPair<QString, QString>& Attribute :
         NodeAttributes(CellNode)) {
        if (Attribute.first == QLatin1String("anchor")) {
            Anchor = Attribute.second;
        } else if (Attribute.first == QLatin1String("align")) {
            Align = Attribute.second;
        } else if (Attribute.first == QLatin1String("colspan") ||
                   Attribute.first == QLatin1String("rowspan")) {
            // Spans are read from the grid, not stored.
        } else {
            Kept.append(Attribute);
        }
    }
    Map[QStringLiteral("anchor")] = Anchor;
    Map[QStringLiteral("align")] = Align;
    Map[QStringLiteral("attrs")] = AttributesToVariant(Kept);
    return Map;
}

/**
 * @brief Rebuilds one cell node from a variant.
 * @param Value The map.
 * @param CellNode Receives the detached <td> or <th> node; the
 *        caller owns it. Its runs and spans are left alone,
 *        since they come from the cell's blocks and the grid.
 * @return False when the map is not a cell map.
 */
static bool TableCellFromVariant(const QVariant& Value, xmlNodePtr& CellNode)
{
    CellNode = nullptr;
    if (!Value.isValid()) {
        return false;
    }
    QVariantMap Map = Value.toMap();
    if (!Map.contains(QStringLiteral("header"))) {
        return false;
    }
    xmlNodePtr Cell = NewNode(
        Map.value(QStringLiteral("header")).toBool() ? "th" : "td");
    QString Anchor = Map.value(QStringLiteral("anchor")).toString();
    if (!Anchor.isEmpty()) {
        DocDocument::SetNodeAttribute(Cell, "anchor", Anchor);
    }
    QString Align = Map.value(QStringLiteral("align")).toString();
    if (!Align.isEmpty()) {
        DocDocument::SetNodeAttribute(Cell, "align", Align);
    }
    SetNodeAttributes(
        Cell, AttributesFromVariant(Map.value(QStringLiteral("attrs"))));
    CellNode = Cell;
    return true;
}

/**
 * @brief Reads a table block's payload.
 * @param Block The block.
 * @param TableNode Receives the detached shell node; the caller
 *        owns it.
 * @param Marks Receives the row marks, in grid order.
 * @return False when the block carries no table payload.
 */
static bool TablePayloadOf(const QTextBlock& Block, xmlNodePtr& TableNode,
                           QVector<DocTableRowMark>& Marks)
{
    return TableFromVariant(
        Block.blockFormat().property(DocTablePayloadProperty), TableNode,
        Marks);
}

/**
 * @brief True when a block is the first block of a header cell.
 * @param Block The block.
 * @return True when the block carries a cell map with the
 *         header flag set.
 */
static bool IsHeaderCellBlock(const QTextBlock& Block)
{
    if (!IsTableCellBlock(Block)) {
        return false;
    }
    xmlNodePtr CellNode = nullptr;
    bool IsHeader = TableCellFromVariant(
        Block.blockFormat().property(DocTableCellProperty), CellNode) &&
        DocDocument::NodeNameEquals(CellNode, "th");
    xmlFreeNode(CellNode);
    return IsHeader;
}

/**
 * @brief True when the cursor's block, or any block its
 *        selection touches, is a table block.
 * @param Document The document.
 * @param Cursor The cursor.
 * @return True when the cursor touches a table.
 */
static bool TouchesTable(QTextDocument* Document, const QTextCursor& Cursor)
{
    if (!Cursor.hasSelection()) {
        return IsTableBlock(Cursor.block()) ||
               Cursor.currentTable() != nullptr;
    }
    int Start = Cursor.selectionStart();
    int End = Cursor.selectionEnd();
    QTextBlock Block = Document->findBlock(Start);
    while (Block.isValid() && Block.position() < End) {
        if (IsTableBlock(Block) || TableOfBlock(Document, Block) != nullptr) {
            return true;
        }
        Block = Block.next();
    }
    return false;
}

/**
 * @brief True when the cursor's block, or any block its
 *        selection touches, is a table caption block.
 * @param Document The document.
 * @param Cursor The cursor.
 * @return True when the cursor touches a table caption.
 */
static bool TouchesTableCaption(QTextDocument* Document,
                                const QTextCursor& Cursor)
{
    if (!Cursor.hasSelection()) {
        return IsTableCaptionBlock(Cursor.block());
    }
    int Start = Cursor.selectionStart();
    int End = Cursor.selectionEnd();
    QTextBlock Block = Document->findBlock(Start);
    while (Block.isValid() && Block.position() < End) {
        if (IsTableCaptionBlock(Block)) {
            return true;
        }
        Block = Block.next();
    }
    return false;
}

/**
 * @brief True when a selection partly crosses a table frame:
 *        it touches the frame without covering it whole, and
 *        without sitting wholly inside it.
 *
 * Deleting or replacing such a selection would delete the
 * whole frame: Qt removes a frame when a text change crosses
 * its boundary. A selection wholly inside one table only
 * clears cell text, and a selection covering a frame whole
 * removes it cleanly, so neither counts as crossing.
 * @param Document The document.
 * @param Cursor The cursor, with a selection.
 * @return True when the selection partly crosses a frame.
 */
static bool SelectionPartlyCrossesTable(QTextDocument* Document,
                                        const QTextCursor& Cursor)
{
    if (!Cursor.hasSelection()) {
        return false;
    }
    int Start = Cursor.selectionStart();
    int End = Cursor.selectionEnd();
    QSet<QTextTable*> Tables;
    for (QTextBlock Block = Document->findBlock(Start);
         Block.isValid() && Block.position() < End;
         Block = Block.next()) {
        if (QTextTable* Table = TableOfBlock(Document, Block)) {
            Tables.insert(Table);
        }
    }
    // The block holding the selection's end counts too: a
    // selection ending at the first position of a cell block
    // touches the frame, though the loop above stops before it.
    QTextBlock EndBlock = Document->findBlock(End);
    if (EndBlock.isValid()) {
        if (QTextTable* Table = TableOfBlock(Document, EndBlock)) {
            Tables.insert(Table);
        }
    }
    for (QTextTable* Table : Tables) {
        int First = Table->firstPosition();
        int Last = Table->lastPosition();
        bool EntirelyInside = Start >= First && End <= Last;
        bool FullyCovers = Start <= First - 1 && End >= Last + 1;
        if (!EntirelyInside && !FullyCovers) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Returns the label the version picker shows for one
 *        artwork version: its type, when it has one.
 * @param Version The version.
 * @return The label.
 */
static QString FigureVersionLabel(xmlNodePtr Artwork)
{
    QString Type = DocDocument::NodeAttribute(Artwork, "type");
    if (!Type.isEmpty()) {
        return Type;
    }
    return ArtworkNodeIsSvg(Artwork) ? QStringLiteral("SVG")
                                     : QStringLiteral("Artwork");
}

/**
 * @brief Stores one tag entry as a variant map.
 * @param Entry The entry.
 * @return The variant.
 */
static QVariant EntryToVariant(const DocTagEntry& Entry)
{
    QVariantMap Map;
    Map[QStringLiteral("name")] = Entry.Name;
    Map[QStringLiteral("attrs")] = AttributesToVariant(Entry.Attributes);
    return Map;
}

/**
 * @brief Reads one tag entry back from a variant map.
 * @param Value The variant.
 * @return The entry.
 */
static DocTagEntry EntryFromVariant(const QVariant& Value)
{
    DocTagEntry Entry;
    QVariantMap Map = Value.toMap();
    Entry.Name = Map.value(QStringLiteral("name")).toString();
    Entry.Attributes = AttributesFromVariant(Map.value(QStringLiteral("attrs")));
    return Entry;
}

/**
 * @brief Stores a tag stack as a variant list.
 * @param Stack The stack.
 * @return The variant.
 */
static QVariant StackToVariant(const DocTagStack& Stack)
{
    QVariantList List;
    for (const DocTagEntry& Entry : Stack) {
        List.append(EntryToVariant(Entry));
    }
    return List;
}

/**
 * @brief Reads a tag stack back from a variant list.
 * @param Value The variant.
 * @return The stack.
 */
static DocTagStack StackFromVariant(const QVariant& Value)
{
    DocTagStack Stack;
    for (const QVariant& Item : Value.toList()) {
        Stack.append(EntryFromVariant(Item));
    }
    return Stack;
}

/**
 * @brief Returns the tag stack a character format carries, if any.
 * @param Format The character format.
 * @return The stack, empty when the format carries none.
 */
static DocTagStack StackOf(const QTextCharFormat& Format)
{
    QVariant Value = Format.property(DocStackProperty);
    if (!Value.isValid()) {
        return DocTagStack();
    }
    return StackFromVariant(Value);
}

/**
 * @brief Returns the hover text for a link, comment, contact,
 *        or Unicode tag.
 * @param Entry The tag entry.
 * @return The hover text.
 */
static QString TooltipForEntry(const DocTagEntry& Entry)
{
    if (Entry.Name == QLatin1String("xref")) {
        QString Target = AttributeOf(Entry.Attributes, QStringLiteral("target"));
        return QStringLiteral("Cross-reference to ") +
               (Target.isEmpty() ? QStringLiteral("(no target)") : Target);
    }
    if (Entry.Name == QLatin1String("eref")) {
        QString Target = AttributeOf(Entry.Attributes, QStringLiteral("target"));
        return QStringLiteral("External link to ") +
               (Target.isEmpty() ? QStringLiteral("(no target)") : Target);
    }
    if (Entry.Name == QLatin1String("relref")) {
        QString Target = AttributeOf(Entry.Attributes, QStringLiteral("target"));
        return QStringLiteral("Relative reference to ") +
               (Target.isEmpty() ? QStringLiteral("(no target)") : Target);
    }
    if (Entry.Name == QLatin1String("cref")) {
        QString Source = AttributeOf(Entry.Attributes, QStringLiteral("source"));
        return Source.isEmpty() ? QStringLiteral("Comment")
                                : QStringLiteral("Comment from ") + Source;
    }
    if (Entry.Name == QLatin1String("u")) {
        return QStringLiteral("Unicode");
    }
    return QStringLiteral("Contact");
}

/**
 * @brief Returns the hover text for a marker.
 * @param Marker The marker entry.
 * @return The hover text.
 */
static QString MarkerTooltip(const DocTagEntry& Marker)
{
    if (Marker.Name == QLatin1String("br")) {
        return QStringLiteral("Line Break");
    }
    if (Marker.Name == QLatin1String("vspace")) {
        QString Lines = AttributeOf(Marker.Attributes, QStringLiteral("blankLines"));
        if (Lines.isEmpty()) {
            Lines = QStringLiteral("0");
        }
        return QStringLiteral("Vertical space, %1 blank lines").arg(Lines);
    }
    QString Item = AttributeOf(Marker.Attributes, QStringLiteral("item"));
    if (Marker.Name == QLatin1String("iref")) {
        QString Text = QStringLiteral("Index entry: ") + Item;
        QString Subitem = AttributeOf(Marker.Attributes, QStringLiteral("subitem"));
        if (!Subitem.isEmpty()) {
            Text += QStringLiteral(", ") + Subitem;
        }
        if (AttributeOf(Marker.Attributes, QStringLiteral("primary")) ==
            QLatin1String("true")) {
            Text += QStringLiteral(" (primary)");
        }
        return Text;
    }
    if (Marker.Name == QLatin1String("xref") ||
        Marker.Name == QLatin1String("eref") ||
        Marker.Name == QLatin1String("relref")) {
        QString Text = LabelFor(Marker.Name) + QStringLiteral(": ") +
            AttributeOf(Marker.Attributes, QStringLiteral("target"));
        if (Marker.Name == QLatin1String("xref")) {
            QString Section =
                AttributeOf(Marker.Attributes, QStringLiteral("section"));
            if (!Section.isEmpty()) {
                Text += QStringLiteral(", section ") + Section;
            }
            QString Format =
                AttributeOf(Marker.Attributes, QStringLiteral("format"));
            if (Format == QLatin1String("counter")) {
                Text += QStringLiteral(" (counter)");
            }
            QString SectionFormat = AttributeOf(
                Marker.Attributes, QStringLiteral("sectionFormat"));
            if (SectionFormat == QLatin1String("bare")) {
                Text += QStringLiteral(" (bare)");
            }
        }
        return Text;
    }
    if (Marker.Name == QLatin1String("comment")) {
        return QStringLiteral("Comment");
    }
    return LabelFor(Marker.Name);
}

/**
 * @brief Returns the marker text shown for a marker entry.
 *
 * A Break shows as one symbol, a Vertical Space as its symbol
 * plus its blank-line count, and an Index Entry as its item
 * text, so the item can be read (and edited) in place. A bare
 * cross-reference shows its rendered form, so the editor reads
 * the way the published RFC does. Any other marker shows a
 * placeholder glyph, so the marker still has a character to
 * stand on: an Index Entry with no item, and every element
 * with no text of its own (Bug 440).
 * @param Marker The marker entry.
 * @param DisplayNames The anchor-to-display-name map, used for
 *        a bare cross-reference (Bug 405).
 * @return The marker text.
 */
/**
 * @brief The text a bare cross-reference shows in the editor.
 *
 * An empty xref renders as its form, not as a placeholder
 * glyph, so the editor reads the way the published RFC does:
 * a bracket shows "[RFC793]", a section "of" shows
 * "Section 4.2.11 of [RFC8446]", a bare section shows
 * "Section 5.4". A counter shows the bracketed target: the
 * number itself is resolved at render time, and the hover
 * says it is a counter. A targetless xref keeps the glyph.
 * A bracket for a mapped anchor shows the display name
 * instead (Bug 405): with a displayreference for RFC793 to
 * TCP, the bracket reads "[TCP]". The mapping applies only
 * to the plain bracket form (RFC 7991 section 2.19: it
 * applies only to xrefs whose format is "default"); a
 * section or counter form keeps the anchor.
 * @param Marker The cross-reference entry.
 * @param DisplayNames The anchor-to-display-name map.
 * @return The display text, or the placeholder glyph.
 */
static QString XrefMarkerText(const DocTagEntry& Marker,
                              const QVariantMap& DisplayNames)
{
    QString Target =
        AttributeOf(Marker.Attributes, QStringLiteral("target"));
    if (Target.isEmpty()) {
        return QString(EmptyItemGlyph);
    }
    QString Format =
        AttributeOf(Marker.Attributes, QStringLiteral("format"));
    if (Format == QLatin1String("counter")) {
        return QStringLiteral("[") + Target + QStringLiteral("]");
    }
    QString Section =
        AttributeOf(Marker.Attributes, QStringLiteral("section"));
    if (!Section.isEmpty()) {
        QString SectionFormat = AttributeOf(
            Marker.Attributes, QStringLiteral("sectionFormat"));
        if (SectionFormat == QLatin1String("bare")) {
            return QStringLiteral("Section ") + Section;
        }
        return QStringLiteral("Section ") + Section +
            QStringLiteral(" of [") + Target + QStringLiteral("]");
    }
    QString Shown = Target;
    if ((Format.isEmpty() || Format == QLatin1String("default")) &&
        DisplayNames.contains(Target)) {
        Shown = DisplayNames.value(Target).toString();
    }
    return QStringLiteral("[") + Shown + QStringLiteral("]");
}

static QString MarkerText(const DocTagEntry& Marker,
                          const QVariantMap& DisplayNames)
{
    if (Marker.Name == QLatin1String("br")) {
        return QString(BreakGlyph);
    }
    if (Marker.Name == QLatin1String("vspace")) {
        QString Lines = AttributeOf(Marker.Attributes, QStringLiteral("blankLines"));
        if (Lines.isEmpty()) {
            Lines = QStringLiteral("0");
        }
        return QString(VspaceGlyph) + Lines;
    }
    if (Marker.Name == QLatin1String("xref")) {
        return XrefMarkerText(Marker, DisplayNames);
    }
    if (Marker.Name == QLatin1String("iref")) {
        // Bug 503: an index entry's marker shows only a glyph,
        // never the item text. The item is the word beside the
        // marker (the word being indexed), so showing it again
        // in the marker doubles the word (RTTRTT). The item
        // stays in the marker's attributes and shows in the
        // hover; it is edited via Edit index entry, not by
        // typing in the marker (Bug 514 approach). The glyph is
        // the circled i (Doug's choice), not the replacement
        // character, which reads as "couldn't be displayed".
        return QString(IrefGlyph);
    }
    QString Item = AttributeOf(Marker.Attributes, QStringLiteral("item"));
    // A newline in an item would split the block when inserted, so
    // newlines become spaces in the marker text.
    Item.replace(QLatin1Char('\n'), QLatin1Char(' '));
    Item.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return Item.isEmpty() ? QString(EmptyItemGlyph) : Item;
}

/**
 * @brief Sets every visual property of a format from a tag stack.
 *
 * Bold, italic, subscript, superscript, typewriter, link
 * styling, and the hover text are all set explicitly, so a
 * format copied from other text never keeps a stale visual. A
 * bcp14 shows bold; a spanx shows italic, or bold when its
 * style says so. A u shows plain, with a Unicode hover note:
 * it wraps non-ASCII text, it is not an underline.
 * @param Format The format to fill.
 * @param Stack The tag stack.
 */
static void ApplyStackVisuals(QTextCharFormat& Format, const DocTagStack& Stack)
{
    bool Bold = false;
    bool Italic = false;
    bool Subscript = false;
    bool Superscript = false;
    bool Typewriter = false;
    bool Link = false;
    QString Tooltip;
    for (int i = Stack.size() - 1; i >= 0; --i) {
        const DocTagEntry& Entry = Stack[i];
        if (Entry.Name == QLatin1String("strong")) {
            Bold = true;
        } else if (Entry.Name == QLatin1String("em")) {
            Italic = true;
        } else if (Entry.Name == QLatin1String("sub")) {
            Subscript = true;
        } else if (Entry.Name == QLatin1String("sup")) {
            Superscript = true;
        } else if (Entry.Name == QLatin1String("tt")) {
            Typewriter = true;
        } else if (Entry.Name == QLatin1String("spanx")) {
            if (AttributeOf(Entry.Attributes, QStringLiteral("style")) ==
                QLatin1String("strong")) {
                Bold = true;
            } else {
                Italic = true;
            }
        } else if (Entry.Name == QLatin1String("bcp14")) {
            Bold = true;
        } else if (Entry.Name == QLatin1String("xref") ||
                   Entry.Name == QLatin1String("eref") ||
                   Entry.Name == QLatin1String("relref") ||
                   Entry.Name == QLatin1String("cref") ||
                   Entry.Name == QLatin1String("contact") ||
                   Entry.Name == QLatin1String("u")) {
            if (Entry.Name != QLatin1String("contact") &&
                Entry.Name != QLatin1String("u")) {
                Link = true;
            }
            if (Tooltip.isEmpty()) {
                Tooltip = TooltipForEntry(Entry);
            }
        }
    }
    Format.setFontWeight(Bold ? QFont::Bold : QFont::Normal);
    Format.setFontItalic(Italic);
    Format.setUnderlineStyle(Link ? QTextCharFormat::SingleUnderline
                                  : QTextCharFormat::NoUnderline);
    Format.setVerticalAlignment(Subscript ? QTextCharFormat::AlignSubScript
                               : Superscript ? QTextCharFormat::AlignSuperScript
                                             : QTextCharFormat::AlignNormal);
    if (Typewriter) {
        Format.setFontFamilies(QStringList{QStringLiteral("Courier New")});
    } else {
        Format.clearProperty(QTextFormat::FontFamilies);
    }
    if (Link) {
        Format.setForeground(QColor(QStringLiteral("#B0C4E8")));
    } else {
        Format.clearProperty(QTextFormat::ForegroundBrush);
    }
    Format.setToolTip(Tooltip);
}

/**
 * @brief Adds a marker's own styling on top of its stack styling.
 *
 * The marker hover text wins over the stack's, and an Index Entry
 * marker shows link-styled, like the links it resembles.
 * @param Format The format to fill.
 * @param Marker The marker entry.
 */
static void ApplyMarkerVisuals(QTextCharFormat& Format, const DocTagEntry& Marker)
{
    Format.setToolTip(MarkerTooltip(Marker));
    if (Marker.Name == QLatin1String("iref") ||
        Marker.Name == QLatin1String("xref") ||
        Marker.Name == QLatin1String("eref") ||
        Marker.Name == QLatin1String("relref") ||
        Marker.Name == QLatin1String("cref")) {
        Format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
        Format.setForeground(QColor(QStringLiteral("#B0C4E8")));
    }
    if (Marker.Name == QLatin1String("comment")) {
        Format.setFontItalic(true);
    }
}

/**
 * @brief Builds a fresh character format for a tag stack.
 * @param Stack The tag stack.
 * @return The format.
 */
static QTextCharFormat FormatForStack(const DocTagStack& Stack)
{
    QTextCharFormat Format;
    Format.setProperty(DocStackProperty, StackToVariant(Stack));
    ApplyStackVisuals(Format, Stack);
    return Format;
}

/**
 * @brief Builds a fresh character format for a marker in a tag stack.
 * @param Stack The tag stack around the marker.
 * @param Marker The marker entry.
 * @return The format.
 */
static QTextCharFormat FormatForMarker(const DocTagStack& Stack, const DocTagEntry& Marker)
{
    QTextCharFormat Format = FormatForStack(Stack);
    Format.setProperty(DocMarkerProperty, EntryToVariant(Marker));
    ApplyMarkerVisuals(Format, Marker);
    return Format;
}

/**
 * @brief Returns the stack entries a tag contributes when applied.
 *
 * A contact contributes two entries, the contact and its
 * organization, because a contact holds an organization element
 * rather than text. Every other tag contributes one.
 * @param Entry The tag entry.
 * @return The entries to push.
 */
static QVector<DocTagEntry> EntriesForTag(const DocTagEntry& Entry)
{
    QVector<DocTagEntry> Entries;
    Entries.append(Entry);
    if (Entry.Name == QLatin1String("contact")) {
        DocTagEntry Organization;
        Organization.Name = QStringLiteral("organization");
        Entries.append(Organization);
    }
    return Entries;
}

/**
 * @brief Returns a stack with a tag pushed, unless it already holds it.
 * @param Stack The stack.
 * @param Entry The tag entry to push.
 * @return The stack with the tag.
 */
static DocTagStack StackWithTag(const DocTagStack& Stack, const DocTagEntry& Entry)
{
    if (StackHolds(Stack, Entry.Name)) {
        return Stack;
    }
    DocTagStack Changed = Stack;
    for (const DocTagEntry& Pushed : EntriesForTag(Entry)) {
        Changed.append(Pushed);
    }
    return Changed;
}

/**
 * @brief Returns a stack with every occurrence of a tag removed.
 *
 * Removing a contact also removes the organization entry that
 * follows it.
 * @param Stack The stack.
 * @param Name The tag name.
 * @return The stack without the tag.
 */
static DocTagStack StackWithoutTag(const DocTagStack& Stack, const QString& Name)
{
    DocTagStack Kept;
    for (int i = 0; i < Stack.size(); ++i) {
        if (Stack[i].Name == Name) {
            if (Name == QLatin1String("contact") && i + 1 < Stack.size() &&
                Stack[i + 1].Name == QLatin1String("organization")) {
                ++i;
            }
            continue;
        }
        Kept.append(Stack[i]);
    }
    return Kept;
}

/**
 * @brief Returns a stack with the innermost occurrence of a tag removed.
 *
 * Ending a tag pops the one started last, which matters for tags
 * that nest inside themselves. Ending a contact pops the contact
 * and its organization together.
 * @param Stack The stack.
 * @param Name The tag name.
 * @return The stack without that occurrence.
 */
static DocTagStack StackWithoutLast(const DocTagStack& Stack, const QString& Name)
{
    for (int i = Stack.size() - 1; i >= 0; --i) {
        if (Stack[i].Name == Name) {
            DocTagStack Kept = Stack;
            Kept.remove(i);
            if (Name == QLatin1String("contact") && i < Kept.size() &&
                Kept[i].Name == QLatin1String("organization")) {
                Kept.remove(i);
            }
            return Kept;
        }
    }
    return Stack;
}

/**
 * @brief One step in rebuilding runs from fragments: open or close
 *        a tag, a run of text, or a marker.
 */
struct DocEvent
{
    enum class Kind { OpenTag, CloseTag, TextRun, MarkerRun };
    Kind EventKind = Kind::TextRun;
    DocTagEntry Tag;
    QString Text;
};

/**
 * @brief Builds runs from events, consuming through the matching close.
 *
 * Markers become element runs: a Break keeps its stored
 * attributes, a Vertical Space takes its blank-line count from
 * the digits typed in its marker text, and an Index Entry takes
 * its item from its marker text, so the item can be edited in
 * place. Characters typed into a Break or Vertical Space marker
 * come out as plain text after the marker, so nothing typed is
 * lost.
 * @param Events The events.
 * @param Pos The next event to read; advanced past the events used.
 * @param Runs Filled with the runs.
 */
/**
 * @brief Tests whether a node is a list element standing alone as a body node.
 * @param Node The node; borrowed.
 * @return True for a list, ul, ol, or dl element.
 */
static bool IsListNode(xmlNodePtr Node)
{
    return Node != nullptr && Node->type == XML_ELEMENT_NODE &&
           (DocDocument::NodeNameEquals(Node, "list") ||
            DocDocument::NodeNameEquals(Node, "ul") ||
            DocDocument::NodeNameEquals(Node, "ol") ||
            DocDocument::NodeNameEquals(Node, "dl"));
}

/**
 * @brief True when a node is a sourcecode element standing alone.
 * @param Node The node; borrowed.
 * @return True when the node is a sourcecode element.
 */
static bool IsSourceNode(xmlNodePtr Node)
{
    return Node != nullptr && Node->type == XML_ELEMENT_NODE &&
           DocDocument::NodeNameEquals(Node, "sourcecode");
}

static bool ListNodeIsBlank(xmlNodePtr ListNode);
static bool NodeIsBlank(xmlNodePtr Node);

/**
 * @brief True when one item element of a list node holds content.
 *
 * An item holding <t> children holds content when any of them
 * does; an item holding bare runs holds content when any run
 * does. A blank <t> or whitespace-only text holds nothing.
 * @param Item The <li>, <dt>, or <dd> element; borrowed.
 * @return True when the item holds content.
 */
static bool ItemElementHasContent(xmlNodePtr Item)
{
    bool HasT = false;
    for (xmlNodePtr Child = Item->children; Child != nullptr;
         Child = Child->next) {
        if (Child->type == XML_ELEMENT_NODE &&
            DocDocument::NodeNameEquals(Child, "t")) {
            HasT = true;
            if (!NodeIsBlank(Child)) {
                return true;
            }
        }
    }
    if (HasT) {
        return false;
    }
    for (xmlNodePtr Child = Item->children; Child != nullptr;
         Child = Child->next) {
        if (Child->type == XML_ELEMENT_NODE ||
            Child->type == XML_COMMENT_NODE) {
            return true;
        }
        if ((Child->type == XML_TEXT_NODE ||
             Child->type == XML_CDATA_SECTION_NODE) &&
            !DocDocument::NodeText(Child).trimmed().isEmpty()) {
            return true;
        }
    }
    return false;
}

/**
 * @brief True when a list node holds no content: none of its
 *        items holds any.
 * @param ListNode The list node; borrowed.
 * @return True when the list is blank.
 */
static bool ListNodeIsBlank(xmlNodePtr ListNode)
{
    for (xmlNodePtr Child = ListNode->children; Child != nullptr;
         Child = Child->next) {
        if (Child->type == XML_ELEMENT_NODE &&
            (DocDocument::NodeNameEquals(Child, "li") ||
             DocDocument::NodeNameEquals(Child, "dt") ||
             DocDocument::NodeNameEquals(Child, "dd")) &&
            ItemElementHasContent(Child)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief True when a body node holds no text and no elements.
 *
 * A figure or a table is never blank. A list node is blank
 * only when none of its items holds content. A sourcecode
 * node is blank when it holds only whitespace text, except a
 * src-only element, which names a file and stays (Bug 469).
 * A <t> node is blank when it holds only whitespace text;
 * any element or comment child makes it non-blank. A comment
 * node is never blank.
 * @param Node The node; borrowed.
 * @return True when the node is blank.
 */
static bool NodeIsBlank(xmlNodePtr Node)
{
    if (Node == nullptr) {
        return true;
    }
    if (Node->type == XML_COMMENT_NODE) {
        return false;
    }
    if (Node->type != XML_ELEMENT_NODE) {
        return DocDocument::NodeText(Node).trimmed().isEmpty();
    }
    if (DocDocument::NodeNameEquals(Node, "figure") ||
        DocDocument::NodeNameEquals(Node, "artset") ||
        DocDocument::NodeNameEquals(Node, "artwork") ||
        DocDocument::NodeNameEquals(Node, "table")) {
        return false;
    }
    if (IsListNode(Node)) {
        return ListNodeIsBlank(Node);
    }
    if (DocDocument::NodeNameEquals(Node, "sourcecode")) {
        if (DocDocument::HasNodeAttribute(Node, "src")) {
            return false;
        }
        for (xmlNodePtr Child = Node->children; Child != nullptr;
             Child = Child->next) {
            if (Child->type == XML_ELEMENT_NODE) {
                return false;
            }
            if ((Child->type == XML_TEXT_NODE ||
                 Child->type == XML_CDATA_SECTION_NODE) &&
                !DocDocument::NodeText(Child).trimmed().isEmpty()) {
                return false;
            }
        }
        return true;
    }
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if (Child->type == XML_ELEMENT_NODE ||
            Child->type == XML_COMMENT_NODE) {
            return false;
        }
        if ((Child->type == XML_TEXT_NODE ||
             Child->type == XML_CDATA_SECTION_NODE) &&
            !DocDocument::NodeText(Child).trimmed().isEmpty()) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Builds node children from events, consuming through the
 *        matching close.
 *
 * Markers become element nodes: a Break keeps its stored
 * attributes, a Vertical Space takes its blank-line count from
 * the digits typed in its marker text, and an Index Entry takes
 * its item from its stored attributes (Bug 503: the marker
 * shows only a glyph, so there is no item text to read; the
 * item is edited via Edit index entry, not in place).
 * Characters typed into a Break or Vertical Space marker
 * come out as plain text after the marker, so nothing typed is
 * lost. Adjacent text joins into one text node.
 * @param Events The events.
 * @param Pos The next event to read; advanced past the events used.
 * @param Parent The node gaining the children.
 * @param DisplayNames The anchor-to-display-name map, so a
 *        mapped bracket strips its shown text (Bug 405).
 */
static void BuildNodeRuns(const QVector<DocEvent>& Events, int& Pos,
                          xmlNodePtr Parent,
                          const QVariantMap& DisplayNames)
{
    while (Pos < Events.size()) {
        const DocEvent& Event = Events[Pos];
        if (Event.EventKind == DocEvent::Kind::CloseTag) {
            ++Pos;
            return;
        }
        if (Event.EventKind == DocEvent::Kind::OpenTag) {
            ++Pos;
            QByteArray Name = Event.Tag.Name.toUtf8();
            xmlNodePtr Child = NewNode(Name.constData());
            SetNodeAttributes(Child, Event.Tag.Attributes);
            xmlAddChild(Parent, Child);
            BuildNodeRuns(Events, Pos, Child, DisplayNames);
            continue;
        }
        if (Event.EventKind == DocEvent::Kind::TextRun) {
            ++Pos;
            AddTextChild(Parent, Event.Text);
            continue;
        }
        ++Pos;
        if (Event.Tag.Name == QLatin1String("comment")) {
            // The marker text is the literal delimiters plus
            // the content; the delimiters come off, and what is
            // left is the comment, exactly as typed (Bug 441).
            QString Content = Event.Text;
            if (Content.startsWith(QStringLiteral("<!--"))) {
                Content.remove(0, 4);
            }
            if (Content.endsWith(QStringLiteral("-->"))) {
                Content.chop(3);
            }
            xmlAddChild(Parent, NewCommentNode(Content));
            continue;
        }
        QByteArray Name = Event.Tag.Name.toUtf8();
        xmlNodePtr Run = NewNode(Name.constData());
        SetNodeAttributes(Run, Event.Tag.Attributes);
        QString Stray;
        if (Event.Tag.Name == QLatin1String("br")) {
            for (QChar Ch : Event.Text) {
                if (Ch != BreakGlyph) {
                    Stray += Ch;
                }
            }
        } else if (Event.Tag.Name == QLatin1String("vspace")) {
            QString Digits;
            for (QChar Ch : Event.Text) {
                if (Ch == VspaceGlyph) {
                    // The glyph itself.
                } else if (Ch.isDigit()) {
                    Digits += Ch;
                } else {
                    Stray += Ch;
                }
            }
            if (!Digits.isEmpty()) {
                DocDocument::SetNodeAttribute(
                    Run, "blankLines", QString::number(Digits.toInt()));
            }
        } else if (Event.Tag.Name == QLatin1String("iref")) {
            // Bug 503: the marker is a glyph (the circled i), so
            // the item comes from the stored attributes, not the
            // marker text. This keeps the save right whatever
            // glyph the marker uses.
            DocDocument::SetNodeAttribute(
                Run, "item",
                AttributeOf(Event.Tag.Attributes, QStringLiteral("item")));
        } else if (Event.Tag.Name == QLatin1String("xref")) {
            // The marker shows its rendered form (the bracket,
            // the section, or the counter); that text is the
            // marker's own display, not text the author typed.
            // It comes off the front or the back, and anything
            // left is text typed into the marker, kept as
            // ordinary text. When the display itself was edited
            // so neither end matches, the non-glyph text is
            // kept, as for other markers.
            QString Shown = MarkerText(Event.Tag, DisplayNames);
            QString Kept = Event.Text;
            if (!Shown.isEmpty() && Kept.startsWith(Shown)) {
                Kept.remove(0, Shown.length());
            } else if (!Shown.isEmpty() && Kept.endsWith(Shown)) {
                Kept.chop(Shown.length());
            }
            for (QChar Ch : Kept) {
                if (Ch != EmptyItemGlyph) {
                    Stray += Ch;
                }
            }
        } else {
            // Any other marker shows the placeholder glyph; text
            // typed into it is kept as ordinary text (Bug 440).
            for (QChar Ch : Event.Text) {
                if (Ch != EmptyItemGlyph) {
                    Stray += Ch;
                }
            }
        }
        xmlAddChild(Parent, Run);
        if (!Stray.isEmpty()) {
            AddTextChild(Parent, Stray);
        }
    }
}

// SrcPlaceholderText is defined with the placeholder code
// below; it is declared here for the read-back below.
static QString SrcPlaceholderText(const QString& Src);

/**
 * @brief Reads one block as a paragraph, from its fragments.
 *
 * Fragments carry the tag stacks, so the events open and close
 * tags where the stacks change. Soft line breaks inside a block
 * read back as newlines (Bug 461), so the lines typed in a
 * paragraph show again on load; a forced break still comes
 * from the Break menu item.
 * @param Block The block.
 * @return The paragraph.
 */
/**
 * @brief Reads one block as a <t> node, from its fragments.
 *
 * Fragments carry the tag stacks, so the events open and close
 * tags where the stacks change. Soft line breaks inside a block
 * read back as newlines (Bug 461), so the lines typed in a
 * paragraph show again on load; a forced break still comes
 * from the Break menu item.
 * @param Block The block.
 * @return The <t> node; the caller owns it.
 */
static xmlNodePtr NodeFromBlock(const QTextBlock& Block)
{
    QVector<DocEvent> Events;
    DocTagStack Current;
    for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
        QTextFragment Fragment = It.fragment();
        QTextCharFormat Format = Fragment.charFormat();
        DocTagStack Stack = StackOf(Format);
        QString Text = Fragment.text();
        Text.replace(SoftBreakChar, QLatin1Char('\n'));
        QVariant MarkerValue = Format.property(DocMarkerProperty);
        int Common = 0;
        while (Common < Current.size() && Common < Stack.size() &&
               Current[Common] == Stack[Common]) {
            ++Common;
        }
        for (int i = Current.size(); i > Common; --i) {
            DocEvent Event;
            Event.EventKind = DocEvent::Kind::CloseTag;
            Events.append(Event);
        }
        for (int i = Common; i < Stack.size(); ++i) {
            DocEvent Event;
            Event.EventKind = DocEvent::Kind::OpenTag;
            Event.Tag = Stack[i];
            Events.append(Event);
        }
        Current = Stack;
        if (MarkerValue.isValid()) {
            DocEvent Event;
            Event.EventKind = DocEvent::Kind::MarkerRun;
            Event.Tag = EntryFromVariant(MarkerValue);
            Event.Text = Text;
            Events.append(Event);
        } else if (Format.property(DocOptionMarkerProperty).isValid()) {
            // An option marker is display only: it converts to
            // no run. Only the gear character itself is
            // dropped, so text typed beside it that took its
            // format still reads back.
            QString Kept = Text;
            Kept.remove(OptionGlyph);
            if (!Kept.isEmpty()) {
                DocEvent Event;
                Event.EventKind = DocEvent::Kind::TextRun;
                Event.Text = Kept;
                Events.append(Event);
            }
        } else if (Format.property(DocSrcPlaceholderProperty).isValid()) {
            // A src placeholder is display only: it converts
            // to no run. Only the placeholder text itself is
            // dropped, so text typed beside it that took its
            // format still reads back.
            QString Kept = Text;
            Kept.remove(SrcPlaceholderText(
                Format.property(DocSrcPlaceholderProperty).toString()));
            if (!Kept.isEmpty()) {
                DocEvent Event;
                Event.EventKind = DocEvent::Kind::TextRun;
                Event.Text = Kept;
                Events.append(Event);
            }
        } else if (!Text.isEmpty()) {
            DocEvent Event;
            Event.EventKind = DocEvent::Kind::TextRun;
            Event.Text = Text;
            Events.append(Event);
        }
    }
    for (int i = Current.size(); i > 0; --i) {
        DocEvent Event;
        Event.EventKind = DocEvent::Kind::CloseTag;
        Events.append(Event);
    }
    xmlNodePtr Paragraph = NewNode("t");
    int Pos = 0;
    // The display names ride on the block's own text document,
    // so a pasted block reads back with the same mappings the
    // body was loaded with (Bug 405).
    BuildNodeRuns(Events, Pos, Paragraph,
                  DisplayNamesForDocument(Block.document()));
    return Paragraph;
}

/**
 * @brief Returns the list style for a list's style attribute.
 *
 * Numbers show as numbers, letters as lowercase letters, and
 * everything else (symbols, hanging, empty, unknown) as bullets.
 * The stored style attribute is what is written back, so the
 * display choice never changes the file.
 * @param Style The style attribute value.
 * @return The list format.
 */
static QTextListFormat ListFormatForStyle(const QString& Style)
{
    QTextListFormat Format;
    if (Style == QLatin1String("numbers")) {
        Format.setStyle(QTextListFormat::ListDecimal);
    } else if (Style == QLatin1String("letters")) {
        Format.setStyle(QTextListFormat::ListLowerAlpha);
    } else {
        Format.setStyle(QTextListFormat::ListDisc);
    }
    return Format;
}

/**
 * @brief Returns the list format for a list element's kind.
 *
 * A numbered list shows numbers and a bulleted list shows
 * bullets; an inline list keeps its style attribute's format. A
 * definition list has no list format: its blocks carry no list,
 * only the list properties.
 * @param Name The list element name.
 * @param Attributes The list element's attributes.
 * @return The list format.
 */
static QTextListFormat ListFormatForKind(
    const QString& Name, const QVector<QPair<QString, QString>>& Attributes)
{
    if (Name == QLatin1String("list")) {
        return ListFormatForStyle(
            AttributeOf(Attributes, QStringLiteral("style")));
    }
    QTextListFormat Format;
    if (Name == QLatin1String("ol")) {
        Format.setStyle(QTextListFormat::ListDecimal);
        // Bug 464 v5: an <ol> starts at its start attribute
        // (default 1). Without this the display always began
        // at 1 even when the file said start='3'.
        bool Ok = false;
        int Start =
            AttributeOf(Attributes, QStringLiteral("start")).toInt(&Ok);
        Format.setStart(Ok ? Start : 1);
    } else {
        Format.setStyle(QTextListFormat::ListDisc);
    }
    return Format;
}

/**
 * @brief Returns a group number no list in the document uses yet.
 * @param Document The document.
 * @return One past the highest group number found, or 1.
 */
static int NextListGroup(QTextDocument* Document)
{
    int Highest = 0;
    for (QTextBlock Block = Document->begin(); Block.isValid();
         Block = Block.next()) {
        int Group = Block.blockFormat().property(DocListGroupProperty).toInt();
        if (Group > Highest) {
            Highest = Group;
        }
    }
    return Highest + 1;
}

/**
 * @brief Bolds every fragment of a block without touching tag stacks.
 *
 * Definition terms show bold as a visual overlay only: the
 * characters keep their stacks, so the bold never reaches the
 * file. Fragments are snapshotted first, since changing a
 * format splits fragments and would disturb a live walk.
 * @param Document The document.
 * @param Block The block to bold.
 */
static void BoldBlockFragments(QTextDocument* Document, const QTextBlock& Block)
{
    struct FragmentBold
    {
        int From;
        int To;
        QTextCharFormat Format;
    };
    QVector<FragmentBold> Bolds;
    for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
        QTextFragment Fragment = It.fragment();
        QTextCharFormat Format = Fragment.charFormat();
        Format.setFontWeight(QFont::Bold);
        FragmentBold Bold;
        Bold.From = Fragment.position();
        Bold.To = Fragment.position() + Fragment.length();
        Bold.Format = Format;
        Bolds.append(Bold);
    }
    for (const FragmentBold& Bold : Bolds) {
        QTextCursor Cursor(Document);
        Cursor.setPosition(Bold.From);
        Cursor.setPosition(Bold.To, QTextCursor::KeepAnchor);
        Cursor.setCharFormat(Bold.Format);
    }
}

/**
 * @brief Rebuilds a block's visuals from its tag stacks.
 *
 * Used when a definition list is removed: the bold overlay on
 * the term blocks is dropped, and each fragment shows only what
 * its stack says. Markers keep their marker visuals.
 * @param Document The document.
 * @param Block The block to refresh.
 */
static void RefreshBlockVisuals(QTextDocument* Document, const QTextBlock& Block)
{
    struct FragmentVisual
    {
        int From;
        int To;
        QTextCharFormat Format;
    };
    QVector<FragmentVisual> Visuals;
    bool InSource = IsSourceCodeBlock(Block);
    for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
        QTextFragment Fragment = It.fragment();
        QTextCharFormat Format = Fragment.charFormat();
        ApplyStackVisuals(Format, StackOf(Format));
        if (InSource) {
            Format.setFontFamilies(CodeFontFamilies());
        }
        QVariant MarkerValue = Format.property(DocMarkerProperty);
        if (MarkerValue.isValid()) {
            ApplyMarkerVisuals(Format, EntryFromVariant(MarkerValue));
        }
        FragmentVisual Visual;
        Visual.From = Fragment.position();
        Visual.To = Fragment.position() + Fragment.length();
        Visual.Format = Format;
        Visuals.append(Visual);
    }
    for (const FragmentVisual& Visual : Visuals) {
        QTextCursor Cursor(Document);
        Cursor.setPosition(Visual.From);
        Cursor.setPosition(Visual.To, QTextCursor::KeepAnchor);
        Cursor.setCharFormat(Visual.Format);
    }
}

/**
 * @brief Returns the list role of a block: term, definition,
 *        item, or continuation.
 *
 * A block carrying a kind but no role is a head block: a term
 * in a definition list, an item in a bulleted or numbered list.
 * A block of the inline list kind has no role: the empty string.
 * @param Block The block.
 * @return The role.
 */
static QString ListRoleOf(const QTextBlock& Block)
{
    QString Role = Block.blockFormat().property(DocListRoleProperty).toString();
    if (!Role.isEmpty()) {
        return Role;
    }
    QString Kind = Block.blockFormat().property(DocListKindProperty).toString();
    if (Kind == QLatin1String("dl")) {
        return QStringLiteral("term");
    }
    if (Kind == QLatin1String("ul") || Kind == QLatin1String("ol")) {
        return QStringLiteral("item");
    }
    return QString();
}

/**
 * @brief Offsets in a term block's text where a new role starts.
 *
 * A term and its definition can end up in one block (Bug 478):
 * a soft break (U+2028), a <br> marker glyph (U+2028's pasted
 * form, U+23CE), or a merge with no separator at all. A merge
 * leaves no character behind, but the term stays bold and the
 * definition starts in normal weight, so the first bold to
 * normal fragment change marks the split. Only a term block
 * is split this way: a definition's own bold spans are not
 * boundaries. A merge can also leave a single space between
 * the term and its definition (Bug.xml: "Care factor An
 * integer from 0 to 100, ..."), with no break character and
 * the whole block restyled bold, so neither a break nor a
 * bold change marks it. For that case the block's text is
 * matched against the document's other term texts: the
 * longest term that is a prefix of this block, with text
 * after it, marks the split. The text after the term must
 * run over 20 characters: a shorter tail is the rest of a
 * longer term (a "Care" term must not split "Care factor"),
 * not a definition. Offsets are sorted, unique, and
 * within the text.
 * @param Block A term block.
 * @param Document The document, for the known-term match.
 * @return Split offsets, empty when the block holds one role.
 */
static QVector<int> TermSplitOffsets(const QTextBlock& Block,
                                     QTextDocument* Document)
{
    QVector<int> Offsets;
    QString Text = Block.text();
    for (int i = 0; i < Text.size(); ++i) {
        if (Text[i] == SoftBreakChar || Text[i] == BreakGlyph) {
            Offsets.append(i);
        }
    }
    int Pos = 0;
    bool PrevBold = false;
    bool First = true;
    for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
        QTextFragment Fragment = It.fragment();
        bool Bold = Fragment.charFormat().fontWeight() == QFont::Bold;
        if (!First && PrevBold && !Bold && Pos > 0 && Pos < Text.size()) {
            Offsets.append(Pos);
        }
        PrevBold = Bold;
        First = false;
        Pos += Fragment.length();
    }
    // A space-joined merge: the longest other term text in the
    // document that is a prefix of this block, with text after
    // it, marks where the term ends and the definition starts.
    if (Document != nullptr) {
        int Best = 0;
        for (QTextBlock Other = Document->begin(); Other.isValid();
             Other = Other.next()) {
            if (Other == Block) {
                continue;
            }
            if (ListRoleOf(Other) != QLatin1String("term")) {
                continue;
            }
            QString Term = Other.text();
            if (Term.isEmpty() || Term.size() >= Text.size()) {
                continue;
            }
            if (Term.contains(SoftBreakChar) ||
                Term.contains(BreakGlyph)) {
                continue;
            }
            if (Text.startsWith(Term) && Term.size() > Best &&
                Text.size() - Term.size() > 20) {
                Best = Term.size();
            }
        }
        if (Best > 0) {
            Offsets.append(Best);
        }
    }
    // Fallback for a space-joined merge with no bold change and
    // no matching term in the document (Bug 478): the term is
    // the longest short prefix (one to three words) whose
    // remainder is long and starts like a definition. A single
    // word term may lead a lowercase definition; a longer term
    // must lead an uppercase one, so "Care" does not win over
    // "Care factor" when the definition starts with "An".
    if (Offsets.isEmpty() && Text.size() > 30) {
        int Best = 0;
        int Words = 0;
        for (int i = 0; i < Text.size(); ++i) {
            if (Text[i] != QChar(' ')) {
                continue;
            }
            ++Words;
            if (Words > 3) {
                break;
            }
            QString TermPart = Text.left(i);
            QString Rest = Text.mid(i + 1);
            if (TermPart.size() > 30 || Rest.size() <= 20) {
                continue;
            }
            bool RestUpper = !Rest.isEmpty() && Rest[0].isUpper();
            bool SingleWord = Words == 1;
            if (RestUpper || SingleWord) {
                Best = i;
            }
        }
        if (Best > 0) {
            Offsets.append(Best);
        }
    }
    // A split needs real text on both sides: an offset at the
    // start of the block, or with only whitespace after it, is
    // a pretty-printing newline or trailing space, not a
    // term/definition boundary (Bug 478). The top
    // "Application identity" term carries a leading newline
    // from the file's pretty-printing, which the load turns
    // into a soft break at offset 0; splitting there empties
    // the term entirely, and the paste drops it. Genuine
    // splits keep non-blank text on both sides, so the filter
    // keeps them: a merged "Care factor An integer..."
    // still splits at its term, and a bold-to-normal boundary
    // still splits where the definition starts.
    QVector<int> Kept;
    for (int Offset : Offsets) {
        if (Offset <= 0 || Offset >= Text.size()) {
            continue;
        }
        if (Text.left(Offset).trimmed().isEmpty()) {
            continue;
        }
        if (Text.mid(Offset).trimmed().isEmpty()) {
            continue;
        }
        Kept.append(Offset);
    }
    Offsets = Kept;
    std::sort(Offsets.begin(), Offsets.end());
    Offsets.erase(std::unique(Offsets.begin(), Offsets.end()),
                  Offsets.end());
    return Offsets;
}

/**
 * @brief True when a role continues the previous item instead of
 *        starting a new one: a definition paragraph, or a
 *        further paragraph of a bulleted or numbered item.
 * @param Role The role.
 * @return True for a continuation role.
 */
static bool IsContinuationRole(const QString& Role)
{
    return Role == QLatin1String("definition") ||
           Role == QLatin1String("continuation");
}

/**
 * @brief True when a block belongs to the list being walked.
 *
 * A block in the list's own list object belongs. A block
 * outside it belongs when it carries the list's kind, group,
 * and attributes: a continuation of an item, or a block that
 * left the list but kept its properties. A block in another
 * list does not belong. A definition list has no list object,
 * so its blocks belong by kind, group, and attributes alone.
 * @param Block The block to test.
 * @param List The list object being walked, or null for a definition list.
 * @param KindValue The list's kind property.
 * @param GroupValue The list's group property.
 * @param AttrsValue The list's attributes property.
 * @param InList True for a bulleted or numbered list.
 * @return True when the block is part of this list.
 */
static bool ListBlockBelongs(const QTextBlock& Block, QTextList* List,
                             const QVariant& KindValue, const QVariant& GroupValue,
                             const QVariant& AttrsValue, bool InList)
{
    if (InList) {
        if (Block.textList() == List) {
            return true;
        }
        if (Block.textList() != nullptr) {
            return false;
        }
    }
    QTextBlockFormat Format = Block.blockFormat();
    return Format.property(DocListKindProperty) == KindValue &&
           Format.property(DocListGroupProperty) == GroupValue &&
           Format.property(DocListProperty) == AttrsValue;
}

/**
 * @brief Marks a block as part of a list, with its role.
 *
 * An empty role clears the role property: the inline list kind
 * has no roles.
 * @param Cursor A cursor in the block.
 * @param Kind The list element name.
 * @param Group The list's group number.
 * @param Attributes The list element's attributes.
 * @param Role The block's role, or empty for none.
 */
static void SetListProperties(QTextCursor& Cursor, const QString& Kind,
                              int Group,
                              const QVector<QPair<QString, QString>>& Attributes,
                              const QString& Role)
{
    QTextBlockFormat Format = Cursor.blockFormat();
    Format.setProperty(DocListProperty, AttributesToVariant(Attributes));
    Format.setProperty(DocListKindProperty, Kind);
    Format.setProperty(DocListGroupProperty, Group);
    if (Role.isEmpty()) {
        Format.clearProperty(DocListRoleProperty);
    } else {
        Format.setProperty(DocListRoleProperty, Role);
    }
    // List blocks sit tight: no paragraph spacing.
    Format.setBottomMargin(0);
    Cursor.setBlockFormat(Format);
}

/**
 * @brief Marks the cursor's block as one line of a sourcecode
 *        element, indented like rendered code.
 * @param Cursor The cursor in the block.
 * @param Attributes The sourcecode element's attributes.
 */
static void SetSourceProperties(
    QTextCursor& Cursor,
    const QVector<QPair<QString, QString>>& Attributes)
{
    QTextBlockFormat Format = Cursor.blockFormat();
    Format.setProperty(DocSourceProperty, AttributesToVariant(Attributes));
    Format.setProperty(DocSourceKindProperty, QStringLiteral("sourcecode"));
    Format.setIndent(1);
    // Code lines sit tight: no paragraph spacing.
    Format.setBottomMargin(0);
    Cursor.setBlockFormat(Format);
}

/**
 * @brief Marks the cursor's block as part of a figure region.
 * @param Cursor The cursor in the block.
 * @param Kind The wrapper kind: figure, artset, or artwork.
 * @param Group The figure's group number.
 * @param Role The block's role: caption or artwork.
 * @param Payload The figure payload for the region.
 */
static void SetFigureProperties(QTextCursor& Cursor, const QString& Kind,
                                int Group, const QString& Role,
                                const QVariant& Payload)
{
    QTextBlockFormat Format = Cursor.blockFormat();
    Format.setProperty(DocFigureKindProperty, Kind);
    Format.setProperty(DocFigureGroupProperty, Group);
    Format.setProperty(DocFigureRoleProperty, Role);
    Format.setProperty(DocFigurePayloadProperty, Payload);
    Format.setIndent(Role == QLatin1String("artwork") ? 1 : 0);
    // Figure blocks sit tight: no paragraph spacing.
    Format.setBottomMargin(0);
    Cursor.setBlockFormat(Format);
}

/**
 * @brief Returns the next free figure group number.
 * @param Document The document.
 * @return One above the highest group number in use.
 */
static int NextFigureGroup(QTextDocument* Document)
{
    int Highest = 0;
    for (QTextBlock Block = Document->begin(); Block.isValid();
         Block = Block.next()) {
        int Group = Block.blockFormat()
                        .property(DocFigureGroupProperty)
                        .toInt();
        if (Group > Highest) {
            Highest = Group;
        }
    }
    return Highest + 1;
}

/**
 * @brief Reads a figure block's payload.
 * @param Block The block.
 * @param Figure Receives the figure.
 * @param Visible Receives the visible version index.
 * @return False when the block carries no figure payload.
 */
static bool FigurePayloadOf(const QTextBlock& Block, xmlNodePtr& FigureNode,
                            int& Visible)
{
    return FigureFromVariant(
        Block.blockFormat().property(DocFigurePayloadProperty), FigureNode,
        Visible);
}

/**
 * @brief Returns the Qt alignment for a table align value
 *        (Bug 448).
 *
 * The schema values are left, center, and right; anything else
 * reads as left, which is the schema default for a cell.
 * @param Align The align value, empty for none.
 * @return The alignment.
 */
static Qt::Alignment AlignmentForAlignValue(const QString& Align)
{
    if (Align == QLatin1String("center")) {
        return Qt::AlignCenter;
    }
    if (Align == QLatin1String("right")) {
        return Qt::AlignRight;
    }
    return Qt::AlignLeft;
}

/**
 * @brief Marks the cursor's block as a table block with a role.
 * @param Cursor The cursor, in the block to mark.
 * @param Group The table's group number.
 * @param Role The role: "caption" or "cell".
 * @param Payload The table payload variant.
 */
static void SetTableProperties(QTextCursor& Cursor, int Group,
                               const QString& Role, const QVariant& Payload)
{
    QTextBlockFormat Format = Cursor.blockFormat();
    Format.setProperty(DocTableKindProperty, QStringLiteral("table"));
    Format.setProperty(DocTableGroupProperty, Group);
    Format.setProperty(DocTableRoleProperty, Role);
    Format.setProperty(DocTablePayloadProperty, Payload);
    // Table blocks sit tight: no paragraph spacing.
    Format.setBottomMargin(0);
    Cursor.setBlockFormat(Format);
}

/**
 * @brief Marks the cursor's block as the first block of a cell,
 *        with the cell's own data.
 * @param Cursor The cursor, in the cell's first block.
 * @param Group The table's group number.
 * @param Payload The table payload variant.
 * @param Cell The cell: its header flag and kept attributes.
 */
static void SetTableCellProperties(QTextCursor& Cursor, int Group,
                                   const QVariant& Payload,
                                   xmlNodePtr CellNode)
{
    SetTableProperties(Cursor, Group, QStringLiteral("cell"), Payload);
    QTextBlockFormat Format = Cursor.blockFormat();
    Format.setProperty(DocTableCellProperty, TableCellToVariant(CellNode));
    // Bug 448: a cell's align shows at once, so the alignment
    // is visible in the editor as well as in the XML.
    QString Align;
    for (const QPair<QString, QString>& Attribute :
         NodeAttributes(CellNode)) {
        if (Attribute.first == QLatin1String("align")) {
            Align = Attribute.second;
            break;
        }
    }
    Format.setAlignment(AlignmentForAlignValue(Align));
    Cursor.setBlockFormat(Format);
}

/**
 * @brief Returns the next free table group number.
 * @param Document The document.
 * @return One above the highest group number in use.
 */
static int NextTableGroup(QTextDocument* Document)
{
    int Highest = 0;
    for (QTextBlock Block = Document->begin(); Block.isValid();
         Block = Block.next()) {
        int Group = Block.blockFormat()
                        .property(DocTableGroupProperty)
                        .toInt();
        if (Group > Highest) {
            Highest = Group;
        }
    }
    return Highest + 1;
}

/**
 * @brief Whether an artwork version is included from a file.
 * @param Version The version.
 * @return True when the version carries a src attribute: the
 *         artwork lives in a file outside the document, so a
 *         copy survives removing it. A version without one is
 *         kept only in the document.
 */
static bool ArtworkVersionIsFromFile(xmlNodePtr Artwork)
{
    return DocDocument::HasNodeAttribute(Artwork, "src");
}

/**
 * @brief Returns the payload variant for a figure block,
 *        synthesizing a one-version figure when the block
 *        carries no readable payload.
 * @param Block The block.
 * @return The payload variant.
 */
static QVariant FigurePayloadOrDefault(const QTextBlock& Block)
{
    xmlNodePtr FigureNode = nullptr;
    int Visible = 0;
    if (FigurePayloadOf(Block, FigureNode, Visible)) {
        QVariant Payload = FigureToVariant(FigureNode, Visible);
        xmlFreeNode(FigureNode);
        return Payload;
    }
    QString KindName = FigureKindOf(Block);
    xmlNodePtr Artwork = NewNode("artwork");
    DocDocument::SetNodeAttribute(Artwork, "type",
                                  QStringLiteral("ascii-art"));
    if (KindName == QLatin1String("artset")) {
        FigureNode = NewNode("artset");
        xmlAddChild(FigureNode, Artwork);
    } else if (KindName == QLatin1String("artwork")) {
        FigureNode = Artwork;
    } else {
        FigureNode = NewNode("figure");
        xmlAddChild(FigureNode, Artwork);
    }
    QVariant Payload = FigureToVariant(FigureNode, 0);
    xmlFreeNode(FigureNode);
    return Payload;
}

/**
 * @brief True when a block is the placeholder line of an SVG
 *        version: an artwork block whose visible version is
 *        preserved SVG.
 * @param Block The block.
 * @return True when the block is a placeholder.
 */
static bool IsPlaceholderBlock(const QTextBlock& Block)
{
    if (!IsFigureArtworkBlock(Block)) {
        return false;
    }
    xmlNodePtr FigureNode = nullptr;
    int Visible = 0;
    if (!FigurePayloadOf(Block, FigureNode, Visible)) {
        return false;
    }
    QVector<xmlNodePtr> Versions = FigureVersionNodes(FigureNode);
    bool IsSvg = Visible >= 0 && Visible < Versions.size() &&
                 ArtworkNodeIsSvg(Versions[Visible]);
    xmlFreeNode(FigureNode);
    return IsSvg;
}

/**
 * @brief Returns every block of one figure region: the
 *        consecutive blocks sharing a kind and a group.
 * @param AnyBlock Any block of the region.
 * @return The region's blocks, in order, or empty when the
 *         block is no figure block.
 */
static QVector<QTextBlock> FigureRegionBlocks(const QTextBlock& AnyBlock)
{
    QVector<QTextBlock> Blocks;
    if (!IsFigureBlock(AnyBlock)) {
        return Blocks;
    }
    QString Kind = FigureKindOf(AnyBlock);
    int Group = AnyBlock.blockFormat()
                    .property(DocFigureGroupProperty)
                    .toInt();
    QTextBlock First = AnyBlock;
    while (First.previous().isValid() &&
           FigureKindOf(First.previous()) == Kind &&
           First.previous().blockFormat()
                   .property(DocFigureGroupProperty)
                   .toInt() == Group) {
        First = First.previous();
    }
    for (QTextBlock Block = First;
         Block.isValid() && FigureKindOf(Block) == Kind &&
         Block.blockFormat().property(DocFigureGroupProperty).toInt() ==
             Group;
         Block = Block.next()) {
        Blocks.append(Block);
    }
    return Blocks;
}

/**
 * @brief True when the cursor's block, or any block its
 *        selection touches, is a figure block.
 * @param Document The document.
 * @param Cursor The cursor.
 * @return True when the cursor touches a figure.
 */
static bool TouchesFigure(QTextDocument* Document, const QTextCursor& Cursor)
{
    if (!Cursor.hasSelection()) {
        return IsFigureBlock(Cursor.block());
    }
    int Start = Cursor.selectionStart();
    int End = Cursor.selectionEnd();
    QTextBlock Block = Document->findBlock(Start);
    while (Block.isValid() && Block.position() < End) {
        if (IsFigureBlock(Block)) {
            return true;
        }
        Block = Block.next();
    }
    return false;
}

/**
 * @brief The space below a plain paragraph: one line.
 *
 * Doug's ruling (Bug 460): one line of paragraph spacing is
 * correct, and when more space is needed a Vertical-Space is
 * inserted. The value is the edit font's line spacing, taken
 * from the document's default font, which is the edit's font:
 * the application sets Arial 10 and nothing overrides it. This
 * is the single definition of the spacing; every site that
 * gives a block the paragraph spacing calls this, and every
 * site that marks a block structural sets the margin to 0.
 * Display only: the writer never reads block formats, so the
 * spacing never reaches the file.
 * @param Document The document.
 * @return One line of spacing, in pixels.
 */
static qreal ParagraphGap(QTextDocument* Document)
{
    return QFontMetrics(Document->defaultFont()).lineSpacing();
}

/**
 * @brief Takes a block out of any list it belongs to, clearing
 *        the list's visuals.
 *
 * List membership lives apart from the list properties: marking
 * a block with a new kind does not take it out of its old list,
 * so a block marked as a definition but left in a numbered list
 * would show a number and read back as nothing. Detaching first
 * keeps that hybrid state from ever forming. The sourcecode,
 * figure, and table properties are cleared too, so a block
 * re-marked with a new kind cannot keep stale marking from the
 * old one.
 * @param Document The document.
 * @param Block The block.
 */
static void DetachBlockFromList(QTextDocument* Document,
                                const QTextBlock& Block)
{
    if (QTextList* Old = Block.textList()) {
        Old->remove(Block);
    }
    QTextCursor At(Document);
    At.setPosition(Block.position());
    QTextBlockFormat Format = At.blockFormat();
    Format.setIndent(0);
    Format.clearProperty(QTextFormat::BackgroundBrush);
    Format.clearProperty(DocListProperty);
    Format.clearProperty(DocListKindProperty);
    Format.clearProperty(DocListGroupProperty);
    Format.clearProperty(DocListRoleProperty);
    Format.clearProperty(DocSourceProperty);
    Format.clearProperty(DocSourceKindProperty);
    Format.clearProperty(DocFigureKindProperty);
    Format.clearProperty(DocFigureGroupProperty);
    Format.clearProperty(DocFigureRoleProperty);
    Format.clearProperty(DocFigurePayloadProperty);
    Format.clearProperty(DocTableKindProperty);
    Format.clearProperty(DocTableGroupProperty);
    Format.clearProperty(DocTableRoleProperty);
    Format.clearProperty(DocTablePayloadProperty);
    Format.clearProperty(DocTableCellProperty);
    // The block is plain again, so it takes the paragraph
    // spacing back. A caller that re-marks the block replaces
    // it with none.
    Format.setBottomMargin(ParagraphGap(Document));
    At.setBlockFormat(Format);
}

/**
 * @brief Background tint for definition-list term blocks.
 *
 * The link color at a low opacity: visible against the dark
 * theme, quiet enough to sit behind text. Display only; the
 * block background never reaches the file.
 */
const QBrush DefinitionTermTint(QColor(176, 196, 232, 32));

/**
 * @brief Applies the term and definition visuals to a whole
 *        definition list.
 *
 * Terms (the blocks marked term) show bold on a tinted
 * background; definitions (the blocks marked definition) are
 * indented one step with no tint, and their text shows only
 * what its tag stacks say. The styling follows the marking, so
 * restyling the whole list keeps every block right whatever
 * was inserted or moved. Display only: nothing here reaches
 * the file.
 * @param Document The document.
 * @param AnyBlock Any block of the list.
 */
static void StyleDefinitionGroup(QTextDocument* Document,
                                 const QTextBlock& AnyBlock)
{
    int Group = AnyBlock.blockFormat().property(DocListGroupProperty).toInt();
    QTextBlock First = AnyBlock;
    while (First.previous().isValid()) {
        QTextBlock Prev = First.previous();
        if (Prev.text().trimmed().isEmpty() &&
            !Prev.blockFormat().property(DocListKindProperty).isValid()) {
            // A blank line inside the list: the group continues
            // past it, so step over it instead of stopping.
            First = Prev;
            continue;
        }
        if (Prev.blockFormat().property(DocListKindProperty).toString() !=
                QLatin1String("dl") ||
            Prev.blockFormat().property(DocListGroupProperty).toInt() != Group) {
            break;
        }
        First = Prev;
    }
    QTextBlock Block = First;
    while (Block.isValid()) {
        if (Block.text().trimmed().isEmpty() &&
            !Block.blockFormat().property(DocListKindProperty).isValid()) {
            Block = Block.next();
            continue;
        }
        if (Block.blockFormat().property(DocListKindProperty).toString() !=
                QLatin1String("dl") ||
            Block.blockFormat().property(DocListGroupProperty).toInt() != Group) {
            break;
        }
        QTextCursor At(Document);
        At.setPosition(Block.position());
        QTextBlockFormat Format = At.blockFormat();
        if (ListRoleOf(Block) == QLatin1String("term")) {
            BoldBlockFragments(Document, Block);
            Format.setIndent(0);
            Format.setBackground(DefinitionTermTint);
        } else {
            RefreshBlockVisuals(Document, Block);
            Format.setIndent(1);
            Format.clearProperty(QTextFormat::BackgroundBrush);
        }
        // Definition blocks sit tight: no paragraph spacing.
        Format.setBottomMargin(0);
        At.setBlockFormat(Format);
        Block = Block.next();
    }
}

/**
 * @brief Inserts runs at the cursor, with the tag stacks they carry.
 *
 * A list run cannot appear here: the reader stands a list alone
 * as its paragraph. If one ever arrives, its items are flattened
 * to text, so no words are lost. An element run with no children
 * becomes a marker, or it would insert no characters and vanish
 * on read-back (Bug 440). A comment run shows as its literal
 * delimiters plus its content, in italics (Bug 441).
 * @param Cursor The cursor to insert at.
 * @param Runs The runs to insert.
 * @param Stack The tag stack around the runs; updated as tags open.
 */
/**
 * @brief Inserts a node's children as runs under one open stack.
 *
 * Newlines show as soft breaks: a newline would split the block
 * when inserted (Bug 461). Comments write as markers, newlines
 * become spaces, as in an index item. An inline list inside a
 * paragraph puts each item's runs in, separated by a space.
 * @param Cursor The cursor.
 * @param Node The node whose children are inserted.
 * @param Stack The tag stack open above this level.
 */
static void InsertNodeChildren(QTextCursor& Cursor, xmlNodePtr Node,
                               DocTagStack& Stack)
{
    if (Node == nullptr) {
        return;
    }
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if (Child->type == XML_TEXT_NODE ||
            Child->type == XML_CDATA_SECTION_NODE) {
            QString Shown = DocDocument::NodeText(Child);
            Shown.replace(QStringLiteral("\r\n"), QLatin1String("\n"));
            Shown.replace(QLatin1Char('\r'), QLatin1Char('\n'));
            // Bug 523: a newline followed by indentation is the
            // file's line wrap (pretty-printing), not a line
            // break: collapse it and the whitespace around it
            // to a single space, so the text pane shows flowing
            // prose instead of the file's wrapped lines and
            // their leading spaces. A bare newline (no
            // indentation after it) is a Shift+Enter break the
            // user put there by hand, so it stays a line break.
            // Preformatted content (sourcecode, artwork) does
            // not reach this branch; it has its own inserter
            // that keeps line breaks.
            Shown.replace(
                QRegularExpression(QStringLiteral("[ \\t]*\\n[ \\t]+")),
                QStringLiteral(" "));
            Shown.replace(QLatin1Char('\n'), SoftBreakChar);
            Cursor.insertText(Shown, FormatForStack(Stack));
        } else if (Child->type == XML_COMMENT_NODE) {
            // Newlines become spaces, as in an index item: a
            // newline would split the block when inserted.
            QString Shown = DocDocument::NodeText(Child);
            Shown.replace(QLatin1Char('\n'), QLatin1Char(' '));
            Shown.replace(QLatin1Char('\r'), QLatin1Char(' '));
            DocTagEntry Marker;
            Marker.Name = QStringLiteral("comment");
            Cursor.insertText(QStringLiteral("<!--") + Shown + QStringLiteral("-->"),
                              FormatForMarker(Stack, Marker));
        } else if (Child->type != XML_ELEMENT_NODE) {
            continue;
        } else if (DocDocument::NodeNameEquals(Child, "br") ||
                   DocDocument::NodeNameEquals(Child, "vspace") ||
                   DocDocument::NodeNameEquals(Child, "iref") ||
                   (Child->children == nullptr && !IsListNode(Child))) {
            DocTagEntry Marker;
            Marker.Name = QString::fromUtf8(
                reinterpret_cast<const char*>(Child->name));
            Marker.Attributes = NodeAttributes(Child);
            Cursor.insertText(
                MarkerText(Marker,
                           DisplayNamesForDocument(Cursor.document())),
                FormatForMarker(Stack, Marker));
        } else if (IsListNode(Child)) {
            for (xmlNodePtr Item = Child->children; Item != nullptr;
                 Item = Item->next) {
                if (Item->type != XML_ELEMENT_NODE) {
                    continue;
                }
                bool HasT = false;
                for (xmlNodePtr Part = Item->children; Part != nullptr;
                     Part = Part->next) {
                    if (Part->type == XML_ELEMENT_NODE &&
                        DocDocument::NodeNameEquals(Part, "t")) {
                        HasT = true;
                        InsertNodeChildren(Cursor, Part, Stack);
                    }
                }
                if (!HasT) {
                    InsertNodeChildren(Cursor, Item, Stack);
                }
                Cursor.insertText(QStringLiteral(" "), FormatForStack(Stack));
            }
        } else {
            DocTagEntry Entry;
            Entry.Name = QString::fromUtf8(
                reinterpret_cast<const char*>(Child->name));
            Entry.Attributes = NodeAttributes(Child);
            Stack.append(Entry);
            InsertNodeChildren(Cursor, Child, Stack);
            Stack.removeLast();
        }
    }
}

/**
 * @brief Inserts a node's children as runs at the cursor.
 *
 * The node is a <t>, a <name>, a table cell, or a list item
 * element: its children are the runs. The tag stack starts
 * empty, as every old caller started it.
 * @param Cursor The cursor.
 * @param Node The node whose children are inserted; borrowed.
 */
static void InsertNodeRuns(QTextCursor& Cursor, xmlNodePtr Node)
{
    DocTagStack Stack;
    InsertNodeChildren(Cursor, Node, Stack);
}

/**
 * @brief Inserts a list paragraph at the cursor, in a fresh block.
 *
 * The items go in as plain blocks, each marked with the list's
 * kind, group, attributes, and role. A bulleted, numbered, or
 * inline list then gets one list created over its head blocks
 * only, so the bullets or numbers show and Enter adds an item;
 * a continuation block sits outside the list object, indented
 * to line up with the item text, or Qt would give it its own
 * bullet or number. A definition list gets no list: its blocks
 * are told apart by their properties alone, its term blocks
 * show bold on a tinted background, and its definition blocks
 * are indented.
 * @param Cursor The cursor, at the start of the fresh block.
 * @param ListRun The list run.
 */
/**
 * @brief The literal text of a block, as code reads it.
 *
 * Marker characters are left out — they are editing marks, not
 * text — and a soft line break counts as a line break, so a
 * paragraph turned into code keeps the lines it showed.
 * @param Block The block.
 * @return The text, with line breaks as newline characters.
 */
static QString CodeTextOf(const QTextBlock& Block)
{
    QString Text;
    for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
        QTextFragment Fragment = It.fragment();
        QTextCharFormat Format = Fragment.charFormat();
        if (Format.property(DocMarkerProperty).isValid()) {
            continue;
        }
        QString Piece = Fragment.text();
        if (Format.property(DocOptionMarkerProperty).isValid()) {
            // An option marker is display only: only the gear
            // character itself is dropped, so text typed beside
            // it that took its format still reads back.
            Piece.remove(OptionGlyph);
            if (Piece.isEmpty()) {
                continue;
            }
        }
        if (Format.property(DocSrcPlaceholderProperty).isValid()) {
            // A src placeholder is display only: only the
            // placeholder text itself is dropped, so text typed
            // beside it that took its format still reads back.
            Piece.remove(SrcPlaceholderText(
                Format.property(DocSrcPlaceholderProperty).toString()));
            if (Piece.isEmpty()) {
                continue;
            }
        }
        Piece.replace(SoftBreakChar, QLatin1Char('\n'));
        Text += Piece;
    }
    return Text;
}

/**
 * @brief The code text of only the selected part of a block.
 *
 * Like CodeTextOf, but only the part of the block inside the
 * selection [Start, End): a block the selection merely touches
 * at its edge reads as empty when the touched part holds no
 * real text. That is what the copy carry needs: a selection
 * starting at the end of a paragraph selects nothing from it,
 * so the paragraph must not block the carry (Bug 469).
 * @param Block The block.
 * @param Start The selection start.
 * @param End The selection end.
 * @return The selected part's code text, display-only marks stripped.
 */
static QString SelectedCodeTextOf(const QTextBlock& Block, int Start, int End)
{
    QString Text;
    int BlockPos = Block.position();
    int BlockEnd = BlockPos + Block.text().length();
    int From = qMax(Start, BlockPos);
    int To = qMin(End, BlockEnd);
    if (From >= To) {
        return Text;
    }
    for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
        QTextFragment Fragment = It.fragment();
        QTextCharFormat Format = Fragment.charFormat();
        if (Format.property(DocMarkerProperty).isValid()) {
            continue;
        }
        int FragPos = Fragment.position();
        int FragEnd = FragPos + Fragment.length();
        int ClipFrom = qMax(From, FragPos);
        int ClipTo = qMin(To, FragEnd);
        if (ClipFrom >= ClipTo) {
            continue;
        }
        QString Piece = Fragment.text().mid(ClipFrom - FragPos,
                                            ClipTo - ClipFrom);
        if (Format.property(DocOptionMarkerProperty).isValid()) {
            // An option marker is display only: only the gear
            // character itself is dropped, so text typed beside
            // it that took its format still reads back.
            Piece.remove(OptionGlyph);
            if (Piece.isEmpty()) {
                continue;
            }
        }
        if (Format.property(DocSrcPlaceholderProperty).isValid()) {
            // A src placeholder is display only: only the
            // placeholder text itself is dropped, so text typed
            // beside it that took its format still reads back.
            Piece.remove(SrcPlaceholderText(
                Format.property(DocSrcPlaceholderProperty).toString()));
            if (Piece.isEmpty()) {
                continue;
            }
        }
        Piece.replace(SoftBreakChar, QLatin1Char('\n'));
        Text += Piece;
    }
    return Text;
}

/**
 * @brief Shows one sourcecode run as its lines, each a block.
 *
 * The cursor sits in the block the lines start in. Every line
 * carries the sourcecode attributes, so the lines read back as
 * one element.
 * @param Cursor The cursor, in the first line's block.
 * @param SourceRun The sourcecode run.
 */
/**
 * @brief Shows one sourcecode node as its lines, each a block.
 *
 * The cursor sits in the block the lines start in. Every line
 * carries the sourcecode attributes, so the lines read back as
 * one element.
 * @param Cursor The cursor, in the first line's block.
 * @param SourceNode The <sourcecode> node; borrowed.
 */
static void InsertSourceNode(QTextCursor& Cursor, xmlNodePtr SourceNode)
{
    QString Text = DocDocument::NodeText(SourceNode);
    QStringList Lines = Text.split(QLatin1Char('\n'));
    bool FirstLine = true;
    for (const QString& Line : Lines) {
        if (!FirstLine) {
            Cursor.insertBlock();
        }
        FirstLine = false;
        SetSourceProperties(Cursor, NodeAttributes(SourceNode));
        Cursor.setCharFormat(CodeCharFormat());
        Cursor.insertText(Line);
    }
}

/**
 * @brief Fills the cursor's blocks with one figure paragraph.
 *
 * A figure starts with its caption block, bold like a
 * definition term, and continues with the artwork lines of the
 * version showing: the first text version, or the first version
 * when every version is SVG. An SVG version shows as one
 * placeholder line. Every block carries the figure payload, so
 * any block can rebuild the figure.
 * @param Cursor The cursor, in the first block.
 * @param Figure The figure.
 */
/**
 * @brief Fills the cursor's blocks with one figure node.
 *
 * A figure starts with its caption block, bold like a
 * definition term, and continues with the artwork lines of the
 * version showing: the first text version, or the first version
 * when every version is SVG. An SVG version shows as one
 * placeholder line. Every block carries the figure payload, so
 * any block can rebuild the figure.
 * @param Cursor The cursor, in the first block.
 * @param FigureNode The <figure>, <artset>, or <artwork> node; borrowed.
 */
static void InsertFigureNode(QTextCursor& Cursor, xmlNodePtr FigureNode)
{
    QVector<xmlNodePtr> Versions = FigureVersionNodes(FigureNode);
    if (Versions.isEmpty()) {
        return;
    }
    QTextDocument* Document = Cursor.document();
    int Group = NextFigureGroup(Document);
    int Visible = 0;
    for (int i = 0; i < Versions.size(); ++i) {
        if (!ArtworkNodeIsSvg(Versions[i])) {
            Visible = i;
            break;
        }
    }
    QVariant Payload = FigureToVariant(FigureNode, Visible);
    QString Kind;
    if (DocDocument::NodeNameEquals(FigureNode, "artset")) {
        Kind = QStringLiteral("artset");
    } else if (DocDocument::NodeNameEquals(FigureNode, "artwork")) {
        Kind = QStringLiteral("artwork");
    } else {
        Kind = QStringLiteral("figure");
    }
    bool FirstBlock = true;
    if (DocDocument::NodeNameEquals(FigureNode, "figure")) {
        SetFigureProperties(Cursor, Kind, Group,
                            QStringLiteral("caption"), Payload);
        xmlNodePtr NameNode = DocDocument::ChildElement(FigureNode, "name");
        if (NameNode != nullptr) {
            InsertNodeRuns(Cursor, NameNode);
        }
        BoldBlockFragments(Document, Cursor.block());
        QTextCharFormat CaptionTyping = Cursor.charFormat();
        CaptionTyping.setFontWeight(QFont::Bold);
        Cursor.setCharFormat(CaptionTyping);
        FirstBlock = false;
    }
    xmlNodePtr Shown = Versions[Visible];
    if (ArtworkNodeIsSvg(Shown)) {
        if (!FirstBlock) {
            Cursor.insertBlock();
        }
        FirstBlock = false;
        SetFigureProperties(Cursor, Kind, Group,
                            QStringLiteral("artwork"), Payload);
        Cursor.setCharFormat(CodeCharFormat());
        Cursor.insertText(SvgPlaceholderText());
    } else {
        QStringList Lines =
            DocDocument::NodeText(Shown).split(QLatin1Char('\n'));
        for (const QString& Line : Lines) {
            if (!FirstBlock) {
                Cursor.insertBlock();
            }
            FirstBlock = false;
            SetFigureProperties(Cursor, Kind, Group,
                                QStringLiteral("artwork"), Payload);
            Cursor.setCharFormat(CodeCharFormat());
            Cursor.insertText(Line);
        }
    }
}

/**
 * @brief One cell's place in the grid: its row, its column, and
 *        the cell itself.
 */
struct DocTableCellPlacement
{
    int Row = 0;
    int Column = 0;
    int Colspan = 1;
    int Rowspan = 1;
    xmlNodePtr Cell = nullptr; /**< Borrowed cell node. */
};

/**
 * @brief Lays a table's cells out on the grid, accounting for spans.
 *
 * Rows are walked as one grid, header, body, and footer alike:
 * a cell is placed in the first free column of its row, and a
 * rowspan keeps its columns covered for the rows below. The
 * result lists every cell once, in row order, with its grid
 * column. The table is not ragged: the reader keeps ragged
 * tables preserved, and the editor never builds one.
 * @param Table The table.
 * @param Columns Receives the grid width: the rightmost column
 *        any cell or continuation reaches.
 * @return The placements, in row order.
 */
/**
 * @brief Lays a table node's cells out on the grid, accounting for spans.
 *
 * Rows are walked as one grid, header, body, and footer alike:
 * a cell is placed in the first free column of its row, and a
 * rowspan keeps its columns covered for the rows below. The
 * result lists every cell once, in row order, with its grid
 * column. The table is not ragged: the reader keeps ragged
 * tables preserved, and the editor never builds one.
 * @param TableNode The <table> node; borrowed.
 * @param Columns Receives the grid width: the rightmost column
 *        any cell or continuation reaches.
 * @return The placements, in row order.
 */
static QVector<DocTableCellPlacement> TableCellPlacements(
    xmlNodePtr TableNode, int& Columns)
{
    QVector<DocTableCellPlacement> Placed;
    QVector<int> Remaining;
    Columns = 0;
    int GridRow = 0;
    auto PlaceRow = [&](xmlNodePtr RowNode) {
        int Col = 0;
        for (xmlNodePtr CellNode = RowNode->children; CellNode != nullptr;
             CellNode = CellNode->next) {
            if (CellNode->type != XML_ELEMENT_NODE ||
                (!DocDocument::NodeNameEquals(CellNode, "td") &&
                 !DocDocument::NodeNameEquals(CellNode, "th"))) {
                continue;
            }
            int Colspan =
                DocDocument::NodeAttribute(CellNode, "colspan").toInt();
            if (Colspan < 1) {
                Colspan = 1;
            }
            int Rowspan =
                DocDocument::NodeAttribute(CellNode, "rowspan").toInt();
            if (Rowspan < 1) {
                Rowspan = 1;
            }
            while (Col < Remaining.size() && Remaining[Col] > 0) {
                ++Col;
            }
            while (Remaining.size() < Col + Colspan) {
                Remaining.append(0);
            }
            for (int c = Col; c < Col + Colspan; ++c) {
                Remaining[c] = Rowspan;
            }
            DocTableCellPlacement Placement;
            Placement.Row = GridRow;
            Placement.Column = Col;
            Placement.Colspan = Colspan;
            Placement.Rowspan = Rowspan;
            Placement.Cell = CellNode;
            Placed.append(Placement);
            Col += Colspan;
        }
        if (Remaining.size() > Columns) {
            Columns = Remaining.size();
        }
        for (int c = 0; c < Remaining.size(); ++c) {
            if (Remaining[c] > 0) {
                --Remaining[c];
            }
        }
        ++GridRow;
    };
    static const char* Sections[] = {"thead", "tbody", "tfoot"};
    for (const char* Section : Sections) {
        xmlNodePtr SectionNode =
            DocDocument::ChildElement(TableNode, Section);
        if (SectionNode == nullptr) {
            continue;
        }
        for (xmlNodePtr RowNode :
             DocDocument::ChildElements(SectionNode, "tr")) {
            PlaceRow(RowNode);
        }
    }
    return Placed;
}

/**
 * @brief Gives a table frame the full text width.
 *
 * Tables edit at full width: a table sized to its content
 * leaves empty cells too narrow to click into. The width is
 * display only: it is not an xml2rfc attribute, and the
 * writer never sees it. The frame's other format settings
 * (border, padding, spacing) are kept as Qt applied them.
 * @param Frame The frame.
 */
static void SetTableFullWidth(QTextTable* Frame)
{
    QTextTableFormat Format = Frame->format();
    Format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    Frame->setFormat(Format);
}

/**
 * @brief Builds a table paragraph at the cursor: a caption block
 *        over a grid.
 *
 * The cursor's block becomes the caption block, holding the
 * caption runs with the bold overlay of a figure caption, and
 * the grid follows it: the table is inserted at the end of the
 * caption block, so the caption stays before the frame. Spans
 * are merged while the cells are still empty, since merging
 * filled cells would join their text; the cells are filled
 * after. Header cells show bold, the way the caption does. The
 * cursor is left in the block after the frame, so the next
 * paragraph starts there.
 * @param Cursor The cursor, in the block becoming the caption.
 * @param Table The table to show.
 */
/**
 * @brief Builds a table node at the cursor: a caption block
 *        over a grid.
 *
 * The cursor's block becomes the caption block, holding the
 * caption runs with the bold overlay of a figure caption, and
 * the grid follows it: the table is inserted at the end of the
 * caption block, so the caption stays before the frame. Spans
 * are merged while the cells are still empty, since merging
 * filled cells would join their text; the cells are filled
 * after. Header cells show bold, the way the caption does. The
 * cursor is left in the block after the frame, so the next
 * paragraph starts there.
 * @param Cursor The cursor, in the block becoming the caption.
 * @param TableNode The <table> node to show; borrowed.
 */
static void InsertTableNode(QTextCursor& Cursor, xmlNodePtr TableNode)
{
    QTextDocument* Document = Cursor.document();
    int Group = NextTableGroup(Document);
    QVector<DocTableRowMark> Marks = TableMarksOf(TableNode);
    QVariant Payload = TableToVariant(TableNode, Marks);
    SetTableProperties(Cursor, Group, QStringLiteral("caption"), Payload);
    xmlNodePtr NameNode = DocDocument::ChildElement(TableNode, "name");
    if (NameNode != nullptr) {
        InsertNodeRuns(Cursor, NameNode);
    }
    BoldBlockFragments(Document, Cursor.block());
    QTextCharFormat CaptionTyping = Cursor.charFormat();
    CaptionTyping.setFontWeight(QFont::Bold);
    Cursor.setCharFormat(CaptionTyping);
    int Columns = 0;
    QVector<DocTableCellPlacement> Placed =
        TableCellPlacements(TableNode, Columns);
    int Rows = Marks.size();
    if (Rows < 1) {
        Rows = 1;
    }
    if (Columns < 1) {
        Columns = 1;
    }
    QTextTable* Frame = Cursor.insertTable(Rows, Columns);
    SetTableFullWidth(Frame);
    for (const DocTableCellPlacement& Placement : Placed) {
        if (Placement.Colspan > 1 || Placement.Rowspan > 1) {
            Frame->mergeCells(Placement.Row, Placement.Column,
                              Placement.Rowspan,
                              Placement.Colspan);
        }
    }
    for (const DocTableCellPlacement& Placement : Placed) {
        QTextCursor CellCursor =
            Frame->cellAt(Placement.Row, Placement.Column)
                .firstCursorPosition();
        SetTableCellProperties(CellCursor, Group, Payload, Placement.Cell);
        InsertNodeRuns(CellCursor, Placement.Cell);
        if (DocDocument::NodeNameEquals(Placement.Cell, "th")) {
            BoldBlockFragments(Document, CellCursor.block());
            QTextCharFormat CellTyping = CellCursor.charFormat();
            CellTyping.setFontWeight(QFont::Bold);
            CellCursor.setCharFormat(CellTyping);
        }
    }
    QTextBlock After = Document->findBlock(Frame->lastPosition()).next();
    if (After.isValid()) {
        Cursor.setPosition(After.position());
    } else {
        Cursor.movePosition(QTextCursor::End);
    }
}

/**
 * @brief Reads a table frame back into the model, with its caption.
 *
 * The caption comes from the caller: it lives in the caption
 * block's text, not in the frame. The kept attributes, the
 * section attributes, and the row sections come from the
 * payload on the first cell's first block; a frame without a
 * readable payload reads every row as a body row, with no kept
 * attributes. Spans are read from the grid itself, and covered
 * grid positions are skipped: each cell is read once, at its
 * origin. A cell's runs come from its blocks, joined with a
 * space when a merged cell holds more than one block. A frame
 * that somehow holds no body row gives up its last row to the
 * body, so the result always writes validly.
 * @param Frame The frame to read.
 * @param Caption The caption runs, from the caption block.
 * @return The table.
 */
/**
 * @brief Reads a table frame back into a node, with its caption.
 *
 * The caption comes from the caller: it lives in the caption
 * block's text, not in the frame. The kept attributes, the
 * section attributes, and the row sections come from the
 * payload on the first cell's first block; a frame without a
 * readable payload reads every row as a body row, with no kept
 * attributes. Spans are read from the grid itself, and covered
 * grid positions are skipped: each cell is read once, at its
 * origin. A cell's runs come from its blocks, joined with a
 * space when a merged cell holds more than one block. A frame
 * that somehow holds no body row gives up its last row to the
 * body, so the result always writes validly.
 * @param Frame The frame to read.
 * @param CaptionNode The caption <t> node, from the caption
 *        block, or nullptr for a bare table; borrowed.
 * @return The <table> node; the caller owns it.
 */
static xmlNodePtr TableFromFrame(QTextTable* Frame, xmlNodePtr CaptionNode)
{
    xmlNodePtr Table = NewNode("table");
    if (CaptionNode != nullptr && CaptionNode->children != nullptr) {
        xmlNodePtr NameNode = AddElementChild(Table, "name");
        for (xmlNodePtr Child = CaptionNode->children; Child != nullptr;
             Child = Child->next) {
            xmlAddChild(NameNode, xmlCopyNode(Child, 1));
        }
    }
    xmlNodePtr Shell = nullptr;
    QVector<DocTableRowMark> Marks;
    QTextBlock FirstBlock =
        Frame->cellAt(0, 0).firstCursorPosition().block();
    if (TablePayloadOf(FirstBlock, Shell, Marks)) {
        SetNodeAttributes(Table, NodeAttributes(Shell));
    }
    xmlNodePtr HeadSection = AddElementChild(Table, "thead");
    xmlNodePtr BodySection = AddElementChild(Table, "tbody");
    xmlNodePtr FootSection = AddElementChild(Table, "tfoot");
    if (Shell != nullptr) {
        auto TakeSectionAttrs = [&](xmlNodePtr Section,
                                    const char* Name) {
            xmlNodePtr Found = DocDocument::ChildElement(Shell, Name);
            if (Found != nullptr) {
                SetNodeAttributes(Section, NodeAttributes(Found));
            }
        };
        TakeSectionAttrs(HeadSection, "thead");
        TakeSectionAttrs(BodySection, "tbody");
        TakeSectionAttrs(FootSection, "tfoot");
        xmlFreeNode(Shell);
    }
    QSet<int> Seen;
    for (int r = 0; r < Frame->rows(); ++r) {
        QString Section = SectionOfRow(Marks, r);
        xmlNodePtr RowNode = NewNode("tr");
        if (!Marks.isEmpty()) {
            int At = r < Marks.size() ? r : Marks.size() - 1;
            if (!Marks[At].Anchor.isEmpty()) {
                DocDocument::SetNodeAttribute(RowNode, "anchor",
                                              Marks[At].Anchor);
            }
            SetNodeAttributes(RowNode, Marks[At].Attributes);
        }
        for (int c = 0; c < Frame->columns(); ++c) {
            QTextTableCell Cell = Frame->cellAt(r, c);
            int FirstPos = Cell.firstCursorPosition().position();
            if (Seen.contains(FirstPos)) {
                continue;
            }
            Seen.insert(FirstPos);
            xmlNodePtr CellNode = nullptr;
            QTextBlock CellFirst = Cell.firstCursorPosition().block();
            if (!TableCellFromVariant(
                    CellFirst.blockFormat().property(DocTableCellProperty),
                    CellNode)) {
                CellNode = NewNode("td");
            }
            int Colspan = Cell.columnSpan();
            int Rowspan = Cell.rowSpan();
            if (Colspan > 1) {
                DocDocument::SetNodeAttribute(
                    CellNode, "colspan", QString::number(Colspan));
            }
            if (Rowspan > 1) {
                DocDocument::SetNodeAttribute(
                    CellNode, "rowspan", QString::number(Rowspan));
            }
            bool FirstCellBlock = true;
            QTextBlock LastCellBlock = Cell.lastCursorPosition().block();
            for (QTextBlock CellBlock = CellFirst; CellBlock.isValid();
                 CellBlock = CellBlock.next()) {
                xmlNodePtr BlockNode = NodeFromBlock(CellBlock);
                if (!FirstCellBlock && BlockNode->children != nullptr) {
                    AddTextChild(CellNode, QStringLiteral(" "));
                }
                for (xmlNodePtr Child = BlockNode->children;
                     Child != nullptr;) {
                    xmlNodePtr Next = Child->next;
                    xmlUnlinkNode(Child);
                    xmlAddChild(CellNode, Child);
                    Child = Next;
                }
                xmlFreeNode(BlockNode);
                FirstCellBlock = false;
                if (CellBlock == LastCellBlock) {
                    break;
                }
            }
            xmlAddChild(RowNode, CellNode);
        }
        xmlNodePtr SectionNode = BodySection;
        if (Section == QLatin1String("thead")) {
            SectionNode = HeadSection;
        } else if (Section == QLatin1String("tfoot")) {
            SectionNode = FootSection;
        }
        xmlAddChild(SectionNode, RowNode);
    }
    if (HeadSection->children == nullptr) {
        xmlUnlinkNode(HeadSection);
        xmlFreeNode(HeadSection);
        HeadSection = nullptr;
    }
    if (FootSection->children == nullptr) {
        xmlUnlinkNode(FootSection);
        xmlFreeNode(FootSection);
        FootSection = nullptr;
    }
    if (BodySection->children == nullptr &&
        (HeadSection != nullptr || FootSection != nullptr)) {
        // No body row: the last row of the last section becomes
        // the body, so the table keeps its order and stays
        // writable. The row operations never allow this state;
        // it is guarded against, not expected.
        xmlNodePtr Donor = nullptr;
        if (FootSection != nullptr && FootSection->children != nullptr) {
            Donor = FootSection;
        } else if (HeadSection != nullptr &&
                   HeadSection->children != nullptr) {
            Donor = HeadSection;
        }
        if (Donor != nullptr) {
            xmlNodePtr LastRow = Donor->last;
            xmlUnlinkNode(LastRow);
            xmlAddChild(BodySection, LastRow);
        }
    }
    return Table;
}

/**
 * @brief Finds the grid row and column of the cell holding a cursor.
 *
 * The cell is found by its first block's position, scanning the
 * grid in row order, so a merged cell answers its origin.
 * @param Frame The frame.
 * @param Cursor The cursor, in the frame.
 * @param Row Receives the grid row.
 * @param Column Receives the grid column.
 * @return False when the cursor sits in no cell of the frame.
 */
static bool TableCellAtCursor(QTextTable* Frame, const QTextCursor& Cursor,
                              int& Row, int& Column)
{
    QTextTableCell Cell = Frame->cellAt(Cursor);
    if (!Cell.isValid()) {
        return false;
    }
    int FirstPos = Cell.firstCursorPosition().position();
    for (int r = 0; r < Frame->rows(); ++r) {
        for (int c = 0; c < Frame->columns(); ++c) {
            if (Frame->cellAt(r, c).firstCursorPosition().position() ==
                FirstPos) {
                Row = r;
                Column = c;
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief Re-stamps a table's payload on its caption block and
 *        every cell's first block, and marks any unmarked cell.
 *
 * Row and column operations change the payload or add blocks
 * carrying no marking: inserted rows and columns are born
 * without properties. Every origin cell's first block gets the
 * payload; a block carrying no cell map is marked as a fresh
 * cell of its row's section, with no kept attributes, and a
 * header cell gets the bold typing format. Cells already
 * marked keep their own data untouched. The frame's full
 * width is re-applied: row and column operations keep the
 * frame format, and the re-apply is cheap insurance that a
 * table never falls back to content width.
 * @param Document The document.
 * @param Frame The frame.
 * @param Payload The payload variant to stamp.
 * @param Marks The row marks, in grid order.
 * @param Group The table's group number.
 */
static void RestampTable(QTextDocument* Document, QTextTable* Frame,
                         const QVariant& Payload,
                         const QVector<DocTableRowMark>& Marks, int Group)
{
    QTextBlock FirstCell =
        Frame->cellAt(0, 0).firstCursorPosition().block();
    QTextBlock Caption = FirstCell.previous();
    if (Caption.isValid() && IsTableCaptionBlock(Caption)) {
        QTextCursor At(Document);
        At.setPosition(Caption.position());
        QTextBlockFormat Format = At.blockFormat();
        Format.setProperty(DocTablePayloadProperty, Payload);
        Format.setBottomMargin(0);
        At.setBlockFormat(Format);
    }
    QSet<int> Seen;
    for (int r = 0; r < Frame->rows(); ++r) {
        for (int c = 0; c < Frame->columns(); ++c) {
            QTextTableCell Cell = Frame->cellAt(r, c);
            int FirstPos = Cell.firstCursorPosition().position();
            if (Seen.contains(FirstPos)) {
                continue;
            }
            Seen.insert(FirstPos);
            QTextBlock First = Cell.firstCursorPosition().block();
            QTextCursor At(Document);
            At.setPosition(First.position());
            QTextBlockFormat Format = At.blockFormat();
            Format.setProperty(DocTablePayloadProperty, Payload);
            bool Fresh = !Format.property(DocTableCellProperty).isValid();
            bool FreshHeader = false;
            if (Fresh) {
                Format.setProperty(DocTableKindProperty,
                                   QStringLiteral("table"));
                Format.setProperty(DocTableGroupProperty, Group);
                Format.setProperty(DocTableRoleProperty,
                                   QStringLiteral("cell"));
                FreshHeader =
                    SectionOfRow(Marks, r) == QLatin1String("thead");
                xmlNodePtr FreshNode =
                    NewNode(FreshHeader ? "th" : "td");
                Format.setProperty(DocTableCellProperty,
                                   TableCellToVariant(FreshNode));
                xmlFreeNode(FreshNode);
            }
            Format.setBottomMargin(0);
            At.setBlockFormat(Format);
            if (Fresh && FreshHeader) {
                QTextCharFormat Typing = At.charFormat();
                Typing.setFontWeight(QFont::Bold);
                At.setCharFormat(Typing);
            }
        }
    }
    SetTableFullWidth(Frame);
}

/**
 * @brief One item paragraph of a list node being inserted.
 *
 * ItemNode is the <t> holding the paragraph, or the <li>,
 * <dt>, or <dd> itself when the item holds bare runs.
 * Borrowed from the list node.
 */
struct DocListItem
{
    xmlNodePtr ItemNode = nullptr;
    bool Continuation = false;
};

/**
 * @brief One unit of a list: a head paragraph plus the paragraphs
 *        that continue it.
 *
 * For a definition list the head is a term and the rest are its
 * definition paragraphs; for a bulleted or numbered list the head
 * is the first paragraph of an item and the rest are its further
 * paragraphs. Head is -1 when the unit has no head paragraph: a
 * leading run of continuations, which a definition list writes
 * with an empty term.
 */
struct DocListUnit
{
    int Head = -1;
    QVector<int> More;
};

/**
 * @brief One item paragraph a list node is built from.
 *
 * TNode is the <t> holding the paragraph; BuildListNode takes
 * ownership of it, adopting or freeing it.
 */
struct DocListBuildItem
{
    xmlNodePtr TNode = nullptr;
    bool Continuation = false;
};

/**
 * @brief Moves the text at and after an offset from one node's
 *        children into another node's children.
 *
 * Children wholly before the offset stay; children wholly after
 * it move whole. A text node holding the offset splits there,
 * and an element spanning the offset is split in place, so
 * tags around the split survive on both sides. Remaining is
 * the flattened text offset still to reach; it ends at 0.
 * @param Source The node losing the after part.
 * @param Target The node gaining the after part.
 * @param Remaining The offset still to reach, in flattened text.
 */
static void SplitNodeChildrenAt(xmlNodePtr Source, xmlNodePtr Target,
                                int& Remaining)
{
    for (xmlNodePtr Child = Source->children; Child != nullptr;) {
        xmlNodePtr Next = Child->next;
        if (Remaining <= 0) {
            xmlUnlinkNode(Child);
            xmlAddChild(Target, Child);
            Child = Next;
            continue;
        }
        if (Child->type == XML_TEXT_NODE ||
            Child->type == XML_CDATA_SECTION_NODE) {
            QString Text = DocDocument::NodeText(Child);
            if (Remaining >= Text.size()) {
                Remaining -= Text.size();
                Child = Next;
                continue;
            }
            QChar At = Text[Remaining];
            int AfterStart = Remaining;
            if (At == QChar(' ') || At == SoftBreakChar ||
                At == BreakGlyph) {
                // A separator at the split joins the two parts
                // and belongs to neither: it is deleted.
                AfterStart = Remaining + 1;
            }
            QString BeforeText = Text.left(Remaining);
            QString AfterText = Text.mid(AfterStart);
            if (BeforeText.isEmpty()) {
                xmlUnlinkNode(Child);
                xmlFreeNode(Child);
            } else {
                DocDocument::SetNodeText(Child, BeforeText);
            }
            if (!AfterText.isEmpty()) {
                xmlAddChild(Target, NewTextNode(AfterText));
            }
            Remaining = 0;
            Child = Next;
            continue;
        }
        if (Child->type == XML_ELEMENT_NODE) {
            QString Text = DocDocument::NodeText(Child);
            if (Remaining >= Text.size()) {
                Remaining -= Text.size();
                Child = Next;
                continue;
            }
            QByteArray Name(
                reinterpret_cast<const char*>(Child->name));
            xmlNodePtr Clone = NewNode(Name.constData());
            SetNodeAttributes(Clone, NodeAttributes(Child));
            xmlAddChild(Target, Clone);
            SplitNodeChildrenAt(Child, Clone, Remaining);
            Child = Next;
            continue;
        }
        // Comments and other nodes stay with the before part.
        Child = Next;
    }
}

/**
 * @brief Splits a <t> node's text at a flattened offset.
 *
 * The node keeps the text before the offset; the returned node
 * holds the text after it, with a separator at the offset
 * deleted.
 * @param TNode The <t> node; keeps the before part.
 * @param Offset The flattened text offset to split at.
 * @return The <t> node holding the after part; the caller owns it.
 */
static xmlNodePtr SplitTNodeAt(xmlNodePtr TNode, int Offset)
{
    xmlNodePtr After = NewNode("t");
    int Remaining = Offset;
    SplitNodeChildrenAt(TNode, After, Remaining);
    return After;
}

/**
 * @brief Builds a list node from its item paragraphs.
 *
 * Items group into units by continuation, as the old writer
 * grouped them: a head paragraph plus its continuations. A
 * single-paragraph unit writes its runs bare inside the item
 * element; several write as <t> children, one per paragraph.
 * A blank unit writes as a blank line, so a blank line typed
 * inside a list is saved and reads back without splitting
 * the list; a blank definition in a surviving pair writes one
 * space, since a <dd> must hold content while an empty <dt>
 * is allowed. The items' <t> nodes are consumed: adopted
 * into the list, or freed when blank.
 * @param Kind The list element name.
 * @param Attributes The list element's attributes.
 * @param Items The item paragraphs, in order; consumed.
 * @return The list node; the caller owns it.
 */
static xmlNodePtr BuildListNode(
    const QString& Kind,
    const QVector<QPair<QString, QString>>& Attributes,
    QVector<DocListBuildItem>& Items)
{
    QByteArray KindBytes = Kind.toUtf8();
    xmlNodePtr List = NewNode(KindBytes.constData());
    SetNodeAttributes(List, Attributes);
    QVector<DocListUnit> Units;
    for (int i = 0; i < Items.size(); ++i) {
        if (!Items[i].Continuation || Units.isEmpty()) {
            DocListUnit Unit;
            if (Items[i].Continuation) {
                Unit.More.append(i);
            } else {
                Unit.Head = i;
            }
            Units.append(Unit);
        } else {
            Units.last().More.append(i);
        }
    }
    auto UnitIsBlank = [&](const DocListUnit& Unit) {
        if (Unit.Head >= 0 && !NodeIsBlank(Items[Unit.Head].TNode)) {
            return false;
        }
        for (int Index : Unit.More) {
            if (!NodeIsBlank(Items[Index].TNode)) {
                return false;
            }
        }
        return true;
    };
    auto AppendBlankLine = [&](xmlNodePtr Parent) {
        xmlAddChild(Parent, NewTextNode(QStringLiteral("\n\n")));
    };
    auto AppendUnitParagraphs = [&](xmlNodePtr ItemElement,
                                    const QVector<int>& Indices) {
        QVector<int> Content;
        for (int Index : Indices) {
            if (!NodeIsBlank(Items[Index].TNode)) {
                Content.append(Index);
            }
        }
        if (Content.isEmpty()) {
            AddTextChild(ItemElement, QStringLiteral(" "));
            return;
        }
        bool Single = Content.size() == 1;
        bool EmittedContent = false;
        for (int Index : Indices) {
            if (NodeIsBlank(Items[Index].TNode)) {
                if (EmittedContent) {
                    AppendBlankLine(ItemElement);
                }
                continue;
            }
            if (Single) {
                xmlNodePtr TNode = Items[Index].TNode;
                for (xmlNodePtr Child = TNode->children;
                     Child != nullptr;) {
                    xmlNodePtr Next = Child->next;
                    xmlUnlinkNode(Child);
                    xmlAddChild(ItemElement, Child);
                    Child = Next;
                }
                xmlFreeNode(TNode);
                Items[Index].TNode = nullptr;
            } else {
                xmlAddChild(ItemElement, Items[Index].TNode);
                Items[Index].TNode = nullptr;
            }
            EmittedContent = true;
        }
    };
    if (Kind == QLatin1String("dl")) {
        for (const DocListUnit& Unit : Units) {
            if (UnitIsBlank(Unit)) {
                AppendBlankLine(List);
                continue;
            }
            xmlNodePtr Term = AddElementChild(List, "dt");
            if (Unit.Head >= 0) {
                xmlNodePtr TNode = Items[Unit.Head].TNode;
                for (xmlNodePtr Child = TNode->children;
                     Child != nullptr;) {
                    xmlNodePtr Next = Child->next;
                    xmlUnlinkNode(Child);
                    xmlAddChild(Term, Child);
                    Child = Next;
                }
                xmlFreeNode(TNode);
                Items[Unit.Head].TNode = nullptr;
            }
            xmlNodePtr Definition = AddElementChild(List, "dd");
            AppendUnitParagraphs(Definition, Unit.More);
        }
    } else {
        for (const DocListUnit& Unit : Units) {
            if (UnitIsBlank(Unit)) {
                AppendBlankLine(List);
                continue;
            }
            xmlNodePtr ItemElement = AddElementChild(List, "li");
            QVector<int> Indices;
            if (Unit.Head >= 0) {
                Indices.append(Unit.Head);
            }
            Indices.append(Unit.More);
            AppendUnitParagraphs(ItemElement, Indices);
        }
    }
    for (const DocListBuildItem& Item : Items) {
        xmlFreeNode(Item.TNode);
    }
    Items.clear();
    return List;
}

/**
 * @brief Inserts a list node at the cursor, in fresh blocks.
 *
 * The items go in as plain blocks, each marked with the list's
 * kind, group, attributes, and role. A bulleted, numbered, or
 * inline list then gets one list created over its head blocks
 * only, so the bullets or numbers show and Enter adds an item;
 * a continuation block sits outside the list object, indented
 * to line up with the item text, or Qt would give it its own
 * bullet or number. A definition list gets no list: its blocks
 * are told apart by their properties alone, its term blocks
 * show bold on a tinted background, and its definition blocks
 * are indented. The node carries its own identity, so there
 * is no group parameter.
 * @param Cursor The cursor, at the start of the fresh block.
 * @param ListNode The <ul>, <ol>, <dl>, or <list> node; borrowed.
 */
static void InsertListNode(QTextCursor& Cursor, xmlNodePtr ListNode)
{
    if (ListNode == nullptr || !IsListNode(ListNode)) {
        return;
    }
    QString Kind = QString::fromUtf8(
        reinterpret_cast<const char*>(ListNode->name));
    QVector<QPair<QString, QString>> Attributes =
        NodeAttributes(ListNode);
    QVector<DocListItem> Items;
    for (xmlNodePtr Child = ListNode->children; Child != nullptr;
         Child = Child->next) {
        if (Child->type == XML_TEXT_NODE ||
            Child->type == XML_CDATA_SECTION_NODE) {
            QString Text = DocDocument::NodeText(Child);
            if (!Text.trimmed().isEmpty()) {
                continue;
            }
            int Newlines = Text.count(QLatin1Char('\n'));
            int Blanks = Newlines > 1 ? Newlines - 1 : 0;
            for (int i = 0; i < Blanks; ++i) {
                DocListItem Item;
                Item.ItemNode = nullptr;
                Item.Continuation = false;
                Items.append(Item);
            }
            continue;
        }
        if (Child->type != XML_ELEMENT_NODE) {
            continue;
        }
        bool IsTerm = DocDocument::NodeNameEquals(Child, "dt");
        bool IsDefinition = DocDocument::NodeNameEquals(Child, "dd");
        bool IsItem = DocDocument::NodeNameEquals(Child, "li");
        if (!IsTerm && !IsDefinition && !IsItem) {
            continue;
        }
        bool HasT = false;
        bool FirstT = true;
        for (xmlNodePtr Part = Child->children; Part != nullptr;
             Part = Part->next) {
            if (Part->type == XML_ELEMENT_NODE &&
                DocDocument::NodeNameEquals(Part, "t")) {
                HasT = true;
                DocListItem Item;
                Item.ItemNode = Part;
                // A <dd> holds continuations of its term. In an
                // <li> or <dt>, the first <t> starts the item
                // and later ones continue it.
                Item.Continuation = IsDefinition || !FirstT;
                FirstT = false;
                Items.append(Item);
            }
        }
        if (!HasT) {
            DocListItem Item;
            Item.ItemNode = Child;
            Item.Continuation = IsDefinition;
            Items.append(Item);
        }
    }
    if (Items.isEmpty()) {
        return;
    }
    QTextDocument* Document = Cursor.document();
    int Group = NextListGroup(Document);
    int StartPos = Cursor.position();
    QVector<QTextBlock> Heads;
    bool FirstItem = true;
    for (const DocListItem& Item : Items) {
        if (!FirstItem) {
            Cursor.insertBlock();
        }
        FirstItem = false;
        if ((Item.ItemNode == nullptr || NodeIsBlank(Item.ItemNode)) &&
            !Item.Continuation) {
            // A blank line between items is a plain blank block,
            // not a list item, or Qt would give it a bullet or
            // number. It keeps no list properties; the walk picks
            // it up by position, between the items around it.
            DetachBlockFromList(Document, Cursor.block());
            Cursor.setCharFormat(QTextCharFormat());
            continue;
        }
        QString Role;
        if (Kind == QLatin1String("dl")) {
            Role = Item.Continuation ? QStringLiteral("definition")
                                     : QStringLiteral("term");
        } else if (Kind == QLatin1String("ul") ||
                   Kind == QLatin1String("ol")) {
            Role = Item.Continuation ? QStringLiteral("continuation")
                                     : QStringLiteral("item");
        }
        SetListProperties(Cursor, Kind, Group, Attributes, Role);
        if (IsContinuationRole(Role)) {
            QTextBlockFormat Indented = Cursor.blockFormat();
            Indented.setIndent(1);
            Cursor.setBlockFormat(Indented);
        } else {
            Heads.append(Cursor.block());
        }
        if (Item.ItemNode != nullptr) {
            InsertNodeRuns(Cursor, Item.ItemNode);
        }
    }
    int EndPos = Cursor.position();
    if (Kind == QLatin1String("dl")) {
        // Style from the first block that is actually part of
        // the list: a leading blank line is a plain block, and
        // styling from it would read the wrong group.
        QTextBlock First = Document->findBlock(StartPos);
        while (First.isValid() && First.position() < EndPos &&
               First.blockFormat().property(DocListKindProperty).toString() !=
                   QLatin1String("dl")) {
            First = First.next();
        }
        if (First.isValid() && First.position() < EndPos) {
            StyleDefinitionGroup(Document, First);
        }
        Cursor.setPosition(EndPos);
        return;
    }
    // One list over the head blocks only: a continuation block
    // sits outside the list object, or Qt would give it its own
    // bullet or number. The heads are added in document order,
    // so the numbers follow the document.
    QTextList* List = nullptr;
    QTextListFormat Format = ListFormatForKind(Kind, Attributes);
    for (const QTextBlock& Head : Heads) {
        if (List == nullptr) {
            QTextCursor At(Document);
            At.setPosition(Head.position());
            List = At.createList(Format);
        } else {
            List->add(Head);
        }
    }
    Cursor.setPosition(EndPos);
}

/**
 * @brief Inserts a new block that belongs to no list.
 *
 * A block inserted after a list item would join the list, so it
 * is taken back out, any indent it inherited is cleared, and the
 * list, sourcecode, and figure properties it inherited are
 * cleared too, so a definition list does not claim it and a
 * figure region does not extend into it. The new block is a
 * plain paragraph, so it takes the paragraph spacing: the gap
 * below it is what shows the blank line between paragraphs
 * after a reload. The typing format is reset to plain text.
 * @param Cursor The cursor to insert at.
 */
static void InsertParagraphBreak(QTextCursor& Cursor)
{
    Cursor.insertBlock();
    if (QTextList* List = Cursor.currentList()) {
        List->remove(Cursor.block());
    }
    QTextBlockFormat Format = Cursor.blockFormat();
    Format.setIndent(0);
    Format.clearProperty(QTextFormat::BackgroundBrush);
    Format.clearProperty(DocListProperty);
    Format.clearProperty(DocListKindProperty);
    Format.clearProperty(DocListGroupProperty);
    Format.clearProperty(DocListRoleProperty);
    Format.clearProperty(DocSourceProperty);
    Format.clearProperty(DocSourceKindProperty);
    Format.clearProperty(DocFigureKindProperty);
    Format.clearProperty(DocFigureGroupProperty);
    Format.clearProperty(DocFigureRoleProperty);
    Format.clearProperty(DocFigurePayloadProperty);
    Format.clearProperty(DocTableKindProperty);
    Format.clearProperty(DocTableGroupProperty);
    Format.clearProperty(DocTableRoleProperty);
    Format.clearProperty(DocTablePayloadProperty);
    Format.clearProperty(DocTableCellProperty);
    Format.setBottomMargin(ParagraphGap(Cursor.document()));
    Cursor.setBlockFormat(Format);
    Cursor.setCharFormat(QTextCharFormat());
}

/**
 * @brief One piece of pasted HTML: text with its tag stack, or a line break.
 *
 * Pieces are the flat middle step between the scratch
 * document's fragments and the run nodes RunsForPastePieces
 * builds: each
 * fragment contributes its text under one stack, split on soft
 * breaks, and a break piece stands for one <br>.
 */
struct DocPastePiece
{
    QString Text;      /**< The text; empty for a break piece. */
    DocTagStack Stack; /**< The tags around the text. */
    bool IsBreak = false; /**< True for a <br> piece. */
};

/**
 * @brief Builds the tag stack one pasted fragment carries.
 *
 * Bold maps to strong, italic to em, a fixed-pitch font to tt,
 * and subscript and superscript to sub and sup. A link whose
 * target is an absolute address maps to an eref carrying the
 * target; a fragment-only or relative link keeps its text and
 * drops the link, since anchor matching belongs to the picker
 * (Bug 322). Underline, strikethrough, colors, sizes, and font
 * families have no equivalent and are dropped. The stack is
 * filtered through the nesting rules as it is built, so it can
 * never hold a nesting the schema forbids; eref goes innermost,
 * since nothing nests inside it.
 * @param Format The fragment's character format.
 * @param Heading True when the fragment sits in a heading block:
 *        the heading's own bold is dropped, not mapped to strong.
 * @return The stack, outermost first.
 */
static DocTagStack StackForPasteFormat(const QTextCharFormat& Format,
                                       bool Heading)
{
    QVector<DocTagEntry> Candidates;
    if (!Heading && Format.fontWeight() >= QFont::Bold) {
        DocTagEntry Entry;
        Entry.Name = QStringLiteral("strong");
        Candidates.append(Entry);
    }
    if (Format.fontItalic()) {
        DocTagEntry Entry;
        Entry.Name = QStringLiteral("em");
        Candidates.append(Entry);
    }
    if (QFontInfo(Format.font()).fixedPitch()) {
        DocTagEntry Entry;
        Entry.Name = QStringLiteral("tt");
        Candidates.append(Entry);
    }
    if (Format.verticalAlignment() == QTextCharFormat::AlignSubScript) {
        DocTagEntry Entry;
        Entry.Name = QStringLiteral("sub");
        Candidates.append(Entry);
    } else if (Format.verticalAlignment() == QTextCharFormat::AlignSuperScript) {
        DocTagEntry Entry;
        Entry.Name = QStringLiteral("sup");
        Candidates.append(Entry);
    }
    QString Href = Format.anchorHref();
    if (!Href.isEmpty() &&
        (Href.contains(QStringLiteral("://")) ||
         Href.startsWith(QStringLiteral("mailto:")))) {
        DocTagEntry Entry;
        Entry.Name = QStringLiteral("eref");
        Entry.Attributes.append(
            QPair<QString, QString>(QStringLiteral("target"), Href));
        Candidates.append(Entry);
    }
    DocTagStack Stack;
    for (const DocTagEntry& Entry : Candidates) {
        QString Parent = Stack.isEmpty()
            ? QStringLiteral("t") : Stack.last().Name;
        if (CanContain(Parent, Entry.Name)) {
            Stack.append(Entry);
        }
    }
    return Stack;
}

/**
 * @brief Splits one scratch-document block into paste pieces.
 *
 * Image fragments are skipped, and object-replacement
 * characters are stripped, so pasted images leave nothing
 * behind. Soft breaks (<br>) become break pieces, so they
 * insert through the marker path; a break piece's stack is
 * popped until its innermost entry may hold a br, since a br
 * may not sit inside a sub, a sup, or a link.
 * @param Block The scratch-document block.
 * @param Heading True when the block is a heading: its bold is
 *        dropped rather than mapped to strong.
 * @return The pieces, in document order.
 */
static QVector<DocPastePiece> PiecesForPasteBlock(const QTextBlock& Block,
                                                  bool Heading)
{
    QVector<DocPastePiece> Pieces;
    const QChar ObjectReplacement(0xFFFC);
    for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
        QTextFragment Fragment = It.fragment();
        QTextCharFormat Format = Fragment.charFormat();
        if (Format.isImageFormat()) {
            continue;
        }
        QString Text = Fragment.text();
        Text.remove(ObjectReplacement);
        if (Text.isEmpty()) {
            continue;
        }
        DocTagStack Stack = StackForPasteFormat(Format, Heading);
        int Start = 0;
        while (Start <= Text.size()) {
            int Break = Text.indexOf(SoftBreakChar, Start);
            int End = Break < 0 ? Text.size() : Break;
            if (End > Start) {
                DocPastePiece Piece;
                Piece.Text = Text.mid(Start, End - Start);
                Piece.Stack = Stack;
                Pieces.append(Piece);
            }
            if (Break < 0) {
                break;
            }
            DocPastePiece Marker;
            Marker.IsBreak = true;
            Marker.Stack = Stack;
            while (!Marker.Stack.isEmpty() &&
                   !CanContain(Marker.Stack.last().Name,
                               QStringLiteral("br"))) {
                Marker.Stack.removeLast();
            }
            Pieces.append(Marker);
            Start = Break + 1;
        }
    }
    return Pieces;
}

/**
 * @brief Tests whether one tag stack starts with another.
 * @param Stack The stack to test.
 * @param Prefix The prefix to look for.
 * @return True when the stack starts with the prefix.
 */
static bool StackHasPrefix(const DocTagStack& Stack, const DocTagStack& Prefix)
{
    if (Stack.size() < Prefix.size()) {
        return false;
    }
    for (int i = 0; i < Prefix.size(); ++i) {
        if (!(Stack[i] == Prefix[i])) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Builds runs from paste pieces under one open stack.
 *
 * Pieces whose stack equals the open stack append as text or
 * break runs at this level; a piece with a longer stack opens
 * its next entry and recurses for the children. Adjacent text
 * pieces at one level join into one run. Every piece under the
 * open stack is consumed, so the walk always ends.
 * @param Pieces The pieces to build from.
 * @param Pos The next piece to take; advanced past the pieces used.
 * @param Parent The stack already open above this level.
 * @param Runs Receives the runs built at this level.
 */
static void AppendPasteRuns(const QVector<DocPastePiece>& Pieces, int& Pos,
                            const DocTagStack& Parent, xmlNodePtr ParentNode)
{
    while (Pos < Pieces.size()) {
        const DocPastePiece& Piece = Pieces[Pos];
        if (!StackHasPrefix(Piece.Stack, Parent)) {
            return;
        }
        if (Piece.Stack.size() == Parent.size()) {
            if (Piece.IsBreak) {
                xmlAddChild(ParentNode, NewNode("br"));
            } else {
                AddTextChild(ParentNode, Piece.Text);
            }
            ++Pos;
        } else {
            DocTagStack Child = Parent;
            Child.append(Piece.Stack[Parent.size()]);
            QByteArray Name = Child.last().Name.toUtf8();
            xmlNodePtr Element = NewNode(Name.constData());
            SetNodeAttributes(Element, Child.last().Attributes);
            xmlAddChild(ParentNode, Element);
            AppendPasteRuns(Pieces, Pos, Child, Element);
        }
    }
}

/**
 * @brief Builds the runs for one block's paste pieces.
 * @param Pieces The pieces, in document order.
 * @return The runs.
 */
static QVector<xmlNodePtr> RunsForPastePieces(
    const QVector<DocPastePiece>& Pieces)
{
    xmlNodePtr Holder = NewNode("t");
    int Pos = 0;
    DocTagStack Parent;
    AppendPasteRuns(Pieces, Pos, Parent, Holder);
    QVector<xmlNodePtr> Runs;
    for (xmlNodePtr Child = Holder->children; Child != nullptr;) {
        xmlNodePtr Next = Child->next;
        xmlUnlinkNode(Child);
        Runs.append(Child);
        Child = Next;
    }
    xmlFreeNode(Holder);
    return Runs;
}

/**
 * @brief Tests whether paste pieces hold anything worth a paragraph.
 *
 * A block of only whitespace holds nothing; a block holding a
 * break holds a blank line, which is content.
 * @param Pieces The pieces.
 * @return True when any piece is a break or holds non-blank text.
 */
static bool PastePiecesHaveContent(const QVector<DocPastePiece>& Pieces)
{
    for (const DocPastePiece& Piece : Pieces) {
        if (Piece.IsBreak || !Piece.Text.trimmed().isEmpty()) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Returns the editor list kind for a scratch-document list.
 *
 * Bullets of every shape map to a bulleted list; decimal,
 * alphabetic, and roman numbering all map to a numbered list,
 * showing decimal numbers -- the source's numbering style is
 * dropped, since the editor shows every numbered list decimal.
 * @param List The scratch-document list.
 * @return "ul" or "ol".
 */
static QString PasteListKind(const QTextList* List)
{
    QTextListFormat::Style Style = List->format().style();
    if (Style == QTextListFormat::ListDecimal ||
        Style == QTextListFormat::ListLowerAlpha ||
        Style == QTextListFormat::ListUpperAlpha ||
        Style == QTextListFormat::ListLowerRoman ||
        Style == QTextListFormat::ListUpperRoman) {
        return QStringLiteral("ol");
    }
    return QStringLiteral("ul");
}

/**
 * @brief Inserts one pasted paragraph at the cursor.
 *
 * The first pasted unit merges into the cursor's block; later
 * units start on a fresh block, detached from any list.
 * @param Cursor The cursor to insert at.
 * @param First True until the first unit is inserted; cleared here.
 * @param Runs The paragraph's runs.
 */
static void EmitPastePlain(QTextCursor& Cursor, bool& First,
                           const QVector<xmlNodePtr>& Runs)
{
    if (!First) {
        InsertParagraphBreak(Cursor);
    }
    First = false;
    xmlNodePtr Holder = NewNode("t");
    for (xmlNodePtr Run : Runs) {
        xmlAddChild(Holder, Run);
    }
    InsertNodeRuns(Cursor, Holder);
    xmlFreeNode(Holder);
}

/**
 * @brief True when the option markers are to show (Bug 420).
 *
 * One setting shared by every text editor pane, remembered
 * across runs; on until the user turns it off.
 * @return True when markers show.
 */
static bool OptionMarkersShown()
{
    QSettings Settings;
    return Settings.value(QStringLiteral("editor/showOptionMarkers"),
                          true).toBool();
}

/**
 * @brief Returns the hover text for a sourcecode option marker.
 *
 * The tag name, then each option set on the block, then the
 * anchor when the block has one, then the hint. The anchor
 * shows read-only (Bug 464 v2): it is displayed, never
 * edited from the gear UI. Prep-tool attributes and
 * base/lang settings never show either.
 * @param Attributes The block's attributes.
 * @return The hover text.
 */
static QString OptionMarkerTooltip(
    const QVector<QPair<QString, QString>>& Attributes)
{
    QStringList Options;
    QString Name = AttributeOf(Attributes, QStringLiteral("name"));
    if (!Name.isEmpty()) {
        Options.append(QStringLiteral("name \"%1\"").arg(Name));
    }
    QString Type = AttributeOf(Attributes, QStringLiteral("type"));
    if (!Type.isEmpty()) {
        Options.append(QStringLiteral("type \"%1\"").arg(Type));
    }
    QString Markers = AttributeOf(Attributes, QStringLiteral("markers"));
    if (!Markers.isEmpty()) {
        Options.append(Markers == QLatin1String("true")
                           ? QStringLiteral("markers on")
                           : QStringLiteral("markers off"));
    }
    QString Src = AttributeOf(Attributes, QStringLiteral("src"));
    if (!Src.isEmpty()) {
        Options.append(QStringLiteral("src \"%1\"").arg(Src));
    }
    QString Text = QStringLiteral("Source code");
    if (!Options.isEmpty()) {
        Text += QStringLiteral(": ") + Options.join(QStringLiteral(", "));
    }
    // Bug 464 v7: the anchor always shows read-only: its
    // value when the block has one, "(none)" when it has none.
    QString Anchor = AttributeOf(Attributes, QStringLiteral("anchor"));
    Text += QStringLiteral("\nAnchor: ") +
            (Anchor.isEmpty() ? QStringLiteral("(none)") : Anchor);
    Text += QStringLiteral("\nRight-click to change options.");
    return Text;
}

/**
 * @brief Returns the hover text for a list option marker (Bug 464).
 *
 * The tag name, then each option set on the list, then the
 * anchor when the list has one, then the hint. For an inline
 * <list> the options are its style, hang indent, and counter;
 * for a <ul> they are its empty and bare switches, shown as
 * on or off, plus its spacing and indent (Bug 464 v2). For
 * an <ol> they are its type, start, group, spacing, and
 * indent; for a <dl> its spacing, newline, and indent
 * (Bug 464 v2). The anchor shows read-only: displayed,
 * never edited from the gear UI. The pn never shows: it is
 * prep-tool assigned.
 * @param Kind The list element name: "list", "ul", "ol",
 *        or "dl".
 * @param Attributes The list's attributes.
 * @return The hover text.
 */
static QString ListOptionTooltip(
    const QString& Kind,
    const QVector<QPair<QString, QString>>& Attributes)
{
    QStringList Options;
    if (Kind == QLatin1String("list")) {
        QString Style = AttributeOf(Attributes, QStringLiteral("style"));
        if (!Style.isEmpty()) {
            Options.append(QStringLiteral("style \"%1\"").arg(Style));
        }
        QString HangIndent =
            AttributeOf(Attributes, QStringLiteral("hangIndent"));
        if (!HangIndent.isEmpty()) {
            Options.append(
                QStringLiteral("hangIndent \"%1\"").arg(HangIndent));
        }
        QString Counter =
            AttributeOf(Attributes, QStringLiteral("counter"));
        if (!Counter.isEmpty()) {
            Options.append(
                QStringLiteral("counter \"%1\"").arg(Counter));
        }
    } else if (Kind == QLatin1String("ul")) {
        QString Empty = AttributeOf(Attributes, QStringLiteral("empty"));
        if (!Empty.isEmpty()) {
            Options.append(Empty == QLatin1String("true")
                               ? QStringLiteral("empty on")
                               : QStringLiteral("empty off"));
        }
        QString Bare = AttributeOf(Attributes, QStringLiteral("bare"));
        if (!Bare.isEmpty()) {
            Options.append(Bare == QLatin1String("true")
                               ? QStringLiteral("bare on")
                               : QStringLiteral("bare off"));
        }
        // Bug 464 v2: a <ul> also carries spacing and indent.
        QString Spacing =
            AttributeOf(Attributes, QStringLiteral("spacing"));
        if (!Spacing.isEmpty()) {
            Options.append(
                QStringLiteral("spacing \"%1\"").arg(Spacing));
        }
        QString Indent = AttributeOf(Attributes, QStringLiteral("indent"));
        if (!Indent.isEmpty()) {
            Options.append(
                QStringLiteral("indent \"%1\"").arg(Indent));
        }
    } else if (Kind == QLatin1String("ol")) {
        // Bug 464 v2: an <ol> carries type, start, group,
        // spacing, and indent.
        QString Type = AttributeOf(Attributes, QStringLiteral("type"));
        if (!Type.isEmpty()) {
            Options.append(QStringLiteral("type \"%1\"").arg(Type));
        }
        QString Start = AttributeOf(Attributes, QStringLiteral("start"));
        if (!Start.isEmpty()) {
            Options.append(QStringLiteral("start \"%1\"").arg(Start));
        }
        QString Group = AttributeOf(Attributes, QStringLiteral("group"));
        if (!Group.isEmpty()) {
            Options.append(QStringLiteral("group \"%1\"").arg(Group));
        }
        QString Spacing =
            AttributeOf(Attributes, QStringLiteral("spacing"));
        if (!Spacing.isEmpty()) {
            Options.append(
                QStringLiteral("spacing \"%1\"").arg(Spacing));
        }
        QString Indent = AttributeOf(Attributes, QStringLiteral("indent"));
        if (!Indent.isEmpty()) {
            Options.append(
                QStringLiteral("indent \"%1\"").arg(Indent));
        }
    } else {
        // Bug 464 v2: a <dl> carries spacing, newline, and indent.
        QString Spacing =
            AttributeOf(Attributes, QStringLiteral("spacing"));
        if (!Spacing.isEmpty()) {
            Options.append(
                QStringLiteral("spacing \"%1\"").arg(Spacing));
        }
        // Bug 464 v10: the effective default is on (xml2rfc
        // puts a bare <dl> definition on the next line), so
        // the tooltip shows the effective value, not just a
        // set attribute: "newline off" only when explicitly
        // "false".
        QString Newline =
            AttributeOf(Attributes, QStringLiteral("newline"));
        Options.append(Newline == QLatin1String("false")
                           ? QStringLiteral("newline off")
                           : QStringLiteral("newline on"));
        QString Indent = AttributeOf(Attributes, QStringLiteral("indent"));
        if (!Indent.isEmpty()) {
            Options.append(
                QStringLiteral("indent \"%1\"").arg(Indent));
        }
    }
    QString Text = QStringLiteral("List");
    if (!Options.isEmpty()) {
        Text += QStringLiteral(": ") + Options.join(QStringLiteral(", "));
    }
    // Bug 464 v7: the anchor always shows read-only: its
    // value when the list has one, "(none)" when it has none.
    QString Anchor = AttributeOf(Attributes, QStringLiteral("anchor"));
    Text += QStringLiteral("\nAnchor: ") +
            (Anchor.isEmpty() ? QStringLiteral("(none)") : Anchor);
    Text += QStringLiteral("\nRight-click to change options.");
    return Text;
}

/**
 * @brief The placeholder line a src sourcecode block shows.
 *
 * The block holds no text of its own, so the placeholder
 * names the file in its place. The text is exactly as shown,
 * with no quotes around the file name.
 * @param Src The src attribute value.
 * @return The placeholder line.
 */
static QString SrcPlaceholderText(const QString& Src)
{
    return QStringLiteral("<Src=") + Src + QStringLiteral(">");
}

/**
 * @brief Builds a fresh character format for a src placeholder.
 *
 * The placeholder shows italic and plain: no underline, no
 * special color, the document's own font. Only the placeholder
 * property is set, carrying the src it was stamped for.
 * @param Src The src attribute value.
 * @return The format.
 */
static QTextCharFormat FormatForSrcPlaceholder(const QString& Src)
{
    QTextCharFormat Format;
    Format.setFontItalic(true);
    Format.setProperty(DocSrcPlaceholderProperty, Src);
    return Format;
}

/**
 * @brief Escapes text the way Qt's HTML writer does.
 *
 * Ampersands first, then brackets, so the replacements never
 * double-escape each other.
 * @param Text The plain text.
 * @return The text with &, <, and > escaped.
 */
static QString EscapeHtmlText(const QString& Text)
{
    QString Escaped = Text;
    Escaped.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    Escaped.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    Escaped.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return Escaped;
}

/**
 * @brief Builds a fresh character format for an option marker.
 *
 * The gear shows plain: no underline, no special color, the
 * document's own font. Only the marker property and the hover
 * text are set.
 * @param Attributes The block's attributes, for the hover text.
 * @return The format.
 */
static QTextCharFormat FormatForOptionMarker(
    const QVector<QPair<QString, QString>>& Attributes)
{
    QTextCharFormat Format;
    Format.setProperty(DocOptionMarkerProperty,
                       QStringLiteral("sourcecode"));
    Format.setToolTip(OptionMarkerTooltip(Attributes));
    return Format;
}

/**
 * @brief Builds a fresh character format for a list option marker
 *        (Bug 464).
 *
 * The gear shows plain: no underline, no special color, the
 * document's own font. Only the marker property and the hover
 * text are set. The property carries the list element name,
 * "list", "ul", "ol", or "dl", the way the sourcecode marker
 * carries "sourcecode".
 * @param Kind The list element name: "list", "ul", "ol",
 *        or "dl".
 * @param Attributes The list's attributes, for the hover text.
 * @return The format.
 */
static QTextCharFormat FormatForListOptionMarker(
    const QString& Kind,
    const QVector<QPair<QString, QString>>& Attributes)
{
    QTextCharFormat Format;
    Format.setProperty(DocOptionMarkerProperty, Kind);
    Format.setToolTip(ListOptionTooltip(Kind, Attributes));
    return Format;
}

/**
 * @brief Returns the hover text for a table option marker (Bug 448).
 *
 * The part name, then each option set on the part, then the
 * anchor when the part has one, then the hint. A table and a
 * cell show their align when set; a section and a row carry
 * only an anchor. The anchor shows read-only: displayed,
 * never edited from the gear UI. The pn never shows: it is
 * prep-tool assigned.
 * @param Kind The table part: "table", "thead", "tbody",
 *        "tfoot", "tr", "td", or "th".
 * @param Attributes The part's attributes.
 * @return The hover text.
 */
/**
 * @brief The body of a table part's hover text: name, align, anchor.
 *
 * No trailing "Right-click..." line: the single-part tooltip adds
 * it, and a combined gear joins the bodies and adds it once.
 * @param Kind The table part name.
 * @param Attributes The part's attributes.
 * @return The hover body.
 */
static QString TableOptionTooltipBody(
    const QString& Kind,
    const QVector<QPair<QString, QString>>& Attributes)
{
    QString Name;
    if (Kind == QLatin1String("table")) {
        Name = QStringLiteral("Table");
    } else if (Kind == QLatin1String("thead")) {
        Name = QStringLiteral("Table head");
    } else if (Kind == QLatin1String("tbody")) {
        Name = QStringLiteral("Table body");
    } else if (Kind == QLatin1String("tfoot")) {
        Name = QStringLiteral("Table foot");
    } else if (Kind == QLatin1String("tr")) {
        Name = QStringLiteral("Row");
    } else if (Kind == QLatin1String("th")) {
        Name = QStringLiteral("Header cell");
    } else {
        Name = QStringLiteral("Cell");
    }
    QStringList Options;
    QString Align = AttributeOf(Attributes, QStringLiteral("align"));
    if (!Align.isEmpty()) {
        Options.append(QStringLiteral("align \"%1\"").arg(Align));
    }
    QString Text = Name;
    if (!Options.isEmpty()) {
        Text += QStringLiteral(": ") + Options.join(QStringLiteral(", "));
    }
    // Bug 464 v7: the anchor always shows read-only: its
    // value when the part has one, "(none)" when it has none.
    QString Anchor = AttributeOf(Attributes, QStringLiteral("anchor"));
    Text += QStringLiteral("\nAnchor: ") +
            (Anchor.isEmpty() ? QStringLiteral("(none)") : Anchor);
    return Text;
}

static QString TableOptionTooltip(
    const QString& Kind,
    const QVector<QPair<QString, QString>>& Attributes)
{
    return TableOptionTooltipBody(Kind, Attributes) +
           QStringLiteral("\nRight-click to change options.");
}

/**
 * @brief Builds a fresh character format for a table option marker
 *        (Bug 448).
 *
 * The gear shows plain: no underline, no special color, the
 * document's own font. Only the marker property and the hover
 * text are set. The property carries the table part name,
 * "table", "thead", "tbody", "tfoot", "tr", "td", or "th", the
 * way the list marker carries the list kind.
 * @param Kind The table part name.
 * @param Attributes The part's attributes, for the hover text.
 * @return The format.
 */
static QTextCharFormat FormatForTableOptionMarker(
    const QString& Kind,
    const QVector<QPair<QString, QString>>& Attributes)
{
    QTextCharFormat Format;
    Format.setProperty(DocOptionMarkerProperty, Kind);
    Format.setToolTip(TableOptionTooltip(Kind, Attributes));
    return Format;
}

/**
 * @brief True when the character at a position is an option marker.
 * @param Document The document.
 * @param Pos The character position.
 * @return True when the character carries the marker property.
 */
static bool HasOptionMarkerAt(QTextDocument* Document, int Pos)
{
    if (Pos < 0) {
        return false;
    }
    QTextBlock Block = Document->findBlock(Pos);
    if (!Block.isValid()) {
        return false;
    }
    for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
        QTextFragment Fragment = It.fragment();
        if (Pos >= Fragment.position() &&
            Pos < Fragment.position() + Fragment.length()) {
            return Fragment.charFormat()
                .property(DocOptionMarkerProperty)
                .isValid();
        }
    }
    return false;
}

/**
 * @brief Returns the block whose option gear was clicked.
 *
 * The gear is the first character of its element's first
 * block. A click on it can land at the end of the block
 * above (the previous element's last block) at a boundary,
 * so the block at the click and the block after it are both
 * checked for a leading gear. Falls back to the block at
 * the click when neither starts with a gear.
 * @param Document The document.
 * @param Pos The clicked position.
 * @return The gear's block.
 */
static QTextBlock GearBlockAt(QTextDocument* Document, int Pos)
{
    QTextBlock Block = Document->findBlock(Pos);
    if (Block.isValid() &&
        HasOptionMarkerAt(Document, Block.position())) {
        return Block;
    }
    QTextBlock Next = Block.isValid() ? Block.next() : QTextBlock();
    if (Next.isValid() &&
        HasOptionMarkerAt(Document, Next.position())) {
        return Next;
    }
    return Block;
}

/**
 * @brief Returns the table part of the option marker at a position
 *        (Bug 448).
 *
 * The kind comes from the marker property of the fragment at
 * the position: "table", "thead", "tbody", "tfoot", "tr", "td",
 * or "th" for a table gear, empty when the character there is
 * not a table marker.
 * @param Document The document.
 * @param Pos The character position.
 * @return The part name, or empty when no table marker is there.
 */
static QString TableGearKindAt(QTextDocument* Document, int Pos)
{
    if (Pos < 0) {
        return QString();
    }
    QTextBlock Block = Document->findBlock(Pos);
    if (!Block.isValid()) {
        return QString();
    }
    for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
        QTextFragment Fragment = It.fragment();
        if (Pos >= Fragment.position() &&
            Pos < Fragment.position() + Fragment.length()) {
            QString Kind = Fragment.charFormat()
                               .property(DocOptionMarkerProperty)
                               .toString();
            if (Kind == QLatin1String("table") ||
                Kind == QLatin1String("thead") ||
                Kind == QLatin1String("tbody") ||
                Kind == QLatin1String("tfoot") ||
                Kind == QLatin1String("tr") ||
                Kind == QLatin1String("td") ||
                Kind == QLatin1String("th")) {
                return Kind;
            }
            return QString();
        }
    }
    return QString();
}

/**
 * @brief One gear a block should carry (Bug 448).
 *
 * A table cell's first block can carry up to three gears, outer
 * part first: the section gear, then the row gear, then the cell
 * gear. A sourcecode, list, or table caption block carries one.
 */
struct DocWantedGear
{
    QString Kind;
    QVector<QPair<QString, QString>> Attributes;
    // A combined table cell gear carries the hover text for all
    // the parts it covers, instead of the one built from its own
    // kind and attributes. Empty for a one-part gear, which keeps
    // the tooltip its kind and attributes build (Bug 448 v2).
    QString ToolTip;
};

/**
 * @brief Builds the character format for one wanted gear.
 *
 * The kind picks the builder: sourcecode, a list kind, or a
 * table part. The gear shows plain, with the part's hover text.
 * @param Gear The wanted gear.
 * @return The format.
 */
static QTextCharFormat FormatForGear(const DocWantedGear& Gear)
{
    QTextCharFormat Format;
    if (Gear.Kind == QLatin1String("sourcecode")) {
        Format = FormatForOptionMarker(Gear.Attributes);
    } else if (Gear.Kind == QLatin1String("list") ||
               Gear.Kind == QLatin1String("ul") ||
               Gear.Kind == QLatin1String("ol") ||
               Gear.Kind == QLatin1String("dl")) {
        Format = FormatForListOptionMarker(Gear.Kind, Gear.Attributes);
    } else {
        Format = FormatForTableOptionMarker(Gear.Kind, Gear.Attributes);
    }
    // A combined table cell gear hovers all the parts it covers;
    // a one-part gear keeps the tooltip its kind builds (Bug 448 v2).
    if (!Gear.ToolTip.isEmpty()) {
        Format.setToolTip(Gear.ToolTip);
    }
    return Format;
}

/**
 * @brief Returns the row and column of the cell holding a block.
 * @param Document The document.
 * @param Block The block, the first block of a cell.
 * @param Row Receives the grid row.
 * @param Column Receives the grid column.
 * @return False when the block sits in no table cell.
 */
static bool TableCellOfBlock(QTextDocument* Document,
                             const QTextBlock& Block, int& Row,
                             int& Column)
{
    Row = -1;
    Column = -1;
    QTextTable* Frame = TableOfBlock(Document, Block);
    if (Frame == nullptr) {
        return false;
    }
    for (int r = 0; r < Frame->rows(); ++r) {
        for (int c = 0; c < Frame->columns(); ++c) {
            QTextTableCell Cell = Frame->cellAt(r, c);
            if (!Cell.isValid()) {
                continue;
            }
            int First = Cell.firstCursorPosition().block().position();
            int Last = Cell.lastCursorPosition().block().position();
            if (Block.position() >= First && Block.position() <= Last) {
                Row = r;
                Column = c;
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief Returns the first grid row of a section (Bug 448).
 *
 * The first row whose section, from the marks, is the section
 * asked for. A table with no marks reads every row as a body
 * row, so the body's first row is row 0 and the head and foot
 * have none.
 * @param Marks The row marks, in grid order.
 * @param Rows The number of grid rows.
 * @param Section The section: "thead", "tbody", or "tfoot".
 * @return The first row of the section, or -1 when it has none.
 */
static int FirstRowOfSection(const QVector<DocTableRowMark>& Marks,
                             int Rows, const QString& Section)
{
    for (int r = 0; r < Rows; ++r) {
        if (SectionOfRow(Marks, r) == Section) {
            return r;
        }
    }
    return -1;
}

/**
 * @brief Returns the gear a block should carry (Bug 448 v2).
 *
 * A caption block carries the table gear. A cell's first block
 * carries ONE gear, whose menu holds a submenu for every part
 * that applies there: the section when it is the first cell of
 * the first row of its section, the row when it is the first
 * cell of its row, and the cell itself, always. The gear's
 * hover text lists all the parts it covers, outer part first.
 * Any other block carries none. (v1 gave each part its own
 * gear, which put three gears on the header row; v2 combines
 * them into one.)
 * @param Document The document.
 * @param Block The block.
 * @return The wanted gear, empty when the block carries none.
 */
static QVector<DocWantedGear> TableGearsForBlock(
    QTextDocument* Document, const QTextBlock& Block)
{
    QVector<DocWantedGear> Gears;
    if (!IsTableBlock(Block)) {
        return Gears;
    }
    xmlNodePtr Shell = nullptr;
    QVector<DocTableRowMark> Marks;
    if (!TablePayloadOf(Block, Shell, Marks)) {
        return Gears;
    }
    if (IsTableCaptionBlock(Block)) {
        DocWantedGear Gear;
        Gear.Kind = QStringLiteral("table");
        Gear.Attributes = NodeAttributes(Shell);
        Gears.append(Gear);
        xmlFreeNode(Shell);
        return Gears;
    }
    if (!IsTableCellBlock(Block)) {
        xmlFreeNode(Shell);
        return Gears;
    }
    int Row = -1;
    int Column = -1;
    if (!TableCellOfBlock(Document, Block, Row, Column)) {
        xmlFreeNode(Shell);
        return Gears;
    }
    QTextTable* Frame = TableOfBlock(Document, Block);
    int Rows = Frame != nullptr ? Frame->rows() : 0;
    QString Section = SectionOfRow(Marks, Row);
    // The parts this one gear covers, outer part first, each
    // with its own kind and attributes for the hover text.
    struct Part {
        QString Kind;
        QVector<QPair<QString, QString>> Attributes;
    };
    QVector<Part> Parts;
    // The section part sits on the first cell of the first row
    // of the section.
    if (Column == 0 && Row == FirstRowOfSection(Marks, Rows, Section)) {
        Part SectionPart;
        SectionPart.Kind = Section;
        if (Section == QLatin1String("thead")) {
            SectionPart.Attributes = NodeAttributes(
                DocDocument::ChildElement(Shell, "thead"));
        } else if (Section == QLatin1String("tfoot")) {
            SectionPart.Attributes = NodeAttributes(
                DocDocument::ChildElement(Shell, "tfoot"));
        } else {
            SectionPart.Attributes = NodeAttributes(
                DocDocument::ChildElement(Shell, "tbody"));
        }
        Parts.append(SectionPart);
    }
    // The row part sits on the first cell of the row. A row the
    // grid gained since the last load has no mark yet, so it
    // carries an empty attribute list until one is set.
    if (Column == 0 && Row >= 0) {
        Part RowPart;
        RowPart.Kind = QStringLiteral("tr");
        if (Row < Marks.size()) {
            RowPart.Attributes = Marks[Row].Attributes;
            QString Anchor = Marks[Row].Anchor;
            if (!Anchor.isEmpty()) {
                RowPart.Attributes.append(
                    QPair<QString, QString>(QStringLiteral("anchor"),
                                            Anchor));
            }
        }
        Parts.append(RowPart);
    }
    // The cell part sits on every cell's first block.
    xmlNodePtr CellNode = nullptr;
    if (TableCellFromVariant(
            Block.blockFormat().property(DocTableCellProperty),
            CellNode)) {
        Part CellPart;
        CellPart.Kind = DocDocument::NodeNameEquals(CellNode, "th")
                            ? QStringLiteral("th")
                            : QStringLiteral("td");
        CellPart.Attributes = NodeAttributes(CellNode);
        Parts.append(CellPart);
        xmlFreeNode(CellNode);
    }
    xmlFreeNode(Shell);
    if (Parts.isEmpty()) {
        return Gears;
    }
    // One gear for the whole cell: its kind and attributes are
    // the cell's own (the innermost part), and its hover text
    // lists every part it covers, outer part first, with one
    // "Right-click..." line at the end.
    DocWantedGear Gear;
    Gear.Kind = Parts.last().Kind;
    Gear.Attributes = Parts.last().Attributes;
    QStringList Tips;
    for (const Part& P : Parts) {
        Tips.append(TableOptionTooltipBody(P.Kind, P.Attributes));
    }
    Gear.ToolTip = Tips.join(QStringLiteral("\n")) +
                   QStringLiteral("\nRight-click to change options.");
    Gears.append(Gear);
    return Gears;
}

/**
 * @brief True when a cursor's selection covers an option marker.
 * @param Document The document.
 * @param Cursor The cursor or selection.
 * @return True when any covered character is a marker.
 */
static bool SelectionCoversOptionMarker(QTextDocument* Document,
                                        const QTextCursor& Cursor)
{
    if (!Cursor.hasSelection()) {
        return false;
    }
    int Start = Cursor.selectionStart();
    int End = Cursor.selectionEnd();
    QTextBlock Block = Document->findBlock(Start);
    while (Block.isValid() && Block.position() < End) {
        for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
            QTextFragment Fragment = It.fragment();
            int FragStart = Fragment.position();
            int FragEnd = FragStart + Fragment.length();
            if (FragEnd <= Start || FragStart >= End) {
                continue;
            }
            if (Fragment.charFormat()
                    .property(DocOptionMarkerProperty)
                    .isValid()) {
                return true;
            }
        }
        Block = Block.next();
    }
    return false;
}

/**
 * @brief True when the character at a position is src placeholder text.
 * @param Document The document.
 * @param Pos The character position.
 * @return True when the character carries the placeholder property.
 */
static bool HasSrcPlaceholderAt(QTextDocument* Document, int Pos)
{
    if (Pos < 0) {
        return false;
    }
    QTextBlock Block = Document->findBlock(Pos);
    if (!Block.isValid()) {
        return false;
    }
    for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
        QTextFragment Fragment = It.fragment();
        if (Pos >= Fragment.position() &&
            Pos < Fragment.position() + Fragment.length()) {
            return Fragment.charFormat()
                .property(DocSrcPlaceholderProperty)
                .isValid();
        }
    }
    return false;
}

/**
 * @brief True when a cursor's selection covers src placeholder text.
 * @param Document The document.
 * @param Cursor The cursor or selection.
 * @return True when any covered character is placeholder text.
 */
static bool SelectionCoversSrcPlaceholder(QTextDocument* Document,
                                          const QTextCursor& Cursor)
{
    if (!Cursor.hasSelection()) {
        return false;
    }
    int Start = Cursor.selectionStart();
    int End = Cursor.selectionEnd();
    QTextBlock Block = Document->findBlock(Start);
    while (Block.isValid() && Block.position() < End) {
        for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
            QTextFragment Fragment = It.fragment();
            int FragStart = Fragment.position();
            int FragEnd = FragStart + Fragment.length();
            if (FragEnd <= Start || FragStart >= End) {
                continue;
            }
            if (Fragment.charFormat()
                    .property(DocSrcPlaceholderProperty)
                    .isValid()) {
                return true;
            }
        }
        Block = Block.next();
    }
    return false;
}

/**
 * @brief Deletes a selection except for the display marks in it.
 *
 * The option markers and the src placeholder text cannot be
 * deleted, so the deletion runs in segments between them, last
 * first so the positions stay valid. The cursor collapses to
 * the selection start.
 * @param Cursor The cursor, holding the selection.
 */
static void RemoveSelectionKeepingDisplayMarks(QTextCursor& Cursor)
{
    QTextDocument* Document = Cursor.document();
    int Start = Cursor.selectionStart();
    int End = Cursor.selectionEnd();
    QVector<int> Kept;
    QTextBlock Block = Document->findBlock(Start);
    while (Block.isValid() && Block.position() < End) {
        for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
            QTextFragment Fragment = It.fragment();
            QString Text = Fragment.text();
            if (Fragment.charFormat()
                    .property(DocOptionMarkerProperty)
                    .isValid()) {
                for (int i = 0; i < Text.size(); ++i) {
                    int Pos = Fragment.position() + i;
                    if (Text.at(i) == OptionGlyph && Pos >= Start &&
                        Pos < End) {
                        Kept.append(Pos);
                    }
                }
            }
            if (Fragment.charFormat()
                    .property(DocSrcPlaceholderProperty)
                    .isValid()) {
                for (int i = 0; i < Text.size(); ++i) {
                    int Pos = Fragment.position() + i;
                    if (Pos >= Start && Pos < End) {
                        Kept.append(Pos);
                    }
                }
            }
        }
        Block = Block.next();
    }
    Cursor.beginEditBlock();
    int SegmentEnd = End;
    for (int i = Kept.size() - 1; i >= 0; --i) {
        if (Kept[i] + 1 < SegmentEnd) {
            QTextCursor Delete(Document);
            Delete.setPosition(Kept[i] + 1);
            Delete.setPosition(SegmentEnd, QTextCursor::KeepAnchor);
            Delete.removeSelectedText();
        }
        SegmentEnd = Kept[i];
    }
    if (Start < SegmentEnd) {
        QTextCursor Delete(Document);
        Delete.setPosition(Start);
        Delete.setPosition(SegmentEnd, QTextCursor::KeepAnchor);
        Delete.removeSelectedText();
    }
    Cursor.endEditBlock();
    Cursor.setPosition(Start);
}

/**
 * @brief True when a block shows the src placeholder: the first
 *        block of a src sourcecode element holding no real
 *        content (Bug 466).
 *
 * SourceElementHasContent is defined with the menu code below,
 * and IsFirstSourceBlock just below this check; both are
 * declared here for this check.
 * @param Block The block.
 * @return True when the block shows the placeholder.
 */
static bool SourceElementHasContent(const QTextDocument* Document,
                                    int FirstBlockPos);
static bool IsFirstSourceBlock(const QTextBlock& Block);
static bool IsSrcPlaceholderBlock(const QTextBlock& Block)
{
    if (!IsFirstSourceBlock(Block)) {
        return false;
    }
    QVector<QPair<QString, QString>> Attributes = AttributesFromVariant(
        Block.blockFormat().property(DocSourceProperty));
    if (AttributeOf(Attributes, QStringLiteral("src")).isEmpty()) {
        return false;
    }
    return !SourceElementHasContent(Block.document(), Block.position());
}

/**
 * @brief True when a block is the first block of its sourcecode element.
 *
 * A block is first when it carries the source kind and the
 * block above it is no sourcecode block with the same
 * attributes: consecutive blocks with the same attributes are
 * one element, and only the first carries the marker.
 * @param Block The block.
 * @return True when the block starts its element.
 */
static bool IsFirstSourceBlock(const QTextBlock& Block)
{
    if (!IsSourceCodeBlock(Block)) {
        return false;
    }
    QTextBlock Previous = Block.previous();
    return !Previous.isValid() || !IsSourceCodeBlock(Previous) ||
           Previous.blockFormat().property(DocSourceProperty) !=
               Block.blockFormat().property(DocSourceProperty);
}

/**
 * @brief The first block of the sourcecode element a block belongs to.
 *
 * Walks up while the block above is a sourcecode block with the
 * same attributes: consecutive blocks with the same attributes
 * are one element.
 * @param Block A block of the element.
 * @return The element's first block.
 */
static QTextBlock FirstBlockOfSourceElement(const QTextBlock& Block)
{
    QTextBlock First = Block;
    while (First.previous().isValid() &&
           IsSourceCodeBlock(First.previous()) &&
           First.previous().blockFormat().property(DocSourceProperty) ==
               First.blockFormat().property(DocSourceProperty)) {
        First = First.previous();
    }
    return First;
}

/**
 * @brief True when a block belongs to a list (Bug 464).
 *
 * A block belongs when it carries a list kind and either sits
 * in its list object, is a definition list (which has no list
 * object: the kind alone claims it), or is a continuation of
 * the item above it. A block that left its list keeps a stale
 * kind but no list object and no continuation role, so it reads
 * back as a plain paragraph and carries no gear.
 * @param Block The block.
 * @return True when the block is part of a list.
 */
static bool IsListBlock(const QTextBlock& Block)
{
    QString Kind =
        Block.blockFormat().property(DocListKindProperty).toString();
    if (Kind.isEmpty()) {
        return false;
    }
    if (Kind == QLatin1String("dl")) {
        return true;
    }
    if (Block.textList() != nullptr) {
        return true;
    }
    return ListRoleOf(Block) == QLatin1String("continuation");
}

/**
 * @brief True when a block is a blank plain block (Bug 464).
 *
 * Such a block is the blank line typed between list items: it
 * keeps no list properties, belongs to no list itself, but does
 * not split the list around it. The first-block walk steps
 * over it. A blank list item is not one: it sits in its list
 * object, or carries the list kind, so it is a list block.
 * @param Block The block.
 * @return True when the block is a blank line between items.
 */
static bool IsBlankPlainBlock(const QTextBlock& Block)
{
    if (!Block.isValid() || !Block.text().trimmed().isEmpty()) {
        return false;
    }
    QTextBlockFormat Format = Block.blockFormat();
    if (IsSourceCodeBlock(Block) ||
        Format.property(DocListKindProperty).isValid() ||
        Format.property(DocFigureKindProperty).isValid() ||
        Format.property(DocTableKindProperty).isValid() ||
        Block.textList() != nullptr ||
        ListRoleOf(Block) == QLatin1String("continuation")) {
        return false;
    }
    return true;
}

/**
 * @brief True when two blocks belong to the same list (Bug 464).
 *
 * Lists group by kind, group number, and attributes, the same
 * grouping the read-back uses: consecutive blocks sharing all
 * three are one list node.
 * @param A The first block.
 * @param B The second block.
 * @return True when both are blocks of one list.
 */
static bool SameListAs(const QTextBlock& A, const QTextBlock& B)
{
    return A.blockFormat().property(DocListKindProperty) ==
               B.blockFormat().property(DocListKindProperty) &&
           A.blockFormat().property(DocListGroupProperty) ==
               B.blockFormat().property(DocListGroupProperty) &&
           A.blockFormat().property(DocListProperty) ==
               B.blockFormat().property(DocListProperty);
}

/**
 * @brief True when a block is the first block of its list (Bug 464).
 *
 * A block is first when it belongs to a list and the block
 * above it -- stepping over blank lines between items, which
 * do not split the list -- is no block of the same list.
 * @param Block The block.
 * @return True when the block starts its list.
 */
static bool IsFirstListBlock(const QTextBlock& Block)
{
    if (!IsListBlock(Block)) {
        return false;
    }
    QTextBlock Previous = Block.previous();
    while (Previous.isValid() && IsBlankPlainBlock(Previous)) {
        Previous = Previous.previous();
    }
    if (!Previous.isValid() || !IsListBlock(Previous)) {
        return true;
    }
    return !SameListAs(Previous, Block);
}

/**
 * @brief The first block of the list a block belongs to (Bug 464).
 *
 * Walks up while the block above -- stepping over blank lines
 * between items -- is a block of the same list.
 * @param Block A block of the list.
 * @return The list's first block.
 */
static QTextBlock FirstListBlock(const QTextBlock& Block)
{
    QTextBlock First = Block;
    while (true) {
        QTextBlock Previous = First.previous();
        while (Previous.isValid() && IsBlankPlainBlock(Previous)) {
            Previous = Previous.previous();
        }
        if (!Previous.isValid() || !IsListBlock(Previous) ||
            !SameListAs(Previous, First)) {
            break;
        }
        First = Previous;
    }
    return First;
}

/**
 * @brief Schedules the empty-sourcecode cleanup after a deletion key.
 *
 * The key has not landed yet when the filter runs, so the check
 * cannot run now: it is queued for the next idle moment, after
 * the edit has applied the deletion and before the next key.
 * HadContent records whether the cursor's element held real
 * content before the deletion, so a fresh empty block -- the
 * Blocks button, which never had any -- is never removed.
 * @param Editor The editor owning the edit.
 * @param Cursor The cursor before the deletion.
 */
static void QueueEmptySourceCleanup(TextEditor* Editor,
                                    const QTextCursor& Cursor)
{
    bool HadContent = false;
    if (IsSourceCodeBlock(Cursor.block())) {
        QTextBlock First = FirstBlockOfSourceElement(Cursor.block());
        HadContent = SourceElementHasContent(Cursor.document(),
                                            First.position());
    }
    QTimer::singleShot(0, Editor, [Editor, HadContent]() {
        Editor->RemoveEmptySourceBlocks(HadContent);
        Editor->EnsureTrailingPlainParagraph();
    });
}

/**
 * @brief Writes an attribute list as clipboard data.
 * @param Attributes The attributes.
 * @return The data: a count followed by name/value pairs.
 */
static QByteArray SourceAttributesToData(
    const QVector<QPair<QString, QString>>& Attributes)
{
    QByteArray Data;
    QDataStream Stream(&Data, QIODevice::WriteOnly);
    Stream << static_cast<qint32>(Attributes.size());
    for (const QPair<QString, QString>& Attribute : Attributes) {
        Stream << Attribute.first << Attribute.second;
    }
    return Data;
}

/**
 * @brief Reads an attribute list back from clipboard data.
 * @param Data The data.
 * @return The attributes, empty when the data is not well formed.
 */
static QVector<QPair<QString, QString>> SourceAttributesFromData(
    const QByteArray& Data)
{
    QVector<QPair<QString, QString>> Attributes;
    QDataStream Stream(Data);
    qint32 Count = 0;
    Stream >> Count;
    if (Count < 0 || Count > 1000) {
        return QVector<QPair<QString, QString>>();
    }
    for (qint32 i = 0; i < Count; ++i) {
        QString Name;
        QString Value;
        Stream >> Name >> Value;
        Attributes.append(QPair<QString, QString>(Name, Value));
    }
    if (Stream.status() != QDataStream::Ok) {
        return QVector<QPair<QString, QString>>();
    }
    return Attributes;
}

/**
 * @brief Refreshes the option markers of the editor owning an edit.
 * @param Edit The edit, whose parent is its editor.
 */
static void RefreshParentOptionMarkers(QTextEdit* Edit)
{
    if (auto* Editor = qobject_cast<TextEditor*>(Edit->parent())) {
        Editor->UpdateOptionMarkers();
    }
}

/**
 * @brief Takes the cursor's block out of any list before it is re-claimed.
 *
 * A list or sourcecode unit merging into a list item must not
 * leave the block in the old list: the block is detached first,
 * so the new claim cannot make a hybrid.
 * @param Cursor The cursor in the block.
 */
static void DetachPasteMergeBlock(QTextCursor& Cursor)
{
    QTextBlock Block = Cursor.block();
    if (Block.textList() != nullptr ||
        !Block.blockFormat().property(DocListKindProperty).toString().isEmpty()) {
        DetachBlockFromList(Cursor.document(), Block);
    }
}

/**
 * @brief Inserts one pasted list at the cursor.
 *
 * The items go in through the same machinery a loaded list
 * uses, so the pasted list behaves like a typed one. The list
 * carries no attributes: numbering styles and start values from
 * the source are dropped.
 * @param Cursor The cursor to insert at.
 * @param First True until the first unit is inserted; cleared here.
 * @param Kind The list kind: "ul" or "ol".
 * @param Items The list items, one paragraph each.
 */
static void EmitPasteList(QTextCursor& Cursor, bool& First,
                          const QString& Kind,
                          const QVector<xmlNodePtr>& ItemNodes)
{
    if (First) {
        DetachPasteMergeBlock(Cursor);
    } else {
        InsertParagraphBreak(Cursor);
    }
    First = false;
    QByteArray KindBytes = Kind.toUtf8();
    xmlNodePtr ListNode = NewNode(KindBytes.constData());
    for (xmlNodePtr ItemNode : ItemNodes) {
        xmlNodePtr Item = AddElementChild(ListNode, "li");
        xmlAddChild(Item, ItemNode);
    }
    InsertListNode(Cursor, ListNode);
    xmlFreeNode(ListNode);
}

/**
 * @brief Inserts an empty plain paragraph after the cursor's block.
 *
 * The cursor must sit at the end of its block. The new paragraph
 * carries no source, list, figure, or table claim, and its typing
 * format is plain, so text typed in it is ordinary text. It is the
 * paragraph a writer types in after a sourcecode block.
 * @param Cursor The cursor, at the end of its block.
 */
static void InsertPlainParagraphAfter(QTextCursor& Cursor)
{
    Cursor.movePosition(QTextCursor::EndOfBlock);
    Cursor.insertBlock();
    DetachBlockFromList(Cursor.document(), Cursor.block());
    Cursor.setCharFormat(QTextCharFormat());
}

/**
 * @brief Moves the cursor out of a pasted sourcecode block.
 *
 * A paste leaves the cursor at the end of the last line of the
 * pasted code, and Enter inside a code block never leaves it, so
 * the writer has no way to type after the paste point. The cursor
 * goes to the paragraph after the pasted block; when there is no
 * paragraph after it -- the block ends the document, or the next
 * block is another sourcecode block -- an empty plain paragraph
 * is made there instead. The new paragraph carries no source or
 * list claim, so typing in it is ordinary text. Nothing happens
 * when the cursor is not in a sourcecode block.
 * @param Cursor The cursor, in the pasted block.
 */
static void MoveCursorOutOfPastedSource(QTextCursor& Cursor)
{
    if (!IsSourceCodeBlock(Cursor.block())) {
        return;
    }
    QTextBlock Next = Cursor.block().next();
    if (Next.isValid() && !IsSourceCodeBlock(Next)) {
        Cursor.setPosition(Next.position());
        return;
    }
    InsertPlainParagraphAfter(Cursor);
}

/**
 * @brief Inserts one pasted preformatted block as sourcecode.
 * @param Cursor The cursor to insert at.
 * @param First True until the first unit is inserted; cleared here.
 * @param Text The code text, lines joined with newlines.
 */
static void EmitPasteSource(QTextCursor& Cursor, bool& First,
                            const QString& Text)
{
    if (First) {
        DetachPasteMergeBlock(Cursor);
    } else {
        InsertParagraphBreak(Cursor);
    }
    First = false;
    xmlNodePtr SourceNode = NewNode("sourcecode");
    AddTextChild(SourceNode, Text);
    InsertSourceNode(Cursor, SourceNode);
    xmlFreeNode(SourceNode);
}

/**
 * @brief Inserts pasted HTML, translated into the tags the editor models.
 *
 * The HTML is parsed into a scratch document and walked block
 * by block. Each block becomes one paragraph, a list item, or
 * part of one sourcecode paragraph: blocks in a list group into
 * one list (a nested list flattens into the outer list's kind,
 * and a plain block between two lists keeps them apart),
 * consecutive preformatted blocks join into one sourcecode
 * paragraph, headings paste as plain paragraphs with their bold
 * dropped, table cells and definition terms paste as plain
 * paragraphs in document order, and blocks holding nothing are
 * skipped. The whole paste is one edit block in the caller, so
 * it undoes in one step; any selection is replaced first.
 * @param Cursor The cursor to insert at, inside an edit block.
 * @param Html The pasted HTML.
 */
static void InsertHtmlPaste(QTextCursor& Cursor, const QString& Html)
{
    if (Cursor.hasSelection()) {
        Cursor.removeSelectedText();
    }
    QTextDocument Scratch;
    Scratch.setHtml(Html);
    bool First = true;
    QVector<const QTextList*> OpenLists;
    QVector<xmlNodePtr> PendingItems;
    QString PendingKind;
    QStringList PendingPreLines;
    auto FlushList = [&]() {
        if (!PendingItems.isEmpty()) {
            EmitPasteList(Cursor, First, PendingKind, PendingItems);
        }
        PendingItems.clear();
        PendingKind.clear();
    };
    auto FlushPre = [&]() {
        if (!PendingPreLines.isEmpty()) {
            QString Text = PendingPreLines.join(QLatin1Char('\n'));
            if (!Text.trimmed().isEmpty()) {
                EmitPasteSource(Cursor, First, Text);
            }
        }
        PendingPreLines.clear();
    };
    for (QTextBlock Block = Scratch.begin(); Block.isValid();
         Block = Block.next()) {
        QTextBlockFormat Format = Block.blockFormat();
        if (Format.nonBreakableLines()) {
            // A preformatted block: literal lines. It ends any
            // open list, and consecutive pre blocks join into
            // one sourcecode paragraph. Tabs expand to spaces,
            // since tabs are prohibited inside <sourcecode>.
            FlushList();
            OpenLists.clear();
            QString Text = Block.text();
            Text.replace(SoftBreakChar, QLatin1Char('\n'));
            Text.remove(QChar(0xFFFC));
            Text.replace(QLatin1Char('\t'), QStringLiteral("    "));
            for (const QString& Line : Text.split(QLatin1Char('\n'))) {
                PendingPreLines.append(Line);
            }
            continue;
        }
        FlushPre();
        QTextList* List = Block.textList();
        if (List != nullptr) {
            if (OpenLists.contains(List)) {
                while (!OpenLists.isEmpty() && OpenLists.last() != List) {
                    OpenLists.removeLast();
                }
            } else if (!OpenLists.isEmpty()) {
                // A new list nested inside the open one: its
                // items flatten into the outer list's kind.
            } else {
                OpenLists.append(List);
                PendingKind = PasteListKind(List);
            }
            QVector<DocPastePiece> Pieces = PiecesForPasteBlock(
                Block, Format.headingLevel() > 0);
            if (PastePiecesHaveContent(Pieces)) {
                xmlNodePtr ItemNode = NewNode("t");
                for (xmlNodePtr Run : RunsForPastePieces(Pieces)) {
                    xmlAddChild(ItemNode, Run);
                }
                PendingItems.append(ItemNode);
            }
            continue;
        }
        FlushList();
        OpenLists.clear();
        QVector<DocPastePiece> Pieces = PiecesForPasteBlock(
            Block, Format.headingLevel() > 0);
        if (PastePiecesHaveContent(Pieces)) {
            EmitPastePlain(Cursor, First, RunsForPastePieces(Pieces));
        }
    }
    FlushList();
    FlushPre();
}

void DocTextEdit::insertFromMimeData(const QMimeData* Source)
{
    if (IsPlaceholderBlock(textCursor().block())) {
        // Pasting into an SVG placeholder would change text
        // that is not editable: the paste is ignored.
        return;
    }
    if (textCursor().hasSelection() &&
        SelectionPartlyCrossesTable(document(), textCursor())) {
        // A paste replacing a selection that partly crosses a
        // table boundary would delete the whole table: the
        // paste is ignored, as a crossing keypress is.
        return;
    }
    if (textCursor().hasSelection() &&
        (SelectionCoversOptionMarker(document(), textCursor()) ||
         SelectionCoversSrcPlaceholder(document(), textCursor()))) {
        // A paste replaces the selection, but the marks in
        // it cannot be deleted: take the text, keep the
        // marks, and paste at the collapsed cursor.
        QTextCursor Cursor = textCursor();
        RemoveSelectionKeepingDisplayMarks(Cursor);
        setTextCursor(Cursor);
    }
    if (Source->hasFormat(SourceOptionsMimeType) &&
        !IsSourceCodeBlock(textCursor().block()) &&
        !IsFigureArtworkBlock(textCursor().block()) &&
        !IsFigureCaptionBlock(textCursor().block()) &&
        !IsTableCaptionBlock(textCursor().block()) &&
        textCursor().currentTable() == nullptr) {
        // A copied sourcecode block carries its options: the
        // paste lands as a new sourcecode block with the same
        // options (Bug 420). The settings branch wins over the
        // rich-text branch below, so the block stays a block.
        // The anchor never travels: the copy path drops it,
        // since an anchor must stay unique. Src travels only
        // when the copied block held no content (Bug 469);
        // pasted text is content, which src forbids.
        QVector<QPair<QString, QString>> Attributes =
            SourceAttributesFromData(
                Source->data(SourceOptionsMimeType));
        QString Text = Source->text();
        Text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        Text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        Text.replace(QLatin1Char('\t'), QStringLiteral("    "));
        if (Text.endsWith(QLatin1Char('\n'))) {
            // A whole-block copy ends at the next block's
            // start, which adds one trailing line ending that
            // is not a line of the block.
            Text.chop(1);
        }
        QTextCursor Cursor = textCursor();
        Cursor.beginEditBlock();
        DetachPasteMergeBlock(Cursor);
        Cursor.removeSelectedText();
        // The paste claims the cursor's block as the new
        // sourcecode block: when that block still holds real
        // text, the paste would swallow it and the gear would
        // land on it (Bug 469). Give the paste a fresh empty
        // block after the paragraph instead, and take it out of
        // any list the split put it in; an empty paragraph is
        // used as it stands.
        if (!CodeTextOf(Cursor.block()).trimmed().isEmpty()) {
            Cursor.movePosition(QTextCursor::EndOfBlock);
            Cursor.insertBlock();
            DetachPasteMergeBlock(Cursor);
        }
        // The pasted block must stand as its own element: when
        // the block before the paste point is a sourcecode block
        // with the same attributes, the paste would merge into
        // it as an invisible continuation, with no gear and no
        // placeholder (Bug 469). The cursor sits at the start
        // of the empty paste block, so splitting here leaves
        // that empty block between the two elements as their
        // separator, and the paste lands after it. The separator
        // is a blank paragraph, kept on save like any other.
        QTextBlock Previous = Cursor.block().previous();
        if (Previous.isValid() && IsSourceCodeBlock(Previous) &&
            Previous.blockFormat().property(DocSourceProperty) ==
                AttributesToVariant(Attributes)) {
            Cursor.insertBlock();
        }
        xmlNodePtr SourceNode = NewNode("sourcecode");
        SetNodeAttributes(SourceNode, Attributes);
        AddTextChild(SourceNode, Text);
        InsertSourceNode(Cursor, SourceNode);
        xmlFreeNode(SourceNode);
        MoveCursorOutOfPastedSource(Cursor);
        Cursor.endEditBlock();
        setTextCursor(Cursor);
        RefreshParentOptionMarkers(this);
        return;
    }
    if (Source->hasFormat(ListRunMimeType) &&
        !IsSourceCodeBlock(textCursor().block()) &&
        !IsFigureArtworkBlock(textCursor().block()) &&
        !IsFigureCaptionBlock(textCursor().block()) &&
        !IsTableCaptionBlock(textCursor().block()) &&
        textCursor().currentTable() == nullptr) {
        // A copied list carries its structure here (Bug 478):
        // the mime data holds the list element itself, so the
        // paste inserts it through InsertListNode and the
        // term/definition roles survive intact. Paste never
        // splits: the split happened at copy time, on the node
        // being built.
        xmlNodePtr ListNode = nullptr;
        if (FragmentNodeFromXml(
                QString::fromUtf8(Source->data(ListRunMimeType)),
                ListNode) &&
            (DocDocument::NodeNameEquals(ListNode, "dl") ||
             DocDocument::NodeNameEquals(ListNode, "ul") ||
             DocDocument::NodeNameEquals(ListNode, "ol"))) {
            QTextCursor Cursor = textCursor();
            Cursor.beginEditBlock();
            // No DetachPasteMergeBlock here (Bug 478): detaching
            // the block at the cursor clears its list properties,
            // so pasting after a definition ejects that definition
            // as a plain paragraph and splits the list in two.
            // The paste already starts in a fresh block below
            // (insertBlock when the cursor block is non-empty),
            // and the marking below gives the pasted blocks their
            // list properties and group, so nothing needs detaching.
            if (!Cursor.block().text().trimmed().isEmpty()) {
                Cursor.movePosition(QTextCursor::EndOfBlock);
                Cursor.insertBlock();
            }
            QString Kind = QString::fromUtf8(
                reinterpret_cast<const char*>(ListNode->name));
            QTextBlock StartBlock = Cursor.block();
            int StartPos = Cursor.position();
            InsertListNode(Cursor, ListNode);
            int EndPos = Cursor.position();
            // A paste next to a list of the same kind joins
            // that list, so a moved entry lands back in the
            // same list instead of starting a second one; only
            // a paste with no list beside it starts a new list.
            // The join re-stamps the pasted blocks' group from
            // the neighbouring list block, and, for a bulleted
            // or numbered list, moves the pasted head blocks
            // into the neighbour's list object, so numbering
            // continues.
            QTextDocument* PasteDocument = Cursor.document();
            QTextBlock Before = StartBlock.previous();
            QTextBlock After =
                PasteDocument->findBlock(EndPos).next();
            int JoinGroup = -1;
            QTextList* JoinList = nullptr;
            if (Before.isValid() &&
                Before.blockFormat().property(DocListKindProperty)
                        .toString() == Kind) {
                JoinGroup = Before.blockFormat()
                                .property(DocListGroupProperty).toInt();
                JoinList = Before.textList();
            } else if (After.isValid() &&
                       After.blockFormat().property(DocListKindProperty)
                               .toString() == Kind) {
                JoinGroup = After.blockFormat()
                                .property(DocListGroupProperty).toInt();
                JoinList = After.textList();
            }
            if (JoinGroup >= 0) {
                for (QTextBlock Pasted = PasteDocument->findBlock(StartPos);
                     Pasted.isValid() && Pasted.position() <= EndPos;
                     Pasted = Pasted.next()) {
                    if (Pasted.blockFormat().property(DocListKindProperty)
                                .toString() != Kind) {
                        continue;
                    }
                    QTextCursor At(PasteDocument);
                    At.setPosition(Pasted.position());
                    QTextBlockFormat Format = At.blockFormat();
                    Format.setProperty(DocListGroupProperty, JoinGroup);
                    At.setBlockFormat(Format);
                    if (JoinList != nullptr &&
                        ListRoleOf(Pasted) !=
                            QLatin1String("continuation") &&
                        ListRoleOf(Pasted) !=
                            QLatin1String("definition")) {
                        JoinList->add(Pasted);
                    }
                }
                if (Kind == QLatin1String("dl")) {
                    QTextBlock First = PasteDocument->findBlock(StartPos);
                    while (First.isValid() && First.position() <= EndPos &&
                           First.blockFormat().property(DocListKindProperty)
                                   .toString() != Kind) {
                        First = First.next();
                    }
                    if (First.isValid()) {
                        StyleDefinitionGroup(PasteDocument, First);
                    }
                }
            }
            xmlFreeNode(ListNode);
            MoveCursorOutOfPastedSource(Cursor);
            Cursor.endEditBlock();
            setTextCursor(Cursor);
            RefreshParentOptionMarkers(this);
            return;
        }
        xmlFreeNode(ListNode);
    }
    if (Source->hasHtml() && !Source->html().isEmpty() &&
        !IsSourceCodeBlock(textCursor().block()) &&
        !IsFigureArtworkBlock(textCursor().block()) &&
        !IsFigureCaptionBlock(textCursor().block()) &&
        !IsTableCaptionBlock(textCursor().block()) &&
        textCursor().currentTable() == nullptr) {
        // Rich-text paste in a plain context: translate the HTML
        // into the tags the editor models, dropping the rest.
        QTextCursor Cursor = textCursor();
        Cursor.beginEditBlock();
        InsertHtmlPaste(Cursor, Source->html());
        MoveCursorOutOfPastedSource(Cursor);
        Cursor.endEditBlock();
        setTextCursor(Cursor);
        return;
    }
    if (Source->hasText()) {
        // Plain text, or the plain-text part of a rich paste
        // landing in code, artwork, a caption, or a table cell,
        // where the HTML branch above declined it and tags
        // cannot go.
        if (IsSourceCodeBlock(textCursor().block()) ||
            IsFigureArtworkBlock(textCursor().block())) {
            // Pasting into a code or artwork block keeps the
            // text literal: every line ending is a line of code
            // or artwork, blank lines included, and leading
            // spaces stay. Tabs expand to spaces, because tabs
            // are prohibited inside <sourcecode> and <artwork>.
            QString Text = Source->text();
            Text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
            Text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
            Text.replace(QLatin1Char('\t'), QStringLiteral("    "));
            QStringList Lines = Text.split(QLatin1Char('\n'));
            QTextCursor Cursor = textCursor();
            Cursor.beginEditBlock();
            Cursor.setCharFormat(CodeCharFormat());
            bool First = true;
            for (const QString& Line : Lines) {
                if (!First) {
                    Cursor.insertBlock();
                    Cursor.setCharFormat(CodeCharFormat());
                }
                First = false;
                Cursor.insertText(Line);
            }
            Cursor.endEditBlock();
            setTextCursor(Cursor);
            return;
        }
        if (IsFigureCaptionBlock(textCursor().block()) ||
            IsTableCaptionBlock(textCursor().block()) ||
            textCursor().currentTable() != nullptr) {
            // Pasting into a caption or a cell keeps one line:
            // line endings become spaces, so the paste cannot
            // split the caption block, the figure region, or
            // the cell block.
            QString Text = Source->text();
            Text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
            Text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
            Text.replace(QLatin1Char('\n'), QLatin1Char(' '));
            QTextCursor Cursor = textCursor();
            Cursor.beginEditBlock();
            Cursor.insertText(Text);
            Cursor.endEditBlock();
            setTextCursor(Cursor);
            return;
        }
        // Plain-text paste: a new paragraph starts at a blank
        // line (two or more line endings); wrapped lines join
        // with a space. Leading and trailing blank lines, and
        // extra blank lines between paragraphs, add nothing.
        QString Text = Source->text();
        Text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        Text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        QStringList Paragraphs;
        QString Current;
        const QStringList Lines = Text.split(QLatin1Char('\n'));
        for (const QString& Line : Lines) {
            QString Trimmed = Line.trimmed();
            if (Trimmed.isEmpty()) {
                if (!Current.isEmpty()) {
                    Paragraphs.append(Current);
                    Current.clear();
                }
            } else if (Current.isEmpty()) {
                Current = Trimmed;
            } else {
                Current += QLatin1Char(' ') + Trimmed;
            }
        }
        if (!Current.isEmpty()) {
            Paragraphs.append(Current);
        }
        if (!Paragraphs.isEmpty()) {
            QTextCursor Cursor = textCursor();
            Cursor.beginEditBlock();
            Cursor.setCharFormat(QTextCharFormat());
            bool First = true;
            for (const QString& Paragraph : Paragraphs) {
                if (!First) {
                    InsertParagraphBreak(Cursor);
                }
                First = false;
                Cursor.insertText(Paragraph);
            }
            Cursor.endEditBlock();
            setTextCursor(Cursor);
        }
        return;
    }
    QTextEdit::insertFromMimeData(Source);
}

QMimeData* DocTextEdit::createMimeDataFromSelection() const
{
    QMimeData* Data = QTextEdit::createMimeDataFromSelection();
    if (Data == nullptr) {
        return nullptr;
    }
    // Option markers are display only: they never leave the
    // editor in the copied text (Bug 420).
    if (Data->hasText()) {
        QString Text = Data->text();
        if (Text.contains(OptionGlyph)) {
            Text.remove(OptionGlyph);
            Data->setText(Text);
        }
    }
    if (Data->hasHtml()) {
        QString Html = Data->html();
        if (Html.contains(OptionGlyph)) {
            Html.remove(OptionGlyph);
            Data->setHtml(Html);
        }
    }
    // A src placeholder is display only too: it never leaves
    // the editor in the copied text (Bug 466). The src values
    // present in the selection are collected from the fragments
    // that carry them, so text the user typed that only looks
    // like a placeholder stays in the copy. A fragment counts
    // when its text is the placeholder for its stored src, or
    // when its text starts with "<Src=": text merged into the
    // display-only placeholder region (a paste inside it, or
    // typing under an older build) dirties the fragment, and
    // the whole fragment text is display text, not content
    // (Bug 469). The pair keeps the fragment's own text, so
    // the dirtied text is stripped instead of the clean
    // placeholder, which the dirtied text no longer contains.
    QVector<QPair<QString, QString>> SrcDisplays;
    if (Data->hasText() || Data->hasHtml()) {
        QTextCursor Cursor = textCursor();
        if (Cursor.hasSelection()) {
            QTextDocument* Document = document();
            int Start = Cursor.selectionStart();
            int End = Cursor.selectionEnd();
            QTextBlock Block = Document->findBlock(Start);
            while (Block.isValid() && Block.position() < End) {
                for (QTextBlock::iterator It = Block.begin();
                     !It.atEnd(); ++It) {
                    QTextFragment Fragment = It.fragment();
                    QVariant Value = Fragment.charFormat().property(
                        DocSrcPlaceholderProperty);
                    if (!Value.isValid()) {
                        continue;
                    }
                    QString FragmentText = Fragment.text();
                    if (!FragmentText.startsWith(
                            QStringLiteral("<Src="))) {
                        continue;
                    }
                    QPair<QString, QString> Display(
                        Value.toString(), FragmentText);
                    if (!SrcDisplays.contains(Display)) {
                        SrcDisplays.append(Display);
                    }
                }
                Block = Block.next();
            }
        }
        if (Data->hasText()) {
            QString Text = Data->text();
            for (const QPair<QString, QString>& Display : SrcDisplays) {
                Text.remove(Display.second);
            }
            Data->setText(Text);
        }
        if (Data->hasHtml()) {
            QString Html = Data->html();
            for (const QPair<QString, QString>& Display : SrcDisplays) {
                Html.remove(EscapeHtmlText(Display.second));
            }
            Data->setHtml(Html);
        }
    }
    // A selection lying wholly in one sourcecode element
    // carries that element's options, so pasting it makes a
    // block with the same options. The anchor and prep-tool
    // attributes never travel: an anchor must stay unique.
    // Src travels only when the copied text is empty after
    // the marks are stripped above (Bug 469): a src-only
    // block copies as empty text, so nothing conflicts with
    // it; copied text is content, which src forbids.
    bool CopiedTextEmpty =
        !Data->hasText() || Data->text().trimmed().isEmpty();
    QTextCursor Cursor = textCursor();
    if (Cursor.hasSelection()) {
        QTextDocument* Document = document();
        int Start = Cursor.selectionStart();
        int End = Cursor.selectionEnd();
        QTextBlock First = Document->findBlock(Start);
        // A blank line at the edge of the selection is not part
        // of any element: skip empty blocks when finding the
        // element, so a selection reaching into the blank line
        // after a sourcecode block still carries that block's
        // options (Bug 469). The blank lines themselves stay in
        // the document and in the copied text; only the carry
        // decision ignores them.
        while (First.isValid() && First.position() < End &&
               !IsSourceCodeBlock(First) &&
               SelectedCodeTextOf(First, Start, End).trimmed().isEmpty()) {
            First = First.next();
        }
        bool OneElement = First.isValid() && First.position() < End &&
            IsSourceCodeBlock(First);
        QVariant Shared = OneElement
            ? First.blockFormat().property(DocSourceProperty)
            : QVariant();
        QTextBlock Block = First;
        while (OneElement && Block.isValid() &&
               Block.position() < End) {
            if (!IsSourceCodeBlock(Block) &&
                SelectedCodeTextOf(Block, Start, End).trimmed().isEmpty()) {
                // A blank line inside the selection is not part
                // of any element: it does not break the carry.
                Block = Block.next();
                continue;
            }
            OneElement = IsSourceCodeBlock(Block) &&
                Block.blockFormat().property(DocSourceProperty) == Shared;
            Block = Block.next();
        }
        if (OneElement) {
            QVector<QPair<QString, QString>> Attributes =
                AttributesFromVariant(Shared);
            QVector<QPair<QString, QString>> Carried;
            for (const QPair<QString, QString>& Attribute : Attributes) {
                if (Attribute.first == QLatin1String("anchor") ||
                    Attribute.first == QLatin1String("pn") ||
                    Attribute.first == QLatin1String("originalSrc") ||
                    Attribute.first == QLatin1String("xml:base") ||
                    Attribute.first == QLatin1String("xml:lang")) {
                    continue;
                }
                if (Attribute.first == QLatin1String("src") &&
                    !CopiedTextEmpty) {
                    continue;
                }
                Carried.append(Attribute);
            }
            Data->setData(SourceOptionsMimeType,
                          SourceAttributesToData(Carried));
        }
    }
    // A selection lying wholly in one list carries that list's
    // structure, so pasting it makes a list again instead of
    // plain paragraphs (Bug 478). The clipboard holds the list
    // element itself, built from the selected blocks: one
    // <li> per item, or one <dt> plus its <dd> per definition
    // entry, each holding <t> children built from the block's
    // runs. A blank line between the entries is not part of
    // the list, but it is part of the copy: it travels as a
    // blank line inside the list, so the paste keeps it. A
    // block of another kind breaks the carry. Only one list
    // carries: a selection spanning two lists pastes as plain
    // text, as before. The split of a merged term happens
    // here, at copy time, on the node being built: a term
    // block of a <dl> whose text splits at the first offset
    // TermSplitOffsets finds becomes a <dt> holding the text
    // before the offset plus a <dd> holding the rest, with
    // the joining space deleted. Paste never splits.
    if (Cursor.hasSelection()) {
        QTextDocument* Document = document();
        int Start = Cursor.selectionStart();
        int End = Cursor.selectionEnd();
        QString Kind;
        QVector<QPair<QString, QString>> Attributes;
        bool OneList = true;
        bool HaveList = false;
        xmlNodePtr ListNode = nullptr;
        xmlNodePtr CurrentItem = nullptr;
        QString CurrentRole;
        int PendingBlanks = 0;
        auto FlushBlanks = [&]() {
            while (PendingBlanks > 0 && ListNode != nullptr) {
                xmlAddChild(ListNode, NewTextNode(QStringLiteral("\n\n")));
                --PendingBlanks;
            }
        };
        for (QTextBlock Block = Document->findBlock(Start);
             Block.isValid() && Block.position() < End && OneList;
             Block = Block.next()) {
            QString BlockKind = Block.blockFormat()
                                    .property(DocListKindProperty)
                                    .toString();
            if (BlockKind.isEmpty()) {
                if (Block.text().trimmed().isEmpty()) {
                    ++PendingBlanks;
                    continue;
                }
                if (!HaveList) {
                    // A non-list block before any list block:
                    // the selection does not start with a list.
                    OneList = false;
                    break;
                }
                // A non-list block after list blocks: stop
                // collecting here, but keep the list carry for
                // the entry already collected, so a selection
                // that runs past the definition into the next
                // paragraph still pastes as a list (Bug 478).
                // The trailing block stays in the HTML and
                // pastes as plain text.
                FlushBlanks();
                break;
            }
            if (Kind.isEmpty()) {
                Kind = BlockKind;
                Attributes = AttributesFromVariant(
                    Block.blockFormat().property(DocListProperty));
                QByteArray KindBytes = Kind.toUtf8();
                ListNode = NewNode(KindBytes.constData());
                SetNodeAttributes(ListNode, Attributes);
                HaveList = true;
            } else if (BlockKind != Kind) {
                OneList = false;
                break;
            }
            FlushBlanks();
            QString Role = ListRoleOf(Block);
            if (Role.isEmpty()) {
                Role = Kind == QLatin1String("dl")
                           ? QStringLiteral("term")
                           : QStringLiteral("item");
            }
            if (Kind == QLatin1String("dl")) {
                if (Role == QLatin1String("term")) {
                    QVector<int> Offsets =
                        TermSplitOffsets(Block, document());
                    xmlNodePtr TermNode = NodeFromBlock(Block);
                    if (!Offsets.isEmpty()) {
                        xmlNodePtr AfterNode =
                            SplitTNodeAt(TermNode, Offsets.first());
                        xmlNodePtr Term =
                            AddElementChild(ListNode, "dt");
                        xmlAddChild(Term, TermNode);
                        xmlNodePtr Definition =
                            AddElementChild(ListNode, "dd");
                        xmlAddChild(Definition, AfterNode);
                        CurrentItem = Definition;
                        CurrentRole = QStringLiteral("definition");
                    } else {
                        xmlNodePtr Term =
                            AddElementChild(ListNode, "dt");
                        xmlAddChild(Term, TermNode);
                        CurrentItem = Term;
                        CurrentRole = QStringLiteral("term");
                    }
                } else {
                    // A definition paragraph: it joins the
                    // definition open after its term, or starts
                    // one (with an empty term) when no term
                    // precedes it.
                    if (CurrentRole != QLatin1String("definition")) {
                        if (CurrentRole != QLatin1String("term")) {
                            AddElementChild(ListNode, "dt");
                        }
                        CurrentItem = AddElementChild(ListNode, "dd");
                        CurrentRole = QStringLiteral("definition");
                    }
                    xmlAddChild(CurrentItem, NodeFromBlock(Block));
                }
            } else {
                if (Role == QLatin1String("continuation") &&
                    CurrentItem != nullptr &&
                    (CurrentRole == QLatin1String("item") ||
                     CurrentRole == QLatin1String("continuation"))) {
                    xmlAddChild(CurrentItem, NodeFromBlock(Block));
                    CurrentRole = QStringLiteral("continuation");
                } else {
                    CurrentItem = AddElementChild(ListNode, "li");
                    xmlAddChild(CurrentItem, NodeFromBlock(Block));
                    CurrentRole = QStringLiteral("item");
                }
            }
        }
        if (OneList && HaveList && ListNode != nullptr) {
            FlushBlanks();
            Data->setData(ListRunMimeType,
                          SerializeNode(ListNode).toUtf8());
        }
        xmlFreeNode(ListNode);
    }
    return Data;
}

/**
 * @brief Returns the tag stack of the character at a position.
 * @param Document The document.
 * @param Pos The character position.
 * @return The stack, empty when the position holds no character.
 */
static DocTagStack StackAtPosition(QTextDocument* Document, int Pos)
{
    if (Pos < 0) {
        return DocTagStack();
    }
    QTextBlock Block = Document->findBlock(Pos);
    if (!Block.isValid()) {
        return DocTagStack();
    }
    for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
        QTextFragment Fragment = It.fragment();
        if (Pos >= Fragment.position() &&
            Pos < Fragment.position() + Fragment.length()) {
            return StackOf(Fragment.charFormat());
        }
    }
    return DocTagStack();
}

/**
 * @brief Whether the two stacks hold the same link tag.
 *
 * Two adjacent links can share a tag name (two xrefs side by
 * side), so the name alone does not say the caret is inside
 * one link: the entries must match, attributes and all.
 * @param Before The stack before the caret.
 * @param After The stack after the caret.
 * @param Name The link tag name.
 * @return True when both stacks hold that tag with the same entry.
 */
static bool SameLink(const DocTagStack& Before, const DocTagStack& After,
                     const QString& Name)
{
    const DocTagEntry* BeforeEntry = nullptr;
    const DocTagEntry* AfterEntry = nullptr;
    for (const DocTagEntry& Entry : Before) {
        if (Entry.Name == Name) {
            BeforeEntry = &Entry;
            break;
        }
    }
    for (const DocTagEntry& Entry : After) {
        if (Entry.Name == Name) {
            AfterEntry = &Entry;
            break;
        }
    }
    return BeforeEntry != nullptr && AfterEntry != nullptr &&
        *BeforeEntry == *AfterEntry;
}

/**
 * @brief Returns the marker at a position, if the character is one.
 * @param Document The document.
 * @param Pos The character position.
 * @return The marker value, invalid when the character is no marker.
 */
static QVariant MarkerAtPosition(QTextDocument* Document, int Pos)
{
    if (Pos < 0) {
        return QVariant();
    }
    QTextBlock Block = Document->findBlock(Pos);
    if (!Block.isValid()) {
        return QVariant();
    }
    for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
        QTextFragment Fragment = It.fragment();
        if (Pos >= Fragment.position() &&
            Pos < Fragment.position() + Fragment.length()) {
            return Fragment.charFormat().property(DocMarkerProperty);
        }
    }
    return QVariant();
}

/**
 * @brief The link entry a fragment's format carries, if it is a link.
 *
 * A link with visible text carries the link in its tag stack; a
 * bare reference (an empty <xref/>, <eref/>, and so on) carries it
 * as the fragment's marker instead, because the marker is the
 * reference itself. Both count as a link here, so Rename and the
 * read-only check work for bare references too (Bug 524).
 * @param Format The fragment's character format.
 * @param LinkName Receives the link tag name.
 * @param LinkEntry Receives the link entry.
 * @return True when the format carries a link.
 */
static bool LinkEntryOfFormat(const QTextCharFormat& Format,
                              QString& LinkName, DocTagEntry& LinkEntry)
{
    DocTagStack Stack = StackOf(Format);
    for (const QString& Name :
         {QStringLiteral("xref"), QStringLiteral("eref"),
          QStringLiteral("relref"), QStringLiteral("cref")}) {
        for (const DocTagEntry& Entry : Stack) {
            if (Entry.Name == Name) {
                LinkName = Name;
                LinkEntry = Entry;
                return true;
            }
        }
    }
    QVariant MarkerValue = Format.property(DocMarkerProperty);
    if (MarkerValue.isValid()) {
        DocTagEntry Marker = EntryFromVariant(MarkerValue);
        if (Marker.Name == QLatin1String("xref") ||
            Marker.Name == QLatin1String("eref") ||
            Marker.Name == QLatin1String("relref") ||
            Marker.Name == QLatin1String("cref")) {
            LinkName = Marker.Name;
            LinkEntry = Marker;
            return true;
        }
    }
    return false;
}

/**
 * @brief The text range of the link holding a position (Bug 514 v2).
 *
 * A reference's visible text is read-only: typing or deleting
 * inside it breaks the reference, so the editor blocks those
 * keys and offers Rename instead. This finds the link's whole
 * text range, so the read-only check and the rename both know
 * what the link covers. A link's text normally sits in one
 * block; the range stops at the block's edge.
 * @param Document The document.
 * @param Pos A position in the link's text.
 * @param Start Receives the link's first character.
 * @param End Receives the position just past its last character.
 * @param LinkName Receives the link tag name.
 * @param LinkEntry Receives the link entry.
 * @return False when no link holds the position.
 */
static bool LinkRangeAt(QTextDocument* Document, int Pos, int& Start,
                        int& End, QString& LinkName,
                        DocTagEntry& LinkEntry)
{
    if (Document == nullptr || Pos < 0) {
        return false;
    }
    QTextBlock Block = Document->findBlock(Pos);
    if (!Block.isValid()) {
        return false;
    }
    // The fragment holding the position, and its link entry.
    QTextBlock::iterator At = Block.end();
    DocTagEntry FoundEntry;
    QString FoundName;
    bool FoundIt = false;
    for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
        QTextFragment Fragment = It.fragment();
        if (Pos >= Fragment.position() &&
            Pos < Fragment.position() + Fragment.length()) {
            At = It;
            FoundIt = LinkEntryOfFormat(Fragment.charFormat(),
                                        FoundName, FoundEntry);
            break;
        }
    }
    if (!FoundIt) {
        return false;
    }
    LinkName = FoundName;
    LinkEntry = FoundEntry;
    // Expand to the link's edges: neighbouring fragments hold
    // the same link entry while the link runs on.
    Start = At.fragment().position();
    End = Start + At.fragment().length();
    QTextBlock::iterator It = At;
    while (It != Block.begin()) {
        --It;
        QTextFragment Fragment = It.fragment();
        QString NeighbourName;
        DocTagEntry NeighbourEntry;
        if (!LinkEntryOfFormat(Fragment.charFormat(),
                               NeighbourName, NeighbourEntry)) {
            break;
        }
        if (NeighbourName != LinkName || !(NeighbourEntry == LinkEntry)) {
            break;
        }
        Start = Fragment.position();
    }
    It = At;
    ++It;
    while (!It.atEnd()) {
        QTextFragment Fragment = It.fragment();
        QString NeighbourName;
        DocTagEntry NeighbourEntry;
        if (!LinkEntryOfFormat(Fragment.charFormat(),
                               NeighbourName, NeighbourEntry)) {
            break;
        }
        if (NeighbourName != LinkName || !(NeighbourEntry == LinkEntry)) {
            break;
        }
        End = Fragment.position() + Fragment.length();
        ++It;
    }
    return true;
}

/**
 * @brief Whether a cursor would change a reference's text
 *        (Bug 514 v2).
 *
 * A reference's visible text is read-only. The caret touches
 * it when it sits in [link start, link end): at the link start
 * and inside it, but not just after it, where typing is plain
 * text after the link. A selection touches it when it overlaps
 * the link's text at all. Deleting a selection that covers a
 * whole link is the one exception: it removes the reference,
 * which is a deliberate delete, not an edit of its text.
 * @param Document The document.
 * @param Cursor The cursor.
 * @param LinkStart Receives the touched link's first character.
 * @return True when the cursor would change a link's text.
 */
static bool CursorTouchesLinkText(QTextDocument* Document,
                                  const QTextCursor& Cursor,
                                  int& LinkStart)
{
    if (Document == nullptr) {
        return false;
    }
    if (Cursor.hasSelection()) {
        int Start = Cursor.selectionStart();
        int End = Cursor.selectionEnd();
        QTextBlock Block = Document->findBlock(Start);
        while (Block.isValid() && Block.position() < End) {
            for (QTextBlock::iterator It = Block.begin();
                 !It.atEnd(); ++It) {
                QTextFragment Fragment = It.fragment();
                int FragStart = Fragment.position();
                int FragEnd = FragStart + Fragment.length();
                if (FragEnd <= Start || FragStart >= End) {
                    continue;
                }
                // Bug 524: a bare reference is a marker, not a
                // stack entry, so the link check reads the
                // fragment's format for either one.
                QString FoundName;
                DocTagEntry FoundEntry;
                if (!LinkEntryOfFormat(Fragment.charFormat(),
                                       FoundName, FoundEntry)) {
                    continue;
                }
                int LS, LE;
                QString LN;
                DocTagEntry LE2;
                if (LinkRangeAt(Document, FragStart, LS, LE, LN,
                                LE2)) {
                    LinkStart = LS;
                    return true;
                }
            }
            Block = Block.next();
        }
        return false;
    }
    int Start, End;
    QString LinkName;
    DocTagEntry LinkEntry;
    if (LinkRangeAt(Document, Cursor.position(), Start, End, LinkName,
                    LinkEntry)) {
        LinkStart = Start;
        return true;
    }
    return false;
}

/**
 * @brief Whether a Backspace/Delete would break a reference
 *        (Bug 514 v2).
 *
 * Deleting a character inside a reference's text breaks it.
 * Deleting a selection that covers one or more whole links
 * removes those references, which is a deliberate delete, so
 * it still goes through. Anything less -- a caret inside a
 * link, or a selection covering only part of one -- is
 * refused.
 * @param Document The document.
 * @param Cursor The cursor.
 * @param Code Qt::Key_Backspace or Qt::Key_Delete.
 * @param LinkStart Receives the broken link's first character.
 * @return True when the key would break a link.
 */
static bool DeletionBreaksLink(QTextDocument* Document,
                               const QTextCursor& Cursor, int Code,
                               int& LinkStart)
{
    if (Document == nullptr) {
        return false;
    }
    if (Cursor.hasSelection()) {
        int Start = Cursor.selectionStart();
        int End = Cursor.selectionEnd();
        QTextBlock Block = Document->findBlock(Start);
        while (Block.isValid() && Block.position() < End) {
            for (QTextBlock::iterator It = Block.begin();
                 !It.atEnd(); ++It) {
                QTextFragment Fragment = It.fragment();
                int FragStart = Fragment.position();
                int FragEnd = FragStart + Fragment.length();
                if (FragEnd <= Start || FragStart >= End) {
                    continue;
                }
                int LS, LE;
                QString LN;
                DocTagEntry LE2;
                if (!LinkRangeAt(Document, FragStart, LS, LE, LN, LE2)) {
                    continue;
                }
                // This link is overlapped. It is fully covered
                // only when the selection spans its whole range.
                if (Start > LS || End < LE) {
                    LinkStart = LS;
                    return true;
                }
            }
            Block = Block.next();
        }
        return false;
    }
    // Backspace takes the character before the caret; Delete
    // takes the character at the caret.
    int Take = (Code == Qt::Key_Backspace)
                   ? Cursor.position() - 1
                   : Cursor.position();
    if (Take < 0) {
        return false;
    }
    int LS, LE;
    QString LN;
    DocTagEntry LE2;
    if (LinkRangeAt(Document, Take, LS, LE, LN, LE2)) {
        LinkStart = LS;
        return true;
    }
    return false;
}

/**
 * @brief The text range of the index entry marker holding a position (Bug 503).
 *
 * An index entry is a marker: a single glyph carrying an iref
 * entry in its marker property. Its text is read-only, like a
 * reference's (Bug 514), so the editor blocks keys that would
 * change it and offers Edit index entry instead. This finds
 * the marker's whole range, so the read-only check and the
 * edit both know what the marker covers. A marker normally
 * sits in one block; the range stops at the block's edge.
 * @param Document The document.
 * @param Pos A position in the marker's text.
 * @param Start Receives the marker's first character.
 * @param End Receives the position just past its last character.
 * @param IrefEntry Receives the marker entry.
 * @return False when no index entry marker holds the position.
 */
static bool IrefRangeAt(QTextDocument* Document, int Pos, int& Start,
                        int& End, DocTagEntry& IrefEntry)
{
    if (Document == nullptr || Pos < 0) {
        return false;
    }
    QTextBlock Block = Document->findBlock(Pos);
    if (!Block.isValid()) {
        return false;
    }
    QTextBlock::iterator At = Block.end();
    DocTagEntry FoundEntry;
    bool FoundIt = false;
    for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
        QTextFragment Fragment = It.fragment();
        if (Pos >= Fragment.position() &&
            Pos < Fragment.position() + Fragment.length()) {
            At = It;
            QVariant MarkerValue =
                Fragment.charFormat().property(DocMarkerProperty);
            if (!MarkerValue.isValid()) {
                break;
            }
            DocTagEntry Entry = EntryFromVariant(MarkerValue);
            if (Entry.Name != QLatin1String("iref")) {
                break;
            }
            FoundEntry = Entry;
            FoundIt = true;
            break;
        }
    }
    if (!FoundIt) {
        return false;
    }
    IrefEntry = FoundEntry;
    Start = At.fragment().position();
    End = Start + At.fragment().length();
    QTextBlock::iterator It = At;
    while (It != Block.begin()) {
        --It;
        QTextFragment Fragment = It.fragment();
        QVariant MarkerValue =
            Fragment.charFormat().property(DocMarkerProperty);
        if (!MarkerValue.isValid()) {
            break;
        }
        if (!(EntryFromVariant(MarkerValue) == IrefEntry)) {
            break;
        }
        Start = Fragment.position();
    }
    It = At;
    ++It;
    while (!It.atEnd()) {
        QTextFragment Fragment = It.fragment();
        QVariant MarkerValue =
            Fragment.charFormat().property(DocMarkerProperty);
        if (!MarkerValue.isValid()) {
            break;
        }
        if (!(EntryFromVariant(MarkerValue) == IrefEntry)) {
            break;
        }
        End = Fragment.position() + Fragment.length();
        ++It;
    }
    return true;
}

/**
 * @brief Whether a caret or selection would change an index
 *        entry marker's text (Bug 503).
 *
 * The marker is a single glyph; typing or deleting inside it
 * breaks the index entry, so the key is refused and the hint
 * says to Edit index entry instead. A caret at the marker's
 * start touches it; a caret just after it does not, where
 * typing is plain text after the marker. A selection touches
 * it when it overlaps the marker at all. Deleting a selection
 * that covers a whole marker is the one exception: it removes
 * the index entry, which is a deliberate delete, not an edit
 * of its text.
 * @param Document The document.
 * @param Cursor The cursor.
 * @param IrefStart Receives the touched marker's first character.
 * @return True when the cursor would change a marker's text.
 */
static bool CursorTouchesIref(QTextDocument* Document,
                              const QTextCursor& Cursor, int& IrefStart)
{
    if (Document == nullptr) {
        return false;
    }
    if (Cursor.hasSelection()) {
        int Start = Cursor.selectionStart();
        int End = Cursor.selectionEnd();
        QTextBlock Block = Document->findBlock(Start);
        while (Block.isValid() && Block.position() < End) {
            for (QTextBlock::iterator It = Block.begin();
                 !It.atEnd(); ++It) {
                QTextFragment Fragment = It.fragment();
                int FragStart = Fragment.position();
                int FragEnd = FragStart + Fragment.length();
                if (FragEnd <= Start || FragStart >= End) {
                    continue;
                }
                int IS, IE;
                DocTagEntry IE2;
                if (IrefRangeAt(Document, FragStart, IS, IE, IE2)) {
                    IrefStart = IS;
                    return true;
                }
            }
            Block = Block.next();
        }
        return false;
    }
    int Start, End;
    DocTagEntry IrefEntry;
    if (IrefRangeAt(Document, Cursor.position(), Start, End, IrefEntry)) {
        IrefStart = Start;
        return true;
    }
    return false;
}

/**
 * @brief Whether a Backspace/Delete would break an index entry
 *        marker (Bug 503).
 *
 * Deleting a character inside a marker breaks the index entry.
 * Deleting a selection that covers a whole marker removes it,
 * which is a deliberate delete, so it still goes through.
 * Anything less -- a caret inside a marker, or a selection
 * covering only part of one -- is refused.
 * @param Document The document.
 * @param Cursor The cursor.
 * @param Code Qt::Key_Backspace or Qt::Key_Delete.
 * @param IrefStart Receives the broken marker's first character.
 * @return True when the key would break a marker.
 */
static bool DeletionBreaksIref(QTextDocument* Document,
                               const QTextCursor& Cursor, int Code,
                               int& IrefStart)
{
    if (Document == nullptr) {
        return false;
    }
    if (Cursor.hasSelection()) {
        int Start = Cursor.selectionStart();
        int End = Cursor.selectionEnd();
        QTextBlock Block = Document->findBlock(Start);
        while (Block.isValid() && Block.position() < End) {
            for (QTextBlock::iterator It = Block.begin();
                 !It.atEnd(); ++It) {
                QTextFragment Fragment = It.fragment();
                int FragStart = Fragment.position();
                int FragEnd = FragStart + Fragment.length();
                if (FragEnd <= Start || FragStart >= End) {
                    continue;
                }
                int IS, IE;
                DocTagEntry IE2;
                if (!IrefRangeAt(Document, FragStart, IS, IE, IE2)) {
                    continue;
                }
                // This marker is overlapped. It is fully covered
                // only when the selection spans its whole range.
                if (Start > IS || End < IE) {
                    IrefStart = IS;
                    return true;
                }
            }
            Block = Block.next();
        }
        return false;
    }
    // Backspace takes the character before the caret; Delete
    // takes the character at the caret.
    int Take = (Code == Qt::Key_Backspace)
                   ? Cursor.position() - 1
                   : Cursor.position();
    if (Take < 0) {
        return false;
    }
    int IS, IE;
    DocTagEntry IE2;
    if (IrefRangeAt(Document, Take, IS, IE, IE2)) {
        IrefStart = IS;
        return true;
    }
    return false;
}

/**
 * @brief The character format of a selection that sits entirely
 *        inside one link, or an invalid format when it does not.
 *
 * Typing over a selection takes the format of the character
 * before the cursor, which is plain text when the selection was
 * made right-to-left, so the replacement text loses the link
 * and the reference is destroyed (Bug 514). The format returned
 * here is the link's own format, so the replacement is inserted
 * with it and stays a link.
 * @param Document The document.
 * @param Cursor The cursor carrying the selection.
 * @return The link's format, or an invalid format.
 */
static QTextCharFormat LinkFormatForSelection(QTextDocument* Document,
                                              const QTextCursor& Cursor)
{
    QTextCharFormat None;
    if (Document == nullptr || !Cursor.hasSelection()) {
        return None;
    }
    int Start = Cursor.selectionStart();
    int End = Cursor.selectionEnd();
    DocTagStack First = StackAtPosition(Document, Start);
    QString Link;
    const DocTagEntry* LinkEntry = nullptr;
    for (const QString& Name :
         {QStringLiteral("xref"), QStringLiteral("eref"),
          QStringLiteral("relref"), QStringLiteral("cref")}) {
        for (const DocTagEntry& Entry : First) {
            if (Entry.Name == Name) {
                Link = Name;
                LinkEntry = &Entry;
                break;
            }
        }
        if (!Link.isEmpty()) {
            break;
        }
    }
    if (Link.isEmpty() || LinkEntry == nullptr) {
        return None;
    }
    // Every fragment in the range must hold the same link entry:
    // a selection spanning a link boundary is not one link.
    QTextBlock Block = Document->findBlock(Start);
    QTextCharFormat LinkFormat;
    while (Block.isValid() && Block.position() < End) {
        for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
            QTextFragment Fragment = It.fragment();
            int FragStart = Fragment.position();
            int FragEnd = FragStart + Fragment.length();
            if (FragEnd <= Start || FragStart >= End) {
                continue;
            }
            DocTagStack Stack = StackOf(Fragment.charFormat());
            const DocTagEntry* Entry = nullptr;
            for (const DocTagEntry& E : Stack) {
                if (E.Name == Link) {
                    Entry = &E;
                    break;
                }
            }
            if (Entry == nullptr || !(*Entry == *LinkEntry)) {
                return None;
            }
            if (!LinkFormat.isValid()) {
                LinkFormat = Fragment.charFormat();
            }
        }
        Block = Block.next();
    }
    return LinkFormat;
}

TextEditor::TextEditor(QWidget* Parent)
    : QWidget(Parent)
    , _Tabs(nullptr)
    , _TextEdit(nullptr)
    , _XmlEdit(nullptr)
    , _XmlPage(nullptr)
    , _XmlReadOnlyCheck(nullptr)
    , _XmlPreservedNote(nullptr)
    , _XmlDirty(false)
    , _AllowSubsections(false)
    , _BulletedButton(nullptr)
    , _NumberedButton(nullptr)
    , _DefinitionButton(nullptr)
    , _BlocksButton(nullptr)
    , _VersionPicker(nullptr)
    , _ShowMarkersCheck(nullptr)
    , _BlockKind()
    , _SettingParagraphs(false)
    , _DiagMarkersShown(false)
    , _DiagMarkersBlocks(0)
    , _DiagMarkersSourceFirst(0)
    , _DiagMarkersListFirst(0)
    , _DiagMarkersGearsInserted(0)
    , _DiagMarkersGearsRemoved(0)
    , _SrcPlaceholderHintBlock(-1)
    , _LinkReadOnlyHintPos(-1)
    , _IrefReadOnlyHintPos(-1)
    , _SpellChecker(nullptr)
    , _Document(nullptr)
    , _BibCache(nullptr)
    , _SpellHighlighter(nullptr)
{
    _TextEdit = new DocTextEdit(this);
    _TextEdit->setToolTip(tr("The text. Press Enter for a new paragraph or "
                             "the next list item; Shift+Enter adds a "
                             "paragraph to the same list item. "
                             "Right-click for text tags."));
    connect(_TextEdit, &QTextEdit::textChanged,
            this, &TextEditor::OnTextDocumentChanged);
    // The policy lives on the edit itself, NOT on its viewport: a
    // policy on the viewport never fires for QTextEdit (the scroll
    // area shows the standard menu instead). The position in the
    // signal is in viewport coordinates, per the QAbstractScrollArea
    // rule in the QWidget documentation.
    _TextEdit->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(_TextEdit, &QTextEdit::customContextMenuRequested,
            this, &TextEditor::OnContextMenuRequested);
    // The event filter watches Enter and Shift+Enter in list
    // blocks, for the Word-style list keys. It also watches
    // the viewport for hover tooltips (Bug 468): tooltip
    // events go to the viewport, not the edit, and Qt shows
    // the edit's own tooltip over a fragment's, so the filter
    // must answer them itself.
    _TextEdit->installEventFilter(this);
    _TextEdit->viewport()->installEventFilter(this);

    _XmlEdit = new QPlainTextEdit(this);
    _XmlEdit->setReadOnly(true);
    _XmlEdit->setToolTip(tr("The raw XML of the text, for reading. "
                            "Edit on the Text tab."));
    connect(_XmlEdit, &QPlainTextEdit::textChanged,
            this, &TextEditor::OnXmlTextChanged);

    // Bug 520: the XML tab carries a Read-only checkbox at the
    // top, checked by default. Unchecking it makes the pane
    // editable, so XML can be pasted and edited directly instead
    // of hand-editing the file outside the editor.
    _XmlReadOnlyCheck = new QCheckBox(tr("Read-only"), this);
    _XmlReadOnlyCheck->setChecked(true);
    _XmlReadOnlyCheck->setToolTip(
        tr("Uncheck to edit the XML directly. Re-checking applies "
           "the edits: the XML is re-parsed and the text rebuilt "
           "from it. A parse error keeps the box unchecked so the "
           "XML can be fixed, and the document is untouched."));
    connect(_XmlReadOnlyCheck, &QCheckBox::toggled,
            this, &TextEditor::OnXmlReadOnlyToggled);
    _XmlPreservedNote = new QLabel(this);
    _XmlPreservedNote->setVisible(false);
    _XmlPage = new QWidget(this);
    QVBoxLayout* XmlLayout = new QVBoxLayout(_XmlPage);
    QHBoxLayout* XmlTopRow = new QHBoxLayout();
    XmlTopRow->addWidget(_XmlReadOnlyCheck);
    XmlTopRow->addWidget(_XmlPreservedNote);
    XmlTopRow->addStretch();
    XmlLayout->addLayout(XmlTopRow);
    XmlLayout->addWidget(_XmlEdit);
    XmlLayout->setContentsMargins(0, 0, 0, 0);

    _BulletedButton = new QPushButton(tr("Bulleted list"), this);
    _NumberedButton = new QPushButton(tr("Numbered list"), this);
    _DefinitionButton = new QPushButton(tr("Definition list"), this);
    _BulletedButton->setCheckable(true);
    _NumberedButton->setCheckable(true);
    _DefinitionButton->setCheckable(true);
    _BulletedButton->setToolTip(
        tr("Turn the paragraph into a bulleted list, or back again."));
    _NumberedButton->setToolTip(
        tr("Turn the paragraph into a numbered list, or back again."));
    _DefinitionButton->setToolTip(
        tr("Turn the paragraph into a definition list, or back again."));
    connect(_BulletedButton, &QPushButton::clicked, this, [this]() {
        ToggleBlockList(QStringLiteral("ul"));
    });
    connect(_NumberedButton, &QPushButton::clicked, this, [this]() {
        ToggleBlockList(QStringLiteral("ol"));
    });
    connect(_DefinitionButton, &QPushButton::clicked, this, [this]() {
        ToggleBlockList(QStringLiteral("dl"));
    });
    // The Blocks button starts and stops a block: its menu
    // picks the kind of block, its label names the picked kind,
    // and pressing the button again stops the block. Until a
    // kind is picked, pressing the button opens the menu: it
    // has no default (Bug 444). The menu holds ASCII art, a
    // figure, and source code, plus the Comment insert, which is
    // a one-shot action rather than a block kind.
    _BlocksButton = new QToolButton(this);
    _BlocksButton->setText(tr("Blocks"));
    _BlocksButton->setCheckable(true);
    _BlocksButton->setPopupMode(QToolButton::MenuButtonPopup);
    _BlocksButton->setToolTip(
        tr("Start or stop a block; the menu picks which kind of block."));
    QMenu* BlockMenu = new QMenu(this);
    QAction* ArtworkAction = BlockMenu->addAction(tr("ASCII art"));
    connect(ArtworkAction, &QAction::triggered, this, [this]() {
        _BlockKind = QStringLiteral("artwork");
        ToggleArtwork();
    });
    QAction* CommentAction = BlockMenu->addAction(tr("Comment"));
    connect(CommentAction, &QAction::triggered, this,
            &TextEditor::InsertComment);
    QAction* FigureAction = BlockMenu->addAction(tr("Figure"));
    connect(FigureAction, &QAction::triggered, this, [this]() {
        _BlockKind = QStringLiteral("figure");
        ToggleFigure();
    });
    QAction* SourceAction = BlockMenu->addAction(tr("Source code"));
    connect(SourceAction, &QAction::triggered, this, [this]() {
        _BlockKind = QStringLiteral("sourcecode");
        ToggleSourceCode();
    });
    QAction* TableAction = BlockMenu->addAction(tr("Table"));
    connect(TableAction, &QAction::triggered, this,
            &TextEditor::InsertTable);
    _BlocksButton->setMenu(BlockMenu);
    connect(_BlocksButton, &QToolButton::clicked, this,
            &TextEditor::OnBlocksButtonClicked);
    // The version picker switches the artwork version showing
    // in the figure the cursor sits in. It stays hidden
    // everywhere else, and for a figure holding one version.
    _VersionPicker = new QComboBox(this);
    _VersionPicker->setToolTip(
        tr("The artwork version shown. Switch to edit another version."));
    _VersionPicker->setEnabled(false);
    _VersionPicker->setVisible(false);
    connect(_VersionPicker, &QComboBox::currentIndexChanged,
            this, &TextEditor::OnVersionPickerChanged);
    // The markers checkbox shows or hides the gear markers
    // that open a block's options. Its setting is shared by
    // every text editor pane and remembered across runs.
    _ShowMarkersCheck = new QCheckBox(
        tr("Show option markers") + QStringLiteral(" ") + OptionGlyph, this);
    _ShowMarkersCheck->setToolTip(
        tr("Show the gear markers that open a block's options."));
    _ShowMarkersCheck->setChecked(OptionMarkersShown());
    connect(_ShowMarkersCheck, &QCheckBox::toggled,
            this, &TextEditor::OnShowMarkersToggled);
    connect(_TextEdit, &QTextEdit::cursorPositionChanged,
            this, &TextEditor::UpdateBlockButtons);
    connect(_TextEdit, &QTextEdit::cursorPositionChanged,
            this, &TextEditor::OnCaretMoved);
    QHBoxLayout* ButtonRow = new QHBoxLayout();
    ButtonRow->addWidget(_BulletedButton);
    ButtonRow->addWidget(_NumberedButton);
    ButtonRow->addWidget(_DefinitionButton);
    ButtonRow->addWidget(_BlocksButton);
    ButtonRow->addWidget(_VersionPicker);
    ButtonRow->addStretch();
    ButtonRow->addWidget(_ShowMarkersCheck);
    QWidget* TextPage = new QWidget(this);
    QVBoxLayout* TextLayout = new QVBoxLayout(TextPage);
    TextLayout->addLayout(ButtonRow);
    TextLayout->addWidget(_TextEdit);
    TextLayout->setContentsMargins(0, 0, 0, 0);

    _Tabs = new QTabWidget(this);
    _Tabs->addTab(TextPage, tr("Text"));
    _Tabs->addTab(_XmlPage, tr("XML"));
    connect(_Tabs, &QTabWidget::currentChanged,
            this, &TextEditor::OnTabChanged);

    QVBoxLayout* Layout = new QVBoxLayout(this);
    Layout->addWidget(_Tabs);
    Layout->setContentsMargins(0, 0, 0, 0);
}

/**
 * @brief Rebuilds a bulleted or numbered list with one more block in it.
 *
 * Adding a block to a list appends it to the list's numbering,
 * whatever its document position, so a block inserted in the
 * middle would number last. Rebuilding instead takes every item
 * block off the old list and adds them back in document order,
 * with the new block in its document place, so the numbers
 * follow the document. The list's format is kept. Removing a
 * block from a list sets its indent to 1, so the indent is
 * restored to 0 on every item afterwards, or the rebuilt items
 * would sit one level deeper than the added block.
 * @param Document The document.
 * @param List The list being rebuilt.
 * @param Added The block joining the list.
 */
static void RebuildList(QTextDocument* Document, QTextList* List,
                        const QTextBlock& Added)
{
    QTextListFormat Format = List->format();
    QTextBlock First = List->item(0);
    QTextBlock Last = List->item(List->count() - 1);
    QVector<QTextBlock> Items;
    for (QTextBlock Block = First;
         Block.isValid() && Block.position() <= Last.position();
         Block = Block.next()) {
        if (Block.textList() == List) {
            Items.append(Block);
        }
    }
    int At = Items.size();
    for (int i = 0; i < Items.size(); ++i) {
        if (Items[i].position() > Added.position()) {
            At = i;
            break;
        }
    }
    Items.insert(At, Added);
    for (const QTextBlock& Item : Items) {
        if (Item != Added) {
            List->remove(Item);
        }
    }
    QTextList* Rebuilt = nullptr;
    for (const QTextBlock& Item : Items) {
        if (Rebuilt == nullptr) {
            QTextCursor Cursor(Document);
            Cursor.setPosition(Item.position());
            Rebuilt = Cursor.createList(Format);
        } else {
            Rebuilt->add(Item);
        }
    }
    // QTextList::remove() sets the removed block's indent to 1,
    // so every item the rebuild touched would end up one level
    // deeper than the added block, which was never removed.
    // Restore indent 0 on every item, the same compensation
    // DetachBlockFromList applies after a remove, touching
    // nothing else. The continuation block keeps its indent 1:
    // it is not an item and never joins the list.
    for (const QTextBlock& Item : Items) {
        QTextCursor Cursor(Document);
        Cursor.setPosition(Item.position());
        QTextBlockFormat BlockFormat = Cursor.blockFormat();
        BlockFormat.setIndent(0);
        Cursor.setBlockFormat(BlockFormat);
    }
}

void TextEditor::ShowSrcPlaceholderHint(int BlockPos)
{
    if (BlockPos == _SrcPlaceholderHintBlock) {
        return;
    }
    _SrcPlaceholderHintBlock = BlockPos;
    QMessageBox::information(
        _TextEdit, tr("Source file line"),
        tr("This line refers to the source file and cannot be "
           "edited. To add text after it, move to the next line."));
}

void TextEditor::ShowLinkReadOnlyHint(int LinkStart)
{
    if (LinkStart == _LinkReadOnlyHintPos) {
        return;
    }
    _LinkReadOnlyHintPos = LinkStart;
    QMessageBox::information(
        _TextEdit, tr("Reference text"),
        tr("This text is part of a reference and cannot be edited "
           "directly. Right-click the reference and choose Rename "
           "to change it, or select the whole reference and press "
           "Delete to remove it."));
}

void TextEditor::ShowIrefReadOnlyHint(int IrefStart)
{
    if (IrefStart == _IrefReadOnlyHintPos) {
        return;
    }
    _IrefReadOnlyHintPos = IrefStart;
    QMessageBox::information(
        _TextEdit, tr("Index entry"),
        tr("This is an index entry marker and cannot be edited "
           "directly. Right-click it and choose Edit index entry "
           "to change it, or select the whole marker and press "
           "Delete to remove it."));
}

QString TextEditor::ResolvedXrefTip(const QTextCursor& At,
                                    const QString& Fallback) const
{
    if (_Document == nullptr) {
        return Fallback;
    }
    int Start, End;
    QString LinkName;
    DocTagEntry LinkEntry;
    if (!LinkRangeAt(_TextEdit->document(), At.position(), Start, End,
                     LinkName, LinkEntry)) {
        return Fallback;
    }
    if (LinkName != QLatin1String("xref")) {
        return Fallback;
    }
    QString Target =
        AttributeOf(LinkEntry.Attributes, QStringLiteral("target"));
    if (Target.isEmpty()) {
        return Fallback;
    }
    for (const DocDocument::InternalTarget& Found :
         _Document->InternalTargets()) {
        if (Found.Anchor != Target) {
            continue;
        }
        QString Text = QStringLiteral("Cross-reference to ") + Found.Kind;
        if (!Found.Number.isEmpty()) {
            Text += QStringLiteral(" ") + Found.Number;
        }
        Text += QStringLiteral(": ") + Found.Title;
        return Text;
    }
    return Fallback;
}

bool TextEditor::eventFilter(QObject* Watched, QEvent* Event)
{
    if (Watched == _TextEdit->viewport() &&
        Event->type() == QEvent::ToolTip) {
        // A fragment's own hover text wins over the edit's:
        // show it when the hovered character carries one,
        // and let the edit's help show otherwise (Bug 468).
        QHelpEvent* Help = static_cast<QHelpEvent*>(Event);
        QTextCursor At = _TextEdit->cursorForPosition(Help->pos());
        QString Tip = At.charFormat().toolTip();
        if (!Tip.isEmpty()) {
            // Bug 515: a cross-reference hover shows the section
            // it points to, not just its anchor, so it stays
            // right when sections are renumbered.
            Tip = ResolvedXrefTip(At, Tip);
            QToolTip::showText(Help->globalPos(), Tip, _TextEdit);
            return true;
        }
        return QWidget::eventFilter(Watched, Event);
    }
    if (Watched != _TextEdit || Event->type() != QEvent::KeyPress) {
        return QWidget::eventFilter(Watched, Event);
    }
    QKeyEvent* Key = static_cast<QKeyEvent*>(Event);
    Qt::KeyboardModifiers Held = Key->modifiers() &
        (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier |
         Qt::MetaModifier);
    bool Plain = Held == Qt::KeyboardModifiers();
    bool ShiftOnly = Held == Qt::ShiftModifier;
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextBlock Block = Cursor.block();
    // Option markers cannot be deleted (Bug 420), and neither
    // can the src placeholder text (Bug 466): a deletion that
    // would take one is refused, and deleting a selection
    // covering one keeps the marks and takes the rest. The
    // check runs before every other branch, so no key handling
    // below can remove a mark. Typing beside a mark must
    // not take its format either, or the typed text would read
    // back as mark material.
    int Code = Key->key();
    bool ControlHeld = Held == Qt::ControlModifier;
    if (Code == Qt::Key_Backspace || Code == Qt::Key_Delete) {
        // Bug 514 v2: a reference's visible text is read-only.
        // Deleting inside it breaks the reference, so the key
        // is swallowed and the hint says to Rename instead.
        // Deleting a selection that covers a whole link removes
        // the reference, which is a deliberate delete, so it
        // still goes through.
        int BreakLinkStart = -1;
        if (DeletionBreaksLink(_TextEdit->document(), Cursor, Code,
                               BreakLinkStart)) {
            ShowLinkReadOnlyHint(BreakLinkStart);
            return true;
        }
        // Bug 503: an index entry marker is read-only the same
        // way. Deleting inside it breaks the entry; deleting a
        // selection that covers the whole marker removes it.
        int BreakIrefStart = -1;
        if (DeletionBreaksIref(_TextEdit->document(), Cursor, Code,
                               BreakIrefStart)) {
            ShowIrefReadOnlyHint(BreakIrefStart);
            return true;
        }
        if (!Cursor.hasSelection()) {
            // The empty paragraph after a trailing sourcecode
            // block is where the writer types after the code:
            // joining it into the code would leave nowhere to
            // type. Backspace at its start, and Delete at the
            // end of the code line before it, are refused, so
            // the paragraph stays.
            if (Code == Qt::Key_Backspace &&
                Cursor.position() == Block.position() &&
                Block.text().trimmed().isEmpty() &&
                !IsSourceCodeBlock(Block) &&
                Block.textList() == nullptr &&
                !Block.next().isValid()) {
                QTextBlock PrevBlock = Block.previous();
                if (PrevBlock.isValid() &&
                    IsSourceCodeBlock(PrevBlock)) {
                    return true;
                }
            }
            if (Code == Qt::Key_Delete &&
                Cursor.position() ==
                    Block.position() + Block.text().length() &&
                IsSourceCodeBlock(Block)) {
                QTextBlock NextBlock = Block.next();
                if (NextBlock.isValid() && !NextBlock.next().isValid() &&
                    NextBlock.text().trimmed().isEmpty() &&
                    !IsSourceCodeBlock(NextBlock) &&
                    NextBlock.textList() == nullptr) {
                    return true;
                }
            }
        }
        if (Cursor.hasSelection() &&
            (SelectionCoversOptionMarker(_TextEdit->document(),
                                         Cursor) ||
             SelectionCoversSrcPlaceholder(_TextEdit->document(),
                                           Cursor))) {
            if (ShiftOnly && Code == Qt::Key_Delete) {
                // Shift+Delete cuts: copy first, as Cut does.
                _TextEdit->copy();
            }
            QueueEmptySourceCleanup(this, Cursor);
            RemoveSelectionKeepingDisplayMarks(Cursor);
            _TextEdit->setTextCursor(Cursor);
            return true;
        }
        if (!Cursor.hasSelection()) {
            // A plain key takes one character inside the block
            // text, or joins blocks at the edges, which takes
            // no character. A control key takes a word: probe
            // the word range for a marker.
            bool TakesMarker = false;
            if (ControlHeld) {
                QTextCursor WordProbe(_TextEdit->document());
                WordProbe.setPosition(Cursor.position());
                if (Code == Qt::Key_Backspace) {
                    WordProbe.movePosition(QTextCursor::PreviousWord,
                                           QTextCursor::KeepAnchor);
                } else {
                    WordProbe.movePosition(QTextCursor::NextWord,
                                           QTextCursor::KeepAnchor);
                }
                TakesMarker = SelectionCoversOptionMarker(
                    _TextEdit->document(), WordProbe) ||
                    SelectionCoversSrcPlaceholder(_TextEdit->document(),
                                                  WordProbe);
            } else if (Code == Qt::Key_Backspace &&
                       Cursor.position() > Block.position()) {
                TakesMarker = HasOptionMarkerAt(_TextEdit->document(),
                                                Cursor.position() - 1) ||
                    HasSrcPlaceholderAt(_TextEdit->document(),
                                        Cursor.position() - 1);
            } else if (Code == Qt::Key_Delete &&
                       Cursor.position() <
                           Block.position() + Block.length() - 1) {
                TakesMarker = HasOptionMarkerAt(_TextEdit->document(),
                                                Cursor.position()) ||
                    HasSrcPlaceholderAt(_TextEdit->document(),
                                        Cursor.position());
            }
            if (TakesMarker) {
                return true;
            }
        }
    } else if (ControlHeld && Code == Qt::Key_X) {
        if (Cursor.hasSelection() &&
            (SelectionCoversOptionMarker(_TextEdit->document(),
                                         Cursor) ||
             SelectionCoversSrcPlaceholder(_TextEdit->document(),
                                           Cursor))) {
            _TextEdit->copy();
            QueueEmptySourceCleanup(this, Cursor);
            RemoveSelectionKeepingDisplayMarks(Cursor);
            _TextEdit->setTextCursor(Cursor);
            return true;
        }
    } else if (!Key->text().isEmpty() && (Plain || ShiftOnly)) {
        // Bug 514 v2: a reference's visible text is read-only.
        // Typing into it breaks the reference, so the key is
        // swallowed and the hint says to Rename instead.
        int LinkStart = -1;
        if (CursorTouchesLinkText(_TextEdit->document(), Cursor,
                                  LinkStart)) {
            ShowLinkReadOnlyHint(LinkStart);
            return true;
        }
        // Bug 503: an index entry marker is read-only the same
        // way. Typing into it breaks the entry, so the key is
        // swallowed and the hint says to Edit index entry.
        int IrefStart = -1;
        if (CursorTouchesIref(_TextEdit->document(), Cursor,
                              IrefStart)) {
            ShowIrefReadOnlyHint(IrefStart);
            return true;
        }
        if (Cursor.hasSelection() &&
            (SelectionCoversOptionMarker(_TextEdit->document(),
                                         Cursor) ||
             SelectionCoversSrcPlaceholder(_TextEdit->document(),
                                           Cursor))) {
            // The key replaces the selection: take the text,
            // keep the markers, and let the key insert at the
            // collapsed cursor below.
            RemoveSelectionKeepingDisplayMarks(Cursor);
            _TextEdit->setTextCursor(Cursor);
            Cursor = _TextEdit->textCursor();
            Block = Cursor.block();
        }
        if (!Cursor.hasSelection()) {
            // The cursor inside the placeholder text would
            // split it on the first typed character, and the
            // sweep would then clear the broken pieces as
            // stale (Bug 471): the placeholder is view-only,
            // so the key is swallowed and a hint says why.
            // At its start the character lands before it, so
            // only the start is left open; the interior and
            // the end are both refused, since a character at
            // the end would land after it and the sweep would
            // clear the placeholder as stale (Bug 469).
            for (QTextBlock::iterator It = Block.begin();
                 !It.atEnd(); ++It) {
                QTextFragment Fragment = It.fragment();
                if (!Fragment.charFormat()
                         .property(DocSrcPlaceholderProperty)
                         .isValid()) {
                    continue;
                }
                int Pos = Cursor.position();
                if (Pos > Fragment.position() &&
                    Pos <= Fragment.position() + Fragment.length()) {
                    ShowSrcPlaceholderHint(Block.position());
                    return true;
                }
                break;
            }
        }
        QTextCharFormat CharFormat = Cursor.charFormat();
        if (CharFormat.property(DocOptionMarkerProperty).isValid() ||
            CharFormat.property(DocSrcPlaceholderProperty).isValid()) {
            QTextCharFormat Fixed =
                IsSourceCodeBlock(Block)
                    ? CodeCharFormat()
                    : FormatForStack(StackOf(CharFormat));
            Cursor.setCharFormat(Fixed);
            _TextEdit->setTextCursor(Cursor);
        }
        // Bug 514: typing over a selection that sits entirely
        // inside one link must keep the link. The default insert
        // takes the format of the character before the cursor,
        // which is plain text when the selection was made
        // right-to-left, so the replacement text loses the link
        // and the reference is destroyed. Insert it here with
        // the link's own format instead.
        if (Cursor.hasSelection()) {
            QTextCharFormat LinkFormat = LinkFormatForSelection(
                _TextEdit->document(), Cursor);
            if (LinkFormat.isValid()) {
                Cursor.beginEditBlock();
                Cursor.removeSelectedText();
                Cursor.insertText(Key->text(), LinkFormat);
                Cursor.endEditBlock();
                _TextEdit->setTextCursor(Cursor);
                return true;
            }
        }
    }
    // A deletion key that was not swallowed by a branch above
    // reaches the edit: the empty sourcecode element it leaves
    // behind goes once the deletion has landed (Bug 469). The
    // check is queued here, before the later branches -- a plain
    // deletion key returns from the Enter section below without
    // reaching the end of this function, so a queue point there
    // would never fire for it. Keys swallowed above already
    // queued their own check and returned, so nothing is queued
    // twice; a swallowed key's queued check is harmless, since
    // the cleanup re-checks the element after the (non-)deletion.
    if (Code == Qt::Key_Backspace || Code == Qt::Key_Delete ||
        (ControlHeld && Code == Qt::Key_X)) {
        QueueEmptySourceCleanup(this, _TextEdit->textCursor());
    }
    if (IsPlaceholderBlock(Block)) {
        // An SVG version shows as one placeholder line: keys
        // that would change the text are swallowed, so the
        // preserved artwork cannot be typed in. Navigation,
        // selection, copying, and undo still pass through.
        int KeyCode = Key->key();
        bool ControlOnly = Held == Qt::ControlModifier;
        if (ControlOnly && (KeyCode == Qt::Key_C ||
                            KeyCode == Qt::Key_Insert ||
                            KeyCode == Qt::Key_A)) {
            return QWidget::eventFilter(Watched, Event);
        }
        if (!Key->text().isEmpty() ||
            KeyCode == Qt::Key_Return || KeyCode == Qt::Key_Enter ||
            KeyCode == Qt::Key_Tab || KeyCode == Qt::Key_Backtab ||
            KeyCode == Qt::Key_Backspace || KeyCode == Qt::Key_Delete ||
            (ControlOnly &&
             (KeyCode == Qt::Key_X || KeyCode == Qt::Key_V))) {
            return true;
        }
        return QWidget::eventFilter(Watched, Event);
    }
    if (Cursor.currentTable() != nullptr &&
        (Plain || ShiftOnly) &&
        (Key->key() == Qt::Key_Tab || Key->key() == Qt::Key_Backtab)) {
        // Tab moves between cells: Qt does not navigate a
        // table on Tab by itself, it inserts a tab character,
        // so the move is done here. At the first and last cells
        // the move refuses and the cursor stays: no row is
        // ever added by Tab.
        if (Key->key() == Qt::Key_Tab) {
            Cursor.movePosition(QTextCursor::NextCell);
        } else {
            Cursor.movePosition(QTextCursor::PreviousCell);
        }
        _TextEdit->setTextCursor(Cursor);
        return true;
    }
    if (Cursor.hasSelection() &&
        SelectionPartlyCrossesTable(_TextEdit->document(), Cursor)) {
        // A selection crossing a table boundary, without
        // covering the frame whole or sitting inside it, would
        // delete the whole table if it were replaced: Qt
        // removes a frame when a text change crosses its
        // boundary. Keys that would replace it are swallowed,
        // so the table cannot be lost to a stray keypress.
        // Copying, selecting all, and undo still pass through.
        int KeyCode = Key->key();
        bool ControlOnly = Held == Qt::ControlModifier;
        if (KeyCode == Qt::Key_Backspace || KeyCode == Qt::Key_Delete ||
            KeyCode == Qt::Key_Return || KeyCode == Qt::Key_Enter ||
            KeyCode == Qt::Key_Tab || KeyCode == Qt::Key_Backtab ||
            (!Key->text().isEmpty() && (Plain || ShiftOnly)) ||
            (ControlOnly &&
             (KeyCode == Qt::Key_X || KeyCode == Qt::Key_V))) {
            return true;
        }
    }
    if (IsSrcPlaceholderBlock(Block) &&
        (Plain || ShiftOnly) &&
        (Key->key() == Qt::Key_Return || Key->key() == Qt::Key_Enter ||
         Key->key() == Qt::Key_Tab || Key->key() == Qt::Key_Backtab)) {
        // The block holds no text to break or indent: Enter
        // would split it into a second empty line that reads
        // back as content alongside src, and Tab would indent
        // nothing into four spaces of content. Both keys are
        // swallowed and a hint says why, since the line is
        // view-only.
        ShowSrcPlaceholderHint(Block.position());
        return true;
    }
    if (IsSourceCodeBlock(Block) || IsFigureArtworkBlock(Block)) {
        if (Plain && Key->key() == Qt::Key_Tab) {
            // Tabs are prohibited inside <sourcecode> and
            // <artwork>, so the Tab key inserts spaces instead.
            Cursor.beginEditBlock();
            Cursor.insertText(QStringLiteral("    "));
            Cursor.endEditBlock();
            _TextEdit->setTextCursor(Cursor);
            return true;
        }
        if ((Plain || ShiftOnly) &&
            (Key->key() == Qt::Key_Return || Key->key() == Qt::Key_Enter)) {
            // Enter and Shift+Enter in a code or artwork block
            // both make a new line, handled here so the key
            // never reaches the list branches below: a block
            // carrying stale list properties would otherwise
            // lose the key to the list-exit branch. Lines are
            // blocks here, so both keys make a hard break. The
            // new block is taken out of any list it inherited,
            // or Qt would draw a marker on it and eat a later
            // Enter on the empty line.
            Cursor.beginEditBlock();
            Cursor.insertBlock();
            if (QTextList* List = Cursor.currentList()) {
                List->remove(Cursor.block());
            }
            Cursor.setCharFormat(CodeCharFormat());
            _TextEdit->setTextCursor(Cursor);
            Cursor.endEditBlock();
            return true;
        }
    }
    if (IsFigureCaptionBlock(Block) && Plain &&
        (Key->key() == Qt::Key_Return || Key->key() == Qt::Key_Enter)) {
        // A caption is one line: Enter moves to the artwork
        // lines instead of splitting it. When the region has no
        // artwork block yet, one is made after the caption.
        // Shift+Enter falls through, and its soft break reads
        // back as a newline.
        QVector<QTextBlock> Region = FigureRegionBlocks(Block);
        QTextBlock FirstArt;
        for (const QTextBlock& RegionBlock : Region) {
            if (FigureRoleOf(RegionBlock) != QLatin1String("caption")) {
                FirstArt = RegionBlock;
                break;
            }
        }
        Cursor.beginEditBlock();
        if (FirstArt.isValid()) {
            Cursor.setPosition(FirstArt.position());
        } else {
            Cursor.setPosition(Block.position());
            Cursor.movePosition(QTextCursor::EndOfBlock);
            Cursor.insertBlock();
            SetFigureProperties(Cursor, FigureKindOf(Block),
                                Block.blockFormat()
                                    .property(DocFigureGroupProperty)
                                    .toInt(),
                                QStringLiteral("artwork"),
                                FigurePayloadOrDefault(Block));
            Cursor.setCharFormat(CodeCharFormat());
        }
        _TextEdit->setTextCursor(Cursor);
        Cursor.endEditBlock();
        return true;
    }
    if (Cursor.currentTable() != nullptr && (Plain || ShiftOnly) &&
        (Key->key() == Qt::Key_Return || Key->key() == Qt::Key_Enter)) {
        // A cell holds one paragraph: Enter and Shift+Enter
        // both insert a line break marker instead of splitting
        // the cell block, so the break writes back as a <br>.
        // A selection inside the cell is replaced by the break.
        // The marker takes the bold overlay in a header cell.
        Cursor.beginEditBlock();
        if (Cursor.hasSelection()) {
            Cursor.removeSelectedText();
        }
        DocTagEntry Entry;
        Entry.Name = QStringLiteral("br");
        DocTagStack Stack = StackOf(Cursor.charFormat());
        QTextCharFormat MarkerFormat = FormatForMarker(Stack, Entry);
        if (IsHeaderCellBlock(Cursor.block())) {
            MarkerFormat.setFontWeight(QFont::Bold);
        }
        Cursor.insertText(
            MarkerText(Entry,
                       DisplayNamesForDocument(_TextEdit->document())),
            MarkerFormat);
        Cursor.endEditBlock();
        _TextEdit->setTextCursor(Cursor);
        return true;
    }
    if (IsTableCaptionBlock(Block) && Plain &&
        (Key->key() == Qt::Key_Return || Key->key() == Qt::Key_Enter)) {
        // A caption is one line: Enter moves to the first cell
        // instead of splitting it, as for a figure caption.
        // Shift+Enter falls through, and its soft break reads
        // back as a newline. A caption with no frame after it is
        // an orphan, and Enter splits it like any paragraph.
        QTextBlock Next = Block.next();
        QTextTable* Frame =
            Next.isValid() ? TableOfBlock(_TextEdit->document(), Next)
                           : nullptr;
        if (Frame != nullptr) {
            Cursor.beginEditBlock();
            Cursor.setPosition(
                Frame->cellAt(0, 0).firstCursorPosition().position());
            _TextEdit->setTextCursor(Cursor);
            Cursor.endEditBlock();
            return true;
        }
    }
    if (Key->key() != Qt::Key_Return && Key->key() != Qt::Key_Enter) {
        return QWidget::eventFilter(Watched, Event);
    }
    if (!Plain && !ShiftOnly) {
        return QWidget::eventFilter(Watched, Event);
    }
    QString Kind = Block.blockFormat().property(DocListKindProperty).toString();
    if (Kind != QLatin1String("dl") && Kind != QLatin1String("ul") &&
        Kind != QLatin1String("ol")) {
        if (!Plain) {
            return QWidget::eventFilter(Watched, Event);
        }
        // Enter in a plain paragraph (Bug 461) makes a new
        // line in the same paragraph: a soft break, which
        // reads back as a newline and shows again on load,
        // so what is typed is what reloads. Only Enter on an
        // empty line starts a new paragraph: the soft breaks
        // bounding the blank line are removed and the block
        // splits. Lists, code, artwork, captions, and cells
        // are handled above and keep their own behavior.
        Cursor.beginEditBlock();
        if (Cursor.hasSelection()) {
            Cursor.removeSelectedText();
            Block = Cursor.block();
        }
        QString Text = Block.text();
        int Offset = Cursor.position() - Block.position();
        int LineStart = 0;
        if (Offset > 0) {
            LineStart = Text.lastIndexOf(SoftBreakChar, Offset - 1) + 1;
        }
        int LineEnd = Text.indexOf(SoftBreakChar, Offset);
        if (LineEnd < 0) {
            LineEnd = Text.size();
        }
        if (Text.mid(LineStart, Offset - LineStart).trimmed().isEmpty() &&
            Text.mid(Offset, LineEnd - Offset).trimmed().isEmpty()) {
            int Position = Cursor.position();
            if (Offset < Text.size() &&
                Text.at(Offset) == SoftBreakChar) {
                Cursor.setPosition(Position);
                Cursor.setPosition(Position + 1, QTextCursor::KeepAnchor);
                Cursor.removeSelectedText();
            }
            if (Offset > 0 &&
                Text.at(Offset - 1) == SoftBreakChar) {
                Cursor.setPosition(Position - 1);
                Cursor.setPosition(Position, QTextCursor::KeepAnchor);
                Cursor.removeSelectedText();
            }
            Cursor.insertBlock();
        } else {
            Cursor.insertText(QString(SoftBreakChar));
        }
        _TextEdit->setTextCursor(Cursor);
        Cursor.endEditBlock();
        // A new paragraph at the end of a long document makes
        // Qt jump the view to the top (Bug 481): bring it back
        // to the cursor, which is where the typing goes.
        _TextEdit->ensureCursorVisible();
        return true;
    }
    QString Role = ListRoleOf(Block);
    QTextDocument* Document = _TextEdit->document();
    bool Empty = Block.text().trimmed().isEmpty();
    int Group = Block.blockFormat().property(DocListGroupProperty).toInt();
    QVector<QPair<QString, QString>> Attributes = AttributesFromVariant(
        Block.blockFormat().property(DocListProperty));

    if (Kind == QLatin1String("dl")) {
        if (Plain && Role == QLatin1String("term") && Empty) {
            // Enter in an empty term leaves the list: the block
            // turns plain, and no new block is made, matching
            // how Enter leaves a bulleted or numbered list. The
            // block is taken out of any list object too, or the
            // next Enter on it would be eaten by the edit's own
            // empty-item list exit.
            if (QTextList* List = Block.textList()) {
                List->remove(Block);
            }
            QTextBlockFormat Format = Block.blockFormat();
            Format.setIndent(0);
            Format.clearProperty(QTextFormat::BackgroundBrush);
            Format.clearProperty(DocListProperty);
            Format.clearProperty(DocListKindProperty);
            Format.clearProperty(DocListGroupProperty);
            Format.clearProperty(DocListRoleProperty);
            Format.setBottomMargin(ParagraphGap(Document));
            Cursor.setBlockFormat(Format);
            return true;
        }
        if (Plain && Role == QLatin1String("definition") && Empty) {
            // Enter in an empty definition starts the next
            // term: the empty block becomes a term, with a
            // fresh empty definition after it.
            Cursor.beginEditBlock();
            QTextBlockFormat Format = Block.blockFormat();
            Format.setProperty(DocListRoleProperty, QStringLiteral("term"));
            Cursor.setBlockFormat(Format);
            Cursor.movePosition(QTextCursor::EndOfBlock);
            Cursor.insertBlock();
            SetListProperties(Cursor, Kind, Group, Attributes,
                              QStringLiteral("definition"));
            Cursor.setCharFormat(QTextCharFormat());
            StyleDefinitionGroup(Document, Cursor.block());
            Cursor.setPosition(Block.position());
            QTextCharFormat Typing = Cursor.charFormat();
            ApplyStackVisuals(Typing, StackOf(Typing));
            Typing.setFontWeight(QFont::Bold);
            Cursor.setCharFormat(Typing);
            _TextEdit->setTextCursor(Cursor);
            Cursor.endEditBlock();
            return true;
        }
        if (Plain && Role == QLatin1String("definition")) {
            // Enter in a definition holding text starts the
            // next term, after the last paragraph of this
            // definition.
            QTextBlock Last = Block;
            while (Last.next().isValid() &&
                   Last.next().blockFormat().property(DocListKindProperty)
                           .toString() == QLatin1String("dl") &&
                   Last.next().blockFormat().property(DocListGroupProperty)
                           .toInt() == Group &&
                   ListRoleOf(Last.next()) == QLatin1String("definition")) {
                Last = Last.next();
            }
            Cursor.beginEditBlock();
            Cursor.setPosition(Last.position());
            Cursor.movePosition(QTextCursor::EndOfBlock);
            Cursor.insertBlock();
            SetListProperties(Cursor, Kind, Group, Attributes,
                              QStringLiteral("term"));
            QTextCharFormat TermTyping = Cursor.charFormat();
            ApplyStackVisuals(TermTyping, StackOf(TermTyping));
            TermTyping.setFontWeight(QFont::Bold);
            Cursor.setCharFormat(TermTyping);
            QTextBlock TermBlock = Cursor.block();
            Cursor.movePosition(QTextCursor::EndOfBlock);
            Cursor.insertBlock();
            SetListProperties(Cursor, Kind, Group, Attributes,
                              QStringLiteral("definition"));
            Cursor.setCharFormat(QTextCharFormat());
            StyleDefinitionGroup(Document, TermBlock);
            Cursor.setPosition(TermBlock.position());
            QTextCharFormat Typing = Cursor.charFormat();
            ApplyStackVisuals(Typing, StackOf(Typing));
            Typing.setFontWeight(QFont::Bold);
            Cursor.setCharFormat(Typing);
            _TextEdit->setTextCursor(Cursor);
            Cursor.endEditBlock();
            return true;
        }
        if ((Plain && Role == QLatin1String("term")) ||
            (ShiftOnly && Role == QLatin1String("definition"))) {
            // Enter in a term starts its definition; Shift+Enter
            // in a definition adds a paragraph to it. Either
            // way the new block is a definition paragraph.
            Cursor.beginEditBlock();
            Cursor.insertBlock();
            SetListProperties(Cursor, Kind, Group, Attributes,
                              QStringLiteral("definition"));
            StyleDefinitionGroup(Document, Cursor.block());
            QTextCharFormat Typing = Cursor.charFormat();
            ApplyStackVisuals(Typing, StackOf(Typing));
            Cursor.setCharFormat(Typing);
            _TextEdit->setTextCursor(Cursor);
            Cursor.endEditBlock();
            return true;
        }
        // Shift+Enter in a term keeps the edit's own soft break.
        return QWidget::eventFilter(Watched, Event);
    }

    if (ShiftOnly) {
        // Shift+Enter adds a paragraph to the same item. The new
        // block sits outside the list object, indented to line
        // up with the item text, or Qt would give it its own
        // bullet or number.
        Cursor.beginEditBlock();
        Cursor.insertBlock();
        if (QTextList* Joined = Cursor.currentList()) {
            Joined->remove(Cursor.block());
        }
        SetListProperties(Cursor, Kind, Group, Attributes,
                          QStringLiteral("continuation"));
        QTextBlockFormat Indented = Cursor.blockFormat();
        Indented.setIndent(1);
        Indented.clearProperty(QTextFormat::BackgroundBrush);
        Cursor.setBlockFormat(Indented);
        QTextCharFormat Typing = Cursor.charFormat();
        ApplyStackVisuals(Typing, StackOf(Typing));
        Cursor.setCharFormat(Typing);
        _TextEdit->setTextCursor(Cursor);
        Cursor.endEditBlock();
        return true;
    }
    if (Plain && Role == QLatin1String("continuation")) {
        // Enter in a continuation paragraph starts the next
        // item: the new block joins the list, rebuilt so the
        // numbers follow the document.
        QTextList* List = nullptr;
        for (QTextBlock Prev = Block.previous(); Prev.isValid();
             Prev = Prev.previous()) {
            if (Prev.blockFormat().property(DocListKindProperty).toString() !=
                    Kind ||
                Prev.blockFormat().property(DocListGroupProperty).toInt() !=
                    Group) {
                break;
            }
            if (Prev.textList() != nullptr) {
                List = Prev.textList();
                break;
            }
        }
        if (List == nullptr) {
            return QWidget::eventFilter(Watched, Event);
        }
        Cursor.beginEditBlock();
        Cursor.insertBlock();
        SetListProperties(Cursor, Kind, Group, Attributes,
                          QStringLiteral("item"));
        QTextBlockFormat Format = Cursor.blockFormat();
        Format.setIndent(0);
        Format.clearProperty(QTextFormat::BackgroundBrush);
        Cursor.setBlockFormat(Format);
        RebuildList(Document, List, Cursor.block());
        QTextCharFormat Typing = Cursor.charFormat();
        ApplyStackVisuals(Typing, StackOf(Typing));
        Cursor.setCharFormat(Typing);
        _TextEdit->setTextCursor(Cursor);
        Cursor.endEditBlock();
        return true;
    }
    // Enter in an item keeps the edit's own handling: a new item.
    return QWidget::eventFilter(Watched, Event);
}

TextEditor::~TextEditor()
{
    FreeOwnedNodes();
}

void TextEditor::FreeOwnedNodes()
{
    for (xmlNodePtr Node : _BodyNodes) {
        xmlFreeNode(Node);
    }
    _BodyNodes.clear();
    for (const DocPreservedItem& Item : _PreservedNodes) {
        xmlFreeNode(Item.Node);
    }
    _PreservedNodes.clear();
}

void TextEditor::SetBodyNodes(const QVector<xmlNodePtr>& Nodes)
{
    QSignalBlocker Blocker(this);
    _SettingParagraphs = true;
    FreeOwnedNodes();
    for (xmlNodePtr Node : Nodes) {
        _BodyNodes.append(DocDocument::CopyNode(Node));
    }
    _TextEdit->clear();
    // The display names ride on the text document from here on,
    // so every marker built below, and every marker read back
    // from this text, shows the mapped name (Bug 405).
    _TextEdit->document()->setProperty(
        DisplayNamesProperty, DisplayNamesFromDocument(_Document));
    QTextCursor Cursor(_TextEdit->document());
    Cursor.beginEditBlock();
    // The first block is a plain paragraph until a structural
    // node claims it, so it starts with the paragraph
    // spacing; a structural setter below replaces it with none.
    // With no nodes at all, the empty block keeps the
    // spacing, so typed paragraphs inherit it.
    QTextBlockFormat FirstFormat = Cursor.blockFormat();
    FirstFormat.setBottomMargin(ParagraphGap(Cursor.document()));
    Cursor.setBlockFormat(FirstFormat);
    bool First = true;
    for (xmlNodePtr Node : Nodes) {
        if (!First) {
            InsertParagraphBreak(Cursor);
        }
        First = false;
        if (Node->type == XML_ELEMENT_NODE) {
            if (DocDocument::NodeNameEquals(Node, "figure") ||
                DocDocument::NodeNameEquals(Node, "artset") ||
                DocDocument::NodeNameEquals(Node, "artwork")) {
                InsertFigureNode(Cursor, Node);
            } else if (DocDocument::NodeNameEquals(Node, "table")) {
                InsertTableNode(Cursor, Node);
            } else if (IsListNode(Node)) {
                InsertListNode(Cursor, Node);
            } else if (IsSourceNode(Node)) {
                InsertSourceNode(Cursor, Node);
            } else {
                InsertNodeRuns(Cursor, Node);
            }
        } else if (Node->type == XML_COMMENT_NODE) {
            // A comment body node shows as its marker text,
            // as a comment run inside a paragraph does.
            QString CommentText = QString::fromUtf8(
                reinterpret_cast<const char*>(Node->content));
            CommentText.replace(QLatin1Char('\n'), QLatin1Char(' '));
            Cursor.insertText(QStringLiteral("<!--") + CommentText +
                              QStringLiteral("-->"));
        } else {
            InsertNodeRuns(Cursor, Node);
        }
    }
    EnsureTrailingPlainParagraph();
    Cursor.endEditBlock();
    // Bug 517: the sweep runs after the edit block closes.
    // A table inserted inside an open edit block has no
    // frame yet for QTextCursor::currentTable(), so the sweep
    // could not find any cell's row and column and skipped
    // every cell gear, leaving only the caption gear. Once
    // the block closes, currentTable() answers, and the cell
    // gears are inserted too.
    UpdateOptionMarkers();
    Cursor.setPosition(0);
    _TextEdit->setTextCursor(Cursor);
    _SettingParagraphs = false;
    RefreshXmlTab();
    UpdateBlockButtons();
}

void TextEditor::SetPreservedNodes(const QVector<DocPreservedItem>& Items)
{
    for (const DocPreservedItem& Item : _PreservedNodes) {
        xmlFreeNode(Item.Node);
    }
    _PreservedNodes.clear();
    for (const DocPreservedItem& Item : Items) {
        DocPreservedItem Copy;
        Copy.AfterNodes = Item.AfterNodes;
        Copy.Node = DocDocument::CopyNode(Item.Node);
        _PreservedNodes.append(Copy);
    }
    RefreshXmlTab();
}

void TextEditor::SetAllowSubsections(bool Allow)
{
    _AllowSubsections = Allow;
}

bool TextEditor::XmlEditsPending() const
{
    return _XmlDirty;
}

bool TextEditor::ApplyXmlEdits()
{
    if (!XmlEditsPending()) {
        return true;
    }
    QVector<xmlNodePtr> Parsed;
    QString Error;
    if (!ParseXmlFragment(_XmlEdit->toPlainText(), Parsed, Error)) {
        // The error is shown and the box stays unchecked, so the
        // XML can be fixed; the model is untouched, so a bad
        // paste cannot corrupt the document or drop edits.
        QMessageBox::warning(
            this, tr("XML error"),
            tr("The XML could not be applied:\n%1\n\nFix the XML "
               "and re-check Read-only to try again. The document "
               "is unchanged.").arg(Error));
        for (xmlNodePtr Node : Parsed) {
            xmlFreeNode(Node);
        }
        return false;
    }
    QVector<xmlNodePtr> Body;
    QVector<xmlNodePtr> Sections;
    for (xmlNodePtr Node : Parsed) {
        QString Name = QString::fromUtf8(
            reinterpret_cast<const char*>(Node->name));
        if (Name == QLatin1String("section")) {
            Sections.append(Node);
        } else {
            Body.append(Node);
        }
    }
    if (!Sections.isEmpty() && !_AllowSubsections) {
        QMessageBox::warning(
            this, tr("Sections cannot be pasted here"),
            tr("A <section> cannot be pasted into this XML view. "
               "Sections belong in a section body, not here. "
               "Nothing was applied."));
        for (xmlNodePtr Node : Parsed) {
            xmlFreeNode(Node);
        }
        return false;
    }
    SetBodyNodes(Body);
    if (!Sections.isEmpty()) {
        // The owner copies the sections into the document
        // synchronously; the editor frees its copies after the
        // signal returns.
        emit SubsectionsPasted(Sections);
    }
    for (xmlNodePtr Node : Parsed) {
        xmlFreeNode(Node);
    }
    _XmlDirty = false;
    if (_XmlReadOnlyCheck != nullptr) {
        QSignalBlocker Blocker(_XmlReadOnlyCheck);
        _XmlReadOnlyCheck->setChecked(true);
    }
    _XmlEdit->setReadOnly(true);
    // SetBodyNodes fills without sending TextChanged, so the
    // edit is announced here: the owner writes the new body
    // into the document and marks it modified.
    emit TextChanged();
    RefreshXmlTab();
    return true;
}

QVector<xmlNodePtr> TextEditor::BodyNodes() const
{
    QVector<xmlNodePtr> Nodes;
    QTextDocument* Document = _TextEdit->document();
    QTextBlock Block = Document->begin();
    while (Block.isValid()) {
        if (IsTableCaptionBlock(Block)) {
            // A caption block and the frame after it are one
            // table node: the caption runs come from the
            // caption block, and the table from the frame. A
            // caption block with no frame after it is an
            // orphan -- its table was deleted out from under
            // it -- and reads back as a plain paragraph.
            QTextBlock Next = Block.next();
            QTextTable* Frame =
                Next.isValid() ? TableOfBlock(Document, Next) : nullptr;
            if (Frame == nullptr) {
                xmlNodePtr Node = NodeFromBlock(Block);
                if (!NodeIsBlank(Node)) {
                    Nodes.append(Node);
                } else {
                    xmlFreeNode(Node);
                }
                Block = Block.next();
                continue;
            }
            xmlNodePtr CaptionNode = NodeFromBlock(Block);
            xmlNodePtr TableNode = TableFromFrame(Frame, CaptionNode);
            xmlFreeNode(CaptionNode);
            Nodes.append(TableNode);
            Block = Document->findBlock(Frame->lastPosition()).next();
            continue;
        }
        if (QTextTable* Frame = TableOfBlock(Document, Block)) {
            // A frame reached without a caption block is a bare
            // table: it reads back with an empty caption. The
            // walk skips the whole frame, so no cell block is
            // ever read as a node of its own.
            Nodes.append(TableFromFrame(Frame, nullptr));
            Block = Document->findBlock(Frame->lastPosition()).next();
            continue;
        }
        if (IsFigureBlock(Block)) {
            // Consecutive blocks of one figure region, sharing
            // a kind and a group, are one figure node. The
            // caption blocks contribute the <name>; the artwork
            // blocks contribute the text of the version showing;
            // the other versions, and the preserved SVG
            // elements, come from the region payload. An SVG
            // version showing keeps its preserved element: the
            // placeholder line is display only. Only a figure
            // carries a <name>: an artset or artwork region
            // drops its caption, as the old writer did.
            QString FigureKindName = FigureKindOf(Block);
            int Group = Block.blockFormat()
                            .property(DocFigureGroupProperty)
                            .toInt();
            xmlNodePtr FigureNode = nullptr;
            int Visible = 0;
            bool HavePayload = FigurePayloadOf(Block, FigureNode, Visible);
            QTextBlock FirstBlock = Block;
            QVector<QTextBlock> Region;
            while (Block.isValid() &&
                   FigureKindOf(Block) == FigureKindName &&
                   Block.blockFormat()
                           .property(DocFigureGroupProperty)
                           .toInt() == Group) {
                Region.append(Block);
                Block = Block.next();
            }
            if (!HavePayload) {
                FigureFromVariant(FigurePayloadOrDefault(FirstBlock),
                                  FigureNode, Visible);
            }
            if (FigureNode == nullptr) {
                continue;
            }
            QStringList Lines;
            xmlNodePtr NameNode = nullptr;
            bool FirstCaption = true;
            for (const QTextBlock& RegionBlock : Region) {
                if (FigureRoleOf(RegionBlock) ==
                    QLatin1String("caption")) {
                    if (FigureKindName != QLatin1String("figure")) {
                        // An artset or artwork region drops its
                        // caption, as the old writer did: only a
                        // figure carries a <name>. The caption
                        // text is not artwork text, so it must
                        // not join the lines below.
                        continue;
                    }
                    xmlNodePtr CaptionNode = NodeFromBlock(RegionBlock);
                    if (NameNode == nullptr) {
                        NameNode = NewNode("name");
                    } else if (!FirstCaption &&
                               CaptionNode->children != nullptr) {
                        AddTextChild(NameNode, QStringLiteral(" "));
                    }
                    for (xmlNodePtr Child = CaptionNode->children;
                         Child != nullptr;) {
                        xmlNodePtr Next = Child->next;
                        xmlUnlinkNode(Child);
                        xmlAddChild(NameNode, Child);
                        Child = Next;
                    }
                    xmlFreeNode(CaptionNode);
                    FirstCaption = false;
                } else {
                    Lines.append(CodeTextOf(RegionBlock));
                }
            }
            if (NameNode != nullptr) {
                if (NameNode->children != nullptr) {
                    // The payload copy may carry a stale <name>
                    // from the loaded document: the caption
                    // blocks above are the caption now.
                    xmlNodePtr StaleName =
                        DocDocument::ChildElement(FigureNode, "name");
                    if (StaleName != nullptr) {
                        xmlUnlinkNode(StaleName);
                        xmlFreeNode(StaleName);
                    }
                    if (FigureNode->children != nullptr) {
                        xmlAddPrevSibling(FigureNode->children, NameNode);
                    } else {
                        xmlAddChild(FigureNode, NameNode);
                    }
                } else {
                    xmlFreeNode(NameNode);
                }
            }
            QVector<xmlNodePtr> Versions =
                FigureVersionNodes(FigureNode);
            if (Visible >= 0 && Visible < Versions.size() &&
                !ArtworkNodeIsSvg(Versions[Visible])) {
                DocDocument::SetNodeText(
                    Versions[Visible], Lines.join(QLatin1Char('\n')));
            }
            Nodes.append(FigureNode);
            continue;
        }
        QVariant SourceValue =
            Block.blockFormat().property(DocSourceProperty);
        if (IsSourceCodeBlock(Block)) {
            // Consecutive code blocks with the same attributes
            // are one sourcecode element; each block is one line
            // of it, blank lines included.
            xmlNodePtr SourceNode = NewNode("sourcecode");
            SetNodeAttributes(SourceNode,
                              AttributesFromVariant(SourceValue));
            QStringList Lines;
            while (Block.isValid() && IsSourceCodeBlock(Block) &&
                   Block.blockFormat().property(DocSourceProperty) ==
                       SourceValue) {
                Lines.append(CodeTextOf(Block));
                Block = Block.next();
            }
            QString Text = Lines.join(QLatin1Char('\n'));
            if (!Text.isEmpty()) {
                AddTextChild(SourceNode, Text);
            }
            if (!NodeIsBlank(SourceNode)) {
                Nodes.append(SourceNode);
            } else {
                xmlFreeNode(SourceNode);
            }
            continue;
        }
        QTextList* List = Block.textList();
        QVariant KindValue = Block.blockFormat().property(DocListKindProperty);
        // A block belongs to a bulleted, numbered, or inline list
        // when it sits in a list; a definition list has no list,
        // so its kind alone claims the block. A block that left
        // its list keeps a stale kind but no list, and reads back
        // as a plain paragraph.
        bool InList = List != nullptr;
        bool InDefinition = List == nullptr &&
                            KindValue.toString() == QLatin1String("dl");
        if (!InList && !InDefinition) {
            // A blank paragraph is dropped: the blank rule
            // keeps blank lines out of the saved body. The
            // empty paragraph the editor keeps after a trailing
            // sourcecode block is an editing convenience, not
            // content, and it drops with the rest.
            xmlNodePtr Node = NodeFromBlock(Block);
            if (!NodeIsBlank(Node)) {
                Nodes.append(Node);
            } else {
                xmlFreeNode(Node);
            }
            Block = Block.next();
            continue;
        }
        // Consecutive blocks in the same list, with the same kind,
        // group, and attributes, are one list node. A nested
        // list (made by indenting an item) comes out flat, as
        // sibling items. A continuation block sits outside the
        // list object but belongs to the item above it, so it
        // joins the walk when its kind, group, and attributes
        // match. A blank line between items is a plain empty
        // block with no list properties, and it joins the walk
        // when the items around it belong to this list, so the
        // list does not split around a blank line. A definition
        // list's blocks are told apart by their roles, never by
        // position.
        QString Kind = KindValue.toString();
        if (Kind != QLatin1String("ul") && Kind != QLatin1String("ol") &&
            Kind != QLatin1String("dl") && Kind != QLatin1String("list")) {
            Kind = QStringLiteral("list");
        }
        QVariant AttrsValue = Block.blockFormat().property(DocListProperty);
        QVariant GroupValue = Block.blockFormat().property(DocListGroupProperty);
        QVector<DocListBuildItem> Items;
        while (Block.isValid()) {
            QTextBlockFormat Format = Block.blockFormat();
            // A blank line typed between items (Enter twice) is a
            // plain empty block with no list properties: Qt drops
            // them when the empty item leaves the list. It belongs
            // to the list when the next non-blank block after it
            // does, so the list does not split around a blank
            // line. A blank line that is not between items of
            // this list ends the list and reads back as a blank
            // paragraph instead.
            if (Block.text().trimmed().isEmpty() &&
                !IsSourceCodeBlock(Block) &&
                !Format.property(DocFigureKindProperty).isValid() &&
                !Format.property(DocTableKindProperty).isValid() &&
                Block.textList() == nullptr &&
                ListRoleOf(Block) != QLatin1String("continuation")) {
                QTextBlock Next = Block.next();
                while (Next.isValid() && Next.text().trimmed().isEmpty() &&
                       !IsSourceCodeBlock(Next) &&
                       !Next.blockFormat().property(DocFigureKindProperty).isValid() &&
                       !Next.blockFormat().property(DocTableKindProperty).isValid() &&
                       Next.textList() == nullptr &&
                       ListRoleOf(Next) != QLatin1String("continuation")) {
                    Next = Next.next();
                }
                if (Next.isValid() &&
                    ListBlockBelongs(Next, List, KindValue, GroupValue, AttrsValue,
                                     InList)) {
                    DocListBuildItem Item;
                    Item.TNode = NodeFromBlock(Block);
                    Item.Continuation = false;
                    Items.append(Item);
                    Block = Block.next();
                    continue;
                }
                break;
            }
            if (Format.property(DocListKindProperty) != KindValue ||
                Format.property(DocListGroupProperty) != GroupValue ||
                Format.property(DocListProperty) != AttrsValue) {
                break;
            }
            if (InList) {
                // In the list, or a continuation of an item in it.
                if (Block.textList() != List &&
                    ListRoleOf(Block) != QLatin1String("continuation")) {
                    break;
                }
            } else if (Block.textList() != nullptr) {
                break;
            }
            DocListBuildItem Item;
            Item.TNode = NodeFromBlock(Block);
            Item.Continuation = IsContinuationRole(ListRoleOf(Block));
            Items.append(Item);
            Block = Block.next();
        }
        if (Items.isEmpty()) {
            // The walk took no block: this is a blank list
            // block whose next non-blank block belongs to no
            // list, so it cannot start a list. It reads back
            // as a plain blank paragraph, and the walk moves
            // on. Without this, the outer walk meets the same
            // block again forever: Enter at the end of a
            // definition list makes an empty term, and the
            // read-back hangs (Bug 478).
            xmlNodePtr Node = NodeFromBlock(Block);
            if (!NodeIsBlank(Node)) {
                Nodes.append(Node);
            } else {
                xmlFreeNode(Node);
            }
            Block = Block.next();
            continue;
        }
        xmlNodePtr ListNode = BuildListNode(
            Kind, AttributesFromVariant(AttrsValue), Items);
        if (ListNode != nullptr && !ListNodeIsBlank(ListNode)) {
            Nodes.append(ListNode);
        } else {
            xmlFreeNode(ListNode);
        }
    }
    return Nodes;
}

void TextEditor::Clear()
{
    QSignalBlocker Blocker(this);
    _SettingParagraphs = true;
    _TextEdit->clear();
    _XmlEdit->clear();
    // Bug 520: a fresh fill starts read-only, with no pending
    // XML edits.
    _XmlDirty = false;
    if (_XmlReadOnlyCheck != nullptr) {
        QSignalBlocker CheckBlocker(_XmlReadOnlyCheck);
        _XmlReadOnlyCheck->setChecked(true);
    }
    if (_XmlPreservedNote != nullptr) {
        _XmlPreservedNote->setVisible(false);
    }
    _XmlEdit->setReadOnly(true);
    FreeOwnedNodes();
    _SettingParagraphs = false;
}

int TextEditor::BlockCount() const
{
    if (_TextEdit == nullptr || _TextEdit->document() == nullptr) {
        return 0;
    }
    return _TextEdit->document()->blockCount();
}

QString TextEditor::MarkerSweepReport() const
{
    return tr("Markers: setting %1, blocks scanned %2, first source "
              "blocks %3, gears inserted %4, gears removed %5, first "
              "list blocks %6.")
        .arg(_DiagMarkersShown ? tr("on") : tr("off"))
        .arg(_DiagMarkersBlocks)
        .arg(_DiagMarkersSourceFirst)
        .arg(_DiagMarkersGearsInserted)
        .arg(_DiagMarkersGearsRemoved)
        .arg(_DiagMarkersListFirst);
}

void TextEditor::OnTextDocumentChanged()
{
    if (_SettingParagraphs) {
        return;
    }
    // Any edit can misplace a marker -- typing ahead of the
    // gear, or an undo -- so the markers are re-checked after
    // every change. A clean document costs the sweep nothing:
    // it edits only when a marker is missing, stray, or stale.
    UpdateOptionMarkers();
    RefreshXmlTab();
    emit TextChanged();
}

void TextEditor::OnTabChanged(int Index)
{
    if (_Tabs->widget(Index) == _XmlPage) {
        RefreshXmlTab();
        return;
    }
    // Switching to the Text tab applies pending XML edits, so
    // the Text tab shows the pasted content and a later save
    // cannot lose it. On a parse error the switch is refused
    // and the user stays on the XML tab to fix the XML (Bug 520).
    if (XmlEditsPending() && !ApplyXmlEdits()) {
        QSignalBlocker Blocker(_Tabs);
        _Tabs->setCurrentWidget(_XmlPage);
    }
}

void TextEditor::OnXmlReadOnlyToggled(bool Checked)
{
    if (Checked) {
        // Re-checking applies the edits. On a parse error the
        // box is left unchecked so the XML can be fixed (Bug 520).
        if (!ApplyXmlEdits()) {
            QSignalBlocker Blocker(_XmlReadOnlyCheck);
            _XmlReadOnlyCheck->setChecked(false);
        }
        return;
    }
    // Unchecking opens the pane for editing: it becomes
    // editable and shows only the body nodes, so the re-parse
    // has no preserved content to separate out. Preserved
    // content is kept, and reappears when the box is re-checked.
    _XmlEdit->setReadOnly(false);
    _XmlDirty = false;
    RefreshXmlTab();
    _XmlEdit->setFocus();
}

void TextEditor::OnXmlTextChanged()
{
    // Programmatic fills block this signal; only a user edit
    // while the pane is editable marks the XML dirty (Bug 520).
    if (_XmlReadOnlyCheck != nullptr && !_XmlReadOnlyCheck->isChecked()) {
        _XmlDirty = true;
    }
}

void TextEditor::UpdateOptionMarkers()
{
    QTextDocument* Document = _TextEdit->document();
    bool Shown = OptionMarkersShown();
    // Bug 502 diagnostic: counts for the sweep report.
    _DiagMarkersShown = Shown;
    _DiagMarkersBlocks = 0;
    _DiagMarkersSourceFirst = 0;
    _DiagMarkersListFirst = 0;
    _DiagMarkersGearsInserted = 0;
    _DiagMarkersGearsRemoved = 0;
    // The sweep's own edits must not re-enter the change
    // handler, which would sweep again mid-walk.
    QSignalBlocker Blocker(_TextEdit);
    QTextCursor EditCursor = _TextEdit->textCursor();
    int CursorPos = EditCursor.position();
    int CursorAnchor = EditCursor.anchor();
    bool MovedText = false;
    QTextCursor Sweep(Document);
    Sweep.beginEditBlock();
    // Last block first, so an insertion or deletion never
    // moves a block this walk has yet to reach.
    for (QTextBlock Block = Document->lastBlock(); Block.isValid();
         Block = Block.previous()) {
        int BlockPos = Block.position();
        // The gears this block should carry, in order, each with
        // the format it wants. A sourcecode or list block carries
        // one; a table caption carries one; a table cell's first
        // block carries up to three, outer part first: section,
        // row, cell (Bug 448). A sourcecode block is never also
        // a list block, so the sourcecode check comes first.
        QVector<DocWantedGear> WantedGears;
        QVector<QPair<QString, QString>> Attributes;
        bool FirstSource = IsFirstSourceBlock(Block);
        bool FirstList = false;
        QString ListKind;
        if (!FirstSource && IsFirstListBlock(Block)) {
            ListKind = Block.blockFormat()
                           .property(DocListKindProperty)
                           .toString();
            // Every list kind gets a gear now (Bug 464 v2):
            // "list", "ul", "ol", and "dl" all carry options.
            FirstList = true;
        }
        if (FirstSource) {
            Attributes = AttributesFromVariant(
                Block.blockFormat().property(DocSourceProperty));
            DocWantedGear Gear;
            Gear.Kind = QStringLiteral("sourcecode");
            Gear.Attributes = Attributes;
            WantedGears.append(Gear);
        } else if (FirstList) {
            Attributes = AttributesFromVariant(
                Block.blockFormat().property(DocListProperty));
            DocWantedGear Gear;
            Gear.Kind = ListKind;
            Gear.Attributes = Attributes;
            WantedGears.append(Gear);
        } else {
            WantedGears = TableGearsForBlock(Document, Block);
        }
        bool First = !WantedGears.isEmpty();
        ++_DiagMarkersBlocks;
        if (FirstSource) {
            ++_DiagMarkersSourceFirst;
        }
        if (FirstList) {
            ++_DiagMarkersListFirst;
        }
        bool ShowGear = Shown && First;
        int WantedCount = ShowGear ? WantedGears.size() : 0;
        // The gears already in the block, with their kinds.
        // A gear is a gear character carrying the marker
        // property; its kind is the property's value.
        struct FoundGear {
            int Pos;
            QString Kind;
            QString ToolTip;
            bool Pure;
        };
        QVector<FoundGear> Found;
        for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
            QTextFragment Fragment = It.fragment();
            if (!Fragment.charFormat()
                     .property(DocOptionMarkerProperty)
                     .isValid()) {
                continue;
            }
            QString Text = Fragment.text();
            for (int i = 0; i < Text.size(); ++i) {
                if (Text.at(i) == OptionGlyph) {
                    FoundGear Gear;
                    Gear.Pos = Fragment.position() + i;
                    Gear.Kind = Fragment.charFormat()
                                    .property(DocOptionMarkerProperty)
                                    .toString();
                    Gear.ToolTip = Fragment.charFormat().toolTip();
                    Gear.Pure = Text == QString(OptionGlyph);
                    Found.append(Gear);
                }
            }
        }
        // The block is already right when its gears sit exactly
        // where the wanted gears belong, each with the right kind
        // and hover text, each alone in its fragment, and no
        // other gear hides anywhere in the block.
        bool GearsRight = Found.size() == WantedCount;
        if (GearsRight) {
            for (int i = 0; i < WantedCount; ++i) {
                QTextCharFormat Wanted = FormatForGear(WantedGears[i]);
                if (Found[i].Pos != BlockPos + i ||
                    Found[i].Kind != WantedGears[i].Kind ||
                    Found[i].ToolTip != Wanted.toolTip() ||
                    !Found[i].Pure) {
                    GearsRight = false;
                    break;
                }
            }
        }
        if (!GearsRight) {
            // Wrong gears go, last first, so the positions stay
            // valid; then the wanted gears go in, first first,
            // each at its own position.
            for (int i = Found.size() - 1; i >= 0; --i) {
                Sweep.setPosition(Found[i].Pos);
                Sweep.setPosition(Found[i].Pos + 1,
                                  QTextCursor::KeepAnchor);
                Sweep.removeSelectedText();
                ++_DiagMarkersGearsRemoved;
                if (CursorPos > Found[i].Pos) {
                    --CursorPos;
                }
                if (CursorAnchor > Found[i].Pos) {
                    --CursorAnchor;
                }
                MovedText = true;
            }
            for (int i = 0; i < WantedCount; ++i) {
                int Pos = BlockPos + i;
                Sweep.setPosition(Pos);
                Sweep.insertText(QString(OptionGlyph),
                                 FormatForGear(WantedGears[i]));
                ++_DiagMarkersGearsInserted;
                if (CursorPos > Pos) {
                    ++CursorPos;
                }
                if (CursorAnchor > Pos) {
                    ++CursorAnchor;
                }
                MovedText = true;
            }
        }
        if (ShowGear) {
            // Any other text in the block that took a gear's
            // format keeps its text but loses the marker property
            // and the hover text, so it reads back as plain text
            // and can take tags again. The wanted gears sit at
            // the block start, so only text past them is cleared.
            for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
                QTextFragment Fragment = It.fragment();
                if (!Fragment.charFormat()
                         .property(DocOptionMarkerProperty)
                         .isValid()) {
                    continue;
                }
                QString Text = Fragment.text();
                int From = Fragment.position();
                int To = From + Text.size();
                int ClearFrom = qMax(From, BlockPos + WantedCount);
                if (ClearFrom >= To) {
                    continue;
                }
                QTextCharFormat Fixed = Fragment.charFormat();
                Fixed.clearProperty(DocOptionMarkerProperty);
                Fixed.setToolTip(QString());
                Sweep.setPosition(ClearFrom);
                Sweep.setPosition(To, QTextCursor::KeepAnchor);
                Sweep.setCharFormat(Fixed);
            }
        }
        // The src placeholder (Bug 466): a src element with no
        // real content shows its file name in place of the
        // missing text. It sits after the gear when markers
        // show, at the block start otherwise, and it stays put
        // when markers toggle. A stale placeholder is deleted,
        // and text that only took the placeholder's format keeps
        // its text but loses the property.
        QString Src = AttributeOf(Attributes, QStringLiteral("src"));
        bool WantsPlaceholder =
            First && !Src.isEmpty() &&
            !SourceElementHasContent(Document, BlockPos);
        QString WantedText = SrcPlaceholderText(Src);
        int WantedStart = BlockPos + WantedCount;
        QVector<QPair<int, int>> StaleRanges;
        for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
            QTextFragment Fragment = It.fragment();
            QVariant Value = Fragment.charFormat().property(
                DocSrcPlaceholderProperty);
            if (!Value.isValid()) {
                continue;
            }
            QString Text = Fragment.text();
            int From = Fragment.position();
            if (WantsPlaceholder && From == WantedStart &&
                Text == WantedText) {
                continue;
            }
            if (Text.startsWith(QStringLiteral("<Src="))) {
                // A stale or dirtied placeholder: the exact
                // placeholder text of a block that no longer
                // wants one, or text that merged into the
                // display-only placeholder region (a paste
                // inside it, or typing under an older build).
                // The region holds no real content -- typing
                // beside a mark never takes its format -- so
                // the whole fragment goes with the stale
                // placeholder instead of surviving as plain
                // text (Bug 469). Without this, the copy path
                // would find no placeholder to strip and the
                // src would be dropped.
                StaleRanges.append(
                    QPair<int, int>(From, From + Text.size()));
            }
        }
        for (int i = StaleRanges.size() - 1; i >= 0; --i) {
            int From = StaleRanges[i].first;
            int To = StaleRanges[i].second;
            Sweep.setPosition(From);
            Sweep.setPosition(To, QTextCursor::KeepAnchor);
            Sweep.removeSelectedText();
            if (CursorPos > To) {
                CursorPos -= (To - From);
            } else if (CursorPos > From) {
                CursorPos = From;
            }
            if (CursorAnchor > To) {
                CursorAnchor -= (To - From);
            } else if (CursorAnchor > From) {
                CursorAnchor = From;
            }
            MovedText = true;
        }
        if (!StaleRanges.isEmpty()) {
            // Deleting a dirtied placeholder may have emptied
            // the element: re-check, so the clean placeholder
            // is stamped back in this same pass (Bug 469).
            WantsPlaceholder = First && !Src.isEmpty() &&
                !SourceElementHasContent(Document, BlockPos);
        }
        bool PlaceholderInPlace = false;
        for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
            QTextFragment Fragment = It.fragment();
            QVariant Value = Fragment.charFormat().property(
                DocSrcPlaceholderProperty);
            if (!Value.isValid()) {
                continue;
            }
            QString Text = Fragment.text();
            int From = Fragment.position();
            int To = From + Text.size();
            if (WantsPlaceholder && From == WantedStart &&
                Text == WantedText) {
                // In place: make sure the format is exactly
                // the placeholder format for this src.
                QTextCharFormat WantedFormat =
                    FormatForSrcPlaceholder(Src);
                if (Value.toString() != Src ||
                    !Fragment.charFormat().fontItalic() ||
                    !Fragment.charFormat().toolTip().isEmpty()) {
                    Sweep.setPosition(From);
                    Sweep.setPosition(To, QTextCursor::KeepAnchor);
                    Sweep.setCharFormat(WantedFormat);
                }
                PlaceholderInPlace = true;
                continue;
            }
            // Any other placeholder-property text keeps its
            // text but loses the property, so it reads back
            // as plain text.
            QTextCharFormat Fixed = Fragment.charFormat();
            Fixed.clearProperty(DocSrcPlaceholderProperty);
            Sweep.setPosition(From);
            Sweep.setPosition(To, QTextCursor::KeepAnchor);
            Sweep.setCharFormat(Fixed);
        }
        if (WantsPlaceholder && !PlaceholderInPlace) {
            Sweep.setPosition(WantedStart);
            Sweep.insertText(WantedText, FormatForSrcPlaceholder(Src));
            if (CursorPos > WantedStart) {
                CursorPos += WantedText.size();
            }
            if (CursorAnchor > WantedStart) {
                CursorAnchor += WantedText.size();
            }
            MovedText = true;
        }
    }
    Sweep.endEditBlock();
    if (MovedText) {
        int Last = Document->characterCount() - 1;
        if (CursorPos < 0) {
            CursorPos = 0;
        }
        if (CursorPos > Last) {
            CursorPos = Last;
        }
        if (CursorAnchor < 0) {
            CursorAnchor = 0;
        }
        if (CursorAnchor > Last) {
            CursorAnchor = Last;
        }
        QTextCursor Restored(Document);
        Restored.setPosition(CursorAnchor);
        Restored.setPosition(CursorPos, QTextCursor::KeepAnchor);
        _TextEdit->setTextCursor(Restored);
    }
}


void TextEditor::OnShowMarkersToggled(bool Checked)
{
    QSettings Settings;
    Settings.setValue(QStringLiteral("editor/showOptionMarkers"),
                      Checked);
    // Toggling the markers is display only: it never marks
    // the document changed.
    _SettingParagraphs = true;
    UpdateOptionMarkers();
    _SettingParagraphs = false;
    // Bug 502 diagnostic: report the sweep that just ran, so the
    // counts are visible without any other step.
    QMessageBox::information(this, tr("Option markers"),
                             MarkerSweepReport());
}

void TextEditor::OnContextMenuRequested(const QPoint& Pos)
{
    QPoint At = Pos;
    if (At.x() < 0 || At.y() < 0) {
        At = _TextEdit->cursorRect().center();
    }
    QMenu* Menu = _TextEdit->createStandardContextMenu();
    Menu->addSeparator();
    AddSpellActions(Menu, At);
    AddSourceOptionsActions(Menu, At);
    AddListOptionsActions(Menu, At);
    AddTableOptionsActions(Menu, At);
    AddTagActions(Menu);
    AddReferenceActions(Menu);
    AddRenameLinkAction(Menu, At);
    AddEditIrefAction(Menu, At);
    bool CoveredMarkers = SelectionCoversOptionMarker(
        _TextEdit->document(), _TextEdit->textCursor());
    Menu->exec(_TextEdit->viewport()->mapToGlobal(At));
    delete Menu;
    if (CoveredMarkers) {
        // A menu Cut or Delete bypasses the key handling that
        // keeps the markers: sweep them back into place.
        UpdateOptionMarkers();
    }
}

void TextEditor::RefreshXmlTab()
{
    // While the pane holds unapplied edits, a refresh must not
    // overwrite the user's text (Bug 520).
    if (_XmlDirty) {
        return;
    }
    QVector<xmlNodePtr> Nodes = BodyNodes();
    QStringList Parts;
    // While the pane is editable it shows only the body nodes:
    // preserved content is kept but hidden, so the re-parse has
    // no preserved content to separate out, and nothing
    // preserved can be lost by an edit. In read-only mode the
    // pane shows body and preserved nodes, as before.
    bool Editing = _XmlReadOnlyCheck != nullptr &&
                   !_XmlReadOnlyCheck->isChecked();
    int PreservedIndex = 0;
    for (int Index = 0; Index < Nodes.size(); ++Index) {
        if (!Editing) {
            while (PreservedIndex < _PreservedNodes.size() &&
                   _PreservedNodes[PreservedIndex].AfterNodes <= Index) {
                Parts.append(
                    SerializeNode(_PreservedNodes[PreservedIndex].Node));
                ++PreservedIndex;
            }
        }
        Parts.append(SerializeNode(Nodes[Index]));
    }
    if (!Editing) {
        while (PreservedIndex < _PreservedNodes.size()) {
            Parts.append(
                SerializeNode(_PreservedNodes[PreservedIndex].Node));
            ++PreservedIndex;
        }
    }
    if (_XmlPreservedNote != nullptr) {
        bool HasPreserved = !_PreservedNodes.isEmpty();
        _XmlPreservedNote->setVisible(Editing && HasPreserved);
        if (Editing && HasPreserved) {
            _XmlPreservedNote->setText(
                tr("Preserved content is hidden while editing; "
                   "it is kept and shown again when Read-only is "
                   "re-checked."));
        }
    }
    {
        QSignalBlocker Blocker(_XmlEdit);
        _XmlEdit->setPlainText(Parts.join(QLatin1Char('\n')));
    }
    for (xmlNodePtr Node : Nodes) {
        xmlFreeNode(Node);
    }
}

/**
 * @brief Returns the list kinds the cursor or selection touches.
 *
 * A block in a list counts under its kind, or "list" when it
 * carries no kind; a block outside any list counts only when its
 * kind says it is a definition block. A block that left its list
 * keeps a stale kind and counts for nothing.
 * @param Document The document.
 * @param Cursor The cursor.
 * @return The kinds touched.
 */
static QSet<QString> ListKindsTouched(QTextDocument* Document,
                                      const QTextCursor& Cursor)
{
    QSet<QString> Kinds;
    auto Consider = [&Kinds](const QTextBlock& Block) {
        QString Kind = Block.blockFormat().property(DocListKindProperty)
                           .toString();
        if (Block.textList() != nullptr) {
            if (Kind.isEmpty()) {
                Kind = QStringLiteral("list");
            }
            Kinds.insert(Kind);
        } else if (Kind == QLatin1String("dl")) {
            Kinds.insert(Kind);
        }
    };
    if (!Cursor.hasSelection()) {
        Consider(Cursor.block());
        return Kinds;
    }
    int Start = Cursor.selectionStart();
    int End = Cursor.selectionEnd();
    QTextBlock Block = Document->findBlock(Start);
    while (Block.isValid() && Block.position() < End) {
        Consider(Block);
        Block = Block.next();
    }
    return Kinds;
}

/**
 * @brief True when the cursor or selection touches a sourcecode block.
 * @param Document The document.
 * @param Cursor The cursor or selection.
 * @return True when any touched block carries the source property.
 */
static bool TouchesSourceCode(QTextDocument* Document,
                              const QTextCursor& Cursor)
{
    auto Consider = [](const QTextBlock& Block) {
        return IsSourceCodeBlock(Block);
    };
    if (!Cursor.hasSelection()) {
        return Consider(Cursor.block());
    }
    int Start = Cursor.selectionStart();
    int End = Cursor.selectionEnd();
    QTextBlock Block = Document->findBlock(Start);
    while (Block.isValid() && Block.position() < End) {
        if (Consider(Block)) {
            return true;
        }
        Block = Block.next();
    }
    return false;
}

/**
 * @brief Cleans highlighted text into a reference name.
 *
 * Line breaks become spaces, surrounding brackets and quotes
 * come off ([iCal] names iCal), trailing sentence punctuation
 * comes off, and runs of spaces collapse to one.
 * @param Text The highlighted text.
 * @return The cleaned name, or empty when nothing is left.
 */
static QString CleanReferenceName(const QString& Text)
{
    QString Cleaned = Text;
    Cleaned.replace(QChar(0x2029), QLatin1Char(' '));
    Cleaned.replace(QLatin1Char('\n'), QLatin1Char(' '));
    Cleaned.replace(QLatin1Char('\r'), QLatin1Char(' '));
    Cleaned = Cleaned.trimmed();
    bool Stripped = true;
    while (Stripped && Cleaned.length() >= 2) {
        Stripped = false;
        QChar First = Cleaned.front();
        QChar Last = Cleaned.back();
        if ((First == QLatin1Char('[') && Last == QLatin1Char(']')) ||
            (First == QLatin1Char('(') && Last == QLatin1Char(')')) ||
            (First == QLatin1Char('{') && Last == QLatin1Char('}')) ||
            (First == QLatin1Char('"') && Last == QLatin1Char('"')) ||
            (First == QLatin1Char('\'') && Last == QLatin1Char('\''))) {
            Cleaned = Cleaned.mid(1, Cleaned.length() - 2).trimmed();
            Stripped = true;
        }
    }
    while (!Cleaned.isEmpty() &&
           (Cleaned.back() == QLatin1Char('.') ||
            Cleaned.back() == QLatin1Char(',') ||
            Cleaned.back() == QLatin1Char(';') ||
            Cleaned.back() == QLatin1Char(':'))) {
        Cleaned.chop(1);
        Cleaned = Cleaned.trimmed();
    }
    Cleaned.replace(QRegularExpression(QStringLiteral("\\s+")),
                    QStringLiteral(" "));
    return Cleaned.trimmed();
}

/**
 * @brief The RFC anchor for highlighted text, or empty.
 *
 * RFC4324, RFC 4324, and [RFC4324] all name RFC4324; anything
 * else names nothing here, and the author is asked for a name.
 * @param Text The cleaned highlighted text.
 * @return The anchor, or empty when the text is not an RFC number.
 */
static QString RfcReferenceAnchor(const QString& Text)
{
    QString Squashed = Text;
    Squashed.remove(QRegularExpression(QStringLiteral("\\s+")));
    if (Squashed.length() <= 3 ||
        !Squashed.startsWith(QStringLiteral("RFC"), Qt::CaseInsensitive)) {
        return QString();
    }
    QString Digits = Squashed.mid(3);
    for (QChar Ch : Digits) {
        if (!Ch.isDigit()) {
            return QString();
        }
    }
    return QStringLiteral("RFC") + Digits;
}

/**
 * @brief The reference list with this name, at any level, or nullptr.
 * @param Document The document, borrowed.
 * @param Name The list name.
 * @return The list node, borrowed, or nullptr.
 */
static xmlNodePtr FindReferenceList(const DocDocument* Document,
                                    const QString& Name)
{
    if (Document == nullptr) {
        return nullptr;
    }
    for (const QString& Path : Document->ReferenceGroupPaths()) {
        xmlNodePtr Group = Document->ReferenceGroupAtPath(Path);
        if (Group != nullptr &&
            DocDocument::ChildText(Group, "name") == Name) {
            return Group;
        }
    }
    return nullptr;
}

/**
 * @brief The citable entries of a list, anchor and label each.
 *
 * <reference> entries by anchor and outline label; <xi:include>
 * entries by the anchor their href carries. Entries with no
 * anchor cannot be cited and are left out.
 * @param Group The <references> node, borrowed.
 * @param Cache The bibliography cache, borrowed, or nullptr:
 * an included reference's anchor comes from its cached file
 * when it is cached, and from the file name otherwise.
 * @return The entries, in list order.
 */
static QVector<QPair<QString, QString>> ReferenceListEntries(
    xmlNodePtr Group, const BibCache* Cache)
{
    QVector<QPair<QString, QString>> Entries;
    if (Group == nullptr) {
        return Entries;
    }
    for (xmlNodePtr Entry : DocDocument::ReferenceEntries(Group)) {
        QString Anchor =
            DocDocument::NodeAttribute(Entry, "anchor").trimmed();
        if (Anchor.isEmpty()) {
            continue;
        }
        Entries.append(qMakePair(Anchor, DocDocument::ReferenceLabel(Entry)));
    }
    for (xmlNodePtr Include : DocDocument::ReferenceIncludeNodes(Group)) {
        QString Href = DocDocument::NodeAttribute(Include, "href");
        // The cached file carries the real anchor (BCP14, not
        // BCP.14 from the file name); the derived anchor is
        // only the fallback until it is fetched.
        QString CachedAnchor = Cache != nullptr
            ? Cache->CachedAnchor(Href)
            : QString();
        QString Anchor = CachedAnchor.isEmpty()
            ? DocDocument::ReferenceAnchorFromIncludeHref(Href)
            : CachedAnchor;
        if (Anchor.isEmpty()) {
            continue;
        }
        Entries.append(qMakePair(Anchor, Anchor));
    }
    return Entries;
}

/**
 * @brief Adds a Rename action for the reference under the click
 *        (Bug 514 v2).
 *
 * A reference's visible text is read-only, so it cannot be
 * typed into; this is the way to change it. Right-clicking on
 * a reference offers Rename, which prompts for the new text
 * prefilled with the current one, then replaces the link's
 * text while keeping the reference itself: same target, same
 * underline and colour. Clearing the text is refused, since
 * an empty reference has nothing to show.
 * @param Menu The context menu.
 * @param Pos The click position, in the text edit's coordinates.
 */
void TextEditor::AddRenameLinkAction(QMenu* Menu, const QPoint& Pos)
{
    if (_Document == nullptr) {
        return;
    }
    QTextCursor At = _TextEdit->cursorForPosition(Pos);
    int Start, End;
    QString LinkName;
    DocTagEntry LinkEntry;
    if (!LinkRangeAt(_TextEdit->document(), At.position(), Start, End,
                     LinkName, LinkEntry)) {
        return;
    }
    // The reference's current visible text, for the prompt.
    QTextCursor Range(_TextEdit->document());
    Range.setPosition(Start);
    Range.setPosition(End, QTextCursor::KeepAnchor);
    QString Current = Range.selectedText();
    QString Label = (LinkName == QLatin1String("xref"))
                        ? tr("Rename reference...")
                        : tr("Rename link...");
    QAction* Action = Menu->addAction(Label);
    connect(Action, &QAction::triggered, this,
            [this, Start, End, Current, LinkName, LinkEntry]() {
        bool Ok = false;
        QString NewText = QInputDialog::getText(
            this, tr("Rename reference"), tr("Text:"),
            QLineEdit::Normal, Current, &Ok);
        if (!Ok) {
            return;
        }
        if (NewText.isEmpty()) {
            QMessageBox::information(
                this, tr("Rename reference"),
                tr("A reference needs some text to show. "
                   "To remove it, select the whole reference "
                   "and press Delete."));
            return;
        }
        if (NewText == Current) {
            return;
        }
        QTextCursor Cursor(_TextEdit->document());
        Cursor.setPosition(Start);
        Cursor.setPosition(End, QTextCursor::KeepAnchor);
        QTextCharFormat LinkFormat =
            LinkFormatForSelection(_TextEdit->document(), Cursor);
        if (!LinkFormat.isValid()) {
            return;
        }
        Cursor.beginEditBlock();
        Cursor.removeSelectedText();
        Cursor.insertText(NewText, LinkFormat);
        // Bug 514 reopened: the rename changed the text but the
        // saved XML lost the <xref> -- insertText with a format
        // does not reliably carry the custom stack property
        // through to the new fragments, so the renamed text can
        // come out as plain text. Re-apply the link's stack
        // explicitly, via the same setCharFormat mechanism that
        // wraps a selection in a link. When the insert already
        // kept the stack, ChangeStacks sees it and does nothing;
        // when the insert dropped it, this puts it back. Either
        // way the renamed text keeps its reference.
        ChangeStacks(Start, Start + NewText.length(), LinkName,
                     &LinkEntry);
        Cursor.endEditBlock();
        _TextEdit->setTextCursor(Cursor);
    });
}

void TextEditor::AddEditIrefAction(QMenu* Menu, const QPoint& Pos)
{
    if (_Document == nullptr) {
        return;
    }
    QTextCursor At = _TextEdit->cursorForPosition(Pos);
    int Start, End;
    DocTagEntry IrefEntry;
    if (!IrefRangeAt(_TextEdit->document(), At.position(), Start, End,
                     IrefEntry)) {
        return;
    }
    // The marker's current item and sub-item, for the prompts.
    QString CurrentItem =
        AttributeOf(IrefEntry.Attributes, QStringLiteral("item"));
    QString CurrentSubitem =
        AttributeOf(IrefEntry.Attributes, QStringLiteral("subitem"));
    QAction* Action = Menu->addAction(tr("Edit index entry..."));
    connect(Action, &QAction::triggered, this,
            [this, Start, End, IrefEntry, CurrentItem, CurrentSubitem]() {
        bool Ok = false;
        QString NewItem = QInputDialog::getText(
            this, tr("Edit index entry"),
            tr("Item (the word or phrase to index):"),
            QLineEdit::Normal, CurrentItem, &Ok);
        if (!Ok) {
            return;
        }
        if (NewItem.trimmed().isEmpty()) {
            QMessageBox::information(
                this, tr("Edit index entry"),
                tr("An index entry needs an item. To remove it, "
                   "select the whole marker and press Delete."));
            return;
        }
        QString NewSubitem = QInputDialog::getText(
            this, tr("Edit index entry"),
            tr("Sub-item (a narrower term under the item, optional):"),
            QLineEdit::Normal, CurrentSubitem, &Ok);
        if (!Ok) {
            return;
        }
        if (NewItem.trimmed() == CurrentItem &&
            NewSubitem.trimmed() == CurrentSubitem) {
            return;
        }
        // Update the marker entry's attributes on every
        // fragment the marker covers, keeping each fragment's
        // own stack and visuals. The marker text stays the
        // glyph; the item lives in the attributes, and the
        // save reads it from there (Bug 503).
        DocTagEntry Updated = IrefEntry;
        SetTagAttribute(Updated.Attributes, QStringLiteral("item"),
                        NewItem.trimmed());
        SetTagAttribute(Updated.Attributes, QStringLiteral("subitem"),
                        NewSubitem.trimmed());
        QTextDocument* Document = _TextEdit->document();
        QTextCursor Cursor(Document);
        Cursor.beginEditBlock();
        QTextBlock Block = Document->findBlock(Start);
        while (Block.isValid() && Block.position() < End) {
            for (QTextBlock::iterator It = Block.begin();
                 !It.atEnd(); ++It) {
                QTextFragment Fragment = It.fragment();
                int FragStart = Fragment.position();
                int FragEnd = FragStart + Fragment.length();
                if (FragEnd <= Start || FragStart >= End) {
                    continue;
                }
                QTextCharFormat Format = Fragment.charFormat();
                QVariant MarkerValue =
                    Format.property(DocMarkerProperty);
                if (!MarkerValue.isValid()) {
                    continue;
                }
                if (!(EntryFromVariant(MarkerValue) == IrefEntry)) {
                    continue;
                }
                Format.setProperty(DocMarkerProperty,
                                   EntryToVariant(Updated));
                ApplyMarkerVisuals(Format, Updated);
                QTextCursor FragCursor(Document);
                int From = FragStart > Start ? FragStart : Start;
                int To = FragEnd < End ? FragEnd : End;
                FragCursor.setPosition(From);
                FragCursor.setPosition(To, QTextCursor::KeepAnchor);
                FragCursor.setCharFormat(Format);
            }
            Block = Block.next();
        }
        Cursor.endEditBlock();
        _TextEdit->setTextCursor(Cursor);
    });
}

void TextEditor::AddReferenceActions(QMenu* Menu)
{
    if (_Document == nullptr) {
        return;
    }
    QTextCursor Cursor = _TextEdit->textCursor();
    if (!Cursor.hasSelection()) {
        return;
    }
    // Code, figures, and tables hold literal text only, so no
    // reference is offered where the cursor touches a sourcecode
    // block, a figure, or a table: the same rule as the tags.
    if (TouchesSourceCode(_TextEdit->document(), Cursor) ||
        TouchesFigure(_TextEdit->document(), Cursor) ||
        TouchesTable(_TextEdit->document(), Cursor)) {
        return;
    }
    QString Selected = CleanReferenceName(Cursor.selectedText());
    if (Selected.isEmpty()) {
        return;
    }
    QString RfcAnchor = RfcReferenceAnchor(Selected);
    QMenu* ReferencesMenu = Menu->addMenu(tr("References"));

    QMenu* NewMenu = ReferencesMenu->addMenu(tr("New"));
    struct ReferenceListChoice
    {
        const char* Name;
        const char* Label;
    };
    static const ReferenceListChoice Choices[] = {
        {"Normative References", "Normative"},
        {"Informative References", "Informative"},
    };
    for (const ReferenceListChoice& Choice : Choices) {
        QString ListName = QString::fromLatin1(Choice.Name);
        QAction* Action = NewMenu->addAction(tr(Choice.Label));
        connect(Action, &QAction::triggered, this,
                [this, ListName, Selected, RfcAnchor]() {
            QString Anchor = RfcAnchor;
            if (Anchor.isEmpty()) {
                // Ask for the minimum information, a name,
                // prefilled from the highlight; the author
                // completes the details later, in the entry
                // form. A bracketed key such as [iCal] arrives
                // with the brackets already off. An RFC number
                // never prompts: the highlight is the name.
                bool Ok = false;
                QString Name = QInputDialog::getText(
                    this, tr("New Reference"), tr("Reference name:"),
                    QLineEdit::Normal, Selected, &Ok);
                if (!Ok) {
                    return;
                }
                Anchor = CleanReferenceName(Name);
                while (Anchor.isEmpty()) {
                    Name = QInputDialog::getText(
                        this, tr("New Reference"),
                        tr("Reference name (required):"),
                        QLineEdit::Normal, QString(), &Ok);
                    if (!Ok) {
                        return;
                    }
                    Anchor = CleanReferenceName(Name);
                }
            }
            emit MakeReferenceRequested(NewReferenceAction, nullptr,
                                        Anchor, ListName);
        });
    }

    QAction* InternalAction = ReferencesMenu->addAction(tr("Internal"));
    connect(InternalAction, &QAction::triggered, this, [this]() {
        QVector<DocDocument::InternalTarget> Targets =
            _Document->InternalTargets();
        if (Targets.isEmpty()) {
            QMessageBox::information(
                this, tr("Internal Reference"),
                tr("This document has no sections, figures, or tables "
                   "to refer to."));
            return;
        }
        QStringList Items;
        QSet<QString> Seen;
        for (const DocDocument::InternalTarget& Target : Targets) {
            // A section shows its outline number in front of its
            // title, as in the outline; figures and tables carry
            // no number here.
            QString Item = Target.Number.isEmpty()
                ? Target.Title
                : Target.Number + QStringLiteral(" ") + Target.Title;
            Item += QStringLiteral(" (") + Target.Kind + QStringLiteral(")");
            // QInputDialog returns the picked string, so two
            // targets with the same title and kind need
            // different strings.
            QString Unique = Item;
            int Suffix = 2;
            while (Seen.contains(Unique)) {
                Unique = Item + QStringLiteral(" ") +
                    QString::number(Suffix++);
            }
            Seen.insert(Unique);
            Items.append(Unique);
        }
        bool Ok = false;
        QString Picked = QInputDialog::getItem(this,
                                               tr("Internal Reference"),
                                               tr("Refer to:"), Items, 0,
                                               false, &Ok);
        if (!Ok) {
            return;
        }
        int Index = Items.indexOf(Picked);
        if (Index < 0 || Index >= Targets.size()) {
            return;
        }
        emit MakeReferenceRequested(InternalReferenceAction,
                                    Targets[Index].Node, QString(),
                                    QString());
    });

    QMenu* ExistingMenu = ReferencesMenu->addMenu(tr("Existing"));
    for (const ReferenceListChoice& Choice : Choices) {
        QString ListName = QString::fromLatin1(Choice.Name);
        QMenu* ListMenu = ExistingMenu->addMenu(tr(Choice.Label));
        xmlNodePtr Group = FindReferenceList(_Document, ListName);
        if (Group == nullptr) {
            ListMenu->setEnabled(false);
            continue;
        }
        QVector<QPair<QString, QString>> Entries =
            ReferenceListEntries(Group, _BibCache);
        if (Entries.isEmpty()) {
            ListMenu->setEnabled(false);
            continue;
        }
        for (const QPair<QString, QString>& Entry : Entries) {
            QAction* Action = ListMenu->addAction(Entry.second);
            connect(Action, &QAction::triggered, this,
                    [this, Anchor = Entry.first]() {
                // No outline rebuild happens on this path, so
                // the live selection is still the highlight.
                int Start = -1;
                int End = -1;
                SelectedRange(Start, End);
                XrefForm Form = XrefForm::LinkText;
                QString Section;
                if (!PickXrefForm(this, Anchor, SelectedText(), Form,
                                  Section)) {
                    return;
                }
                InsertXref(Anchor, Section, Form, Start, End);
            });
        }
    }
}

void TextEditor::AddTagActions(QMenu* Menu)
{
    QTextCursor Cursor = _TextEdit->textCursor();
    bool HasSelection = Cursor.hasSelection();
    DocTagStack Stack = StackOf(Cursor.charFormat());
    bool InCell = Cursor.currentTable() != nullptr;
    QString Parent;
    if (!Stack.isEmpty()) {
        Parent = Stack.last().Name;
    } else if (InCell) {
        // A cell is its own container: the cell rules keep
        // contact, list, spanx, and vspace out of cells.
        Parent = IsHeaderCellBlock(Cursor.block()) ? QStringLiteral("th")
                                                   : QStringLiteral("td");
    } else {
        Parent = QStringLiteral("t");
    }
    QSet<QString> ListKinds = ListKindsTouched(_TextEdit->document(), Cursor);
    bool InSource = TouchesSourceCode(_TextEdit->document(), Cursor);
    bool InFigure = TouchesFigure(_TextEdit->document(), Cursor);
    bool InTable = TouchesTable(_TextEdit->document(), Cursor);
    bool InTableCaption = TouchesTableCaption(_TextEdit->document(), Cursor);

    // Bug 524: the block-level inserts (ASCII art, figures, source
    // code, tables) share one alphabetical list with the tags, so
    // ASCII art sorts before BCP 14 instead of after Vertical
    // Space. They are added at their alphabetical points in the
    // tag loop below: ASCII art before the first tag, Figure
    // before Index Entry, Source code before Strong, Table before
    // Teletype. The enablement matches the old block after the
    // loop: offered wherever the cursor sits, but never where it
    // already touches a list, a sourcecode block, a figure, or a
    // table.
    bool CanInsertBlock = ListKinds.isEmpty() && !InSource && !InFigure &&
                          !InTable;
    auto AddInsertAction = [&](const QString& Label,
                               void (TextEditor::*Slot)()) {
        QAction* Action = Menu->addAction(Label);
        Action->setEnabled(CanInsertBlock);
        connect(Action, &QAction::triggered, this, Slot);
    };
    AddInsertAction(tr("ASCII art"), &TextEditor::InsertArtwork);

    for (const DocTagInfo& Info : TagInfos) {
        QString Name = QString::fromLatin1(Info.Name);
        QString Label = QString::fromLatin1(Info.Label);
        // Bug 524: the remaining inserts, at their alphabetical
        // points. These run before any filtering below, so an
        // insert is added (disabled when it does not apply) even
        // when the tag at this point is skipped.
        if (Name == QLatin1String("iref")) {
            AddInsertAction(tr("Figure"), &TextEditor::InsertFigure);
        } else if (Name == QLatin1String("strong")) {
            AddInsertAction(tr("Source code"),
                            &TextEditor::InsertSourceCode);
        } else if (Name == QLatin1String("tt")) {
            AddInsertAction(tr("Table"), &TextEditor::InsertTable);
        }
        if (Name == QLatin1String("list")) {
            // The List entry opens a submenu with the three
            // section-level list kinds, alphabetical by label. A
            // list is a block-level change, so it is offered
            // wherever the cursor sits, but never where the
            // cursor or selection already touches a list, a
            // sourcecode block, a figure, or a table: nested
            // lists stay out of this phase, and code, artwork,
            // and cells are not list items.
            QMenu* Submenu = Menu->addMenu(Label);
            struct ListKind
            {
                const char* Kind;
                const char* Label;
            };
            static const ListKind Kinds[] = {
                {"ul", "Bulleted list"},
                {"dl", "Definition list"},
                {"ol", "Numbered list"},
            };
            for (const ListKind& Kind : Kinds) {
                QString KindName = QString::fromLatin1(Kind.Kind);
                QAction* Action = Submenu->addAction(tr(Kind.Label));
                Action->setEnabled(ListKinds.isEmpty() && !InSource &&
                                   !InFigure && !InTable);
                connect(Action, &QAction::triggered, this, [this, KindName]() {
                    InsertBlockList(KindName);
                });
            }
            continue;
        }
        // Code and figures hold literal text only, so no tag
        // is offered where the cursor touches a sourcecode
        // block or a figure block, captions included: a
        // caption keeps the markup it came with, and gains no
        // new tags from the menu. A table caption is the same.
        // Cells do offer tags, filtered to the cell set by the
        // nesting rules above.
        if (InSource || InFigure || InTableCaption) {
            continue;
        }
        if (!CanContain(Parent, Name)) {
            continue;
        }
        if (Info.Empty || Info.BlockLevel) {
            QAction* Action = Menu->addAction(Label);
            connect(Action, &QAction::triggered, this, [this, Name]() {
                InsertTag(Name);
            });
        } else if (HasSelection) {
            QAction* Action = Menu->addAction(Label);
            connect(Action, &QAction::triggered, this, [this, Name]() {
                WrapSelection(Name);
            });
        } else {
            bool Active = StackHolds(Stack, Name);
            QString Text = Active ? tr("%1 - End").arg(Label)
                                  : tr("%1 - Start").arg(Label);
            QAction* Action = Menu->addAction(Text);
            connect(Action, &QAction::triggered, this, [this, Name, Active]() {
                ToggleTag(Name, Active);
            });
        }
    }

    // Tags present, for removal: the stack at the cursor, or every
    // stack across the selection. Structural names (organization,
    // address) are never offered: removing a contact takes them.
    QSet<QString> Present;
    if (HasSelection) {
        QTextDocument* Document = _TextEdit->document();
        int Start = Cursor.selectionStart();
        int End = Cursor.selectionEnd();
        QTextBlock Block = Document->findBlock(Start);
        while (Block.isValid() && Block.position() < End) {
            for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
                QTextFragment Fragment = It.fragment();
                int FragStart = Fragment.position();
                int FragEnd = FragStart + Fragment.length();
                if (FragEnd <= Start || FragStart >= End) {
                    continue;
                }
                for (const DocTagEntry& Entry : StackOf(Fragment.charFormat())) {
                    Present.insert(Entry.Name);
                }
            }
            Block = Block.next();
        }
    } else {
        for (const DocTagEntry& Entry : Stack) {
            Present.insert(Entry.Name);
        }
    }
    QStringList Removable;
    for (const DocTagInfo& Info : TagInfos) {
        if (Present.contains(QString::fromLatin1(Info.Name))) {
            Removable.append(QString::fromLatin1(Info.Name));
        }
    }

    DocTagEntry Marker;
    bool HasMarker = false;
    if (HasSelection) {
        QTextDocument* Document = _TextEdit->document();
        int Start = Cursor.selectionStart();
        int End = Cursor.selectionEnd();
        QTextBlock Block = Document->findBlock(Start);
        while (Block.isValid() && Block.position() < End && !HasMarker) {
            for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
                QTextFragment Fragment = It.fragment();
                int FragStart = Fragment.position();
                int FragEnd = FragStart + Fragment.length();
                if (FragEnd <= Start || FragStart >= End) {
                    continue;
                }
                QVariant Value = Fragment.charFormat().property(DocMarkerProperty);
                if (Value.isValid()) {
                    Marker = EntryFromVariant(Value);
                    HasMarker = true;
                    break;
                }
            }
            Block = Block.next();
        }
    } else {
        QVariant Value = MarkerAtPosition(_TextEdit->document(), Cursor.position());
        if (!Value.isValid()) {
            Value = MarkerAtPosition(_TextEdit->document(), Cursor.position() - 1);
        }
        if (Value.isValid()) {
            Marker = EntryFromVariant(Value);
            HasMarker = true;
        }
    }

    // (Bug 524: the block-level inserts are added inside the tag
    // loop above, at their alphabetical points, so they are not
    // repeated here.)
    if (InCell) {
        // Row, column, merge, and split operations for the
        // table the cursor sits in, plus setting the body row
        // and column counts and toggling the header and footer
        // sections. The last body row and the last column
        // cannot be deleted; merging needs a selection spanning
        // two or more cells of this table; splitting needs a
        // cell that spans.
        QTextTable* Frame = Cursor.currentTable();
        int Row = -1;
        int Column = -1;
        bool HaveCell = TableCellAtCursor(Frame, Cursor, Row, Column);
        bool CanDeleteRow = false;
        bool CanSplit = false;
        bool HasHeader = false;
        bool HasFooter = false;
        if (HaveCell) {
            xmlNodePtr Shell = nullptr;
            QVector<DocTableRowMark> Marks;
            QTextBlock FirstBlock =
                Frame->cellAt(0, 0).firstCursorPosition().block();
            TablePayloadOf(FirstBlock, Shell, Marks);
            int BodyRows = 0;
            if (Marks.isEmpty()) {
                BodyRows = Frame->rows();
            } else {
                for (const DocTableRowMark& Mark : Marks) {
                    if (Mark.Section == QLatin1String("tbody")) {
                        ++BodyRows;
                    } else if (Mark.Section == QLatin1String("thead")) {
                        HasHeader = true;
                    } else if (Mark.Section == QLatin1String("tfoot")) {
                        HasFooter = true;
                    }
                }
            }
            CanDeleteRow =
                SectionOfRow(Marks, Row) != QLatin1String("tbody") ||
                BodyRows > 1;
            QTextTableCell Cell = Frame->cellAt(Row, Column);
            CanSplit = Cell.rowSpan() > 1 || Cell.columnSpan() > 1;
            xmlFreeNode(Shell);
        }
        bool CanMerge = false;
        if (HaveCell && HasSelection) {
            QTextCursor From(_TextEdit->document());
            From.setPosition(Cursor.selectionStart());
            QTextCursor To(_TextEdit->document());
            To.setPosition(Cursor.selectionEnd());
            if (From.currentTable() == Frame && To.currentTable() == Frame) {
                int FromRow = -1;
                int FromColumn = -1;
                int ToRow = -1;
                int ToColumn = -1;
                if (TableCellAtCursor(Frame, From, FromRow, FromColumn) &&
                    TableCellAtCursor(Frame, To, ToRow, ToColumn)) {
                    CanMerge = FromRow != ToRow || FromColumn != ToColumn;
                }
            }
        }
        Menu->addSeparator();
        QAction* RowAboveAction = Menu->addAction(tr("Insert row above"));
        RowAboveAction->setEnabled(HaveCell);
        connect(RowAboveAction, &QAction::triggered, this,
                &TextEditor::InsertTableRowAbove);
        QAction* RowBelowAction = Menu->addAction(tr("Insert row below"));
        RowBelowAction->setEnabled(HaveCell);
        connect(RowBelowAction, &QAction::triggered, this,
                &TextEditor::InsertTableRowBelow);
        QAction* DeleteRowAction = Menu->addAction(tr("Delete row"));
        DeleteRowAction->setEnabled(HaveCell && CanDeleteRow);
        connect(DeleteRowAction, &QAction::triggered, this,
                &TextEditor::DeleteTableRow);
        QAction* ColumnLeftAction = Menu->addAction(tr("Insert column left"));
        ColumnLeftAction->setEnabled(HaveCell);
        connect(ColumnLeftAction, &QAction::triggered, this,
                &TextEditor::InsertTableColumnLeft);
        QAction* ColumnRightAction =
            Menu->addAction(tr("Insert column right"));
        ColumnRightAction->setEnabled(HaveCell);
        connect(ColumnRightAction, &QAction::triggered, this,
                &TextEditor::InsertTableColumnRight);
        QAction* DeleteColumnAction = Menu->addAction(tr("Delete column"));
        DeleteColumnAction->setEnabled(HaveCell && Frame->columns() > 1);
        connect(DeleteColumnAction, &QAction::triggered, this,
                &TextEditor::DeleteTableColumn);
        QAction* BodyRowsAction = Menu->addAction(tr("Body rows..."));
        BodyRowsAction->setEnabled(HaveCell);
        connect(BodyRowsAction, &QAction::triggered, this,
                &TextEditor::SetTableBodyRowCount);
        QAction* ColumnsAction = Menu->addAction(tr("Columns..."));
        ColumnsAction->setEnabled(HaveCell);
        connect(ColumnsAction, &QAction::triggered, this,
                &TextEditor::SetTableColumnCount);
        QAction* HeaderAction = Menu->addAction(tr("Header row"));
        HeaderAction->setCheckable(true);
        HeaderAction->setChecked(HasHeader);
        HeaderAction->setEnabled(HaveCell);
        connect(HeaderAction, &QAction::triggered, this,
                &TextEditor::ToggleTableHeaderRow);
        QAction* FooterAction = Menu->addAction(tr("Footer row"));
        FooterAction->setCheckable(true);
        FooterAction->setChecked(HasFooter);
        FooterAction->setEnabled(HaveCell);
        connect(FooterAction, &QAction::triggered, this,
                &TextEditor::ToggleTableFooterRow);
        QAction* MergeAction = Menu->addAction(tr("Merge cells"));
        MergeAction->setEnabled(CanMerge);
        connect(MergeAction, &QAction::triggered, this,
                &TextEditor::MergeTableCells);
        QAction* SplitAction = Menu->addAction(tr("Split cell"));
        SplitAction->setEnabled(HaveCell && CanSplit);
        connect(SplitAction, &QAction::triggered, this,
                &TextEditor::SplitTableCell);
    }

    if (!Removable.isEmpty() || HasMarker || !ListKinds.isEmpty() ||
        InSource || InFigure || InTable) {
        Menu->addSeparator();
    }
    for (const QString& Name : Removable) {
        QAction* Action = Menu->addAction(tr("Remove %1").arg(LabelFor(Name)));
        connect(Action, &QAction::triggered, this, [this, Name]() {
            RemoveTag(Name);
        });
    }
    if (HasMarker) {
        QAction* Action = Menu->addAction(tr("Remove %1").arg(LabelFor(Marker.Name)));
        connect(Action, &QAction::triggered, this, &TextEditor::RemoveMarker);
    }
    // Lists, for removal: one action per kind touched,
    // alphabetical by label.
    static const char* KindOrder[] = {"ul", "dl", "list", "ol"};
    for (const char* KindName : KindOrder) {
        QString Kind = QString::fromLatin1(KindName);
        if (!ListKinds.contains(Kind)) {
            continue;
        }
        QString Text;
        if (Kind == QLatin1String("ul")) {
            Text = tr("Remove bulleted list");
        } else if (Kind == QLatin1String("dl")) {
            Text = tr("Remove definition list");
        } else if (Kind == QLatin1String("ol")) {
            Text = tr("Remove numbered list");
        } else {
            Text = tr("Remove list");
        }
        QAction* Action = Menu->addAction(Text);
        connect(Action, &QAction::triggered, this, [this, Kind]() {
            RemoveBlockList(Kind);
        });
    }
    if (InSource) {
        QAction* Action = Menu->addAction(tr("Remove source code"));
        connect(Action, &QAction::triggered, this,
                &TextEditor::RemoveSourceCode);
    }
    if (InFigure) {
        QString Kind = FigureKindOf(Cursor.block());
        QAction* Action = Menu->addAction(
            Kind == QLatin1String("figure") ? tr("Remove figure")
                                            : tr("Remove ASCII art"));
        connect(Action, &QAction::triggered, this,
                &TextEditor::RemoveFigure);
    }
    if (InTable) {
        QAction* Action = Menu->addAction(tr("Remove table"));
        connect(Action, &QAction::triggered, this,
                &TextEditor::RemoveTable);
    }
}

/**
 * @brief Returns the attributes of the sourcecode element
 *        holding a position, or none when the position holds
 *        no sourcecode block.
 * @param Document The document.
 * @param Pos The position.
 * @return The element's attributes, in file order.
 */
static QVector<QPair<QString, QString>> SourceAttributesAt(
    QTextDocument* Document, int Pos)
{
    QTextBlock Block = Document->findBlock(Pos);
    if (!IsSourceCodeBlock(Block)) {
        return QVector<QPair<QString, QString>>();
    }
    return AttributesFromVariant(
        Block.blockFormat().property(DocSourceProperty));
}

/**
 * @brief Returns the list attributes at a position (Bug 464).
 * @param Document The document.
 * @param Pos A position in the list.
 * @return The list element's attributes, or empty when the
 *         position is in no list.
 */
static QVector<QPair<QString, QString>> ListAttributesAt(
    QTextDocument* Document, int Pos)
{
    QTextBlock Block = Document->findBlock(Pos);
    if (!IsListBlock(Block)) {
        return QVector<QPair<QString, QString>>();
    }
    return AttributesFromVariant(
        Block.blockFormat().property(DocListProperty));
}

/**
 * @brief Returns the table's own attributes from a block of it
 *        (Bug 448).
 * @param Document The document.
 * @param Pos A position in a block of the table.
 * @return The table's attributes, empty when the block carries
 *         no table payload.
 */
static QVector<QPair<QString, QString>> TableAttributesAt(
    QTextDocument* Document, int Pos)
{
    QTextBlock Block = Document->findBlock(Pos);
    xmlNodePtr Shell = nullptr;
    QVector<DocTableRowMark> Marks;
    if (!TablePayloadOf(Block, Shell, Marks)) {
        return QVector<QPair<QString, QString>>();
    }
    QVector<QPair<QString, QString>> Attributes = NodeAttributes(Shell);
    xmlFreeNode(Shell);
    return Attributes;
}

/**
 * @brief Returns one section's attributes from a block of its
 *        table (Bug 448).
 * @param Document The document.
 * @param Pos A position in a block of the table.
 * @param Section The section: "thead", "tbody", or "tfoot".
 * @return The section's attributes, empty when the block carries
 *         no table payload.
 */
static QVector<QPair<QString, QString>> TableSectionAttributesAt(
    QTextDocument* Document, int Pos, const QString& Section)
{
    QTextBlock Block = Document->findBlock(Pos);
    xmlNodePtr Shell = nullptr;
    QVector<DocTableRowMark> Marks;
    if (!TablePayloadOf(Block, Shell, Marks)) {
        return QVector<QPair<QString, QString>>();
    }
    xmlNodePtr SectionNode = nullptr;
    if (Section == QLatin1String("thead")) {
        SectionNode = DocDocument::ChildElement(Shell, "thead");
    } else if (Section == QLatin1String("tfoot")) {
        SectionNode = DocDocument::ChildElement(Shell, "tfoot");
    } else {
        SectionNode = DocDocument::ChildElement(Shell, "tbody");
    }
    QVector<QPair<QString, QString>> Attributes =
        NodeAttributes(SectionNode);
    xmlFreeNode(Shell);
    return Attributes;
}

/**
 * @brief Returns one row's attributes from its first cell's
 *        first block (Bug 448).
 *
 * The row's anchor rides in the mark alongside its other
 * attributes, so it is folded back in here.
 * @param Document The document.
 * @param Pos A position in the row's first cell's first block.
 * @return The row's attributes, empty when the block carries
 *         no table payload or is in no row.
 */
static QVector<QPair<QString, QString>> TableRowAttributesAt(
    QTextDocument* Document, int Pos)
{
    QTextBlock Block = Document->findBlock(Pos);
    xmlNodePtr Shell = nullptr;
    QVector<DocTableRowMark> Marks;
    if (!TablePayloadOf(Block, Shell, Marks)) {
        return QVector<QPair<QString, QString>>();
    }
    xmlFreeNode(Shell);
    int Row = -1;
    int Column = -1;
    if (!TableCellOfBlock(Document, Block, Row, Column) || Row < 0 ||
        Row >= Marks.size()) {
        return QVector<QPair<QString, QString>>();
    }
    QVector<QPair<QString, QString>> Attributes = Marks[Row].Attributes;
    if (!Marks[Row].Anchor.isEmpty()) {
        Attributes.append(QPair<QString, QString>(
            QStringLiteral("anchor"), Marks[Row].Anchor));
    }
    return Attributes;
}

/**
 * @brief Returns one cell's attributes from its first block
 *        (Bug 448).
 * @param Document The document.
 * @param Pos A position in the cell's first block.
 * @return The cell's attributes, empty when the block carries
 *         no cell property.
 */
static QVector<QPair<QString, QString>> TableCellAttributesAt(
    QTextDocument* Document, int Pos)
{
    QTextBlock Block = Document->findBlock(Pos);
    xmlNodePtr CellNode = nullptr;
    if (!TableCellFromVariant(
            Block.blockFormat().property(DocTableCellProperty),
            CellNode)) {
        return QVector<QPair<QString, QString>>();
    }
    QVector<QPair<QString, QString>> Attributes =
        NodeAttributes(CellNode);
    xmlFreeNode(CellNode);
    return Attributes;
}

/**
 * @brief Returns every block of one table, by its group number
 *        (Bug 448).
 *
 * The payload lives on the caption block and on every cell's
 * first block, so restamping it means touching all of them.
 * @param Document The document.
 * @param Group The table's group number.
 * @return The blocks, in document order.
 */
static QVector<QTextBlock> TableBlocksOfGroup(QTextDocument* Document,
                                              int Group)
{
    QVector<QTextBlock> Blocks;
    for (QTextBlock Block = Document->begin(); Block.isValid();
         Block = Block.next()) {
        if (Block.blockFormat()
                .property(DocTableGroupProperty)
                .toInt() == Group) {
            Blocks.append(Block);
        }
    }
    return Blocks;
}

/**
 * @brief True when a sourcecode element holds any non-blank content.
 *
 * The marker characters do not count: a block holding only its
 * gear is blank.
 * @param Document The document.
 * @param FirstBlockPos The position of the element's first block.
 * @return True when any block of the element has content.
 */
static bool SourceElementHasContent(const QTextDocument* Document,
                                    int FirstBlockPos)
{
    QTextBlock First = Document->findBlock(FirstBlockPos);
    if (!IsSourceCodeBlock(First)) {
        return false;
    }
    QVariant Shared = First.blockFormat().property(DocSourceProperty);
    QTextBlock Block = First;
    while (Block.isValid() && IsSourceCodeBlock(Block) &&
           Block.blockFormat().property(DocSourceProperty) == Shared) {
        if (!CodeTextOf(Block).trimmed().isEmpty()) {
            return true;
        }
        Block = Block.next();
    }
    return false;
}

void TextEditor::AddSourceOptionsActions(QMenu* Menu, const QPoint& Pos)
{
    QTextCursor Clicked = _TextEdit->cursorForPosition(Pos);
    int ClickPos = Clicked.position();
    QTextDocument* Document = _TextEdit->document();
    if (!HasOptionMarkerAt(Document, ClickPos) &&
        !HasOptionMarkerAt(Document, ClickPos - 1) &&
        !HasSrcPlaceholderAt(Document, ClickPos) &&
        !HasSrcPlaceholderAt(Document, ClickPos - 1)) {
        return;
    }
    // Bug 464 v9: the block comes from the gear itself, not
    // from the click position, which can land in the block
    // above at a boundary.
    QTextBlock Block = GearBlockAt(Document, ClickPos);
    if (!IsSourceCodeBlock(Block)) {
        return;
    }
    // Walk up to the element's first block: consecutive blocks
    // with the same attributes are one element, and the options
    // belong to the element, not the block clicked.
    QTextBlock First = Block;
    while (true) {
        QTextBlock Previous = First.previous();
        if (!Previous.isValid() || !IsSourceCodeBlock(Previous) ||
            Previous.blockFormat().property(DocSourceProperty) !=
                First.blockFormat().property(DocSourceProperty)) {
            break;
        }
        First = Previous;
    }
    int FirstBlockPos = First.position();
    QVector<QPair<QString, QString>> Attributes =
        SourceAttributesAt(Document, FirstBlockPos);
    QMenu* Options = Menu->addMenu(tr("Source code options"));
    // Bug 464 v7: the anchor is settable from the gear menu,
    // so an element without one can gain one. First in the
    // menu (alphabetical). Prefilled with the current anchor;
    // empty clears it.
    QAction* AnchorAction = Options->addAction(tr("Anchor..."));
    connect(AnchorAction, &QAction::triggered, this, [this, FirstBlockPos]() {
        QVector<QPair<QString, QString>> Current =
            SourceAttributesAt(_TextEdit->document(), FirstBlockPos);
        bool Ok = false;
        QString Value = QInputDialog::getText(
            this, tr("Source code options"), tr("Anchor:"),
            QLineEdit::Normal,
            AttributeOf(Current, QStringLiteral("anchor")), &Ok);
        if (!Ok) {
            return;
        }
        // Bug 464 v8: the editor asks the owner to change the
        // anchor. The owner refuses a delete when the anchor
        // is cited, renames the cross-references when it
        // changes, and calls SetElementAnchor to set it.
        emit AnchorChangeRequested(
            AttributeOf(Current, QStringLiteral("anchor")),
            Value.trimmed(), FirstBlockPos, true, QString());
    });
    QAction* MarkersAction = Options->addAction(tr("Markers"));
    MarkersAction->setCheckable(true);
    MarkersAction->setChecked(
        AttributeOf(Attributes, QStringLiteral("markers")) ==
        QLatin1String("true"));
    connect(MarkersAction, &QAction::triggered, this,
            [this, FirstBlockPos](bool Checked) {
                QVector<QPair<QString, QString>> Current =
                    SourceAttributesAt(_TextEdit->document(),
                                       FirstBlockPos);
                if (Checked) {
                    SetAttribute(Current, QStringLiteral("markers"),
                                 QStringLiteral("true"));
                } else {
                    RemoveAttribute(Current, QStringLiteral("markers"));
                }
                ApplySourceAttributes(FirstBlockPos, Current);
            });
    QAction* NameAction = Options->addAction(tr("Name..."));
    connect(NameAction, &QAction::triggered, this, [this, FirstBlockPos]() {
        QVector<QPair<QString, QString>> Current =
            SourceAttributesAt(_TextEdit->document(), FirstBlockPos);
        bool Ok = false;
        QString Value = QInputDialog::getText(
            this, tr("Source code options"), tr("Name:"),
            QLineEdit::Normal,
            AttributeOf(Current, QStringLiteral("name")), &Ok);
        if (!Ok) {
            return;
        }
        QString Trimmed = Value.trimmed();
        if (Trimmed.isEmpty()) {
            RemoveAttribute(Current, QStringLiteral("name"));
        } else {
            SetAttribute(Current, QStringLiteral("name"), Trimmed);
        }
        ApplySourceAttributes(FirstBlockPos, Current);
    });
    QAction* SrcAction = Options->addAction(tr("Src..."));
    connect(SrcAction, &QAction::triggered, this, [this, FirstBlockPos]() {
        QVector<QPair<QString, QString>> Current =
            SourceAttributesAt(_TextEdit->document(), FirstBlockPos);
        bool Ok = false;
        QString Value = QInputDialog::getText(
            this, tr("Source code options"), tr("Src:"),
            QLineEdit::Normal,
            AttributeOf(Current, QStringLiteral("src")), &Ok);
        if (!Ok) {
            return;
        }
        QString Trimmed = Value.trimmed();
        if (!Trimmed.isEmpty()) {
            if (SourceElementHasContent(_TextEdit->document(),
                                        FirstBlockPos)) {
                QMessageBox::warning(
                    this, tr("Source code options"),
                    tr("A sourcecode block with content cannot carry "
                       "a src attribute."));
                return;
            }
            SetAttribute(Current, QStringLiteral("src"), Trimmed);
        } else {
            RemoveAttribute(Current, QStringLiteral("src"));
        }
        ApplySourceAttributes(FirstBlockPos, Current);
    });
    QMenu* TypeMenu = Options->addMenu(tr("Type"));
    static const char* Types[] = {
        "abnf", "asn.1", "bash", "c", "c++", "cbor", "dtd",
        "java", "javascript", "json", "mib", "perl",
        "pseudocode", "python", "rnc", "xml", "yang",
    };
    QString CurrentType =
        AttributeOf(Attributes, QStringLiteral("type"));
    for (const char* Type : Types) {
        QString TypeName = QString::fromLatin1(Type);
        QAction* TypeAction = TypeMenu->addAction(TypeName);
        TypeAction->setCheckable(true);
        TypeAction->setChecked(TypeName == CurrentType);
        connect(TypeAction, &QAction::triggered, this,
                [this, FirstBlockPos, TypeName]() {
                    QVector<QPair<QString, QString>> Current =
                        SourceAttributesAt(_TextEdit->document(),
                                           FirstBlockPos);
                    SetAttribute(Current, QStringLiteral("type"),
                                 TypeName);
                    ApplySourceAttributes(FirstBlockPos, Current);
                });
    }
    TypeMenu->addSeparator();
    QAction* CustomAction = TypeMenu->addAction(tr("Custom..."));
    connect(CustomAction, &QAction::triggered, this,
            [this, FirstBlockPos]() {
                QVector<QPair<QString, QString>> Current =
                    SourceAttributesAt(_TextEdit->document(),
                                       FirstBlockPos);
                bool Ok = false;
                QString Value = QInputDialog::getText(
                    this, tr("Source code options"), tr("Type:"),
                    QLineEdit::Normal,
                    AttributeOf(Current, QStringLiteral("type")), &Ok);
                if (!Ok) {
                    return;
                }
                QString Trimmed = Value.trimmed();
                if (Trimmed.isEmpty()) {
                    RemoveAttribute(Current, QStringLiteral("type"));
                } else {
                    SetAttribute(Current, QStringLiteral("type"),
                                 Trimmed);
                }
                ApplySourceAttributes(FirstBlockPos, Current);
            });
    QAction* NoneAction = TypeMenu->addAction(tr("None"));
    connect(NoneAction, &QAction::triggered, this,
            [this, FirstBlockPos]() {
                QVector<QPair<QString, QString>> Current =
                    SourceAttributesAt(_TextEdit->document(),
                                       FirstBlockPos);
                RemoveAttribute(Current, QStringLiteral("type"));
                ApplySourceAttributes(FirstBlockPos, Current);
            });
    Menu->addSeparator();
}

/**
 * @brief Adds a Spacing submenu to a list options menu
 *        (Bug 464 v2).
 *
 * The spacing values (v3.rng): normal or compact, plus
 * None to remove the attribute. Shared by the <ul>, <ol>,
 * and <dl> menus: all three carry spacing.
 * @param Options The menu to add the submenu to.
 * @param Attributes The list's attributes, for the current
 *        value.
 * @param FirstBlockPos The position of the list's first
 *        block.
 */
void TextEditor::AddSpacingActions(
    QMenu* Options,
    const QVector<QPair<QString, QString>>& Attributes,
    int FirstBlockPos)
{
    QMenu* SpacingMenu = Options->addMenu(tr("Spacing"));
    // The spacing values (v3.rng): normal or compact, in
    // alphabetical order, as in the Style submenu.
    static const char* Spacings[] = {
        "compact", "normal",
    };
    QString CurrentSpacing =
        AttributeOf(Attributes, QStringLiteral("spacing"));
    for (const char* Spacing : Spacings) {
        QString SpacingName = QString::fromLatin1(Spacing);
        QAction* SpacingAction = SpacingMenu->addAction(SpacingName);
        SpacingAction->setCheckable(true);
        SpacingAction->setChecked(SpacingName == CurrentSpacing);
        connect(SpacingAction, &QAction::triggered, this,
                [this, FirstBlockPos, SpacingName]() {
                    QVector<QPair<QString, QString>> Current =
                        ListAttributesAt(_TextEdit->document(),
                                         FirstBlockPos);
                    SetAttribute(Current, QStringLiteral("spacing"),
                                 SpacingName);
                    ApplyListAttributes(FirstBlockPos, Current);
                });
    }
    SpacingMenu->addSeparator();
    QAction* SpacingNoneAction = SpacingMenu->addAction(tr("None"));
    connect(SpacingNoneAction, &QAction::triggered, this,
            [this, FirstBlockPos]() {
                QVector<QPair<QString, QString>> Current =
                    ListAttributesAt(_TextEdit->document(),
                                     FirstBlockPos);
                RemoveAttribute(Current, QStringLiteral("spacing"));
                ApplyListAttributes(FirstBlockPos, Current);
            });
}

void TextEditor::AddListOptionsActions(QMenu* Menu, const QPoint& Pos)
{
    QTextCursor Clicked = _TextEdit->cursorForPosition(Pos);
    int ClickPos = Clicked.position();
    QTextDocument* Document = _TextEdit->document();
    if (!HasOptionMarkerAt(Document, ClickPos) &&
        !HasOptionMarkerAt(Document, ClickPos - 1)) {
        return;
    }
    // Bug 464 v9: the block comes from the gear itself, not
    // from the click position, which can land in the block
    // above at a boundary.
    QTextBlock Block = GearBlockAt(Document, ClickPos);
    if (!IsListBlock(Block)) {
        return;
    }
    QString Kind =
        Block.blockFormat().property(DocListKindProperty).toString();
    // Every list kind carries options now (Bug 464 v2):
    // "list", "ul", "ol", and "dl". Nothing is added for any
    // other kind.
    if (Kind != QLatin1String("list") && Kind != QLatin1String("ul") &&
        Kind != QLatin1String("ol") && Kind != QLatin1String("dl")) {
        return;
    }
    // Walk up to the list's first block: blank lines between
    // items do not split the list, and the options belong to
    // the list, not the block clicked.
    QTextBlock First = FirstListBlock(Block);
    // Bug 464 v9: the gear is on the first block, so the walk
    // up must not cross into the previous list. A bulleted or
    // numbered list carries its own QTextList object, so when
    // the first block found and the clicked block sit in
    // different list objects, the walk crossed a boundary and
    // the clicked block is the first block.
    if (Kind != QLatin1String("dl") && First.textList() != nullptr &&
        Block.textList() != nullptr &&
        First.textList() != Block.textList()) {
        First = Block;
    }
    int FirstBlockPos = First.position();
    QVector<QPair<QString, QString>> Attributes =
        ListAttributesAt(Document, FirstBlockPos);
    QMenu* Options = Menu->addMenu(tr("List options"));
    // Bug 464 v7: the anchor is settable from the gear menu,
    // so a list without one can gain one. It sits first in
    // every kind's menu (alphabetical), ahead of the kind's
    // own options. Prefilled with the current anchor; empty
    // clears it.
    QAction* AnchorAction = Options->addAction(tr("Anchor..."));
    connect(AnchorAction, &QAction::triggered, this, [this, FirstBlockPos]() {
        QVector<QPair<QString, QString>> Current =
            ListAttributesAt(_TextEdit->document(), FirstBlockPos);
        bool Ok = false;
        QString Value = QInputDialog::getText(
            this, tr("List options"), tr("Anchor:"),
            QLineEdit::Normal,
            AttributeOf(Current, QStringLiteral("anchor")), &Ok);
        if (!Ok) {
            return;
        }
        // Bug 464 v8: the editor asks the owner to change the
        // anchor. The owner refuses a delete when the anchor
        // is cited, renames the cross-references when it
        // changes, and calls SetElementAnchor to set it.
        emit AnchorChangeRequested(
            AttributeOf(Current, QStringLiteral("anchor")),
            Value.trimmed(), FirstBlockPos, false, QString());
    });
    if (Kind == QLatin1String("list")) {
        // The options in alphabetical order, as in the
        // sourcecode menu: Counter, Hang indent, Style.
        QAction* CounterAction = Options->addAction(tr("Counter..."));
        connect(CounterAction, &QAction::triggered, this,
                [this, FirstBlockPos]() {
                    QVector<QPair<QString, QString>> Current =
                        ListAttributesAt(_TextEdit->document(),
                                         FirstBlockPos);
                    bool Ok = false;
                    QString Value = QInputDialog::getText(
                        this, tr("List options"), tr("Counter:"),
                        QLineEdit::Normal,
                        AttributeOf(Current, QStringLiteral("counter")),
                        &Ok);
                    if (!Ok) {
                        return;
                    }
                    QString Trimmed = Value.trimmed();
                    if (Trimmed.isEmpty()) {
                        RemoveAttribute(Current,
                                        QStringLiteral("counter"));
                    } else {
                        SetAttribute(Current, QStringLiteral("counter"),
                                     Trimmed);
                    }
                    ApplyListAttributes(FirstBlockPos, Current);
                });
        QAction* HangIndentAction =
            Options->addAction(tr("Hang indent..."));
        connect(HangIndentAction, &QAction::triggered, this,
                [this, FirstBlockPos]() {
                    QVector<QPair<QString, QString>> Current =
                        ListAttributesAt(_TextEdit->document(),
                                         FirstBlockPos);
                    bool Ok = false;
                    QString Value = QInputDialog::getText(
                        this, tr("List options"), tr("Hang indent:"),
                        QLineEdit::Normal,
                        AttributeOf(Current,
                                    QStringLiteral("hangIndent")),
                        &Ok);
                    if (!Ok) {
                        return;
                    }
                    QString Trimmed = Value.trimmed();
                    if (Trimmed.isEmpty()) {
                        RemoveAttribute(Current,
                                        QStringLiteral("hangIndent"));
                    } else {
                        SetAttribute(Current,
                                     QStringLiteral("hangIndent"),
                                     Trimmed);
                    }
                    ApplyListAttributes(FirstBlockPos, Current);
                });
        QMenu* StyleMenu = Options->addMenu(tr("Style"));
        // The plain style values (RFC 7991 section 3.4 maps
        // them onto <dl>, <ul>, and <ol>), in alphabetical
        // order. A format string such as "format (%d)" goes
        // through Custom.
        static const char* Styles[] = {
            "counter", "empty", "hanging", "letters", "numbers",
            "symbols",
        };
        QString CurrentStyle =
            AttributeOf(Attributes, QStringLiteral("style"));
        for (const char* Style : Styles) {
            QString StyleName = QString::fromLatin1(Style);
            QAction* StyleAction = StyleMenu->addAction(StyleName);
            StyleAction->setCheckable(true);
            StyleAction->setChecked(StyleName == CurrentStyle);
            connect(StyleAction, &QAction::triggered, this,
                    [this, FirstBlockPos, StyleName]() {
                        QVector<QPair<QString, QString>> Current =
                            ListAttributesAt(_TextEdit->document(),
                                             FirstBlockPos);
                        SetAttribute(Current, QStringLiteral("style"),
                                     StyleName);
                        ApplyListAttributes(FirstBlockPos, Current);
                    });
        }
        StyleMenu->addSeparator();
        QAction* CustomAction = StyleMenu->addAction(tr("Custom..."));
        connect(CustomAction, &QAction::triggered, this,
                [this, FirstBlockPos]() {
                    QVector<QPair<QString, QString>> Current =
                        ListAttributesAt(_TextEdit->document(),
                                         FirstBlockPos);
                    bool Ok = false;
                    QString Value = QInputDialog::getText(
                        this, tr("List options"), tr("Style:"),
                        QLineEdit::Normal,
                        AttributeOf(Current, QStringLiteral("style")),
                        &Ok);
                    if (!Ok) {
                        return;
                    }
                    QString Trimmed = Value.trimmed();
                    if (Trimmed.isEmpty()) {
                        RemoveAttribute(Current,
                                        QStringLiteral("style"));
                    } else {
                        SetAttribute(Current, QStringLiteral("style"),
                                     Trimmed);
                    }
                    ApplyListAttributes(FirstBlockPos, Current);
                });
        QAction* NoneAction = StyleMenu->addAction(tr("None"));
        connect(NoneAction, &QAction::triggered, this,
                [this, FirstBlockPos]() {
                    QVector<QPair<QString, QString>> Current =
                        ListAttributesAt(_TextEdit->document(),
                                         FirstBlockPos);
                    RemoveAttribute(Current, QStringLiteral("style"));
                    ApplyListAttributes(FirstBlockPos, Current);
                });
    } else if (Kind == QLatin1String("ul")) {
        // A <ul>: its bare and empty switches, indent, and
        // spacing, in alphabetical order.
        QAction* BareAction = Options->addAction(tr("Bare"));
        BareAction->setCheckable(true);
        BareAction->setChecked(
            AttributeOf(Attributes, QStringLiteral("bare")) ==
            QLatin1String("true"));
        connect(BareAction, &QAction::triggered, this,
                [this, FirstBlockPos](bool Checked) {
                    QVector<QPair<QString, QString>> Current =
                        ListAttributesAt(_TextEdit->document(),
                                         FirstBlockPos);
                    if (Checked) {
                        // Bare sits inside empty's group in the
                        // schema: a file with bare but no empty
                        // fails validation, so turning on bare
                        // writes empty alongside it (Bug 312
                        // ruling 11).
                        SetAttribute(Current, QStringLiteral("bare"),
                                     QStringLiteral("true"));
                        SetAttribute(Current, QStringLiteral("empty"),
                                     QStringLiteral("true"));
                    } else {
                        RemoveAttribute(Current,
                                        QStringLiteral("bare"));
                    }
                    ApplyListAttributes(FirstBlockPos, Current);
                });
        QAction* EmptyAction = Options->addAction(tr("Empty"));
        EmptyAction->setCheckable(true);
        EmptyAction->setChecked(
            AttributeOf(Attributes, QStringLiteral("empty")) ==
            QLatin1String("true"));
        connect(EmptyAction, &QAction::triggered, this,
                [this, FirstBlockPos](bool Checked) {
                    QVector<QPair<QString, QString>> Current =
                        ListAttributesAt(_TextEdit->document(),
                                         FirstBlockPos);
                    if (Checked) {
                        SetAttribute(Current, QStringLiteral("empty"),
                                     QStringLiteral("true"));
                    } else {
                        RemoveAttribute(Current,
                                        QStringLiteral("empty"));
                    }
                    ApplyListAttributes(FirstBlockPos, Current);
                });
        // Bug 464 v2: indent is free text (v3.rng gives no
        // enumerated values, default "3"). Bug 464 v3: prefilled
        // with the current value, or the default "3" when unset;
        // empty clears the attribute, as with Counter and Hang
        // indent.
        QAction* IndentAction = Options->addAction(tr("Indent..."));
        connect(IndentAction, &QAction::triggered, this,
                [this, FirstBlockPos]() {
                    QVector<QPair<QString, QString>> Current =
                        ListAttributesAt(_TextEdit->document(),
                                         FirstBlockPos);
                    bool Ok = false;
                    QString Value = QInputDialog::getText(
                        this, tr("List options"), tr("Indent:"),
                        QLineEdit::Normal,
                        PrefillAttribute(Current, QStringLiteral("indent"), QStringLiteral("3")),
                        &Ok);
                    if (!Ok) {
                        return;
                    }
                    QString Trimmed = Value.trimmed();
                    if (Trimmed.isEmpty()) {
                        RemoveAttribute(Current,
                                        QStringLiteral("indent"));
                    } else {
                        SetAttribute(Current, QStringLiteral("indent"),
                                     Trimmed);
                    }
                    ApplyListAttributes(FirstBlockPos, Current);
                });
        AddSpacingActions(Options, Attributes, FirstBlockPos);
    } else if (Kind == QLatin1String("ol")) {
        // An <ol>: its type, start, group, spacing, and
        // indent, in alphabetical order (Bug 464 v2). The
        // schema gives no enumerated values for type (default
        // "1"), so type is free text, as are start, group,
        // and indent. Bug 464 v3: each is prefilled with its
        // current value, or its schema default when unset
        // (type "1", start "1", indent "adaptive"; group has
        // no default); empty clears it.
        QAction* GroupAction = Options->addAction(tr("Group..."));
        connect(GroupAction, &QAction::triggered, this,
                [this, FirstBlockPos]() {
                    QVector<QPair<QString, QString>> Current =
                        ListAttributesAt(_TextEdit->document(),
                                         FirstBlockPos);
                    bool Ok = false;
                    QString Value = QInputDialog::getText(
                        this, tr("List options"), tr("Group:"),
                        QLineEdit::Normal,
                        AttributeOf(Current, QStringLiteral("group")),
                        &Ok);
                    if (!Ok) {
                        return;
                    }
                    QString Trimmed = Value.trimmed();
                    if (Trimmed.isEmpty()) {
                        RemoveAttribute(Current,
                                        QStringLiteral("group"));
                    } else {
                        SetAttribute(Current, QStringLiteral("group"),
                                     Trimmed);
                    }
                    ApplyListAttributes(FirstBlockPos, Current);
                });
        QAction* IndentAction = Options->addAction(tr("Indent..."));
        connect(IndentAction, &QAction::triggered, this,
                [this, FirstBlockPos]() {
                    QVector<QPair<QString, QString>> Current =
                        ListAttributesAt(_TextEdit->document(),
                                         FirstBlockPos);
                    bool Ok = false;
                    QString Value = QInputDialog::getText(
                        this, tr("List options"), tr("Indent:"),
                        QLineEdit::Normal,
                        PrefillAttribute(Current, QStringLiteral("indent"), QStringLiteral("adaptive")),
                        &Ok);
                    if (!Ok) {
                        return;
                    }
                    QString Trimmed = Value.trimmed();
                    if (Trimmed.isEmpty()) {
                        RemoveAttribute(Current,
                                        QStringLiteral("indent"));
                    } else {
                        SetAttribute(Current, QStringLiteral("indent"),
                                     Trimmed);
                    }
                    ApplyListAttributes(FirstBlockPos, Current);
                });
        AddSpacingActions(Options, Attributes, FirstBlockPos);
        QAction* StartAction = Options->addAction(tr("Start..."));
        connect(StartAction, &QAction::triggered, this,
                [this, FirstBlockPos]() {
                    QVector<QPair<QString, QString>> Current =
                        ListAttributesAt(_TextEdit->document(),
                                         FirstBlockPos);
                    bool Ok = false;
                    QString Value = QInputDialog::getText(
                        this, tr("List options"), tr("Start:"),
                        QLineEdit::Normal,
                        PrefillAttribute(Current, QStringLiteral("start"), QStringLiteral("1")),
                        &Ok);
                    if (!Ok) {
                        return;
                    }
                    QString Trimmed = Value.trimmed();
                    if (Trimmed.isEmpty()) {
                        RemoveAttribute(Current,
                                        QStringLiteral("start"));
                    } else {
                        SetAttribute(Current, QStringLiteral("start"),
                                     Trimmed);
                    }
                    ApplyListAttributes(FirstBlockPos, Current);
                });
        QAction* TypeAction = Options->addAction(tr("Type..."));
        connect(TypeAction, &QAction::triggered, this,
                [this, FirstBlockPos]() {
                    QVector<QPair<QString, QString>> Current =
                        ListAttributesAt(_TextEdit->document(),
                                         FirstBlockPos);
                    bool Ok = false;
                    QString Value = QInputDialog::getText(
                        this, tr("List options"), tr("Type:"),
                        QLineEdit::Normal,
                        PrefillAttribute(Current, QStringLiteral("type"), QStringLiteral("1")),
                        &Ok);
                    if (!Ok) {
                        return;
                    }
                    QString Trimmed = Value.trimmed();
                    if (Trimmed.isEmpty()) {
                        RemoveAttribute(Current,
                                        QStringLiteral("type"));
                    } else {
                        SetAttribute(Current, QStringLiteral("type"),
                                     Trimmed);
                    }
                    ApplyListAttributes(FirstBlockPos, Current);
                });
    } else {
        // A <dl>: its spacing, newline, and indent, in
        // alphabetical order (Bug 464 v2).
        QAction* IndentAction = Options->addAction(tr("Indent..."));
        connect(IndentAction, &QAction::triggered, this,
                [this, FirstBlockPos]() {
                    QVector<QPair<QString, QString>> Current =
                        ListAttributesAt(_TextEdit->document(),
                                         FirstBlockPos);
                    bool Ok = false;
                    QString Value = QInputDialog::getText(
                        this, tr("List options"), tr("Indent:"),
                        QLineEdit::Normal,
                        PrefillAttribute(Current, QStringLiteral("indent"), QStringLiteral("3")),
                        &Ok);
                    if (!Ok) {
                        return;
                    }
                    QString Trimmed = Value.trimmed();
                    if (Trimmed.isEmpty()) {
                        RemoveAttribute(Current,
                                        QStringLiteral("indent"));
                    } else {
                        SetAttribute(Current, QStringLiteral("indent"),
                                     Trimmed);
                    }
                    ApplyListAttributes(FirstBlockPos, Current);
                });
        QAction* NewlineAction = Options->addAction(tr("Newline"));
        NewlineAction->setCheckable(true);
        // Bug 464 v10: xml2rfc puts a bare <dl> definition on
        // the next line, so an absent newline reads as on, not
        // off. The box shows the effective value: checked
        // unless the attribute is explicitly "false".
        NewlineAction->setChecked(
            AttributeOf(Attributes, QStringLiteral("newline")) !=
            QLatin1String("false"));
        connect(NewlineAction, &QAction::triggered, this,
                [this, FirstBlockPos](bool Checked) {
                    QVector<QPair<QString, QString>> Current =
                        ListAttributesAt(_TextEdit->document(),
                                         FirstBlockPos);
                    // Bug 464 v10: always write the value
                    // explicitly, true or false. Removing it on
                    // uncheck would fall back to the next-line
                    // default, the opposite of what unchecking
                    // means, and xml2rfc mishandles the absent
                    // and "false" cases (issue 1045).
                    SetAttribute(Current, QStringLiteral("newline"),
                                 Checked ? QStringLiteral("true")
                                         : QStringLiteral("false"));
                    ApplyListAttributes(FirstBlockPos, Current);
                });
        AddSpacingActions(Options, Attributes, FirstBlockPos);
    }
    Menu->addSeparator();
}

void TextEditor::AddTableAlignActions(
    QMenu* Options,
    const QVector<QPair<QString, QString>>& Attributes,
    int FirstBlockPos, const QString& PartKind)
{
    QMenu* AlignMenu = Options->addMenu(tr("Align"));
    // The align values (v3.rng): left, center, or right, in
    // alphabetical order, as in the Style submenu.
    static const char* Aligns[] = {
        "center", "left", "right",
    };
    QString CurrentAlign =
        AttributeOf(Attributes, QStringLiteral("align"));
    for (const char* Align : Aligns) {
        QString AlignName = QString::fromLatin1(Align);
        QAction* AlignAction = AlignMenu->addAction(AlignName);
        AlignAction->setCheckable(true);
        AlignAction->setChecked(AlignName == CurrentAlign);
        connect(AlignAction, &QAction::triggered, this,
                [this, FirstBlockPos, PartKind, AlignName]() {
                    QVector<QPair<QString, QString>> Current;
                    if (PartKind == QLatin1String("table")) {
                        Current = TableAttributesAt(
                            _TextEdit->document(), FirstBlockPos);
                    } else {
                        Current = TableCellAttributesAt(
                            _TextEdit->document(), FirstBlockPos);
                    }
                    SetAttribute(Current, QStringLiteral("align"),
                                 AlignName);
                    if (PartKind == QLatin1String("table")) {
                        ApplyTableAttributes(FirstBlockPos, Current);
                    } else {
                        ApplyTableCellAttributes(FirstBlockPos, Current);
                    }
                });
    }
    AlignMenu->addSeparator();
    QAction* NoneAction = AlignMenu->addAction(tr("None"));
    connect(NoneAction, &QAction::triggered, this,
            [this, FirstBlockPos, PartKind]() {
                QVector<QPair<QString, QString>> Current;
                if (PartKind == QLatin1String("table")) {
                    Current = TableAttributesAt(_TextEdit->document(),
                                                FirstBlockPos);
                } else {
                    Current = TableCellAttributesAt(
                        _TextEdit->document(), FirstBlockPos);
                }
                RemoveAttribute(Current, QStringLiteral("align"));
                if (PartKind == QLatin1String("table")) {
                    ApplyTableAttributes(FirstBlockPos, Current);
                } else {
                    ApplyTableCellAttributes(FirstBlockPos, Current);
                }
            });
}

/**
 * @brief Adds one table part's submenu to a gear menu (Bug 448 v2).
 *
 * One submenu per part: its Anchor... action first, then the
 * part's own options (Align for a table or a cell, Caption for
 * the table). A combined cell gear calls this once per part it
 * covers, outer part first, so the header gear shows the Table
 * head, Row, and Header cell submenus, a row gear shows the Row
 * and Cell submenus, and a plain cell gear shows only its own.
 * @param Menu The menu being built.
 * @param PartKind The part: "table", "thead", "tbody", "tfoot",
 *        "tr", "td", or "th".
 * @param Attributes The part's attributes, for the current values.
 * @param FirstBlockPos A block of the table.
 */
void TextEditor::AddTablePartSubmenu(
    QMenu* Menu, const QString& PartKind,
    const QVector<QPair<QString, QString>>& Attributes,
    int FirstBlockPos)
{
    QString MenuTitle;
    if (PartKind == QLatin1String("table")) {
        MenuTitle = tr("Table options");
    } else if (PartKind == QLatin1String("thead")) {
        MenuTitle = tr("Table head options");
    } else if (PartKind == QLatin1String("tbody")) {
        MenuTitle = tr("Table body options");
    } else if (PartKind == QLatin1String("tfoot")) {
        MenuTitle = tr("Table foot options");
    } else if (PartKind == QLatin1String("tr")) {
        MenuTitle = tr("Row options");
    } else if (PartKind == QLatin1String("th")) {
        MenuTitle = tr("Header cell options");
    } else {
        MenuTitle = tr("Cell options");
    }
    QMenu* Options = Menu->addMenu(MenuTitle);
    // Bug 464 v7: the anchor is settable from the gear menu,
    // so a part without one can gain one. It sits first in
    // every part's menu (alphabetical), ahead of the part's
    // own options. Prefilled with the current anchor; empty
    // clears it.
    QAction* AnchorAction = Options->addAction(tr("Anchor..."));
    connect(AnchorAction, &QAction::triggered, this,
            [this, FirstBlockPos, PartKind, MenuTitle]() {
                QVector<QPair<QString, QString>> Current;
                if (PartKind == QLatin1String("table")) {
                    Current = TableAttributesAt(_TextEdit->document(),
                                                FirstBlockPos);
                } else if (PartKind == QLatin1String("thead") ||
                           PartKind == QLatin1String("tbody") ||
                           PartKind == QLatin1String("tfoot")) {
                    Current = TableSectionAttributesAt(
                        _TextEdit->document(), FirstBlockPos, PartKind);
                } else if (PartKind == QLatin1String("tr")) {
                    Current = TableRowAttributesAt(
                        _TextEdit->document(), FirstBlockPos);
                } else {
                    Current = TableCellAttributesAt(
                        _TextEdit->document(), FirstBlockPos);
                }
                bool Ok = false;
                QString Value = QInputDialog::getText(
                    this, MenuTitle, tr("Anchor:"), QLineEdit::Normal,
                    AttributeOf(Current, QStringLiteral("anchor")),
                    &Ok);
                if (!Ok) {
                    return;
                }
                // Bug 464 v8: the editor asks the owner to change
                // the anchor. The owner refuses a delete when the
                // anchor is cited, renames the cross-references
                // when it changes, and calls SetTablePartAnchor
                // to set it.
                emit AnchorChangeRequested(
                    AttributeOf(Current, QStringLiteral("anchor")),
                    Value.trimmed(), FirstBlockPos, false, PartKind);
            });
    if (PartKind == QLatin1String("table")) {
        // The table's own options, in alphabetical order after
        // the anchor: Align, then Caption.
        AddTableAlignActions(Options, Attributes, FirstBlockPos,
                             PartKind);
        QAction* CaptionAction = Options->addAction(tr("Caption..."));
        connect(CaptionAction, &QAction::triggered, this,
                [this, FirstBlockPos]() {
                    QTextDocument* Document = _TextEdit->document();
                    QTextBlock Caption =
                        Document->findBlock(FirstBlockPos);
                    if (!IsTableCaptionBlock(Caption)) {
                        return;
                    }
                    // The current caption, without the gear.
                    QString Current = Caption.text();
                    bool HasGear = HasOptionMarkerAt(
                        Document, Caption.position());
                    if (HasGear && Current.startsWith(OptionGlyph)) {
                        Current.remove(0, 1);
                    }
                    bool Ok = false;
                    QString Value = QInputDialog::getText(
                        this, tr("Table options"), tr("Caption:"),
                        QLineEdit::Normal, Current, &Ok);
                    if (!Ok) {
                        return;
                    }
                    // The caption lives in the caption block's
                    // text: replace it after the gear, keeping
                    // the caption's bold. An empty caption writes
                    // no <name>. The read-back carries it into
                    // the model.
                    QTextCursor Cursor(Document);
                    Cursor.beginEditBlock();
                    int Start = Caption.position() +
                                (HasGear ? 1 : 0);
                    int End = Caption.position() +
                              Caption.length() - 1;
                    Cursor.setPosition(Start);
                    if (End > Start) {
                        Cursor.setPosition(End,
                                           QTextCursor::KeepAnchor);
                    }
                    QTextCharFormat Bold;
                    Bold.setFontWeight(QFont::Bold);
                    Cursor.insertText(Value.trimmed(), Bold);
                    Cursor.endEditBlock();
                    _TextEdit->setFocus();
                });
    } else if (PartKind == QLatin1String("td") ||
               PartKind == QLatin1String("th")) {
        // A cell's own option, after the anchor: Align.
        AddTableAlignActions(Options, Attributes, FirstBlockPos,
                             PartKind);
    }
}

void TextEditor::AddTableOptionsActions(QMenu* Menu, const QPoint& Pos)
{
    QTextCursor Clicked = _TextEdit->cursorForPosition(Pos);
    int ClickPos = Clicked.position();
    QTextDocument* Document = _TextEdit->document();
    QString Kind = TableGearKindAt(Document, ClickPos);
    if (Kind.isEmpty()) {
        Kind = TableGearKindAt(Document, ClickPos - 1);
    }
    if (Kind.isEmpty()) {
        return;
    }
    // Bug 464 v9: the block comes from the gear itself, not
    // from the click position, which can land in the block
    // above at a boundary.
    QTextBlock Block = GearBlockAt(Document, ClickPos);
    if (!IsTableBlock(Block)) {
        return;
    }
    int FirstBlockPos = Block.position();
    if (Kind == QLatin1String("table")) {
        // The caption gear: the table's own submenu only.
        AddTablePartSubmenu(Menu, Kind,
                            TableAttributesAt(Document, FirstBlockPos),
                            FirstBlockPos);
    } else if (Kind == QLatin1String("th") ||
               Kind == QLatin1String("td")) {
        // Bug 448 v2: one gear covers the cell and its row and
        // section, so the menu holds a submenu for each part
        // that applies here, outer part first.
        int Row = -1;
        int Column = -1;
        if (!TableCellOfBlock(Document, Block, Row, Column)) {
            return;
        }
        xmlNodePtr Shell = nullptr;
        QVector<DocTableRowMark> Marks;
        if (!TablePayloadOf(Block, Shell, Marks)) {
            return;
        }
        QTextTable* Frame = TableOfBlock(Document, Block);
        int Rows = Frame != nullptr ? Frame->rows() : 0;
        QString Section = SectionOfRow(Marks, Row);
        bool FirstOfSection =
            Column == 0 &&
            Row == FirstRowOfSection(Marks, Rows, Section);
        xmlFreeNode(Shell);
        if (FirstOfSection) {
            AddTablePartSubmenu(
                Menu, Section,
                TableSectionAttributesAt(Document, FirstBlockPos, Section),
                FirstBlockPos);
        }
        if (Column == 0 && Row >= 0) {
            AddTablePartSubmenu(
                Menu, QStringLiteral("tr"),
                TableRowAttributesAt(Document, FirstBlockPos),
                FirstBlockPos);
        }
        AddTablePartSubmenu(
            Menu, Kind,
            TableCellAttributesAt(Document, FirstBlockPos),
            FirstBlockPos);
    } else if (Kind == QLatin1String("thead") ||
               Kind == QLatin1String("tbody") ||
               Kind == QLatin1String("tfoot")) {
        // A section gear on its own (a build that still wrote
        // one per part): its own submenu only.
        AddTablePartSubmenu(
            Menu, Kind,
            TableSectionAttributesAt(Document, FirstBlockPos, Kind),
            FirstBlockPos);
    } else if (Kind == QLatin1String("tr")) {
        // A row gear on its own: its own submenu only.
        AddTablePartSubmenu(
            Menu, Kind,
            TableRowAttributesAt(Document, FirstBlockPos),
            FirstBlockPos);
    }
    Menu->addSeparator();
}

void TextEditor::AddSpellActions(QMenu* Menu, const QPoint& Pos)
{
    if (_SpellChecker == nullptr || !_SpellChecker->IsAvailable()) {
        return;
    }
    QTextCursor Clicked = _TextEdit->cursorForPosition(Pos);
    QTextBlock Block = Clicked.block();
    if (!Block.isValid()) {
        return;
    }
    // Sourcecode and artwork blocks are never checked, so there
    // is no misspelled word to offer anything for there.
    if (IsSourceCodeBlock(Block) || IsFigureArtworkBlock(Block)) {
        return;
    }
    int Start = -1;
    int End = -1;
    if (!SpellHighlighter::WordAt(Block.text(), Clicked.positionInBlock(),
                                  Start, End)) {
        return;
    }
    QString Word = Block.text().mid(Start, End - Start);
    if (_SpellChecker->CheckWord(Word)) {
        return;
    }
    int WordStart = Block.position() + Start;
    int WordEnd = Block.position() + End;
    QStringList Suggestions = _SpellChecker->Suggestions(Word);
    // Bug 524: the suggestions read as one alphabetical list
    // (case-insensitive, as a dictionary would), not in the
    // spell checker's relevance order.
    Suggestions.sort(Qt::CaseInsensitive);
    for (const QString& Suggestion : Suggestions) {
        QAction* Action = Menu->addAction(Suggestion);
        connect(Action, &QAction::triggered, this,
                [this, WordStart, WordEnd, Suggestion]() {
                    QTextCursor Replace(_TextEdit->document());
                    Replace.setPosition(WordStart);
                    Replace.setPosition(WordEnd, QTextCursor::KeepAnchor);
                    Replace.insertText(Suggestion);
                });
    }
    if (!Suggestions.isEmpty()) {
        Menu->addSeparator();
    }
    // Add to dictionary: the personal dictionary takes the word
    // for every draft; the project dictionary takes it for this
    // draft only. The project choice is off while no draft file
    // is open, since there is no project file to keep it in.
    QMenu* AddMenu = Menu->addMenu(tr("Add to dictionary"));
    QAction* Personal = AddMenu->addAction(tr("Personal dictionary"));
    connect(Personal, &QAction::triggered, this, [this, Word]() {
        _SpellChecker->AddToPersonal(Word);
        if (_SpellHighlighter != nullptr) {
            _SpellHighlighter->rehighlight();
        }
    });
    QAction* Project = AddMenu->addAction(tr("Project dictionary"));
    Project->setEnabled(!_SpellChecker->ProjectDictionaryPath().isEmpty());
    connect(Project, &QAction::triggered, this, [this, Word]() {
        _SpellChecker->AddToProject(Word);
        if (_SpellHighlighter != nullptr) {
            _SpellHighlighter->rehighlight();
        }
    });
    QAction* Ignore = Menu->addAction(tr("Ignore word"));
    connect(Ignore, &QAction::triggered, this, [this, Word]() {
        _SpellChecker->IgnoreWord(Word);
        if (_SpellHighlighter != nullptr) {
            _SpellHighlighter->rehighlight();
        }
    });
    Menu->addSeparator();
}

void TextEditor::ApplySourceAttributes(
    int FirstBlockPos,
    const QVector<QPair<QString, QString>>& Attributes)
{
    QTextDocument* Document = _TextEdit->document();
    QTextBlock First = Document->findBlock(FirstBlockPos);
    if (!IsSourceCodeBlock(First)) {
        return;
    }
    QVariant Shared = First.blockFormat().property(DocSourceProperty);
    // Walk up to the element's first block, then collect every
    // block of the element before restamping any of them:
    // restamping changes the attributes the grouping reads.
    while (true) {
        QTextBlock Previous = First.previous();
        if (!Previous.isValid() || !IsSourceCodeBlock(Previous) ||
            Previous.blockFormat().property(DocSourceProperty) != Shared) {
            break;
        }
        First = Previous;
    }
    QVector<QTextBlock> Blocks;
    QTextBlock Block = First;
    while (Block.isValid() && IsSourceCodeBlock(Block) &&
           Block.blockFormat().property(DocSourceProperty) == Shared) {
        Blocks.append(Block);
        Block = Block.next();
    }
    QTextCursor Cursor(Document);
    Cursor.beginEditBlock();
    for (const QTextBlock& Target : Blocks) {
        Cursor.setPosition(Target.position());
        SetSourceProperties(Cursor, Attributes);
    }
    UpdateOptionMarkers();
    Cursor.endEditBlock();
    _TextEdit->setFocus();
}

void TextEditor::SetElementAnchor(int FirstBlockPos, bool IsSourceCode,
                                  const QString& Anchor)
{
    // Bug 464 v8: the owner calls this after checking the old
    // anchor is not cited and updating the cross-references.
    // An empty anchor clears it.
    QTextDocument* Document = _TextEdit->document();
    QVector<QPair<QString, QString>> Current;
    if (IsSourceCode) {
        Current = SourceAttributesAt(Document, FirstBlockPos);
    } else {
        Current = ListAttributesAt(Document, FirstBlockPos);
    }
    QString Trimmed = Anchor.trimmed();
    if (Trimmed.isEmpty()) {
        RemoveAttribute(Current, QStringLiteral("anchor"));
    } else {
        SetAttribute(Current, QStringLiteral("anchor"), Trimmed);
    }
    if (IsSourceCode) {
        ApplySourceAttributes(FirstBlockPos, Current);
    } else {
        ApplyListAttributes(FirstBlockPos, Current);
    }
}

void TextEditor::RestoreSelection(int Start, int End)
{
    QTextDocument* Document = _TextEdit->document();
    if (Document == nullptr) {
        return;
    }
    int Max = Document->characterCount() - 1;
    if (Max < 0) {
        Max = 0;
    }
    int From = qBound(0, Start, Max);
    int To = qBound(0, End, Max);
    QTextCursor Cursor(Document);
    Cursor.setPosition(From);
    Cursor.setPosition(To, QTextCursor::KeepAnchor);
    _TextEdit->setTextCursor(Cursor);
}

void TextEditor::ApplyListAttributes(
    int FirstBlockPos,
    const QVector<QPair<QString, QString>>& Attributes)
{
    QTextDocument* Document = _TextEdit->document();
    QTextBlock First = FirstListBlock(Document->findBlock(FirstBlockPos));
    if (!IsListBlock(First)) {
        return;
    }
    QString Kind =
        First.blockFormat().property(DocListKindProperty).toString();
    // Bug 464 v9: the walk up must not cross into the previous
    // list. A bulleted or numbered list carries its own
    // QTextList object, so when the first block found and the
    // block at the given position sit in different list
    // objects, the walk crossed a boundary and the given block
    // is the one to use.
    QTextBlock Given = Document->findBlock(FirstBlockPos);
    if (Kind != QLatin1String("dl") && First.textList() != nullptr &&
        Given.textList() != nullptr &&
        First.textList() != Given.textList()) {
        First = Given;
        Kind = First.blockFormat().property(DocListKindProperty).toString();
    }
    QVariant GroupValue =
        First.blockFormat().property(DocListGroupProperty);
    QVariant Shared = First.blockFormat().property(DocListProperty);
    // Collect every block of the list before restamping any of
    // them: restamping changes the attributes the grouping
    // reads. Blank lines between items are stepped over, not
    // collected, so the list does not split around them.
    QVector<QTextBlock> Blocks;
    QTextBlock Block = First;
    while (Block.isValid()) {
        if (!IsListBlock(Block) ||
            Block.blockFormat().property(DocListKindProperty).toString() !=
                Kind ||
            Block.blockFormat().property(DocListGroupProperty) !=
                GroupValue ||
            Block.blockFormat().property(DocListProperty) != Shared) {
            if (IsBlankPlainBlock(Block)) {
                QTextBlock Next = Block.next();
                while (Next.isValid() && IsBlankPlainBlock(Next)) {
                    Next = Next.next();
                }
                if (Next.isValid() && IsListBlock(Next) &&
                    Next.blockFormat()
                            .property(DocListKindProperty)
                            .toString() == Kind &&
                    Next.blockFormat().property(DocListGroupProperty) ==
                        GroupValue &&
                    Next.blockFormat().property(DocListProperty) ==
                        Shared) {
                    Block = Next;
                    continue;
                }
            }
            break;
        }
        Blocks.append(Block);
        Block = Block.next();
    }
    QTextCursor Cursor(Document);
    Cursor.beginEditBlock();
    QTextList* List = nullptr;
    for (const QTextBlock& Target : Blocks) {
        if (List == nullptr) {
            List = Target.textList();
        }
        // Each block keeps its own role: an item stays an
        // item, a term stays a term, a continuation stays a
        // continuation.
        QString Role = ListRoleOf(Target);
        Cursor.setPosition(Target.position());
        SetListProperties(Cursor, Kind, GroupValue.toInt(), Attributes,
                          Role);
    }
    // A style change re-labels the list: numbers become
    // numbers, letters become letters, the rest show bullets.
    // The list object holds the display format, so it is
    // restamped too. Only an inline <list> is driven by its
    // style; a <ul> is always bullets. An <ol> is restamped
    // too, so a Start change takes effect at once (Bug 464
    // v5); its format carries the start attribute.
    if (List != nullptr &&
        (Kind == QLatin1String("list") ||
         Kind == QLatin1String("ol"))) {
        List->setFormat(ListFormatForKind(Kind, Attributes));
    }
    UpdateOptionMarkers();
    Cursor.endEditBlock();
    _TextEdit->setFocus();
}

/**
 * @brief Reads a table payload variant map from a block (Bug 448).
 * @param Block The block.
 * @param Map Receives the map.
 * @return False when the block carries no table payload.
 */
static bool TablePayloadMapOf(const QTextBlock& Block, QVariantMap& Map)
{
    QVariant Value =
        Block.blockFormat().property(DocTablePayloadProperty);
    if (!Value.isValid()) {
        return false;
    }
    Map = Value.toMap();
    return Map.contains(QStringLiteral("rows"));
}

/**
 * @brief Writes one table payload variant onto every block of
 *        the table (Bug 448).
 *
 * Only the payload property changes: each block keeps its kind,
 * group, role, and cell property.
 * @param Document The document.
 * @param Group The table's group number.
 * @param Payload The payload variant to write.
 */
static void RestampTablePayload(QTextDocument* Document, int Group,
                                const QVariant& Payload)
{
    QTextCursor Cursor(Document);
    for (const QTextBlock& Block : TableBlocksOfGroup(Document, Group)) {
        QTextBlockFormat Format = Block.blockFormat();
        Format.setProperty(DocTablePayloadProperty, Payload);
        Cursor.setPosition(Block.position());
        Cursor.setBlockFormat(Format);
    }
}

/**
 * @brief Sets the alignment on every block of one cell
 *        (Bug 448).
 *
 * A cell's text alignment is a display property: the model
 * carries the align attribute in the cell property, and the
 * blocks show it. Only the first block carries the property,
 * so the cell is found from it.
 * @param Document The document.
 * @param FirstBlock The cell's first block.
 * @param Align The align value, empty for none (left).
 */
static void SetCellBlockAlignment(QTextDocument* Document,
                                  const QTextBlock& FirstBlock,
                                  const QString& Align)
{
    int Row = -1;
    int Column = -1;
    if (!TableCellOfBlock(Document, FirstBlock, Row, Column)) {
        return;
    }
    QTextTable* Frame = TableOfBlock(Document, FirstBlock);
    if (Frame == nullptr) {
        return;
    }
    QTextTableCell Cell = Frame->cellAt(Row, Column);
    if (!Cell.isValid()) {
        return;
    }
    Qt::Alignment Wanted = AlignmentForAlignValue(Align);
    QTextCursor Cursor(Document);
    QTextBlock Block = Cell.firstCursorPosition().block();
    QTextBlock Last = Cell.lastCursorPosition().block();
    while (Block.isValid()) {
        QTextBlockFormat Format = Block.blockFormat();
        if (Format.alignment() != Wanted) {
            Format.setAlignment(Wanted);
            Cursor.setPosition(Block.position());
            Cursor.setBlockFormat(Format);
        }
        if (Block == Last) {
            break;
        }
        Block = Block.next();
    }
}

void TextEditor::ApplyTableAttributes(
    int FirstBlockPos,
    const QVector<QPair<QString, QString>>& Attributes)
{
    QTextDocument* Document = _TextEdit->document();
    QTextBlock Block = Document->findBlock(FirstBlockPos);
    if (!IsTableBlock(Block)) {
        return;
    }
    int Group = Block.blockFormat()
                    .property(DocTableGroupProperty)
                    .toInt();
    QVariantMap Map;
    if (!TablePayloadMapOf(Block, Map)) {
        return;
    }
    Map[QStringLiteral("attrs")] = AttributesToVariant(Attributes);
    QTextCursor Cursor(Document);
    Cursor.beginEditBlock();
    RestampTablePayload(Document, Group, Map);
    UpdateOptionMarkers();
    Cursor.endEditBlock();
    _TextEdit->setFocus();
}

void TextEditor::ApplyTableSectionAttributes(
    int FirstBlockPos, const QString& Section,
    const QVector<QPair<QString, QString>>& Attributes)
{
    QTextDocument* Document = _TextEdit->document();
    QTextBlock Block = Document->findBlock(FirstBlockPos);
    if (!IsTableBlock(Block)) {
        return;
    }
    int Group = Block.blockFormat()
                    .property(DocTableGroupProperty)
                    .toInt();
    QVariantMap Map;
    if (!TablePayloadMapOf(Block, Map)) {
        return;
    }
    QString Key;
    if (Section == QLatin1String("thead")) {
        Key = QStringLiteral("headattrs");
    } else if (Section == QLatin1String("tfoot")) {
        Key = QStringLiteral("footattrs");
    } else {
        Key = QStringLiteral("bodyattrs");
    }
    Map[Key] = AttributesToVariant(Attributes);
    QTextCursor Cursor(Document);
    Cursor.beginEditBlock();
    RestampTablePayload(Document, Group, Map);
    UpdateOptionMarkers();
    Cursor.endEditBlock();
    _TextEdit->setFocus();
}

void TextEditor::ApplyTableRowAttributes(
    int FirstBlockPos,
    const QVector<QPair<QString, QString>>& Attributes)
{
    QTextDocument* Document = _TextEdit->document();
    QTextBlock Block = Document->findBlock(FirstBlockPos);
    if (!IsTableCellBlock(Block)) {
        return;
    }
    int Group = Block.blockFormat()
                    .property(DocTableGroupProperty)
                    .toInt();
    int Row = -1;
    int Column = -1;
    if (!TableCellOfBlock(Document, Block, Row, Column)) {
        return;
    }
    QVariantMap Map;
    if (!TablePayloadMapOf(Block, Map)) {
        return;
    }
    QVariantList Rows = Map.value(QStringLiteral("rows")).toList();
    if (Row < 0) {
        return;
    }
    // A row the grid gained since the last load has no mark
    // yet: extend the marks to it, in the section the grid
    // reads it as (the last mark's section), so the anchor has
    // a mark to land on.
    while (Rows.size() <= Row) {
        QVariantMap RowMap;
        QString Section = QStringLiteral("tbody");
        if (!Rows.isEmpty()) {
            QString LastSection = Rows.last()
                                      .toMap()
                                      .value(QStringLiteral("section"))
                                      .toString();
            if (LastSection == QLatin1String("thead") ||
                LastSection == QLatin1String("tfoot")) {
                Section = LastSection;
            }
        }
        RowMap[QStringLiteral("section")] = Section;
        RowMap[QStringLiteral("anchor")] = QString();
        RowMap[QStringLiteral("attrs")] = QVariantList();
        Rows.append(RowMap);
    }
    // The row's anchor rides in the mark alongside its other
    // attributes, so it is split back out here.
    QString Anchor = AttributeOf(Attributes, QStringLiteral("anchor"));
    QVector<QPair<QString, QString>> Kept;
    for (const QPair<QString, QString>& Attribute : Attributes) {
        if (Attribute.first != QLatin1String("anchor")) {
            Kept.append(Attribute);
        }
    }
    QVariantMap RowMap = Rows[Row].toMap();
    RowMap[QStringLiteral("anchor")] = Anchor;
    RowMap[QStringLiteral("attrs")] = AttributesToVariant(Kept);
    Rows[Row] = RowMap;
    Map[QStringLiteral("rows")] = Rows;
    QTextCursor Cursor(Document);
    Cursor.beginEditBlock();
    RestampTablePayload(Document, Group, Map);
    UpdateOptionMarkers();
    Cursor.endEditBlock();
    _TextEdit->setFocus();
}

void TextEditor::ApplyTableCellAttributes(
    int FirstBlockPos,
    const QVector<QPair<QString, QString>>& Attributes)
{
    QTextDocument* Document = _TextEdit->document();
    QTextBlock Block = Document->findBlock(FirstBlockPos);
    if (!IsTableCellBlock(Block)) {
        return;
    }
    xmlNodePtr CurrentNode = nullptr;
    if (!TableCellFromVariant(
            Block.blockFormat().property(DocTableCellProperty),
            CurrentNode)) {
        return;
    }
    // A fresh node takes exactly the new list: SetNodeAttributes
    // only sets, so building fresh is the replacement.
    xmlNodePtr CellNode = NewNode(
        DocDocument::NodeNameEquals(CurrentNode, "th") ? "th" : "td");
    xmlFreeNode(CurrentNode);
    SetNodeAttributes(CellNode, Attributes);
    QVariant CellValue = TableCellToVariant(CellNode);
    QString Align = AttributeOf(Attributes, QStringLiteral("align"));
    xmlFreeNode(CellNode);
    QTextCursor Cursor(Document);
    Cursor.beginEditBlock();
    QTextBlockFormat Format = Block.blockFormat();
    Format.setProperty(DocTableCellProperty, CellValue);
    Cursor.setPosition(Block.position());
    Cursor.setBlockFormat(Format);
    SetCellBlockAlignment(Document, Block, Align);
    UpdateOptionMarkers();
    Cursor.endEditBlock();
    _TextEdit->setFocus();
}

void TextEditor::SetTablePartAnchor(int FirstBlockPos,
                                    const QString& PartKind,
                                    const QString& Anchor)
{
    // Bug 448: the owner calls this after checking the old
    // anchor is not cited and updating the cross-references.
    // An empty anchor clears it.
    QTextDocument* Document = _TextEdit->document();
    QString Trimmed = Anchor.trimmed();
    if (PartKind == QLatin1String("table")) {
        QVector<QPair<QString, QString>> Current =
            TableAttributesAt(Document, FirstBlockPos);
        if (Trimmed.isEmpty()) {
            RemoveAttribute(Current, QStringLiteral("anchor"));
        } else {
            SetAttribute(Current, QStringLiteral("anchor"), Trimmed);
        }
        ApplyTableAttributes(FirstBlockPos, Current);
    } else if (PartKind == QLatin1String("thead") ||
               PartKind == QLatin1String("tbody") ||
               PartKind == QLatin1String("tfoot")) {
        QVector<QPair<QString, QString>> Current =
            TableSectionAttributesAt(Document, FirstBlockPos, PartKind);
        if (Trimmed.isEmpty()) {
            RemoveAttribute(Current, QStringLiteral("anchor"));
        } else {
            SetAttribute(Current, QStringLiteral("anchor"), Trimmed);
        }
        ApplyTableSectionAttributes(FirstBlockPos, PartKind, Current);
    } else if (PartKind == QLatin1String("tr")) {
        QVector<QPair<QString, QString>> Current =
            TableRowAttributesAt(Document, FirstBlockPos);
        if (Trimmed.isEmpty()) {
            RemoveAttribute(Current, QStringLiteral("anchor"));
        } else {
            SetAttribute(Current, QStringLiteral("anchor"), Trimmed);
        }
        ApplyTableRowAttributes(FirstBlockPos, Current);
    } else if (PartKind == QLatin1String("td") ||
               PartKind == QLatin1String("th")) {
        QVector<QPair<QString, QString>> Current =
            TableCellAttributesAt(Document, FirstBlockPos);
        if (Trimmed.isEmpty()) {
            RemoveAttribute(Current, QStringLiteral("anchor"));
        } else {
            SetAttribute(Current, QStringLiteral("anchor"), Trimmed);
        }
        ApplyTableCellAttributes(FirstBlockPos, Current);
    }
}

bool TextEditor::PromptForTag(const QString& Name, DocTagEntry& Entry,
                              const QString& Prefill)
{
    Entry.Name = Name;
    Entry.Attributes.clear();
    if (Name == QLatin1String("xref") || Name == QLatin1String("eref") ||
        Name == QLatin1String("relref")) {
        QString Value;
        while (true) {
            bool Ok = false;
            Value = QInputDialog::getText(this, LabelFor(Name), tr("Target:"),
                                          QLineEdit::Normal, QString(), &Ok);
            if (!Ok) {
                return false;
            }
            if (!Value.trimmed().isEmpty()) {
                break;
            }
        }
        Entry.Attributes.append(
            QPair<QString, QString>(QStringLiteral("target"), Value.trimmed()));
    } else if (Name == QLatin1String("iref")) {
        // Bug 503: the Item is prefilled with the highlighted
        // word (the word being indexed), and the prompts say
        // what they want. A sub-item is a narrower term filed
        // under the item, e.g. item "RTT", sub-item
        // "Round Trip Time".
        QString Item;
        while (true) {
            bool Ok = false;
            Item = QInputDialog::getText(
                this, LabelFor(Name),
                tr("Item (the word or phrase to index):"),
                QLineEdit::Normal, Prefill, &Ok);
            if (!Ok) {
                return false;
            }
            if (!Item.trimmed().isEmpty()) {
                break;
            }
        }
        Entry.Attributes.append(
            QPair<QString, QString>(QStringLiteral("item"), Item.trimmed()));
        bool Ok = false;
        QString Subitem = QInputDialog::getText(
            this, LabelFor(Name),
            tr("Sub-item (a narrower term under the item, optional):"),
            QLineEdit::Normal, QString(), &Ok);
        if (Ok && !Subitem.trimmed().isEmpty()) {
            Entry.Attributes.append(
                QPair<QString, QString>(QStringLiteral("subitem"), Subitem.trimmed()));
        }
    } else if (Name == QLatin1String("vspace")) {
        bool Ok = false;
        int Lines = QInputDialog::getInt(this, LabelFor(Name), tr("Blank lines:"),
                                         1, 0, 99, 1, &Ok);
        if (!Ok) {
            return false;
        }
        Entry.Attributes.append(
            QPair<QString, QString>(QStringLiteral("blankLines"),
                                    QString::number(Lines)));
    }
    return true;
}

void TextEditor::InsertTag(const QString& Name)
{
    QTextCursor Cursor = _TextEdit->textCursor();
    // Bug 503: capture the highlighted word before collapsing
    // the selection -- it prefills the index entry's Item.
    // A multi-line highlight becomes one line in the prompt.
    QString Highlight;
    if (Cursor.hasSelection()) {
        Highlight = Cursor.selectedText();
        Highlight.replace(QChar(0x2029), QLatin1Char(' '));
        Highlight.replace(QLatin1Char('\n'), QLatin1Char(' '));
        Highlight.replace(QLatin1Char('\r'), QLatin1Char(' '));
        Highlight = Highlight.trimmed();
    }
    Cursor.beginEditBlock();
    if (Cursor.hasSelection()) {
        Cursor.setPosition(Cursor.selectionStart());
    }
    if (Name == QLatin1String("list")) {
        // Split the paragraph at the cursor, put a new list
        // paragraph between the halves, and leave the cursor in
        // its first (empty) item. A new list shows bullets.
        Cursor.insertBlock();
        Cursor.insertBlock();
        Cursor.movePosition(QTextCursor::PreviousBlock);
        QVector<QPair<QString, QString>> Attrs;
        Attrs.append(QPair<QString, QString>(QStringLiteral("style"),
                                             QStringLiteral("symbols")));
        QTextBlockFormat BlockFormat = Cursor.blockFormat();
        BlockFormat.setProperty(DocListProperty, AttributesToVariant(Attrs));
        BlockFormat.setProperty(DocListKindProperty, QStringLiteral("list"));
        BlockFormat.setProperty(DocListGroupProperty,
                                NextListGroup(Cursor.document()));
        BlockFormat.setBottomMargin(0);
        Cursor.setBlockFormat(BlockFormat);
        Cursor.createList(ListFormatForStyle(QStringLiteral("symbols")));
        Cursor.endEditBlock();
        _TextEdit->setTextCursor(Cursor);
        return;
    }
    DocTagEntry Entry;
    if (!PromptForTag(Name, Entry, Highlight)) {
        Cursor.endEditBlock();
        return;
    }
    DocTagStack Stack = StackOf(Cursor.charFormat());
    Cursor.insertText(
        MarkerText(Entry,
                   DisplayNamesForDocument(_TextEdit->document())),
        FormatForMarker(Stack, Entry));
    Cursor.endEditBlock();
    _TextEdit->setTextCursor(Cursor);
}

void TextEditor::SetDocument(const DocDocument* Document)
{
    _Document = Document;
}

void TextEditor::SetBibCache(const BibCache* Cache)
{
    _BibCache = Cache;
}

void TextEditor::SelectedRange(int& Start, int& End) const
{
    QTextCursor Cursor = _TextEdit->textCursor();
    if (!Cursor.hasSelection()) {
        Start = -1;
        End = -1;
        return;
    }
    Start = Cursor.selectionStart();
    End = Cursor.selectionEnd();
}

QString TextEditor::SelectedText() const
{
    QTextCursor Cursor = _TextEdit->textCursor();
    if (!Cursor.hasSelection()) {
        return QString();
    }
    return Cursor.selectedText();
}

bool TextEditor::PickXrefForm(QWidget* Parent, const QString& Anchor,
                              const QString& Highlight, XrefForm& Form,
                              QString& Section)
{
    // The forms published RFCs use, with the anchor in each
    // label so the choice is concrete. The forms the spec
    // defines but no published RFC uses (title, none, comma,
    // parens, pageno, relative) are not offered.
    QStringList Choices;
    Choices.append(tr("Bracket: [%1]").arg(Anchor));
    if (Highlight.isEmpty()) {
        Choices.append(tr("Link text"));
    } else {
        Choices.append(tr("Link text: %1").arg(Highlight));
    }
    Choices.append(tr("Section of: Section <number> of [%1]").arg(Anchor));
    Choices.append(tr("Section bare: Section <number>"));
    Choices.append(tr("Counter: <number>"));
    bool Ok = false;
    QString Picked = QInputDialog::getItem(
        Parent, tr("Cross-reference form"),
        tr("How should this reference appear?"), Choices, 0, false, &Ok);
    if (!Ok) {
        return false;
    }
    if (Picked == Choices.at(0)) {
        Form = XrefForm::Bracket;
    } else if (Picked == Choices.at(1)) {
        Form = XrefForm::LinkText;
    } else if (Picked == Choices.at(2)) {
        Form = XrefForm::SectionOf;
    } else if (Picked == Choices.at(3)) {
        Form = XrefForm::SectionBare;
    } else {
        Form = XrefForm::Counter;
    }
    Section.clear();
    if (Form == XrefForm::SectionOf || Form == XrefForm::SectionBare) {
        bool SectionOk = false;
        QString Value = QInputDialog::getText(
            Parent, tr("Cross-reference section"),
            tr("Section number:"), QLineEdit::Normal, QString(),
            &SectionOk);
        if (!SectionOk || Value.trimmed().isEmpty()) {
            return false;
        }
        Section = Value.trimmed();
    }
    return true;
}

/**
 * @brief Puts the caret just after a link, with the format the
 *        text would have without the link.
 *
 * Wrapping or inserting a link leaves the caret holding the
 * link's format, so the next typed text joins the link. The
 * caret goes just after the link with the link tag stripped
 * from its format, so typing after the link is plain text.
 * @param Edit The text edit (borrowed).
 * @param End The document position just after the link.
 * @param Tag The link tag to strip (xref, eref, relref, cref).
 */
static void PlaceCaretAfterTag(QTextEdit* Edit, int End, const QString& Tag)
{
    if (Edit == nullptr) {
        return;
    }
    QTextCursor Cursor = Edit->textCursor();
    Cursor.setPosition(End);
    QTextCharFormat Plain = Cursor.charFormat();
    DocTagStack Stack = StackOf(Plain);
    if (StackHolds(Stack, Tag)) {
        Stack = StackWithoutTag(Stack, Tag);
        Plain.setProperty(DocStackProperty, StackToVariant(Stack));
        ApplyStackVisuals(Plain, Stack);
        QVariant MarkerValue = Plain.property(DocMarkerProperty);
        if (MarkerValue.isValid()) {
            ApplyMarkerVisuals(Plain, EntryFromVariant(MarkerValue));
        }
    }
    Cursor.setCharFormat(Plain);
    Edit->setTextCursor(Cursor);
}

void TextEditor::InsertXref(const QString& Anchor, const QString& Section,
                            XrefForm Form, int SelectionStart,
                            int SelectionEnd)
{
    DocTagEntry Entry;
    Entry.Name = QStringLiteral("xref");
    Entry.Attributes.append(
        QPair<QString, QString>(QStringLiteral("target"), Anchor));
    if (Form == XrefForm::SectionOf) {
        Entry.Attributes.append(
            QPair<QString, QString>(QStringLiteral("section"), Section));
        Entry.Attributes.append(
            QPair<QString, QString>(QStringLiteral("sectionFormat"),
                                    QStringLiteral("of")));
    } else if (Form == XrefForm::SectionBare) {
        Entry.Attributes.append(
            QPair<QString, QString>(QStringLiteral("section"), Section));
        Entry.Attributes.append(
            QPair<QString, QString>(QStringLiteral("sectionFormat"),
                                    QStringLiteral("bare")));
    } else if (Form == XrefForm::Counter) {
        Entry.Attributes.append(
            QPair<QString, QString>(QStringLiteral("format"),
                                    QStringLiteral("counter")));
    }
    QTextDocument* Document = _TextEdit->document();
    int End = -1;
    if (Form == XrefForm::LinkText && SelectionStart >= 0 &&
        SelectionEnd > SelectionStart) {
        // The highlighted text stays as the cross-reference's
        // content, the same way the tag menu wraps a selection in
        // a Cross-Reference, External Link, or Relative Reference.
        // A fragment that already holds a cross-reference is left
        // alone: remove the old link first, then cite the new one.
        ChangeStacks(SelectionStart, SelectionEnd,
                     QStringLiteral("xref"), &Entry);
        End = SelectionEnd;
    } else {
        // An empty xref (no content): the words stay plain and
        // the reference follows them as a bare marker, which
        // renders as the bracketed anchor (or its display name,
        // when a displayreference maps it), the section, or the
        // counter, depending on the form. With a selection the
        // marker goes just after it, in the highlight's own
        // stack, so it sits beside the plain words rather than
        // inside whatever fragment follows them. With no
        // selection it drops at the cursor, as the tag menu
        // does. The References menu only offers itself with a
        // selection, so the no-selection case is a fallback only.
        QTextCursor Cursor = _TextEdit->textCursor();
        DocTagStack Stack;
        if (SelectionStart >= 0 && SelectionEnd > SelectionStart) {
            Stack = StackAtPosition(Document, SelectionEnd - 1);
            Cursor.setPosition(SelectionEnd);
        } else {
            Stack = StackOf(Cursor.charFormat());
        }
        Cursor.beginEditBlock();
        Cursor.insertText(
            MarkerText(Entry, DisplayNamesForDocument(Document)),
            FormatForMarker(Stack, Entry));
        Cursor.endEditBlock();
        End = Cursor.position();
    }
    // The caret lands just after the reference, with the format
    // the text would have without the link: typing after the
    // reference is plain text, not more of the link.
    PlaceCaretAfterTag(_TextEdit, End, QStringLiteral("xref"));
}

void TextEditor::InsertXrefMarker(const QString& Anchor, int SelectionStart,
                                  int SelectionEnd)
{
    InsertXref(Anchor, QString(), XrefForm::LinkText, SelectionStart,
               SelectionEnd);
}

void TextEditor::InsertXrefWithText(const QString& Anchor,
                                    const QString& Text,
                                    const QString& Section, XrefForm Form)
{
    QTextCursor Cursor = _TextEdit->textCursor();
    if (Cursor.hasSelection()) {
        // A selection is cited by wrapping it, as InsertXrefMarker does.
        InsertXref(Anchor, Section, Form, Cursor.selectionStart(),
                   Cursor.selectionEnd());
        return;
    }
    if (Form != XrefForm::LinkText) {
        // An empty xref has no content to show: drop a bare
        // marker at the cursor instead, and ignore the text.
        InsertXref(Anchor, Section, Form, -1, -1);
        return;
    }
    if (Text.isEmpty()) {
        // Nothing to show: fall back to the bare marker.
        InsertXref(Anchor, Section, Form, -1, -1);
        return;
    }
    DocTagEntry Entry;
    Entry.Name = QStringLiteral("xref");
    Entry.Attributes.append(
        QPair<QString, QString>(QStringLiteral("target"), Anchor));
    // The caret may sit inside a link: the inserted text would
    // join it, and the wrap below would leave it there, since a
    // fragment that already holds a cross-reference is left
    // alone. Step just past any link first, so the new words
    // are plain text before they are wrapped.
    PlaceCaretAfterTag(_TextEdit, Cursor.position(),
                       QStringLiteral("xref"));
    Cursor = _TextEdit->textCursor();
    int Start = Cursor.position();
    Cursor.beginEditBlock();
    Cursor.insertText(Text);
    Cursor.endEditBlock();
    int End = Start + Text.length();
    ChangeStacks(Start, End, QStringLiteral("xref"), &Entry);
    PlaceCaretAfterTag(_TextEdit, End, QStringLiteral("xref"));
}

/**
 * @brief Splits the cursor's line out of a multi-line block.
 *
 * A paragraph holds its lines separated by soft breaks, so one
 * block can carry several lines: Enter adds a soft break, and
 * only Enter on an empty line starts a new paragraph. A list
 * marks whole blocks, so without a split the bullet would land
 * on the block's first line no matter which line the cursor is
 * on (Bug 469). The block is split at the soft breaks bounding
 * the cursor's line -- the bounding soft break is removed and
 * a block break takes its place, as Enter does on an empty
 * line -- and the cursor ends in that line's own block. A
 * single-line block is left alone.
 * @param Cursor The cursor, on the line to split out.
 */
static void SplitBlockAtCursorLine(QTextCursor& Cursor)
{
    QTextBlock Block = Cursor.block();
    QString Text = Block.text();
    int Offset = Cursor.position() - Block.position();
    int LineStart = 0;
    if (Offset > 0) {
        LineStart = Text.lastIndexOf(SoftBreakChar, Offset - 1) + 1;
    }
    int LineEnd = Text.indexOf(SoftBreakChar, Offset);
    if (LineEnd < 0) {
        LineEnd = Text.size();
    }
    if (LineStart == 0 && LineEnd == Text.size()) {
        return;
    }
    int BlockPos = Block.position();
    bool SplitEnd = LineEnd < Text.size();
    bool SplitStart = LineStart > 0;
    // Split after the line first: the line stays in the original
    // block, so the start split's position stays valid.
    if (SplitEnd) {
        Cursor.setPosition(BlockPos + LineEnd);
        Cursor.setPosition(BlockPos + LineEnd + 1,
                           QTextCursor::KeepAnchor);
        Cursor.removeSelectedText();
        Cursor.insertBlock();
    }
    // Split before the line: the line becomes its own block, and
    // the cursor lands at its start.
    if (SplitStart) {
        Cursor.setPosition(BlockPos + LineStart - 1);
        Cursor.setPosition(BlockPos + LineStart,
                           QTextCursor::KeepAnchor);
        Cursor.removeSelectedText();
        Cursor.insertBlock();
    } else if (SplitEnd) {
        // The line was first: the end split left the cursor in
        // the following block, so step back into the line.
        Cursor.movePosition(QTextCursor::PreviousBlock);
    }
}

void TextEditor::InsertBlockList(const QString& Kind)
{
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextDocument* Document = _TextEdit->document();
    Cursor.beginEditBlock();
    int Group = NextListGroup(Document);
    QVector<QPair<QString, QString>> NoAttributes;
    QString HeadRole = Kind == QLatin1String("dl") ? QStringLiteral("term")
                                                   : QStringLiteral("item");
    if (Cursor.hasSelection()) {
        int Start = Cursor.selectionStart();
        int End = Cursor.selectionEnd();
        QVector<QTextBlock> Blocks;
        for (QTextBlock Block = Document->findBlock(Start);
             Block.isValid() && Block.position() < End;
             Block = Block.next()) {
            Blocks.append(Block);
        }
        if (Blocks.isEmpty()) {
            Cursor.endEditBlock();
            return;
        }
        if (Kind == QLatin1String("dl")) {
            // Blocks next to a definition list join that list,
            // so marking pasted entries makes them part of the
            // same <dl> instead of a second one (Bug 478). The
            // list above wins; the list below is the fallback
            // when there is none above.
            QTextBlock Before = Blocks.first().previous();
            QTextBlock After = Blocks.last().next();
            if (Before.isValid() &&
                Before.blockFormat().property(DocListKindProperty)
                        .toString() == QLatin1String("dl")) {
                Group = Before.blockFormat()
                            .property(DocListGroupProperty).toInt();
            } else if (After.isValid() &&
                       After.blockFormat().property(DocListKindProperty)
                               .toString() == QLatin1String("dl")) {
                Group = After.blockFormat()
                            .property(DocListGroupProperty).toInt();
            }
        }
        // Take the blocks out of any list they already belong
        // to, so marking them with the new kind cannot leave a
        // block in two lists at once.
        for (const QTextBlock& Block : Blocks) {
            DetachBlockFromList(Document, Block);
        }
        if (Kind == QLatin1String("dl")) {
            // Selected blocks pair up as term and definition:
            // the first is a term, the second its definition,
            // the third the next term, and so on. A lone trailing
            // term gets a new empty definition after it, as a
            // single block does. (Before, every selected block
            // became a term with a new empty definition after
            // it, so a copied term and its definition came out
            // as two terms with two empty definitions — Bug 478.)
            for (int i = 0; i < Blocks.size(); ++i) {
                QTextCursor At(Document);
                At.setPosition(Blocks[i].position());
                QString Role = (i % 2 == 0) ? QStringLiteral("term")
                                            : QStringLiteral("definition");
                SetListProperties(At, Kind, Group, NoAttributes, Role);
            }
            if (Blocks.size() % 2 == 1) {
                QTextCursor At(Document);
                At.setPosition(Blocks.last().position());
                At.movePosition(QTextCursor::EndOfBlock);
                At.insertBlock();
                SetListProperties(At, Kind, Group, NoAttributes,
                                  QStringLiteral("definition"));
                At.setCharFormat(QTextCharFormat());
            }
            StyleDefinitionGroup(Document, Blocks.first());
        } else {
            for (const QTextBlock& Block : Blocks) {
                QTextCursor At(Document);
                At.setPosition(Block.position());
                SetListProperties(At, Kind, Group, NoAttributes, HeadRole);
            }
            QTextCursor Selection(Document);
            Selection.setPosition(Blocks.first().position());
            Selection.setPosition(Blocks.last().position(),
                                  QTextCursor::KeepAnchor);
            Selection.movePosition(QTextCursor::EndOfBlock,
                                   QTextCursor::KeepAnchor);
            Selection.createList(ListFormatForKind(Kind, NoAttributes));
            // A block converted from a definition list keeps its
            // bold term overlay until its visuals are rebuilt
            // from its tag stacks.
            for (const QTextBlock& Block : Blocks) {
                RefreshBlockVisuals(Document, Block);
            }
        }
        Cursor.endEditBlock();
        UpdateBlockButtons();
        return;
    }
    // A line of a multi-line block becomes its own block first,
    // or the bullet would land on the block's first line no
    // matter which line the cursor is on (Bug 469).
    SplitBlockAtCursorLine(Cursor);
    // A line next to a list of the same kind joins that list, so
    // bulleting lines one at a time makes one list instead of
    // one list per line. The list above wins; the list below is
    // the fallback when there is none above. A list carrying
    // attributes is never joined: the new item carries none,
    // and different attributes would split the list on save.
    QTextList* JoinList = nullptr;
    int JoinGroup = 0;
    auto SameKindList = [&Kind](const QTextBlock& Block) {
        if (!Block.isValid()) {
            return false;
        }
        if (Block.blockFormat().property(DocListKindProperty)
                    .toString() != Kind) {
            return false;
        }
        if (!AttributesFromVariant(
                Block.blockFormat().property(DocListProperty))
                .isEmpty()) {
            return false;
        }
        // A bulleted or numbered list joins through its list
        // object; a definition list has none, so its kind
        // alone claims the block (Bug 478: a line next to a
        // definition list must join it, not start a second one).
        return Kind == QLatin1String("dl") ||
               Block.textList() != nullptr;
    };
    QTextBlock Previous = Cursor.block().previous();
    QTextBlock Next = Cursor.block().next();
    if (SameKindList(Previous)) {
        JoinList = Previous.textList();
        JoinGroup = Previous.blockFormat()
                        .property(DocListGroupProperty).toInt();
    } else if (SameKindList(Next)) {
        JoinList = Next.textList();
        JoinGroup =
            Next.blockFormat().property(DocListGroupProperty).toInt();
    }
    DetachBlockFromList(Document, Cursor.block());
    if (Kind != QLatin1String("dl") && JoinList != nullptr) {
        SetListProperties(Cursor, Kind, JoinGroup, NoAttributes, HeadRole);
        RebuildList(Document, JoinList, Cursor.block());
        RefreshBlockVisuals(Document, Cursor.block());
        Cursor.endEditBlock();
        _TextEdit->setTextCursor(Cursor);
        UpdateBlockButtons();
        return;
    }
    // A definition list next to the cursor joins it: the term
    // and its new definition take the joined group, not a fresh
    // one (Bug 478). A group of 0 means no list was found.
    int UseGroup = (Kind == QLatin1String("dl") && JoinGroup != 0)
                       ? JoinGroup : Group;
    SetListProperties(Cursor, Kind, UseGroup, NoAttributes, HeadRole);
    if (Kind == QLatin1String("dl")) {
        Cursor.movePosition(QTextCursor::EndOfBlock);
        Cursor.insertBlock();
        SetListProperties(Cursor, Kind, UseGroup, NoAttributes,
                          QStringLiteral("definition"));
        Cursor.setCharFormat(QTextCharFormat());
        StyleDefinitionGroup(Document, Cursor.block());
        // The cursor stays in the term, so the term is typed
        // first; Enter moves on to the definition.
        Cursor.setPosition(Cursor.block().previous().position());
        Cursor.movePosition(QTextCursor::EndOfBlock);
        QTextCharFormat Typing = Cursor.charFormat();
        ApplyStackVisuals(Typing, StackOf(Typing));
        Typing.setFontWeight(QFont::Bold);
        Cursor.setCharFormat(Typing);
    } else {
        Cursor.createList(ListFormatForKind(Kind, NoAttributes));
        RefreshBlockVisuals(Document, Cursor.block());
    }
    Cursor.endEditBlock();
    _TextEdit->setTextCursor(Cursor);
    UpdateBlockButtons();
}

void TextEditor::RemoveBlockList(const QString& Kind)
{
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextDocument* Document = _TextEdit->document();
    Cursor.beginEditBlock();
    QVector<QTextBlock> Touched;
    if (Cursor.hasSelection()) {
        int Start = Cursor.selectionStart();
        int End = Cursor.selectionEnd();
        for (QTextBlock Block = Document->findBlock(Start);
             Block.isValid() && Block.position() < End;
             Block = Block.next()) {
            Touched.append(Block);
        }
    } else {
        Touched.append(Cursor.block());
    }
    auto ClearBlock = [](QTextCursor& At) {
        QTextBlockFormat Format = At.blockFormat();
        Format.setIndent(0);
        Format.clearProperty(QTextFormat::BackgroundBrush);
        Format.clearProperty(DocListProperty);
        Format.clearProperty(DocListKindProperty);
        Format.clearProperty(DocListGroupProperty);
        Format.clearProperty(DocListRoleProperty);
        Format.setBottomMargin(ParagraphGap(At.document()));
        At.setBlockFormat(Format);
    };
    QSet<QTextList*> DoneLists;
    QSet<int> DoneGroups;
    for (const QTextBlock& TouchedBlock : Touched) {
        QString BlockKind = TouchedBlock.blockFormat()
                                .property(DocListKindProperty).toString();
        if (BlockKind.isEmpty()) {
            BlockKind = QStringLiteral("list");
        }
        if (BlockKind != Kind) {
            continue;
        }
        if (QTextList* List = TouchedBlock.textList()) {
            if (DoneLists.contains(List)) {
                continue;
            }
            DoneLists.insert(List);
            // The run holds the list's items plus any
            // continuation blocks between them, which sit
            // outside the list object.
            QTextBlock First = List->item(0);
            QTextBlock Last = List->item(List->count() - 1);
            int Group = First.blockFormat()
                            .property(DocListGroupProperty).toInt();
            QVector<QTextBlock> Run;
            for (QTextBlock Block = First;
                 Block.isValid() && Block.position() <= Last.position();
                 Block = Block.next()) {
                QTextBlockFormat Format = Block.blockFormat();
                if (Format.property(DocListKindProperty).toString() != Kind ||
                    Format.property(DocListGroupProperty).toInt() != Group) {
                    continue;
                }
                Run.append(Block);
            }
            for (const QTextBlock& Item : Run) {
                if (Item.textList() == List) {
                    List->remove(Item);
                }
                QTextCursor At(Document);
                At.setPosition(Item.position());
                ClearBlock(At);
                if (Kind == QLatin1String("dl")) {
                    RefreshBlockVisuals(Document, Item);
                }
            }
        } else if (Kind == QLatin1String("dl")) {
            int Group = TouchedBlock.blockFormat()
                            .property(DocListGroupProperty).toInt();
            if (DoneGroups.contains(Group)) {
                continue;
            }
            DoneGroups.insert(Group);
            QTextBlock First = TouchedBlock;
            while (First.previous().isValid() &&
                   First.previous().blockFormat()
                           .property(DocListKindProperty).toString() ==
                       QLatin1String("dl") &&
                   First.previous().blockFormat()
                           .property(DocListGroupProperty).toInt() == Group) {
                First = First.previous();
            }
            QVector<QTextBlock> Run;
            for (QTextBlock Block = First;
                 Block.isValid() &&
                 Block.blockFormat().property(DocListKindProperty).toString() ==
                     QLatin1String("dl") &&
                 Block.blockFormat().property(DocListGroupProperty).toInt() ==
                     Group;
                 Block = Block.next()) {
                Run.append(Block);
            }
            for (const QTextBlock& Item : Run) {
                QTextCursor At(Document);
                At.setPosition(Item.position());
                ClearBlock(At);
                RefreshBlockVisuals(Document, Item);
            }
        }
    }
    Cursor.endEditBlock();
    UpdateBlockButtons();
}

void TextEditor::ToggleBlockList(const QString& Kind)
{
    QTextDocument* Document = _TextEdit->document();
    QTextCursor Cursor = _TextEdit->textCursor();
    if (TouchesSourceCode(Document, Cursor) ||
        TouchesFigure(Document, Cursor)) {
        return;
    }
    QSet<QString> Touched = ListKindsTouched(Document, Cursor);
    if (Touched.contains(Kind)) {
        RemoveBlockList(Kind);
    } else {
        InsertBlockList(Kind);
    }
    UpdateBlockButtons();
    // The button keeps keyboard focus after a click; hand it
    // back to the text so typing continues there.
    _TextEdit->setFocus();
}

void TextEditor::OnCaretMoved()
{
    QTextCursor Cursor = _TextEdit->textCursor();
    if (Cursor.hasSelection()) {
        return;
    }
    int Pos = Cursor.position();
    if (Pos <= 0) {
        return;
    }
    QTextDocument* Document = _TextEdit->document();
    DocTagStack Before = StackAtPosition(Document, Pos - 1);
    // The link tag on the character before the caret, if any.
    QString Link;
    for (const QString& Name :
         {QStringLiteral("xref"), QStringLiteral("eref"),
          QStringLiteral("relref"), QStringLiteral("cref")}) {
        if (StackHolds(Before, Name)) {
            Link = Name;
            break;
        }
    }
    if (Link.isEmpty()) {
        return;
    }
    // Inside one link the character after the caret holds the
    // same link, and typing there extends it, as it should.
    // Just after the link it does not, so typing there is plain
    // text: strip the link from the caret's format.
    DocTagStack After = StackAtPosition(Document, Pos);
    if (SameLink(Before, After, Link)) {
        return;
    }
    QTextCharFormat Plain = Cursor.charFormat();
    DocTagStack Stack = StackOf(Plain);
    if (!StackHolds(Stack, Link)) {
        return;
    }
    Stack = StackWithoutTag(Stack, Link);
    Plain.setProperty(DocStackProperty, StackToVariant(Stack));
    ApplyStackVisuals(Plain, Stack);
    QVariant MarkerValue = Plain.property(DocMarkerProperty);
    if (MarkerValue.isValid()) {
        ApplyMarkerVisuals(Plain, EntryFromVariant(MarkerValue));
    }
    _TextEdit->setCurrentCharFormat(Plain);
}

void TextEditor::UpdateBlockButtons()
{
    QTextCursor Cursor = _TextEdit->textCursor();
    QSet<QString> Touched = ListKindsTouched(_TextEdit->document(), Cursor);
    bool InSource = TouchesSourceCode(_TextEdit->document(), Cursor);
    bool InFigure = TouchesFigure(_TextEdit->document(), Cursor);
    bool InTable = TouchesTable(_TextEdit->document(), Cursor);
    // The markers checkbox follows the shared setting, so a
    // toggle in another pane shows here too. Its own signal is
    // blocked while it syncs, so syncing never re-sweeps.
    {
        QSignalBlocker Blocker(_ShowMarkersCheck);
        _ShowMarkersCheck->setChecked(OptionMarkersShown());
    }
    // A list button works where nothing is a list, a sourcecode
    // block, a figure, or a table, or where the cursor touches
    // only its own kind (clicking again removes that list). It
    // is greyed out where the cursor touches a different kind,
    // the same rule the List submenu follows, so lists cannot
    // nest and a block cannot land in two lists or in a list
    // and code, a figure, or a table.
    _BulletedButton->setEnabled(
        !InSource && !InFigure && !InTable && (Touched.isEmpty() ||
                      (Touched.size() == 1 &&
                       Touched.contains(QStringLiteral("ul")))));
    _NumberedButton->setEnabled(
        !InSource && !InFigure && !InTable && (Touched.isEmpty() ||
                      (Touched.size() == 1 &&
                       Touched.contains(QStringLiteral("ol")))));
    _DefinitionButton->setEnabled(
        !InSource && !InFigure && !InTable && (Touched.isEmpty() ||
                      (Touched.size() == 1 &&
                       Touched.contains(QStringLiteral("dl")))));
    _BulletedButton->setChecked(Touched.contains(QStringLiteral("ul")));
    _NumberedButton->setChecked(Touched.contains(QStringLiteral("ol")));
    _DefinitionButton->setChecked(Touched.contains(QStringLiteral("dl")));
    // The Blocks button works wherever the cursor touches no
    // list and no table: in prose it starts the picked kind of
    // block, and in a block of that kind it stops it. Its label
    // names the kind the cursor is in, or the picked kind. A
    // table has no stop — removal lives in the right-click
    // menu — so the button is greyed out in one, showing Table.
    _BlocksButton->setEnabled(Touched.isEmpty() && !InTable);
    _BlocksButton->setChecked(InSource || InFigure || InTable);
    QString Kind;
    if (IsTableBlock(Cursor.block())) {
        Kind = QStringLiteral("table");
    } else if (IsFigureBlock(Cursor.block())) {
        Kind = FigureKindOf(Cursor.block());
    } else if (InSource) {
        Kind = QStringLiteral("sourcecode");
    } else {
        Kind = _BlockKind;
    }
    if (Kind == QLatin1String("table")) {
        _BlocksButton->setText(tr("Table"));
    } else if (Kind == QLatin1String("figure")) {
        _BlocksButton->setText(tr("Figure"));
    } else if (Kind == QLatin1String("artwork") ||
               Kind == QLatin1String("artset")) {
        _BlocksButton->setText(tr("ASCII art"));
    } else if (Kind == QLatin1String("sourcecode")) {
        _BlocksButton->setText(tr("Source code"));
    } else {
        _BlocksButton->setText(tr("Blocks"));
    }
    // The version picker lists the figure's versions when the
    // cursor sits in a figure holding several of them, and is
    // hidden everywhere else. The signal is blocked while
    // it is rebuilt, so the rebuild cannot switch versions.
    QSignalBlocker PickerBlocker(_VersionPicker);
    _VersionPicker->clear();
    xmlNodePtr FigureNode = nullptr;
    int Visible = 0;
    if (IsFigureBlock(Cursor.block()) &&
        FigurePayloadOf(Cursor.block(), FigureNode, Visible)) {
        QVector<xmlNodePtr> Versions = FigureVersionNodes(FigureNode);
        if (Versions.size() > 1) {
            for (int i = 0; i < Versions.size(); ++i) {
                _VersionPicker->addItem(FigureVersionLabel(Versions[i]), i);
            }
            _VersionPicker->setCurrentIndex(Visible);
            _VersionPicker->setEnabled(true);
            _VersionPicker->setVisible(true);
        } else {
            _VersionPicker->setEnabled(false);
            _VersionPicker->setVisible(false);
        }
    } else {
        _VersionPicker->setEnabled(false);
        _VersionPicker->setVisible(false);
    }
    xmlFreeNode(FigureNode);
}

void TextEditor::InsertSourceCode()
{
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextDocument* Document = _TextEdit->document();
    Cursor.beginEditBlock();
    QVector<QTextBlock> Touched;
    if (Cursor.hasSelection()) {
        int Start = Cursor.selectionStart();
        int End = Cursor.selectionEnd();
        for (QTextBlock Block = Document->findBlock(Start);
             Block.isValid() && Block.position() < End;
             Block = Block.next()) {
            Touched.append(Block);
        }
    } else {
        Touched.append(Cursor.block());
    }
    QVector<QPair<QString, QString>> Attributes;
    for (const QTextBlock& Block : Touched) {
        DetachBlockFromList(Document, Block);
        // Code holds literal text only, so the block's text is
        // rewritten without tags or markers. The block is
        // marked first: the rewritten text may hold line breaks,
        // and the blocks they split off inherit the marking.
        QString Text = CodeTextOf(Block);
        QTextCursor At(Document);
        At.setPosition(Block.position());
        SetSourceProperties(At, Attributes);
        At.setPosition(Block.position());
        At.setPosition(Block.position() + Block.length() - 1,
                       QTextCursor::KeepAnchor);
        At.setCharFormat(CodeCharFormat());
        At.insertText(Text);
    }
    UpdateOptionMarkers();
    Cursor.endEditBlock();
    QTextCursor At = _TextEdit->textCursor();
    At.setCharFormat(CodeCharFormat());
    _TextEdit->setTextCursor(At);
    UpdateBlockButtons();
}

void TextEditor::RemoveSourceCode()
{
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextDocument* Document = _TextEdit->document();
    Cursor.beginEditBlock();
    QVector<QTextBlock> Touched;
    if (Cursor.hasSelection()) {
        int Start = Cursor.selectionStart();
        int End = Cursor.selectionEnd();
        for (QTextBlock Block = Document->findBlock(Start);
             Block.isValid() && Block.position() < End;
             Block = Block.next()) {
            Touched.append(Block);
        }
    } else {
        Touched.append(Cursor.block());
    }
    QSet<int> DoneStarts;
    for (const QTextBlock& TouchedBlock : Touched) {
        if (!IsSourceCodeBlock(TouchedBlock)) {
            continue;
        }
        QVariant Value =
            TouchedBlock.blockFormat().property(DocSourceProperty);
        QTextBlock First = TouchedBlock;
        while (First.previous().isValid() &&
               IsSourceCodeBlock(First.previous()) &&
               First.previous().blockFormat().property(
                   DocSourceProperty) == Value) {
            First = First.previous();
        }
        if (DoneStarts.contains(First.position())) {
            continue;
        }
        DoneStarts.insert(First.position());
        for (QTextBlock Block = First;
             Block.isValid() && IsSourceCodeBlock(Block) &&
             Block.blockFormat().property(DocSourceProperty) == Value;
             Block = Block.next()) {
            QTextCursor At(Document);
            At.setPosition(Block.position());
            QTextBlockFormat Format = At.blockFormat();
            Format.setIndent(0);
            Format.clearProperty(QTextFormat::BackgroundBrush);
            Format.clearProperty(DocSourceProperty);
            Format.clearProperty(DocSourceKindProperty);
            Format.setBottomMargin(ParagraphGap(Document));
            At.setBlockFormat(Format);
            RefreshBlockVisuals(Document, Block);
        }
    }
    UpdateOptionMarkers();
    Cursor.endEditBlock();
    // The edit cursor keeps the code typing format after the
    // block is un-coded; reset it to plain, or the next typed
    // text stays fixed-width. The cursor is collapsed to its
    // own position first, so the reset cannot reformat a
    // selection.
    QTextCursor At = _TextEdit->textCursor();
    At.setPosition(At.position());
    At.setCharFormat(QTextCharFormat());
    _TextEdit->setTextCursor(At);
    UpdateBlockButtons();
}

void TextEditor::EnsureTrailingPlainParagraph()
{
    QTextDocument* Document = _TextEdit->document();
    QTextBlock Last = Document->lastBlock();
    if (!Last.isValid() || !IsSourceCodeBlock(Last)) {
        return;
    }
    QTextCursor Cursor(Document);
    Cursor.setPosition(Last.position());
    InsertPlainParagraphAfter(Cursor);
}

void TextEditor::SetSpellChecker(SpellChecker* Checker)
{
    _SpellChecker = Checker;
    if (_SpellHighlighter != nullptr) {
        _SpellHighlighter->SetChecker(Checker);
        return;
    }
    if (Checker == nullptr) {
        return;
    }
    // The highlighter skips sourcecode and artwork blocks: they
    // hold literal text, not prose. The test is a lambda so the
    // block-kind properties stay private to this file.
    _SpellHighlighter = new SpellHighlighter(
        _TextEdit->document(), Checker,
        [](const QTextBlock& Block) {
            return IsSourceCodeBlock(Block) || IsFigureArtworkBlock(Block);
        });
}

void TextEditor::RefreshSpelling()
{
    if (_SpellHighlighter != nullptr) {
        _SpellHighlighter->rehighlight();
    }
}

void TextEditor::RemoveEmptySourceBlocks(bool HadContent)
{
    if (!HadContent) {
        // The element never held content before this deletion:
        // a fresh empty block from the Blocks button, or an
        // empty block that was already there. Leave it alone.
        return;
    }
    QTextDocument* Document = _TextEdit->document();
    QTextCursor EditCursor = _TextEdit->textCursor();
    if (!IsSourceCodeBlock(EditCursor.block())) {
        return;
    }
    QTextBlock First = FirstBlockOfSourceElement(EditCursor.block());
    if (SourceElementHasContent(Document, First.position())) {
        return;
    }
    QVector<QPair<QString, QString>> Attributes = AttributesFromVariant(
        First.blockFormat().property(DocSourceProperty));
    if (!AttributeOf(Attributes, QStringLiteral("src")).isEmpty()) {
        // A src-only block is a real block: it names a file and
        // shows its placeholder, so it stays.
        return;
    }
    // Remove the element's blocks in one selection. The sweep's
    // own edits must not re-enter the change handler, so the
    // signals are blocked and the views are refreshed by hand
    // below, in the same order the change handler would.
    QSignalBlocker Blocker(_TextEdit);
    int CursorPos = EditCursor.position();
    int CursorAnchor = EditCursor.anchor();
    QTextBlock Last = First;
    while (Last.next().isValid() && IsSourceCodeBlock(Last.next()) &&
           Last.next().blockFormat().property(DocSourceProperty) ==
               First.blockFormat().property(DocSourceProperty)) {
        Last = Last.next();
    }
    QTextBlock After = Last.next();
    QTextCursor Sweep(Document);
    Sweep.beginEditBlock();
    if (After.isValid()) {
        int FirstPos = First.position();
        int AfterPos = After.position();
        Sweep.setPosition(FirstPos);
        Sweep.setPosition(AfterPos, QTextCursor::KeepAnchor);
        Sweep.removeSelectedText();
        if (CursorPos > AfterPos) {
            CursorPos -= (AfterPos - FirstPos);
        } else if (CursorPos > FirstPos) {
            CursorPos = FirstPos;
        }
        if (CursorAnchor > AfterPos) {
            CursorAnchor -= (AfterPos - FirstPos);
        } else if (CursorAnchor > FirstPos) {
            CursorAnchor = FirstPos;
        }
    } else if (First.previous().isValid()) {
        // The element is last: take the paragraph break before
        // it, so the previous paragraph absorbs the empty lines.
        QTextBlock Previous = First.previous();
        int Join = Previous.position() + Previous.length() - 1;
        int End = Last.position() + Last.length() - 1;
        Sweep.setPosition(Join);
        Sweep.setPosition(End, QTextCursor::KeepAnchor);
        Sweep.removeSelectedText();
        if (CursorPos > End) {
            CursorPos -= (End - Join);
        } else if (CursorPos > Join) {
            CursorPos = Join;
        }
        if (CursorAnchor > End) {
            CursorAnchor -= (End - Join);
        } else if (CursorAnchor > Join) {
            CursorAnchor = Join;
        }
    } else {
        // The element is the whole document: the document always
        // keeps one block, so the block stays but loses its
        // source marking and reads back as a plain paragraph.
        QTextBlockFormat Format = First.blockFormat();
        Format.clearProperty(DocSourceProperty);
        Format.clearProperty(DocSourceKindProperty);
        Format.setBottomMargin(ParagraphGap(Document));
        Sweep.setPosition(First.position());
        Sweep.setBlockFormat(Format);
    }
    Sweep.endEditBlock();
    int LastPos = Document->characterCount() - 1;
    if (CursorPos < 0) {
        CursorPos = 0;
    }
    if (CursorPos > LastPos) {
        CursorPos = LastPos;
    }
    if (CursorAnchor < 0) {
        CursorAnchor = 0;
    }
    if (CursorAnchor > LastPos) {
        CursorAnchor = LastPos;
    }
    QTextCursor Restored(Document);
    Restored.setPosition(CursorAnchor);
    Restored.setPosition(CursorPos, QTextCursor::KeepAnchor);
    _TextEdit->setTextCursor(Restored);
    UpdateOptionMarkers();
    RefreshXmlTab();
    UpdateBlockButtons();
}

void TextEditor::InsertComment()
{
    QTextDocument* Document = _TextEdit->document();
    QTextCursor Cursor = _TextEdit->textCursor();
    if (TouchesTable(Document, Cursor)) {
        // A comment inside a cell or a caption keeps the whole
        // table preserved, so no comment is offered there.
        return;
    }
    if (TouchesSourceCode(Document, Cursor)) {
        // A comment is not code: stop the block first, so the
        // comment lands in a plain paragraph after it as a real
        // comment, not as literal text inside the code (Bug 443).
        StopSourceCode();
        Cursor = _TextEdit->textCursor();
    }
    if (TouchesFigure(Document, Cursor)) {
        // A comment is not artwork either: stop the figure
        // first, so the comment lands in a plain paragraph
        // after it.
        StopFigure();
        Cursor = _TextEdit->textCursor();
    }
    Cursor.beginEditBlock();
    DocTagEntry Entry;
    Entry.Name = QStringLiteral("comment");
    DocTagStack Stack = StackOf(Cursor.charFormat());
    Cursor.insertText(QStringLiteral("<!-- -->"),
                      FormatForMarker(Stack, Entry));
    Cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, 4);
    Cursor.endEditBlock();
    _TextEdit->setTextCursor(Cursor);
    _TextEdit->setFocus();
}

void TextEditor::OnBlocksButtonClicked()
{
    QTextDocument* Document = _TextEdit->document();
    QTextCursor Cursor = _TextEdit->textCursor();
    if (IsFigureBlock(Cursor.block())) {
        // The cursor sits in a figure, so the press stops it.
        ToggleFigure();
        return;
    }
    if (IsSourceCodeBlock(Cursor.block())) {
        // The cursor sits in code, so the press stops it.
        ToggleSourceCode();
        return;
    }
    if (TouchesFigure(Document, Cursor) ||
        TouchesSourceCode(Document, Cursor)) {
        // A selection touches a block but the cursor sits
        // outside it: there is nothing to stop, and nothing
        // starts.
        UpdateBlockButtons();
        _TextEdit->setFocus();
        return;
    }
    if (!_BlockKind.isEmpty()) {
        // A kind was picked from the menu, so the press toggles
        // that kind.
        if (_BlockKind == QLatin1String("artwork")) {
            ToggleArtwork();
        } else if (_BlockKind == QLatin1String("figure")) {
            ToggleFigure();
        } else {
            ToggleSourceCode();
        }
        return;
    }
    // No kind picked yet: the button has no default, so it
    // opens the menu instead of starting anything (Bug 444).
    _BlocksButton->showMenu();
}

void TextEditor::ToggleSourceCode()
{
    QTextDocument* Document = _TextEdit->document();
    QTextCursor Cursor = _TextEdit->textCursor();
    if (TouchesSourceCode(Document, Cursor)) {
        StopSourceCode();
    } else if (!TouchesFigure(Document, Cursor) &&
               !TouchesTable(Document, Cursor) &&
               ListKindsTouched(Document, Cursor).isEmpty()) {
        InsertSourceCode();
    }
    UpdateBlockButtons();
    // The button keeps keyboard focus after a click; hand it
    // back to the text so typing continues there.
    _TextEdit->setFocus();
}

void TextEditor::StopSourceCode()
{
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextBlock Block = Cursor.block();
    if (!IsSourceCodeBlock(Block)) {
        return;
    }
    QVariant Value = Block.blockFormat().property(DocSourceProperty);
    QTextBlock Last = Block;
    while (Last.next().isValid() && IsSourceCodeBlock(Last.next()) &&
           Last.next().blockFormat().property(DocSourceProperty) == Value) {
        Last = Last.next();
    }
    Cursor.beginEditBlock();
    Cursor.setPosition(Last.position());
    Cursor.movePosition(QTextCursor::EndOfBlock);
    InsertParagraphBreak(Cursor);
    _TextEdit->setTextCursor(Cursor);
    Cursor.endEditBlock();
    UpdateBlockButtons();
}

void TextEditor::InsertArtwork()
{
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextDocument* Document = _TextEdit->document();
    Cursor.beginEditBlock();
    QVector<QTextBlock> Touched;
    if (Cursor.hasSelection()) {
        int Start = Cursor.selectionStart();
        int End = Cursor.selectionEnd();
        for (QTextBlock Block = Document->findBlock(Start);
             Block.isValid() && Block.position() < End;
             Block = Block.next()) {
            Touched.append(Block);
        }
    } else {
        Touched.append(Cursor.block());
    }
    xmlNodePtr FigureNode = NewNode("artwork");
    DocDocument::SetNodeAttribute(FigureNode, "type", "ascii-art");
    QVariant Payload = FigureToVariant(FigureNode, 0);
    xmlFreeNode(FigureNode);
    int Group = NextFigureGroup(Document);
    for (const QTextBlock& Block : Touched) {
        DetachBlockFromList(Document, Block);
        // Artwork holds literal text only, so the block's text
        // is rewritten without tags or markers. The block is
        // marked first: the rewritten text may hold line breaks,
        // and the blocks they split off inherit the marking.
        QString Text = CodeTextOf(Block);
        QTextCursor At(Document);
        At.setPosition(Block.position());
        SetFigureProperties(At, QStringLiteral("artwork"), Group,
                            QStringLiteral("artwork"), Payload);
        At.setPosition(Block.position());
        At.setPosition(Block.position() + Block.length() - 1,
                       QTextCursor::KeepAnchor);
        At.setCharFormat(CodeCharFormat());
        At.insertText(Text);
    }
    Cursor.endEditBlock();
    QTextCursor At = _TextEdit->textCursor();
    At.setCharFormat(CodeCharFormat());
    _TextEdit->setTextCursor(At);
    UpdateBlockButtons();
}

void TextEditor::InsertFigure()
{
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextDocument* Document = _TextEdit->document();
    Cursor.beginEditBlock();
    QVector<QTextBlock> Touched;
    if (Cursor.hasSelection()) {
        int Start = Cursor.selectionStart();
        int End = Cursor.selectionEnd();
        for (QTextBlock Block = Document->findBlock(Start);
             Block.isValid() && Block.position() < End;
             Block = Block.next()) {
            Touched.append(Block);
        }
    } else {
        Touched.append(Cursor.block());
    }
    if (Touched.isEmpty()) {
        Cursor.endEditBlock();
        return;
    }
    xmlNodePtr FigureNode = NewNode("figure");
    xmlNodePtr Artwork = AddElementChild(FigureNode, "artwork");
    DocDocument::SetNodeAttribute(Artwork, "type", "ascii-art");
    QVariant Payload = FigureToVariant(FigureNode, 0);
    xmlFreeNode(FigureNode);
    int Group = NextFigureGroup(Document);
    // The touched blocks are marked as artwork first: the
    // caption block is split off in front of them afterwards,
    // and a block split changes which block a collected handle
    // names, so the caption is found by position, not by handle.
    int FirstPos = Touched.first().position();
    for (const QTextBlock& Block : Touched) {
        DetachBlockFromList(Document, Block);
        QString Text = CodeTextOf(Block);
        QTextCursor At(Document);
        At.setPosition(Block.position());
        SetFigureProperties(At, QStringLiteral("figure"), Group,
                            QStringLiteral("artwork"), Payload);
        At.setPosition(Block.position());
        At.setPosition(Block.position() + Block.length() - 1,
                       QTextCursor::KeepAnchor);
        At.setCharFormat(CodeCharFormat());
        At.insertText(Text);
    }
    // The caption block is an empty block split off in front of
    // the first artwork block, then marked as the caption.
    QTextCursor Before(Document);
    Before.setPosition(FirstPos);
    Before.insertBlock();
    Before.movePosition(QTextCursor::PreviousBlock);
    DetachBlockFromList(Document, Before.block());
    SetFigureProperties(Before, QStringLiteral("figure"), Group,
                        QStringLiteral("caption"), Payload);
    Cursor.endEditBlock();
    // The cursor goes to the caption, with a bold typing
    // format, so the figure starts with its caption.
    QTextCursor At(Document);
    At.setPosition(FirstPos);
    QTextCharFormat CaptionTyping = At.charFormat();
    CaptionTyping.setFontWeight(QFont::Bold);
    At.setCharFormat(CaptionTyping);
    _TextEdit->setTextCursor(At);
    UpdateBlockButtons();
}

void TextEditor::RemoveFigure()
{
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextDocument* Document = _TextEdit->document();
    QVector<QTextBlock> Touched;
    if (Cursor.hasSelection()) {
        int Start = Cursor.selectionStart();
        int End = Cursor.selectionEnd();
        for (QTextBlock Block = Document->findBlock(Start);
             Block.isValid() && Block.position() < End;
             Block = Block.next()) {
            Touched.append(Block);
        }
    } else {
        Touched.append(Cursor.block());
    }
    // A version included from a file still has a copy outside
    // the document, so removing it needs no question. A version
    // kept only in the document has no other copy, so removing
    // a figure holding one is confirmed first. The reader keeps
    // figures holding a src version whole today, so every
    // version reaching this check is inline; the check stands
    // for when src versions are modeled (Bug 445).
    for (const QTextBlock& TouchedBlock : Touched) {
        xmlNodePtr FigureNode = nullptr;
        int Visible = 0;
        if (!IsFigureBlock(TouchedBlock) ||
            !FigurePayloadOf(TouchedBlock, FigureNode, Visible)) {
            xmlFreeNode(FigureNode);
            continue;
        }
        bool HasInline = false;
        QVector<xmlNodePtr> Versions = FigureVersionNodes(FigureNode);
        for (xmlNodePtr Version : Versions) {
            if (!ArtworkVersionIsFromFile(Version)) {
                HasInline = true;
                break;
            }
        }
        if (!HasInline) {
            xmlFreeNode(FigureNode);
            continue;
        }
        QString Title;
        QString Text;
        if (DocDocument::NodeNameEquals(FigureNode, "figure")) {
            Title = tr("Remove figure");
            if (Versions.size() > 1) {
                Text = tr("Removing this figure removes all of its versions. "
                          "The versions kept only in this document have no "
                          "other copy and cannot be recovered. "
                          "Remove the figure?");
            } else {
                Text = tr("This figure is kept only in this document. There "
                          "is no other copy of it, and removing it cannot be "
                          "recovered. Remove the figure?");
            }
        } else {
            Title = tr("Remove ASCII art");
            if (Versions.size() > 1) {
                Text = tr("Removing this artwork removes all of its versions. "
                          "The versions kept only in this document have no "
                          "other copy and cannot be recovered. "
                          "Remove the artwork?");
            } else {
                Text = tr("This artwork is kept only in this document. There "
                          "is no other copy of it, and removing it cannot be "
                          "recovered. Remove the artwork?");
            }
        }
        if (QMessageBox::question(this, Title, Text,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            xmlFreeNode(FigureNode);
            return;
        }
        xmlFreeNode(FigureNode);
        break;
    }
    Cursor.beginEditBlock();
    QSet<int> DoneStarts;
    for (const QTextBlock& TouchedBlock : Touched) {
        if (!IsFigureBlock(TouchedBlock)) {
            continue;
        }
        QVector<QTextBlock> Region = FigureRegionBlocks(TouchedBlock);
        if (Region.isEmpty() ||
            DoneStarts.contains(Region.first().position())) {
            continue;
        }
        DoneStarts.insert(Region.first().position());
        // The region unwraps to plain paragraphs: the caption
        // becomes a plain paragraph and the artwork lines become
        // plain paragraphs. Every version goes; a figure holding
        // a version kept only in the document was confirmed
        // before this loop.
        for (const QTextBlock& Block : Region) {
            QTextCursor At(Document);
            At.setPosition(Block.position());
            if (IsPlaceholderBlock(Block)) {
                // The placeholder line is display only: its text
                // must not survive as a paragraph once the figure
                // properties are gone. Clearing it leaves an empty
                // block, which the read-back skips.
                At.movePosition(QTextCursor::EndOfBlock,
                                QTextCursor::KeepAnchor);
                At.removeSelectedText();
                At.setPosition(Block.position());
            }
            QTextBlockFormat Format = At.blockFormat();
            Format.setIndent(0);
            Format.clearProperty(QTextFormat::BackgroundBrush);
            Format.clearProperty(DocFigureKindProperty);
            Format.clearProperty(DocFigureGroupProperty);
            Format.clearProperty(DocFigureRoleProperty);
            Format.clearProperty(DocFigurePayloadProperty);
            Format.setBottomMargin(ParagraphGap(Document));
            At.setBlockFormat(Format);
            RefreshBlockVisuals(Document, Block);
        }
    }
    Cursor.endEditBlock();
    // The edit cursor keeps the figure typing format after the
    // region is unwrapped; reset it to plain, or the next typed
    // text stays fixed-width. The cursor is collapsed to its
    // own position first, so the reset cannot reformat a
    // selection.
    QTextCursor At = _TextEdit->textCursor();
    At.setPosition(At.position());
    At.setCharFormat(QTextCharFormat());
    _TextEdit->setTextCursor(At);
    UpdateBlockButtons();
}

/**
 * @brief Asks for a table count: body rows or columns.
 *
 * A small dialog in the Insert table dialog's style: one spin
 * box showing the current value, with OK and Cancel. The value
 * stays between 1 and 99.
 * @param Parent The parent widget.
 * @param Title The dialog's title.
 * @param Label The spin box's label.
 * @param Current The current value, shown to start.
 * @param Value Receives the picked value.
 * @return False when the dialog was cancelled.
 */
static bool PromptForTableCount(QWidget* Parent, const QString& Title,
                                const QString& Label, int Current,
                                int& Value)
{
    QDialog Dialog(Parent);
    Dialog.setWindowTitle(Title);
    QSpinBox* CountBox = new QSpinBox(&Dialog);
    CountBox->setRange(1, 99);
    CountBox->setValue(Current);
    QDialogButtonBox* Buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &Dialog);
    QObject::connect(Buttons, &QDialogButtonBox::accepted, &Dialog, &QDialog::accept);
    QObject::connect(Buttons, &QDialogButtonBox::rejected, &Dialog, &QDialog::reject);
    QFormLayout* Layout = new QFormLayout(&Dialog);
    Layout->addRow(Label, CountBox);
    Layout->addWidget(Buttons);
    if (Dialog.exec() != QDialog::Accepted) {
        return false;
    }
    Value = CountBox->value();
    return true;
}

void TextEditor::InsertTable()
{
    QTextDocument* Document = _TextEdit->document();
    QTextCursor Cursor = _TextEdit->textCursor();
    if (TouchesTable(Document, Cursor) ||
        TouchesFigure(Document, Cursor) ||
        TouchesSourceCode(Document, Cursor) ||
        !ListKindsTouched(Document, Cursor).isEmpty()) {
        return;
    }
    QDialog Dialog(this);
    Dialog.setWindowTitle(tr("Insert table"));
    QSpinBox* RowsBox = new QSpinBox(&Dialog);
    RowsBox->setRange(1, 99);
    RowsBox->setValue(2);
    QSpinBox* ColumnsBox = new QSpinBox(&Dialog);
    ColumnsBox->setRange(1, 99);
    ColumnsBox->setValue(2);
    QCheckBox* HeaderBox = new QCheckBox(tr("Header row"), &Dialog);
    HeaderBox->setChecked(true);
    QCheckBox* FooterBox = new QCheckBox(tr("Footer row"), &Dialog);
    FooterBox->setChecked(false);
    QDialogButtonBox* Buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &Dialog);
    connect(Buttons, &QDialogButtonBox::accepted, &Dialog, &QDialog::accept);
    connect(Buttons, &QDialogButtonBox::rejected, &Dialog, &QDialog::reject);
    QFormLayout* Layout = new QFormLayout(&Dialog);
    Layout->addRow(tr("Body rows:"), RowsBox);
    Layout->addRow(tr("Columns:"), ColumnsBox);
    Layout->addRow(HeaderBox);
    Layout->addRow(FooterBox);
    Layout->addWidget(Buttons);
    if (Dialog.exec() != QDialog::Accepted) {
        return;
    }
    xmlNodePtr TableNode = NewNode("table");
    if (HeaderBox->isChecked()) {
        xmlNodePtr Head = AddElementChild(TableNode, "thead");
        xmlNodePtr HeadRow = AddElementChild(Head, "tr");
        for (int c = 0; c < ColumnsBox->value(); ++c) {
            AddElementChild(HeadRow, "th");
        }
    }
    xmlNodePtr Body = AddElementChild(TableNode, "tbody");
    for (int r = 0; r < RowsBox->value(); ++r) {
        xmlNodePtr BodyRow = AddElementChild(Body, "tr");
        for (int c = 0; c < ColumnsBox->value(); ++c) {
            AddElementChild(BodyRow, "td");
        }
    }
    if (FooterBox->isChecked()) {
        xmlNodePtr Foot = AddElementChild(TableNode, "tfoot");
        xmlNodePtr FootRow = AddElementChild(Foot, "tr");
        for (int c = 0; c < ColumnsBox->value(); ++c) {
            AddElementChild(FootRow, "td");
        }
    }
    Cursor.beginEditBlock();
    if (Cursor.hasSelection() ||
        !Cursor.block().text().trimmed().isEmpty()) {
        // The table needs a fresh block for its caption: the
        // cursor moves past the paragraph it sits in, and a new
        // block is made after it, so no paragraph is split.
        Cursor.setPosition(Cursor.block().position());
        Cursor.movePosition(QTextCursor::EndOfBlock);
        InsertParagraphBreak(Cursor);
    }
    int CaptionPos = Cursor.block().position();
    InsertTableNode(Cursor, TableNode);
    xmlFreeNode(TableNode);
    Cursor.endEditBlock();
    // The cursor goes to the caption, with a bold typing
    // format, so the table starts with its caption.
    QTextCursor At(Document);
    At.setPosition(CaptionPos);
    QTextCharFormat CaptionTyping = At.charFormat();
    CaptionTyping.setFontWeight(QFont::Bold);
    At.setCharFormat(CaptionTyping);
    _TextEdit->setTextCursor(At);
    _TextEdit->setFocus();
    UpdateBlockButtons();
}

void TextEditor::InsertTableRowAbove()
{
    InsertTableRow(true);
}

void TextEditor::InsertTableRowBelow()
{
    InsertTableRow(false);
}

void TextEditor::InsertTableRow(bool Above)
{
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextTable* Frame = Cursor.currentTable();
    if (Frame == nullptr) {
        return;
    }
    int Row = -1;
    int Column = -1;
    if (!TableCellAtCursor(Frame, Cursor, Row, Column)) {
        return;
    }
    QTextDocument* Document = _TextEdit->document();
    QTextBlock FirstBlock =
        Frame->cellAt(0, 0).firstCursorPosition().block();
    xmlNodePtr Shell = nullptr;
    QVector<DocTableRowMark> Marks;
    TablePayloadOf(FirstBlock, Shell, Marks);
    int At = Above ? Row : Row + 1;
    DocTableRowMark Mark;
    Mark.Section = SectionOfRow(Marks, Row);
    if (At <= Marks.size()) {
        Marks.insert(At, Mark);
    } else {
        Marks.append(Mark);
    }
    QVariant Payload = TableToVariant(Shell, Marks);
    xmlFreeNode(Shell);
    int Group = FirstBlock.blockFormat()
                    .property(DocTableGroupProperty)
                    .toInt();
    Cursor.beginEditBlock();
    Frame->insertRows(At, 1);
    RestampTable(Document, Frame, Payload, Marks, Group);
    Cursor.endEditBlock();
    // The cursor goes to the new row's first cell of its own:
    // a rowspan from above can cover the row's first columns,
    // and those positions belong to the row above.
    QTextCursor AtCell;
    for (int c = 0; c < Frame->columns(); ++c) {
        QTextCursor Probe = Frame->cellAt(At, c).firstCursorPosition();
        int ProbeRow = -1;
        int ProbeColumn = -1;
        if (TableCellAtCursor(Frame, Probe, ProbeRow, ProbeColumn) &&
            ProbeRow == At) {
            AtCell = Probe;
            break;
        }
    }
    if (AtCell.isNull()) {
        AtCell = Frame->cellAt(At, 0).firstCursorPosition();
    }
    _TextEdit->setTextCursor(AtCell);
    _TextEdit->setFocus();
    UpdateBlockButtons();
}

void TextEditor::DeleteTableRow()
{
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextTable* Frame = Cursor.currentTable();
    if (Frame == nullptr) {
        return;
    }
    int Row = -1;
    int Column = -1;
    if (!TableCellAtCursor(Frame, Cursor, Row, Column)) {
        return;
    }
    QTextDocument* Document = _TextEdit->document();
    QTextBlock FirstBlock =
        Frame->cellAt(0, 0).firstCursorPosition().block();
    xmlNodePtr Shell = nullptr;
    QVector<DocTableRowMark> Marks;
    TablePayloadOf(FirstBlock, Shell, Marks);
    if (SectionOfRow(Marks, Row) == QLatin1String("tbody")) {
        int BodyRows = 0;
        if (Marks.isEmpty()) {
            BodyRows = Frame->rows();
        } else {
            for (const DocTableRowMark& Mark : Marks) {
                if (Mark.Section == QLatin1String("tbody")) {
                    ++BodyRows;
                }
            }
        }
        if (BodyRows <= 1) {
            // The last body row cannot go: a table always
            // keeps a body.
            xmlFreeNode(Shell);
            return;
        }
    }
    if (Row < Marks.size()) {
        Marks.removeAt(Row);
    }
    QVariant Payload = TableToVariant(Shell, Marks);
    xmlFreeNode(Shell);
    int Group = FirstBlock.blockFormat()
                    .property(DocTableGroupProperty)
                    .toInt();
    Cursor.beginEditBlock();
    Frame->removeRows(Row, 1);
    RestampTable(Document, Frame, Payload, Marks, Group);
    Cursor.endEditBlock();
    int ToRow = Row < Frame->rows() ? Row : Frame->rows() - 1;
    int ToColumn =
        Column < Frame->columns() ? Column : Frame->columns() - 1;
    QTextCursor AtCell =
        Frame->cellAt(ToRow, ToColumn).firstCursorPosition();
    _TextEdit->setTextCursor(AtCell);
    _TextEdit->setFocus();
    UpdateBlockButtons();
}

void TextEditor::InsertTableColumnLeft()
{
    InsertTableColumn(true);
}

void TextEditor::InsertTableColumnRight()
{
    InsertTableColumn(false);
}

void TextEditor::InsertTableColumn(bool Left)
{
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextTable* Frame = Cursor.currentTable();
    if (Frame == nullptr) {
        return;
    }
    int Row = -1;
    int Column = -1;
    if (!TableCellAtCursor(Frame, Cursor, Row, Column)) {
        return;
    }
    QTextDocument* Document = _TextEdit->document();
    QTextBlock FirstBlock =
        Frame->cellAt(0, 0).firstCursorPosition().block();
    xmlNodePtr Shell = nullptr;
    QVector<DocTableRowMark> Marks;
    TablePayloadOf(FirstBlock, Shell, Marks);
    QVariant Payload = TableToVariant(Shell, Marks);
    xmlFreeNode(Shell);
    int Group = FirstBlock.blockFormat()
                    .property(DocTableGroupProperty)
                    .toInt();
    int At = Left ? Column : Column + 1;
    Cursor.beginEditBlock();
    Frame->insertColumns(At, 1);
    RestampTable(Document, Frame, Payload, Marks, Group);
    Cursor.endEditBlock();
    QTextCursor AtCell =
        Frame->cellAt(Row, At).firstCursorPosition();
    _TextEdit->setTextCursor(AtCell);
    _TextEdit->setFocus();
    UpdateBlockButtons();
}

void TextEditor::DeleteTableColumn()
{
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextTable* Frame = Cursor.currentTable();
    if (Frame == nullptr || Frame->columns() <= 1) {
        return;
    }
    int Row = -1;
    int Column = -1;
    if (!TableCellAtCursor(Frame, Cursor, Row, Column)) {
        return;
    }
    QTextDocument* Document = _TextEdit->document();
    QTextBlock FirstBlock =
        Frame->cellAt(0, 0).firstCursorPosition().block();
    xmlNodePtr Shell = nullptr;
    QVector<DocTableRowMark> Marks;
    TablePayloadOf(FirstBlock, Shell, Marks);
    QVariant Payload = TableToVariant(Shell, Marks);
    xmlFreeNode(Shell);
    int Group = FirstBlock.blockFormat()
                    .property(DocTableGroupProperty)
                    .toInt();
    Cursor.beginEditBlock();
    Frame->removeColumns(Column, 1);
    RestampTable(Document, Frame, Payload, Marks, Group);
    Cursor.endEditBlock();
    int ToColumn =
        Column < Frame->columns() ? Column : Frame->columns() - 1;
    QTextCursor AtCell =
        Frame->cellAt(Row, ToColumn).firstCursorPosition();
    _TextEdit->setTextCursor(AtCell);
    _TextEdit->setFocus();
    UpdateBlockButtons();
}

void TextEditor::SetTableBodyRowCount()
{
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextTable* Frame = Cursor.currentTable();
    if (Frame == nullptr) {
        return;
    }
    int Row = -1;
    int Column = -1;
    if (!TableCellAtCursor(Frame, Cursor, Row, Column)) {
        return;
    }
    QTextDocument* Document = _TextEdit->document();
    QTextBlock FirstBlock =
        Frame->cellAt(0, 0).firstCursorPosition().block();
    xmlNodePtr Shell = nullptr;
    QVector<DocTableRowMark> Marks;
    TablePayloadOf(FirstBlock, Shell, Marks);
    int FirstBody = 0;
    int BodyRows = 0;
    for (int r = 0; r < Frame->rows(); ++r) {
        if (SectionOfRow(Marks, r) == QLatin1String("tbody")) {
            if (BodyRows == 0) {
                FirstBody = r;
            }
            ++BodyRows;
        }
    }
    int Wanted = BodyRows;
    if (!PromptForTableCount(this, tr("Body rows"), tr("Body rows:"),
                             BodyRows, Wanted)) {
        xmlFreeNode(Shell);
        return;
    }
    if (Wanted == BodyRows) {
        xmlFreeNode(Shell);
        return;
    }
    int Group = FirstBlock.blockFormat()
                    .property(DocTableGroupProperty)
                    .toInt();
    int At = FirstBody + (Wanted < BodyRows ? Wanted : BodyRows);
    if (Wanted > BodyRows) {
        for (int i = 0; i < Wanted - BodyRows; ++i) {
            DocTableRowMark Mark;
            Mark.Section = QStringLiteral("tbody");
            if (At + i <= Marks.size()) {
                Marks.insert(At + i, Mark);
            } else {
                Marks.append(Mark);
            }
        }
    } else {
        for (int i = 0; i < BodyRows - Wanted; ++i) {
            if (At < Marks.size()) {
                Marks.removeAt(At);
            }
        }
    }
    QVariant Payload = TableToVariant(Shell, Marks);
    xmlFreeNode(Shell);
    Cursor.beginEditBlock();
    if (Wanted > BodyRows) {
        Frame->insertRows(At, Wanted - BodyRows);
    } else {
        // One row at a time: removing several rows in one call
        // across a rowspan aborts Qt (harness-proven), while
        // single removals at the same index are safe. Each
        // removal pulls the next body row into the index.
        for (int i = 0; i < BodyRows - Wanted; ++i) {
            Frame->removeRows(At, 1);
        }
    }
    RestampTable(Document, Frame, Payload, Marks, Group);
    Cursor.endEditBlock();
    int ToRow = Row < Frame->rows() ? Row : Frame->rows() - 1;
    int ToColumn =
        Column < Frame->columns() ? Column : Frame->columns() - 1;
    QTextCursor AtCell =
        Frame->cellAt(ToRow, ToColumn).firstCursorPosition();
    _TextEdit->setTextCursor(AtCell);
    _TextEdit->setFocus();
    UpdateBlockButtons();
}

void TextEditor::SetTableColumnCount()
{
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextTable* Frame = Cursor.currentTable();
    if (Frame == nullptr) {
        return;
    }
    int Row = -1;
    int Column = -1;
    if (!TableCellAtCursor(Frame, Cursor, Row, Column)) {
        return;
    }
    int Current = Frame->columns();
    int Wanted = Current;
    if (!PromptForTableCount(this, tr("Columns"), tr("Columns:"),
                             Current, Wanted)) {
        return;
    }
    if (Wanted == Current) {
        return;
    }
    QTextDocument* Document = _TextEdit->document();
    QTextBlock FirstBlock =
        Frame->cellAt(0, 0).firstCursorPosition().block();
    xmlNodePtr Shell = nullptr;
    QVector<DocTableRowMark> Marks;
    TablePayloadOf(FirstBlock, Shell, Marks);
    QVariant Payload = TableToVariant(Shell, Marks);
    xmlFreeNode(Shell);
    int Group = FirstBlock.blockFormat()
                    .property(DocTableGroupProperty)
                    .toInt();
    Cursor.beginEditBlock();
    if (Wanted > Current) {
        Frame->insertColumns(Current, Wanted - Current);
    } else {
        // One column at a time: removing several columns in
        // one call across a colspan adds a phantom row
        // (harness-proven), while single removals at the same
        // index are safe. Each removal pulls the next column
        // into the index.
        for (int i = 0; i < Current - Wanted; ++i) {
            Frame->removeColumns(Wanted, 1);
        }
    }
    RestampTable(Document, Frame, Payload, Marks, Group);
    Cursor.endEditBlock();
    int ToRow = Row < Frame->rows() ? Row : Frame->rows() - 1;
    int ToColumn =
        Column < Frame->columns() ? Column : Frame->columns() - 1;
    QTextCursor AtCell =
        Frame->cellAt(ToRow, ToColumn).firstCursorPosition();
    _TextEdit->setTextCursor(AtCell);
    _TextEdit->setFocus();
    UpdateBlockButtons();
}

void TextEditor::ToggleTableHeaderRow()
{
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextTable* Frame = Cursor.currentTable();
    if (Frame == nullptr) {
        return;
    }
    int Row = -1;
    int Column = -1;
    if (!TableCellAtCursor(Frame, Cursor, Row, Column)) {
        return;
    }
    QTextDocument* Document = _TextEdit->document();
    QTextBlock FirstBlock =
        Frame->cellAt(0, 0).firstCursorPosition().block();
    xmlNodePtr Shell = nullptr;
    QVector<DocTableRowMark> Marks;
    TablePayloadOf(FirstBlock, Shell, Marks);
    int HeaderRows = 0;
    for (int r = 0; r < Frame->rows(); ++r) {
        if (SectionOfRow(Marks, r) == QLatin1String("thead")) {
            ++HeaderRows;
        }
    }
    if (HeaderRows > 0 && Frame->rows() - HeaderRows < 1) {
        xmlFreeNode(Shell);
        return;
    }
    int Group = FirstBlock.blockFormat()
                    .property(DocTableGroupProperty)
                    .toInt();
    if (HeaderRows > 0) {
        for (int i = 0; i < HeaderRows; ++i) {
            if (!Marks.isEmpty()) {
                Marks.removeAt(0);
            }
        }
    } else {
        DocTableRowMark Mark;
        Mark.Section = QStringLiteral("thead");
        Marks.insert(0, Mark);
    }
    QVariant Payload = TableToVariant(Shell, Marks);
    xmlFreeNode(Shell);
    Cursor.beginEditBlock();
    if (HeaderRows > 0) {
        // One row at a time, at the head of the grid: see
        // SetTableBodyRowCount for why a multi-row removal is
        // never used.
        for (int i = 0; i < HeaderRows; ++i) {
            Frame->removeRows(0, 1);
        }
    } else {
        Frame->insertRows(0, 1);
    }
    RestampTable(Document, Frame, Payload, Marks, Group);
    Cursor.endEditBlock();
    int ToRow = 0;
    if (HeaderRows > 0) {
        ToRow = Row - HeaderRows;
        if (ToRow < 0) {
            ToRow = 0;
        }
        if (ToRow >= Frame->rows()) {
            ToRow = Frame->rows() - 1;
        }
    }
    int ToColumn =
        Column < Frame->columns() ? Column : Frame->columns() - 1;
    QTextCursor AtCell =
        Frame->cellAt(ToRow, ToColumn).firstCursorPosition();
    _TextEdit->setTextCursor(AtCell);
    _TextEdit->setFocus();
    UpdateBlockButtons();
}

void TextEditor::ToggleTableFooterRow()
{
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextTable* Frame = Cursor.currentTable();
    if (Frame == nullptr) {
        return;
    }
    int Row = -1;
    int Column = -1;
    if (!TableCellAtCursor(Frame, Cursor, Row, Column)) {
        return;
    }
    QTextDocument* Document = _TextEdit->document();
    QTextBlock FirstBlock =
        Frame->cellAt(0, 0).firstCursorPosition().block();
    xmlNodePtr Shell = nullptr;
    QVector<DocTableRowMark> Marks;
    TablePayloadOf(FirstBlock, Shell, Marks);
    int FooterRows = 0;
    for (int r = 0; r < Frame->rows(); ++r) {
        if (SectionOfRow(Marks, r) == QLatin1String("tfoot")) {
            ++FooterRows;
        }
    }
    if (FooterRows > 0 && Frame->rows() - FooterRows < 1) {
        xmlFreeNode(Shell);
        return;
    }
    int Group = FirstBlock.blockFormat()
                    .property(DocTableGroupProperty)
                    .toInt();
    int FirstFooter = Frame->rows() - FooterRows;
    if (FooterRows > 0) {
        for (int i = 0; i < FooterRows; ++i) {
            if (FirstFooter < Marks.size()) {
                Marks.removeAt(FirstFooter);
            }
        }
    } else {
        DocTableRowMark Mark;
        Mark.Section = QStringLiteral("tfoot");
        Marks.append(Mark);
    }
    QVariant Payload = TableToVariant(Shell, Marks);
    xmlFreeNode(Shell);
    Cursor.beginEditBlock();
    if (FooterRows > 0) {
        // One row at a time, at the foot of the grid: see
        // SetTableBodyRowCount for why a multi-row removal is
        // never used.
        for (int i = 0; i < FooterRows; ++i) {
            Frame->removeRows(FirstFooter, 1);
        }
    } else {
        Frame->insertRows(Frame->rows(), 1);
    }
    RestampTable(Document, Frame, Payload, Marks, Group);
    Cursor.endEditBlock();
    int ToRow = Row;
    if (FooterRows == 0) {
        ToRow = Frame->rows() - 1;
    }
    if (ToRow >= Frame->rows()) {
        ToRow = Frame->rows() - 1;
    }
    int ToColumn =
        Column < Frame->columns() ? Column : Frame->columns() - 1;
    QTextCursor AtCell =
        Frame->cellAt(ToRow, ToColumn).firstCursorPosition();
    _TextEdit->setTextCursor(AtCell);
    _TextEdit->setFocus();
    UpdateBlockButtons();
}

void TextEditor::MergeTableCells()
{
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextTable* Frame = Cursor.currentTable();
    if (Frame == nullptr || !Cursor.hasSelection()) {
        return;
    }
    QTextDocument* Document = _TextEdit->document();
    QTextCursor From(Document);
    From.setPosition(Cursor.selectionStart());
    QTextCursor To(Document);
    To.setPosition(Cursor.selectionEnd());
    if (From.currentTable() != Frame || To.currentTable() != Frame) {
        return;
    }
    int FromRow = -1;
    int FromColumn = -1;
    int ToRow = -1;
    int ToColumn = -1;
    if (!TableCellAtCursor(Frame, From, FromRow, FromColumn) ||
        !TableCellAtCursor(Frame, To, ToRow, ToColumn)) {
        return;
    }
    int TopRow = FromRow < ToRow ? FromRow : ToRow;
    int LeftColumn = FromColumn < ToColumn ? FromColumn : ToColumn;
    int BottomRow = FromRow > ToRow ? FromRow : ToRow;
    int RightColumn = FromColumn > ToColumn ? FromColumn : ToColumn;
    if (TopRow == BottomRow && LeftColumn == RightColumn) {
        return;
    }
    QTextBlock FirstBlock =
        Frame->cellAt(0, 0).firstCursorPosition().block();
    xmlNodePtr Shell = nullptr;
    QVector<DocTableRowMark> Marks;
    TablePayloadOf(FirstBlock, Shell, Marks);
    QVariant Payload = TableToVariant(Shell, Marks);
    xmlFreeNode(Shell);
    int Group = FirstBlock.blockFormat()
                    .property(DocTableGroupProperty)
                    .toInt();
    // The merged cell keeps the top-left cell's data: its
    // header flag and kept attributes. The other cells' text
    // joins it, and their kept attributes are dropped.
    xmlNodePtr TopLeft = nullptr;
    QTextBlock OriginBlock =
        Frame->cellAt(TopRow, LeftColumn).firstCursorPosition().block();
    TableCellFromVariant(
        OriginBlock.blockFormat().property(DocTableCellProperty), TopLeft);
    Cursor.beginEditBlock();
    Frame->mergeCells(TopRow, LeftColumn, BottomRow - TopRow + 1,
                      RightColumn - LeftColumn + 1);
    QTextTableCell Merged = Frame->cellAt(TopRow, LeftColumn);
    QTextCursor AtCell = Merged.firstCursorPosition();
    SetTableCellProperties(AtCell, Group, Payload, TopLeft);
    if (TopLeft != nullptr &&
        DocDocument::NodeNameEquals(TopLeft, "th")) {
        // The joined text shows bold throughout, as one
        // header cell.
        QTextBlock LastBlock = Merged.lastCursorPosition().block();
        for (QTextBlock MergedBlock = Merged.firstCursorPosition().block();
             MergedBlock.isValid(); MergedBlock = MergedBlock.next()) {
            BoldBlockFragments(Document, MergedBlock);
            if (MergedBlock == LastBlock) {
                break;
            }
        }
        QTextCharFormat Typing = AtCell.charFormat();
        Typing.setFontWeight(QFont::Bold);
        AtCell.setCharFormat(Typing);
    }
    xmlFreeNode(TopLeft);
    Cursor.endEditBlock();
    _TextEdit->setTextCursor(AtCell);
    _TextEdit->setFocus();
    UpdateBlockButtons();
}

void TextEditor::SplitTableCell()
{
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextTable* Frame = Cursor.currentTable();
    if (Frame == nullptr) {
        return;
    }
    int Row = -1;
    int Column = -1;
    if (!TableCellAtCursor(Frame, Cursor, Row, Column)) {
        return;
    }
    QTextTableCell Cell = Frame->cellAt(Row, Column);
    int OldRowSpan = Cell.rowSpan();
    int OldColumnSpan = Cell.columnSpan();
    if (OldRowSpan <= 1 && OldColumnSpan <= 1) {
        return;
    }
    QTextBlock FirstBlock =
        Frame->cellAt(0, 0).firstCursorPosition().block();
    xmlNodePtr Shell = nullptr;
    QVector<DocTableRowMark> Marks;
    TablePayloadOf(FirstBlock, Shell, Marks);
    QVariant Payload = TableToVariant(Shell, Marks);
    xmlFreeNode(Shell);
    int Group = FirstBlock.blockFormat()
                    .property(DocTableGroupProperty)
                    .toInt();
    xmlNodePtr Origin = nullptr;
    TableCellFromVariant(
        Cell.firstCursorPosition().block().blockFormat().property(
            DocTableCellProperty),
        Origin);
    Cursor.beginEditBlock();
    Frame->splitCell(Row, Column, 1, 1);
    // The split hands the new cells the origin cell's marking,
    // so each is re-marked fresh: the header flag is copied,
    // and the kept attributes stay on the origin cell only.
    for (int r = Row; r < Row + OldRowSpan; ++r) {
        for (int c = Column; c < Column + OldColumnSpan; ++c) {
            if (r == Row && c == Column) {
                continue;
            }
            QTextTableCell Fresh = Frame->cellAt(r, c);
            QTextCursor AtCell = Fresh.firstCursorPosition();
            xmlNodePtr FreshCell = nullptr;
            if (Origin != nullptr &&
                DocDocument::NodeNameEquals(Origin, "th")) {
                FreshCell = NewNode("th");
            } else {
                FreshCell = NewNode("td");
            }
            SetTableCellProperties(AtCell, Group, Payload, FreshCell);
            if (FreshCell != nullptr &&
                DocDocument::NodeNameEquals(FreshCell, "th")) {
                QTextCharFormat Typing = AtCell.charFormat();
                Typing.setFontWeight(QFont::Bold);
                AtCell.setCharFormat(Typing);
            }
            xmlFreeNode(FreshCell);
        }
    }
    xmlFreeNode(Origin);
    Cursor.endEditBlock();
    QTextCursor AtCell =
        Frame->cellAt(Row, Column).firstCursorPosition();
    _TextEdit->setTextCursor(AtCell);
    _TextEdit->setFocus();
    UpdateBlockButtons();
}

void TextEditor::RemoveTable()
{
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextDocument* Document = _TextEdit->document();
    QTextTable* Frame = Cursor.currentTable();
    if (Frame == nullptr && IsTableCaptionBlock(Cursor.block())) {
        // The cursor sits in the caption, before the frame.
        QTextBlock Next = Cursor.block().next();
        if (Next.isValid()) {
            Frame = TableOfBlock(Document, Next);
        }
    }
    if (Frame == nullptr) {
        return;
    }
    if (QMessageBox::question(
            this, tr("Remove table"),
            tr("This table is kept only in this document. There is no "
               "other copy of it, and removing it cannot be recovered. "
               "Remove the table?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    // The table is read before the frame is deleted: the
    // caption and the cell text become plain paragraphs, in row
    // order. The caption block is the block before the frame;
    // its text stays where it is, as the first paragraph.
    QTextBlock FirstCellBlock =
        Frame->cellAt(0, 0).firstCursorPosition().block();
    QTextBlock BeforeBlock = FirstCellBlock.previous();
    xmlNodePtr CaptionNode = nullptr;
    int CaptionPos = -1;
    if (BeforeBlock.isValid() && IsTableCaptionBlock(BeforeBlock)) {
        CaptionNode = NodeFromBlock(BeforeBlock);
        CaptionPos = BeforeBlock.position();
    } else if (BeforeBlock.isValid()) {
        CaptionPos = BeforeBlock.position();
    }
    xmlNodePtr TableNode = TableFromFrame(Frame, CaptionNode);
    xmlFreeNode(CaptionNode);
    Cursor.beginEditBlock();
    // An empty block is made after the frame first, so deleting
    // the frame merges the block before it with an empty block,
    // and the paragraphs around the table survive untouched.
    // The frame itself goes when the selection covers its
    // boundary markers on both sides: deleting only its text
    // would leave an empty grid behind.
    QTextCursor After(Document);
    After.setPosition(Frame->lastPosition() + 1);
    After.insertBlock();
    int First = Frame->firstPosition();
    int Last = Frame->lastPosition();
    QTextCursor Delete(Document);
    Delete.setPosition(First - 1);
    Delete.setPosition(Last + 1, QTextCursor::KeepAnchor);
    Delete.removeSelectedText();
    // The block the frame merged into sits where the block
    // before the frame sat; with no block before the frame, it
    // sits at the start of the document.
    QTextBlock Merged = Document->findBlock(CaptionPos >= 0 ? CaptionPos : 0);
    if (Merged.isValid()) {
        QTextCursor At(Document);
        At.setPosition(Merged.position());
        QTextBlockFormat Format = At.blockFormat();
        Format.clearProperty(DocTableKindProperty);
        Format.clearProperty(DocTableGroupProperty);
        Format.clearProperty(DocTableRoleProperty);
        Format.clearProperty(DocTablePayloadProperty);
        Format.clearProperty(DocTableCellProperty);
        Format.setBottomMargin(ParagraphGap(Document));
        At.setBlockFormat(Format);
        RefreshBlockVisuals(Document, Merged);
        At.movePosition(QTextCursor::EndOfBlock);
        auto UnwrapRow = [&At](xmlNodePtr RowNode) {
            for (xmlNodePtr CellNode = RowNode->children;
                 CellNode != nullptr; CellNode = CellNode->next) {
                if (CellNode->type != XML_ELEMENT_NODE) {
                    continue;
                }
                InsertParagraphBreak(At);
                InsertNodeRuns(At, CellNode);
            }
        };
        for (const char* Section : {"thead", "tbody", "tfoot"}) {
            for (xmlNodePtr SectionNode :
                 DocDocument::ChildElements(TableNode, Section)) {
                for (xmlNodePtr RowNode :
                     DocDocument::ChildElements(SectionNode, "tr")) {
                    UnwrapRow(RowNode);
                }
            }
        }
        QTextCursor Placed(Document);
        Placed.setPosition(Merged.position());
        Placed.setCharFormat(QTextCharFormat());
        _TextEdit->setTextCursor(Placed);
    }
    xmlFreeNode(TableNode);
    Cursor.endEditBlock();
    _TextEdit->setFocus();
    UpdateBlockButtons();
}

void TextEditor::ToggleArtwork()
{
    QTextDocument* Document = _TextEdit->document();
    QTextCursor Cursor = _TextEdit->textCursor();
    if (TouchesFigure(Document, Cursor)) {
        StopFigure();
    } else if (!TouchesSourceCode(Document, Cursor) &&
               !TouchesTable(Document, Cursor) &&
               ListKindsTouched(Document, Cursor).isEmpty()) {
        InsertArtwork();
    }
    UpdateBlockButtons();
    // The button keeps keyboard focus after a click; hand it
    // back to the text so typing continues there.
    _TextEdit->setFocus();
}

void TextEditor::ToggleFigure()
{
    QTextDocument* Document = _TextEdit->document();
    QTextCursor Cursor = _TextEdit->textCursor();
    if (TouchesFigure(Document, Cursor)) {
        StopFigure();
    } else if (!TouchesSourceCode(Document, Cursor) &&
               !TouchesTable(Document, Cursor) &&
               ListKindsTouched(Document, Cursor).isEmpty()) {
        InsertFigure();
    }
    UpdateBlockButtons();
    // The button keeps keyboard focus after a click; hand it
    // back to the text so typing continues there.
    _TextEdit->setFocus();
}

void TextEditor::StopFigure()
{
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextBlock Block = Cursor.block();
    if (!IsFigureBlock(Block)) {
        return;
    }
    QVector<QTextBlock> Region = FigureRegionBlocks(Block);
    QTextBlock Last = Region.last();
    Cursor.beginEditBlock();
    Cursor.setPosition(Last.position());
    Cursor.movePosition(QTextCursor::EndOfBlock);
    InsertParagraphBreak(Cursor);
    _TextEdit->setTextCursor(Cursor);
    Cursor.endEditBlock();
    UpdateBlockButtons();
}

void TextEditor::OnVersionPickerChanged(int Index)
{
    if (Index < 0) {
        return;
    }
    int Wanted = _VersionPicker->itemData(Index).toInt();
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextBlock Block = Cursor.block();
    if (!IsFigureBlock(Block)) {
        return;
    }
    xmlNodePtr FigureNode = nullptr;
    int Visible = 0;
    if (!FigurePayloadOf(Block, FigureNode, Visible)) {
        return;
    }
    QVector<xmlNodePtr> Versions = FigureVersionNodes(FigureNode);
    if (Wanted < 0 || Wanted >= Versions.size() ||
        Wanted == Visible) {
        xmlFreeNode(FigureNode);
        return;
    }
    QTextDocument* Document = _TextEdit->document();
    QVector<QTextBlock> Region = FigureRegionBlocks(Block);
    // The text showing is saved into the version it belongs to,
    // unless it is an SVG version, whose text is preserved.
    if (!ArtworkNodeIsSvg(Versions[Visible])) {
        QStringList Lines;
        for (const QTextBlock& RegionBlock : Region) {
            if (FigureRoleOf(RegionBlock) != QLatin1String("caption")) {
                Lines.append(CodeTextOf(RegionBlock));
            }
        }
        DocDocument::SetNodeText(Versions[Visible],
                                 Lines.join(QLatin1Char('\n')));
    }
    QVariant Payload = FigureToVariant(FigureNode, Wanted);
    QString Kind = FigureKindOf(Block);
    int Group = Block.blockFormat().property(DocFigureGroupProperty).toInt();
    QTextCursor Edit(Document);
    Edit.beginEditBlock();
    QTextBlock FirstArt;
    QTextBlock LastArt;
    for (const QTextBlock& RegionBlock : Region) {
        if (FigureRoleOf(RegionBlock) != QLatin1String("caption")) {
            if (!FirstArt.isValid()) {
                FirstArt = RegionBlock;
            }
            LastArt = RegionBlock;
        }
    }
    if (FirstArt.isValid()) {
        // The artwork span is replaced, leaving one block
        // carrying the region's marking.
        Edit.setPosition(FirstArt.position());
        Edit.setPosition(LastArt.position() + LastArt.length() - 1,
                         QTextCursor::KeepAnchor);
        Edit.removeSelectedText();
    } else {
        // The region lost its artwork blocks: one is made after
        // the last block of the region.
        Edit.setPosition(Region.last().position());
        Edit.movePosition(QTextCursor::EndOfBlock);
        Edit.insertBlock();
    }
    xmlNodePtr Shown = Versions[Wanted];
    if (ArtworkNodeIsSvg(Shown)) {
        SetFigureProperties(Edit, Kind, Group,
                            QStringLiteral("artwork"), Payload);
        Edit.setCharFormat(CodeCharFormat());
        Edit.insertText(SvgPlaceholderText());
    } else {
        QStringList Lines =
            DocDocument::NodeText(Shown).split(QLatin1Char('\n'));
        bool FirstLine = true;
        for (const QString& Line : Lines) {
            if (!FirstLine) {
                Edit.insertBlock();
            }
            FirstLine = false;
            SetFigureProperties(Edit, Kind, Group,
                                QStringLiteral("artwork"), Payload);
            Edit.setCharFormat(CodeCharFormat());
            Edit.insertText(Line);
        }
    }
    xmlFreeNode(FigureNode);
    // Every block of the region carries the new payload, so the
    // figure reads back with the version showing saved.
    QVector<QTextBlock> After = FigureRegionBlocks(Edit.block());
    for (const QTextBlock& RegionBlock : After) {
        QTextCursor At(Document);
        At.setPosition(RegionBlock.position());
        QTextBlockFormat Format = At.blockFormat();
        Format.setProperty(DocFigurePayloadProperty, Payload);
        At.setBlockFormat(Format);
    }
    Edit.endEditBlock();
    // The cursor goes to the first artwork line of the version
    // now showing.
    for (const QTextBlock& RegionBlock : After) {
        if (FigureRoleOf(RegionBlock) != QLatin1String("caption")) {
            Cursor.setPosition(RegionBlock.position());
            break;
        }
    }
    _TextEdit->setTextCursor(Cursor);
    UpdateBlockButtons();
    _TextEdit->setFocus();
}

void TextEditor::WrapSelection(const QString& Name)
{
    QTextCursor Cursor = _TextEdit->textCursor();
    if (!Cursor.hasSelection()) {
        return;
    }
    DocTagEntry Entry;
    if (!PromptForTag(Name, Entry, Cursor.selectedText())) {
        return;
    }
    int End = Cursor.selectionEnd();
    ChangeStacks(Cursor.selectionStart(), Cursor.selectionEnd(), Name, &Entry);
    // A wrapped link leaves the caret holding the link's format,
    // so typing after it would join the link: put the caret just
    // after it with the link stripped. Other tags keep the
    // caret's format as it was.
    if (Name == QStringLiteral("xref") || Name == QStringLiteral("eref") ||
        Name == QStringLiteral("relref") || Name == QStringLiteral("cref")) {
        PlaceCaretAfterTag(_TextEdit, End, Name);
    }
}

void TextEditor::ToggleTag(const QString& Name, bool Active)
{
    QTextCursor Cursor = _TextEdit->textCursor();
    DocTagStack Stack = StackOf(Cursor.charFormat());
    DocTagStack Changed;
    if (Active) {
        Changed = StackWithoutLast(Stack, Name);
    } else {
        DocTagEntry Entry;
        if (!PromptForTag(Name, Entry)) {
            return;
        }
        Changed = StackWithTag(Stack, Entry);
    }
    QTextCharFormat Format;
    Format.setProperty(DocStackProperty, StackToVariant(Changed));
    ApplyStackVisuals(Format, Changed);
    if (IsHeaderCellBlock(Cursor.block())) {
        // Header cells show bold whatever their stacks say.
        Format.setFontWeight(QFont::Bold);
    }
    _TextEdit->setCurrentCharFormat(Format);
}

void TextEditor::RemoveTag(const QString& Name)
{
    QTextCursor Cursor = _TextEdit->textCursor();
    if (Cursor.hasSelection()) {
        ChangeStacks(Cursor.selectionStart(), Cursor.selectionEnd(), Name, nullptr);
        return;
    }
    // No selection: the run of text around the cursor holding the
    // tag, in either direction, within the paragraph.
    QTextDocument* Document = _TextEdit->document();
    int Pos = Cursor.position();
    int Start = Pos;
    while (Start > 0 && StackHolds(StackAtPosition(Document, Start - 1), Name)) {
        --Start;
    }
    int End = Pos;
    while (StackHolds(StackAtPosition(Document, End), Name)) {
        ++End;
    }
    if (Start < End) {
        ChangeStacks(Start, End, Name, nullptr);
    }
}

void TextEditor::RemoveMarker()
{
    QTextCursor Cursor = _TextEdit->textCursor();
    QTextDocument* Document = _TextEdit->document();
    QVector<QPair<int, int>> Ranges;
    if (Cursor.hasSelection()) {
        int Start = Cursor.selectionStart();
        int End = Cursor.selectionEnd();
        QTextBlock Block = Document->findBlock(Start);
        while (Block.isValid() && Block.position() < End) {
            for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
                QTextFragment Fragment = It.fragment();
                int FragStart = Fragment.position();
                int FragEnd = FragStart + Fragment.length();
                if (FragEnd <= Start || FragStart >= End) {
                    continue;
                }
                if (Fragment.charFormat().property(DocMarkerProperty).isValid()) {
                    int From = FragStart > Start ? FragStart : Start;
                    int To = FragEnd < End ? FragEnd : End;
                    Ranges.append(QPair<int, int>(From, To));
                }
            }
            Block = Block.next();
        }
    } else {
        int Pos = Cursor.position();
        QVariant Value = MarkerAtPosition(Document, Pos);
        int At = Pos;
        if (!Value.isValid()) {
            Value = MarkerAtPosition(Document, Pos - 1);
            At = Pos - 1;
        }
        if (Value.isValid()) {
            int Start = At;
            while (Start > 0 && MarkerAtPosition(Document, Start - 1) == Value) {
                --Start;
            }
            int End = At + 1;
            while (MarkerAtPosition(Document, End) == Value) {
                ++End;
            }
            Ranges.append(QPair<int, int>(Start, End));
        }
    }
    if (Ranges.isEmpty()) {
        return;
    }
    QTextCursor Guard(Document);
    Guard.beginEditBlock();
    for (int i = Ranges.size() - 1; i >= 0; --i) {
        QTextCursor Delete(Document);
        Delete.setPosition(Ranges[i].first);
        Delete.setPosition(Ranges[i].second, QTextCursor::KeepAnchor);
        Delete.removeSelectedText();
    }
    Guard.endEditBlock();
}

void TextEditor::ChangeStacks(int Start, int End, const QString& Name,
                              const DocTagEntry* Entry)
{
    QTextDocument* Document = _TextEdit->document();

    // Snapshot the fragments first: changing formats splits
    // fragments, which would disturb a live walk.
    struct FragmentChange
    {
        int From;
        int To;
        QTextCharFormat Format;
    };
    QVector<FragmentChange> Changes;
    QTextBlock Block = Document->findBlock(Start);
    while (Block.isValid() && Block.position() < End) {
        for (QTextBlock::iterator It = Block.begin(); !It.atEnd(); ++It) {
            QTextFragment Fragment = It.fragment();
            int FragStart = Fragment.position();
            int FragEnd = FragStart + Fragment.length();
            int From = FragStart > Start ? FragStart : Start;
            int To = FragEnd < End ? FragEnd : End;
            if (From >= To) {
                continue;
            }
            if (Fragment.charFormat()
                    .property(DocOptionMarkerProperty)
                    .isValid()) {
                // An option marker is display only: no tag is
                // ever applied to it, and its format stays.
                continue;
            }
            DocTagStack Stack = StackOf(Fragment.charFormat());
            DocTagStack Changed;
            if (Entry != nullptr) {
                // Never nest the same tag, and never nest where
                // the schema forbids it: a selection can span
                // fragments whose stacks differ from the one the
                // menu was built from. A fragment in a cell
                // nests under its cell, not under a paragraph,
                // so the cell rules keep contact, list, spanx,
                // and vspace out of cells here too.
                QString Parent;
                if (!Stack.isEmpty()) {
                    Parent = Stack.last().Name;
                } else if (IsTableCellBlock(Block)) {
                    Parent = IsHeaderCellBlock(Block)
                                 ? QStringLiteral("th")
                                 : QStringLiteral("td");
                } else {
                    Parent = QStringLiteral("t");
                }
                if (StackHolds(Stack, Name) || !CanContain(Parent, Name)) {
                    continue;
                }
                Changed = StackWithTag(Stack, *Entry);
            } else {
                if (!StackHolds(Stack, Name)) {
                    continue;
                }
                Changed = StackWithoutTag(Stack, Name);
            }
            QTextCharFormat Format = Fragment.charFormat();
            Format.setProperty(DocStackProperty, StackToVariant(Changed));
            ApplyStackVisuals(Format, Changed);
            if (IsHeaderCellBlock(Block)) {
                // Header cells show bold whatever their stacks
                // say: the bold is the cell's, not a tag's.
                Format.setFontWeight(QFont::Bold);
            }
            QVariant MarkerValue = Format.property(DocMarkerProperty);
            if (MarkerValue.isValid()) {
                ApplyMarkerVisuals(Format, EntryFromVariant(MarkerValue));
            }
            FragmentChange Change;
            Change.From = From;
            Change.To = To;
            Change.Format = Format;
            Changes.append(Change);
        }
        Block = Block.next();
    }

    QTextCursor Guard(Document);
    Guard.beginEditBlock();
    for (const FragmentChange& Change : Changes) {
        QTextCursor FragCursor(Document);
        FragCursor.setPosition(Change.From);
        FragCursor.setPosition(Change.To, QTextCursor::KeepAnchor);
        FragCursor.setCharFormat(Change.Format);
    }
    Guard.endEditBlock();
}
