// Copyright (c) 2026 RiverExplorer LLC
// RiverExplorer is a registered trademark of Douglas Mark Royer.
//
// This software is released under the MIT License.
// See LICENSE.md for the full license text.

/**
 * @file SpellHighlighter.h
 * @brief Underlines misspelled words in a text document.
 *
 * A QSyntaxHighlighter over one text document. Each block it is
 * asked to highlight is split into words; a word the spell
 * checker rejects gets a red wavy underline. Blocks the caller
 * marks to skip (sourcecode and artwork, which hold literal
 * text, not prose) are left alone. Words inside a web address
 * or an email address are left alone too. The checker is
 * shared: one SpellChecker serves every text pane.
 */

#ifndef DOCEDITOR_SPELLHIGHLIGHTER_H
#define DOCEDITOR_SPELLHIGHLIGHTER_H

#include <QSyntaxHighlighter>

#include <functional>

class QTextBlock;
class QTextDocument;
class SpellChecker;

/**
 * @brief Red wavy underlines for misspelled words in one document.
 */
class SpellHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    /**
     * @brief Creates the highlighter for a document.
     * @param Document The document to highlight.
     * @param Checker The spell checker to ask, or nullptr to
     *        highlight nothing.
     * @param Skip A test for blocks to leave alone (sourcecode
     *        and artwork), or empty to check every block.
     */
    SpellHighlighter(QTextDocument* Document, SpellChecker* Checker,
                     std::function<bool(const QTextBlock&)> Skip = {});

    /**
     * @brief Replaces the spell checker.
     * @param Checker The spell checker to ask, or nullptr.
     */
    void SetChecker(SpellChecker* Checker);

    /**
     * @brief The word span holding a position, by the spell
     *        checker's word rule.
     *
     * A word is letters and digits with apostrophes and hyphens
     * inside it, so don't, CO-OP, and xml2rfc each count as one
     * word. The same rule finds the words the highlighter
     * underlines, so the menu and the underlines agree on what
     * a word is.
     * @param Text The block text.
     * @param Pos The position within the text.
     * @param Start Filled with the word's start, or -1.
     * @param End Filled with the position after the word.
     * @return True when a word holds the position.
     */
    static bool WordAt(const QString& Text, int Pos, int& Start, int& End);

protected:
    /**
     * @brief Underlines the misspelled words of one block.
     * @param Text The block's text.
     */
    void highlightBlock(const QString& Text) override;

private:
    SpellChecker* _Checker;
    std::function<bool(const QTextBlock&)> _Skip;
};

#endif // DOCEDITOR_SPELLHIGHLIGHTER_H
