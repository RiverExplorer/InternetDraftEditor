// Copyright (c) 2026 RiverExplorer LLC
// RiverExplorer is a registered trademark of Douglas Mark Royer.
//
// This software is released under the MIT License.
// See LICENSE.md for the full license text.

/**
 * @file LegendDialog.cpp
 * @brief The xml2rfc legend: the language, described by topic.
 *
 * The content comes from RFC 7991 (the v3 vocabulary) and the
 * v3.rng schema: the descriptions follow the RFC's own terms,
 * and the "where it goes" notes follow the schema's content
 * models. Elements added after RFC 7991 (artset, stream, toc,
 * u, and the extra postal parts) are described from the schema
 * and the xml2rfc changelog.
 */

#include "LegendDialog.h"

#include <QPushButton>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>

/**
 * @brief One legend entry: an element, what it is, and where it goes.
 */
struct LegendEntry
{
    const char* Name;
    const char* Text;
    const char* Where;
    bool Deprecated;
};

/**
 * @brief One legend tab: a topic, an introduction, and its entries.
 */
struct LegendTab
{
    const char* Title;
    const char* Intro;
    const LegendEntry* Entries;
    int Count;
};

static const LegendEntry DocumentEntries[] = {
    {"back", "The back matter: references, appendices, and the authors' addresses.",
     "In rfc.", false},
    {"front", "The front matter: title, authors, date, and abstract.",
     "In rfc, and in reference.", false},
    {"link", "A link from the document to an external resource, such as a style sheet or a canonical copy.",
     "In rfc, before front.", false},
    {"middle", "The body of the document: its sections.",
     "In rfc.", false},
    {"rfc", "The whole document. Its attributes name the draft, its category, and its intellectual property rules.",
     "The root; holds link, front, middle, and back.", false},
};

static const LegendEntry FrontEntries[] = {
    {"abstract", "A summary of the document, shown on the front page.",
     "In front.", false},
    {"address", "Contact information for an author: postal address, phone, email, and web address.",
     "In author and contact.", false},
    {"area", "The IETF area the document relates to, by full name or abbreviation. Currently unused when generating documents.",
     "In front.", false},
    {"author", "One author of the document, or of a referenced document. An author can also be just an organization.",
     "In front, and in sections holding authors' addresses.", false},
    {"boilerplate", "The generated front-page text: status, copyright, and notices. Filled in by the prep tool, not written by hand.",
     "In front.", false},
    {"city", "The city in a postal address.",
     "In postal.", false},
    {"cityarea", "The city area or district in a postal address.",
     "In postal.", false},
    {"code", "The postal code in a postal address.",
     "In postal.", false},
    {"country", "The country name or code in a postal address.",
     "In postal.", false},
    {"date", "The publication date. For a draft it is always the current date until submission; the prep tool uses it to compute expiry.",
     "In front.", false},
    {"email", "An email address.",
     "In address.", false},
    {"extaddr", "An extra address line, such as an apartment or suite number.",
     "In postal.", false},
    {"facsimile", "A fax number. No replacement; fax numbers are no longer used.",
     "In address.", true},
    {"keyword", "A keyword for searching and indexing the document.",
     "In front.", false},
    {"note", "A note shown in the front matter, before the body.",
     "In front.", false},
    {"organization", "An organization name.",
     "In author and contact.", false},
    {"phone", "A phone number.",
     "In address.", false},
    {"pobox", "A post office box in a postal address.",
     "In postal.", false},
    {"postal", "A postal address, built from street, city, region, code, and country parts, or from free-form lines.",
     "In address.", false},
    {"postalLine", "One free-form line of a postal address, used instead of the structured parts.",
     "In postal.", false},
    {"region", "The state, province, or region in a postal address.",
     "In postal.", false},
    {"seriesInfo", "Identifies the document's series and number, such as Internet-Draft or RFC, or a DOI.",
     "In front, and in reference.", false},
    {"sortingcode", "A postal sorting code in a postal address.",
     "In postal.", false},
    {"street", "The street address in a postal address.",
     "In postal.", false},
    {"title", "The title of the document. Holds text and line breaks only.",
     "In front.", false},
    {"toc", "The table of contents. Filled in by the prep tool.",
     "In front.", false},
    {"uri", "A web address.",
     "In address.", false},
    {"workgroup", "The working group that produced the document.",
     "In front.", false},
};

static const LegendEntry SectionsEntries[] = {
    {"aside", "Content that is less important or tangential to what surrounds it, shown indented.",
     "In sections and definition descriptions.", false},
    {"blockquote", "A block of quoted text.",
     "In sections and other blocks.", false},
    {"name", "The title of a section, figure, table, note, or references list.",
     "At the start of sections, figures, tables, notes, and references.", false},
    {"section", "A section of the document, numbered unless marked otherwise. Sections nest inside sections.",
     "In middle, back, sections, boilerplate, and toc.", false},
    {"t", "A paragraph of text. The main block of writing in the document.",
     "In sections, abstracts, notes, list items, and table cells.", false},
};

static const LegendEntry InlineEntries[] = {
    {"bcp14", "A BCP 14 requirement word: MUST, SHOULD, MAY, and their negatives.",
     "In text.", false},
    {"br", "A line break. Several in a row count as one.",
     "In text and titles.", false},
    {"contact", "Contact information: an organization name and an address.",
     "In text, and in sections holding authors' addresses.", false},
    {"cref", "A comment, visible while the document is work in progress.",
     "In text.", false},
    {"em", "Emphasized text, usually shown in italics.",
     "In text.", false},
    {"eref", "An external link to a web address.",
     "In text.", false},
    {"iref", "An index entry. It prints nothing; it marks a spot for the back-of-document index.",
     "In text, and in figures, sections, and tables.", false},
    {"list", "A list inside a paragraph. Use ul, ol, or dl instead.",
     "In t.", true},
    {"relref", "A reference to a section of another document.",
     "In text.", false},
    {"spanx", "Text with a formatting style. Use em, strong, or tt instead.",
     "In text.", true},
    {"strong", "Strong text, usually shown in bold.",
     "In text.", false},
    {"sub", "Subscript text.",
     "In text.", false},
    {"sup", "Superscript text.",
     "In text.", false},
    {"tt", "Teletype text, shown in a constant-width font.",
     "In text.", false},
    {"u", "Unicode text: non-ASCII characters, expanded for plain-text output.",
     "In text.", false},
    {"vspace", "Blank lines in the output.",
     "In t.", true},
    {"xref", "A cross-reference to an anchor in this document: a section, figure, table, or reference.",
     "In text.", false},
};

static const LegendEntry ListsEntries[] = {
    {"dd", "The definition part of an entry in a definition list.",
     "In dl, after a dt.", false},
    {"dl", "A definition list: terms paired with definitions.",
     "In sections, abstracts, notes, list items, and table cells.", false},
    {"dt", "The term part of an entry in a definition list.",
     "In dl, before a dd.", false},
    {"li", "One item in a bulleted or numbered list. Holds paragraphs, or text directly.",
     "In ul and ol.", false},
    {"ol", "An ordered (numbered or lettered) list.",
     "In sections, abstracts, notes, list items, and table cells.", false},
    {"ul", "An unordered (bulleted) list.",
     "In sections, abstracts, notes, list items, and table cells.", false},
};

static const LegendEntry FiguresEntries[] = {
    {"artset", "A set of artwork versions for one figure, such as text and SVG; the formatter picks the best one.",
     "In figures and other blocks.", false},
    {"artwork", "Artwork: diagrams and line art with exact spacing and line breaks, or an external graphic such as SVG.",
     "In figures, artsets, sections, and other blocks.", false},
    {"figure", "A captioned figure holding artwork, source code, or a set of artwork versions.",
     "In sections and other blocks.", false},
    {"postamble", "Text after the artwork in a figure. Use paragraphs around the figure instead.",
     "In figures and texttables.", true},
    {"preamble", "Text before the artwork in a figure. Use paragraphs around the figure instead.",
     "In figures and texttables.", true},
    {"sourcecode", "Source code or formal language text, with exact spacing and line breaks.",
     "In figures, sections, and other blocks.", false},
};

static const LegendEntry TablesEntries[] = {
    {"c", "A cell in a text table.",
     "In texttable.", true},
    {"table", "A table with a caption, a header, a body, and an optional footer.",
     "In sections and other blocks.", false},
    {"tbody", "The body rows of a table.",
     "In table.", false},
    {"td", "A data cell in a table row.",
     "In tr.", false},
    {"texttable", "A table made of preformatted text columns. Use table instead.",
     "In sections.", true},
    {"tfoot", "The footer rows of a table.",
     "In table.", false},
    {"th", "A header cell in a table row.",
     "In tr.", false},
    {"thead", "The header rows of a table.",
     "In table.", false},
    {"tr", "One row of a table.",
     "In thead, tbody, and tfoot.", false},
    {"ttcol", "A column in a text table.",
     "In texttable.", true},
};

static const LegendEntry ReferencesEntries[] = {
    {"annotation", "Extra prose shown after a reference entry.",
     "In reference.", false},
    {"displayreference", "Shows a different name for a reference anchor in the body text.",
     "In back.", false},
    {"format", "Named a format a reference is available in.",
     "In reference.", true},
    {"refcontent", "Additional text shown as part of a reference entry.",
     "In reference.", false},
    {"reference", "One bibliographic reference: its front matter plus notes about it.",
     "In references and referencegroup.", false},
    {"referencegroup", "A group of references cited as one, such as the parts of a standard.",
     "In references.", false},
    {"references", "A list of references, normative or informative.",
     "In back, and inside references.", false},
    {"stream", "The stream a referenced document belongs to, such as IETF or IRTF.",
     "In reference.", false},
};

static const LegendTab LegendTabs[] = {
    {"Document", "The skeleton of every document.",
     DocumentEntries, sizeof(DocumentEntries) / sizeof(DocumentEntries[0])},
    {"Front Matter", "Everything before the body: who wrote it, what it is called, and when.",
     FrontEntries, sizeof(FrontEntries) / sizeof(FrontEntries[0])},
    {"Sections & Text", "The blocks the body is built from.",
     SectionsEntries, sizeof(SectionsEntries) / sizeof(SectionsEntries[0])},
    {"Inline Marks", "The marks inside text: emphasis, links, and the rest.",
     InlineEntries, sizeof(InlineEntries) / sizeof(InlineEntries[0])},
    {"Lists", "Three kinds of list.",
     ListsEntries, sizeof(ListsEntries) / sizeof(ListsEntries[0])},
    {"Figures & Code", "Pictures, diagrams, and code.",
     FiguresEntries, sizeof(FiguresEntries) / sizeof(FiguresEntries[0])},
    {"Tables", "Tables, and the old text tables.",
     TablesEntries, sizeof(TablesEntries) / sizeof(TablesEntries[0])},
    {"References", "The reference list and its entries.",
     ReferencesEntries, sizeof(ReferencesEntries) / sizeof(ReferencesEntries[0])},
};

/**
 * @brief Builds the rich text for one tab.
 * @param Tab The tab.
 * @return The tab's HTML.
 */
static QString TabHtml(const LegendTab& Tab)
{
    QString Html = QStringLiteral("<p><i>%1</i></p>").arg(
        QString::fromLatin1(Tab.Intro));
    for (int i = 0; i < Tab.Count; ++i) {
        const LegendEntry& Entry = Tab.Entries[i];
        Html += QStringLiteral("<p><b>&lt;%1&gt;</b>").arg(
            QString::fromLatin1(Entry.Name));
        if (Entry.Deprecated) {
            Html += QStringLiteral(" <i>(deprecated)</i>");
        }
        Html += QStringLiteral(" &mdash; %1 <i>%2</i></p>").arg(
            QString::fromLatin1(Entry.Text),
            QString::fromLatin1(Entry.Where));
    }
    return Html;
}

LegendDialog::LegendDialog(QWidget* Parent)
    : QDialog(Parent)
{
    setWindowTitle(tr("xml2rfc Legend"));
    resize(700, 520);

    QTabWidget* Tabs = new QTabWidget(this);
    for (const LegendTab& Tab : LegendTabs) {
        QTextEdit* Page = new QTextEdit(Tabs);
        Page->setReadOnly(true);
        Page->setHtml(TabHtml(Tab));
        Tabs->addTab(Page, tr(Tab.Title));
    }

    QPushButton* CloseButton = new QPushButton(tr("Close"), this);
    connect(CloseButton, &QPushButton::clicked, this, &QDialog::accept);

    QVBoxLayout* Layout = new QVBoxLayout(this);
    Layout->addWidget(Tabs, 1);
    Layout->addWidget(CloseButton, 0, Qt::AlignRight);
}
