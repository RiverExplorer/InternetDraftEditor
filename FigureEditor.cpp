// Copyright (c) 2026 RiverExplorer LLC
// RiverExplorer is a registered trademark of Douglas Mark Royer.
//
// This software is released under the MIT License.
// See LICENSE.md for the full license text.

/**
 * @file FigureEditor.cpp
 * @brief The figures editor (Bug 504): a mouse-draw ASCII canvas
 *        with a tag list below the figure.
 */

#include "FigureEditor.h"

#include "DocModel.h"
#include "TextEditor.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtMath>
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// The shape rasterization: pure functions on a grid of lines.
// Each line is one row; a cell holds one character. The grid
// grows as shapes reach past its edges, padded with spaces.
// ---------------------------------------------------------------------------

/**
 * @brief Grows the grid so a cell exists, padding with spaces.
 * @param Lines The grid.
 * @param Row The row.
 * @param Col The column.
 */
static void EnsureCell(QStringList& Lines, int Row, int Col)
{
    while (Lines.size() <= Row) {
        Lines.append(QString());
    }
    while (Lines[Row].size() <= Col) {
        Lines[Row].append(QLatin1Char(' '));
    }
}

/**
 * @brief Sets one cell of the grid.
 * @param Lines The grid.
 * @param Row The row.
 * @param Col The column.
 * @param Ch The character.
 */
static void SetCell(QStringList& Lines, int Row, int Col, QChar Ch)
{
    if (Row < 0 || Col < 0) {
        return;
    }
    EnsureCell(Lines, Row, Col);
    Lines[Row][Col] = Ch;
}

/**
 * @brief The character for a line cell, from its overall direction.
 * @param Dx The line's horizontal run.
 * @param Dy The line's vertical run.
 * @return '-' when mostly horizontal, '|' when mostly vertical,
 *         '\' when the run goes down-right or up-left, '/'
 *         when it goes down-left or up-right.
 */
static QChar LineChar(int Dx, int Dy)
{
    if (Dy == 0) {
        return QLatin1Char('-');
    }
    if (Dx == 0) {
        return QLatin1Char('|');
    }
    if (std::abs(Dx) > 2 * std::abs(Dy)) {
        return QLatin1Char('-');
    }
    if (std::abs(Dy) > 2 * std::abs(Dx)) {
        return QLatin1Char('|');
    }
    return (Dx * Dy > 0) ? QLatin1Char('\\') : QLatin1Char('/');
}

/**
 * @brief Draws a straight line between two cells (Bresenham).
 * @param Lines The grid.
 * @param X0 The start column.
 * @param Y0 The start row.
 * @param X1 The end column.
 * @param Y1 The end row.
 */
static void DrawLine(QStringList& Lines, int X0, int Y0, int X1, int Y1)
{
    QChar Ch = LineChar(X1 - X0, Y1 - Y0);
    int Dx = std::abs(X1 - X0);
    int Dy = -std::abs(Y1 - Y0);
    int Sx = X0 < X1 ? 1 : -1;
    int Sy = Y0 < Y1 ? 1 : -1;
    int Err = Dx + Dy;
    int X = X0;
    int Y = Y0;
    while (true) {
        SetCell(Lines, Y, X, Ch);
        if (X == X1 && Y == Y1) {
            break;
        }
        int Next = 2 * Err;
        if (Next >= Dy) {
            Err += Dy;
            X += Sx;
        }
        if (Next <= Dx) {
            Err += Dx;
            Y += Sy;
        }
    }
}

/**
 * @brief Draws a rectangle between two corners.
 * @param Lines The grid.
 * @param X0 One corner's column.
 * @param Y0 One corner's row.
 * @param X1 The other corner's column.
 * @param Y1 The other corner's row.
 */
static void DrawRectangle(QStringList& Lines, int X0, int Y0, int X1,
                          int Y1)
{
    int Left = std::min(X0, X1);
    int Right = std::max(X0, X1);
    int Top = std::min(Y0, Y1);
    int Bottom = std::max(Y0, Y1);
    for (int X = Left + 1; X < Right; ++X) {
        SetCell(Lines, Top, X, QLatin1Char('-'));
        SetCell(Lines, Bottom, X, QLatin1Char('-'));
    }
    for (int Y = Top + 1; Y < Bottom; ++Y) {
        SetCell(Lines, Y, Left, QLatin1Char('|'));
        SetCell(Lines, Y, Right, QLatin1Char('|'));
    }
    // The corners are set last, so they win over the edges:
    // a one-cell-wide rectangle is a column of '|' with '+'
    // at its ends, and a one-cell-tall one a row of '-'.
    SetCell(Lines, Top, Left, QLatin1Char('+'));
    SetCell(Lines, Top, Right, QLatin1Char('+'));
    SetCell(Lines, Bottom, Left, QLatin1Char('+'));
    SetCell(Lines, Bottom, Right, QLatin1Char('+'));
}

/**
 * @brief Draws a diamond (decision) inside a bounding box.
 * @param Lines The grid.
 * @param X0 One corner's column.
 * @param Y0 One corner's row.
 * @param X1 The other corner's column.
 * @param Y1 The other corner's row.
 */
static void DrawDiamond(QStringList& Lines, int X0, int Y0, int X1, int Y1)
{
    int Left = std::min(X0, X1);
    int Right = std::max(X0, X1);
    int Top = std::min(Y0, Y1);
    int Bottom = std::max(Y0, Y1);
    int Cx = (Left + Right) / 2;
    int Cy = (Top + Bottom) / 2;
    DrawLine(Lines, Cx, Top, Right, Cy);
    DrawLine(Lines, Right, Cy, Cx, Bottom);
    DrawLine(Lines, Cx, Bottom, Left, Cy);
    DrawLine(Lines, Left, Cy, Cx, Top);
    SetCell(Lines, Top, Cx, QLatin1Char('+'));
    SetCell(Lines, Cy, Right, QLatin1Char('+'));
    SetCell(Lines, Bottom, Cx, QLatin1Char('+'));
    SetCell(Lines, Cy, Left, QLatin1Char('+'));
}

/**
 * @brief Pi, as a literal: qPi() is not visible in this Qt build.
 */
constexpr double FigurePi = 3.14159265358979323846;

/**
 * @brief Draws an oval inside a bounding box.
 *
 * The ellipse is sampled by angle; each sample's character
 * follows its tangent, so the flat top and bottom read '-',
 * the sides read '|', and the diagonals read '/' or '\'.
 * @param Lines The grid.
 * @param X0 One corner's column.
 * @param Y0 One corner's row.
 * @param X1 The other corner's column.
 * @param Y1 The other corner's row.
 */
static void DrawOval(QStringList& Lines, int X0, int Y0, int X1, int Y1)
{
    int Left = std::min(X0, X1);
    int Right = std::max(X0, X1);
    int Top = std::min(Y0, Y1);
    int Bottom = std::max(Y0, Y1);
    double Rx = (Right - Left) / 2.0;
    double Ry = (Bottom - Top) / 2.0;
    if (Rx < 1.0 || Ry < 1.0) {
        // Too small for an oval: fall back to a rectangle.
        DrawRectangle(Lines, X0, Y0, X1, Y1);
        return;
    }
    double Cx = (Left + Right) / 2.0;
    double Cy = (Top + Bottom) / 2.0;
    int Steps = 4 * static_cast<int>(std::max(Rx, Ry)) + 8;
    for (int i = 0; i < Steps; ++i) {
        double Angle = 2.0 * FigurePi * i / Steps;
        double Cos = std::cos(Angle);
        double Sin = std::sin(Angle);
        int X = static_cast<int>(std::round(Cx + Rx * Cos));
        int Y = static_cast<int>(std::round(Cy + Ry * Sin));
        // The tangent is (-Rx*sin, Ry*cos): its direction
        // picks the character, as in LineChar.
        double Tx = -Rx * Sin;
        double Ty = Ry * Cos;
        QChar Ch;
        if (std::abs(Ty) < 0.001) {
            Ch = QLatin1Char('|');
        } else if (std::abs(Tx) < 0.001) {
            Ch = QLatin1Char('-');
        } else if (std::abs(Tx) > 2.0 * std::abs(Ty)) {
            Ch = QLatin1Char('-');
        } else if (std::abs(Ty) > 2.0 * std::abs(Tx)) {
            Ch = QLatin1Char('|');
        } else {
            Ch = (Tx * Ty > 0) ? QLatin1Char('\\') : QLatin1Char('/');
        }
        SetCell(Lines, Y, X, Ch);
    }
}

/**
 * @brief Draws a parallelogram (input/output) inside a bounding box.
 *
 * The top edge is shifted right by the slant, the bottom edge
 * stays, and the sides slant up-right. The slant is half the
 * height, capped at a quarter of the width, so the shape
 * stays inside its box.
 * @param Lines The grid.
 * @param X0 One corner's column.
 * @param Y0 One corner's row.
 * @param X1 The other corner's column.
 * @param Y1 The other corner's row.
 */
static void DrawParallelogram(QStringList& Lines, int X0, int Y0, int X1,
                              int Y1)
{
    int Left = std::min(X0, X1);
    int Right = std::max(X0, X1);
    int Top = std::min(Y0, Y1);
    int Bottom = std::max(Y0, Y1);
    int Height = Bottom - Top;
    int Width = Right - Left;
    int Slant = std::max(1, std::min(Height / 2, Width / 4));
    DrawLine(Lines, Left, Bottom, Left + Slant, Top);
    DrawLine(Lines, Right, Bottom, Right + Slant, Top);
    for (int X = Left + 1; X < Right; ++X) {
        SetCell(Lines, Top, X + Slant, QLatin1Char('-'));
        SetCell(Lines, Bottom, X, QLatin1Char('-'));
    }
    SetCell(Lines, Top, Left + Slant, QLatin1Char('+'));
    SetCell(Lines, Top, Right + Slant, QLatin1Char('+'));
    SetCell(Lines, Bottom, Left, QLatin1Char('+'));
    SetCell(Lines, Bottom, Right, QLatin1Char('+'));
}

/**
 * @brief Draws an arrow: a line with a head at its end.
 * @param Lines The grid.
 * @param X0 The start column.
 * @param Y0 The start row.
 * @param X1 The end column.
 * @param Y1 The end row.
 */
static void DrawArrow(QStringList& Lines, int X0, int Y0, int X1, int Y1)
{
    DrawLine(Lines, X0, Y0, X1, Y1);
    int Dx = X1 - X0;
    int Dy = Y1 - Y0;
    QChar Head;
    if (Dx == 0 && Dy == 0) {
        return;
    }
    // The head points along the dominant direction, so a
    // diagonal arrow still gets a visible head.
    if (std::abs(Dx) >= std::abs(Dy)) {
        Head = Dx > 0 ? QLatin1Char('>') : QLatin1Char('<');
    } else {
        Head = Dy > 0 ? QLatin1Char('v') : QLatin1Char('^');
    }
    SetCell(Lines, Y1, X1, Head);
}

// ---------------------------------------------------------------------------
// FigureCanvas
// ---------------------------------------------------------------------------

FigureCanvas::FigureCanvas(QWidget* Parent) : QPlainTextEdit(Parent)
{
    QFont Font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    Font.setPointSize(11);
    setFont(Font);
    setWordWrapMode(QTextOption::NoWrap);
    // The canvas owns undo (a snapshot per stroke or typing
    // run); the built-in undo would fight it, since drawing
    // replaces the text wholesale.
    setUndoRedoEnabled(false);
    setTabStopDistance(QFontMetricsF(Font).horizontalAdvance(
        QLatin1Char('M')) * 4);
    setPlainText(QString());
}

void FigureCanvas::setTool(Tool Value)
{
    _Tool = Value;
}

FigureCanvas::Tool FigureCanvas::tool() const
{
    return _Tool;
}

void FigureCanvas::undoLastStroke()
{
    if (_UndoStack.isEmpty()) {
        return;
    }
    QString Text = _UndoStack.takeLast();
    QSignalBlocker Blocker(this);
    setPlainText(Text);
}

bool FigureCanvas::canUndo() const
{
    return !_UndoStack.isEmpty();
}

void FigureCanvas::pushUndoSnapshot()
{
    QString Current = toPlainText();
    if (_UndoStack.isEmpty() || _UndoStack.last() != Current) {
        _UndoStack.append(Current);
        if (_UndoStack.size() > 100) {
            _UndoStack.removeFirst();
        }
    }
}

QPoint FigureCanvas::cellAt(const QPoint& Pos) const
{
    // The row and the column clamped to the line come from
    // Qt's own layout, so scrolling and margins are exact;
    // the cell is then extended past the line end (or past
    // the last line) by character widths, so the mouse can
    // draw beyond the text already there.
    QTextCursor Cursor = cursorForPosition(Pos);
    int Row = Cursor.blockNumber();
    int Col = Cursor.positionInBlock();
    QFontMetricsF Metrics(font());
    qreal CharWidth = Metrics.horizontalAdvance(QLatin1Char('M'));
    qreal LineHeight = Metrics.lineSpacing();
    if (CharWidth > 0) {
        QRect CursorRect = cursorRect(Cursor);
        if (Pos.x() > CursorRect.x()) {
            Col += static_cast<int>(
                std::round((Pos.x() - CursorRect.x()) / CharWidth));
        }
    }
    if (LineHeight > 0) {
        QRect CursorRect = cursorRect(Cursor);
        if (Pos.y() > CursorRect.bottom()) {
            Row += static_cast<int>(
                std::round((Pos.y() - CursorRect.bottom()) / LineHeight));
        }
    }
    return QPoint(std::max(0, Col), std::max(0, Row));
}

void FigureCanvas::drawShapeTo(const QPoint& To)
{
    QStringList Lines = _Snapshot;
    int X0 = _StartCell.x();
    int Y0 = _StartCell.y();
    int X1 = To.x();
    int Y1 = To.y();
    switch (_Tool) {
    case Rectangle:
        DrawRectangle(Lines, X0, Y0, X1, Y1);
        break;
    case Diamond:
        DrawDiamond(Lines, X0, Y0, X1, Y1);
        break;
    case Oval:
        DrawOval(Lines, X0, Y0, X1, Y1);
        break;
    case Parallelogram:
        DrawParallelogram(Lines, X0, Y0, X1, Y1);
        break;
    case Line:
        DrawLine(Lines, X0, Y0, X1, Y1);
        break;
    case Arrow:
        DrawArrow(Lines, X0, Y0, X1, Y1);
        break;
    case Text:
        break;
    }
    QSignalBlocker Blocker(this);
    setPlainText(Lines.join(QLatin1Char('\n')));
}

void FigureCanvas::mousePressEvent(QMouseEvent* Event)
{
    if (Event->button() == Qt::LeftButton && _Tool != Text) {
        // A stroke starts: snapshot the text, so the drag
        // previews against it and one undo step reverts the
        // whole stroke.
        pushUndoSnapshot();
        _Snapshot = toPlainText().split(QLatin1Char('\n'));
        _StartCell = cellAt(Event->pos());
        _Dragging = true;
        drawShapeTo(_StartCell);
        Event->accept();
        return;
    }
    if (Event->button() == Qt::LeftButton && _Tool == Text) {
        // Placing the cursor to type starts a typing run.
        pushUndoSnapshot();
    }
    QPlainTextEdit::mousePressEvent(Event);
}

void FigureCanvas::mouseMoveEvent(QMouseEvent* Event)
{
    if (_Dragging && (Event->buttons() & Qt::LeftButton)) {
        drawShapeTo(cellAt(Event->pos()));
        Event->accept();
        return;
    }
    QPlainTextEdit::mouseMoveEvent(Event);
}

void FigureCanvas::mouseReleaseEvent(QMouseEvent* Event)
{
    if (_Dragging && Event->button() == Qt::LeftButton) {
        drawShapeTo(cellAt(Event->pos()));
        _Dragging = false;
        Event->accept();
        return;
    }
    QPlainTextEdit::mouseReleaseEvent(Event);
}

void FigureCanvas::keyPressEvent(QKeyEvent* Event)
{
    if (Event->modifiers() == Qt::ControlModifier &&
        Event->key() == Qt::Key_Z) {
        undoLastStroke();
        Event->accept();
        return;
    }
    QPlainTextEdit::keyPressEvent(Event);
}

// ---------------------------------------------------------------------------
// FigureEditorDialog
// ---------------------------------------------------------------------------

FigureEditorDialog::FigureEditorDialog(QWidget* Parent) : QDialog(Parent)
{
    setWindowTitle(tr("Edit figure"));
    resize(760, 620);

    _Canvas = new FigureCanvas(this);

    // The tools, in the order the canvas offers them. Text is
    // the default: the canvas starts ready to type.
    QButtonGroup* Tools = new QButtonGroup(this);
    Tools->setExclusive(true);
    auto AddTool = [&](const QString& Label, FigureCanvas::Tool Value) {
        QPushButton* Button = new QPushButton(Label, this);
        Button->setCheckable(true);
        Button->setChecked(Value == FigureCanvas::Text);
        Tools->addButton(Button, static_cast<int>(Value));
        return Button;
    };
    QHBoxLayout* ToolRow = new QHBoxLayout();
    ToolRow->addWidget(AddTool(tr("Text"), FigureCanvas::Text));
    ToolRow->addWidget(AddTool(tr("Rectangle"), FigureCanvas::Rectangle));
    ToolRow->addWidget(AddTool(tr("Diamond"), FigureCanvas::Diamond));
    ToolRow->addWidget(AddTool(tr("Oval"), FigureCanvas::Oval));
    ToolRow->addWidget(
        AddTool(tr("Parallelogram"), FigureCanvas::Parallelogram));
    ToolRow->addWidget(AddTool(tr("Line"), FigureCanvas::Line));
    ToolRow->addWidget(AddTool(tr("Arrow"), FigureCanvas::Arrow));
    connect(Tools, &QButtonGroup::idClicked, this,
            [this](int Id) {
                _Canvas->setTool(static_cast<FigureCanvas::Tool>(Id));
            });
    QPushButton* UndoButton = new QPushButton(tr("Undo"), this);
    UndoButton->setEnabled(false);
    connect(UndoButton, &QPushButton::clicked, this,
            [this]() { _Canvas->undoLastStroke(); });
    connect(_Canvas, &QPlainTextEdit::textChanged, this, [this, UndoButton]() {
        UndoButton->setEnabled(_Canvas->canUndo());
    });
    ToolRow->addWidget(UndoButton);
    ToolRow->addStretch(1);

    _CaptionEdit = new QLineEdit(this);
    _CaptionEdit->setReadOnly(true);
    // The caption holds formatted text, so the preview is
    // display-only and the button opens the inline editor.
    // It stays disabled until a <name> node is given, since
    // only then is there a node to edit in place.
    _EditCaptionButton = new QPushButton(tr("Edit Caption"), this);
    _EditCaptionButton->setToolTip(tr("Edit the caption, including formatted text."));
    _EditCaptionButton->setEnabled(false);
    connect(_EditCaptionButton, &QPushButton::clicked,
            this, &FigureEditorDialog::OnEditCaption);
    QWidget* CaptionWidget = new QWidget(this);
    QHBoxLayout* CaptionLayout = new QHBoxLayout(CaptionWidget);
    CaptionLayout->setContentsMargins(0, 0, 0, 0);
    CaptionLayout->addWidget(_CaptionEdit, 1);
    CaptionLayout->addWidget(_EditCaptionButton);
    _AnchorEdit = new QLineEdit(this);
    QFormLayout* Fields = new QFormLayout();
    Fields->addRow(tr("Caption:"), CaptionWidget);
    Fields->addRow(tr("Anchor:"), _AnchorEdit);

    _TagTable = new QTableWidget(0, 3, this);
    _TagTable->setHorizontalHeaderLabels(
        {tr("Tag"), tr("Description"), tr("Anchor")});
    _TagTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    _TagTable->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    _TagTable->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents);
    _TagTable->verticalHeader()->setVisible(false);
    _TagTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    QPushButton* AddRowButton = new QPushButton(tr("Add tag"), this);
    QPushButton* RemoveRowButton = new QPushButton(tr("Remove tag"), this);
    connect(AddRowButton, &QPushButton::clicked, this, [this]() {
        _TagTable->insertRow(_TagTable->rowCount());
    });
    connect(RemoveRowButton, &QPushButton::clicked, this, [this]() {
        int Row = _TagTable->currentRow();
        if (Row >= 0) {
            _TagTable->removeRow(Row);
        }
    });
    QHBoxLayout* TagButtons = new QHBoxLayout();
    TagButtons->addWidget(AddRowButton);
    TagButtons->addWidget(RemoveRowButton);
    TagButtons->addStretch(1);

    _Buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(_Buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(_Buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // A splitter between the canvas and the caption/tags
    // below it, so the draw area can be grown at the expense
    // of the tag list (and back). Neither side collapses
    // away entirely, and the canvas starts with the larger
    // share.
    QSplitter* Splitter = new QSplitter(Qt::Vertical, this);
    Splitter->setChildrenCollapsible(false);
    Splitter->setHandleWidth(8);
    Splitter->addWidget(_Canvas);
    QWidget* Lower = new QWidget(Splitter);
    QVBoxLayout* LowerLayout = new QVBoxLayout(Lower);
    LowerLayout->setContentsMargins(0, 0, 0, 0);
    LowerLayout->addLayout(Fields);
    LowerLayout->addWidget(
        new QLabel(tr("Tags (written as a list below the figure):"), Lower));
    LowerLayout->addWidget(_TagTable, 1);
    LowerLayout->addLayout(TagButtons);
    Splitter->addWidget(Lower);
    Splitter->setStretchFactor(0, 3);
    Splitter->setStretchFactor(1, 2);

    QVBoxLayout* Layout = new QVBoxLayout(this);
    Layout->addLayout(ToolRow);
    Layout->addWidget(Splitter, 1);
    Layout->addWidget(_Buttons);
}

void FigureEditorDialog::setArtworkText(const QString& Text)
{
    _Canvas->setPlainText(Text);
}

QString FigureEditorDialog::artworkText() const
{
    return _Canvas->toPlainText();
}

void FigureEditorDialog::setCaption(const QString& Caption)
{
    // With a <name> node the preview shows the node's text,
    // so a string here must not overwrite it: the node is the
    // caption's home, the string is only its starting value
    // for callers that pass no node.
    if (_NameNode == nullptr) {
        _CaptionEdit->setText(Caption);
    }
}

QString FigureEditorDialog::caption() const
{
    if (_NameNode != nullptr) {
        return DocDocument::NodeText(_NameNode).trimmed();
    }
    return _CaptionEdit->text().trimmed();
}

void FigureEditorDialog::setNameNode(xmlNodePtr NameNode,
                                     const DocDocument* Document)
{
    _NameNode = NameNode;
    _Document = Document;
    if (_NameNode != nullptr) {
        _CaptionEdit->setText(DocDocument::NodeText(_NameNode).trimmed());
    }
    _EditCaptionButton->setEnabled(_NameNode != nullptr);
}

xmlNodePtr FigureEditorDialog::nameNode() const
{
    return _NameNode;
}

void FigureEditorDialog::OnEditCaption()
{
    if (_NameNode == nullptr) {
        return;
    }
    if (!TextEditor::EditInlineElement(this, tr("Edit Caption"), _NameNode,
                                       QStringLiteral("name"), _Document)) {
        return;
    }
    _CaptionEdit->setText(DocDocument::NodeText(_NameNode).trimmed());
}

void FigureEditorDialog::setAnchor(const QString& Anchor)
{
    _AnchorEdit->setText(Anchor);
}

QString FigureEditorDialog::anchor() const
{
    return _AnchorEdit->text().trimmed();
}

void FigureEditorDialog::fillTagTable(const QVector<FigureTagRow>& Rows)
{
    _TagTable->setRowCount(0);
    for (const FigureTagRow& Row : Rows) {
        int At = _TagTable->rowCount();
        _TagTable->insertRow(At);
        _TagTable->setItem(At, 0, new QTableWidgetItem(Row.Tag));
        _TagTable->setItem(At, 1, new QTableWidgetItem(Row.Description));
        _TagTable->setItem(At, 2, new QTableWidgetItem(Row.Anchor));
    }
}

void FigureEditorDialog::setTagRows(const QVector<FigureTagRow>& Rows)
{
    fillTagTable(Rows);
}

QVector<FigureTagRow> FigureEditorDialog::tagRows() const
{
    QVector<FigureTagRow> Rows;
    for (int Row = 0; Row < _TagTable->rowCount(); ++Row) {
        auto Cell = [this, Row](int Column) {
            QTableWidgetItem* Item = _TagTable->item(Row, Column);
            return Item == nullptr ? QString() : Item->text().trimmed();
        };
        FigureTagRow TagRow;
        TagRow.Tag = Cell(0);
        TagRow.Description = Cell(1);
        TagRow.Anchor = Cell(2);
        // A row with nothing in it is not a tag.
        if (TagRow.Tag.isEmpty() && TagRow.Description.isEmpty() &&
            TagRow.Anchor.isEmpty()) {
            continue;
        }
        Rows.append(TagRow);
    }
    return Rows;
}
