/**
 * @file LegendDialog.h
 * @brief The xml2rfc legend: the language, described by topic.
 *
 * A dialog with one tab per topic. Each tab lists its elements
 * with a plain-word description and where each element goes.
 * The entries are alphabetical by element name within a tab.
 * Deprecated elements are marked, with what replaces them.
 */

#ifndef DOCEDITOR_LEGENDDIALOG_H
#define DOCEDITOR_LEGENDDIALOG_H

#include <QDialog>

class QTabWidget;

/**
 * @brief A modal dialog describing the xml2rfc vocabulary.
 */
class LegendDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Builds the dialog with its topic tabs.
     * @param Parent The parent widget.
     */
    explicit LegendDialog(QWidget* Parent = nullptr);
};

#endif // DOCEDITOR_LEGENDDIALOG_H
