// Copyright (c) 2026 RiverExplorer LLC
// RiverExplorer is a registered trademark of Douglas Mark Royer.
//
// This software is released under the MIT License.
// See LICENSE.md for the full license text.

/**
 * @file FigureEditor.h
 * @brief The figures editor (Bug 504): a mouse-draw ASCII canvas
 *        with a tag list below the figure.
 *
 * The dialog edits one figure and its tag list as one pop-up.
 * The upper pane is the artwork canvas: a fixed-width text grid
 * where the mouse draws flowchart shapes (rectangle, diamond,
 * oval, parallelogram, line, arrow) or types text. The lower
 * pane is the tag list: tag versus description rows, written as
 * a <dl> below the <figure> in the XML, grouped with the figure
 * by three comments carrying the figure's anchor. A tag's
 * <dt> can carry an anchor, so hand-written content can
 * cross-reference it.
 */

#ifndef DOCEDITOR_FIGUREEDITOR_H
#define DOCEDITOR_FIGUREEDITOR_H

#include <QDialog>
#include <QPlainTextEdit>
#include <QPoint>
#include <QVector>
#include <libxml/tree.h>

class DocDocument;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QDialogButtonBox;

/**
 * @brief One row of the tag list: a tag, its description, and
 *        the tag's anchor (empty when the tag has none).
 */
struct FigureTagRow
{
    QString Tag;
    QString Description;
    QString Anchor;
};

/**
 * @brief The artwork canvas: a fixed-width text grid the mouse
 *        draws flowchart shapes on (Bug 504).
 *
 * The text is the <artwork> text itself: each line is one row
 * of the grid, and drawing a shape sets the characters of the
 * cells it covers. Typing inserts at the cursor, pushing
 * existing characters right (Bug 552: the canvas no longer
 * uses overwrite mode). Undo is a
 * snapshot stack: one entry per completed stroke or typing
 * run, restored by Ctrl+Z or the Undo button.
 */
class FigureCanvas : public QPlainTextEdit
{
    Q_OBJECT

public:
    /**
     * @brief The drawing tool in hand.
     */
    enum Tool
    {
        Text,           /**< Click to place the cursor, then type. */
        Rectangle,      /**< Drag a rectangle. */
        Diamond,        /**< Drag a diamond (decision). */
        Oval,           /**< Drag an oval. */
        Parallelogram,  /**< Drag a parallelogram (input/output). */
        Line,           /**< Drag a line. */
        Arrow           /**< Drag a line with an arrowhead. */
    };

    /**
     * @brief Builds the canvas with a fixed-width font.
     * @param Parent The parent widget.
     */
    explicit FigureCanvas(QWidget* Parent = nullptr);

    /**
     * @brief Sets the drawing tool in hand.
     * @param Value The tool.
     */
    void setTool(Tool Value);

    /**
     * @brief The drawing tool in hand.
     * @return The tool.
     */
    Tool tool() const;

    /**
     * @brief Undoes the last stroke or typing run.
     */
    void undoLastStroke();

    /**
     * @brief True when an undo step is available.
     * @return True when the undo stack is not empty.
     */
    bool canUndo() const;

protected:
    void mousePressEvent(QMouseEvent* Event) override;
    void mouseMoveEvent(QMouseEvent* Event) override;
    void mouseReleaseEvent(QMouseEvent* Event) override;
    void keyPressEvent(QKeyEvent* Event) override;

private:
    /**
     * @brief The cell (column, row) under a mouse position.
     * @param Pos The position in the viewport.
     * @return The cell, clamped to the grid origin.
     */
    QPoint cellAt(const QPoint& Pos) const;

    /**
     * @brief Draws the current shape from the drag start to a cell.
     * @param To The cell the drag has reached.
     */
    void drawShapeTo(const QPoint& To);

    /**
     * @brief Pushes the current text on the undo stack, once.
     *
     * A snapshot is pushed when a drag or typing run starts,
     * so one undo step reverts that whole stroke or run. A
     * snapshot identical to the stack top is not pushed twice.
     */
    void pushUndoSnapshot();

    Tool _Tool = Text;
    bool _Dragging = false;
    QPoint _StartCell;
    QStringList _Snapshot;
    QVector<QString> _UndoStack;
};

/**
 * @brief The figures editor dialog (Bug 504).
 *
 * Upper pane: the artwork canvas. Middle: the figure's caption
 * and anchor. Lower pane: the tag list (tag, description,
 * anchor). OK writes the figure and its tag list; Cancel
 * discards everything.
 */
class FigureEditorDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Builds the dialog with its canvas and tag list.
     * @param Parent The parent widget.
     */
    explicit FigureEditorDialog(QWidget* Parent = nullptr);

    /**
     * @brief Sets the artwork text shown on the canvas.
     * @param Text The <artwork> text.
     */
    void setArtworkText(const QString& Text);

    /**
     * @brief The artwork text on the canvas.
     * @return The <artwork> text.
     */
    QString artworkText() const;

    /**
     * @brief Sets the figure's caption.
     *
     * The caption is display-only: it is the plain text of
     * the <name> node when one was given with setNameNode,
     * and the string given here otherwise. A later setNameNode
     * refreshes the preview from the node.
     * @param Caption The <name> text.
     */
    void setCaption(const QString& Caption);

    /**
     * @brief The figure's caption, as plain text.
     *
     * When a <name> node was given with setNameNode, this is
     * the node's text content (markup stripped); otherwise it
     * is the caption string given with setCaption.
     * @return The <name> text.
     */
    QString caption() const;

    /**
     * @brief Gives the dialog the figure's <name> node.
     *
     * The node is borrowed, owned by the caller, and edited
     * in place by the Edit Caption button through the shared
     * inline editor. Passing a node enables that button; with
     * no node the caption stays a plain string and the button
     * stays disabled. The preview refreshes from the node's
     * text, so the string from setCaption and the node agree
     * only when the caller passes the node the caption came
     * from.
     * @param NameNode The <name> node, borrowed; null keeps
     *        the plain-text caption path.
     * @param Document The document, for the inline editor; may
     *        be null.
     */
    void setNameNode(xmlNodePtr NameNode, const DocDocument* Document);

    /**
     * @brief The figure's <name> node, edited in place.
     *
     * The caller reads this after acceptance to carry the
     * edited runs into the figure it writes back: the node
     * already holds the runs, so its text must not be written
     * back over it as plain text.
     * @return The node passed to setNameNode, or null.
     */
    xmlNodePtr nameNode() const;

    /**
     * @brief Sets the figure's anchor.
     * @param Anchor The anchor, empty when the figure has none.
     */
    void setAnchor(const QString& Anchor);

    /**
     * @brief The figure's anchor.
     * @return The anchor, trimmed.
     */
    QString anchor() const;

    /**
     * @brief Sets the tag list rows.
     * @param Rows The rows.
     */
    void setTagRows(const QVector<FigureTagRow>& Rows);

    /**
     * @brief The tag list rows, without blank rows.
     * @return The rows.
     */
    QVector<FigureTagRow> tagRows() const;

private:
    /**
     * @brief Fills the tag table from a row list.
     * @param Rows The rows.
     */
    void fillTagTable(const QVector<FigureTagRow>& Rows);

    /**
     * @brief Opens the inline editor on the figure's <name>
     *        node and refreshes the caption preview.
     *
     * Does nothing when no <name> node was given.
     */
    void OnEditCaption();

    FigureCanvas* _Canvas = nullptr;
    QLineEdit* _CaptionEdit = nullptr;
    QPushButton* _EditCaptionButton = nullptr;
    xmlNodePtr _NameNode = nullptr;
    const DocDocument* _Document = nullptr;
    QLineEdit* _AnchorEdit = nullptr;
    QTableWidget* _TagTable = nullptr;
    QDialogButtonBox* _Buttons = nullptr;
};

#endif // DOCEDITOR_FIGUREEDITOR_H
