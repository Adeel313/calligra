/* This file is part of the KDE project
   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef FORMULALINEARINPUT_H
#define FORMULALINEARINPUT_H

#include <QString>

/**
 * Turns the linear notation people type ("a/b", "x^2", "\alpha", "sqrt(x)")
 * into MathML, so typing builds the same structures that the ribbon buttons
 * insert. This is the behaviour of the equation editors in Office and LaTeX:
 * the notation is only built up when it actually says something, plain text is
 * left alone.
 */
namespace FormulaLinearInput
{
/// @return true when @p text contains notation that can be built up
bool hasLinearSyntax(const QString &text);

/// @return the MathML for @p text, or an empty string when nothing parses
QString toMathML(const QString &text);
}

#endif // FORMULALINEARINPUT_H
