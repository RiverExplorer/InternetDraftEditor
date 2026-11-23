/**
 * @file DocModel.cpp
 * @brief Implementation of the draft document model: the draft as a
 *        libxml2 tree.
 *
 * @note COMPILE-UNTESTED: this file was written on a machine
 *       without Qt or the libxml2 headers, and has not been
 *       compiled. The libxml2 symbols used are the stable core
 *       API, verified against the libxml2 2.15.4 headers in
 *       Doug's RelatedProjects/include, which are not on this VM.
 *       Three companions of the listed core functions are used
 *       because the listed functions require them: xmlFree frees
 *       the strings xmlNodeGetContent and xmlGetProp return,
 *       xmlBufferCreate and xmlBufferFree create and free the
 *       xmlBuffer the serializer writes into, xmlBufferAdd
 *       appends to it, xmlDocGetRootElement reads the root
 *       element, xmlNewDoc creates a document, xmlDocSetRootElement
 *       sets its root, xmlNewText creates text nodes, and
 *       xmlNewComment creates comment nodes. No other libxml2
 *       symbol outside the design's core list is used.
 */

#include "DocModel.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDate>
#include <QFile>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <functional>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlerror.h>
#include <libxml/xmlstring.h>

/**
 * @brief Longest literal line xml2rfc renders without warning
 *        L106 (Bug 458): the 72-character render width less the
 *        3-character indent xml2rfc applies to literal content.
 */
const int MaxLiteralLineLength = 69;

/**
 * @brief The text of an xmlChar string, or empty when null.
 * @param Text The libxml2 string.
 * @return The text as a QString.
 */
static QString TextFromXmlChars(const xmlChar* Text)
{
    if (Text == nullptr) {
        return QString();
    }
    return QString::fromUtf8(reinterpret_cast<const char*>(Text));
}

/**
 * @brief A QString as UTF-8 bytes for a libxml2 call.
 * @param Text The text.
 * @return The UTF-8 bytes.
 */
static QByteArray Utf8Bytes(const QString& Text)
{
    return Text.toUtf8();
}

/**
 * @brief True when a node is an element node.
 * @param Node The node.
 * @return True for an element.
 */
static bool IsElementKind(xmlNodePtr Node)
{
    return Node != nullptr && Node->type == XML_ELEMENT_NODE;
}

/**
 * @brief True when a node is a text node.
 * @param Node The node.
 * @return True for a text node.
 */
static bool IsTextKind(xmlNodePtr Node)
{
    return Node != nullptr && Node->type == XML_TEXT_NODE;
}

/**
 * @brief True when a node is a CDATA section node.
 * @param Node The node.
 * @return True for a CDATA section.
 */
static bool IsCdataKind(xmlNodePtr Node)
{
    return Node != nullptr && Node->type == XML_CDATA_SECTION_NODE;
}

/**
 * @brief True when a node is a comment node.
 * @param Node The node.
 * @return True for a comment.
 */
static bool IsCommentKind(xmlNodePtr Node)
{
    return Node != nullptr && Node->type == XML_COMMENT_NODE;
}

/**
 * @brief True when a node is a processing instruction node.
 * @param Node The node.
 * @return True for a processing instruction.
 */
static bool IsPiKind(xmlNodePtr Node)
{
    return Node != nullptr && Node->type == XML_PI_NODE;
}

/**
 * @brief True when a node is an entity reference node.
 * @param Node The node.
 * @return True for an entity reference.
 */
static bool IsEntityRefKind(xmlNodePtr Node)
{
    return Node != nullptr && Node->type == XML_ENTITY_REF_NODE;
}

/**
 * @brief True when an element carries no namespace declarations.
 * @param Node The element.
 * @return True when the element declares no namespaces.
 */
static bool ElementHasNoNsDecls(xmlNodePtr Node)
{
    return Node == nullptr || Node->nsDef == nullptr;
}

/**
 * @brief The child nodes of a parent, in order.
 * @param Parent The parent.
 * @return The children, borrowed.
 */
static QVector<xmlNodePtr> CollectChildren(xmlNodePtr Parent)
{
    QVector<xmlNodePtr> Children;
    if (Parent == nullptr) {
        return Children;
    }
    for (xmlNodePtr Child = Parent->children; Child != nullptr;
         Child = Child->next) {
        Children.append(Child);
    }
    return Children;
}

/**
 * @brief Unlinks a node from its tree and frees it.
 * @param Node The node; null does nothing.
 */
static void UnlinkAndFreeNode(xmlNodePtr Node)
{
    if (Node == nullptr) {
        return;
    }
    xmlUnlinkNode(Node);
    xmlFreeNode(Node);
}

/**
 * @brief Unlinks and frees every child of a node.
 * @param Parent The parent.
 */
static void RemoveAllChildren(xmlNodePtr Parent)
{
    for (xmlNodePtr Child : CollectChildren(Parent)) {
        UnlinkAndFreeNode(Child);
    }
}

/**
 * @brief Tests whether a name is one of the 17 inline elements a <t> holds.
 *
 * The set comes from the <t> content model in v3.rng: bcp14, br,
 * contact, cref, em, eref, iref, list, relref, spanx, strong, sub,
 * sup, tt, u, vspace, xref.
 * @param Name The element name.
 * @return True when the name is one of the 17.
 */
static bool IsInlineElementName(const QString& Name)
{
    static const char* Names[] = {
        "bcp14", "br", "contact", "cref", "em", "eref", "iref", "list",
        "relref", "spanx", "strong", "sub", "sup", "tt", "u", "vspace", "xref"
    };
    for (const char* Known : Names) {
        if (Name == QLatin1String(Known)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Tests whether a name is one of the 13 inline elements a
 *        table cell may hold.
 *
 * A cell holds text plus bcp14, br, cref, em, eref, iref,
 * relref, strong, sub, sup, tt, u, and xref. Contact, list,
 * spanx, and vspace are not cell content: a cell holding any
 * of them keeps the whole table preserved.
 * @param Name The element name.
 * @return True for the 13 cell inline names.
 */
static bool IsCellInlineName(const QString& Name)
{
    static const char* Names[] = {
        "bcp14", "br", "cref", "em", "eref", "iref", "relref",
        "strong", "sub", "sup", "tt", "u", "xref"
    };
    for (const char* Known : Names) {
        if (Name == QLatin1String(Known)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Tests whether a name is a section-level list element.
 * @param Name The element name.
 * @return True for ul, ol, and dl.
 */
static bool IsBlockListName(const QString& Name)
{
    return Name == QLatin1String("ul") || Name == QLatin1String("ol") ||
           Name == QLatin1String("dl");
}

/**
 * @brief Tests whether a name is any list element.
 * @param Name The element name.
 * @return True for the inline list element and the section-level ones.
 */
static bool IsListElementName(const QString& Name)
{
    return Name == QLatin1String("list") || IsBlockListName(Name);
}

/**
 * @brief Tests whether a name is one of the nine structured postal parts.
 * @param Name The element name.
 * @return True for street, cityarea, extaddr, pobox, city, region,
 *         code, country, and sortingcode.
 */
static bool IsPostalPartName(const QString& Name)
{
    static const char* Names[] = {
        "street", "cityarea", "extaddr", "pobox", "city", "region",
        "code", "country", "sortingcode"
    };
    for (const char* Known : Names) {
        if (Name == QLatin1String(Known)) {
            return true;
        }
    }
    return false;
}

// The acceptance predicates call each other: a list's items hold
// <t> elements, and a <t> may hold an inline list.
static bool TElementConverts(xmlNodePtr Node);
static bool InlineChildrenConvert(xmlNodePtr Node);
static bool RunsHoldListNode(xmlNodePtr Node);
static bool ItemParagraphsConvert(xmlNodePtr Node, bool ForTerm,
                                  int& ParagraphCount);

/**
 * @brief Tests whether any descendant element, at any depth, is a list element.
 * @param Node The node to search.
 * @return True when a list element is found below the node.
 */
static bool RunsHoldListNode(xmlNodePtr Node)
{
    if (Node == nullptr) {
        return false;
    }
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if (IsElementKind(Child)) {
            if (IsListElementName(TextFromXmlChars(Child->name))) {
                return true;
            }
            if (RunsHoldListNode(Child)) {
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief Tests whether any descendant element holds content a
 *        definition term may not hold.
 *
 * A term holds a smaller inline set than a paragraph: no contact,
 * list, spanx, u, or vspace, and no organization or address run.
 * The check is recursive, so forbidden content nested inside an
 * allowed tag is caught too.
 * @param Node The node to search.
 * @return True when forbidden content is found.
 */
static bool RunsHoldTermForbiddenNode(xmlNodePtr Node)
{
    if (Node == nullptr) {
        return false;
    }
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if (IsElementKind(Child)) {
            QString Name = TextFromXmlChars(Child->name);
            if (Name == QLatin1String("contact") ||
                Name == QLatin1String("list") ||
                Name == QLatin1String("spanx") ||
                Name == QLatin1String("u") ||
                Name == QLatin1String("vspace") ||
                Name == QLatin1String("organization") ||
                Name == QLatin1String("address")) {
                return true;
            }
            if (RunsHoldTermForbiddenNode(Child)) {
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief Tests whether the children of an element convert to runs.
 *
 * Text and CDATA children convert as text. Element children
 * holding one of the 17 inline names, or organization/address
 * inside a contact, convert recursively. A list element converts
 * when its items are <t> elements that convert. A comment child
 * converts as a comment run. Processing instructions, entity
 * references, namespace declarations, and any other element name
 * cannot be structured. The empty elements br, vspace, and iref
 * must hold nothing at all. A spanx child is accepted as it
 * stands; the tree keeps it, and no conversion is done here.
 * @param Node The element whose children are tested.
 * @return False when the children cannot be structured.
 */
static bool InlineChildrenConvert(xmlNodePtr Node)
{
    if (Node == nullptr) {
        return false;
    }
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if (IsTextKind(Child) || IsCdataKind(Child) ||
            IsCommentKind(Child)) {
            continue;
        }
        if (!IsElementKind(Child)) {
            return false;
        }
        if (!ElementHasNoNsDecls(Child)) {
            return false;
        }
        QString Name = TextFromXmlChars(Child->name);
        if (Name == QLatin1String("list")) {
            for (xmlNodePtr Item = Child->children; Item != nullptr;
                 Item = Item->next) {
                if ((IsTextKind(Item) || IsCdataKind(Item)) &&
                    DocDocument::IsWhitespaceText(Item)) {
                    continue;
                }
                if (!IsElementKind(Item) ||
                    !DocDocument::NodeNameEquals(Item, "t")) {
                    return false;
                }
                if (!TElementConverts(Item) || RunsHoldListNode(Item)) {
                    return false;
                }
            }
            continue;
        }
        if (IsInlineElementName(Name) ||
            Name == QLatin1String("organization") ||
            Name == QLatin1String("address")) {
            // The empty elements hold nothing: any child at all
            // means the paragraph is kept whole instead.
            if ((Name == QLatin1String("br") ||
                 Name == QLatin1String("vspace") ||
                 Name == QLatin1String("iref")) &&
                Child->children != nullptr) {
                return false;
            }
            if (!InlineChildrenConvert(Child)) {
                return false;
            }
            continue;
        }
        return false;
    }
    return true;
}

/**
 * @brief Tests whether a <t> element converts to a structured paragraph.
 *
 * The <t> must carry no attributes or namespace declarations: the
 * editor does not edit them, so an attributed paragraph is kept
 * whole as a preserved block, as before. A list stands alone as
 * the whole paragraph: a paragraph mixing a list with other
 * content, or nesting a list inside an element or another list,
 * is kept whole instead. Whitespace-only text around a lone list
 * is layout, not content.
 * @param Node The <t> element.
 * @return False when the paragraph must be kept whole instead.
 */
static bool TElementConverts(xmlNodePtr Node)
{
    if (!IsElementKind(Node) ||
        !DocDocument::NodeNameEquals(Node, "t") ||
        Node->properties != nullptr || !ElementHasNoNsDecls(Node)) {
        return false;
    }
    if (!InlineChildrenConvert(Node)) {
        return false;
    }
    int ListChildren = 0;
    int SignificantChildren = 0;
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if (IsElementKind(Child) &&
            DocDocument::NodeNameEquals(Child, "list")) {
            ++ListChildren;
        }
        if (IsElementKind(Child) || IsCommentKind(Child) ||
            ((IsTextKind(Child) || IsCdataKind(Child)) &&
             !DocDocument::IsWhitespaceText(Child))) {
            ++SignificantChildren;
        }
    }
    if (ListChildren > 0) {
        return ListChildren == 1 && SignificantChildren == 1;
    }
    return !RunsHoldListNode(Node);
}

/**
 * @brief True when a <t> element holds no text and no elements.
 *
 * A <t> holding only whitespace text is blank; any element or
 * comment child makes it non-blank.
 * @param Node The <t> element.
 * @return True when the element is blank.
 */
static bool TElementIsBlank(xmlNodePtr Node)
{
    if (Node == nullptr) {
        return true;
    }
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if (IsElementKind(Child) || IsCommentKind(Child)) {
            return false;
        }
        if ((IsTextKind(Child) || IsCdataKind(Child)) &&
            !DocDocument::IsWhitespaceText(Child)) {
            return false;
        }
        if (IsPiKind(Child) || IsEntityRefKind(Child)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief True when a <t> element holds only comments, plus blank text.
 *
 * At least one comment child is required, so an empty or blank
 * <t> never counts.
 * @param Node The <t> element.
 * @return True when the element holds only comments.
 */
static bool CommentsOnlyTElement(xmlNodePtr Node)
{
    if (Node == nullptr) {
        return false;
    }
    bool HasComment = false;
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if (IsCommentKind(Child)) {
            HasComment = true;
            continue;
        }
        if ((IsTextKind(Child) || IsCdataKind(Child)) &&
            DocDocument::IsWhitespaceText(Child)) {
            continue;
        }
        return false;
    }
    return HasComment;
}

/**
 * @brief Tests whether a list item (li, dt, or dd) converts to paragraphs.
 *
 * The item must carry no attributes or namespace declarations:
 * the editor does not edit them, so an attributed item keeps the
 * whole list preserved. An item holding only <t> elements
 * converts to one paragraph per <t> (a term never holds a <t>:
 * the schema allows a term inline content only). Otherwise the
 * item's children convert as inline runs in a single paragraph.
 * Comments, processing instructions, and entity references
 * directly inside an item keep the whole list preserved.
 * @param Node The item element.
 * @param ForTerm True when the item is a definition term, whose
 *        inline set is smaller than a paragraph's.
 * @param ParagraphCount Filled with the paragraph count, on success.
 * @return False when the item cannot be structured.
 */
static bool ItemParagraphsConvert(xmlNodePtr Node, bool ForTerm,
                                  int& ParagraphCount)
{
    ParagraphCount = 0;
    if (!IsElementKind(Node) || Node->properties != nullptr ||
        !ElementHasNoNsDecls(Node)) {
        return false;
    }
    bool HasT = false;
    bool HasOtherElement = false;
    bool Mixed = false;
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if (IsElementKind(Child)) {
            if (DocDocument::NodeNameEquals(Child, "t")) {
                HasT = true;
            } else {
                HasOtherElement = true;
            }
        } else if (IsTextKind(Child) || IsCdataKind(Child)) {
            if (!DocDocument::IsWhitespaceText(Child)) {
                Mixed = true;
            }
        } else {
            return false;
        }
    }
    if (HasT) {
        // <t> children mixed with text or other elements match
        // neither side of the schema's choice: keep preserved.
        if (ForTerm || Mixed || HasOtherElement) {
            return false;
        }
        for (xmlNodePtr Child = Node->children; Child != nullptr;
             Child = Child->next) {
            if (!IsElementKind(Child)) {
                continue;
            }
            if (!TElementConverts(Child) || RunsHoldListNode(Child)) {
                return false;
            }
            ++ParagraphCount;
        }
        return ParagraphCount > 0;
    }
    if (!InlineChildrenConvert(Node)) {
        return false;
    }
    if (RunsHoldListNode(Node)) {
        return false;
    }
    if (ForTerm && RunsHoldTermForbiddenNode(Node)) {
        return false;
    }
    ParagraphCount = 1;
    return true;
}

/**
 * @brief True when a list item element holds no content.
 *
 * An item holding <t> children is blank when every <t> is blank;
 * an item holding inline content is blank when every child is
 * whitespace text.
 * @param Node The item element.
 * @return True when the item is blank.
 */
static bool ItemNodeIsBlank(xmlNodePtr Node)
{
    if (Node == nullptr) {
        return true;
    }
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if (IsElementKind(Child) &&
            DocDocument::NodeNameEquals(Child, "t")) {
            if (!TElementIsBlank(Child)) {
                return false;
            }
        } else if (IsElementKind(Child) || IsCommentKind(Child)) {
            return false;
        } else if ((IsTextKind(Child) || IsCdataKind(Child)) &&
                   !DocDocument::IsWhitespaceText(Child)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Tests whether a <ul>, <ol>, or <dl> element converts to a list.
 *
 * The list element itself may carry attributes: they are kept on
 * the node and written back, since the editor does not edit list
 * options yet. A bulleted or numbered list holds li items; a
 * definition list holds dt/dd pairs. An empty list, a list
 * carrying namespace declarations, a definition term without its
 * definition, and any item the reader cannot structure keep the
 * whole list preserved instead. Comments and processing
 * instructions between items do not block conversion: they stay
 * as children of the list node.
 * @param Node The list element.
 * @return False when the list must be kept whole instead.
 */
static bool ListElementConverts(xmlNodePtr Node)
{
    if (!IsElementKind(Node) || !ElementHasNoNsDecls(Node)) {
        return false;
    }
    QString Name = TextFromXmlChars(Node->name);
    if (!IsBlockListName(Name)) {
        return false;
    }
    QVector<xmlNodePtr> Children = CollectChildren(Node);
    bool HasContent = false;
    if (Name == QLatin1String("dl")) {
        int At = 0;
        int Pairs = 0;
        while (At < Children.size()) {
            xmlNodePtr Child = Children.at(At);
            if ((IsTextKind(Child) || IsCdataKind(Child)) &&
                DocDocument::IsWhitespaceText(Child)) {
                ++At;
                continue;
            }
            if (IsCommentKind(Child) || IsPiKind(Child)) {
                ++At;
                continue;
            }
            if (!IsElementKind(Child) ||
                !DocDocument::NodeNameEquals(Child, "dt")) {
                return false;
            }
            int TermCount = 0;
            if (!ItemParagraphsConvert(Child, true, TermCount) ||
                TermCount != 1) {
                return false;
            }
            bool TermBlank = ItemNodeIsBlank(Child);
            ++At;
            while (At < Children.size() &&
                   ((IsTextKind(Children.at(At)) ||
                     IsCdataKind(Children.at(At))) &&
                    DocDocument::IsWhitespaceText(Children.at(At)))) {
                ++At;
            }
            while (At < Children.size() &&
                   (IsCommentKind(Children.at(At)) ||
                    IsPiKind(Children.at(At)))) {
                ++At;
                while (At < Children.size() &&
                       ((IsTextKind(Children.at(At)) ||
                         IsCdataKind(Children.at(At))) &&
                        DocDocument::IsWhitespaceText(Children.at(At)))) {
                    ++At;
                }
            }
            if (At >= Children.size()) {
                return false;
            }
            xmlNodePtr Definition = Children.at(At);
            if (!IsElementKind(Definition) ||
                !DocDocument::NodeNameEquals(Definition, "dd")) {
                return false;
            }
            int DefinedCount = 0;
            if (!ItemParagraphsConvert(Definition, false, DefinedCount) ||
                DefinedCount < 1) {
                return false;
            }
            if (!TermBlank || !ItemNodeIsBlank(Definition)) {
                HasContent = true;
            }
            ++Pairs;
            ++At;
        }
        return Pairs > 0 && HasContent;
    }
    for (xmlNodePtr Child : Children) {
        if ((IsTextKind(Child) || IsCdataKind(Child)) &&
            DocDocument::IsWhitespaceText(Child)) {
            continue;
        }
        if (IsCommentKind(Child) || IsPiKind(Child)) {
            continue;
        }
        if (!IsElementKind(Child) ||
            !DocDocument::NodeNameEquals(Child, "li")) {
            return false;
        }
        int ItemCount = 0;
        if (!ItemParagraphsConvert(Child, false, ItemCount) ||
            ItemCount < 1) {
            return false;
        }
        if (!ItemNodeIsBlank(Child)) {
            HasContent = true;
        }
    }
    return HasContent;
}

/**
 * @brief Tests whether a <name> element converts to caption runs.
 *
 * The caption holds text plus cref, eref, relref, tt, and xref,
 * the inline set the schema allows in a <name>; any other run
 * keeps the whole figure preserved. Attributes and namespace
 * declarations on the <name> keep it preserved too, as for <t>.
 * @param Node The <name> element.
 * @return False when the caption cannot be structured.
 */
static bool CaptionRunsConvert(xmlNodePtr Node)
{
    if (!IsElementKind(Node) ||
        !DocDocument::NodeNameEquals(Node, "name") ||
        Node->properties != nullptr || !ElementHasNoNsDecls(Node)) {
        return false;
    }
    if (!InlineChildrenConvert(Node)) {
        return false;
    }
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if (IsTextKind(Child) || IsCdataKind(Child)) {
            continue;
        }
        if (!IsElementKind(Child)) {
            return false;
        }
        QString RunName = TextFromXmlChars(Child->name);
        if (RunName == QLatin1String("cref") ||
            RunName == QLatin1String("eref") ||
            RunName == QLatin1String("relref") ||
            RunName == QLatin1String("tt") ||
            RunName == QLatin1String("xref")) {
            continue;
        }
        return false;
    }
    return true;
}

/**
 * @brief Tests whether an <artwork> element converts to one version.
 *
 * An artwork holding an element (an <svg>, in practice) converts
 * as an SVG version: SVG is never edited, only preserved,
 * comments inside included. An artwork holding only text converts
 * as a text version, its text taken exactly as read, line breaks
 * and spacing included, because artwork is literal. A src
 * attribute, a namespace declaration, or an entity reference
 * keeps the whole figure preserved.
 * @param Node The <artwork> element.
 * @return False when the artwork must be kept whole instead.
 */
static bool ArtworkVersionConverts(xmlNodePtr Node)
{
    if (!IsElementKind(Node) ||
        !DocDocument::NodeNameEquals(Node, "artwork") ||
        !ElementHasNoNsDecls(Node)) {
        return false;
    }
    if (DocDocument::HasNodeAttribute(Node, "src")) {
        return false;
    }
    bool HasElement = false;
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if (IsElementKind(Child)) {
            HasElement = true;
        } else if (IsEntityRefKind(Child)) {
            return false;
        } else if (!IsTextKind(Child) && !IsCdataKind(Child) &&
                   !IsCommentKind(Child) && !IsPiKind(Child)) {
            return false;
        }
    }
    if (HasElement) {
        return true;
    }
    return true;
}

/**
 * @brief Tests whether an <artset> element converts to versions.
 *
 * The artset holds artwork elements; whitespace between them is
 * layout, and comments and processing instructions between them
 * are kept as children. An empty artset, a namespace
 * declaration, and anything else keep the whole figure
 * preserved instead.
 * @param Node The <artset> element.
 * @return False when the artset must be kept whole instead.
 */
static bool ArtsetConverts(xmlNodePtr Node)
{
    if (!IsElementKind(Node) ||
        !DocDocument::NodeNameEquals(Node, "artset") ||
        !ElementHasNoNsDecls(Node)) {
        return false;
    }
    int Versions = 0;
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if ((IsTextKind(Child) || IsCdataKind(Child)) &&
            DocDocument::IsWhitespaceText(Child)) {
            continue;
        }
        if (IsCommentKind(Child) || IsPiKind(Child)) {
            continue;
        }
        if (!IsElementKind(Child) ||
            !DocDocument::NodeNameEquals(Child, "artwork") ||
            !ArtworkVersionConverts(Child)) {
            return false;
        }
        ++Versions;
    }
    return Versions > 0;
}

/**
 * @brief Tests whether a <figure>, <artset>, or <artwork> element converts.
 *
 * A figure holds an optional caption, then either one artset or
 * one or more artwork elements; mixing an artset with artwork,
 * a second artset, a caption after the artwork, and a figure
 * with no artwork keep the whole element preserved. Source
 * code, a preamble or postamble, an index entry, a namespace
 * declaration, an entity reference, and any other child keep
 * it preserved too, as does a caption holding runs outside the
 * <name> set.
 * @param Node The element.
 * @return False when the element must be kept whole instead.
 */
static bool FigureElementConverts(xmlNodePtr Node)
{
    if (!IsElementKind(Node) || !ElementHasNoNsDecls(Node)) {
        return false;
    }
    QString Name = TextFromXmlChars(Node->name);
    if (Name == QLatin1String("artwork")) {
        return ArtworkVersionConverts(Node);
    }
    if (Name == QLatin1String("artset")) {
        return ArtsetConverts(Node);
    }
    if (Name != QLatin1String("figure")) {
        return false;
    }
    bool SawName = false;
    bool SawContent = false;
    bool HasArtset = false;
    int Versions = 0;
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if ((IsTextKind(Child) || IsCdataKind(Child)) &&
            DocDocument::IsWhitespaceText(Child)) {
            continue;
        }
        if (IsCommentKind(Child) || IsPiKind(Child)) {
            continue;
        }
        if (!IsElementKind(Child)) {
            return false;
        }
        QString ChildName = TextFromXmlChars(Child->name);
        if (ChildName == QLatin1String("name")) {
            if (SawName || SawContent) {
                return false;
            }
            SawName = true;
            if (!CaptionRunsConvert(Child)) {
                return false;
            }
            continue;
        }
        if (ChildName == QLatin1String("artset")) {
            if (HasArtset || SawContent) {
                return false;
            }
            HasArtset = true;
            SawContent = true;
            if (!ArtsetConverts(Child)) {
                return false;
            }
            for (xmlNodePtr Version = Child->children; Version != nullptr;
                 Version = Version->next) {
                if (IsElementKind(Version) &&
                    DocDocument::NodeNameEquals(Version, "artwork")) {
                    ++Versions;
                }
            }
            continue;
        }
        if (ChildName == QLatin1String("artwork")) {
            if (HasArtset) {
                return false;
            }
            SawContent = true;
            if (!ArtworkVersionConverts(Child)) {
                return false;
            }
            ++Versions;
            continue;
        }
        return false;
    }
    return Versions > 0;
}

/**
 * @brief Tests whether the runs under a node hold only cell inline elements.
 *
 * The check recurses through element children, so a nested
 * element outside the cell set rejects the runs too. Comment
 * runs reject them: a comment inside a cell keeps the whole
 * table preserved. List runs reject them: a list is not cell
 * content in this phase.
 * @param Node The node to test.
 * @return True when every run is text or a cell inline element.
 */
static bool CellRunsAreInlineOnlyNode(xmlNodePtr Node)
{
    if (Node == nullptr) {
        return false;
    }
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if (IsTextKind(Child) || IsCdataKind(Child)) {
            continue;
        }
        if (!IsElementKind(Child) ||
            !IsCellInlineName(TextFromXmlChars(Child->name))) {
            return false;
        }
        if (!CellRunsAreInlineOnlyNode(Child)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Tests whether a <td> or <th> element converts to one table cell.
 *
 * A cell holds inline runs only in this phase: block content is
 * rejected, and content outside the 13-element cell set is
 * rejected by the inline-only check. Comments and processing
 * instructions inside a cell keep the whole table preserved, as
 * do namespace declarations and entity references. The span
 * attributes are modeled as counts, and a span that is not a
 * positive integer keeps the whole table preserved.
 * @param Node The cell element.
 * @return False when the cell must be kept whole instead.
 */
static bool TableCellConverts(xmlNodePtr Node)
{
    if (!IsElementKind(Node) || !ElementHasNoNsDecls(Node)) {
        return false;
    }
    QString Name = TextFromXmlChars(Node->name);
    if (Name != QLatin1String("td") && Name != QLatin1String("th")) {
        return false;
    }
    for (xmlAttrPtr Attribute = Node->properties; Attribute != nullptr;
         Attribute = Attribute->next) {
        QString AttributeName = TextFromXmlChars(Attribute->name);
        if (AttributeName == QLatin1String("colspan") ||
            AttributeName == QLatin1String("rowspan")) {
            QByteArray AttributeUtf8 = Utf8Bytes(AttributeName);
            QString Value = DocDocument::NodeAttribute(
                Node, AttributeUtf8.constData());
            bool Ok = false;
            int Span = Value.toInt(&Ok);
            if (!Ok || Span < 1) {
                return false;
            }
        }
    }
    if (!InlineChildrenConvert(Node)) {
        return false;
    }
    return CellRunsAreInlineOnlyNode(Node);
}

/**
 * @brief Tests whether a <tr> element converts to one table row.
 *
 * A row holds one or more <td> or <th> cells, mixed. Whitespace
 * between the cells is layout. Comments and processing
 * instructions between cells keep the whole table preserved.
 * @param Node The row element.
 * @return False when the row must be kept whole instead.
 */
static bool TableRowConverts(xmlNodePtr Node)
{
    if (!IsElementKind(Node) ||
        !DocDocument::NodeNameEquals(Node, "tr") ||
        !ElementHasNoNsDecls(Node)) {
        return false;
    }
    int Cells = 0;
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if ((IsTextKind(Child) || IsCdataKind(Child)) &&
            DocDocument::IsWhitespaceText(Child)) {
            continue;
        }
        if (!IsElementKind(Child) || !TableCellConverts(Child)) {
            return false;
        }
        ++Cells;
    }
    return Cells > 0;
}

/**
 * @brief Tests whether a <thead>, <tbody>, or <tfoot> element converts.
 * @param Node The section element.
 * @param Expected The section element name.
 * @return False when the section must be kept whole instead.
 */
static bool TableSectionConverts(xmlNodePtr Node, const char* Expected)
{
    if (!IsElementKind(Node) ||
        !DocDocument::NodeNameEquals(Node, Expected) ||
        !ElementHasNoNsDecls(Node)) {
        return false;
    }
    int Rows = 0;
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if ((IsTextKind(Child) || IsCdataKind(Child)) &&
            DocDocument::IsWhitespaceText(Child)) {
            continue;
        }
        if (IsCommentKind(Child) || IsPiKind(Child)) {
            continue;
        }
        if (!IsElementKind(Child) ||
            !DocDocument::NodeNameEquals(Child, "tr") ||
            !TableRowConverts(Child)) {
            return false;
        }
        ++Rows;
    }
    return Rows > 0;
}

/**
 * @brief Reads the span of one table cell: colspan or rowspan, 1 when absent.
 * @param Cell The cell element.
 * @param Name The span attribute name.
 * @return The span.
 */
static int TableCellSpan(xmlNodePtr Cell, const char* Name)
{
    QString Value = DocDocument::NodeAttribute(Cell, Name);
    if (Value.isEmpty()) {
        return 1;
    }
    bool Ok = false;
    int Span = Value.toInt(&Ok);
    return Ok ? Span : 1;
}

/**
 * @brief Tests whether every row of a table spans the same
 *        effective width, counting spans and rowspan continuation.
 *
 * Rows are walked as one grid, header, body, and footer alike:
 * a cell covers its colspan columns for its rowspan rows, and a
 * row's width is the rightmost column any cell or continuation
 * reaches. A table whose rows disagree is ragged, and the grid
 * editor cannot show it, so it stays preserved, whole.
 * @param TableNode The <table> element.
 * @param Width Receives the shared width, when the rows agree.
 * @return False when the rows disagree.
 */
static bool TableGridWidthsAgreeNode(xmlNodePtr TableNode, int& Width)
{
    QVector<int> Remaining;
    Width = -1;
    const char* Sections[] = { "thead", "tbody", "tfoot" };
    for (const char* SectionName : Sections) {
        xmlNodePtr Section = DocDocument::ChildElement(TableNode, SectionName);
        if (Section == nullptr) {
            continue;
        }
        for (xmlNodePtr Row = Section->children; Row != nullptr;
             Row = Row->next) {
            if (!IsElementKind(Row) ||
                !DocDocument::NodeNameEquals(Row, "tr")) {
                continue;
            }
            int Column = 0;
            for (xmlNodePtr Cell = Row->children; Cell != nullptr;
                 Cell = Cell->next) {
                if (!IsElementKind(Cell)) {
                    continue;
                }
                QString CellName = TextFromXmlChars(Cell->name);
                if (CellName != QLatin1String("td") &&
                    CellName != QLatin1String("th")) {
                    continue;
                }
                int Colspan = TableCellSpan(Cell, "colspan");
                int Rowspan = TableCellSpan(Cell, "rowspan");
                while (Column < Remaining.size() && Remaining[Column] > 0) {
                    ++Column;
                }
                while (Remaining.size() < Column + Colspan) {
                    Remaining.append(0);
                }
                for (int c = Column; c < Column + Colspan; ++c) {
                    Remaining[c] = Rowspan;
                }
                Column += Colspan;
            }
            int RowWidth = Column;
            for (int c = 0; c < Remaining.size(); ++c) {
                if (Remaining[c] > 0 && c + 1 > RowWidth) {
                    RowWidth = c + 1;
                }
            }
            if (Width < 0) {
                Width = RowWidth;
            } else if (RowWidth != Width) {
                return false;
            }
            for (int c = 0; c < Remaining.size(); ++c) {
                if (Remaining[c] > 0) {
                    --Remaining[c];
                }
            }
        }
    }
    return Width > 0;
}

/**
 * @brief Tests whether a <table> element converts to a table.
 *
 * A table holds an optional caption, an optional header, one
 * body, and an optional footer, in that order. An index entry
 * keeps the whole table preserved, as for figures, and a second
 * body keeps it preserved too: this phase models one body only.
 * A table with no body, a caption holding runs outside the
 * <name> set, a cell holding block content or content outside
 * the cell set, a comment or processing instruction inside a
 * cell or between cells, a namespace declaration, an entity
 * reference, and a ragged grid keep it preserved too.
 * @param Node The <table> element.
 * @return False when the element must be kept whole instead.
 */
static bool TableElementConverts(xmlNodePtr Node)
{
    if (!IsElementKind(Node) ||
        !DocDocument::NodeNameEquals(Node, "table") ||
        !ElementHasNoNsDecls(Node)) {
        return false;
    }
    bool SawName = false;
    bool SawHead = false;
    bool SawBody = false;
    bool SawFoot = false;
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if ((IsTextKind(Child) || IsCdataKind(Child)) &&
            DocDocument::IsWhitespaceText(Child)) {
            continue;
        }
        if (IsCommentKind(Child) || IsPiKind(Child)) {
            continue;
        }
        if (!IsElementKind(Child)) {
            return false;
        }
        QString ChildName = TextFromXmlChars(Child->name);
        if (ChildName == QLatin1String("name")) {
            if (SawName || SawHead || SawBody || SawFoot) {
                return false;
            }
            SawName = true;
            if (!CaptionRunsConvert(Child)) {
                return false;
            }
            continue;
        }
        if (ChildName == QLatin1String("iref")) {
            return false;
        }
        if (ChildName == QLatin1String("thead")) {
            if (SawHead || SawBody || SawFoot) {
                return false;
            }
            SawHead = true;
            if (!TableSectionConverts(Child, "thead")) {
                return false;
            }
            continue;
        }
        if (ChildName == QLatin1String("tbody")) {
            if (SawBody || SawFoot) {
                return false;
            }
            SawBody = true;
            if (!TableSectionConverts(Child, "tbody")) {
                return false;
            }
            continue;
        }
        if (ChildName == QLatin1String("tfoot")) {
            if (SawFoot) {
                return false;
            }
            SawFoot = true;
            if (!TableSectionConverts(Child, "tfoot")) {
                return false;
            }
            continue;
        }
        return false;
    }
    if (!SawBody) {
        return false;
    }
    int Width = 0;
    return TableGridWidthsAgreeNode(Node, Width);
}

/**
 * @brief Tests whether a <sourcecode> element converts.
 *
 * A sourcecode element is modeled only when it holds nothing but
 * text: any child element or entity reference keeps the whole
 * element preserved. A src attribute is modeled too, when the
 * element holds no real content: the content lives in a file
 * the editor cannot show, so the editor shows the placeholder
 * for it. Src with real content stays preserved, since src and
 * content cannot go together.
 * @param Node The <sourcecode> element.
 * @return False when the element must be kept whole instead.
 */
static bool SourceCodeElementConverts(xmlNodePtr Node)
{
    if (!IsElementKind(Node) ||
        !DocDocument::NodeNameEquals(Node, "sourcecode") ||
        !ElementHasNoNsDecls(Node)) {
        return false;
    }
    bool HasSrc = DocDocument::HasNodeAttribute(Node, "src");
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if (IsCommentKind(Child) || IsPiKind(Child)) {
            continue;
        }
        if (!IsTextKind(Child) && !IsCdataKind(Child)) {
            return false;
        }
        if (HasSrc && !DocDocument::IsWhitespaceText(Child)) {
            return false;
        }
    }
    return true;
}

bool DocDocument::BodyNodeIsEditable(xmlNodePtr Node)
{
    if (IsCommentKind(Node)) {
        return true;
    }
    if (!IsElementKind(Node)) {
        return false;
    }
    QString Name = TextFromXmlChars(Node->name);
    if (Name == QLatin1String("t")) {
        return TElementConverts(Node);
    }
    if (IsBlockListName(Name)) {
        return ListElementConverts(Node);
    }
    if (Name == QLatin1String("figure") ||
        Name == QLatin1String("artset") ||
        Name == QLatin1String("artwork")) {
        return FigureElementConverts(Node);
    }
    if (Name == QLatin1String("table")) {
        return TableElementConverts(Node);
    }
    if (Name == QLatin1String("sourcecode")) {
        return SourceCodeElementConverts(Node);
    }
    return false;
}

/**
 * @brief The leading <name> child element of a parent, if the first
 *        child element is a <name>.
 * @param Parent The parent.
 * @return The <name> element, or nullptr when the first child
 *         element is not a <name>.
 */
static xmlNodePtr LeadingNameElement(xmlNodePtr Parent)
{
    if (Parent == nullptr) {
        return nullptr;
    }
    for (xmlNodePtr Child = Parent->children; Child != nullptr;
         Child = Child->next) {
        if (IsElementKind(Child)) {
            return DocDocument::NodeNameEquals(Child, "name") ? Child : nullptr;
        }
    }
    return nullptr;
}

QVector<xmlNodePtr> DocDocument::BodyNodes(xmlNodePtr Parent)
{
    QVector<xmlNodePtr> Nodes;
    if (Parent == nullptr) {
        return Nodes;
    }
    xmlNodePtr KeptName = LeadingNameElement(Parent);
    for (xmlNodePtr Child = Parent->children; Child != nullptr;
         Child = Child->next) {
        if (Child == KeptName) {
            continue;
        }
        if (BodyNodeIsEditable(Child)) {
            Nodes.append(Child);
        }
    }
    return Nodes;
}

QVector<DocPreservedItem> DocDocument::PreservedBodyNodes(xmlNodePtr Parent)
{
    QVector<DocPreservedItem> Items;
    if (Parent == nullptr) {
        return Items;
    }
    xmlNodePtr KeptName = LeadingNameElement(Parent);
    int EditableCount = 0;
    for (xmlNodePtr Child = Parent->children; Child != nullptr;
         Child = Child->next) {
        if (Child == KeptName) {
            continue;
        }
        if (IsWhitespaceText(Child)) {
            continue;
        }
        if (IsElementKind(Child) &&
            NodeNameEquals(Child, "section")) {
            continue;
        }
        if (BodyNodeIsEditable(Child)) {
            ++EditableCount;
            continue;
        }
        DocPreservedItem Item;
        Item.AfterNodes = EditableCount;
        Item.Node = Child;
        Items.append(Item);
    }
    return Items;
}

void DocDocument::RebuildBodyChildren(xmlNodePtr Parent,
                                      const QVector<xmlNodePtr>& Editable,
                                      const QVector<DocPreservedItem>& Preserved)
{
    if (Parent == nullptr) {
        return;
    }
    xmlNodePtr KeptName = LeadingNameElement(Parent);
    // Unlink every non-<name> child. Old editable nodes are
    // freed, unless the caller passed the very same node back
    // in Editable. Old preserved nodes that the caller did not
    // pass back are freed too, except child sections, which the
    // caller never lists: they are kept, with an anchor counting
    // the editable nodes before them, so they stay in place.
    QVector<DocPreservedItem> KeptSections;
    int EditableSeen = 0;
    for (xmlNodePtr Child : CollectChildren(Parent)) {
        if (Child == KeptName) {
            continue;
        }
        xmlUnlinkNode(Child);
        if (IsWhitespaceText(Child)) {
            xmlFreeNode(Child);
            continue;
        }
        if (BodyNodeIsEditable(Child)) {
            ++EditableSeen;
            if (!Editable.contains(Child)) {
                xmlFreeNode(Child);
            }
            continue;
        }
        bool PassedBack = false;
        for (const DocPreservedItem& Item : Preserved) {
            if (Item.Node == Child) {
                PassedBack = true;
                break;
            }
        }
        if (PassedBack) {
            continue;
        }
        if (IsElementKind(Child) &&
            NodeNameEquals(Child, "section")) {
            DocPreservedItem Kept;
            Kept.AfterNodes = EditableSeen;
            Kept.Node = Child;
            KeptSections.append(Kept);
            continue;
        }
        xmlFreeNode(Child);
    }
    // Merge the passed preserved items with the kept sections,
    // keeping file order among items sharing one anchor.
    QVector<DocPreservedItem> Ordered = Preserved;
    Ordered.append(KeptSections);
    std::stable_sort(Ordered.begin(), Ordered.end(),
                     [](const DocPreservedItem& Left,
                        const DocPreservedItem& Right) {
                         return Left.AfterNodes < Right.AfterNodes;
                     });
    int Emitted = 0;
    for (const DocPreservedItem& Item : Ordered) {
        int At = Item.AfterNodes;
        if (At < 0) {
            At = 0;
        }
        if (At > Editable.size()) {
            At = Editable.size();
        }
        while (Emitted < At) {
            xmlAddChild(Parent, Editable.at(Emitted));
            ++Emitted;
        }
        if (Item.Node != nullptr) {
            xmlAddChild(Parent, Item.Node);
        }
    }
    while (Emitted < Editable.size()) {
        xmlAddChild(Parent, Editable.at(Emitted));
        ++Emitted;
    }
}

void DocDocument::SetSectionBody(xmlNodePtr Section,
                                 const QVector<xmlNodePtr>& Editable,
                                 const QVector<DocPreservedItem>& Preserved)
{
    if (Section == nullptr) {
        return;
    }
    RebuildBodyChildren(Section, Editable, Preserved);
}

void DocDocument::SetNoteBody(xmlNodePtr Note,
                              const QVector<xmlNodePtr>& Editable,
                              const QVector<DocPreservedItem>& Preserved)
{
    if (Note == nullptr) {
        return;
    }
    RebuildBodyChildren(Note, Editable, Preserved);
}

/**
 * @brief The last child element of a parent with a given name.
 * @param Parent The parent.
 * @param Name The element name.
 * @return The child, borrowed, or nullptr when absent.
 */
static xmlNodePtr LastChildElementNamed(xmlNodePtr Parent, const char* Name)
{
    xmlNodePtr Found = nullptr;
    if (Parent == nullptr) {
        return Found;
    }
    for (xmlNodePtr Child = Parent->children; Child != nullptr;
         Child = Child->next) {
        if (DocDocument::NodeNameEquals(Child, Name)) {
            Found = Child;
        }
    }
    return Found;
}

/**
 * @brief Inserts a detached node after the last of several
 *        predecessor elements, or at the front when none exists.
 *
 * The predecessors are tried in preference order: the first
 * predecessor name that has a child element wins, and the node
 * goes after that element's last occurrence, so a new element
 * lands in its canonical spot in the parent.
 * @param Parent The parent.
 * @param Node The detached node to insert.
 * @param Predecessors The predecessor names, best first.
 */
static void InsertAfterPredecessor(xmlNodePtr Parent, xmlNodePtr Node,
                                   const QVector<const char*>& Predecessors)
{
    if (Parent == nullptr || Node == nullptr) {
        return;
    }
    for (const char* Predecessor : Predecessors) {
        xmlNodePtr Anchor = LastChildElementNamed(Parent, Predecessor);
        if (Anchor != nullptr) {
            xmlAddNextSibling(Anchor, Node);
            return;
        }
    }
    xmlNodePtr First = Parent->children;
    if (First != nullptr) {
        xmlAddPrevSibling(First, Node);
    } else {
        xmlAddChild(Parent, Node);
    }
}

/**
 * @brief Removes every child element of a parent with a given name.
 * @param Parent The parent.
 * @param Name The element name.
 */
static void RemoveChildElements(xmlNodePtr Parent, const char* Name)
{
    if (Parent == nullptr) {
        return;
    }
    for (xmlNodePtr Child : CollectChildren(Parent)) {
        if (DocDocument::NodeNameEquals(Child, Name)) {
            UnlinkAndFreeNode(Child);
        }
    }
}

/**
 * @brief Rebuilds the simple text children of a parent.
 *
 * The old children with the name are removed, and one element
 * per value is inserted after the latest canonical predecessor,
 * so areas, workgroups, and keywords land in file order in
 * their usual spot.
 * @param Parent The parent.
 * @param Name The element name.
 * @param Values The texts, in order.
 * @param Predecessors The predecessor names, best first.
 */
static void RebuildTextChildren(xmlNodePtr Parent, const char* Name,
                                const QStringList& Values,
                                const QVector<const char*>& Predecessors)
{
    if (Parent == nullptr) {
        return;
    }
    RemoveChildElements(Parent, Name);
    xmlNodePtr Anchor = nullptr;
    for (const char* Predecessor : Predecessors) {
        Anchor = LastChildElementNamed(Parent, Predecessor);
        if (Anchor != nullptr) {
            break;
        }
    }
    for (const QString& Value : Values) {
        QByteArray TextUtf8 = Utf8Bytes(Value.trimmed());
        xmlNodePtr Element = xmlNewNode(nullptr, reinterpret_cast<const xmlChar*>(Name));
        if (Element == nullptr) {
            continue;
        }
        if (!TextUtf8.isEmpty()) {
            xmlNodePtr TextNode = xmlNewText(reinterpret_cast<const xmlChar*>(TextUtf8.constData()));
            if (TextNode != nullptr) {
                xmlAddChild(Element, TextNode);
            }
        }
        if (Anchor != nullptr) {
            xmlAddNextSibling(Anchor, Element);
            Anchor = Element;
        } else {
            xmlNodePtr First = Parent->children;
            if (First != nullptr) {
                xmlAddPrevSibling(First, Element);
            } else {
                xmlAddChild(Parent, Element);
            }
            Anchor = Element;
        }
    }
}

DocDocument::DocDocument()
    : HadSkippedContent(false)
    , _Document(nullptr)
{
}

DocDocument::DocDocument(const DocDocument& Other)
    : HadSkippedContent(Other.HadSkippedContent)
    , _Document(Other._Document != nullptr
                ? xmlCopyDoc(Other._Document, 1) : nullptr)
{
}

DocDocument::DocDocument(DocDocument&& Other) noexcept
    : HadSkippedContent(Other.HadSkippedContent)
    , _Document(Other._Document)
{
    Other._Document = nullptr;
    Other.HadSkippedContent = false;
}

DocDocument& DocDocument::operator=(const DocDocument& Other)
{
    if (this != &Other) {
        xmlDocPtr Copied = Other._Document != nullptr
            ? xmlCopyDoc(Other._Document, 1) : nullptr;
        if (Copied != nullptr || Other._Document == nullptr) {
            ResetDocument(Copied);
            HadSkippedContent = Other.HadSkippedContent;
        }
    }
    return *this;
}

DocDocument& DocDocument::operator=(DocDocument&& Other) noexcept
{
    if (this != &Other) {
        ResetDocument(Other._Document);
        HadSkippedContent = Other.HadSkippedContent;
        Other._Document = nullptr;
        Other.HadSkippedContent = false;
    }
    return *this;
}

DocDocument::~DocDocument()
{
    if (_Document != nullptr) {
        xmlFreeDoc(_Document);
        _Document = nullptr;
    }
}

void DocDocument::ResetDocument(xmlDocPtr Document)
{
    if (_Document != nullptr) {
        xmlFreeDoc(_Document);
    }
    _Document = Document;
}

bool DocDocument::IsEmpty() const
{
    return _Document == nullptr;
}

xmlDocPtr DocDocument::XmlDocument() const
{
    return _Document;
}

xmlNodePtr DocDocument::RfcNode() const
{
    if (_Document == nullptr) {
        return nullptr;
    }
    return xmlDocGetRootElement(_Document);
}

xmlNodePtr DocDocument::FrontNode() const
{
    return ChildElement(RfcNode(), "front");
}

xmlNodePtr DocDocument::MiddleNode() const
{
    return ChildElement(RfcNode(), "middle");
}

xmlNodePtr DocDocument::BackNode() const
{
    return ChildElement(RfcNode(), "back");
}

xmlNodePtr DocDocument::ChildElement(xmlNodePtr Parent, const char* Name)
{
    if (Parent == nullptr || Name == nullptr) {
        return nullptr;
    }
    for (xmlNodePtr Child = Parent->children; Child != nullptr;
         Child = Child->next) {
        if (NodeNameEquals(Child, Name)) {
            return Child;
        }
    }
    return nullptr;
}

QVector<xmlNodePtr> DocDocument::ChildElements(xmlNodePtr Parent,
                                              const char* Name)
{
    QVector<xmlNodePtr> Found;
    if (Parent == nullptr || Name == nullptr) {
        return Found;
    }
    for (xmlNodePtr Child = Parent->children; Child != nullptr;
         Child = Child->next) {
        if (NodeNameEquals(Child, Name)) {
            Found.append(Child);
        }
    }
    return Found;
}

QString DocDocument::ChildText(xmlNodePtr Parent, const char* Name)
{
    xmlNodePtr Child = ChildElement(Parent, Name);
    if (Child == nullptr) {
        return QString();
    }
    return NodeText(Child).trimmed();
}

void DocDocument::SetChildText(xmlNodePtr Parent, const char* Name,
                               const QString& Text)
{
    if (Parent == nullptr || Name == nullptr) {
        return;
    }
    xmlNodePtr Child = ChildElement(Parent, Name);
    if (Text.trimmed().isEmpty()) {
        if (Child != nullptr) {
            UnlinkAndFreeNode(Child);
        }
        return;
    }
    if (Child == nullptr) {
        Child = xmlNewNode(nullptr, reinterpret_cast<const xmlChar*>(Name));
        if (Child == nullptr) {
            return;
        }
        xmlAddChild(Parent, Child);
    }
    SetNodeText(Child, Text);
}

QString DocDocument::NodeText(xmlNodePtr Node)
{
    if (Node == nullptr) {
        return QString();
    }
    xmlChar* Content = xmlNodeGetContent(Node);
    if (Content == nullptr) {
        return QString();
    }
    QString Text = TextFromXmlChars(Content);
    xmlFree(Content);
    return Text;
}

void DocDocument::SetNodeText(xmlNodePtr Node, const QString& Text)
{
    if (Node == nullptr) {
        return;
    }
    QByteArray TextUtf8 = Utf8Bytes(Text);
    xmlNodeSetContent(Node, reinterpret_cast<const xmlChar*>(TextUtf8.constData()));
}

QString DocDocument::NodeAttribute(xmlNodePtr Node, const char* Name)
{
    if (Node == nullptr || Name == nullptr) {
        return QString();
    }
    xmlChar* Value = xmlGetProp(Node, reinterpret_cast<const xmlChar*>(Name));
    if (Value == nullptr) {
        return QString();
    }
    QString Text = TextFromXmlChars(Value);
    xmlFree(Value);
    return Text;
}

void DocDocument::SetNodeAttribute(xmlNodePtr Node, const char* Name,
                                  const QString& Value)
{
    if (Node == nullptr || Name == nullptr) {
        return;
    }
    if (Value.isEmpty()) {
        xmlUnsetProp(Node, reinterpret_cast<const xmlChar*>(Name));
        return;
    }
    QByteArray ValueUtf8 = Utf8Bytes(Value);
    xmlSetProp(Node, reinterpret_cast<const xmlChar*>(Name),
               reinterpret_cast<const xmlChar*>(ValueUtf8.constData()));
}

bool DocDocument::HasNodeAttribute(xmlNodePtr Node, const char* Name)
{
    if (Node == nullptr || Name == nullptr) {
        return false;
    }
    return xmlHasProp(Node, reinterpret_cast<const xmlChar*>(Name)) != nullptr;
}

xmlNodePtr DocDocument::NewChildElement(xmlNodePtr Parent, const char* Name)
{
    if (Parent == nullptr || Name == nullptr) {
        return nullptr;
    }
    return xmlNewChild(Parent, nullptr, reinterpret_cast<const xmlChar*>(Name),
                       nullptr);
}

xmlNodePtr DocDocument::CopyNode(xmlNodePtr Node)
{
    if (Node == nullptr) {
        return nullptr;
    }
    return xmlCopyNode(Node, 1);
}

void DocDocument::FreeNodes(QVector<xmlNodePtr>& Nodes)
{
    for (xmlNodePtr Node : Nodes) {
        if (Node != nullptr) {
            xmlFreeNode(Node);
        }
    }
    Nodes.clear();
}

bool DocDocument::NodeNameEquals(xmlNodePtr Node, const char* Name)
{
    if (!IsElementKind(Node) || Name == nullptr) {
        return false;
    }
    return xmlStrEqual(Node->name, reinterpret_cast<const xmlChar*>(Name)) != 0;
}

bool DocDocument::IsWhitespaceText(xmlNodePtr Node)
{
    if (!IsTextKind(Node)) {
        return false;
    }
    QString Text = NodeText(Node);
    for (QChar Character : Text) {
        if (Character != QLatin1Char(' ') && Character != QLatin1Char('\t') &&
            Character != QLatin1Char('\n') && Character != QLatin1Char('\r')) {
            return false;
        }
    }
    return true;
}

QString DocDocument::DocName() const
{
    return NodeAttribute(RfcNode(), "docName");
}

void DocDocument::SetDocName(const QString& Name)
{
    xmlNodePtr Rfc = RfcNode();
    if (Rfc == nullptr) {
        return;
    }
    SetNodeAttribute(Rfc, "docName", Name);
}

QString DocDocument::Title() const
{
    xmlNodePtr TitleNode = ChildElement(FrontNode(), "title");
    if (TitleNode == nullptr) {
        return QString();
    }
    return NodeText(TitleNode).trimmed();
}

QVector<int> DocDocument::TitleBreaks() const
{
    QVector<int> Breaks;
    xmlNodePtr TitleNode = ChildElement(FrontNode(), "title");
    if (TitleNode == nullptr) {
        return Breaks;
    }
    QString Raw;
    QVector<int> Found;
    for (xmlNodePtr Child = TitleNode->children; Child != nullptr;
         Child = Child->next) {
        if (IsTextKind(Child) || IsCdataKind(Child)) {
            Raw += NodeText(Child);
        } else if (IsElementKind(Child) &&
                   NodeNameEquals(Child, "br") &&
                   Child->properties == nullptr &&
                   Child->nsDef == nullptr) {
            Found.append(Raw.size());
        } else if (IsElementKind(Child)) {
            Raw += NodeText(Child);
        }
    }
    int First = 0;
    while (First < Raw.size() && Raw.at(First).isSpace()) {
        ++First;
    }
    int Last = Raw.size();
    while (Last > First && Raw.at(Last - 1).isSpace()) {
        --Last;
    }
    QString Text = Raw.mid(First, Last - First);
    for (int Offset : Found) {
        int Adjusted = Offset - First;
        if (Adjusted > 0 && Adjusted < Text.size() &&
            !Breaks.contains(Adjusted)) {
            Breaks.append(Adjusted);
        }
    }
    std::sort(Breaks.begin(), Breaks.end());
    return Breaks;
}

/**
 * @brief The <title> element of a front, creating it first in the
 *        front when missing.
 * @param Front The <front> element.
 * @return The title element, or nullptr when there is no front.
 */
static xmlNodePtr EnsureFrontTitle(xmlNodePtr Front)
{
    if (Front == nullptr) {
        return nullptr;
    }
    xmlNodePtr TitleNode = DocDocument::ChildElement(Front, "title");
    if (TitleNode != nullptr) {
        return TitleNode;
    }
    TitleNode = xmlNewNode(nullptr, reinterpret_cast<const xmlChar*>("title"));
    if (TitleNode == nullptr) {
        return nullptr;
    }
    xmlNodePtr First = nullptr;
    for (xmlNodePtr Child = Front->children; Child != nullptr;
         Child = Child->next) {
        if (IsElementKind(Child)) {
            First = Child;
            break;
        }
    }
    if (First != nullptr) {
        xmlAddPrevSibling(First, TitleNode);
    } else {
        xmlAddChild(Front, TitleNode);
    }
    return TitleNode;
}

void DocDocument::SetTitle(const QString& Text, const QVector<int>& Breaks)
{
    xmlNodePtr Front = FrontNode();
    if (Front == nullptr) {
        return;
    }
    QString Clean = Text.trimmed();
    xmlNodePtr TitleNode = EnsureFrontTitle(Front);
    if (TitleNode == nullptr) {
        return;
    }
    RemoveAllChildren(TitleNode);
    QVector<int> Ordered = Breaks;
    std::sort(Ordered.begin(), Ordered.end());
    int At = 0;
    for (int Offset : Ordered) {
        if (Offset <= At || Offset >= Clean.size()) {
            continue;
        }
        QByteArray Segment = Utf8Bytes(Clean.mid(At, Offset - At));
        if (!Segment.isEmpty()) {
            xmlNodePtr TextNode = xmlNewText(reinterpret_cast<const xmlChar*>(Segment.constData()));
            if (TextNode != nullptr) {
                xmlAddChild(TitleNode, TextNode);
            }
        }
        xmlNewChild(TitleNode, nullptr, reinterpret_cast<const xmlChar*>("br"),
                    nullptr);
        At = Offset;
    }
    QByteArray Tail = Utf8Bytes(Clean.mid(At));
    if (!Tail.isEmpty()) {
        xmlNodePtr TextNode = xmlNewText(reinterpret_cast<const xmlChar*>(Tail.constData()));
        if (TextNode != nullptr) {
            xmlAddChild(TitleNode, TextNode);
        }
    }
}

QString DocDocument::TitleAbbrev() const
{
    return NodeAttribute(ChildElement(FrontNode(), "title"), "abbrev");
}

void DocDocument::SetTitleAbbrev(const QString& Abbrev)
{
    xmlNodePtr Front = FrontNode();
    if (Front == nullptr) {
        return;
    }
    if (Abbrev.trimmed().isEmpty()) {
        xmlNodePtr TitleNode = ChildElement(Front, "title");
        if (TitleNode != nullptr) {
            SetNodeAttribute(TitleNode, "abbrev", QString());
        }
        return;
    }
    xmlNodePtr TitleNode = EnsureFrontTitle(Front);
    if (TitleNode != nullptr) {
        SetNodeAttribute(TitleNode, "abbrev", Abbrev.trimmed());
    }
}

QString DocDocument::TitleAscii() const
{
    return NodeAttribute(ChildElement(FrontNode(), "title"), "ascii");
}

void DocDocument::SetTitleAscii(const QString& Ascii)
{
    xmlNodePtr Front = FrontNode();
    if (Front == nullptr) {
        return;
    }
    if (Ascii.trimmed().isEmpty()) {
        xmlNodePtr TitleNode = ChildElement(Front, "title");
        if (TitleNode != nullptr) {
            SetNodeAttribute(TitleNode, "ascii", QString());
        }
        return;
    }
    xmlNodePtr TitleNode = EnsureFrontTitle(Front);
    if (TitleNode != nullptr) {
        SetNodeAttribute(TitleNode, "ascii", Ascii.trimmed());
    }
}

QVector<xmlNodePtr> DocDocument::SeriesInfoNodes() const
{
    return ChildElements(FrontNode(), "seriesInfo");
}

void DocDocument::AddSeriesInfoNode()
{
    xmlNodePtr Front = FrontNode();
    if (Front == nullptr) {
        return;
    }
    xmlNodePtr Info = xmlNewNode(nullptr, reinterpret_cast<const xmlChar*>("seriesInfo"));
    if (Info == nullptr) {
        return;
    }
    InsertAfterPredecessor(Front, Info, { "seriesInfo", "title" });
}

void DocDocument::RemoveSeriesInfoNode(int Index)
{
    QVector<xmlNodePtr> Nodes = SeriesInfoNodes();
    if (Index < 0 || Index >= Nodes.size()) {
        return;
    }
    UnlinkAndFreeNode(Nodes.at(Index));
}

QStringList DocDocument::Areas() const
{
    QStringList Found;
    for (xmlNodePtr Node : ChildElements(FrontNode(), "area")) {
        Found.append(NodeText(Node).trimmed());
    }
    return Found;
}

void DocDocument::SetAreas(const QStringList& Values)
{
    RebuildTextChildren(FrontNode(), "area", Values,
                        { "date", "author", "seriesInfo", "title" });
}

QStringList DocDocument::Workgroups() const
{
    QStringList Found;
    for (xmlNodePtr Node : ChildElements(FrontNode(), "workgroup")) {
        Found.append(NodeText(Node).trimmed());
    }
    return Found;
}

void DocDocument::SetWorkgroups(const QStringList& Values)
{
    RebuildTextChildren(FrontNode(), "workgroup", Values,
                        { "area", "date", "author", "seriesInfo", "title" });
}

QStringList DocDocument::Keywords() const
{
    QStringList Found;
    for (xmlNodePtr Node : ChildElements(FrontNode(), "keyword")) {
        Found.append(NodeText(Node).trimmed());
    }
    return Found;
}

void DocDocument::SetKeywords(const QStringList& Values)
{
    RebuildTextChildren(FrontNode(), "keyword", Values,
                        { "workgroup", "area", "date", "author", "seriesInfo",
                          "title" });
}

QVector<xmlNodePtr> DocDocument::AuthorNodes() const
{
    return ChildElements(FrontNode(), "author");
}

void DocDocument::AddAuthorNode(xmlNodePtr Author)
{
    xmlNodePtr Front = FrontNode();
    if (Front == nullptr || Author == nullptr) {
        return;
    }
    InsertAfterPredecessor(Front, Author, { "author", "seriesInfo", "title" });
}

void DocDocument::RemoveAuthorNode(int Index)
{
    QVector<xmlNodePtr> Nodes = AuthorNodes();
    if (Index < 0 || Index >= Nodes.size()) {
        return;
    }
    UnlinkAndFreeNode(Nodes.at(Index));
}

QVector<xmlNodePtr> DocDocument::NoteNodes() const
{
    return ChildElements(FrontNode(), "note");
}

xmlNodePtr DocDocument::AddNoteNode()
{
    xmlNodePtr Front = FrontNode();
    if (Front == nullptr) {
        return nullptr;
    }
    xmlNodePtr Note = xmlNewNode(nullptr, reinterpret_cast<const xmlChar*>("note"));
    if (Note == nullptr) {
        return nullptr;
    }
    xmlNewChild(Note, nullptr, reinterpret_cast<const xmlChar*>("t"), nullptr);
    InsertAfterPredecessor(Front, Note,
                           { "note", "abstract", "keyword", "workgroup",
                             "area", "date", "author", "seriesInfo", "title" });
    return Note;
}

void DocDocument::RemoveNoteNode(int Index)
{
    QVector<xmlNodePtr> Nodes = NoteNodes();
    if (Index < 0 || Index >= Nodes.size()) {
        return;
    }
    UnlinkAndFreeNode(Nodes.at(Index));
}

QVector<xmlNodePtr> DocDocument::AbstractBodyNodes() const
{
    xmlNodePtr Abstract = ChildElement(FrontNode(), "abstract");
    if (Abstract == nullptr) {
        return QVector<xmlNodePtr>();
    }
    return BodyNodes(Abstract);
}

QVector<DocPreservedItem> DocDocument::AbstractPreservedNodes() const
{
    xmlNodePtr Abstract = ChildElement(FrontNode(), "abstract");
    if (Abstract == nullptr) {
        return QVector<DocPreservedItem>();
    }
    return PreservedBodyNodes(Abstract);
}

/**
 * @brief The <abstract> element of a front, creating it when missing.
 * @param Front The <front> element.
 * @return The abstract element, or nullptr when there is no front.
 */
static xmlNodePtr EnsureFrontAbstract(xmlNodePtr Front)
{
    if (Front == nullptr) {
        return nullptr;
    }
    xmlNodePtr Abstract = DocDocument::ChildElement(Front, "abstract");
    if (Abstract != nullptr) {
        return Abstract;
    }
    Abstract = xmlNewNode(nullptr, reinterpret_cast<const xmlChar*>("abstract"));
    if (Abstract == nullptr) {
        return nullptr;
    }
    InsertAfterPredecessor(Front, Abstract,
                           { "keyword", "workgroup", "area", "date", "author",
                             "seriesInfo", "title" });
    return Abstract;
}

void DocDocument::SetAbstractBody(const QVector<xmlNodePtr>& Editable,
                                  const QVector<DocPreservedItem>& Preserved)
{
    xmlNodePtr Rfc = RfcNode();
    if (Rfc == nullptr) {
        return;
    }
    xmlNodePtr Front = FrontNode();
    if (Front == nullptr) {
        Front = xmlNewNode(nullptr, reinterpret_cast<const xmlChar*>("front"));
        if (Front == nullptr) {
            return;
        }
        xmlNodePtr First = nullptr;
        for (xmlNodePtr Child = Rfc->children; Child != nullptr;
             Child = Child->next) {
            if (IsElementKind(Child)) {
                First = Child;
                break;
            }
        }
        if (First != nullptr) {
            xmlAddPrevSibling(First, Front);
        } else {
            xmlAddChild(Rfc, Front);
        }
    }
    xmlNodePtr Abstract = EnsureFrontAbstract(Front);
    if (Abstract == nullptr) {
        return;
    }
    RebuildBodyChildren(Abstract, Editable, Preserved);
}

QVector<xmlNodePtr> DocDocument::TopSectionNodes() const
{
    return ChildElements(MiddleNode(), "section");
}

xmlNodePtr DocDocument::FindSectionNode(const QString& Path) const
{
    if (Path.trimmed().isEmpty()) {
        return nullptr;
    }
    QStringList Parts = Path.split(QLatin1Char('.'));
    QVector<xmlNodePtr> Level = TopSectionNodes();
    xmlNodePtr Found = nullptr;
    for (const QString& Part : Parts) {
        bool Ok = false;
        int Index = Part.toInt(&Ok);
        if (!Ok || Index < 0 || Index >= Level.size()) {
            return nullptr;
        }
        Found = Level.at(Index);
        Level = ChildSectionNodes(Found);
    }
    return Found;
}

QVector<xmlNodePtr> DocDocument::ChildSectionNodes(xmlNodePtr Section)
{
    return ChildElements(Section, "section");
}

QString DocDocument::SectionTitle(xmlNodePtr Section)
{
    if (Section == nullptr) {
        return QString();
    }
    xmlNodePtr NameNode = ChildElement(Section, "name");
    if (NameNode != nullptr) {
        return NodeText(NameNode).trimmed();
    }
    return NodeAttribute(Section, "title");
}

void DocDocument::SetSectionTitle(xmlNodePtr Section, const QString& Title)
{
    if (Section == nullptr) {
        return;
    }
    xmlNodePtr NameNode = ChildElement(Section, "name");
    if (NameNode == nullptr) {
        NameNode = xmlNewNode(nullptr, reinterpret_cast<const xmlChar*>("name"));
        if (NameNode == nullptr) {
            return;
        }
        xmlNodePtr First = nullptr;
        for (xmlNodePtr Child = Section->children; Child != nullptr;
             Child = Child->next) {
            if (IsElementKind(Child)) {
                First = Child;
                break;
            }
        }
        if (First != nullptr) {
            xmlAddPrevSibling(First, NameNode);
        } else {
            xmlAddChild(Section, NameNode);
        }
    }
    SetNodeText(NameNode, Title);
}

xmlNodePtr DocDocument::NewSectionNode(const QString& Title)
{
    xmlNodePtr Section = xmlNewNode(nullptr, reinterpret_cast<const xmlChar*>("section"));
    if (Section == nullptr) {
        return nullptr;
    }
    QByteArray TitleUtf8 = Utf8Bytes(Title);
    xmlNewTextChild(Section, nullptr, reinterpret_cast<const xmlChar*>("name"),
                    reinterpret_cast<const xmlChar*>(TitleUtf8.constData()));
    return Section;
}

QVector<xmlNodePtr> DocDocument::ReferenceGroupNodes() const
{
    return ChildElements(BackNode(), "references");
}

QVector<xmlNodePtr> DocDocument::BackChildNodes() const
{
    QVector<xmlNodePtr> Nodes;
    xmlNodePtr Back = BackNode();
    if (Back == nullptr) {
        return Nodes;
    }
    for (xmlNodePtr Child = Back->children; Child != nullptr;
         Child = Child->next) {
        if (IsWhitespaceText(Child)) {
            continue;
        }
        Nodes.append(Child);
    }
    return Nodes;
}

QVector<xmlNodePtr> DocDocument::ReferenceEntries(xmlNodePtr Group)
{
    return ChildElements(Group, "reference");
}

QString DocDocument::ReferenceLabel(xmlNodePtr Entry)
{
    if (Entry == nullptr) {
        return QString();
    }
    QString Label = NodeAttribute(Entry, "anchor").trimmed();
    xmlNodePtr Front = ChildElement(Entry, "front");
    QString EntryTitle = Front != nullptr ? ChildText(Front, "title") : QString();
    if (!EntryTitle.trimmed().isEmpty()) {
        Label = Label.isEmpty()
            ? EntryTitle : Label + QStringLiteral(" - ") + EntryTitle;
    }
    return Label;
}

QVector<xmlNodePtr> DocDocument::ReferenceAuthors(xmlNodePtr Entry)
{
    xmlNodePtr Front = ChildElement(Entry, "front");
    if (Front == nullptr) {
        return QVector<xmlNodePtr>();
    }
    return ChildElements(Front, "author");
}

QString DocDocument::ReferenceDatePart(xmlNodePtr Entry, const char* Part)
{
    if (Entry == nullptr || Part == nullptr) {
        return QString();
    }
    xmlNodePtr Front = ChildElement(Entry, "front");
    xmlNodePtr DateNode = Front != nullptr ? ChildElement(Front, "date") : nullptr;
    if (DateNode == nullptr) {
        return QString();
    }
    return NodeAttribute(DateNode, Part);
}

QString DocDocument::AuthorAttribute(xmlNodePtr Author, const char* Name)
{
    return NodeAttribute(Author, Name);
}

void DocDocument::SetAuthorAttribute(xmlNodePtr Author, const char* Name,
                                     const QString& Value)
{
    SetNodeAttribute(Author, Name, Value);
}

QString DocDocument::AuthorOrganization(xmlNodePtr Author)
{
    return ChildText(Author, "organization");
}

void DocDocument::SetAuthorOrganization(xmlNodePtr Author,
                                        const QString& Organization)
{
    if (Author == nullptr) {
        return;
    }
    xmlNodePtr OrganizationNode = ChildElement(Author, "organization");
    if (Organization.trimmed().isEmpty()) {
        if (OrganizationNode != nullptr) {
            UnlinkAndFreeNode(OrganizationNode);
        }
        return;
    }
    if (OrganizationNode == nullptr) {
        OrganizationNode = xmlNewNode(nullptr, reinterpret_cast<const xmlChar*>("organization"));
        if (OrganizationNode == nullptr) {
            return;
        }
        xmlNodePtr Address = ChildElement(Author, "address");
        if (Address != nullptr) {
            xmlAddPrevSibling(Address, OrganizationNode);
        } else {
            xmlAddChild(Author, OrganizationNode);
        }
    }
    SetNodeText(OrganizationNode, Organization.trimmed());
}

/**
 * @brief The <address> element of an author, creating it when missing.
 * @param Author The <author> element.
 * @return The address element, or nullptr when there is no author.
 */
static xmlNodePtr EnsureAuthorAddress(xmlNodePtr Author)
{
    if (Author == nullptr) {
        return nullptr;
    }
    xmlNodePtr Address = DocDocument::ChildElement(Author, "address");
    if (Address != nullptr) {
        return Address;
    }
    Address = xmlNewNode(nullptr, reinterpret_cast<const xmlChar*>("address"));
    if (Address == nullptr) {
        return nullptr;
    }
    xmlAddChild(Author, Address);
    return Address;
}

/**
 * @brief The <postal> element of an author, creating it when missing.
 * @param Author The <author> element.
 * @return The postal element, or nullptr when there is no author.
 */
static xmlNodePtr EnsureAuthorPostal(xmlNodePtr Author)
{
    xmlNodePtr Address = EnsureAuthorAddress(Author);
    if (Address == nullptr) {
        return nullptr;
    }
    xmlNodePtr Postal = DocDocument::ChildElement(Address, "postal");
    if (Postal != nullptr) {
        return Postal;
    }
    Postal = xmlNewNode(nullptr, reinterpret_cast<const xmlChar*>("postal"));
    if (Postal == nullptr) {
        return nullptr;
    }
    xmlNodePtr First = Address->children;
    if (First != nullptr) {
        xmlAddPrevSibling(First, Postal);
    } else {
        xmlAddChild(Address, Postal);
    }
    return Postal;
}

/**
 * @brief Removes an empty <postal> element, and an <address> left empty by it.
 * @param Author The <author> element.
 */
static void PruneEmptyAuthorAddress(xmlNodePtr Author)
{
    if (Author == nullptr) {
        return;
    }
    xmlNodePtr Address = DocDocument::ChildElement(Author, "address");
    if (Address == nullptr) {
        return;
    }
    xmlNodePtr Postal = DocDocument::ChildElement(Address, "postal");
    if (Postal != nullptr && Postal->children == nullptr &&
        Postal->properties == nullptr && Postal->nsDef == nullptr) {
        UnlinkAndFreeNode(Postal);
    }
    if (Address->children == nullptr && Address->properties == nullptr &&
        Address->nsDef == nullptr) {
        UnlinkAndFreeNode(Address);
    }
}

QString DocDocument::AuthorContactField(xmlNodePtr Author, const char* Field)
{
    if (Author == nullptr || Field == nullptr) {
        return QString();
    }
    xmlNodePtr Address = ChildElement(Author, "address");
    return ChildText(Address, Field);
}

void DocDocument::SetAuthorContactField(xmlNodePtr Author, const char* Field,
                                        const QString& Value)
{
    if (Author == nullptr || Field == nullptr) {
        return;
    }
    QString FieldName = QString::fromUtf8(Field);
    if (FieldName != QLatin1String("email") &&
        FieldName != QLatin1String("phone") &&
        FieldName != QLatin1String("uri")) {
        return;
    }
    xmlNodePtr Address = EnsureAuthorAddress(Author);
    if (Address == nullptr) {
        return;
    }
    xmlNodePtr FieldNode = ChildElement(Address, Field);
    if (Value.trimmed().isEmpty()) {
        if (FieldNode != nullptr) {
            UnlinkAndFreeNode(FieldNode);
        }
        PruneEmptyAuthorAddress(Author);
        return;
    }
    if (FieldNode == nullptr) {
        FieldNode = xmlNewNode(nullptr, reinterpret_cast<const xmlChar*>(Field));
        if (FieldNode == nullptr) {
            return;
        }
        // The address order is postal, phone, facsimile, email,
        // uri: a phone goes before the email and uri, an email
        // before the uri, a uri last.
        xmlNodePtr Before = nullptr;
        if (FieldName == QLatin1String("phone")) {
            Before = ChildElement(Address, "email");
            if (Before == nullptr) {
                Before = ChildElement(Address, "uri");
            }
        } else if (FieldName == QLatin1String("email")) {
            Before = ChildElement(Address, "uri");
        }
        if (Before != nullptr) {
            xmlAddPrevSibling(Before, FieldNode);
        } else {
            xmlAddChild(Address, FieldNode);
        }
    }
    SetNodeText(FieldNode, Value.trimmed());
}

QString DocDocument::AuthorPostalPart(xmlNodePtr Author, const char* Part)
{
    if (Author == nullptr || Part == nullptr) {
        return QString();
    }
    xmlNodePtr Address = ChildElement(Author, "address");
    xmlNodePtr Postal = Address != nullptr ? ChildElement(Address, "postal")
                                           : nullptr;
    return ChildText(Postal, Part);
}

void DocDocument::SetAuthorPostalPart(xmlNodePtr Author, const char* Part,
                                      const QString& Value)
{
    if (Author == nullptr || Part == nullptr) {
        return;
    }
    if (!IsPostalPartName(QString::fromUtf8(Part))) {
        return;
    }
    xmlNodePtr Postal = EnsureAuthorPostal(Author);
    if (Postal == nullptr) {
        return;
    }
    xmlNodePtr PartNode = ChildElement(Postal, Part);
    if (Value.trimmed().isEmpty()) {
        if (PartNode != nullptr) {
            UnlinkAndFreeNode(PartNode);
        }
        PruneEmptyAuthorAddress(Author);
        return;
    }
    if (PartNode == nullptr) {
        // A structured part switches the postal address to the
        // structured form: any free-form lines go.
        for (xmlNodePtr Child : CollectChildren(Postal)) {
            if (NodeNameEquals(Child, "postalLine")) {
                UnlinkAndFreeNode(Child);
            }
        }
        PartNode = xmlNewNode(nullptr, reinterpret_cast<const xmlChar*>(Part));
        if (PartNode == nullptr) {
            return;
        }
        // The conventional order is post office box, street,
        // extended address, city area, city, region, code,
        // country, sorting code: the part goes before the first
        // part that follows it in that order.
        static const char* Order[] = {
            "pobox", "street", "extaddr", "cityarea", "city", "region",
            "code", "country", "sortingcode"
        };
        int Wanted = -1;
        for (int i = 0; i < 9; ++i) {
            if (QString::fromUtf8(Part) == QLatin1String(Order[i])) {
                Wanted = i;
                break;
            }
        }
        xmlNodePtr Before = nullptr;
        for (xmlNodePtr Child = Postal->children; Child != nullptr;
             Child = Child->next) {
            if (!IsElementKind(Child)) {
                continue;
            }
            QString ChildName = TextFromXmlChars(Child->name);
            for (int i = Wanted + 1; i < 9; ++i) {
                if (ChildName == QLatin1String(Order[i])) {
                    Before = Child;
                    break;
                }
            }
            if (Before != nullptr) {
                break;
            }
        }
        if (Before != nullptr) {
            xmlAddPrevSibling(Before, PartNode);
        } else {
            xmlAddChild(Postal, PartNode);
        }
    }
    SetNodeText(PartNode, Value.trimmed());
}

bool DocDocument::AuthorPostalUsesLines(xmlNodePtr Author)
{
    if (Author == nullptr) {
        return false;
    }
    xmlNodePtr Address = ChildElement(Author, "address");
    xmlNodePtr Postal = Address != nullptr ? ChildElement(Address, "postal")
                                           : nullptr;
    if (Postal == nullptr) {
        return false;
    }
    for (xmlNodePtr Child = Postal->children; Child != nullptr;
         Child = Child->next) {
        if (!IsElementKind(Child)) {
            continue;
        }
        QString Name = TextFromXmlChars(Child->name);
        if (Name == QLatin1String("postalLine")) {
            return true;
        }
        if (IsPostalPartName(Name)) {
            return false;
        }
    }
    return false;
}

QStringList DocDocument::AuthorPostalLines(xmlNodePtr Author)
{
    QStringList Lines;
    if (Author == nullptr) {
        return Lines;
    }
    xmlNodePtr Address = ChildElement(Author, "address");
    xmlNodePtr Postal = Address != nullptr ? ChildElement(Address, "postal")
                                           : nullptr;
    for (xmlNodePtr Node : ChildElements(Postal, "postalLine")) {
        Lines.append(NodeText(Node).trimmed());
    }
    return Lines;
}

void DocDocument::SetAuthorPostalLines(xmlNodePtr Author,
                                       const QVector<QString>& Lines)
{
    if (Author == nullptr) {
        return;
    }
    xmlNodePtr Postal = nullptr;
    if (!Lines.isEmpty()) {
        Postal = EnsureAuthorPostal(Author);
        if (Postal == nullptr) {
            return;
        }
    } else {
        xmlNodePtr Address = ChildElement(Author, "address");
        Postal = Address != nullptr ? ChildElement(Address, "postal") : nullptr;
        if (Postal == nullptr) {
            return;
        }
    }
    // Lines switch the postal address to the free-form form: any
    // structured parts go.
    for (xmlNodePtr Child : CollectChildren(Postal)) {
        if (IsElementKind(Child)) {
            QString Name = TextFromXmlChars(Child->name);
            if (Name == QLatin1String("postalLine") || IsPostalPartName(Name)) {
                UnlinkAndFreeNode(Child);
            }
        }
    }
    for (const QString& Line : Lines) {
        QByteArray LineUtf8 = Utf8Bytes(Line.trimmed());
        xmlNodePtr LineNode = xmlNewTextChild(Postal, nullptr,
            reinterpret_cast<const xmlChar*>("postalLine"),
            reinterpret_cast<const xmlChar*>(LineUtf8.constData()));
        Q_UNUSED(LineNode);
    }
    PruneEmptyAuthorAddress(Author);
}

// The address and postal preserved-content checks call each other
// through AuthorHasPreservedContent, which is defined first.
static bool AddressHasPreservedContent(xmlNodePtr Address);
static bool PostalHasPreservedContent(xmlNodePtr Postal);

bool DocDocument::AuthorHasPreservedContent(xmlNodePtr Author)
{
    if (Author == nullptr) {
        return false;
    }
    if (Author->nsDef != nullptr) {
        return true;
    }
    for (xmlAttrPtr Attribute = Author->properties; Attribute != nullptr;
         Attribute = Attribute->next) {
        QString Name = TextFromXmlChars(Attribute->name);
        if (Name == QLatin1String("fullname") ||
            Name == QLatin1String("initials") ||
            Name == QLatin1String("surname")) {
            continue;
        }
        if (Name == QLatin1String("role")) {
            QString Role = NodeAttribute(Author, "role");
            if (Role == QLatin1String("editor")) {
                continue;
            }
        }
        return true;
    }
    bool SawOrganization = false;
    bool SawAddress = false;
    for (xmlNodePtr Child = Author->children; Child != nullptr;
         Child = Child->next) {
        if (IsElementKind(Child)) {
            QString Name = TextFromXmlChars(Child->name);
            if (Name == QLatin1String("organization") && !SawOrganization) {
                SawOrganization = true;
                if (Child->properties != nullptr || Child->nsDef != nullptr) {
                    return true;
                }
                continue;
            }
            if (Name == QLatin1String("address") && !SawAddress) {
                SawAddress = true;
                if (Child->properties != nullptr || Child->nsDef != nullptr) {
                    return true;
                }
                if (AddressHasPreservedContent(Child)) {
                    return true;
                }
                continue;
            }
            return true;
        }
        if (IsCommentKind(Child) || IsPiKind(Child) || IsEntityRefKind(Child)) {
            return true;
        }
        if ((IsTextKind(Child) || IsCdataKind(Child)) &&
            !IsWhitespaceText(Child)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief True when an <address> holds anything the author form does not edit.
 * @param Address The <address> element.
 * @return True when preserved content exists in the address.
 */
static bool AddressHasPreservedContent(xmlNodePtr Address)
{
    if (Address == nullptr) {
        return false;
    }
    bool SawPostal = false;
    bool SawPhone = false;
    bool SawEmail = false;
    bool SawUri = false;
    for (xmlNodePtr Child = Address->children; Child != nullptr;
         Child = Child->next) {
        if (IsElementKind(Child)) {
            QString Name = TextFromXmlChars(Child->name);
            if (Name == QLatin1String("postal") && !SawPostal) {
                SawPostal = true;
                if (PostalHasPreservedContent(Child)) {
                    return true;
                }
                continue;
            }
            if (Name == QLatin1String("phone") && !SawPhone) {
                SawPhone = true;
                continue;
            }
            if (Name == QLatin1String("email") && !SawEmail) {
                SawEmail = true;
                continue;
            }
            if (Name == QLatin1String("uri") && !SawUri) {
                SawUri = true;
                continue;
            }
            return true;
        }
        if (IsCommentKind(Child) || IsPiKind(Child) || IsEntityRefKind(Child)) {
            return true;
        }
        if ((IsTextKind(Child) || IsCdataKind(Child)) &&
            !DocDocument::IsWhitespaceText(Child)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief True when a <postal> holds anything the author form does not edit.
 * @param Postal The <postal> element.
 * @return True when preserved content exists in the postal address.
 */
static bool PostalHasPreservedContent(xmlNodePtr Postal)
{
    if (Postal == nullptr) {
        return false;
    }
    if (Postal->properties != nullptr || Postal->nsDef != nullptr) {
        return true;
    }
    // A postal element is either structured parts or free-form
    // lines, never both: the first kind seen wins, and the other
    // kind stays preserved.
    bool UsesLines = false;
    bool SawKind = false;
    QSet<QString> SawParts;
    for (xmlNodePtr Child = Postal->children; Child != nullptr;
         Child = Child->next) {
        if (IsElementKind(Child)) {
            QString Name = TextFromXmlChars(Child->name);
            if (Name == QLatin1String("postalLine")) {
                if (!SawKind) {
                    UsesLines = true;
                    SawKind = true;
                }
                if (!UsesLines) {
                    return true;
                }
                if (Child->properties != nullptr || Child->nsDef != nullptr) {
                    return true;
                }
                continue;
            }
            if (IsPostalPartName(Name)) {
                if (!SawKind) {
                    UsesLines = false;
                    SawKind = true;
                }
                if (UsesLines) {
                    return true;
                }
                if (SawParts.contains(Name)) {
                    return true;
                }
                SawParts.insert(Name);
                if (Child->properties != nullptr || Child->nsDef != nullptr) {
                    return true;
                }
                continue;
            }
            return true;
        }
        if (IsCommentKind(Child) || IsPiKind(Child) || IsEntityRefKind(Child)) {
            return true;
        }
        if ((IsTextKind(Child) || IsCdataKind(Child)) &&
            !DocDocument::IsWhitespaceText(Child)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief The words of a full name, split on whitespace.
 * @param Fullname The full name.
 * @return The words, with empty parts skipped.
 */
static QStringList SplitFullnameWords(const QString& Fullname)
{
    return Fullname.trimmed().split(QRegularExpression(QStringLiteral("\\s+")),
                                    Qt::SkipEmptyParts);
}

xmlNodePtr DocDocument::AuthorNodeFromFullname(const QString& Fullname,
                                               const QString& Organization,
                                               const QString& Email)
{
    xmlNodePtr Author = xmlNewNode(nullptr, reinterpret_cast<const xmlChar*>("author"));
    if (Author == nullptr) {
        return nullptr;
    }
    QString CleanName = Fullname.trimmed();
    QString CleanOrganization = Organization.trimmed();
    QString CleanEmail = Email.trimmed();
    if (!CleanName.isEmpty()) {
        SetNodeAttribute(Author, "fullname", CleanName);
    }
    QString Initials = InitialsFromFullname(CleanName);
    if (!Initials.isEmpty()) {
        SetNodeAttribute(Author, "initials", Initials);
    }
    QString Surname = SurnameFromFullname(CleanName);
    if (!Surname.isEmpty()) {
        SetNodeAttribute(Author, "surname", Surname);
    }
    if (!CleanOrganization.isEmpty()) {
        QByteArray OrganizationUtf8 = Utf8Bytes(CleanOrganization);
        xmlNewTextChild(Author, nullptr,
                        reinterpret_cast<const xmlChar*>("organization"),
                        reinterpret_cast<const xmlChar*>(OrganizationUtf8.constData()));
    }
    if (!CleanEmail.isEmpty()) {
        xmlNodePtr Address = xmlNewChild(Author, nullptr,
            reinterpret_cast<const xmlChar*>("address"), nullptr);
        if (Address != nullptr) {
            QByteArray EmailUtf8 = Utf8Bytes(CleanEmail);
            xmlNewTextChild(Address, nullptr,
                            reinterpret_cast<const xmlChar*>("email"),
                            reinterpret_cast<const xmlChar*>(EmailUtf8.constData()));
        }
    }
    return Author;
}

QString DocDocument::SurnameFromFullname(const QString& Fullname)
{
    QStringList Words = SplitFullnameWords(Fullname);
    if (Words.isEmpty()) {
        return QString();
    }
    return Words.last();
}

QString DocDocument::InitialsFromFullname(const QString& Fullname)
{
    QStringList Words = SplitFullnameWords(Fullname);
    if (Words.size() < 2) {
        return QString();
    }
    Words.removeLast();
    QString Initials;
    for (const QString& Word : Words) {
        Initials += Word.left(1) + QStringLiteral(".");
    }
    return Initials;
}

xmlNodePtr DocDocument::ExampleAbstractNode()
{
    xmlNodePtr Paragraph = xmlNewNode(nullptr, reinterpret_cast<const xmlChar*>("t"));
    if (Paragraph == nullptr) {
        return nullptr;
    }
    SetNodeText(Paragraph, QStringLiteral(
        "This is an example of an abstract. It is a short "
        "paragraph that gives an overview of the document "
        "in order to help the reader determine whether or "
        "not they are interested in reading further."));
    return Paragraph;
}

bool DocDocument::IsRequiredSectionTitle(const QString& Title)
{
    return Title == QStringLiteral("Introduction") ||
        Title == QStringLiteral("Security Considerations") ||
        Title == QStringLiteral("IANA Considerations");
}

bool DocDocument::IsRecommendedSectionTitle(const QString& Title)
{
    return Title == QStringLiteral("Privacy Considerations") ||
        Title == QStringLiteral("Implementation Status");
}

QStringList DocDocument::SplitParagraphs(const QString& Text)
{
    QStringList Paragraphs;
    QStringList Blocks = Text.split(
        QRegularExpression(QStringLiteral("\r?\n\\s*\r?\n")),
        Qt::SkipEmptyParts);
    for (const QString& Block : Blocks) {
        QString Paragraph = Block;
        Paragraph.replace(QRegularExpression(QStringLiteral("\\s+")),
                          QStringLiteral(" "));
        Paragraph = Paragraph.trimmed();
        if (!Paragraph.isEmpty()) {
            Paragraphs.append(Paragraph);
        }
    }
    return Paragraphs;
}

QString DocDocument::JoinParagraphs(const QStringList& Paragraphs)
{
    return Paragraphs.join(QStringLiteral("\n\n"));
}

/**
 * @brief True when a node holds a comment containing "--".
 * @param Node The node to search.
 * @return True when such a comment is found below the node.
 */
static bool DescendantHoldsBadComment(xmlNodePtr Node)
{
    if (Node == nullptr) {
        return false;
    }
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if (IsCommentKind(Child)) {
            if (DocDocument::NodeText(Child).contains(QStringLiteral("--"))) {
                return true;
            }
            continue;
        }
        if (DescendantHoldsBadComment(Child)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief True when one editable body node holds a comment containing "--".
 *
 * A bare comment node is checked itself; a <t> is checked through
 * all its descendants; a list is checked through its items only,
 * since comments between items were hoisted out of the runs and
 * are not flagged. Figures, tables, and source code are not
 * checked: their comments never reached the old run walk either.
 * @param Node The body node.
 * @return True when a bad comment is found.
 */
static bool BodyNodeHoldsBadComment(xmlNodePtr Node)
{
    if (IsCommentKind(Node)) {
        return DocDocument::NodeText(Node).contains(QStringLiteral("--"));
    }
    if (!IsElementKind(Node)) {
        return false;
    }
    QString Name = TextFromXmlChars(Node->name);
    if (Name == QLatin1String("t")) {
        return DescendantHoldsBadComment(Node);
    }
    if (IsBlockListName(Name)) {
        for (xmlNodePtr Child = Node->children; Child != nullptr;
             Child = Child->next) {
            if (!IsElementKind(Child)) {
                continue;
            }
            if (DescendantHoldsBadComment(Child)) {
                return true;
            }
        }
        return false;
    }
    return false;
}

/**
 * @brief True when any body node of a parent holds a bad comment.
 * @param Parent The section, abstract, or note.
 * @return True when a bad comment is found.
 */
static bool BodyNodesHoldBadComment(xmlNodePtr Parent)
{
    for (xmlNodePtr Node : DocDocument::BodyNodes(Parent)) {
        if (BodyNodeHoldsBadComment(Node)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief True when any section holds a bad comment, recursing into children.
 * @param Sections The sections to search.
 * @return True when a bad comment is found.
 */
static bool SectionsHoldBadComment(const QVector<xmlNodePtr>& Sections)
{
    for (xmlNodePtr Section : Sections) {
        if (BodyNodesHoldBadComment(Section)) {
            return true;
        }
        if (SectionsHoldBadComment(DocDocument::ChildSectionNodes(Section))) {
            return true;
        }
    }
    return false;
}

QStringList DocDocument::Problems() const
{
    QStringList Found;
    if (DocName().trimmed().isEmpty()) {
        Found.append(QStringLiteral("The draft name is empty."));
    }
    if (Title().trimmed().isEmpty()) {
        Found.append(QStringLiteral("The title is empty."));
    }
    // The abbreviated title is mandatory and at most 40
    // characters. The edit field caps typing at 40; this check
    // covers values loaded from a file.
    if (TitleAbbrev().trimmed().isEmpty()) {
        Found.append(QStringLiteral("The abbreviated title is empty."));
    } else if (TitleAbbrev().trimmed().size() > 40) {
        Found.append(QStringLiteral("The abbreviated title is longer than 40 characters."));
    }
    // A title holding non-ASCII characters needs its ASCII form.
    bool TitleHasNonAscii = false;
    for (QChar Character : Title()) {
        if (Character.unicode() > 0x7F) {
            TitleHasNonAscii = true;
            break;
        }
    }
    if (TitleHasNonAscii && TitleAscii().trimmed().isEmpty()) {
        Found.append(QStringLiteral("The title holds non-ASCII characters, but the ASCII title is empty."));
    }
    QVector<xmlNodePtr> Authors = AuthorNodes();
    if (Authors.isEmpty()) {
        Found.append(QStringLiteral("There are no authors."));
    } else {
        for (xmlNodePtr Author : Authors) {
            if (AuthorAttribute(Author, "fullname").trimmed().isEmpty()) {
                Found.append(QStringLiteral("An author has no name."));
                break;
            }
        }
    }
    if (TopSectionNodes().isEmpty()) {
        Found.append(QStringLiteral("There are no sections."));
    }
    // Reference anchors must be present and unique among
    // themselves: the editor edits them, so a problem here is
    // fixable in the outline. Anchors on sections and other
    // preserved content are left to schema validation, since the
    // editor does not edit them.
    QSet<QString> ReferenceAnchors;
    for (xmlNodePtr Group : ReferenceGroupNodes()) {
        QString GroupAnchor = NodeAttribute(Group, "anchor").trimmed();
        if (!GroupAnchor.isEmpty()) {
            if (ReferenceAnchors.contains(GroupAnchor)) {
                Found.append(QStringLiteral("Two reference lists share the anchor \"%1\".").arg(GroupAnchor));
            } else {
                ReferenceAnchors.insert(GroupAnchor);
            }
        }
        for (xmlNodePtr Entry : ReferenceEntries(Group)) {
            QString EntryAnchor = NodeAttribute(Entry, "anchor").trimmed();
            if (EntryAnchor.isEmpty()) {
                Found.append(QStringLiteral("A reference has no anchor."));
                break;
            }
            if (ReferenceAnchors.contains(EntryAnchor)) {
                Found.append(QStringLiteral("Two references share the anchor \"%1\".").arg(EntryAnchor));
            } else {
                ReferenceAnchors.insert(EntryAnchor);
            }
        }
    }
    // A series info row needs its name and its value: the
    // schema requires both attributes. The one exception is the
    // status-row form of RFC 7991 section 2.47: a row carrying a
    // status may use an empty name. The Internet-Draft row's
    // value is generated from the draft name on save, so only
    // its name is checked here.
    bool SeriesNameMissing = false;
    bool SeriesValueMissing = false;
    for (xmlNodePtr Info : SeriesInfoNodes()) {
        bool HasStatus = HasNodeAttribute(Info, "status");
        QString InfoName = NodeAttribute(Info, "name").trimmed();
        QString InfoValue = NodeAttribute(Info, "value").trimmed();
        if (InfoName.isEmpty() && !HasStatus) {
            SeriesNameMissing = true;
        } else if (InfoValue.isEmpty() && !HasStatus &&
                   InfoName != QLatin1String("Internet-Draft")) {
            SeriesValueMissing = true;
        }
    }
    if (SeriesNameMissing) {
        Found.append(QStringLiteral("A series info row has no name."));
    }
    if (SeriesValueMissing) {
        Found.append(QStringLiteral("A series info row has no value."));
    }
    // A comment holding "--" cannot be written: XML forbids a
    // double hyphen inside a comment, and the file would not
    // parse again.
    xmlNodePtr Abstract = ChildElement(FrontNode(), "abstract");
    bool BadComment = false;
    if (Abstract != nullptr && BodyNodesHoldBadComment(Abstract)) {
        BadComment = true;
    }
    if (!BadComment && SectionsHoldBadComment(TopSectionNodes())) {
        BadComment = true;
    }
    if (!BadComment) {
        for (xmlNodePtr Note : NoteNodes()) {
            if (BodyNodesHoldBadComment(Note)) {
                BadComment = true;
                break;
            }
        }
    }
    if (BadComment) {
        Found.append(QStringLiteral(
            "A comment contains \"--\", which XML does not allow inside a comment."));
    }
    return Found;
}

/**
 * @brief The display location for a literal line: the section
 *        path when one is known, else the fallback label.
 * @param Path The section path, empty outside sections.
 * @param Label The fallback, such as the abstract or the back matter.
 * @return The location phrase for the message.
 */
static QString LiteralLineLocation(const QString& Path, const QString& Label)
{
    if (Path.isEmpty()) {
        return Label;
    }
    return QStringLiteral("section \"%1\"").arg(Path);
}

/**
 * @brief Appends one description per over-long line in literal text.
 *
 * Lines split on \n; a trailing \r is stripped before counting,
 * since loaded content may carry CRLF endings. Line numbers
 * count from 1 within the block, blank lines included.
 * @param Text The literal text to check.
 * @param Kind The block kind for the message, such as "sourcecode".
 * @param Location The display location for the message.
 * @param Found The descriptions, appended to.
 */
static void CollectLongLiteralLines(const QString& Text, const QString& Kind,
                                    const QString& Location, QStringList& Found)
{
    const QStringList Lines = Text.split(QLatin1Char('\n'));
    for (int Index = 0; Index < Lines.size(); ++Index) {
        QString Line = Lines.at(Index);
        if (Line.endsWith(QLatin1Char('\r'))) {
            Line.chop(1);
        }
        if (Line.size() > MaxLiteralLineLength) {
            Found.append(QStringLiteral("%1 in %2, line %3: %4 characters (limit %5).")
                             .arg(Kind, Location, QString::number(Index + 1),
                                  QString::number(Line.size()),
                                  QString::number(MaxLiteralLineLength)));
        }
    }
}

/**
 * @brief The literal text of an element: its direct text and CDATA
 *        children, joined in order.
 * @param Node The element.
 * @return The text; comments and other children add nothing.
 */
static QString DirectTextContent(xmlNodePtr Node)
{
    QString Text;
    if (Node == nullptr) {
        return Text;
    }
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if (IsTextKind(Child) || IsCdataKind(Child)) {
            Text += DocDocument::NodeText(Child);
        }
    }
    return Text;
}

/**
 * @brief Appends descriptions for over-long lines in a preserved tree.
 *
 * A <sourcecode> element contributes its text, unless a src
 * attribute says the content lives in another file. An <artwork>
 * element contributes its text only when it holds no element
 * (an <svg> makes it an image) and its type is absent or
 * ascii-art. Any other element is walked for nested literal
 * blocks; entering a <section> with a name retitles the location
 * to that section, so appendix content names its appendix.
 * @param Node The node to search.
 * @param Path The section path so far, empty outside sections.
 * @param Label The fallback location when no section is known.
 * @param Found The descriptions, appended to.
 */
static void CollectPreservedLiteralLines(xmlNodePtr Node, const QString& Path,
                                         const QString& Label,
                                         QStringList& Found)
{
    if (!IsElementKind(Node)) {
        return;
    }
    QString Name = TextFromXmlChars(Node->name);
    if (Name == QLatin1String("sourcecode")) {
        if (!DocDocument::HasNodeAttribute(Node, "src")) {
            CollectLongLiteralLines(DirectTextContent(Node),
                                    QStringLiteral("sourcecode"),
                                    LiteralLineLocation(Path, Label), Found);
        }
        return;
    }
    if (Name == QLatin1String("artwork")) {
        bool HasElement = false;
        for (xmlNodePtr Child = Node->children; Child != nullptr;
             Child = Child->next) {
            if (IsElementKind(Child)) {
                HasElement = true;
                break;
            }
        }
        QString Type = DocDocument::NodeAttribute(Node, "type");
        if (!HasElement &&
            !DocDocument::HasNodeAttribute(Node, "src") &&
            (Type.isEmpty() || Type == QLatin1String("ascii-art"))) {
            CollectLongLiteralLines(DirectTextContent(Node),
                                    QStringLiteral("artwork"),
                                    LiteralLineLocation(Path, Label), Found);
        }
        return;
    }
    QString ChildPath = Path;
    if (Name == QLatin1String("section")) {
        QString Title = DocDocument::SectionTitle(Node);
        if (!Title.isEmpty()) {
            ChildPath = Path.isEmpty()
                ? Title : Path + QStringLiteral(" > ") + Title;
        }
    }
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        CollectPreservedLiteralLines(Child, ChildPath, Label, Found);
    }
}

/**
 * @brief Appends descriptions for over-long lines in one modeled body node.
 *
 * A <sourcecode> node contributes its literal text. A figure,
 * artset, or artwork node contributes its ASCII-art versions:
 * SVG versions are skipped, and so are versions of another type,
 * such as binary, whose text is not rendered as literal lines.
 * Any other modeled node holds prose, which xml2rfc re-wraps.
 * @param Node The body node.
 * @param Location The display location for the message.
 * @param Found The descriptions, appended to.
 */
static void CollectModeledLiteralLines(xmlNodePtr Node, const QString& Location,
                                       QStringList& Found)
{
    if (!IsElementKind(Node)) {
        return;
    }
    QString Name = TextFromXmlChars(Node->name);
    if (Name == QLatin1String("sourcecode")) {
        if (!DocDocument::HasNodeAttribute(Node, "src")) {
            CollectLongLiteralLines(DocDocument::NodeText(Node),
                                    QStringLiteral("sourcecode"),
                                    Location, Found);
        }
        return;
    }
    QVector<xmlNodePtr> Artworks;
    if (Name == QLatin1String("artwork")) {
        Artworks.append(Node);
    } else if (Name == QLatin1String("artset")) {
        for (xmlNodePtr Child = Node->children; Child != nullptr;
             Child = Child->next) {
            if (IsElementKind(Child) &&
                DocDocument::NodeNameEquals(Child, "artwork")) {
                Artworks.append(Child);
            }
        }
    } else if (Name == QLatin1String("figure")) {
        for (xmlNodePtr Child = Node->children; Child != nullptr;
             Child = Child->next) {
            if (!IsElementKind(Child)) {
                continue;
            }
            if (DocDocument::NodeNameEquals(Child, "artwork")) {
                Artworks.append(Child);
            } else if (DocDocument::NodeNameEquals(Child, "artset")) {
                for (xmlNodePtr Version = Child->children; Version != nullptr;
                     Version = Version->next) {
                    if (IsElementKind(Version) &&
                        DocDocument::NodeNameEquals(Version, "artwork")) {
                        Artworks.append(Version);
                    }
                }
            }
        }
    } else {
        return;
    }
    for (int Index = 0; Index < Artworks.size(); ++Index) {
        xmlNodePtr Artwork = Artworks.at(Index);
        bool HasElement = false;
        for (xmlNodePtr Child = Artwork->children; Child != nullptr;
             Child = Child->next) {
            if (IsElementKind(Child)) {
                HasElement = true;
                break;
            }
        }
        QString Type = DocDocument::NodeAttribute(Artwork, "type");
        if (HasElement || (!Type.isEmpty() &&
                           Type != QLatin1String("ascii-art"))) {
            continue;
        }
        QString Kind = QStringLiteral("artwork");
        if (Artworks.size() > 1) {
            Kind = QStringLiteral("artwork version %1").arg(Index + 1);
        }
        CollectLongLiteralLines(DirectTextContent(Artwork), Kind, Location,
                                Found);
    }
}

/**
 * @brief Appends descriptions for over-long lines in sections.
 *
 * Each section contributes its body nodes and its preserved
 * nodes, then its children, with the section path extended.
 * A section without a title shows as "(no title)" in the path.
 * @param Sections The sections to search.
 * @param ParentPath The enclosing section path, empty at the top.
 * @param Found The descriptions, appended to.
 */
static void CollectSectionLiteralLines(const QVector<xmlNodePtr>& Sections,
                                       const QString& ParentPath,
                                       QStringList& Found)
{
    for (xmlNodePtr Section : Sections) {
        QString Title = DocDocument::SectionTitle(Section).trimmed();
        if (Title.isEmpty()) {
            Title = QStringLiteral("(no title)");
        }
        QString Path = ParentPath.isEmpty()
            ? Title : ParentPath + QStringLiteral(" > ") + Title;
        QString Location = LiteralLineLocation(Path, QString());
        for (xmlNodePtr Node : DocDocument::BodyNodes(Section)) {
            CollectModeledLiteralLines(Node, Location, Found);
        }
        for (const DocPreservedItem& Item :
             DocDocument::PreservedBodyNodes(Section)) {
            CollectPreservedLiteralLines(Item.Node, Path, QString(), Found);
        }
        CollectSectionLiteralLines(DocDocument::ChildSectionNodes(Section),
                                   Path, Found);
    }
}

/**
 * @brief True when a child of a parent is a modeled front child.
 *
 * The modeled set is title, seriesInfo, author, date, area,
 * workgroup, keyword, abstract, and note. Only the first title
 * and the first abstract count as modeled; later ones are
 * preserved, as the old reader kept only the first.
 * @param Child The child node.
 * @param SawTitle Set to true once a title has been seen.
 * @param SawAbstract Set to true once an abstract has been seen.
 * @return True when the child is modeled front content.
 */
static bool IsModeledFrontChild(xmlNodePtr Child, bool& SawTitle,
                                bool& SawAbstract)
{
    if (!IsElementKind(Child)) {
        return false;
    }
    QString Name = TextFromXmlChars(Child->name);
    if (Name == QLatin1String("title")) {
        if (SawTitle) {
            return false;
        }
        SawTitle = true;
        return true;
    }
    if (Name == QLatin1String("abstract")) {
        if (SawAbstract) {
            return false;
        }
        SawAbstract = true;
        return true;
    }
    return Name == QLatin1String("seriesInfo") ||
        Name == QLatin1String("author") ||
        Name == QLatin1String("date") ||
        Name == QLatin1String("area") ||
        Name == QLatin1String("workgroup") ||
        Name == QLatin1String("keyword") ||
        Name == QLatin1String("note");
}

QStringList DocDocument::LongLiteralLines() const
{
    QStringList Found;
    xmlNodePtr Rfc = RfcNode();
    if (Rfc == nullptr) {
        return Found;
    }
    // Document order: the <rfc> level, the front matter, the
    // abstract, the notes, the middle sections, the unmodeled
    // middle items, the references, and the back matter.
    bool SawFront = false;
    bool SawMiddle = false;
    bool SawBack = false;
    for (xmlNodePtr Child = Rfc->children; Child != nullptr;
         Child = Child->next) {
        if (!IsElementKind(Child)) {
            continue;
        }
        QString Name = TextFromXmlChars(Child->name);
        if (Name == QLatin1String("front") && !SawFront) {
            SawFront = true;
            continue;
        }
        if (Name == QLatin1String("middle") && !SawMiddle) {
            SawMiddle = true;
            continue;
        }
        if (Name == QLatin1String("back") && !SawBack) {
            SawBack = true;
            continue;
        }
        CollectPreservedLiteralLines(Child, QString(),
                                     QStringLiteral("the document"), Found);
    }
    xmlNodePtr Front = FrontNode();
    if (Front != nullptr) {
        bool SawTitle = false;
        bool SawAbstract = false;
        for (xmlNodePtr Child = Front->children; Child != nullptr;
             Child = Child->next) {
            if (!IsElementKind(Child)) {
                continue;
            }
            if (IsModeledFrontChild(Child, SawTitle, SawAbstract)) {
                continue;
            }
            CollectPreservedLiteralLines(Child, QString(),
                                         QStringLiteral("the front matter"),
                                         Found);
        }
    }
    xmlNodePtr Abstract = ChildElement(FrontNode(), "abstract");
    if (Abstract != nullptr) {
        QString AbstractLocation = QStringLiteral("the abstract");
        for (xmlNodePtr Node : BodyNodes(Abstract)) {
            CollectModeledLiteralLines(Node, AbstractLocation, Found);
        }
        for (const DocPreservedItem& Item : PreservedBodyNodes(Abstract)) {
            CollectPreservedLiteralLines(Item.Node, QString(),
                                         AbstractLocation, Found);
        }
    }
    for (xmlNodePtr Note : NoteNodes()) {
        QString NoteName = DocDocument::SectionTitle(Note);
        QString Location = NoteName.trimmed().isEmpty()
            ? QStringLiteral("a note")
            : QStringLiteral("note \"%1\"").arg(NoteName.trimmed());
        for (xmlNodePtr Node : BodyNodes(Note)) {
            CollectModeledLiteralLines(Node, Location, Found);
        }
        for (const DocPreservedItem& Item : PreservedBodyNodes(Note)) {
            CollectPreservedLiteralLines(Item.Node, QString(), Location, Found);
        }
    }
    CollectSectionLiteralLines(TopSectionNodes(), QString(), Found);
    xmlNodePtr Middle = MiddleNode();
    if (Middle != nullptr) {
        for (xmlNodePtr Child = Middle->children; Child != nullptr;
             Child = Child->next) {
            if (!IsElementKind(Child)) {
                continue;
            }
            if (NodeNameEquals(Child, "section")) {
                continue;
            }
            CollectPreservedLiteralLines(Child, QString(),
                                         QStringLiteral("the middle matter"),
                                         Found);
        }
    }
    for (xmlNodePtr Group : ReferenceGroupNodes()) {
        QString GroupName = SectionTitle(Group);
        QString GroupLabel = GroupName.trimmed().isEmpty()
            ? QStringLiteral("a reference list")
            : QStringLiteral("reference list \"%1\"").arg(GroupName.trimmed());
        for (xmlNodePtr Child = Group->children; Child != nullptr;
             Child = Child->next) {
            if (!IsElementKind(Child)) {
                continue;
            }
            if (NodeNameEquals(Child, "reference") ||
                NodeNameEquals(Child, "name")) {
                continue;
            }
            CollectPreservedLiteralLines(Child, QString(), GroupLabel, Found);
        }
        for (xmlNodePtr Entry : ReferenceEntries(Group)) {
            QString EntryAnchor = NodeAttribute(Entry, "anchor").trimmed();
            QString EntryLabel = EntryAnchor.isEmpty()
                ? QStringLiteral("a reference")
                : QStringLiteral("reference \"%1\"").arg(EntryAnchor);
            xmlNodePtr EntryFront = ChildElement(Entry, "front");
            if (EntryFront != nullptr) {
                bool SawEntryTitle = false;
                bool SawEntryDate = false;
                for (xmlNodePtr Child = EntryFront->children; Child != nullptr;
                     Child = Child->next) {
                    if (!IsElementKind(Child)) {
                        continue;
                    }
                    QString Name = TextFromXmlChars(Child->name);
                    if (Name == QLatin1String("title")) {
                        if (SawEntryTitle) {
                            CollectPreservedLiteralLines(Child, QString(),
                                                         EntryLabel, Found);
                        } else {
                            SawEntryTitle = true;
                        }
                        continue;
                    }
                    if (Name == QLatin1String("date")) {
                        if (SawEntryDate) {
                            CollectPreservedLiteralLines(Child, QString(),
                                                         EntryLabel, Found);
                        } else {
                            SawEntryDate = true;
                        }
                        continue;
                    }
                    if (Name == QLatin1String("seriesInfo") ||
                        Name == QLatin1String("author")) {
                        continue;
                    }
                    CollectPreservedLiteralLines(Child, QString(), EntryLabel,
                                                 Found);
                }
            }
            bool SawStream = false;
            for (xmlNodePtr Child = Entry->children; Child != nullptr;
                 Child = Child->next) {
                if (!IsElementKind(Child)) {
                    continue;
                }
                QString Name = TextFromXmlChars(Child->name);
                if (Name == QLatin1String("front")) {
                    continue;
                }
                if (Name == QLatin1String("stream") && !SawStream) {
                    SawStream = true;
                    continue;
                }
                CollectPreservedLiteralLines(Child, QString(), EntryLabel,
                                             Found);
            }
        }
    }
    xmlNodePtr Back = BackNode();
    if (Back != nullptr) {
        for (xmlNodePtr Child = Back->children; Child != nullptr;
             Child = Child->next) {
            if (!IsElementKind(Child)) {
                continue;
            }
            if (NodeNameEquals(Child, "references")) {
                continue;
            }
            CollectPreservedLiteralLines(Child, QString(),
                                         QStringLiteral("the back matter"),
                                         Found);
        }
    }
    return Found;
}

/**
 * @brief True when a node carries an attribute outside a modeled set.
 * @param Node The element.
 * @param Modeled The modeled attribute names.
 * @return True when an extra attribute exists.
 */
static bool NodeHasExtraAttribute(xmlNodePtr Node,
                                  const QVector<const char*>& Modeled)
{
    if (Node == nullptr) {
        return false;
    }
    for (xmlAttrPtr Attribute = Node->properties; Attribute != nullptr;
         Attribute = Attribute->next) {
        QString Name = TextFromXmlChars(Attribute->name);
        bool Known = false;
        for (const char* Wanted : Modeled) {
            if (Name == QLatin1String(Wanted)) {
                Known = true;
                break;
            }
        }
        if (!Known) {
            return true;
        }
    }
    return false;
}

/**
 * @brief True when a section or any child section holds preserved content.
 * @param Section The section element.
 * @return True when preserved content exists here or below.
 */
static bool SectionHasPreservedContent(xmlNodePtr Section)
{
    if (Section == nullptr) {
        return false;
    }
    if (Section->properties != nullptr || Section->nsDef != nullptr) {
        return true;
    }
    if (!DocDocument::PreservedBodyNodes(Section).isEmpty()) {
        return true;
    }
    for (xmlNodePtr Child : DocDocument::ChildSectionNodes(Section)) {
        if (SectionHasPreservedContent(Child)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief True when a note holds preserved content.
 * @param Note The note element.
 * @return True when preserved attributes or blocks exist.
 */
static bool NoteHasPreservedContent(xmlNodePtr Note)
{
    if (Note == nullptr) {
        return false;
    }
    for (xmlAttrPtr Attribute = Note->properties; Attribute != nullptr;
         Attribute = Attribute->next) {
        QString Name = TextFromXmlChars(Attribute->name);
        if (Name != QLatin1String("removeInRFC")) {
            return true;
        }
    }
    if (Note->nsDef != nullptr) {
        return true;
    }
    xmlNodePtr NameNode = DocDocument::ChildElement(Note, "name");
    if (NameNode != nullptr &&
        (NameNode->properties != nullptr || NameNode->nsDef != nullptr)) {
        return true;
    }
    return !DocDocument::PreservedBodyNodes(Note).isEmpty();
}

/**
 * @brief True when a reference entry holds preserved content.
 * @param Entry The <reference> element.
 * @return True when preserved content exists in the entry.
 */
static bool ReferenceHasPreservedContent(xmlNodePtr Entry)
{
    if (Entry == nullptr) {
        return false;
    }
    if (NodeHasExtraAttribute(Entry, { "anchor", "target" }) ||
        Entry->nsDef != nullptr) {
        return true;
    }
    xmlNodePtr Front = DocDocument::ChildElement(Entry, "front");
    if (Front != nullptr) {
        if (Front->properties != nullptr || Front->nsDef != nullptr) {
            return true;
        }
        bool SawTitle = false;
        bool SawDate = false;
        for (xmlNodePtr Child = Front->children; Child != nullptr;
             Child = Child->next) {
            if (IsElementKind(Child)) {
                QString Name = TextFromXmlChars(Child->name);
                if (Name == QLatin1String("title")) {
                    if (SawTitle) {
                        return true;
                    }
                    SawTitle = true;
                    continue;
                }
                if (Name == QLatin1String("seriesInfo")) {
                    if (NodeHasExtraAttribute(Child, { "name", "value" }) ||
                        Child->nsDef != nullptr) {
                        return true;
                    }
                    continue;
                }
                if (Name == QLatin1String("author")) {
                    if (DocDocument::AuthorHasPreservedContent(Child)) {
                        return true;
                    }
                    continue;
                }
                if (Name == QLatin1String("date")) {
                    if (SawDate) {
                        return true;
                    }
                    SawDate = true;
                    if (NodeHasExtraAttribute(Child,
                                              { "day", "month", "year" }) ||
                        Child->nsDef != nullptr) {
                        return true;
                    }
                    continue;
                }
                return true;
            }
            if (IsCommentKind(Child) || IsPiKind(Child) ||
                IsEntityRefKind(Child)) {
                return true;
            }
            if ((IsTextKind(Child) || IsCdataKind(Child)) &&
                !DocDocument::IsWhitespaceText(Child)) {
                return true;
            }
        }
    }
    bool SawStream = false;
    for (xmlNodePtr Child = Entry->children; Child != nullptr;
         Child = Child->next) {
        if (IsElementKind(Child)) {
            QString Name = TextFromXmlChars(Child->name);
            if (Name == QLatin1String("front")) {
                continue;
            }
            if (Name == QLatin1String("stream") && !SawStream) {
                SawStream = true;
                continue;
            }
            return true;
        }
        if (IsCommentKind(Child) || IsPiKind(Child) || IsEntityRefKind(Child)) {
            return true;
        }
        if ((IsTextKind(Child) || IsCdataKind(Child)) &&
            !DocDocument::IsWhitespaceText(Child)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief True when a reference group holds preserved content.
 * @param Group The <references> element.
 * @return True when preserved content exists in the group.
 */
static bool ReferenceGroupHasPreservedContent(xmlNodePtr Group)
{
    if (Group == nullptr) {
        return false;
    }
    if (NodeHasExtraAttribute(Group, { "anchor" }) ||
        Group->nsDef != nullptr) {
        return true;
    }
    for (xmlNodePtr Child = Group->children; Child != nullptr;
         Child = Child->next) {
        if (IsElementKind(Child)) {
            QString Name = TextFromXmlChars(Child->name);
            if (Name == QLatin1String("reference")) {
                if (ReferenceHasPreservedContent(Child)) {
                    return true;
                }
                continue;
            }
            if (Name == QLatin1String("name")) {
                continue;
            }
            return true;
        }
        if (IsCommentKind(Child) || IsPiKind(Child) || IsEntityRefKind(Child)) {
            return true;
        }
        if ((IsTextKind(Child) || IsCdataKind(Child)) &&
            !DocDocument::IsWhitespaceText(Child)) {
            return true;
        }
    }
    return false;
}

bool DocDocument::HasPreservedContent() const
{
    xmlNodePtr Rfc = RfcNode();
    if (Rfc == nullptr) {
        return false;
    }
    // The modeled <rfc> attributes are docName, category, ipr,
    // and submissionType: the old writer modeled no others.
    if (NodeHasExtraAttribute(Rfc, { "docName", "category", "ipr",
                                     "submissionType" }) ||
        Rfc->nsDef != nullptr) {
        return true;
    }
    bool SawFront = false;
    bool SawMiddle = false;
    bool SawBack = false;
    for (xmlNodePtr Child = Rfc->children; Child != nullptr;
         Child = Child->next) {
        if (IsElementKind(Child)) {
            QString Name = TextFromXmlChars(Child->name);
            if (Name == QLatin1String("front") && !SawFront) {
                SawFront = true;
                continue;
            }
            if (Name == QLatin1String("middle") && !SawMiddle) {
                SawMiddle = true;
                continue;
            }
            if (Name == QLatin1String("back") && !SawBack) {
                SawBack = true;
                continue;
            }
            return true;
        }
        if (IsCommentKind(Child) || IsPiKind(Child) || IsEntityRefKind(Child)) {
            return true;
        }
    }
    xmlNodePtr Front = FrontNode();
    if (Front != nullptr) {
        if (Front->properties != nullptr || Front->nsDef != nullptr) {
            return true;
        }
        bool SawTitle = false;
        bool SawAbstract = false;
        for (xmlNodePtr Child = Front->children; Child != nullptr;
             Child = Child->next) {
            if (IsElementKind(Child)) {
                if (IsModeledFrontChild(Child, SawTitle, SawAbstract)) {
                    QString Name = TextFromXmlChars(Child->name);
                    if (Name == QLatin1String("seriesInfo")) {
                        if (NodeHasExtraAttribute(Child, { "name", "value" }) ||
                            Child->nsDef != nullptr) {
                            return true;
                        }
                    } else if (Name == QLatin1String("area") ||
                               Name == QLatin1String("workgroup") ||
                               Name == QLatin1String("keyword")) {
                        if (Child->properties != nullptr ||
                            Child->nsDef != nullptr) {
                            return true;
                        }
                    } else if (Name == QLatin1String("note")) {
                        if (NoteHasPreservedContent(Child)) {
                            return true;
                        }
                    } else if (Name == QLatin1String("abstract")) {
                        if (!PreservedBodyNodes(Child).isEmpty()) {
                            return true;
                        }
                        if (Child->properties != nullptr ||
                            Child->nsDef != nullptr) {
                            return true;
                        }
                    }
                    continue;
                }
                return true;
            }
            if (IsCommentKind(Child) || IsPiKind(Child) ||
                IsEntityRefKind(Child)) {
                return true;
            }
            if ((IsTextKind(Child) || IsCdataKind(Child)) &&
                !IsWhitespaceText(Child)) {
                return true;
            }
        }
        for (xmlNodePtr Author : AuthorNodes()) {
            if (AuthorHasPreservedContent(Author)) {
                return true;
            }
        }
    }
    xmlNodePtr Middle = MiddleNode();
    if (Middle != nullptr) {
        if (Middle->properties != nullptr || Middle->nsDef != nullptr) {
            return true;
        }
        for (xmlNodePtr Child = Middle->children; Child != nullptr;
             Child = Child->next) {
            if (IsElementKind(Child)) {
                if (NodeNameEquals(Child, "section")) {
                    if (SectionHasPreservedContent(Child)) {
                        return true;
                    }
                    continue;
                }
                return true;
            }
            if (IsCommentKind(Child) || IsPiKind(Child) ||
                IsEntityRefKind(Child)) {
                return true;
            }
            if ((IsTextKind(Child) || IsCdataKind(Child)) &&
                !IsWhitespaceText(Child)) {
                return true;
            }
        }
    }
    xmlNodePtr Back = BackNode();
    if (Back != nullptr) {
        if (Back->properties != nullptr || Back->nsDef != nullptr) {
            return true;
        }
        for (xmlNodePtr Child = Back->children; Child != nullptr;
             Child = Child->next) {
            if (IsElementKind(Child)) {
                if (NodeNameEquals(Child, "references")) {
                    if (ReferenceGroupHasPreservedContent(Child)) {
                        return true;
                    }
                    continue;
                }
                return true;
            }
            if (IsCommentKind(Child) || IsPiKind(Child) ||
                IsEntityRefKind(Child)) {
                return true;
            }
            if ((IsTextKind(Child) || IsCdataKind(Child)) &&
                !IsWhitespaceText(Child)) {
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief Appends text to a libxml2 buffer.
 * @param Buffer The buffer.
 * @param Text The text to append.
 */
static void AppendToBuffer(xmlBufferPtr Buffer, const QString& Text)
{
    if (Buffer == nullptr) {
        return;
    }
    QByteArray TextUtf8 = Utf8Bytes(Text);
    xmlBufferAdd(Buffer, reinterpret_cast<const xmlChar*>(TextUtf8.constData()),
                 TextUtf8.size());
}

/**
 * @brief The content of a libxml2 buffer as a QString.
 * @param Buffer The buffer.
 * @return The content.
 */
static QString BufferText(xmlBufferPtr Buffer)
{
    if (Buffer == nullptr) {
        return QString();
    }
    const xmlChar* Content = xmlBufferContent(Buffer);
    if (Content == nullptr) {
        return QString();
    }
    return TextFromXmlChars(Content);
}

/**
 * @brief Dumps one node into a buffer, inline, with no reflow.
 * @param Buffer The buffer.
 * @param Node The node to dump.
 */
static void DumpNode(xmlBufferPtr Buffer, xmlNodePtr Node)
{
    if (Buffer == nullptr || Node == nullptr) {
        return;
    }
    xmlNodeDump(Buffer, Node->doc, Node, 0, 0);
}

/**
 * @brief The qualified name of a node: prefix:name, or the plain name.
 * @param Node The node.
 * @return The qualified name.
 */
static QString QualifiedName(xmlNodePtr Node)
{
    if (Node == nullptr) {
        return QString();
    }
    QString Name = TextFromXmlChars(Node->name);
    if (Node->ns != nullptr && Node->ns->prefix != nullptr) {
        return TextFromXmlChars(Node->ns->prefix) + QStringLiteral(":") + Name;
    }
    return Name;
}

/**
 * @brief The qualified name of an attribute: prefix:name, or the plain name.
 * @param Attribute The attribute.
 * @return The qualified name.
 */
static QString QualifiedAttributeName(xmlAttrPtr Attribute)
{
    if (Attribute == nullptr) {
        return QString();
    }
    QString Name = TextFromXmlChars(Attribute->name);
    if (Attribute->ns != nullptr && Attribute->ns->prefix != nullptr) {
        return TextFromXmlChars(Attribute->ns->prefix) + QStringLiteral(":") + Name;
    }
    return Name;
}

/**
 * @brief Escapes a value for an attribute in double quotes.
 * @param Value The value.
 * @return The escaped value.
 */
static QString EscapeAttributeValue(const QString& Value)
{
    QString Escaped = Value;
    Escaped.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    Escaped.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    Escaped.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    Escaped.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    Escaped.replace(QLatin1Char('\t'), QStringLiteral("&#9;"));
    Escaped.replace(QLatin1Char('\n'), QStringLiteral("&#10;"));
    Escaped.replace(QLatin1Char('\r'), QStringLiteral("&#13;"));
    return Escaped;
}

/**
 * @brief The open tag of an element, or its self-closing tag.
 * @param Node The element.
 * @param SelfClosing True for a self-closing tag.
 * @return The tag text.
 */
static QString OpenTagText(xmlNodePtr Node, bool SelfClosing)
{
    QString Tag = QStringLiteral("<") + QualifiedName(Node);
    for (xmlNsPtr Ns = Node->nsDef; Ns != nullptr; Ns = Ns->next) {
        QString Prefix = TextFromXmlChars(Ns->prefix);
        QString Href = TextFromXmlChars(Ns->href);
        if (Prefix.isEmpty()) {
            Tag += QStringLiteral(" xmlns=\"") + EscapeAttributeValue(Href) +
                   QStringLiteral("\"");
        } else {
            Tag += QStringLiteral(" xmlns:") + Prefix + QStringLiteral("=\"") +
                   EscapeAttributeValue(Href) + QStringLiteral("\"");
        }
    }
    for (xmlAttrPtr Attribute = Node->properties; Attribute != nullptr;
         Attribute = Attribute->next) {
        QByteArray NameUtf8 = Utf8Bytes(TextFromXmlChars(Attribute->name));
        QString Value = DocDocument::NodeAttribute(Node, NameUtf8.constData());
        Tag += QStringLiteral(" ") + QualifiedAttributeName(Attribute) +
               QStringLiteral("=\"") + EscapeAttributeValue(Value) +
               QStringLiteral("\"");
    }
    Tag += SelfClosing ? QStringLiteral("/>") : QStringLiteral(">");
    return Tag;
}

/**
 * @brief The indentation for a depth: four spaces per depth.
 * @param Depth The depth.
 * @return The indentation.
 */
static QString IndentText(int Depth)
{
    return QString(4 * Depth, QLatin1Char(' '));
}

/**
 * @brief Tests whether a name is a block-level container.
 *
 * Block-level containers get a newline and indent before each
 * child element and before their closing tag. Every other
 * element is mixed content and is written inline.
 * @param Name The element name.
 * @return True for a block-level container.
 */
static bool IsBlockContainerName(const QString& Name)
{
    static const char* Names[] = {
        "rfc", "front", "middle", "back", "section", "references",
        "reference", "abstract", "note", "table", "thead", "tbody",
        "tfoot", "tr", "figure", "artset", "dl", "ul", "ol", "address",
        "postal", "author"
    };
    for (const char* Known : Names) {
        if (Name == QLatin1String(Known)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief True when an <abstract> holds anything worth writing.
 *
 * Any child that is not a blank <t> counts: a non-blank element,
 * a comment, a processing instruction, an entity reference, or
 * non-whitespace text. An abstract holding only blank lines is
 * empty, not content: writing it would leave an empty
 * <abstract>, which the schema rejects.
 * @param Node The <abstract> element.
 * @return True when the abstract must be written.
 */
static bool AbstractHasTreeContent(xmlNodePtr Node)
{
    if (Node == nullptr) {
        return false;
    }
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if (IsElementKind(Child)) {
            if (DocDocument::NodeNameEquals(Child, "t") &&
                TElementIsBlank(Child)) {
                continue;
            }
            return true;
        }
        if (IsCommentKind(Child) || IsPiKind(Child) || IsEntityRefKind(Child)) {
            return true;
        }
        if ((IsTextKind(Child) || IsCdataKind(Child)) &&
            !DocDocument::IsWhitespaceText(Child)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief True when a <note> holds anything worth writing.
 *
 * A name, a kept attribute other than removeInRFC, or any body
 * child that is not a blank <t> counts. A note whose body is all
 * blank lines holds nothing: writing it would leave an empty
 * <note>, which the schema rejects, so such a note is skipped
 * on save, the same way an empty abstract is skipped.
 * @param Note The <note> element.
 * @return True when the note must be written.
 */
static bool NoteHasTreeContent(xmlNodePtr Note)
{
    if (Note == nullptr) {
        return false;
    }
    xmlNodePtr NameNode = DocDocument::ChildElement(Note, "name");
    if (NameNode != nullptr &&
        !DocDocument::NodeText(NameNode).trimmed().isEmpty()) {
        return true;
    }
    for (xmlAttrPtr Attribute = Note->properties; Attribute != nullptr;
         Attribute = Attribute->next) {
        if (TextFromXmlChars(Attribute->name) != QLatin1String("removeInRFC")) {
            return true;
        }
    }
    for (xmlNodePtr Child = Note->children; Child != nullptr;
         Child = Child->next) {
        if (IsElementKind(Child)) {
            if (DocDocument::NodeNameEquals(Child, "name")) {
                continue;
            }
            if (DocDocument::NodeNameEquals(Child, "t") &&
                TElementIsBlank(Child)) {
                continue;
            }
            return true;
        }
        if (IsCommentKind(Child) || IsPiKind(Child) || IsEntityRefKind(Child)) {
            return true;
        }
        if ((IsTextKind(Child) || IsCdataKind(Child)) &&
            !DocDocument::IsWhitespaceText(Child)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Serializes one element into a buffer, recursively.
 *
 * Block-level containers are pretty-printed: a newline and
 * indent before each child element and before the closing tag,
 * at four spaces per depth. Mixed-content elements are dumped
 * whole with xmlNodeDump, format 0, so no whitespace is added
 * inside them. Whitespace-only text nodes between block-level
 * siblings are skipped; the writer adds its own indentation.
 * A blank <t> writes as a blank line, and a comments-only <t>
 * writes as its bare comments.
 * @param Buffer The buffer.
 * @param Node The element.
 * @param Depth The indent depth of the element.
 */
static void SerializeElementInto(xmlBufferPtr Buffer, xmlNodePtr Node,
                                 int Depth)
{
    if (Buffer == nullptr || !IsElementKind(Node)) {
        return;
    }
    QString Name = TextFromXmlChars(Node->name);
    if (!IsBlockContainerName(Name)) {
        DumpNode(Buffer, Node);
        return;
    }
    if (Name == QLatin1String("abstract") && !AbstractHasTreeContent(Node)) {
        return;
    }
    if (Name == QLatin1String("note") && !NoteHasTreeContent(Node)) {
        return;
    }
    QVector<xmlNodePtr> Renderable;
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if (DocDocument::IsWhitespaceText(Child)) {
            continue;
        }
        Renderable.append(Child);
    }
    if (Renderable.isEmpty()) {
        AppendToBuffer(Buffer, OpenTagText(Node, true));
        return;
    }
    AppendToBuffer(Buffer, OpenTagText(Node, false));
    bool HasBodyChild = false;
    for (xmlNodePtr Child : Renderable) {
        if (IsElementKind(Child)) {
            QString ChildName = TextFromXmlChars(Child->name);
            if (ChildName != QLatin1String("name")) {
                HasBodyChild = true;
            }
            if (ChildName == QLatin1String("t") && TElementIsBlank(Child)) {
                AppendToBuffer(Buffer, QStringLiteral("\n"));
                continue;
            }
            AppendToBuffer(Buffer, QStringLiteral("\n") + IndentText(Depth + 1));
            if (ChildName == QLatin1String("t") &&
                CommentsOnlyTElement(Child)) {
                for (xmlNodePtr Comment = Child->children; Comment != nullptr;
                     Comment = Comment->next) {
                    if (IsCommentKind(Comment)) {
                        DumpNode(Buffer, Comment);
                    }
                }
                continue;
            }
            SerializeElementInto(Buffer, Child, Depth + 1);
        } else {
            AppendToBuffer(Buffer, QStringLiteral("\n") + IndentText(Depth + 1));
            DumpNode(Buffer, Child);
        }
    }
    if (Name == QLatin1String("note") && !HasBodyChild) {
        // A note with a name but no body writes one empty <t>:
        // the schema requires at least one block.
        AppendToBuffer(Buffer, QStringLiteral("\n") + IndentText(Depth + 1) +
                                QStringLiteral("<t/>"));
    }
    AppendToBuffer(Buffer, QStringLiteral("\n") + IndentText(Depth) +
                            QStringLiteral("</") + QualifiedName(Node) +
                            QStringLiteral(">"));
}

QString DocDocument::SerializeNode(xmlNodePtr Node)
{
    if (Node == nullptr) {
        return QString();
    }
    if (IsElementKind(Node) && NodeNameEquals(Node, "t") &&
        TElementIsBlank(Node)) {
        return QString();
    }
    xmlBufferPtr Buffer = xmlBufferCreate();
    if (Buffer == nullptr) {
        return QString();
    }
    if (IsElementKind(Node) && NodeNameEquals(Node, "t") &&
        CommentsOnlyTElement(Node)) {
        for (xmlNodePtr Child = Node->children; Child != nullptr;
             Child = Child->next) {
            if (IsCommentKind(Child)) {
                DumpNode(Buffer, Child);
            }
        }
    } else if (IsElementKind(Node)) {
        SerializeElementInto(Buffer, Node, 0);
    } else {
        DumpNode(Buffer, Node);
    }
    QString Text = BufferText(Buffer);
    xmlBufferFree(Buffer);
    return Text;
}

QString DocDocument::SerializeBodyNodes(const QVector<xmlNodePtr>& Editable,
                                        const QVector<DocPreservedItem>& Preserved)
{
    QStringList Pieces;
    QVector<DocPreservedItem> Ordered = Preserved;
    std::stable_sort(Ordered.begin(), Ordered.end(),
                     [](const DocPreservedItem& Left,
                        const DocPreservedItem& Right) {
                         return Left.AfterNodes < Right.AfterNodes;
                     });
    int Emitted = 0;
    for (const DocPreservedItem& Item : Ordered) {
        int At = Item.AfterNodes;
        if (At < 0) {
            At = 0;
        }
        if (At > Editable.size()) {
            At = Editable.size();
        }
        while (Emitted < At) {
            Pieces.append(SerializeNode(Editable.at(Emitted)));
            ++Emitted;
        }
        Pieces.append(SerializeNode(Item.Node));
    }
    while (Emitted < Editable.size()) {
        Pieces.append(SerializeNode(Editable.at(Emitted)));
        ++Emitted;
    }
    return Pieces.join(QStringLiteral("\n"));
}

QString DocDocument::ListRunToXml(xmlNodePtr Run)
{
    if (Run == nullptr || !IsElementKind(Run)) {
        return QString();
    }
    QString Name = TextFromXmlChars(Run->name);
    if (!IsBlockListName(Name)) {
        return QString();
    }
    if (!ListElementConverts(Run)) {
        return QString();
    }
    return SerializeNode(Run);
}

bool DocDocument::ListRunFromXml(const QString& Xml, xmlNodePtr& Run)
{
    Run = nullptr;
    xmlInitParser();
    xmlResetLastError();
    QByteArray XmlUtf8 = Utf8Bytes(Xml);
    xmlDocPtr Parsed = xmlReadMemory(XmlUtf8.constData(), XmlUtf8.size(),
                                     nullptr, nullptr,
                                     XML_PARSE_NONET | XML_PARSE_NOERROR |
                                     XML_PARSE_NOWARNING);
    if (Parsed == nullptr) {
        return false;
    }
    xmlNodePtr Root = xmlDocGetRootElement(Parsed);
    bool Ok = false;
    if (Root != nullptr && IsElementKind(Root)) {
        QString Name = TextFromXmlChars(Root->name);
        if (IsBlockListName(Name) && ListElementConverts(Root)) {
            Run = CopyNode(Root);
            Ok = Run != nullptr;
        }
    }
    xmlFreeDoc(Parsed);
    return Ok;
}

/**
 * @brief Updates the write-time values of a document tree.
 *
 * The <date> element of the front is set to the current date,
 * created after the authors when missing. The <rfc> attributes
 * take their defaults when empty (category "std", ipr
 * "trust200902", submissionType "IETF"), and an empty docName
 * is removed. The title's abbrev and ascii attributes are
 * trimmed, and removed when empty. A seriesInfo row naming
 * Internet-Draft takes its value from the draft name: the two
 * must match, or xml2rfc refuses the file.
 * @param Rfc The <rfc> element.
 */
static void UpdateWriteDefaults(xmlNodePtr Rfc)
{
    if (Rfc == nullptr) {
        return;
    }
    QString Name = DocDocument::NodeAttribute(Rfc, "docName").trimmed();
    DocDocument::SetNodeAttribute(Rfc, "docName", Name);
    QString Category = DocDocument::NodeAttribute(Rfc, "category").trimmed();
    DocDocument::SetNodeAttribute(Rfc, "category",
                                  Category.isEmpty() ? QStringLiteral("std")
                                                     : Category);
    QString Ipr = DocDocument::NodeAttribute(Rfc, "ipr").trimmed();
    DocDocument::SetNodeAttribute(Rfc, "ipr",
                                  Ipr.isEmpty() ? QStringLiteral("trust200902")
                                                : Ipr);
    QString SubmissionType =
        DocDocument::NodeAttribute(Rfc, "submissionType").trimmed();
    DocDocument::SetNodeAttribute(Rfc, "submissionType",
                                  SubmissionType.isEmpty()
                                      ? QStringLiteral("IETF")
                                      : SubmissionType);
    xmlNodePtr Front = DocDocument::ChildElement(Rfc, "front");
    if (Front == nullptr) {
        return;
    }
    xmlNodePtr TitleNode = DocDocument::ChildElement(Front, "title");
    if (TitleNode != nullptr) {
        QString Abbrev = DocDocument::NodeAttribute(TitleNode, "abbrev").trimmed();
        DocDocument::SetNodeAttribute(TitleNode, "abbrev", Abbrev);
        QString Ascii = DocDocument::NodeAttribute(TitleNode, "ascii").trimmed();
        DocDocument::SetNodeAttribute(TitleNode, "ascii", Ascii);
    }
    for (xmlNodePtr Info : DocDocument::ChildElements(Front, "seriesInfo")) {
        if (DocDocument::NodeAttribute(Info, "name").trimmed() ==
            QLatin1String("Internet-Draft")) {
            DocDocument::SetNodeAttribute(Info, "value", Name);
        }
    }
    QDate Today = QDate::currentDate();
    xmlNodePtr DateNode = DocDocument::ChildElement(Front, "date");
    if (DateNode == nullptr) {
        DateNode = xmlNewNode(nullptr, reinterpret_cast<const xmlChar*>("date"));
        if (DateNode == nullptr) {
            return;
        }
        InsertAfterPredecessor(Front, DateNode,
                               { "author", "seriesInfo", "title" });
    }
    DocDocument::SetNodeAttribute(DateNode, "day", QString::number(Today.day()));
    DocDocument::SetNodeAttribute(DateNode, "month",
                                  Today.toString(QStringLiteral("MMMM")));
    DocDocument::SetNodeAttribute(DateNode, "year",
                                  QString::number(Today.year()));
}

bool DocDocument::WriteXml(const QString& Path, QString& Error) const
{
    xmlNodePtr Rfc = RfcNode();
    if (Rfc == nullptr) {
        Error = QStringLiteral("There is no document to write.");
        return false;
    }
    UpdateWriteDefaults(Rfc);
    xmlBufferPtr Buffer = xmlBufferCreate();
    if (Buffer == nullptr) {
        Error = QStringLiteral("The XML writer could not be started.");
        return false;
    }
    AppendToBuffer(Buffer, QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"));
    SerializeElementInto(Buffer, Rfc, 0);
    AppendToBuffer(Buffer, QStringLiteral("\n"));
    QString Xml = BufferText(Buffer);
    xmlBufferFree(Buffer);
    QFile File(Path);
    if (!File.open(QIODevice::WriteOnly | QIODevice::Text)) {
        Error = File.errorString();
        return false;
    }
    if (File.write(Xml.toUtf8()) < 0) {
        Error = File.errorString();
        return false;
    }
    return true;
}

/**
 * @brief Replaces an element with a text node holding its text.
 * @param Node The element to replace.
 * @param Text The text; empty text just removes the element.
 */
static void ReplaceNodeWithText(xmlNodePtr Node, const QString& Text)
{
    if (Node == nullptr) {
        return;
    }
    if (!Text.isEmpty()) {
        QByteArray TextUtf8 = Utf8Bytes(Text);
        xmlNodePtr TextNode = xmlNewText(
            reinterpret_cast<const xmlChar*>(TextUtf8.constData()));
        if (TextNode != nullptr) {
            xmlAddPrevSibling(Node, TextNode);
        }
    }
    UnlinkAndFreeNode(Node);
}

/**
 * @brief Flattens a plain-text element: element children are
 *        replaced by their text, and comments, processing
 *        instructions, and entity references are dropped.
 *
 * The text is kept and the markup is dropped, which sets Skipped.
 * @param Element The element to flatten.
 * @param Skipped Set to true when markup was dropped.
 */
static void FlattenTextElement(xmlNodePtr Element, bool& Skipped)
{
    for (xmlNodePtr Child : CollectChildren(Element)) {
        if (IsElementKind(Child)) {
            QString Text = DocDocument::NodeText(Child);
            ReplaceNodeWithText(Child, Text);
            Skipped = true;
        } else if (IsCommentKind(Child) || IsPiKind(Child) ||
                   IsEntityRefKind(Child)) {
            UnlinkAndFreeNode(Child);
        }
    }
}

/**
 * @brief Normalizes a <title> element: text plus line breaks.
 *
 * Plain <br/> elements are kept as breaks. Any other markup is
 * replaced by its text, which sets Skipped. A <br/> carrying
 * attributes, or holding element children, is markup too. A
 * <br/> holding non-whitespace text loses that text, as the old
 * reader dropped it. Comments, processing instructions, and
 * entity references are dropped silently, as the old reader did.
 * @param Title The <title> element.
 * @param Skipped Set to true when markup was dropped.
 */
static void NormalizeTitleElement(xmlNodePtr Title, bool& Skipped)
{
    for (xmlNodePtr Child : CollectChildren(Title)) {
        if (IsTextKind(Child) || IsCdataKind(Child)) {
            continue;
        }
        if (IsElementKind(Child) &&
            DocDocument::NodeNameEquals(Child, "br")) {
            if (Child->properties != nullptr || Child->nsDef != nullptr) {
                ReplaceNodeWithText(Child, DocDocument::NodeText(Child));
                Skipped = true;
                continue;
            }
            bool HasElement = false;
            bool HasContent = false;
            for (xmlNodePtr GrandChild = Child->children; GrandChild != nullptr;
                 GrandChild = GrandChild->next) {
                if (IsElementKind(GrandChild)) {
                    HasElement = true;
                } else if ((IsTextKind(GrandChild) || IsCdataKind(GrandChild)) &&
                           !DocDocument::IsWhitespaceText(GrandChild)) {
                    HasContent = true;
                }
            }
            if (HasElement) {
                ReplaceNodeWithText(Child, DocDocument::NodeText(Child));
                Skipped = true;
            } else {
                RemoveAllChildren(Child);
                if (HasContent) {
                    Skipped = true;
                }
            }
            continue;
        }
        if (IsElementKind(Child)) {
            ReplaceNodeWithText(Child, DocDocument::NodeText(Child));
            Skipped = true;
            continue;
        }
        UnlinkAndFreeNode(Child);
    }
}

/**
 * @brief True when a kept <note> element holds nothing at all.
 *
 * An element with no element, comment, processing-instruction,
 * or entity-reference child, no attributes, and only whitespace
 * text holds nothing: it is an empty note, which the schema
 * rejects (a note must hold an element). A name is the note's
 * title, not its content: a note holding only a name still
 * fails the schema, which requires a content element after it.
 * Such a note is dropped on load instead of being kept and
 * written back invalid.
 * @param Note The kept <note> element to check.
 * @return True when the note is empty and may be dropped.
 */
static bool NoteNodeIsEmpty(xmlNodePtr Note)
{
    if (!IsElementKind(Note) ||
        !DocDocument::NodeNameEquals(Note, "note") ||
        Note->properties != nullptr || Note->nsDef != nullptr) {
        return false;
    }
    for (xmlNodePtr Child = Note->children; Child != nullptr;
         Child = Child->next) {
        if (IsTextKind(Child) || IsCdataKind(Child)) {
            if (!DocDocument::IsWhitespaceText(Child)) {
                return false;
            }
        } else if (IsElementKind(Child) &&
                   DocDocument::NodeNameEquals(Child, "name")) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

/**
 * @brief The structural pass over a freshly parsed tree.
 *
 * A document type declaration, markup other than a line break
 * inside a title, markup inside a section or reference-list
 * name, an organization, or an email, and stray text outside
 * elements at the <rfc> level cannot be kept; those set
 * Skipped. A section's title attribute becomes a <name> child.
 * Whitespace-only text inside a src-only <sourcecode> is
 * dropped. Empty notes in the front are dropped on load.
 * @param Document The parsed document.
 * @param Skipped Set to true when content that cannot be kept was found.
 */
static void ApplyStructuralPass(xmlDocPtr Document, bool& Skipped)
{
    if (Document == nullptr) {
        return;
    }
    if (Document->intSubset != nullptr || Document->extSubset != nullptr) {
        Skipped = true;
    }
    xmlNodePtr Root = xmlDocGetRootElement(Document);
    for (xmlNodePtr Child = Document->children; Child != nullptr;
         Child = Child->next) {
        if (Child == Root) {
            continue;
        }
        if (IsCommentKind(Child) || IsPiKind(Child) ||
            IsEntityRefKind(Child)) {
            Skipped = true;
        } else if (Child->type == XML_DTD_NODE) {
            Skipped = true;
        } else if ((IsTextKind(Child) || IsCdataKind(Child)) &&
                   !DocDocument::IsWhitespaceText(Child)) {
            Skipped = true;
        }
    }
    if (Root == nullptr) {
        return;
    }
    for (xmlNodePtr Child : CollectChildren(Root)) {
        if ((IsTextKind(Child) || IsCdataKind(Child)) &&
            !DocDocument::IsWhitespaceText(Child)) {
            UnlinkAndFreeNode(Child);
            Skipped = true;
        }
    }
    std::function<void(xmlNodePtr)> Visit = [&](xmlNodePtr Node) {
        for (xmlNodePtr Child : CollectChildren(Node)) {
            Visit(Child);
        }
        if (!IsElementKind(Node)) {
            return;
        }
        QString Name = TextFromXmlChars(Node->name);
        xmlNodePtr Parent = Node->parent;
        QString ParentName;
        if (Parent != nullptr && IsElementKind(Parent)) {
            ParentName = TextFromXmlChars(Parent->name);
        }
        if (Name == QLatin1String("title")) {
            NormalizeTitleElement(Node, Skipped);
            return;
        }
        if (Name == QLatin1String("name") &&
            (ParentName == QLatin1String("section") ||
             ParentName == QLatin1String("references"))) {
            FlattenTextElement(Node, Skipped);
            return;
        }
        if (Name == QLatin1String("organization") &&
            ParentName == QLatin1String("author")) {
            FlattenTextElement(Node, Skipped);
            return;
        }
        if (Name == QLatin1String("email") &&
            ParentName == QLatin1String("address")) {
            FlattenTextElement(Node, Skipped);
            return;
        }
        if (Name == QLatin1String("section")) {
            if (DocDocument::HasNodeAttribute(Node, "title")) {
                QString TitleAttr = DocDocument::NodeAttribute(Node, "title");
                xmlUnsetProp(Node, reinterpret_cast<const xmlChar*>("title"));
                if (DocDocument::ChildElement(Node, "name") == nullptr &&
                    !TitleAttr.isEmpty()) {
                    xmlNodePtr NameNode = xmlNewNode(
                        nullptr, reinterpret_cast<const xmlChar*>("name"));
                    if (NameNode != nullptr) {
                        DocDocument::SetNodeText(NameNode, TitleAttr);
                        xmlNodePtr First = nullptr;
                        for (xmlNodePtr Child = Node->children;
                             Child != nullptr; Child = Child->next) {
                            if (IsElementKind(Child)) {
                                First = Child;
                                break;
                            }
                        }
                        if (First != nullptr) {
                            xmlAddPrevSibling(First, NameNode);
                        } else {
                            xmlAddChild(Node, NameNode);
                        }
                    }
                }
            }
            return;
        }
        if (Name == QLatin1String("sourcecode") &&
            DocDocument::HasNodeAttribute(Node, "src")) {
            for (xmlNodePtr Child : CollectChildren(Node)) {
                if (IsTextKind(Child) &&
                    DocDocument::IsWhitespaceText(Child)) {
                    UnlinkAndFreeNode(Child);
                }
            }
            return;
        }
    };
    Visit(Root);
    xmlNodePtr Front = DocDocument::ChildElement(Root, "front");
    if (Front != nullptr) {
        for (xmlNodePtr Child : CollectChildren(Front)) {
            if (IsElementKind(Child) &&
                DocDocument::NodeNameEquals(Child, "note") &&
                NoteNodeIsEmpty(Child)) {
                UnlinkAndFreeNode(Child);
            }
        }
    }
}

bool DocDocument::ReadXml(const QString& Path, QString& Error)
{
    xmlInitParser();
    xmlResetLastError();
    QByteArray PathUtf8 = Utf8Bytes(Path);
    xmlDocPtr Parsed = xmlReadFile(PathUtf8.constData(), nullptr,
                                   XML_PARSE_NONET | XML_PARSE_NOERROR |
                                   XML_PARSE_NOWARNING);
    if (Parsed == nullptr) {
        const xmlError* LastError = xmlGetLastError();
        QString Text;
        if (LastError != nullptr && LastError->message != nullptr) {
            Text = TextFromXmlChars(
                reinterpret_cast<const xmlChar*>(LastError->message)).trimmed();
        }
        if (LastError != nullptr && LastError->line > 0 && !Text.isEmpty()) {
            Text = QStringLiteral("Line %1: %2").arg(LastError->line).arg(Text);
        }
        Error = Text.isEmpty()
            ? QStringLiteral("The file could not be parsed as XML.") : Text;
        return false;
    }
    xmlNodePtr Root = xmlDocGetRootElement(Parsed);
    if (Root == nullptr || !NodeNameEquals(Root, "rfc")) {
        Error = QStringLiteral("The file does not start with an <rfc> element.");
        xmlFreeDoc(Parsed);
        return false;
    }
    bool Skipped = false;
    ApplyStructuralPass(Parsed, Skipped);
    ResetDocument(Parsed);
    HadSkippedContent = Skipped;
    return true;
}

DocDocument DocDocument::NewWithDefaults(const QString& DocName,
                                         const QString& Title,
                                         const QString& Fullname,
                                         const QString& Organization,
                                         const QString& Email)
{
    xmlInitParser();
    DocDocument Document;
    QString CleanName = DocName.trimmed();
    QString CleanTitle = Title.trimmed();
    xmlDocPtr Parsed = xmlNewDoc(reinterpret_cast<const xmlChar*>("1.0"));
    if (Parsed == nullptr) {
        return Document;
    }
    xmlNodePtr Rfc = xmlNewNode(nullptr, reinterpret_cast<const xmlChar*>("rfc"));
    if (Rfc == nullptr) {
        xmlFreeDoc(Parsed);
        return Document;
    }
    xmlDocSetRootElement(Parsed, Rfc);
    if (!CleanName.isEmpty()) {
        SetNodeAttribute(Rfc, "docName", CleanName);
    }
    SetNodeAttribute(Rfc, "category", QStringLiteral("std"));
    SetNodeAttribute(Rfc, "ipr", QStringLiteral("trust200902"));
    SetNodeAttribute(Rfc, "submissionType", QStringLiteral("IETF"));
    xmlNodePtr Front = xmlNewChild(Rfc, nullptr,
                                   reinterpret_cast<const xmlChar*>("front"),
                                   nullptr);
    if (Front == nullptr) {
        xmlFreeDoc(Parsed);
        return Document;
    }
    xmlNodePtr TitleNode = xmlNewChild(Front, nullptr,
                                       reinterpret_cast<const xmlChar*>("title"),
                                       nullptr);
    if (TitleNode != nullptr) {
        // The abbreviated title prefills with the title when the
        // title is 40 characters or fewer; a longer title leaves
        // it empty for the author to write.
        if (CleanTitle.size() <= 40) {
            SetNodeAttribute(TitleNode, "abbrev", CleanTitle);
        }
        SetNodeText(TitleNode, CleanTitle);
    }
    xmlNodePtr Series = xmlNewChild(Front, nullptr,
                                    reinterpret_cast<const xmlChar*>("seriesInfo"),
                                    nullptr);
    if (Series != nullptr) {
        SetNodeAttribute(Series, "name", QStringLiteral("Internet-Draft"));
        SetNodeAttribute(Series, "value", CleanName);
    }
    xmlNodePtr Author = AuthorNodeFromFullname(Fullname, Organization, Email);
    if (Author != nullptr) {
        xmlAddChild(Front, Author);
    }
    QByteArray AreaUtf8 = Utf8Bytes(QStringLiteral("General"));
    xmlNewTextChild(Front, nullptr, reinterpret_cast<const xmlChar*>("area"),
                    reinterpret_cast<const xmlChar*>(AreaUtf8.constData()));
    xmlNodePtr Abstract = xmlNewChild(Front, nullptr,
                                      reinterpret_cast<const xmlChar*>("abstract"),
                                      nullptr);
    if (Abstract != nullptr) {
        xmlNodePtr Example = ExampleAbstractNode();
        if (Example != nullptr) {
            xmlAddChild(Abstract, Example);
        }
    }
    // One note is prefilled: the tool that created the draft,
    // with its version. The version comes from the application
    // version, so it can never drift from the version in main.cpp.
    xmlNodePtr Note = xmlNewChild(Front, nullptr,
                                  reinterpret_cast<const xmlChar*>("note"),
                                  nullptr);
    if (Note != nullptr) {
        QByteArray NoteNameUtf8 = Utf8Bytes(QStringLiteral("Created With"));
        xmlNewTextChild(Note, nullptr, reinterpret_cast<const xmlChar*>("name"),
                        reinterpret_cast<const xmlChar*>(NoteNameUtf8.constData()));
        QString CreatedText = QStringLiteral("ProtocolDocEditor ") +
                              QCoreApplication::applicationVersion();
        QByteArray CreatedUtf8 = Utf8Bytes(CreatedText);
        xmlNewTextChild(Note, nullptr, reinterpret_cast<const xmlChar*>("t"),
                        reinterpret_cast<const xmlChar*>(CreatedUtf8.constData()));
    }
    xmlNodePtr Middle = xmlNewChild(Rfc, nullptr,
                                    reinterpret_cast<const xmlChar*>("middle"),
                                    nullptr);
    if (Middle != nullptr) {
        // The outline is prefilled with the required and optional
        // middle sections, in the recommended order: Introduction,
        // Terminology, Implementation Status, Privacy
        // Considerations, Security Considerations, IANA
        // Considerations, Acknowledgements. Status of This Memo,
        // Copyright Notice, and Authors' Addresses are generated
        // by xml2rfc, not prefilled here. Each optional section
        // starts with a comment saying it is optional and what
        // it is for; the author deletes the section when it is
        // not needed. Required sections start empty, and the
        // author adds body sections between Terminology and
        // Implementation Status.
        auto AddSection = [&](const QString& TitleText, const QString& Comment) {
            xmlNodePtr Section = NewSectionNode(TitleText);
            if (Section == nullptr) {
                return;
            }
            xmlAddChild(Middle, Section);
            if (!Comment.isEmpty()) {
                QByteArray CommentUtf8 = Utf8Bytes(Comment);
                xmlNodePtr CommentNode = xmlNewComment(
                    reinterpret_cast<const xmlChar*>(CommentUtf8.constData()));
                if (CommentNode != nullptr) {
                    xmlAddChild(Section, CommentNode);
                }
            }
        };
        AddSection(QStringLiteral("Introduction"), QString());
        AddSection(QStringLiteral("Terminology"),
                   QStringLiteral("Optional section. Define the terms this document uses here, including the BCP 14 requirement words (MUST, SHOULD, MAY, and their negatives) and the paragraph explaining them. Delete this section if the document needs no definitions."));
        AddSection(QStringLiteral("Implementation Status"),
                   QStringLiteral("Optional section. Record the implementation status of this specification here, as described in BCP 205 (RFC 7942). This section is temporary and is removed before publication as an RFC. Delete this section if it does not apply."));
        AddSection(QStringLiteral("Privacy Considerations"),
                   QStringLiteral("Optional section, recommended by RFC 6973. Discuss the privacy properties of the protocol here. Delete this section if there is nothing to record."));
        AddSection(QStringLiteral("Security Considerations"), QString());
        AddSection(QStringLiteral("IANA Considerations"), QString());
        AddSection(QStringLiteral("Acknowledgements"),
                   QStringLiteral("Optional section. Thank the people who contributed to this document here, including contributors whose work is not covered by authorship. Delete this section if it is not needed."));
    }
    xmlNewChild(Rfc, nullptr, reinterpret_cast<const xmlChar*>("back"), nullptr);
    Document.ResetDocument(Parsed);
    Document.HadSkippedContent = false;
    return Document;
}
