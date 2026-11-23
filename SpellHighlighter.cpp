/**
 * @file SpellHighlighter.cpp
 * @brief Underlines misspelled words in a text document.
 */

#include "SpellHighlighter.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCharFormat>

#include <utility>

#include "SpellChecker.h"

SpellHighlighter::SpellHighlighter(QTextDocument* Document, SpellChecker* Checker,
                                   std::function<bool(const QTextBlock&)> Skip)
    : QSyntaxHighlighter(Document)
    , _Checker(Checker)
    , _Skip(std::move(Skip))
{
}

void SpellHighlighter::SetChecker(SpellChecker* Checker)
{
    _Checker = Checker;
    rehighlight();
}

// A word is letters and digits with apostrophes and hyphens
// inside it, so don't, CO-OP, and xml2rfc each count as one
// word. A run holding no letter at all is a number, not a
// word, and is left alone.
static const QRegularExpression& WordPattern()
{
    static const QRegularExpression Pattern(
        QStringLiteral("[A-Za-z0-9]+(?:['-][A-Za-z0-9]+)*"));
    return Pattern;
}

bool SpellHighlighter::WordAt(const QString& Text, int Pos, int& Start, int& End)
{
    Start = -1;
    End = -1;
    if (Pos < 0 || Pos > Text.length()) {
        return false;
    }
    QRegularExpressionMatchIterator It = WordPattern().globalMatch(Text);
    while (It.hasNext()) {
        QRegularExpressionMatch Match = It.next();
        if (Pos >= Match.capturedStart() && Pos <= Match.capturedEnd()) {
            QString Word = Match.captured(0);
            static const QRegularExpression HasLetter(
                QStringLiteral("[A-Za-z]"));
            if (!HasLetter.match(Word).hasMatch()) {
                return false;
            }
            Start = Match.capturedStart();
            End = Match.capturedEnd();
            return true;
        }
        if (Match.capturedStart() > Pos) {
            break;
        }
    }
    return false;
}

void SpellHighlighter::highlightBlock(const QString& Text)
{
    if (_Checker == nullptr || !_Checker->IsAvailable()) {
        return;
    }
    if (_Skip && _Skip(currentBlock())) {
        return;
    }
    // Web addresses and email addresses are not words: find
    // their spans first, and leave any word inside one alone.
    static const QRegularExpression UrlPattern(
        QStringLiteral("https?://\\S+|www\\.\\S+|\\S+@\\S+\\.\\S+"));
    QVector<QPair<int, int>> UrlSpans;
    QRegularExpressionMatchIterator UrlIt = UrlPattern.globalMatch(Text);
    while (UrlIt.hasNext()) {
        QRegularExpressionMatch Match = UrlIt.next();
        UrlSpans.append(qMakePair(Match.capturedStart(), Match.capturedEnd()));
    }
    auto InUrl = [&UrlSpans](int Start, int End) {
        for (const QPair<int, int>& Span : UrlSpans) {
            if (Start < Span.second && End > Span.first) {
                return true;
            }
        }
        return false;
    };
    QTextCharFormat Misspelled;
    Misspelled.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    Misspelled.setUnderlineColor(Qt::red);
    QRegularExpressionMatchIterator It = WordPattern().globalMatch(Text);
    while (It.hasNext()) {
        QRegularExpressionMatch Match = It.next();
        QString Word = Match.captured(0);
        static const QRegularExpression HasLetter(
            QStringLiteral("[A-Za-z]"));
        if (!HasLetter.match(Word).hasMatch()) {
            continue;
        }
        if (InUrl(Match.capturedStart(), Match.capturedEnd())) {
            continue;
        }
        if (!_Checker->CheckWord(Word)) {
            setFormat(Match.capturedStart(), Match.capturedLength(), Misspelled);
        }
    }
}
