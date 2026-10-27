/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2012-2014 Inge Wallin <inge@lysator.liu.se>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef DOCXFILE_H
#define DOCXFILE_H

// Qt
#include <QString>

// Calligra
#include <KoFilter.h>

// This filter
#include "FileCollector.h"
#include "OpcRelSetManager.h"

class QString;
class QByteArray;

class KoStore;

class OdtTraverserDocxContext;
class OdfReaderDocxContext;

class DocxFile : public FileCollector
{
public:
    DocxFile();
    ~DocxFile() override;

    /** When you have created all the content and added it using
     * addContentFile(), call this function once and it will write the
     * docx to the disk.
     */
    KoFilter::ConversionStatus
    writeDocx(const QString &fileName, const QByteArray &appIdentification, const OdtTraverserDocxContext &context, bool commentsExist);

    /** When you have created all the content and added it using
     * addContentFile(), call this function once and it will write the
     * docx to the disk. This function is overloaded.
     */
    KoFilter::ConversionStatus writeDocx(const QString &fileName, const QByteArray &appIdentification, const OdfReaderDocxContext &context, bool commentsExist);

    QString addRelationship(const QString &sourcePart, const QString &type, const QString &target, bool external = false);

private:
    OpcRelSetManager m_relationships;

};

#endif // DOCXFILE_H
