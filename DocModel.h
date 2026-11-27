// Copyright (c) 2026 RiverExplorer LLC
// RiverExplorer is a registered trademark of Douglas Mark Royer.
//
// This software is released under the MIT License.
// See LICENSE.md for the full license text.

/**
 * @file DocModel.h
 * @brief Document model for the InternetDraftsEditor: the draft as a libxml2 tree.
 *
 * A DocDocument owns one libxml2 document and nothing else mirrors
 * XML anymore. Every accessor hands out borrowed node pointers into
 * that tree; the only exceptions are functions whose docs say
 * "caller owns". The tree holds the file's content, including the
 * parts the editor does not model yet, so loading a file and saving
 * it again keeps them in place. Only a document type declaration,
 * markup other than a line break inside a title, markup inside a
 * section or reference-list name, an organization, or an email, and
 * stray text outside elements at the <rfc> level cannot be kept;
 * ReadXml notes those in HadSkippedContent so the user is warned
 * before saving would drop them.
 *
 * WriteXml writes RFCXML version 3, valid against the v3.rng schema
 * from the xml2rfc 3.35.0 distribution, with a custom recursive
 * serializer (see DocModel.cpp): block-level elements are
 * pretty-printed at two spaces per depth, mixed-content elements
 * are written inline, so inline markup inside a paragraph keeps
 * its exact spacing. The date written is always the current date:
 * a draft's date stays current until the draft is submitted, and
 * only then is it frozen.
 *
 * @note COMPILE-UNTESTED: this file was written on a machine
 *       without Qt or the libxml2 headers, and has not been
 *       compiled. The libxml2 symbols used are the stable core
 *       API, verified against the libxml2 2.15.4 headers in
 *       Doug's RelatedProjects/include, which are not on this VM.
 */

#ifndef DOCEDITOR_DOCMODEL_H
#define DOCEDITOR_DOCMODEL_H

#include <QString>
#include <QStringList>
#include <QVector>

#include <libxml/tree.h>

/**
 * @brief One preserved body node, with its anchor.
 *
 * AfterNodes is the number of editable body nodes that precede
 * the item: 0 means before the first editable node. When the
 * editable count changes through editing, the item stays after
 * that many editable nodes, or after all of them when fewer are
 * left. Node is borrowed into the document tree, unless stated
 * otherwise.
 */
struct DocPreservedItem
{
    int AfterNodes = 0;        /**< Editable body nodes before this item. */
    xmlNodePtr Node = nullptr; /**< Borrowed, unless stated otherwise. */
};

/**
 * @brief The whole draft document the editor works with.
 *
 * The document IS a libxml2 tree: the draft name, the front matter
 * (title, series info, authors, date, areas, workgroups, keywords,
 * abstract, notes), the middle sections, and the back matter's
 * reference lists all live as nodes in the one owned xmlDocPtr.
 * Copying a DocDocument deep-copies the tree; moving one transfers
 * ownership.
 */
class DocDocument
{
public:
    /**
     * @brief Builds an empty document: no tree is loaded.
     */
    DocDocument();

    /**
     * @brief Deep-copies the document's tree.
     * @param Other The document to copy.
     */
    DocDocument(const DocDocument& Other);

    /**
     * @brief Takes ownership of another document's tree.
     * @param Other The document to move from; left empty.
     */
    DocDocument(DocDocument&& Other) noexcept;

    /**
     * @brief Deep-copies another document's tree over this one.
     * @param Other The document to copy.
     * @return This document.
     */
    DocDocument& operator=(const DocDocument& Other);

    /**
     * @brief Takes ownership of another document's tree.
     * @param Other The document to move from; left empty.
     * @return This document.
     */
    DocDocument& operator=(DocDocument&& Other) noexcept;

    /**
     * @brief Frees the owned tree.
     */
    ~DocDocument();

    /**
     * @brief True when no document is loaded.
     * @return True when the owned tree is null.
     */
    bool IsEmpty() const;

    /**
     * @brief The owned libxml2 document, borrowed.
     * @return The document, or nullptr when empty.
     */
    xmlDocPtr XmlDocument() const;

    /**
     * @brief The <rfc> root element, borrowed.
     * @return The root, or nullptr when absent.
     */
    xmlNodePtr RfcNode() const;

    /**
     * @brief The <front> element, borrowed.
     * @return The front, or nullptr when absent.
     */
    xmlNodePtr FrontNode() const;

    /**
     * @brief The <middle> element, borrowed.
     * @return The middle, or nullptr when absent.
     */
    xmlNodePtr MiddleNode() const;

    /**
     * @brief The <back> element, borrowed.
     * @return The back, or nullptr when absent.
     */
    xmlNodePtr BackNode() const;

    /**
     * @brief The draft name: the docName attribute of <rfc>.
     * @return The draft name, or empty when absent.
     */
    QString DocName() const;

    /**
     * @brief Sets the draft name.
     * @param Name The draft name; an empty name removes the attribute.
     */
    void SetDocName(const QString& Name);

    /**
     * @brief The title text, with <br/> elements removed.
     * @return The title text, trimmed.
     */
    QString Title() const;

    /**
     * @brief The offsets into Title() where a line break sits.
     *
     * A <br/> inside a loaded title is kept as its character
     * offset, so the title stays a single line in the editor
     * and the break writes back in place. Offsets are sorted.
     * @return The break offsets.
     */
    QVector<int> TitleBreaks() const;

    /**
     * @brief Rebuilds the <title> children from text plus breaks.
     * @param Text The title text.
     * @param Breaks The break offsets into Text.
     */
    void SetTitle(const QString& Text, const QVector<int>& Breaks);

    /**
     * @brief The title's abbreviated form: the abbrev attribute of <title>.
     * @return The abbreviated title, or empty when absent.
     */
    QString TitleAbbrev() const;

    /**
     * @brief Sets the title's abbreviated form.
     * @param Abbrev The abbreviated title; empty removes the attribute.
     */
    void SetTitleAbbrev(const QString& Abbrev);

    /**
     * @brief The title's ASCII form: the ascii attribute of <title>.
     * @return The ASCII title, or empty when absent.
     */
    QString TitleAscii() const;

    /**
     * @brief Sets the title's ASCII form.
     * @param Ascii The ASCII title; empty removes the attribute.
     */
    void SetTitleAscii(const QString& Ascii);

    /**
     * @brief The <seriesInfo> children of <front>, borrowed, file order.
     * @return The series info nodes.
     */
    QVector<xmlNodePtr> SeriesInfoNodes() const;

    /**
     * @brief Appends an empty <seriesInfo> to <front>.
     */
    void AddSeriesInfoNode();

    /**
     * @brief Removes one <seriesInfo> node from <front>.
     * @param Index The node to remove; out of range does nothing.
     */
    void RemoveSeriesInfoNode(int Index);

    /**
     * @brief The <area> texts of <front>, in file order.
     * @return The areas.
     */
    QStringList Areas() const;

    /**
     * @brief Rebuilds the <area> children of <front>.
     * @param Values The areas, in order.
     */
    void SetAreas(const QStringList& Values);

    /**
     * @brief The <workgroup> texts of <front>, in file order.
     * @return The workgroups.
     */
    QStringList Workgroups() const;

    /**
     * @brief Rebuilds the <workgroup> children of <front>.
     * @param Values The workgroups, in order.
     */
    void SetWorkgroups(const QStringList& Values);

    /**
     * @brief The <keyword> texts of <front>, in file order.
     * @return The keywords.
     */
    QStringList Keywords() const;

    /**
     * @brief Rebuilds the <keyword> children of <front>.
     * @param Values The keywords, in order.
     */
    void SetKeywords(const QStringList& Values);

    /**
     * @brief The <author> children of <front>, borrowed, file order.
     * @return The author nodes.
     */
    QVector<xmlNodePtr> AuthorNodes() const;

    /**
     * @brief Appends a detached <author> node, adopting it.
     * @param Author The detached node to adopt; null does nothing.
     */
    void AddAuthorNode(xmlNodePtr Author);

    /**
     * @brief Removes one <author> node from <front>.
     * @param Index The node to remove; out of range does nothing.
     */
    void RemoveAuthorNode(int Index);

    /**
     * @brief The <note> children of <front>, borrowed, file order.
     * @return The note nodes.
     */
    QVector<xmlNodePtr> NoteNodes() const;

    /**
     * @brief Appends a <note> holding one empty <t>.
     * @return The new note, borrowed.
     */
    xmlNodePtr AddNoteNode();

    /**
     * @brief Removes one <note> node from <front>.
     * @param Index The node to remove; out of range does nothing.
     */
    void RemoveNoteNode(int Index);

    /**
     * @brief The editable body child elements of <abstract>, borrowed.
     * @return The abstract body nodes.
     */
    QVector<xmlNodePtr> AbstractBodyNodes() const;

    /**
     * @brief The preserved body child nodes of <abstract>, borrowed.
     * @return The abstract preserved nodes, with anchors.
     */
    QVector<DocPreservedItem> AbstractPreservedNodes() const;

    /**
     * @brief Replaces the <abstract> body, adopting the editable nodes.
     * @param Editable The new editable body nodes, adopted.
     * @param Preserved The preserved nodes, borrowed from this tree.
     */
    void SetAbstractBody(const QVector<xmlNodePtr>& Editable,
                         const QVector<DocPreservedItem>& Preserved);

    /**
     * @brief The <section> children of <middle>, borrowed, file order.
     * @return The top sections.
     */
    QVector<xmlNodePtr> TopSectionNodes() const;

    /**
     * @brief The <section> children of <back> (the appendices),
     *        borrowed, file order. Empty when there is no <back>.
     * @return The appendix sections.
     */
    QVector<xmlNodePtr> BackSectionNodes() const;

    /**
     * @brief The appendix letter for a zero-based appendix index:
     *        0 is A, 25 is Z, 26 is AA, and so on.
     * @param Index The zero-based appendix index.
     * @return The letter or letters.
     */
    static QString AppendixLetter(int Index);

    /**
     * @brief Finds a section by its dotted path ("0", "1.2", or a
     *        back path "back:0", "back:0.1").
     *
     * The path indexes into the top sections, then into child
     * sections, the same scheme the outline already uses. A path
     * starting with "back:" indexes into the appendix sections
     * instead.
     * @param Path The dotted path.
     * @return The section, borrowed, or nullptr when missing.
     */
    xmlNodePtr FindSectionNode(const QString& Path) const;

    /**
     * @brief The <section> children of a section, borrowed, file order.
     * @param Section The section, borrowed; null gives no children.
     * @return The child sections.
     */
    static QVector<xmlNodePtr> ChildSectionNodes(xmlNodePtr Section);

    /**
     * @brief The text of a section's <name> child.
     * @param Section The section, borrowed; null gives empty text.
     * @return The section title, trimmed.
     */
    static QString SectionTitle(xmlNodePtr Section);

    /**
     * @brief Replaces or creates a section's <name> child, as the
     *        first child element.
     * @param Section The section, borrowed.
     * @param Title The new title.
     */
    static void SetSectionTitle(xmlNodePtr Section, const QString& Title);

    /**
     * @brief Builds a detached <section> holding a <name>.
     * @param Title The section title.
     * @return The section; the caller owns it.
     */
    static xmlNodePtr NewSectionNode(const QString& Title);

    /**
     * @brief The editable body child nodes of a section, abstract,
     *        or note, borrowed, file order.
     *
     * Direct child elements except a leading <name> (sections and
     * notes), excluding preserved nodes; comment nodes are
     * included, since the editor models them. Whitespace-only
     * text children are never returned.
     * @param Parent The section, abstract, or note, borrowed.
     * @return The editable body nodes.
     */
    static QVector<xmlNodePtr> BodyNodes(xmlNodePtr Parent);

    /**
     * @brief The preserved body child nodes of a section, abstract,
     *        or note, borrowed, with anchors.
     *
     * Preserved is every child node that is not a leading <name>
     * and not editable: unknown elements, non-whitespace stray
     * text nodes, comments at body level that the editor does not
     * model, and anything BodyNodeIsEditable rejects. Child
     * sections are not listed here; RebuildBodyChildren keeps
     * them in place on its own.
     * @param Parent The section, abstract, or note, borrowed.
     * @return The preserved body nodes, with anchors.
     */
    static QVector<DocPreservedItem> PreservedBodyNodes(xmlNodePtr Parent);

    /**
     * @brief True when a body child node is one the editor models.
     *
     * True for a <t> holding inline-only runs, a <ul>, <ol>, or
     * <dl> with structurable items, a <figure>, <artset>, or
     * <artwork> the reader can structure, a <table> the reader
     * can structure, a <sourcecode>, and a comment node.
     * Everything else is preserved.
     * @param Node The node to test, borrowed.
     * @return True when the node is editable.
     */
    static bool BodyNodeIsEditable(xmlNodePtr Node);

    /**
     * @brief Rebuilds a parent's body children from editable and
     *        preserved nodes.
     *
     * Unlinks (does not free) the parent's existing non-<name>
     * child nodes; frees the old editable ones; keeps the old
     * preserved ones and any child sections (they are the tree's
     * own nodes, unlinked then re-linked); inserts the passed
     * editable nodes (adopted) and the preserved nodes
     * (re-linked) interleaved by anchor, clamped to the editable
     * count. A leading <name> child element stays in place.
     * @param Parent The section, abstract, or note, borrowed.
     * @param Editable The new editable nodes, adopted.
     * @param Preserved The preserved nodes, borrowed from this tree.
     */
    static void RebuildBodyChildren(xmlNodePtr Parent,
                                    const QVector<xmlNodePtr>& Editable,
                                    const QVector<DocPreservedItem>& Preserved);

    /**
     * @brief Replaces a section's body, adopting the editable nodes.
     * @param Section The section, borrowed.
     * @param Editable The new editable body nodes, adopted.
     * @param Preserved The preserved nodes, borrowed from this tree.
     */
    void SetSectionBody(xmlNodePtr Section,
                        const QVector<xmlNodePtr>& Editable,
                        const QVector<DocPreservedItem>& Preserved);

    /**
     * @brief Replaces a note's body, adopting the editable nodes.
     * @param Note The note, borrowed.
     * @param Editable The new editable body nodes, adopted.
     * @param Preserved The preserved nodes, borrowed from this tree.
     */
    void SetNoteBody(xmlNodePtr Note,
                     const QVector<xmlNodePtr>& Editable,
                     const QVector<DocPreservedItem>& Preserved);

    /**
     * @brief The <references> children of <back>, borrowed, file order.
     * @return The reference groups.
     */
    QVector<xmlNodePtr> ReferenceGroupNodes() const;

    /**
     * @brief The <references> list at a dotted path, borrowed, or nullptr.
     *
     * A path names a list by its position among its siblings at
     * each level: "0" is the first top-level list, "0.1" the
     * second nested list under it. An empty or invalid path
     * returns nullptr.
     * @param Path The dotted path.
     * @return The list node, or nullptr.
     */
    xmlNodePtr ReferenceGroupAtPath(const QString& Path) const;

    /**
     * @brief Every reference list path, in document order.
     *
     * Top-level lists first, each followed by its nested lists
     * depth-first, so every list the editor can edit is named.
     * @return The dotted paths.
     */
    QStringList ReferenceGroupPaths() const;

    /**
     * @brief The <references> children of a list, borrowed, file order.
     * @param Group The <references> node, borrowed.
     * @return The nested lists.
     */
    static QVector<xmlNodePtr> NestedReferenceGroups(xmlNodePtr Group);

    /**
     * @brief True when a list holds nested lists.
     *
     * A list holds either nested lists or reference entries,
     * never both, so a list with nested lists takes no entries.
     * @param Group The <references> node, borrowed.
     * @return True for a container list.
     */
    static bool ReferenceGroupHasNestedLists(xmlNodePtr Group);

    /**
     * @brief True when a list holds reference entries.
     *
     * A list holds either nested lists or reference entries,
     * never both, so a list with entries takes no nested list.
     * A <referencegroup> child counts as an entry here.
     * @param Group The <references> node, borrowed.
     * @return True for a leaf list.
     */
    static bool ReferenceGroupHasEntries(xmlNodePtr Group);

    /**
     * @brief All child nodes of <back>, borrowed, file order.
     *
     * Elements and non-whitespace text, including comments and
     * anything else the back matter holds; whitespace-only text
     * children are skipped.
     * @return The back children.
     */
    QVector<xmlNodePtr> BackChildNodes() const;

    /**
     * @brief The <reference> children of a group, borrowed, file order.
     * @param Group The <references> node, borrowed.
     * @return The reference entries.
     */
    static QVector<xmlNodePtr> ReferenceEntries(xmlNodePtr Group);

    /**
     * @brief The outline label for a reference entry.
     *
     * The anchor, plus " - " plus the title when the title is
     * non-empty; the title alone when the anchor is empty. A
     * display name, when given, stands in place of the anchor:
     * an entry mapped by a displayreference lists under the
     * name readers see, not the anchor they never see.
     * @param Entry The <reference> node, borrowed.
     * @param DisplayName The display name for the entry's anchor,
     *        or empty to use the anchor itself.
     * @return The label.
     */
    static QString ReferenceLabel(xmlNodePtr Entry,
                                  const QString& DisplayName = QString());

    /**
     * @brief The distinct texts that cite an anchor.
     *
     * The text contents of the <xref> elements under Root whose
     * target is Anchor, in document order: the words the author
     * tagged when they marked something as a reference. Empty
     * contents are skipped, and a text that appears more than
     * once is listed once.
     * @param Root The tree to walk, borrowed.
     * @param Anchor The anchor the xrefs point at.
     * @return The citing texts, distinct, in document order.
     */
    static QStringList CitedTextsForAnchor(xmlNodePtr Root,
                                           const QString& Anchor);

    /**
     * @brief How many cross-references cite an anchor.
     *
     * Counts the <xref> and <relref> elements under Root whose
     * target is Anchor, with content or without: a reference
     * with any count at all is in use and must not be removed
     * while the citations stand.
     * @param Root The tree to walk, borrowed.
     * @param Anchor The anchor the citations point at.
     * @return The number of citations.
     */
    static int CitationCountForAnchor(xmlNodePtr Root,
                                      const QString& Anchor);

    /**
     * @brief One internal target a cross-reference can point at.
     *
     * Title is the section, figure, or table title as the author
     * sees it; Number is a section's outline number (1, 1.2),
     * empty for figures and tables; Kind names which it is;
     * Anchor is its anchor, which may be empty when the editor
     * has not given it one yet; Node is the element, borrowed.
     */
    struct InternalTarget
    {
        QString Title;
        QString Number;
        QString Kind;
        QString Anchor;
        xmlNodePtr Node;
    };

    /**
     * @brief The sections, figures, and tables a cross-reference can point at.
     *
     * Sections in outline order, then figures and tables in
     * document order. Every target is listed whether or not it
     * already has an anchor: one without an anchor gets one,
     * derived from its title, when it is picked.
     * @return The targets.
     */
    QVector<InternalTarget> InternalTargets() const;

    /**
     * @brief The <xi:include> children of a reference list, borrowed, file order.
     * @param Group The <references> node, borrowed.
     * @return The included references.
     */
    static QVector<xmlNodePtr> ReferenceIncludeNodes(xmlNodePtr Group);

    /**
     * @brief The <xi:include> nodes under this node, borrowed, file order.
     * @param Root The tree to walk, borrowed.
     * @return The included references.
     */
    static QVector<xmlNodePtr> DocumentIncludeNodes(xmlNodePtr Root);

    /**
     * @brief The anchor an included reference carries, from its href.
     *
     * A bibxml href for an RFC names the anchor: reference.RFC.4324.xml
     * carries RFC4324. Any other href gives its file name without the
     * extension, which is the best the editor can tell.
     * @param Href The xi:include href.
     * @return The anchor, or empty when the href names none.
     */
    static QString ReferenceAnchorFromIncludeHref(const QString& Href);

    /**
     * @brief The XInclude namespace on the root, creating it when missing.
     *
     * Reuses an existing namespace with the XInclude URI whatever its
     * prefix; otherwise declares xmlns:xi for it.
     * @param Rfc The <rfc> node, borrowed.
     * @return The namespace, or nullptr when there is no root.
     */
    static xmlNsPtr EnsureXIncludeNamespace(xmlNodePtr Rfc);

    /**
     * @brief True when any element in the tree carries this anchor.
     * @param Root The tree to walk, borrowed.
     * @param Anchor The anchor to look for.
     * @return True when the anchor is already used.
     */
    static bool DocumentHasAnchor(xmlNodePtr Root, const QString& Anchor);

    /**
     * @brief The first element in the tree carrying this anchor (Bug 529).
     *
     * Ctrl+click on a cross-reference follows it: the editor
     * needs the element the anchor belongs to, so it can select
     * it in the outline. The walk is document order, so the
     * first match wins when an anchor is (wrongly) duplicated.
     * @param Root The tree to walk, borrowed.
     * @param Anchor The anchor to look for.
     * @return The element, borrowed, or null when no element carries it.
     */
    static xmlNodePtr FindNodeByAnchor(xmlNodePtr Root,
                                       const QString& Anchor);

    /**
     * @brief Renames the target on every cross-reference pointing at it.
     *
     * When a reference entry's anchor changes, the cross-references
     * that cited it must follow: the old target is replaced with
     * the new one wherever an <xref> carries it. Other reference
     * tags are left alone.
     * @param Root The tree to walk, borrowed.
     * @param OldTarget The target to replace.
     * @param NewTarget The target to put in its place.
     */
    static void RenameXrefTargets(xmlNodePtr Root, const QString& OldTarget,
                                  const QString& NewTarget);

    /**
     * @brief The <author> children of a reference entry's <front>.
     * @param Entry The <reference> node, borrowed.
     * @return The authors, borrowed, file order.
     */
    static QVector<xmlNodePtr> ReferenceAuthors(xmlNodePtr Entry);

    /**
     * @brief One part of a reference entry's <date> element.
     * @param Entry The <reference> node, borrowed.
     * @param Part The part: "day", "month", or "year".
     * @return The attribute value, or empty when absent.
     */
    static QString ReferenceDatePart(xmlNodePtr Entry, const char* Part);

    /**
     * @brief One attribute of an <author> node.
     * @param Author The <author> node, borrowed.
     * @param Name The attribute name.
     * @return The value, or empty when absent.
     */
    static QString AuthorAttribute(xmlNodePtr Author, const char* Name);

    /**
     * @brief Sets one attribute of an <author> node.
     * @param Author The <author> node, borrowed.
     * @param Name The attribute name.
     * @param Value The value; empty removes the attribute.
     */
    static void SetAuthorAttribute(xmlNodePtr Author, const char* Name,
                                   const QString& Value);

    /**
     * @brief The text of an author's <organization> child.
     * @param Author The <author> node, borrowed.
     * @return The organization text, or empty when absent.
     */
    static QString AuthorOrganization(xmlNodePtr Author);

    /**
     * @brief Sets an author's <organization> child text.
     * @param Author The <author> node, borrowed.
     * @param Organization The organization text; empty removes the element.
     */
    static void SetAuthorOrganization(xmlNodePtr Author,
                                      const QString& Organization);

    /**
     * @brief The text of an author's first <email>, <phone>, or <uri>.
     * @param Author The <author> node, borrowed.
     * @param Field The field: "email", "phone", or "uri".
     * @return The field text, or empty when absent.
     */
    static QString AuthorContactField(xmlNodePtr Author, const char* Field);

    /**
     * @brief Sets an author's first <email>, <phone>, or <uri> text.
     * @param Author The <author> node, borrowed.
     * @param Field The field: "email", "phone", or "uri".
     * @param Value The field text; empty removes the element.
     */
    static void SetAuthorContactField(xmlNodePtr Author, const char* Field,
                                      const QString& Value);

    /**
     * @brief The text of one structured <postal> part of an author.
     * @param Author The <author> node, borrowed.
     * @param Part The part: "street", "cityarea", "extaddr", "pobox",
     *        "city", "region", "code", "country", or "sortingcode".
     * @return The part text, or empty when absent.
     */
    static QString AuthorPostalPart(xmlNodePtr Author, const char* Part);

    /**
     * @brief Sets one structured <postal> part of an author.
     *
     * Setting a structured part switches the postal address to
     * the structured form: any <postalLine> children are removed.
     * @param Author The <author> node, borrowed.
     * @param Part The part to set.
     * @param Value The part text; empty removes the element.
     */
    static void SetAuthorPostalPart(xmlNodePtr Author, const char* Part,
                                    const QString& Value);

    /**
     * @brief True when an author's <postal> holds <postalLine>
     *        children before any structured part.
     * @param Author The <author> node, borrowed.
     * @return True when the postal address is written as lines.
     */
    static bool AuthorPostalUsesLines(xmlNodePtr Author);

    /**
     * @brief The <postalLine> texts of an author, in order.
     * @param Author The <author> node, borrowed.
     * @return The line texts.
     */
    static QStringList AuthorPostalLines(xmlNodePtr Author);

    /**
     * @brief Rebuilds an author's <postal> lines.
     *
     * Setting lines switches the postal address to the free-form
     * form: any structured parts are removed.
     * @param Author The <author> node, borrowed.
     * @param Lines The line texts, in order.
     */
    static void SetAuthorPostalLines(xmlNodePtr Author,
                                     const QVector<QString>& Lines);

    /**
     * @brief True when an author holds anything the author form
     *        does not edit.
     *
     * Extra attributes, extra children, extra address children,
     * and extra postal children all count, including a second
     * email, a second street, and kept attributes on the
     * organization, the postal element, and the postal parts.
     * @param Author The <author> node, borrowed.
     * @return True when preserved content exists in the author.
     */
    static bool AuthorHasPreservedContent(xmlNodePtr Author);

    /**
     * @brief Builds a detached <author> from a full name.
     *
     * The surname is the last word; the initials are the first
     * letter of each earlier word, each followed by a dot. A
     * single-word name becomes the surname with no initials.
     * The <organization> and <address>/<email> children are
     * added when non-empty.
     * @param Fullname The author's full name.
     * @param Organization The author's organization.
     * @param Email The author's email address.
     * @return The author; the caller owns it.
     */
    static xmlNodePtr AuthorNodeFromFullname(const QString& Fullname,
                                             const QString& Organization,
                                             const QString& Email);

    /**
     * @brief The surname derived from a full name: its last word.
     * @param Fullname The full name.
     * @return The surname, or empty when the name is empty.
     */
    static QString SurnameFromFullname(const QString& Fullname);

    /**
     * @brief The initials derived from a full name.
     * @param Fullname The full name.
     * @return The initials, or empty for a single-word name.
     */
    static QString InitialsFromFullname(const QString& Fullname);

    /**
     * @brief Builds a detached <t> holding the example abstract text.
     *
     * The text is the abstract from draft-hoffman-rfcexamples,
     * the IETF's RFCXML example document; it says it is an
     * example, so it cannot be mistaken for draft content.
     * @return The <t> element; the caller owns it.
     */
    static xmlNodePtr ExampleAbstractNode();

    /**
     * @brief True when a title is one of the three required section
     *        titles: Introduction, Security Considerations, or IANA
     *        Considerations.
     * @param Title The section title to test.
     * @return True when the title is a required section title.
     */
    static bool IsRequiredSectionTitle(const QString& Title);

    /**
     * @brief True when a title is one of the two recommended
     *        section titles: Privacy Considerations or
     *        Implementation Status.
     * @param Title The section title to test.
     * @return True when the title is a recommended section title.
     */
    static bool IsRecommendedSectionTitle(const QString& Title);

    /**
     * @brief Splits free text into paragraphs on blank lines.
     * @param Text The text to split.
     * @return The paragraphs, with surrounding blank lines removed.
     */
    static QStringList SplitParagraphs(const QString& Text);

    /**
     * @brief Joins paragraphs into free text with blank lines between.
     * @param Paragraphs The paragraphs to join.
     * @return The joined text.
     */
    static QString JoinParagraphs(const QStringList& Paragraphs);

    /**
     * @brief Lists what would keep this document from being a valid
     *        draft: an empty draft name, title, author list, or
     *        section list, or a series info row with no name or
     *        value.
     * @return The problems found, empty when the document is valid.
     */
    QStringList Problems() const;

    /**
     * @brief Lists the literal lines longer than xml2rfc renders
     *        without a warning.
     *
     * xml2rfc never wraps source code or ASCII art, and renders
     * it with a 3-character indent, so a literal line over 69
     * characters draws warning L106 when the draft is rendered.
     * This lists every over-long line in the modeled
     * <sourcecode> blocks and ASCII-art <artwork>, and in the
     * preserved blocks, with its block kind, its section, its
     * line number within the block, and its length. SVG artwork
     * is not listed: it is markup, rendered as an image. Prose
     * is not listed: xml2rfc re-wraps it. This is a warning,
     * not a problem: it does not keep the draft from saving.
     * @return One description per over-long line, in document
     *         order; empty when no literal line is over-long.
     */
    QStringList LongLiteralLines() const;

    /**
     * @brief True when any part of the document holds preserved content.
     * @return True when preserved content exists anywhere.
     */
    bool HasPreservedContent() const;

    /**
     * @brief Writes the document as RFCXML version 3.
     *
     * The date written is always the current date. Preserved
     * content is written back in its original position.
     * @param Path The file to write.
     * @param Error Set to the reason when writing fails.
     * @param AddAidComment When true, a comment naming this tool
     *        and its version is written at the top of the file,
     *        before the root element (Bug 505). The caller passes
     *        true only when the tool created or modified the
     *        content, or when the file already carries the
     *        comment (so an unchanged re-save keeps it).
     * @return True when the file was written.
     */
    bool WriteXml(const QString& Path, QString& Error,
                  bool AddAidComment) const;

    /**
     * @brief Reads a draft XML file into this document.
     *
     * The modeled subset is carried over as tree nodes, and
     * everything else stays in the tree in its original
     * position, so saving writes it back. Only a document type
     * declaration, markup other than a line break inside a
     * title, markup inside a section or reference-list name, an
     * organization, or an email, and stray text outside elements
     * at the <rfc> level cannot be kept; those set
     * HadSkippedContent.
     * @param Path The file to read.
     * @param Error Set to the reason when reading fails.
     * @return True when the file was read.
     */
    bool ReadXml(const QString& Path, QString& Error);

    /**
     * @brief Builds the starting document for a new draft.
     *
     * The middle sections are prefilled with Introduction,
     * Terminology, Implementation Status, Privacy
     * Considerations, Security Considerations, IANA
     * Considerations, and Acknowledgements, in that order; the
     * optional ones start with a comment saying what they are
     * for. The abstract is prefilled with the IETF example text.
     * The date is not stored: the writer always writes the
     * current date. The author is built from the given full
     * name, organization, and email, with initials and surname
     * derived from the full name. The category is "std", the
     * intellectual property rights "trust200902", and the
     * submission type "IETF".
     * @param DocName The draft name, used as the file name and the
     *        docName attribute (for example draft-royer-coop-00).
     * @param Title The draft title.
     * @param Fullname The first author's full name.
     * @param Organization The first author's organization.
     * @param Email The first author's email address.
     * @return The starting document.
     */
    static DocDocument NewWithDefaults(const QString& DocName,
                                       const QString& Title,
                                       const QString& Fullname,
                                       const QString& Organization,
                                       const QString& Email);

    /**
     * @brief The first child element of a parent with a given name.
     * @param Parent The parent, borrowed; null gives nullptr.
     * @param Name The element name.
     * @return The child, borrowed, or nullptr when absent.
     */
    static xmlNodePtr ChildElement(xmlNodePtr Parent, const char* Name);

    /**
     * @brief All child elements of a parent with a given name.
     * @param Parent The parent, borrowed; null gives no children.
     * @param Name The element name.
     * @return The children, borrowed, file order.
     */
    static QVector<xmlNodePtr> ChildElements(xmlNodePtr Parent,
                                             const char* Name);

    /**
     * @brief The trimmed text of a parent's first child element.
     * @param Parent The parent, borrowed; null gives empty text.
     * @param Name The element name.
     * @return The text, or empty when the child is absent.
     */
    static QString ChildText(xmlNodePtr Parent, const char* Name);

    /**
     * @brief Sets the text of a parent's first child element.
     *
     * Creates the element, appended last, when missing; an empty
     * text removes the element.
     * @param Parent The parent, borrowed.
     * @param Name The element name.
     * @param Text The new text.
     */
    static void SetChildText(xmlNodePtr Parent, const char* Name,
                             const QString& Text);

    /**
     * @brief The text content of a node (xmlNodeGetContent).
     * @param Node The node, borrowed; null gives empty text.
     * @return The text content.
     */
    static QString NodeText(xmlNodePtr Node);

    /**
     * @brief Sets the text content of a node (xmlNodeSetContent).
     * @param Node The node, borrowed.
     * @param Text The new text.
     */
    static void SetNodeText(xmlNodePtr Node, const QString& Text);

    /**
     * @brief One attribute value of a node (xmlGetProp).
     * @param Node The node, borrowed; null gives empty text.
     * @param Name The attribute name.
     * @return The value, or empty when absent.
     */
    static QString NodeAttribute(xmlNodePtr Node, const char* Name);

    /**
     * @brief Sets one attribute of a node (xmlSetProp).
     * @param Node The node, borrowed.
     * @param Name The attribute name.
     * @param Value The value; empty removes the attribute.
     */
    static void SetNodeAttribute(xmlNodePtr Node, const char* Name,
                                 const QString& Value);

    /**
     * @brief True when a node carries an attribute (xmlHasProp).
     * @param Node The node, borrowed.
     * @param Name The attribute name.
     * @return True when the attribute is present.
     */
    static bool HasNodeAttribute(xmlNodePtr Node, const char* Name);

    /**
     * @brief Appends a new child element to a parent (xmlNewChild).
     * @param Parent The parent, borrowed; null gives nullptr.
     * @param Name The element name.
     * @return The new child, borrowed (owned by the tree once added).
     */
    static xmlNodePtr NewChildElement(xmlNodePtr Parent, const char* Name);

    /**
     * @brief Deep-copies a node (xmlCopyNode, recursive).
     * @param Node The node to copy, borrowed; null gives nullptr.
     * @return The copy; the caller owns it.
     */
    static xmlNodePtr CopyNode(xmlNodePtr Node);

    /**
     * @brief Frees each node in a list and clears the list.
     * @param Nodes The nodes to free; cleared on return.
     */
    static void FreeNodes(QVector<xmlNodePtr>& Nodes);

    /**
     * @brief True when a node is an element with a given local name.
     * @param Node The node, borrowed.
     * @param Name The element name.
     * @return True for a matching element.
     */
    static bool NodeNameEquals(xmlNodePtr Node, const char* Name);

    /**
     * @brief True when a node is a text node holding only XML whitespace.
     * @param Node The node, borrowed.
     * @return True for a whitespace-only text node.
     */
    static bool IsWhitespaceText(xmlNodePtr Node);

    /**
     * @brief Serializes one node with the section-4 serializer.
     *
     * The node is written at depth 0 with no base indentation;
     * a blank <t> writes as an empty string, a comments-only <t>
     * as its bare comments. Used by the XML tab and the list
     * clipboard fragment, which share this serializer.
     * @param Node The node to serialize, borrowed.
     * @return The serialized node, or empty when there is nothing to write.
     */
    static QString SerializeNode(xmlNodePtr Node);

    /**
     * @brief Serializes body nodes plus preserved items, interleaved
     *        by anchor, at depth 0 with no base indentation.
     *
     * Display only, for the XML tab of the text editor.
     * @param Editable The editable body nodes, borrowed.
     * @param Preserved The preserved nodes, borrowed, with anchors.
     * @return The serialized nodes, in file order.
     */
    static QString SerializeBodyNodes(const QVector<xmlNodePtr>& Editable,
                                      const QVector<DocPreservedItem>& Preserved);

    /**
     * @brief Serializes a detached list node as a clipboard fragment.
     * @param Run The <dl>, <ul>, or <ol> node, borrowed.
     * @return The list element, or empty when the node is not a list.
     */
    static QString ListRunToXml(xmlNodePtr Run);

    /**
     * @brief Reads one list node from a clipboard fragment.
     *
     * The fragment must hold a single structurable <dl>, <ul>, or
     * <ol> element; anything else fails.
     * @param Xml The fragment to read.
     * @param Run The list node, on success; the caller owns it.
     * @return True when the fragment held one list node.
     */
    static bool ListRunFromXml(const QString& Xml, xmlNodePtr& Run);

    /**
     * @brief True when ReadXml found content that cannot be kept.
     *
     * Only a document type declaration, markup other than a line
     * break inside a title, markup inside a section or
     * reference-list name, an organization, or an email, and
     * stray text outside elements at the <rfc> level set this.
     * Saving such a document drops that content; preserved
     * content is not affected.
     */
    bool HadSkippedContent = false;

    /**
     * @brief True when the file already carries the "used as an
     *        aid" comment (Bug 505).
     *
     * ReadXml sets this when the file's top holds the
     * InternetDraftEditor comment. WriteXml re-writes the
     * comment when it is set, so an unchanged re-save does not
     * strip a marker the tool added on an earlier save.
     */
    bool HasAidComment = false;

private:
    /**
     * @brief Replaces the owned tree, freeing the old one.
     * @param Document The tree to take ownership of, or nullptr.
     */
    void ResetDocument(xmlDocPtr Document);

    /** @brief The owned libxml2 document, or nullptr when empty. */
    xmlDocPtr _Document = nullptr;
};

#endif // DOCEDITOR_DOCMODEL_H
