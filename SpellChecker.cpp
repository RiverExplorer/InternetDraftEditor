// Copyright (c) 2026 RiverExplorer LLC
// RiverExplorer is a registered trademark of Douglas Mark Royer.
//
// This software is released under the MIT License.
// See LICENSE.md for the full license text.

/**
 * @file SpellChecker.cpp
 * @brief English spell checking for the editor, backed by Hunspell.
 */

#include "SpellChecker.h"

#include <QFile>
#include <QStringConverter>
#include <QTextStream>

#include <vector>

#include "hunspell/hunspell.hxx"

SpellChecker::SpellChecker(const QString& AffPath, const QString& DicPath,
                           const QString& PersonalPath)
    : _Hunspell(nullptr)
    , _PersonalPath(PersonalPath)
{
    // Both dictionary files must exist: with a missing file
    // Hunspell loads nothing and would flag every word, so the
    // checker stays off instead (IsAvailable false, CheckWord
    // accepts everything).
    if (!QFile::exists(AffPath) || !QFile::exists(DicPath)) {
        return;
    }
    QByteArray Aff = AffPath.toUtf8();
    QByteArray Dic = DicPath.toUtf8();
    _Hunspell = new Hunspell(Aff.constData(), Dic.constData());
    _PersonalWords = ReadWordFile(_PersonalPath);
    for (const QString& Word : _PersonalWords) {
        _Hunspell->add(Word.toStdString());
    }
}

SpellChecker::~SpellChecker()
{
    delete _Hunspell;
}

bool SpellChecker::IsAvailable() const
{
    return _Hunspell != nullptr;
}

bool SpellChecker::CheckWord(const QString& Word) const
{
    if (Word.isEmpty()) {
        return true;
    }
    if (_Hunspell == nullptr) {
        return true;
    }
    QString Lower = Word.toLower();
    if (_IgnoredWords.contains(Lower)) {
        return true;
    }
    if (_ProjectWords.contains(Lower)) {
        return true;
    }
    return _Hunspell->spell(Word.toStdString());
}

QStringList SpellChecker::Suggestions(const QString& Word) const
{
    QStringList Result;
    if (_Hunspell == nullptr || Word.isEmpty()) {
        return Result;
    }
    std::vector<std::string> Found = _Hunspell->suggest(Word.toStdString());
    for (const std::string& Item : Found) {
        Result.append(QString::fromStdString(Item));
        if (Result.size() >= 8) {
            break;
        }
    }
    return Result;
}

void SpellChecker::AddToPersonal(const QString& Word)
{
    QString Trimmed = Word.trimmed();
    if (Trimmed.isEmpty() || _PersonalPath.isEmpty()) {
        return;
    }
    QString Lower = Trimmed.toLower();
    if (_PersonalWords.contains(Lower)) {
        return;
    }
    _PersonalWords.insert(Lower);
    if (_Hunspell != nullptr && !_Hunspell->spell(Trimmed.toStdString())) {
        _Hunspell->add(Trimmed.toStdString());
    }
    AppendWordFile(_PersonalPath, Trimmed);
}

void SpellChecker::AddToProject(const QString& Word)
{
    QString Trimmed = Word.trimmed();
    if (Trimmed.isEmpty() || _ProjectPath.isEmpty()) {
        return;
    }
    QString Lower = Trimmed.toLower();
    if (_ProjectWords.contains(Lower)) {
        return;
    }
    _ProjectWords.insert(Lower);
    AppendWordFile(_ProjectPath, Trimmed);
}

void SpellChecker::IgnoreWord(const QString& Word)
{
    QString Trimmed = Word.trimmed();
    if (!Trimmed.isEmpty()) {
        _IgnoredWords.insert(Trimmed.toLower());
    }
}

void SpellChecker::SetProjectDictionary(const QString& Path)
{
    _ProjectPath = Path;
    _ProjectWords = ReadWordFile(Path);
}

QString SpellChecker::ProjectDictionaryPath() const
{
    return _ProjectPath;
}

QSet<QString> SpellChecker::ReadWordFile(const QString& Path)
{
    QSet<QString> Words;
    if (Path.isEmpty()) {
        return Words;
    }
    QFile File(Path);
    if (!File.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return Words;
    }
    QTextStream In(&File);
    In.setEncoding(QStringConverter::Utf8);
    while (!In.atEnd()) {
        QString Line = In.readLine().trimmed();
        if (Line.isEmpty() || Line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        Words.insert(Line.toLower());
    }
    return Words;
}

void SpellChecker::AppendWordFile(const QString& Path, const QString& Word)
{
    QFile File(Path);
    if (!File.open(QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream Out(&File);
    Out.setEncoding(QStringConverter::Utf8);
    Out << Word << '\n';
}
