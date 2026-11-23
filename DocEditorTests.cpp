/**
 * @file DocEditorTests.cpp
 * @brief Qt Test suite for the ProtocolDocEditor (Bug 462),
 *        rewritten for the libxml2 document model.
 *
 * The first tests drive the text editor the way it is driven
 * by hand: typing keys, pressing the list button, and reading
 * back the body nodes and the document blocks. Each test gets
 * a fresh TextEditor. The editor sources are compiled in
 * unchanged; the tests reach the editor only through its public
 * API, findChild, and QTest. No test reaches a private member,
 * and no hook was added to the editor for testing.
 *
 * The document model is a libxml2 tree (see
 * doceditor-libxml2-design.md, sections 1, 2, 5, 6, 9, 10).
 * The helpers build xmlNodePtr nodes (a <t> node for a plain
 * paragraph, a <ul>/<ol>/<dl> node for a list, a <sourcecode>
 * node for a code block), the editor takes them through
 * SetBodyNodes, which deep-copies them, and BodyNodes returns
 * deep copies the caller owns: the tests free each node passed
 * to SetBodyNodes with xmlFreeNode right after the call, and
 * free each read-back list with DocDocument::FreeNodes. Nodes
 * borrowed from a DocDocument (Bug 467) are never freed. A
 * failed comparison returns from the test early, so a failing
 * run may leak its read-back nodes; a passing run frees them
 * all.
 *
 * The three cases cover the three most recently fixed behaviors:
 * Enter in a plain paragraph (Bug 461), the list indent after
 * Shift+Enter then Enter (Bug 459), and the paragraph spacing
 * (Bug 460). A fourth check renders the options-marker gear
 * glyph, answering whether it draws as a real glyph in the
 * editor's font on this machine. Six more tests cover the
 * options mechanism (Bug 420), the src placeholder (Bug 466),
 * and the src-only load (Bug 467), including one test that
 * drives the gear's options menu and its Name prompt.
 *
 * @note COMPILE-UNTESTED: this VM cannot compile Qt C++, and
 *       the libxml2 headers are not installed on this VM, so
 *       the suite has not been compiled. Only libxml2 symbols
 *       from the stable core list in the design doc, section
 *       10, are used. Doug's build is the verification.
 */

#include "DocModel.h"
#include "TextEditor.h"

#include <libxml/tree.h>

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QFontMetrics>
#include <QImage>
#include <QInputDialog>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QTemporaryFile>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextList>
#include <QTimer>

/**
 * @brief The text edit of a text editor.
 *
 * The only QTextEdit child: the XML pane is a QPlainTextEdit,
 * so the lookup is unambiguous.
 * @param Editor The text editor.
 * @return The text edit.
 */
static QTextEdit* TextEditOf(TextEditor* Editor)
{
    return Editor->findChild<QTextEdit*>();
}

/**
 * @brief The bulleted-list button of a text editor.
 *
 * The buttons carry no object name, so the button is found by
 * its label. No translator is installed in the test program,
 * so the label is the source text from the constructor.
 * @param Editor The text editor.
 * @return The button, or nullptr when not found.
 */
static QPushButton* BulletedButtonOf(TextEditor* Editor)
{
    for (QPushButton* Button : Editor->findChildren<QPushButton*>()) {
        if (Button->text() == QStringLiteral("Bulleted list")) {
            return Button;
        }
    }
    return nullptr;
}

/**
 * @brief The plain text of a node: its top-level text children
 *        joined, for nodes holding no markup.
 * @param Node The node; a <t> node for a plain paragraph.
 * @return The joined text.
 */
static QString PlainTextOf(xmlNodePtr Node)
{
    QString Text;
    if (Node == nullptr) {
        return Text;
    }
    for (xmlNodePtr Child = Node->children; Child != nullptr;
         Child = Child->next) {
        if (Child->type == XML_TEXT_NODE && Child->content != nullptr) {
            Text += QString::fromUtf8(
                reinterpret_cast<const char*>(Child->content));
        }
    }
    return Text;
}

/**
 * @brief Builds a plain paragraph node: a <t> element holding
 *        the given text, empty when the text is empty.
 * @param Text The paragraph text.
 * @return The node; the caller owns it and must free it with
 *         xmlFreeNode.
 * @note SetBodyNodes deep-copies the node, so the caller frees
 *       its own node right after the call.
 */
static xmlNodePtr MakePlainParagraph(const QString& Text)
{
    xmlNodePtr Paragraph = xmlNewNode(
        nullptr, reinterpret_cast<const xmlChar*>("t"));
    QByteArray TextUtf8 = Text.toUtf8();
    xmlNodeSetContent(Paragraph,
                      reinterpret_cast<const xmlChar*>(TextUtf8.constData()));
    return Paragraph;
}

/**
 * @brief The block at a document position, by index.
 * @param Document The document.
 * @param Index The block index, from 0.
 * @return The block, invalid when out of range.
 */
static QTextBlock BlockAt(QTextDocument* Document, int Index)
{
    QTextBlock Block = Document->begin();
    for (int Step = 0; Step < Index && Block.isValid(); ++Step) {
        Block = Block.next();
    }
    return Block;
}

/**
 * @brief Builds a list node: a <ul>, <ol>, or <dl> element
 *        holding one item per given text.
 *
 * A bulleted or numbered item is an <li> holding a <t> with
 * the text. A definition item is a <dt> holding the text
 * directly: the old helper's items carried no continuation
 * flag, so every one of them was a term. The tests only
 * exercise "ul".
 * @param Kind The list element name: ul, ol, or dl.
 * @param Items The item texts.
 * @return The node; the caller owns it and must free it with
 *         xmlFreeNode.
 * @note SetBodyNodes deep-copies the node, so the caller frees
 *       its own node right after the call.
 */
static xmlNodePtr MakeListParagraph(const QString& Kind,
                                    const QStringList& Items)
{
    QByteArray KindUtf8 = Kind.toUtf8();
    xmlNodePtr List = xmlNewNode(
        nullptr,
        reinterpret_cast<const xmlChar*>(KindUtf8.constData()));
    for (const QString& Item : Items) {
        QByteArray ItemUtf8 = Item.toUtf8();
        const xmlChar* ItemText =
            reinterpret_cast<const xmlChar*>(ItemUtf8.constData());
        if (Kind == QStringLiteral("dl")) {
            xmlNewChild(List, nullptr,
                        reinterpret_cast<const xmlChar*>("dt"), ItemText);
        } else {
            xmlNodePtr ListItem = xmlNewChild(
                List, nullptr, reinterpret_cast<const xmlChar*>("li"),
                nullptr);
            xmlNewChild(ListItem, nullptr,
                        reinterpret_cast<const xmlChar*>("t"), ItemText);
        }
    }
    return List;
}

/**
 * @brief Builds a sourcecode node: a <sourcecode> element with
 *        the given attributes and text.
 * @param Attributes The element's attributes, in order.
 * @param Text The code text, lines joined with newlines; empty
 *        for a src-only block.
 * @return The node; the caller owns it and must free it with
 *         xmlFreeNode.
 * @note SetBodyNodes deep-copies the node, so the caller frees
 *       its own node right after the call.
 */
static xmlNodePtr MakeSourceParagraph(
    const QVector<QPair<QString, QString>>& Attributes,
    const QString& Text)
{
    xmlNodePtr Source = xmlNewNode(
        nullptr, reinterpret_cast<const xmlChar*>("sourcecode"));
    for (const QPair<QString, QString>& Attribute : Attributes) {
        QByteArray NameUtf8 = Attribute.first.toUtf8();
        QByteArray ValueUtf8 = Attribute.second.toUtf8();
        xmlSetProp(Source,
                   reinterpret_cast<const xmlChar*>(NameUtf8.constData()),
                   reinterpret_cast<const xmlChar*>(ValueUtf8.constData()));
    }
    if (!Text.isEmpty()) {
        QByteArray TextUtf8 = Text.toUtf8();
        xmlNodeSetContent(
            Source,
            reinterpret_cast<const xmlChar*>(TextUtf8.constData()));
    }
    return Source;
}

/**
 * @brief The sourcecode node itself, when the given node is
 *        one.
 * @param Node The node.
 * @return The node, or nullptr when it is not a <sourcecode>
 *         element.
 */
static xmlNodePtr SourceRunOf(xmlNodePtr Node)
{
    if (Node != nullptr &&
        DocDocument::NodeNameEquals(Node, "sourcecode")) {
        return Node;
    }
    return nullptr;
}

/**
 * @brief The value of one attribute of a sourcecode node.
 * @param Node The node.
 * @param Name The attribute name.
 * @return The value, empty when the attribute is absent.
 */
static QString SourceAttributeOf(xmlNodePtr Node, const QString& Name)
{
    if (Node == nullptr) {
        return QString();
    }
    QByteArray NameUtf8 = Name.toUtf8();
    return DocDocument::NodeAttribute(Node, NameUtf8.constData());
}

/**
 * @brief The code text of a sourcecode node: its text children
 *        joined.
 * @param Node The node.
 * @return The text, empty for a src-only block.
 */
static QString SourceTextOf(xmlNodePtr Node)
{
    return PlainTextOf(Node);
}

/**
 * @brief Asserts one read-back holds a sourcecode node with the
 *        given attributes and text, then frees the nodes.
 *
 * The list is taken by value because it holds deep copies the
 * caller owns: BodyNodes hands them to this check, which owns
 * them from here on.
 * @param Nodes The read-back nodes.
 * @param Name The expected name attribute, empty when absent.
 * @param Type The expected type attribute, empty when absent.
 * @param Markers The expected markers attribute, empty when absent.
 * @param Src The expected src attribute, empty when absent.
 * @param Text The expected code text, empty for a src-only block.
 * @note A failed comparison returns from this check early, so
 *       a failing run may leak the read-back nodes; a passing
 *       run frees them all.
 */
static void VerifySourceReadBack(QVector<xmlNodePtr> Nodes,
                                 const QString& Name,
                                 const QString& Type,
                                 const QString& Markers,
                                 const QString& Src,
                                 const QString& Text)
{
    QCOMPARE(Nodes.size(), 1);
    xmlNodePtr Run = SourceRunOf(Nodes[0]);
    QVERIFY(Run != nullptr);
    QCOMPARE(SourceAttributeOf(Run, QStringLiteral("name")), Name);
    QCOMPARE(SourceAttributeOf(Run, QStringLiteral("type")), Type);
    QCOMPARE(SourceAttributeOf(Run, QStringLiteral("markers")), Markers);
    QCOMPARE(SourceAttributeOf(Run, QStringLiteral("src")), Src);
    QCOMPARE(SourceTextOf(Run), Text);
    DocDocument::FreeNodes(Nodes);
}

/**
 * @brief The "Show option markers" checkbox of a text editor.
 *
 * The checkbox carries no object name, so it is found by its
 * label. No translator is installed in the test program, so
 * the label is the source text from the constructor.
 * @param Editor The text editor.
 * @return The checkbox, or nullptr when not found.
 */
static QCheckBox* MarkersCheckBoxOf(TextEditor* Editor)
{
    for (QCheckBox* Check : Editor->findChildren<QCheckBox*>()) {
        if (Check->text() == QStringLiteral("Show option markers")) {
            return Check;
        }
    }
    return nullptr;
}

/**
 * @brief Sets the show-markers checkbox to a state, clicking
 *        it only when it is not already there.
 *
 * The setting is remembered across runs, so a test that needs
 * the gear must set the state instead of assuming it.
 * @param Editor The text editor.
 * @param Shown True when the markers are to show.
 */
static void SetMarkersShown(TextEditor* Editor, bool Shown)
{
    QCheckBox* Check = MarkersCheckBoxOf(Editor);
    QVERIFY(Check != nullptr);
    if (Check->isChecked() != Shown) {
        QTest::mouseClick(Check, Qt::LeftButton);
    }
    QCOMPARE(Check->isChecked(), Shown);
}

/**
 * @brief The first tests for the text editor.
 */
class DocEditorTests : public QObject
{
    Q_OBJECT

private slots:
    /**
     * @brief Shows a fresh text editor for one test.
     */
    void init();

    /**
     * @brief Deletes the text editor after one test.
     */
    void cleanup();

    /**
     * @brief Two Enters end a plain paragraph (Bug 461).
     */
    void Bug461EnterDouble();

    /**
     * @brief One Enter is a new line in the same paragraph
     *        (Bug 461).
     */
    void Bug461EnterSingle();

    /**
     * @brief The bulleted-list button lists only the cursor's
     *        paragraph (Bug 461).
     */
    void Bug461BulletedButton();

    /**
     * @brief Shift+Enter then Enter keeps every list item at
     *        indent 0 (Bug 459).
     */
    void Bug459ListIndent();

    /**
     * @brief Plain paragraphs carry one line of spacing, list
     *        blocks carry none (Bug 460).
     */
    void Bug460ParagraphSpacing();

    /**
     * @brief The gear glyph draws as a real glyph in the
     *        editor's font (options markers, Bug 312).
     */
    void GearGlyphRenders();

    /**
     * @brief The gear shows at the start of a sourcecode
     *        block's first line only, with the set options as
     *        its hover text, and reads back as no gear (Bug 420).
     */
    void Bug420GearShowsAndTooltip();

    /**
     * @brief Toggling the markers off removes the gear and
     *        toggling them on restores it, and the body nodes
     *        read back the same either way (Bug 420).
     */
    void Bug420MarkersToggle();

    /**
     * @brief A src-only block shows the italic placeholder on
     *        the gear's line, alone when markers are off, and
     *        a block holding text shows no placeholder (Bug 466).
     */
    void Bug466SrcPlaceholder();

    /**
     * @brief The placeholder never reaches the body nodes, and
     *        changing the src restamps it with the new file
     *        (Bug 466).
     */
    void Bug466PlaceholderStrippedAndRestamped();

    /**
     * @brief A draft holding a src-only sourcecode element loads
     *        it as a modeled body node, and the text edit shows
     *        the gear and the placeholder for it (Bug 467).
     */
    void Bug467SrcOnlyLoadsAsParagraph();

    /**
     * @brief The gear's menu edits the name through its prompt
     *        (Bug 420).
     */
    void Bug420OptionsMenuName();

private:
    TextEditor* _Editor = nullptr;
};

void DocEditorTests::init()
{
    _Editor = new TextEditor();
    _Editor->show();
    QVERIFY(QTest::qWaitForWindowExposed(_Editor));
    QTextEdit* Edit = TextEditOf(_Editor);
    QVERIFY(Edit != nullptr);
    Edit->setFocus();
}

void DocEditorTests::cleanup()
{
    delete _Editor;
    _Editor = nullptr;
}

void DocEditorTests::Bug461EnterDouble()
{
    // Two Enters end the paragraph: the first Enter makes a
    // new line in the same paragraph, and the second, on the
    // empty line, splits the block.
    QTextEdit* Edit = TextEditOf(_Editor);
    xmlNodePtr Empty = MakePlainParagraph(QString());
    _Editor->SetBodyNodes({Empty});
    xmlFreeNode(Empty);
    QTest::keyClicks(Edit, QStringLiteral("line 1"));
    QTest::keyClick(Edit, Qt::Key_Return);
    QTest::keyClick(Edit, Qt::Key_Return);
    QTest::keyClicks(Edit, QStringLiteral("word"));
    QCOMPARE(Edit->document()->blockCount(), 2);
    QVector<xmlNodePtr> Nodes = _Editor->BodyNodes();
    QCOMPARE(Nodes.size(), 2);
    QCOMPARE(PlainTextOf(Nodes[0]), QStringLiteral("line 1"));
    QCOMPARE(PlainTextOf(Nodes[1]), QStringLiteral("word"));
    DocDocument::FreeNodes(Nodes);
}

void DocEditorTests::Bug461EnterSingle()
{
    // One Enter is a new line in the same paragraph: the soft
    // break reads back as a newline, so the paragraph holds
    // both lines.
    QTextEdit* Edit = TextEditOf(_Editor);
    xmlNodePtr Empty = MakePlainParagraph(QString());
    _Editor->SetBodyNodes({Empty});
    xmlFreeNode(Empty);
    QTest::keyClicks(Edit, QStringLiteral("line 1"));
    QTest::keyClick(Edit, Qt::Key_Return);
    QTest::keyClicks(Edit, QStringLiteral("word"));
    QCOMPARE(Edit->document()->blockCount(), 1);
    QVector<xmlNodePtr> Nodes = _Editor->BodyNodes();
    QCOMPARE(Nodes.size(), 1);
    QCOMPARE(PlainTextOf(Nodes[0]), QStringLiteral("line 1\nword"));
    DocDocument::FreeNodes(Nodes);
}

void DocEditorTests::Bug461BulletedButton()
{
    // The button lists only the paragraph at the cursor: the
    // first line stays a plain paragraph. The Bug 461 report
    // was the button always making line 1 the bullet.
    QTextEdit* Edit = TextEditOf(_Editor);
    xmlNodePtr Empty = MakePlainParagraph(QString());
    _Editor->SetBodyNodes({Empty});
    xmlFreeNode(Empty);
    QTest::keyClicks(Edit, QStringLiteral("line 1"));
    QTest::keyClick(Edit, Qt::Key_Return);
    QTest::keyClick(Edit, Qt::Key_Return);
    QTest::keyClicks(Edit, QStringLiteral("word"));
    QPushButton* Button = BulletedButtonOf(_Editor);
    QVERIFY(Button != nullptr);
    QTest::mouseClick(Button, Qt::LeftButton);
    QTextDocument* Document = Edit->document();
    QCOMPARE(Document->blockCount(), 2);
    QVERIFY(BlockAt(Document, 0).textList() == nullptr);
    QVERIFY(BlockAt(Document, 1).textList() != nullptr);
}

void DocEditorTests::Bug459ListIndent()
{
    // A two-item bulleted list, the cursor at the end of the
    // second item. Shift+Enter adds a continuation paragraph
    // to that item, and Enter in the continuation starts a new
    // item, rebuilding the list. QTextList::remove() leaves
    // indent 1 behind, so without the fix every rebuilt item
    // would sit one level deeper than the new item; the
    // continuation keeps its indent 1, since it never joins
    // the list. The blocks asserted, in document order, are
    // the two items, the continuation, and the new item.
    QTextEdit* Edit = TextEditOf(_Editor);
    xmlNodePtr List = MakeListParagraph(QStringLiteral("ul"),
                                        {QStringLiteral("First"),
                                         QStringLiteral("Second")});
    _Editor->SetBodyNodes({List});
    xmlFreeNode(List);
    QTextDocument* Document = Edit->document();
    QCOMPARE(Document->blockCount(), 2);
    QVERIFY(BlockAt(Document, 0).textList() != nullptr);
    QCOMPARE(BlockAt(Document, 1).textList(),
             BlockAt(Document, 0).textList());
    QCOMPARE(BlockAt(Document, 0).blockFormat().indent(), 0);
    QCOMPARE(BlockAt(Document, 1).blockFormat().indent(), 0);
    QTextCursor Cursor = Edit->textCursor();
    Cursor.movePosition(QTextCursor::End);
    Edit->setTextCursor(Cursor);
    QTest::keyClick(Edit, Qt::Key_Return, Qt::ShiftModifier);
    QTest::keyClick(Edit, Qt::Key_Return);
    QCOMPARE(Document->blockCount(), 4);
    QCOMPARE(BlockAt(Document, 0).text(), QStringLiteral("First"));
    QCOMPARE(BlockAt(Document, 1).text(), QStringLiteral("Second"));
    QCOMPARE(BlockAt(Document, 2).text(), QString());
    QCOMPARE(BlockAt(Document, 3).text(), QString());
    QTextList* Rebuilt = BlockAt(Document, 0).textList();
    QVERIFY(Rebuilt != nullptr);
    QCOMPARE(BlockAt(Document, 1).textList(), Rebuilt);
    QVERIFY(BlockAt(Document, 2).textList() == nullptr);
    QCOMPARE(BlockAt(Document, 3).textList(), Rebuilt);
    QCOMPARE(BlockAt(Document, 0).blockFormat().indent(), 0);
    QCOMPARE(BlockAt(Document, 1).blockFormat().indent(), 0);
    QCOMPARE(BlockAt(Document, 2).blockFormat().indent(), 1);
    QCOMPARE(BlockAt(Document, 3).blockFormat().indent(), 0);
}

void DocEditorTests::Bug460ParagraphSpacing()
{
    // Two plain paragraphs plus a two-item list. Plain blocks
    // carry one line of spacing below (the document default
    // font's line spacing, the same value the editor's
    // ParagraphGap helper returns); list blocks sit tight.
    // Pure state check: no keys are simulated.
    xmlNodePtr Alpha = MakePlainParagraph(QStringLiteral("Alpha"));
    xmlNodePtr Beta = MakePlainParagraph(QStringLiteral("Beta"));
    xmlNodePtr List = MakeListParagraph(QStringLiteral("ul"),
                                        {QStringLiteral("First"),
                                         QStringLiteral("Second")});
    _Editor->SetBodyNodes({Alpha, Beta, List});
    xmlFreeNode(Alpha);
    xmlFreeNode(Beta);
    xmlFreeNode(List);
    QTextDocument* Document = TextEditOf(_Editor)->document();
    qreal Gap = QFontMetrics(Document->defaultFont()).lineSpacing();
    QCOMPARE(Document->blockCount(), 4);
    QCOMPARE(BlockAt(Document, 0).blockFormat().bottomMargin(), Gap);
    QCOMPARE(BlockAt(Document, 1).blockFormat().bottomMargin(), Gap);
    QCOMPARE(BlockAt(Document, 2).blockFormat().bottomMargin(), 0.0);
    QCOMPARE(BlockAt(Document, 3).blockFormat().bottomMargin(), 0.0);
}

void DocEditorTests::GearGlyphRenders()
{
    // The gear (U+2699) must draw as a real glyph in the
    // editor's font. Asking the primary font alone would
    // mislead: Qt falls back to other fonts, so a missing
    // glyph in the primary font can still render. The render
    // itself is the question, so the gear is painted into an
    // image next to U+FFFF, which no font has, and the two
    // must differ: identical pictures would mean the gear
    // drew as the missing-glyph box. The gear picture must
    // also hold ink, so a blank render cannot pass.
    QTextEdit* Edit = TextEditOf(_Editor);
    QFont Font = Edit->font();
    Font.setPixelSize(32);
    auto Render = [&Font](QChar Ch) {
        QImage Image(64, 64, QImage::Format_ARGB32);
        Image.fill(Qt::white);
        QPainter Painter(&Image);
        Painter.setFont(Font);
        Painter.setPen(Qt::black);
        Painter.drawText(Image.rect(), Qt::AlignCenter, QString(Ch));
        Painter.end();
        return Image;
    };
    QImage Gear = Render(QChar(0x2699));
    QImage Missing = Render(QChar(0xFFFF));
    bool HasInk = false;
    for (int Y = 0; Y < Gear.height() && !HasInk; ++Y) {
        for (int X = 0; X < Gear.width(); ++X) {
            if (Gear.pixelColor(X, Y) != Qt::white) {
                HasInk = true;
                break;
            }
        }
    }
    QVERIFY(HasInk);
    QVERIFY(Gear != Missing);
}

void DocEditorTests::Bug420GearShowsAndTooltip()
{
    // A two-line sourcecode block with name, type, and markers
    // set. The sweep puts one gear at the start of the first
    // block only, with the set options as its hover text, and
    // the body nodes read back without the gear.
    SetMarkersShown(_Editor, true);
    QVector<QPair<QString, QString>> Attributes;
    Attributes.append(QPair<QString, QString>(
        QStringLiteral("name"), QStringLiteral("Sample")));
    Attributes.append(QPair<QString, QString>(
        QStringLiteral("type"), QStringLiteral("c")));
    Attributes.append(QPair<QString, QString>(
        QStringLiteral("markers"), QStringLiteral("true")));
    xmlNodePtr Source = MakeSourceParagraph(
        Attributes, QStringLiteral("int x;\nint y;"));
    _Editor->SetBodyNodes({Source});
    xmlFreeNode(Source);
    QTextDocument* Document = TextEditOf(_Editor)->document();
    QCOMPARE(Document->blockCount(), 2);
    QCOMPARE(BlockAt(Document, 0).text(),
             QString(QChar(0x2699)) + QStringLiteral("int x;"));
    QCOMPARE(BlockAt(Document, 1).text(), QStringLiteral("int y;"));
    QTextBlock::iterator First = BlockAt(Document, 0).begin();
    QVERIFY(!First.atEnd());
    QCOMPARE(First.fragment().charFormat().toolTip(),
             QStringLiteral("Source code: name \"Sample\", type \"c\", "
                            "markers on\nRight-click to change options."));
    VerifySourceReadBack(_Editor->BodyNodes(), QStringLiteral("Sample"),
                         QStringLiteral("c"), QStringLiteral("true"),
                         QString(), QStringLiteral("int x;\nint y;"));
}

void DocEditorTests::Bug420MarkersToggle()
{
    // Toggling the markers off removes the gear and toggling
    // them on puts it back, and the body nodes read back the
    // same either way: the markers are display only. The test
    // ends with the markers on, the state the other tests
    // assume.
    SetMarkersShown(_Editor, true);
    QVector<QPair<QString, QString>> Attributes;
    Attributes.append(QPair<QString, QString>(
        QStringLiteral("name"), QStringLiteral("Sample")));
    Attributes.append(QPair<QString, QString>(
        QStringLiteral("markers"), QStringLiteral("true")));
    xmlNodePtr Source = MakeSourceParagraph(
        Attributes, QStringLiteral("int x;"));
    _Editor->SetBodyNodes({Source});
    xmlFreeNode(Source);
    QTextDocument* Document = TextEditOf(_Editor)->document();
    QCOMPARE(BlockAt(Document, 0).text(),
             QString(QChar(0x2699)) + QStringLiteral("int x;"));
    SetMarkersShown(_Editor, false);
    QCOMPARE(BlockAt(Document, 0).text(), QStringLiteral("int x;"));
    VerifySourceReadBack(_Editor->BodyNodes(), QStringLiteral("Sample"),
                         QString(), QStringLiteral("true"),
                         QString(), QStringLiteral("int x;"));
    SetMarkersShown(_Editor, true);
    QCOMPARE(BlockAt(Document, 0).text(),
             QString(QChar(0x2699)) + QStringLiteral("int x;"));
    VerifySourceReadBack(_Editor->BodyNodes(), QStringLiteral("Sample"),
                         QString(), QStringLiteral("true"),
                         QString(), QStringLiteral("int x;"));
}

void DocEditorTests::Bug466SrcPlaceholder()
{
    // A src-only block shows the italic placeholder naming
    // the file, on the gear's line when markers are on and
    // alone when they are off. A block holding real text shows
    // the text and no placeholder.
    SetMarkersShown(_Editor, true);
    QVector<QPair<QString, QString>> Attributes;
    Attributes.append(QPair<QString, QString>(
        QStringLiteral("src"), QStringLiteral("Examples/TestFile.cpp")));
    xmlNodePtr Source = MakeSourceParagraph(Attributes, QString());
    _Editor->SetBodyNodes({Source});
    xmlFreeNode(Source);
    QTextDocument* Document = TextEditOf(_Editor)->document();
    QCOMPARE(Document->blockCount(), 1);
    QCOMPARE(BlockAt(Document, 0).text(),
             QString(QChar(0x2699)) +
                 QStringLiteral("<Src=Examples/TestFile.cpp>"));
    bool Italic = false;
    for (QTextBlock::iterator It = BlockAt(Document, 0).begin();
         !It.atEnd(); ++It) {
        QTextFragment Fragment = It.fragment();
        if (Fragment.text().contains(QStringLiteral("<Src="))) {
            Italic = Fragment.charFormat().fontItalic();
        }
    }
    QVERIFY(Italic);
    SetMarkersShown(_Editor, false);
    QCOMPARE(BlockAt(Document, 0).text(),
             QStringLiteral("<Src=Examples/TestFile.cpp>"));
    SetMarkersShown(_Editor, true);
    xmlNodePtr SourceWithText = MakeSourceParagraph(
        QVector<QPair<QString, QString>>(), QStringLiteral("int x;"));
    _Editor->SetBodyNodes({SourceWithText});
    xmlFreeNode(SourceWithText);
    QCOMPARE(BlockAt(Document, 0).text(),
             QString(QChar(0x2699)) + QStringLiteral("int x;"));
}

void DocEditorTests::Bug466PlaceholderStrippedAndRestamped()
{
    // The placeholder never reaches the body nodes: the
    // read-back holds the src attribute and no text. Changing
    // the src restamps the placeholder with the new file.
    SetMarkersShown(_Editor, true);
    QVector<QPair<QString, QString>> First;
    First.append(QPair<QString, QString>(
        QStringLiteral("src"), QStringLiteral("Examples/TestFile.cpp")));
    xmlNodePtr FirstSource = MakeSourceParagraph(First, QString());
    _Editor->SetBodyNodes({FirstSource});
    xmlFreeNode(FirstSource);
    QTextDocument* Document = TextEditOf(_Editor)->document();
    VerifySourceReadBack(_Editor->BodyNodes(), QString(), QString(),
                         QString(),
                         QStringLiteral("Examples/TestFile.cpp"),
                         QString());
    QVector<QPair<QString, QString>> Second;
    Second.append(QPair<QString, QString>(
        QStringLiteral("src"), QStringLiteral("Other/File.cpp")));
    xmlNodePtr SecondSource = MakeSourceParagraph(Second, QString());
    _Editor->SetBodyNodes({SecondSource});
    xmlFreeNode(SecondSource);
    QCOMPARE(BlockAt(Document, 0).text(),
             QString(QChar(0x2699)) +
                 QStringLiteral("<Src=Other/File.cpp>"));
    VerifySourceReadBack(_Editor->BodyNodes(), QString(), QString(),
                         QString(), QStringLiteral("Other/File.cpp"),
                         QString());
}

void DocEditorTests::Bug467SrcOnlyLoadsAsParagraph()
{
    // A draft holding a src-only sourcecode element loads it
    // as a modeled body node, not a preserved node: the node
    // carries the src attribute and no text, and the text edit
    // shows the gear and the placeholder for it. The file is
    // written to a temporary file and closed before reading, so
    // the reader opens it the way it opens any draft.
    QTemporaryFile File;
    QVERIFY(File.open());
    File.write("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
               "<rfc docName=\"draft-test-00\" category=\"std\" "
               "ipr=\"trust200902\" submissionType=\"IETF\">\n"
               "  <front>\n"
               "    <title>Test</title>\n"
               "    <author fullname=\"Test Author\">\n"
               "      <organization>Test</organization>\n"
               "    </author>\n"
               "    <abstract><t>Abstract.</t></abstract>\n"
               "  </front>\n"
               "  <middle>\n"
               "    <section anchor=\"introduction\">\n"
               "      <name>Introduction</name>\n"
               "      <sourcecode src=\"Examples/TestFile.cpp\"/>\n"
               "    </section>\n"
               "  </middle>\n"
               "  <back/>\n"
               "</rfc>\n");
    File.close();
    DocDocument Loaded;
    QString Error;
    QVERIFY2(Loaded.ReadXml(File.fileName(), Error), qPrintable(Error));
    // Sections and Nodes are borrowed from Loaded and are never
    // freed; SetBodyNodes deep-copies the nodes it is given.
    QVector<xmlNodePtr> Sections = Loaded.TopSectionNodes();
    QCOMPARE(Sections.size(), 1);
    QVector<xmlNodePtr> Nodes = DocDocument::BodyNodes(Sections[0]);
    QCOMPARE(Nodes.size(), 1);
    xmlNodePtr Run = SourceRunOf(Nodes[0]);
    QVERIFY(Run != nullptr);
    QCOMPARE(SourceAttributeOf(Run, QStringLiteral("src")),
             QStringLiteral("Examples/TestFile.cpp"));
    QCOMPARE(SourceTextOf(Run), QString());
    SetMarkersShown(_Editor, true);
    _Editor->SetBodyNodes(Nodes);
    QTextDocument* Document = TextEditOf(_Editor)->document();
    QCOMPARE(BlockAt(Document, 0).text(),
             QString(QChar(0x2699)) +
                 QStringLiteral("<Src=Examples/TestFile.cpp>"));
}

void DocEditorTests::Bug420OptionsMenuName()
{
    // The gear's menu edits the name: right-clicking the gear
    // opens the menu, the Name... action opens the prompt, and
    // accepting the prompt with a new name writes it onto the
    // element. The prompt is driven by timers that find the
    // open menu and the open dialog. When the menu is not found
    // it is closed, and when the dialog is not found any modal
    // dialog is rejected, so a failure fails the assertions
    // instead of hanging the run in a modal loop.
    SetMarkersShown(_Editor, true);
    QVector<QPair<QString, QString>> Attributes;
    Attributes.append(QPair<QString, QString>(
        QStringLiteral("name"), QStringLiteral("Sample")));
    Attributes.append(QPair<QString, QString>(
        QStringLiteral("type"), QStringLiteral("c")));
    xmlNodePtr Source = MakeSourceParagraph(
        Attributes, QStringLiteral("int x;"));
    _Editor->SetBodyNodes({Source});
    xmlFreeNode(Source);
    QTextEdit* Edit = TextEditOf(_Editor);
    QTextDocument* Document = Edit->document();
    QVERIFY(BlockAt(Document, 0).text().startsWith(QChar(0x2699)));
    auto DriveInputDialog = [this]() {
        QInputDialog* Dialog = nullptr;
        for (QWidget* Top : QApplication::topLevelWidgets()) {
            Dialog = qobject_cast<QInputDialog*>(Top);
            if (Dialog != nullptr && Dialog->isVisible()) {
                break;
            }
            Dialog = nullptr;
        }
        if (Dialog != nullptr) {
            Dialog->setTextValue(QStringLiteral("Renamed"));
            Dialog->accept();
            return;
        }
        for (QWidget* Top : QApplication::topLevelWidgets()) {
            if (QDialog* Other = qobject_cast<QDialog*>(Top)) {
                if (Other->isVisible() && Other->isModal()) {
                    Other->reject();
                    return;
                }
            }
        }
    };
    auto DriveMenu = [this, DriveInputDialog]() {
        QWidget* Popup = QApplication::activePopupWidget();
        QMenu* Menu = qobject_cast<QMenu*>(Popup);
        if (Menu == nullptr) {
            return;
        }
        QAction* OptionsAction = nullptr;
        for (QAction* Action : Menu->actions()) {
            if (Action->text() == QStringLiteral("Source code options") &&
                Action->menu() != nullptr) {
                OptionsAction = Action;
                break;
            }
        }
        if (OptionsAction == nullptr) {
            Menu->close();
            return;
        }
        QAction* NameAction = nullptr;
        for (QAction* Action : OptionsAction->menu()->actions()) {
            if (Action->text() == QStringLiteral("Name...")) {
                NameAction = Action;
                break;
            }
        }
        if (NameAction == nullptr) {
            Menu->close();
            return;
        }
        QTimer::singleShot(200, this, DriveInputDialog);
        NameAction->trigger();
        Menu->close();
    };
    QTimer::singleShot(300, this, DriveMenu);
    QTextCursor Cursor(Document);
    Cursor.setPosition(0);
    QPoint GearPos = Edit->cursorRect(Cursor).center();
    QTest::mouseClick(Edit->viewport(), Qt::RightButton, Qt::NoModifier,
                      GearPos);
    VerifySourceReadBack(_Editor->BodyNodes(), QStringLiteral("Renamed"),
                         QStringLiteral("c"), QString(),
                         QString(), QStringLiteral("int x;"));
}

QTEST_MAIN(DocEditorTests)

#include "DocEditorTests.moc"
