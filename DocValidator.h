// Copyright (c) 2026 RiverExplorer LLC
// RiverExplorer is a registered trademark of Douglas Mark Royer.
//
// This software is released under the MIT License.
// See LICENSE.md for the full license text.

/**
 * @file DocValidator.h
 * @brief Validates a draft XML file against the RFCXML v3 schema.
 *
 * Goal #2: the tool reads back in what was saved and checks it is
 * usable, so a file edited by hand gets checked too. Validation runs
 * inside the app with libxml2's RelaxNG checker, against the v3.rng
 * schema. The schema file must sit where the caller can find it (the
 * main window looks for a schemas folder next to the program); the
 * schema's own included files resolve from the schema file's folder.
 */

#ifndef DOCEDITOR_DOCVALIDATOR_H
#define DOCEDITOR_DOCVALIDATOR_H

#include <QString>
#include <QStringList>

/**
 * @brief The result of validating one file.
 */
struct DocValidationResult
{
    bool Valid = false;   ///< True when the file parsed and matched the schema.
    bool Checked = false; ///< True when validation actually ran.
    QStringList Errors;   ///< One line per problem found.
};

/**
 * @brief Validates an XML file against a RelaxNG schema.
 * @param XmlPath The XML file to validate.
 * @param SchemaPath The RelaxNG schema file (.rng).
 * @return The result: whether the file matched, and the problems.
 * @note The Errors list may hold warnings even when Valid is true;
 *       the verdict is Valid, not the list. When Checked is false,
 *       validation did not run (the file would not parse, or the
 *       schema would not load) and Errors says why.
 */
DocValidationResult ValidateRfcXml(const QString& XmlPath, const QString& SchemaPath);

#endif // DOCEDITOR_DOCVALIDATOR_H
