/**
 * @file main.cpp
 * @brief Application entry point for the ProtocolDocEditor.
 *
 * The start wizard runs first. The user picks an existing draft
 * XML file to open, or creates a new one by answering the minimum
 * questions a draft needs. When the user cancels the wizard, the
 * application exits cleanly.
 */

#include <QApplication>
#include <QDialog>
#include <QFont>

#include "MainWindow.h"
#include "StartWizard.h"

/**
 * @brief Runs the ProtocolDocEditor.
 * @param ArgCount The number of command-line arguments.
 * @param ArgValues The command-line arguments.
 * @return The application exit code.
 */
int main(int ArgCount, char* ArgValues[])
{
    QApplication App(ArgCount, ArgValues);
    App.setApplicationName(QStringLiteral("ProtocolDocEditor"));
    App.setApplicationVersion(QStringLiteral("1.1.0"));
    App.setOrganizationName(QStringLiteral("RiverExplorer"));

    // Font: Arial 10, like the CoOp editor.
    App.setFont(QFont(QStringLiteral("Arial"), 10));

    // Dark theme, the same palette as the CoOp editor (from
    // RiverExplorer.games, via colorkit.co):
    //   #28375E darkest: the background, the same shade everywhere.
    //   #45619E: buttons, headers, selected tab, menu selection,
    //     gridlines, input fields.
    //   #5D86CA: selected item, hover states.
    // Text on the dark background is white. Input fields are
    // dark with white text.
    App.setStyleSheet(QStringLiteral(
        "QWidget { color: #FFFFFF; }"
        "QMainWindow, QDialog { background-color: #28375E; }"
        "QPushButton { background-color: #45619E; color: #FFFFFF; "
        "border: none; padding: 6px 14px; border-radius: 3px; }"
        "QPushButton:hover { background-color: #5D86CA; }"
        "QPushButton:pressed { background-color: #28375E; }"
        // A checked button is the one naming the structure the
        // cursor is in: it shows the selected shade with a light
        // border, so it reads as on at a glance. A disabled
        // button recedes into the background with dimmed text:
        // the earlier light-grey disabled rule made the buttons
        // that could not be pressed the brightest thing in the
        // row, which read backwards (reported 2026-11-17).
        "QPushButton:checked { background-color: #5D86CA; "
        "border: 1px solid #B0C4E8; }"
        "QPushButton:disabled { background-color: #28375E; color: #6E7FA3; }"
        // The Blocks button is a QToolButton: without these rules
        // it kept the native style under the theme's white text,
        // so it gets the same treatment as the push buttons.
        "QToolButton { background-color: #45619E; color: #FFFFFF; "
        "border: none; padding: 6px 14px; border-radius: 3px; }"
        "QToolButton:hover { background-color: #5D86CA; }"
        "QToolButton:pressed { background-color: #28375E; }"
        "QToolButton:checked { background-color: #5D86CA; "
        "border: 1px solid #B0C4E8; }"
        "QToolButton:disabled { background-color: #28375E; color: #6E7FA3; }"
        "QTabWidget::pane { border: 1px solid #45619E; background-color: #28375E; }"
        "QTabBar::tab { background-color: #28375E; color: #FFFFFF; "
        "padding: 6px 12px; border: 1px solid #45619E; }"
        "QTabBar::tab:selected { background-color: #45619E; color: #FFFFFF; }"
        "QTabBar::tab:hover { background-color: #5D86CA; color: #FFFFFF; }"
        "QMenuBar::item:selected { background-color: #45619E; color: #FFFFFF; }"
        "QMenuBar { background-color: #28375E; color: #FFFFFF; }"
        "QMenuBar::item:disabled { color: #6E7FA3; }"
        "QSplitter { background-color: #28375E; }"
        "QMenu { background-color: #28375E; color: #FFFFFF; }"
        "QMenu::item:selected { background-color: #45619E; color: #FFFFFF; }"
        "QMenu::item:disabled { color: #6E7FA3; }"
        "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, "
        "QPlainTextEdit, QTextEdit, QDateEdit, QDateTimeEdit, QTimeEdit { "
        "background-color: #45619E; color: #FFFFFF; }"
        "QLineEdit:focus { border: 1px solid #5D86CA; }"
        "QComboBox:focus { border: 1px solid #5D86CA; }"
        // The template picker in the start wizard stays disabled
        // until "Create from an IETF template" is picked: without
        // this rule it kept the live colors and read as broken
        // (reported 2026-11-23).
        "QComboBox:disabled { background-color: #28375E; color: #6E7FA3; }"
        // The abstract and section-body edits are QPlainTextEdit, and
        // the date edit is a QDateEdit: without these rules they kept
        // the default white background under the theme's white text
        // (white on white, reported 2026-11-16).
        "QPlainTextEdit:focus, QTextEdit:focus, QDateEdit:focus { "
        "border: 1px solid #5D86CA; }"
        "QTreeWidget, QTableWidget, QListWidget { background-color: #28375E; color: #FFFFFF; "
        "alternate-background-color: #28375E; gridline-color: #45619E; }"
        "QTreeWidget::item:selected, QTableWidget::item:selected, QListWidget::item:selected { "
        "background-color: #5D86CA; color: #FFFFFF; }"
        "QHeaderView::section { background-color: #45619E; color: #FFFFFF; "
        "border: none; padding: 4px; }"
        // Scrollbars in the theme colors, so a scrollbar is
        // unmistakable against the dark background when it appears
        // (Bug 247, from the CoOp editor). No arrow buttons: the
        // handle and track carry the whole control, as in most
        // dark themes.
        "QScrollBar:vertical { background-color: #28375E; width: 16px; "
        "border: none; margin: 0px; }"
        "QScrollBar::handle:vertical { background-color: #45619E; "
        "min-height: 20px; border-radius: 3px; }"
        "QScrollBar::handle:vertical:hover { background-color: #5D86CA; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
        "QScrollBar:horizontal { background-color: #28375E; height: 16px; "
        "border: none; margin: 0px; }"
        "QScrollBar::handle:horizontal { background-color: #45619E; "
        "min-width: 20px; border-radius: 3px; }"
        "QScrollBar::handle:horizontal:hover { background-color: #5D86CA; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }"));

    StartWizard Wizard;
    if (Wizard.exec() != QDialog::Accepted)
    {
        // The user cancelled the wizard; exit without opening a window.
        return 0;
    }

    MainWindow Window;
    if (Wizard.IsOpeningDraft())
    {
        // The user picked an existing draft file: open the main
        // window on it.
        Window.OpenDraftFile(Wizard.GetSelectedDraftFile());
    }
    else
    {
        // The user created a new draft: open the main window on the
        // collected document.
        Window.SetDocument(Wizard.GetDocument(), Wizard.GetDocumentFilePath());
    }
    Window.show();
    return App.exec();
}
