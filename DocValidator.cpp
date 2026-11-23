/**
 * @file DocValidator.cpp
 * @brief Implementation of the in-app RelaxNG validator (goal #2).
 *
 * Uses libxml2 directly: xmlReadFile parses the document,
 * xmlRelaxNGParse loads the schema, xmlRelaxNGValidateDoc checks the
 * document against it. All libxml2 symbols used here were verified
 * against the libxml2 2.15.4 headers in RelatedProjects/include.
 * Note xmlRelaxNGValidateDoc is the document check in this version;
 * there is no xmlRelaxNGValidate in the 2.15 headers.
 */

#include "DocValidator.h"

#include <cstdarg>
#include <cstdio>

#include <QByteArray>

#include <libxml/parser.h>
#include <libxml/relaxng.h>
#include <libxml/tree.h>
#include <libxml/xmlerror.h>

namespace {

/**
 * @brief Collects libxml2 messages into a string list.
 */
struct ErrorCollector
{
    QStringList* Errors;
};

/**
 * @brief libxml2 error callback: formats one message into the list.
 * @param Context The ErrorCollector.
 * @param Message The printf-style message.
 */
void CollectError(void* Context, const char* Message, ...)
{
    if (Context == nullptr || Message == nullptr) {
        return;
    }
    char Buffer[2048];
    va_list Args;
    va_start(Args, Message);
    vsnprintf(Buffer, sizeof(Buffer), Message, Args);
    va_end(Args);
    QString Text = QString::fromUtf8(Buffer).trimmed();
    if (!Text.isEmpty()) {
        static_cast<ErrorCollector*>(Context)->Errors->append(Text);
    }
}

/**
 * @brief libxml2 warning callback: warnings join the list, tagged.
 * @param Context The ErrorCollector.
 * @param Message The printf-style message.
 */
void CollectWarning(void* Context, const char* Message, ...)
{
    if (Context == nullptr || Message == nullptr) {
        return;
    }
    char Buffer[2048];
    va_list Args;
    va_start(Args, Message);
    vsnprintf(Buffer, sizeof(Buffer), Message, Args);
    va_end(Args);
    QString Text = QString::fromUtf8(Buffer).trimmed();
    if (!Text.isEmpty()) {
        static_cast<ErrorCollector*>(Context)->Errors->append(
            QStringLiteral("Warning: ") + Text);
    }
}

/**
 * @brief Describes the last libxml2 error, with its line number.
 * @return The description, or an empty string when there is none.
 */
QString LastErrorText()
{
    const xmlError *Error = xmlGetLastError();
    if (Error == nullptr || Error->message == nullptr) {
        return QString();
    }
    QString Text = QString::fromUtf8(Error->message).trimmed();
    if (Text.isEmpty()) {
        return QString();
    }
    if (Error->line > 0) {
        return QStringLiteral("Line %1: %2").arg(Error->line).arg(Text);
    }
    return Text;
}

} // namespace

DocValidationResult ValidateRfcXml(const QString& XmlPath, const QString& SchemaPath)
{
    DocValidationResult Result;
    ErrorCollector Collector{&Result.Errors};

    xmlInitParser();
    xmlResetLastError();

    // Parse the document. Errors and warnings are suppressed here:
    // they are reported from the last-error record below instead.
    // NONET keeps the parser from fetching anything over the network.
    QByteArray XmlUtf8 = XmlPath.toUtf8();
    xmlDocPtr Document = xmlReadFile(
        XmlUtf8.constData(), nullptr,
        XML_PARSE_NOERROR | XML_PARSE_NOWARNING | XML_PARSE_NONET);
    if (Document == nullptr) {
        QString Text = LastErrorText();
        Result.Errors.append(Text.isEmpty()
            ? QStringLiteral("The file could not be parsed as XML.")
            : Text);
        return Result;
    }

    // Load the schema. Its included files resolve from the schema
    // file's own folder.
    QByteArray SchemaUtf8 = SchemaPath.toUtf8();
    xmlRelaxNGParserCtxtPtr ParserContext =
        xmlRelaxNGNewParserCtxt(SchemaUtf8.constData());
    if (ParserContext == nullptr) {
        Result.Errors.append(
            QStringLiteral("The schema could not be loaded: %1").arg(SchemaPath));
        xmlFreeDoc(Document);
        return Result;
    }
    xmlRelaxNGSetParserErrors(ParserContext, CollectError, CollectWarning,
                              &Collector);
    xmlRelaxNGPtr Schema = xmlRelaxNGParse(ParserContext);
    xmlRelaxNGFreeParserCtxt(ParserContext);
    if (Schema == nullptr) {
        if (Result.Errors.isEmpty()) {
            QString Text = LastErrorText();
            Result.Errors.append(Text.isEmpty()
                ? QStringLiteral("The schema could not be loaded: %1").arg(SchemaPath)
                : Text);
        }
        xmlFreeDoc(Document);
        return Result;
    }

    // Validate. 0 means valid, 1 means invalid, -1 means the check
    // itself failed.
    xmlRelaxNGValidCtxtPtr ValidContext = xmlRelaxNGNewValidCtxt(Schema);
    if (ValidContext == nullptr) {
        Result.Errors.append(QStringLiteral("The validator could not be started."));
        xmlRelaxNGFree(Schema);
        xmlFreeDoc(Document);
        return Result;
    }
    xmlRelaxNGSetValidErrors(ValidContext, CollectError, CollectWarning,
                             &Collector);
    int Verdict = xmlRelaxNGValidateDoc(ValidContext, Document);
    xmlRelaxNGFreeValidCtxt(ValidContext);
    xmlRelaxNGFree(Schema);
    xmlFreeDoc(Document);

    Result.Checked = true;
    if (Verdict == 0) {
        Result.Valid = true;
    } else if (Verdict == 1) {
        if (Result.Errors.isEmpty()) {
            Result.Errors.append(
                QStringLiteral("The file does not match the v3 schema."));
        }
    } else {
        Result.Checked = false;
        if (Result.Errors.isEmpty()) {
            Result.Errors.append(
                QStringLiteral("The validation check failed to run."));
        }
    }
    return Result;
}
