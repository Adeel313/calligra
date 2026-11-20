/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2026 Adeel <adeel@example.com>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef WORD97WRITER_H
#define WORD97WRITER_H

#include <QByteArray>
#include <QList>
#include <QString>

class QIODevice;

namespace Word97 {

struct Image
{
    QByteArray data;
    QString mimeType;
    // All geometry in twips (1/1440 inch).
    qint64 width = 0;
    qint64 height = 0;
    qint64 posX = 0;
    qint64 posY = 0;
};

struct Paragraph
{
    QString text;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    int fontSizeHalfPoints = 0; // 0 = default
    int alignment = 0;          // 0=left, 1=center, 2=right, 3=justify
};

struct Document
{
    QList<Paragraph> paragraphs;
    QList<Image> images;
};

/** Write a binary Word 97 (MS-DOC) document to @p device. */
bool writeDocument(QIODevice *device, const Document &doc);

} // namespace Word97

#endif
