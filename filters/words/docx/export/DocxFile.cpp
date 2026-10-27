/* This file is part of the KDE project

   SPDX-FileCopyrightText: 2012-2014 Inge Wallin <inge@lysator.liu.se>
   SPDX-FileCopyrightText: 2012 Mojtaba Shahi Senobari <mojtaba.shahi3000@gmail.com>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

// Own
#include "DocxFile.h"

// Qt
#include <QByteArray>
#include <QList>
#include <QString>

// Calligra
#include <KoStore.h>

// This filter
#include "DocxExportDebug.h"
#include "OdfReaderDocxContext.h"
#include "OpcContentTypes.h"
#include "OpcRelSet.h"
#include "OpcRelSetManager.h"

// ================================================================
//                         class DocxFile

DocxFile::DocxFile() = default;

DocxFile::~DocxFile() = default;

QString DocxFile::addRelationship(const QString &sourcePart, const QString &type, const QString &target, bool external)
{
    OpcRelSet *relationshipSet = nullptr;
    if (sourcePart.isEmpty()) {
        relationshipSet = m_relationships.documentRelSet();
        if (!relationshipSet) {
            relationshipSet = new OpcRelSet;
            m_relationships.setDocumentRelSet(relationshipSet);
        }
    } else {
        relationshipSet = m_relationships.relSet(sourcePart);
        if (!relationshipSet) {
            relationshipSet = new OpcRelSet;
            m_relationships.setRelSet(sourcePart, relationshipSet);
        }
    }
    return relationshipSet->addRelationship(type, target, external);
}

KoFilter::ConversionStatus
DocxFile::writeDocx(const QString &fileName, const QByteArray &appIdentification, const OdfReaderDocxContext &context, bool commentsExist)
{
    Q_UNUSED(context);

    KoStore *docxStore = KoStore::createStore(fileName, KoStore::Write, appIdentification, KoStore::Auto, false);
    if (!docxStore || docxStore->bad()) {
        warnDocx << "Unable to create output file!";
        delete docxStore;
        return KoFilter::FileNotFound;
    }
    addRelationship(QString(),
                    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument",
                    "word/document.xml");
    addRelationship("word/document.xml", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles", "styles.xml");
    if (commentsExist) {
        addRelationship("word/document.xml", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments", "comments.xml");
    }

    // Write contents of added files.
    KoFilter::ConversionStatus status = FileCollector::writeFiles(docxStore);
    if (status != KoFilter::OK) {
        delete docxStore;
        return status;
    }

    if (!m_relationships.saveRelSets(docxStore)) {
        delete docxStore;
        return KoFilter::CreationError;
    }

    // Finally, write the [Content_Types}.xml file.
    OpcContentTypes contentTypes;
    contentTypes.addDefault("rels", "application/vnd.openxmlformats-package.relationships+xml");
    contentTypes.addDefault("xml", "application/xml");
    foreach (const FileInfo *file, files()) {
        contentTypes.addFile(file->fileName, file->mimetype);
    }
    status = contentTypes.writeToStore(docxStore);

    delete docxStore;
    return status;
}
