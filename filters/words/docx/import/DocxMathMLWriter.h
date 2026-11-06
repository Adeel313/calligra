/* SPDX-License-Identifier: LGPL-2.1-only */

#ifndef DOCXMATHMLWRITER_H
#define DOCXMATHMLWRITER_H

#include <QByteArray>

class QXmlStreamReader;

namespace DocxMathMLWriter
{
/**
 * Converts the OMML subtree at the current position of @p reader to an
 * embedded ODF formula document (a MathML content.xml).
 *
 * The reader must point at a m:oMath or m:oMathPara start element. The whole
 * subtree is consumed; on return the reader is positioned at its end element.
 *
 * @return the content.xml bytes, or an empty array if no formula could be
 *         built (the subtree is consumed either way).
 */
QByteArray readMathML(QXmlStreamReader &reader);
}

#endif // DOCXMATHMLWRITER_H
