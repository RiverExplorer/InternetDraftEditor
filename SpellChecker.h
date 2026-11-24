// Copyright (c) 2026 RiverExplorer LLC
// RiverExplorer is a registered trademark of Douglas Mark Royer.
//
// This software is released under the MIT License.
// See LICENSE.md for the full license text.

/**
 * @file SpellChecker.h
 * @brief English spell checking for the editor, backed by Hunspell.
 *
 * Hunspell holds the en_US dictionary. Two word lists sit on top
 * of it, both one word per line in plain UTF-8 text:
 *
 * - The personal dictionary holds words the writer always wants
 *   accepted, in every draft. It lives in the user's profile
 *   (the dictionaries folder) and is loaded once at startup.
 *   Words added to it join Hunspell, so they are also offered
 *   as suggestions.
 * - The project dictionary holds words for one draft only, such
 *   as protocol terms and names. It lives beside the draft file
 *   (same name, .dic extension) and is loaded when that draft
 *   is opened, so it travels with the draft. Switching drafts
 *   swaps the project words; the personal words stay.
 *
 * A word ignored from the context menu is kept for this run
 * only and is never written to a file.
 */

#ifndef DOCEDITOR_SPELLCHECKER_H
#define DOCEDITOR_SPELLCHECKER_H

#include <QString>
#include <QStringList>
#include <QSet>

class Hunspell;

/**
 * @brief English spell checking over Hunspell plus two word lists.
 */
class SpellChecker
{
public:
    /**
     * @brief Creates the checker and loads the dictionaries.
     *
     * The en_US dictionary comes from AffPath and DicPath. The
     * personal dictionary is read from PersonalPath when it
     * exists; a missing personal file is not an error, it is
     * created when the first word is added. No project
     * dictionary is loaded until SetProjectDictionary is called.
     * @param AffPath The en_US .aff file.
     * @param DicPath The en_US .dic file.
     * @param PersonalPath The personal dictionary file.
     */
    SpellChecker(const QString& AffPath, const QString& DicPath,
                 const QString& PersonalPath);

    /**
     * @brief Releases the Hunspell instance.
     */
    ~SpellChecker();

    SpellChecker(const SpellChecker&) = delete;
    SpellChecker& operator=(const SpellChecker&) = delete;

    /**
     * @brief True when the en_US dictionary loaded.
     * @return True when spell checking is available.
     */
    bool IsAvailable() const;

    /**
     * @brief True when a word is spelled correctly.
     *
     * A word counts as correct when Hunspell accepts it, when
     * it is in the personal dictionary, when it is in the
     * project dictionary, or when it was ignored for this run.
     * The check is case-insensitive for the word lists. An
     * empty word counts as correct.
     * @param Word The word to check.
     * @return True when the word is correct.
     */
    bool CheckWord(const QString& Word) const;

    /**
     * @brief Suggestions for a misspelled word, best first.
     * @param Word The misspelled word.
     * @return Up to eight suggestions, possibly empty.
     */
    QStringList Suggestions(const QString& Word) const;

    /**
     * @brief Adds a word to the personal dictionary.
     *
     * The word joins Hunspell at once and is appended to the
     * personal file, so it stays accepted in every draft and
     * in later runs. Adding a word already present does nothing.
     * @param Word The word to add.
     */
    void AddToPersonal(const QString& Word);

    /**
     * @brief Adds a word to the project dictionary.
     *
     * The word is accepted for the current draft at once and
     * is appended to the project file, so it stays accepted
     * when the draft is reopened. Adding a word already present
     * does nothing.
     * @param Word The word to add.
     */
    void AddToProject(const QString& Word);

    /**
     * @brief Ignores a word for this run only.
     *
     * The word is accepted until the program closes. It is
     * never written to a file.
     * @param Word The word to ignore.
     */
    void IgnoreWord(const QString& Word);

    /**
     * @brief Loads the project dictionary for a draft.
     *
     * The project words are replaced by the words in the file
     * at Path, when it exists. A missing file leaves the
     * project list empty, not an error. Call this when the
     * open draft changes, and with an empty path when no draft
     * is open.
     * @param Path The project dictionary file, or empty.
     */
    void SetProjectDictionary(const QString& Path);

    /**
     * @brief The project dictionary file in use, or empty.
     * @return The project dictionary path.
     */
    QString ProjectDictionaryPath() const;

private:
    /**
     * @brief Reads one word per line from a dictionary file.
     * @param Path The file to read.
     * @return The words, lowercased, without blanks or comments.
     */
    static QSet<QString> ReadWordFile(const QString& Path);

    /**
     * @brief Appends one word to a dictionary file.
     * @param Path The file to append to.
     * @param Word The word to append.
     */
    static void AppendWordFile(const QString& Path, const QString& Word);

    Hunspell* _Hunspell;
    QString _PersonalPath;
    QString _ProjectPath;
    QSet<QString> _PersonalWords;
    QSet<QString> _ProjectWords;
    QSet<QString> _IgnoredWords;
};

#endif // DOCEDITOR_SPELLCHECKER_H
