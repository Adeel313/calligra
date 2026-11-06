/* SPDX-License-Identifier: LGPL-2.0-or-later */
#ifndef DOCXMATHWRITER_H
#define DOCXMATHWRITER_H

#include <QByteArray>

/**
 * Convert a MathML document (the content of an embedded ODF formula object)
 * to an OMML fragment that can be placed inside a WordprocessingML run.
 *
 * Returns an empty array when the input is not MathML.
 */
QByteArray mathmlToOmml(const QByteArray &mathml);

#endif
